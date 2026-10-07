/*
 * 004-driver Phase 2 implementation (tasks T-004..T-007): N-gate, dim check,
 * pointer, overlap, finiteness and norm checks, the matrix-free apply loop and
 * the annealing initial state (FR-001, FR-002, FR-003, FR-004, FR-006, FR-010).
 *
 * Purpose: validate the board side `n` and the buffer length `dim` before any
 * shift, multiply or write, and fill `outPsi` with
 * `psi0[k] = (-1)^popcount(k) * s`, `s = 1.0 / sqrt((double) dim)`.
 * `qaHamiltonianApplyDriver` validates fully (T-005, T-006) and then
 * applies `H_driver` matrix-free (T-007).
 *
 * Ownership: no allocation, no globals, no state across calls; `outPsi` is
 * caller-owned storage and is written only on `QA_OK`.
 * Errors: `QA_ERR_UNSUPPORTED` for `n == 5`, `QA_ERR_RANGE` for the other
 * invalid `n`, a wrong `dim` or a NULL pointer; failures write nothing.
 * Numerical assumptions: accepted `n` is 2, 3 or 4, so `numCells` is at most
 * 16 and `dim <= 65536` fits every shift below; `s` is exact in `double` for
 * even `numCells` and correctly rounded otherwise.
 */

#include "qa/hamiltonian/driver.h"

#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Validate the board side and derive `numCells = n * n` (FR-002,
 * FR-003).
 *
 * Fixed order: `n == 5` -> `QA_ERR_UNSUPPORTED`, then `n < 2` or `n > 5` ->
 * `QA_ERR_RANGE`; it runs before any pointer or `dim` is looked at (EC-002).
 *
 * @param[in]  n           Board side: 2..4 accepted, 5 reserved, else invalid.
 * @param[out] outNumCells Non-NULL caller-owned receiver of `n * n`; written
 *                         only on `QA_OK`.
 *
 * @return `QA_OK`; `QA_ERR_UNSUPPORTED` (`n == 5`); `QA_ERR_RANGE`
 *         (`n < 2` or `n > 5`). On failure the receiver is untouched.
 *
 * @owner No allocation; the receiver stays owned by the caller.
 * @assumes The multiply runs only for `n` in [2, 4], so it cannot overflow.
 */
static QaStatus qaDriverResolveN(unsigned int n, unsigned int *outNumCells)
{
    if (n == 5u) {
        return QA_ERR_UNSUPPORTED;
    }
    if (n < 2u || n > 5u) {
        return QA_ERR_RANGE;
    }

    /* Checked arithmetic: `n` is 2, 3 or 4 here, so `n * n` is 4, 9 or 16. */
    *outNumCells = n * n;
    return QA_OK;
}

/**
 * @brief Check `dim == 2^numCells` (FR-006), the first precondition after
 * the N-gate.
 *
 * @param[in] numCells Already validated cell count, 4, 9 or 16.
 * @param[in] dim      Caller-supplied buffer length.
 *
 * @return `QA_OK` when `dim == 2^numCells`; `QA_ERR_RANGE` otherwise
 *         (including 0 and `SIZE_MAX`).
 *
 * @owner No allocation, no pointers.
 * @assumes `numCells <= 16`, so the shift is far below the width of `size_t`
 *          and `2^numCells <= 65536`.
 */
static QaStatus qaDriverCheckDim(unsigned int numCells, size_t dim)
{
    /* Checked arithmetic: `numCells <= 16 < width of size_t`, no overflow. */
    if (dim != ((size_t)1u << numCells)) {
        return QA_ERR_RANGE;
    }
    return QA_OK;
}

/**
 * @brief Parity of the population count of `k` as a sign (FR-010).
 *
 * @param[in] k Basis index, below 65536 for accepted `n`.
 *
 * @return `+1.0` when `popcount(k)` is even, `-1.0` when odd.
 *
 * @owner No allocation, no pointers.
 * @assumes Pure bit folding on `unsigned int`; the fold is exact for any
 *          32-bit value and independent of the platform popcount builtin.
 */
