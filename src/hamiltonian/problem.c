/*
 * 002-hamiltonian Phase 2 implementation, complete (tasks T-003-T-006):
 * N-gate, pointer/overlap/`dim` validation, the `E(k)` energy core with the
 * pointwise apply, and the finiteness scan with the norm gate.
 *
 * Purpose: validate the board edge `n` and derive `numCells = n * n` and
 * `expectedDim = 2^numCells` before any pointer, `dim`, or float work runs
 * (spec §1, FR-002, FR-003), enforce the two-pass shape contract (spec §1,
 * FR-006), then require every amplitude finite and the state normalized
 * within `1e-12` (spec §1, FR-007, FR-008) before applying the diagonal
 * operator matrix-free as `outPsi[k] = E(k) * phi[k]` (spec §1, FR-001,
 * FR-004). The pipeline is validate-everything-then-compute: all checks
 * complete before the first write.
 *
 * Ownership: no allocation; every buffer stays caller-owned. Errors: every
 * validation failure returns `QaStatus` before the first write, so existing
 * non-NULL buffers are unchanged. Numerical assumptions: `unsigned int`
 * wraps modulo `2^N`, which the overflow probe below relies on; accepted
 * `n` keeps every derived quantity exact; `E(k)` is a small exact integer
 * scaled into `complex double` without rounding; the norm sums finite
 * squares, so an infinite sum means a rejected input, never an unchecked
 * overflow.
 */

#include "qa/hamiltonian/problem.h"

#include "qa/core/grid.h"

#include <complex.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* Largest queen roster on any accepted board: `numCells <= 16` by the
 * N-gate, so every board's queens fit this fixed roster. */
#define QA_HAMILTONIAN_MAX_QUEENS 16u

/* Norm-gate tolerance (spec §1, FR-008): `|norm(phi) - 1| <= 1e-12`. */
#define QA_HAMILTONIAN_NORM_TOL 1e-12

/**
 * @brief Validate the board edge and derive `numCells` and `expectedDim`
 * (T-003).
 *
 * Fixed order, completed before any `numCells`/`dim` arithmetic (FR-002
 * before FR-003, spec §1):
 *   1. `outNumCells == NULL` or `outExpectedDim == NULL` -> `QA_ERR_RANGE`
 *      (codestyle §7: receivers are caller storage proven first).
 *   2. `n == 5` -> `QA_ERR_UNSUPPORTED` (FR-002, EC-002, EC-013). Checked
 *      before every other gate, so the reserved `N` wins over NULL, `dim`,
 *      and finiteness faults.
 *   3. `n < 2` -> `QA_ERR_RANGE` (FR-003, EC-001).
 *   4. `n > 5` -> `QA_ERR_RANGE`, unless `n * n` would overflow `unsigned
 *      int` (detected via `n > UINT_MAX / n`, which cannot itself overflow
 *      or trap) -> `QA_ERR_OVERFLOW` (FR-003, EC-001). Boundary: `65535`
 *      fits (`RANGE`), `65536` and `UINT_MAX` overflow (`OVERFLOW`).
 * On success (`n` in [2, 4]) `*outNumCells` is `n * n` (4, 9, or 16), a
 * multiply that cannot overflow, and `*outExpectedDim` is
 * `((size_t)1 << numCells)` (16, 512, or 65536); the shift width is guarded
 * first, so a narrow `size_t` would yield `QA_ERR_OVERFLOW` instead of an
 * out-of-range shift.
 *
 * @param[in]  n              Board edge: 2..4 accepted, 5 reserved, else invalid.
 * @param[out] outNumCells    Non-NULL caller-owned receiver of `n * n`.
 * @param[out] outExpectedDim Non-NULL caller-owned receiver of `2^numCells`.
 *
 * @return `QA_OK` (both receivers written); `QA_ERR_UNSUPPORTED` (`n == 5`);
 *         `QA_ERR_RANGE` (NULL receiver, `n < 2`, or `n > 5` with a fitting
 *         square); `QA_ERR_OVERFLOW` (`n > 5` with an overflowing square, or
 *         a `size_t` too narrow for the shift).
 *
 * @owner No allocation. Both receivers stay owned by the caller on every
 *        path; they are poisoned to zero on entry per codestyle §8.
 * @assumes `unsigned int` wraps modulo `2^N` (standard unsigned arithmetic),
 *          so the division probe detects overflow without overflowing;
 *          accepted `n` keeps every derived quantity exact.
 */
static QaStatus qaHamiltonianResolveN(unsigned int n, unsigned int *outNumCells,
                                      size_t *outExpectedDim)
{
    if (outNumCells == NULL || outExpectedDim == NULL) {
        return QA_ERR_RANGE;
    }
    *outNumCells = 0;
    *outExpectedDim = 0;

    if (n == 5u) {
        return QA_ERR_UNSUPPORTED;
    }
    if (n < 2u) {
        return QA_ERR_RANGE;
    }
    if (n > 5u) {
        if (n > UINT_MAX / n) {
            return QA_ERR_OVERFLOW;
        }
        return QA_ERR_RANGE;
    }

    unsigned int numCells = n * n;

    if (numCells >= sizeof (size_t) * (size_t)CHAR_BIT) {
        return QA_ERR_OVERFLOW;
    }

    *outNumCells = numCells;
    *outExpectedDim = (size_t)1 << numCells;
    return QA_OK;
}

/**
 * @brief Enforce the pointer/overlap/`dim` shape contract, two-pass (T-004).
 *
 * Fixed order, completed before any float work and before the first write
 * (FR-006, spec §1):
 *   1. `phi == NULL` or `outPsi == NULL` -> `QA_ERR_RANGE` (EC-004). No
 *      arithmetic runs here, so NULL never dereferences.
 *   2. `dim != expectedDim` -> `QA_ERR_RANGE` (EC-003, EC-014). A plain
 *      comparison, so even `SIZE_MAX` is safe; it also bounds every later
 *      range computation (`dim` in {16, 512, 65536} from here on).
 *   3. The `uintptr_t` byte ranges `[phi, phi + dim)` and
 *      `[outPsi, outPsi + dim)` intersect (`phi == outPsi` subsumed) ->
 *      `QA_ERR_RANGE` (EC-005, EC-015). `dim` is bounded by step 2, so
 *      `dim * sizeof *phi` is guarded explicitly and both range-end
 *      additions are guarded against `uintptr_t` wrap; exact adjacency
 *      (`outPsi == phi + dim`) is not an intersection and passes.
 * A combined overlap-plus-`dim` fault still yields `QA_ERR_RANGE` (EC-015),
 * so comparing `dim` before building ranges is unobservable and keeps the
 * range arithmetic provably overflow-free.
 *
 * @param[in] expectedDim Gated `2^numCells` from the `N`-gate (16, 512, 65536).
 * @param[in] phi         Immutable input state; NULL rejected, never read here.
 * @param[in] outPsi      Caller-owned receiver; NULL or overlapping rejected.
 * @param[in] dim         Claimed state length; must equal `expectedDim`.
 *
 * @return `QA_OK` (shape holds; caller may proceed); `QA_ERR_RANGE` (NULL,
 *         `dim` mismatch, intersecting ranges, or bounds-unsafe range math).
 *         On failure both buffers are untouched (nothing is written here).
 *
 * @owner No allocation. Both buffers stay owned by the caller on every path.
 * @assumes `expectedDim` is an exact gated value, so `dim == expectedDim`
 *          bounds the range math; `uintptr_t` round-trips object pointers.
 */
static QaStatus qaHamiltonianCheckShape(size_t expectedDim, const complex double *phi,
                                       complex double *outPsi, size_t dim)
{
    if (phi == NULL || outPsi == NULL) {
        return QA_ERR_RANGE;
    }
    if (dim != expectedDim) {
        return QA_ERR_RANGE;
    }
    if (dim > SIZE_MAX / sizeof *phi) {
        return QA_ERR_RANGE;
    }
    size_t bytes = dim * sizeof *phi;

    uintptr_t phiBegin = (uintptr_t)phi;
    uintptr_t outBegin = (uintptr_t)outPsi;
    if (bytes > UINTPTR_MAX - phiBegin || bytes > UINTPTR_MAX - outBegin) {
        return QA_ERR_RANGE;
    }
    uintptr_t phiEnd = phiBegin + bytes;
    uintptr_t outEnd = outBegin + bytes;

    if (phiBegin < outEnd && outBegin < phiEnd) {
        return QA_ERR_RANGE;
    }
    return QA_OK;
}