static double qaDriverParitySign(unsigned int k)
{
    /* Shift bound: the folds 16, 8, 4, 2, 1 are below the 32-bit width of
     * `unsigned int`, so each shift is defined and the result is the XOR of
     * all 32 bits of `k` in bit 0. */
    k ^= k >> 16;
    k ^= k >> 8;
    k ^= k >> 4;
    k ^= k >> 2;
    k ^= k >> 1;
    return (k & 1u) != 0u ? -1.0 : 1.0;
}

/**
 * @brief Check the apply pointers and the `uintptr_t` byte ranges (FR-006).
 *
 * Fixed order: `phi == NULL` or `outPsi == NULL` -> `QA_ERR_RANGE`, then
 * overlap of `[phi, phi + dim)` and `[outPsi, outPsi + dim)` as byte ranges
 * -> `QA_ERR_RANGE` (EC-004, EC-005). Pointers are only converted to
 * `uintptr_t`, never dereferenced or compared as unrelated pointers.
 *
 * @param[in] phi    Candidate input buffer, may be NULL.
 * @param[in] outPsi Candidate output buffer, may be NULL.
 * @param[in] dim    Already validated length, 16, 512 or 65536.
 *
 * @return `QA_OK` when both are non-NULL and disjoint; `QA_ERR_RANGE`
 *         otherwise.
 *
 * @owner No allocation; both buffers stay owned by the caller.
 * @assumes `dim` passed `qaDriverCheckDim`, so the byte length is at most
 *          `65536 * sizeof (complex double)` and fits `uintptr_t`.
 */
static QaStatus qaDriverCheckBuffers(const complex double *phi,
                                     const complex double *outPsi, size_t dim)
{
    if (phi == NULL || outPsi == NULL) {
        return QA_ERR_RANGE;
    }

    /* Checked arithmetic: `dim <= 65536`, so `bytes <= 1 MiB` cannot overflow
     * `uintptr_t`; the wrap guards below reject a range that would. */
    uintptr_t bytes = (uintptr_t)(dim * sizeof *phi);
    uintptr_t a = (uintptr_t)phi;
    uintptr_t b = (uintptr_t)outPsi;

    if (bytes > UINTPTR_MAX - a || bytes > UINTPTR_MAX - b) {
        return QA_ERR_RANGE;
    }
    /* Half-open ranges [a, a + bytes) and [b, b + bytes) intersect iff each
     * starts before the other ends; equal pointers are covered. */
    if (a < b + bytes && b < a + bytes) {
        return QA_ERR_RANGE;
    }
    return QA_OK;
}

/**
 * @brief Finiteness scan, then norm gate on `phi` (FR-007, FR-008).
 *
 * Step 6: the full scan of `phi[0..dim-1]` runs to completion; any
 * non-finite real or imaginary part -> `QA_ERR_DOMAIN`. Step 7: `||phi||` is
 * `sqrt(sum_k (re^2 + im^2))`, accumulated sequentially in `double` in
 * ascending `k`; a non-finite norm (overflowing squares) or
 * `| ||phi|| - 1 | > 1e-12` -> `QA_ERR_DOMAIN`. Reads only.
 *
 * @param[in] phi Non-NULL caller-owned input of `dim` amplitudes.
 * @param[in] dim Already validated length, 16, 512 or 65536.
 *
 * @return `QA_OK` when finite and normalized within `1e-12`;
 *         `QA_ERR_DOMAIN` otherwise.
 *
 * @owner No allocation; `phi` is never modified.
 * @assumes Sequential `double` accumulation of at most 65536 terms of a unit
 *          vector keeps the rounding error of the norm well below `1e-12`
 *          (checked by EC-026); no OpenMP, so the sum order is fixed.
 */