/**
 * @brief Count the unordered attacking queen pairs of board id `k` (T-005).
 *
 * Uniform rule for every `N` (FR-001): `+1` per queen pair sharing a row, a
 * column, or a diagonal (`|i1 - i2| == |j1 - j2|`), so `E(k) = 0` iff no
 * pair attacks. Queen cells are read through the 001-states
 * `qaGridGetBit` helper (MSB-first row-major), the single source of the bit
 * mapping; this module duplicates no indexing logic (plan decision). At most
 * `numCells <= 16` queens exist, so the fixed roster below always fits.
 *
 * @param[in]  n          Board edge, gated to 2..4 by the caller.
 * @param[in]  k          Board id, `< 2^(n*n)` by the caller (canonical).
 * @param[out] outEnergy  Non-NULL caller-owned receiver of the pair count.
 *
 * @return `QA_OK` (receiver written); `QA_ERR_RANGE` (NULL receiver, or a
 *         cell read the caller already excluded by gating `n` and `k`).
 *
 * @owner No allocation. The receiver stays owned by the caller on every
 *        path; it is poisoned to zero on entry per codestyle §8.
 * @assumes Gated `n` and canonical `k`, so every `qaGridGetBit` call below
 *          succeeds and the error return is unreachable by construction;
 *          the pair count (`<= C(16,2)`) fits `unsigned int` exactly.
 */
static QaStatus qaHamiltonianPairEnergy(unsigned int n, QaGridId k,
                                       unsigned int *outEnergy)
{
    unsigned int rows[QA_HAMILTONIAN_MAX_QUEENS];
    unsigned int cols[QA_HAMILTONIAN_MAX_QUEENS];
    unsigned int queens = 0;

    if (outEnergy == NULL) {
        return QA_ERR_RANGE;
    }
    *outEnergy = 0;

    for (unsigned int i = 0; i < n; ++i) {
        for (unsigned int j = 0; j < n; ++j) {
            unsigned int bit = 0;
            QaStatus get = qaGridGetBit(k, n, i, j, &bit);
            if (get != QA_OK) {
                return get;
            }
            if (bit == 1u) {
                rows[queens] = i;
                cols[queens] = j;
                ++queens;
            }
        }
    }

    unsigned int energy = 0;
    for (unsigned int a = 0; a < queens; ++a) {
        for (unsigned int b = a + 1u; b < queens; ++b) {
            unsigned int rowGap = (rows[a] > rows[b]) ? rows[a] - rows[b] : rows[b] - rows[a];
            unsigned int colGap = (cols[a] > cols[b]) ? cols[a] - cols[b] : cols[b] - cols[a];
            if (rows[a] == rows[b] || cols[a] == cols[b] || rowGap == colGap) {
                ++energy;
            }
        }
    }

    *outEnergy = energy;
    return QA_OK;
}

/**
 * @brief Scan every amplitude for finiteness, before the norm gate (T-006).
 *
 * Rejects any `phi[k]` with a non-finite real or imaginary part with
 * `QA_ERR_DOMAIN` (FR-007, EC-006): infinities and NaNs in either part,
 * including a single poisoned index. The scan always runs to completion
 * over all `dim` cells before the norm gate below and before any write, so
 * a poison at the last index is caught exactly like one at the first.
 *
 * @param[in] dim State length, gated to `2^(n*n)` by the caller.
 * @param[in] phi Immutable input state; NULL rejected by the shape check.
 *
 * @return `QA_OK` (every amplitude finite); `QA_ERR_DOMAIN` (any amplitude
 *         non-finite). On failure both buffers are untouched (nothing is
 *         read from `outPsi` and nothing is written here).
 *
 * @owner No allocation. Both buffers stay owned by the caller on every path.
 * @assumes Gated `dim` bounds the scan; `isfinite` classifies every
 *          `double` part including NaN payloads and signed infinities.
 */
static QaStatus qaHamiltonianCheckFinite(size_t dim, const complex double *phi)
{
    for (size_t k = 0; k < dim; ++k) {
        if (!isfinite(creal(phi[k])) || !isfinite(cimag(phi[k]))) {
            return QA_ERR_DOMAIN;
        }
    }
    return QA_OK;
}

/**
 * @brief Gate the input norm within `1e-12` of one, before any write (T-006).
 *
 * Computes `norm = sqrt(sum_k |phi[k]|^2)` over the finite amplitudes the
 * scan above accepted and requires `|norm - 1| <= 1e-12` (FR-008): the zero
 * vector and any unnormalized finite scaling (e.g. `2.5 * |k>`) report
 * `QA_ERR_DOMAIN` (EC-009). A non-finite or overflowing sum yields
 * `QA_ERR_DOMAIN` as well; floating-point overflow is defined to infinity,
 * so the check classifies the result instead of avoiding the arithmetic.
 *
 * @param[in] dim State length, gated to `2^(n*n)` by the caller.
 * @param[in] phi Immutable input state, every amplitude finite per the scan.
 *
 * @return `QA_OK` (`|norm - 1| <= 1e-12`); `QA_ERR_DOMAIN` (non-finite norm
 *         or outside tolerance). On failure both buffers are untouched.
 *
 * @owner No allocation. Both buffers stay owned by the caller on every path.
 * @assumes Finite inputs (scan first), so the sum classifies the norm
 *          without NaN surprises; accepted states keep the sum near one,
 *          far from overflow.
 */
static QaStatus qaHamiltonianCheckNorm(size_t dim, const complex double *phi)
{
    double sum = 0.0;
    for (size_t k = 0; k < dim; ++k) {
        double re = creal(phi[k]);
        double im = cimag(phi[k]);
        sum += re * re + im * im;
    }

    double norm = sqrt(sum);
    if (!isfinite(norm) || fabs(norm - 1.0) > QA_HAMILTONIAN_NORM_TOL) {
        return QA_ERR_DOMAIN;
    }
    return QA_OK;
}

/*
 * Stage 1 (T-003) is the `N`-gate above, stage 2 (T-004) the shape check,
 * stage 3 (T-005) the energy core with the pointwise apply below, and stage
 * 4 (T-006) the finiteness scan with the norm gate: the full
 * validate-everything-then-compute pipeline of spec §1. The scan runs after
 * shape and before the norm gate; the gate runs before the first write; the
 * loop recomputes `E(k)` per board id and scales `phi[k]` into `outPsi[k]`
 * without ever materializing a matrix. Energy errors are unreachable by
 * construction (gated `n`, canonical `k < dim`), which is what keeps the
 * no-partial-write contract intact across the loop.
 */
QaStatus qaHamiltonianApplyProblem(unsigned int n, const complex double *phi, complex double *outPsi, size_t dim)
{
    unsigned int numCells = 0;
    size_t expectedDim = 0;

    QaStatus status = qaHamiltonianResolveN(n, &numCells, &expectedDim);
    if (status != QA_OK) {
        return status;
    }

    status = qaHamiltonianCheckShape(expectedDim, phi, outPsi, dim);
    if (status != QA_OK) {
        return status;
    }

    status = qaHamiltonianCheckFinite(dim, phi);
    if (status != QA_OK) {
        return status;
    }

    status = qaHamiltonianCheckNorm(dim, phi);
    if (status != QA_OK) {
        return status;
    }

    (void)numCells;

    for (size_t k = 0; k < dim; ++k) {
        unsigned int energy = 0;
        status = qaHamiltonianPairEnergy(n, (QaGridId)k, &energy);
        if (status != QA_OK) {
            return status;
        }
        outPsi[k] = (double)energy * phi[k];
    }
    return QA_OK;
}