static QaStatus qaDriverCheckDomain(const complex double *phi, size_t dim)
{
    double sum = 0.0;

    /* Read bound: `k < dim`, the validated length of `phi`. */
    for (size_t k = 0u; k < dim; ++k) {
        if (!isfinite(creal(phi[k])) || !isfinite(cimag(phi[k]))) {
            return QA_ERR_DOMAIN;
        }
    }
    for (size_t k = 0u; k < dim; ++k) {
        double re = creal(phi[k]);
        double im = cimag(phi[k]);
        sum += re * re + im * im;
    }
    double norm = sqrt(sum);

    /* Rejects an infinite norm (overflowing squares) before the compare;
     * NaN cannot occur after the scan but would also fail `isfinite`. */
    if (!isfinite(norm) || fabs(norm - 1.0) > 1e-12) {
        return QA_ERR_DOMAIN;
    }
    return QA_OK;
}

/*
 * `qaHamiltonianApplyDriver` (tasks T-005..T-007, FR-001..FR-008).
 *
 * Implements the contract in `include/qa/hamiltonian/driver.h`: steps 1..7
 * (N-gate, `dim`, NULL, overlap, finiteness scan, norm gate) complete before
 * the first write, then `outPsi[k] = sum_c phi[k XOR m_c]` is computed with
 * `m_c = 1 << (numCells - 1 - c)` in ascending `c`. The header carries the
 * normative documentation; what follows states only the implementation-side
 * arithmetic facts.
 */
QaStatus qaHamiltonianApplyDriver(unsigned int n, const complex double *phi,
                                  complex double *outPsi, size_t dim)
{
    unsigned int numCells = 0u;
    QaStatus status;

    status = qaDriverResolveN(n, &numCells);
    if (status != QA_OK) {
        return status;
    }
    status = qaDriverCheckDim(numCells, dim);
    if (status != QA_OK) {
        return status;
    }
    status = qaDriverCheckBuffers(phi, outPsi, dim);
    if (status != QA_OK) {
        return status;
    }
    status = qaDriverCheckDomain(phi, dim);
    if (status != QA_OK) {
        return status;
    }

    /* Write bound: `k < dim`, the validated length of `outPsi`. `c` is below
     * `numCells <= 16`, so the shift `numCells - 1 - c` is in [0, 15] and
     * `m_c < dim`, hence `k ^ m_c < dim` stays inside `phi`. `outPsi` is
     * disjoint from `phi` (step 5), so reads never see partial output.
     * Real and imaginary parts are summed separately in ascending `c`. */
    for (size_t k = 0u; k < dim; ++k) {
        double re = 0.0;
        double im = 0.0;
        for (unsigned int c = 0u; c < numCells; ++c) {
            size_t mask = (size_t)1u << (numCells - 1u - c);
            re += creal(phi[k ^ mask]);
            im += cimag(phi[k ^ mask]);
        }
        outPsi[k] = CMPLX(re, im);
    }
    return QA_OK;
}

/*
 * `qaHamiltonianInitialState` (task T-004, FR-002, FR-003, FR-006, FR-010).
 *
 * Implements the contract declared in `include/qa/hamiltonian/driver.h`:
 * N-gate, `dim`, then the NULL pointer, all completed before the first
 * write. The header carries the normative documentation; what follows states
 * only the implementation-side arithmetic facts.
 */
QaStatus qaHamiltonianInitialState(unsigned int n, complex double *outPsi,
                                   size_t dim)
{
    unsigned int numCells = 0u;
    QaStatus status;

    status = qaDriverResolveN(n, &numCells);
    if (status != QA_OK) {
        return status;
    }
    status = qaDriverCheckDim(numCells, dim);
    if (status != QA_OK) {
        return status;
    }
    if (outPsi == NULL) {
        return QA_ERR_RANGE;
    }

    /* Numerical bound: `dim` is 16, 512 or 65536 here, so the sqrt is of a
     * small exact integer and `s` is finite, positive and at most 0.25. */
    double s = 1.0 / sqrt((double)dim);

    /* Cast bound: `k < dim <= 65536`, so `(unsigned int)k` is lossless.
     * Write bound: `k < dim`, the validated length of `outPsi`; the
     * imaginary part is the literal `+0.0` (EC-015). */
    for (size_t k = 0u; k < dim; ++k) {
        outPsi[k] = CMPLX(qaDriverParitySign((unsigned int)k) * s, 0.0);
    }
    return QA_OK;
}
