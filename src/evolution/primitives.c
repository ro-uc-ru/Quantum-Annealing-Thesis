/*
 * 005-evolution split-operator primitives `Dz` and `Rx` (spec 005-evolution,
 * tasks T-007 for `Dz` and T-008 for `Rx`).
 *
 * Purpose: `qaEvolutionDz` applies the pointwise phase
 * `exp(-i s a H_target(k))` to every amplitude of a caller-owned state,
 * matrix-free (FR-004, ARC-1): `H_target(k)` is recomputed from `k` through
 * `qaHamiltonianTarget` on every use and no table or matrix is built. The
 * validation order is the fixed one of FR-027, completed before the first
 * write, so the state is unchanged on every error (EC-021).
 *
 * Ownership: no allocation, no globals, no state across calls. The state
 * `psi` stays owned by the caller on every path and is only rewritten in place
 * by the final pass after every check has passed.
 * Errors: `QA_ERR_UNSUPPORTED` for `n == 5`, `QA_ERR_RANGE` for `n` outside
 * `[2, 5]` or `dim != 2^(n*n)`, `QA_ERR_DOMAIN` for a NULL state, a non-finite
 * `s` or `a`, or a phase argument that overflows. Every status returned by
 * `qaHamiltonianTarget` is checked (ERR-1) and propagated.
 * Numerical assumptions: accepted `n` is 2, 3 or 4, so `numCells <= 16`,
 * `dim <= 65536` and `|H_target(k)| <= 120`, exact in `double`. The phase is
 * the double product `(s * a) * H_target(k)`; once `s * a * max|H_target|` is
 * finite, every per-amplitude phase is finite, and `cos`/`sin` of a finite
 * argument give a factor of modulus 1 up to rounding, so the norm is preserved
 * up to rounding.
 *
 * `qaEvolutionRx` applies `prod_c (cos(theta) I - i sin(theta) sigma^x_c)`,
 * `theta = dt b`, as one in-place butterfly pass per cell in ascending `c`
 * (FR-003): the cell blocks commute, so the product is exact with no splitting
 * error. Its validation order puts the finiteness and `dt * b` overflow check
 * (step 3) before the `dim` check (step 4), as the header documents. A large
 * finite `theta` is accepted (EC-014): `cos` and `sin` of a finite double are
 * defined, so each 2x2 block stays unitary up to rounding. Each pass touches
 * the pairs `(i, i + 2^c)` with `i` having bit `c` clear, all below `dim`.
 */

#include "qa/evolution/primitives.h"

#include <math.h>
#include <stddef.h>

#include "qa/hamiltonian/target.h"

/**
 * @brief Apply the `N`-gate and derive the expected `dim = 2^(n*n)`
 * (FR-016, FR-027 step 1).
 *
 * Fixed order: `n == 5` -> `QA_ERR_UNSUPPORTED`, then `n < 2` or `n > 5` ->
 * `QA_ERR_RANGE`. It runs before any size arithmetic and before any pointer
 * or scalar is looked at.
 *
 * @param[in]  n      Board edge: `2..4` accepted, `5` reserved, else out of
 *                    range.
 * @param[out] outDim Non-NULL caller-owned receiver of `2^(n*n)`; written only
 *                    on `QA_OK`.
 *
 * @return `QA_OK`; `QA_ERR_UNSUPPORTED` (`n == 5`); `QA_ERR_RANGE` (`n < 2`
 *         or `n > 5`). On failure the receiver is untouched.
 *
 * @owner No allocation; the receiver stays owned by the caller.
 * @assumes The arithmetic runs only for `n` in `[2, 4]`, so `n * n <= 16` and
 *          the shift is below the width of `size_t`.
 */
static QaStatus qaPrimitivesResolveDim(unsigned int n, size_t *outDim)
{
    if (n == 5u) {
        return QA_ERR_UNSUPPORTED;
    }
    if (n < 2u || n > 5u) {
        return QA_ERR_RANGE;
    }

    /* Bound: `n` is 2, 3 or 4 here, so `n * n` is 4, 9 or 16 and the shift
     * gives 16, 512 or 65536, with no overflow of `size_t`. */
    *outDim = (size_t)1u << (n * n);
    return QA_OK;
}

/**
 * @brief Compute `max_k |H_target(k)|` over the `dim` boards of an `n x n`
 * board, the first (read-only) pass of `Dz` (EC-021).
 *
 * Pure read: it never touches the state. `H_target(k)` is recomputed pointwise
 * (matrix-free, ARC-1).
 *
 * @param[in]  n      Gated board edge, 2, 3 or 4.
 * @param[in]  dim    Gated `2^(n*n)`.
 * @param[out] outMax Non-NULL caller-owned receiver of the maximum absolute
 *                    value; written only on `QA_OK`.
 *
 * @return `QA_OK`, or the first error status of `qaHamiltonianTarget`
 *         (ERR-1); on failure the receiver is untouched.
 *
 * @owner No allocation; the receiver stays owned by the caller.
 * @assumes Every `|H_target(k)| <= 120` and is exact, so the maximum is exact
 *          and the comparison is exact.
 */
static QaStatus qaPrimitivesMaxTarget(unsigned int n, size_t dim,
                                      double *outMax)
{
    double maxAbs = 0.0;

    /* Index bound: `k < dim`, so `qaHamiltonianTarget` never reports
     * `k >= dim`; any other status is still propagated. */
    for (size_t k = 0; k < dim; ++k) {
        double h = 0.0;
        QaStatus status = qaHamiltonianTarget(n, k, &h);
        if (status != QA_OK) {
            return status;
        }
        double mag = fabs(h);
        if (mag > maxAbs) {
            maxAbs = mag;
        }
    }

    *outMax = maxAbs;
    return QA_OK;
}

/*
 * `qaEvolutionDz` (task T-007, FR-004, FR-022, FR-027, EC-021).
 *
 * Implements the contract declared in `include/qa/evolution/primitives.h`:
 * the `N`-gate, NULL `psi`, non-finite `s`/`a`, `dim` check, then the phase
 * overflow pre-check (EC-021); only then are the phases written in place.
 */
QaStatus qaEvolutionDz(unsigned int n, double s, double a,
                       complex double *psi, size_t dim)
{
    size_t expectedDim = 0;

    /* Step 1 (FR-016): the gate precedes every size computation. */
    QaStatus status = qaPrimitivesResolveDim(n, &expectedDim);
    if (status != QA_OK) {
        return status;
    }

    /* Step 2 (FR-027): a NULL state is a domain error. */
    if (psi == NULL) {
        return QA_ERR_DOMAIN;
    }

    /* Step 3 (FR-027): non-finite scalars are rejected before any use. */
    if (!isfinite(s) || !isfinite(a)) {
        return QA_ERR_DOMAIN;
    }

    /* Step 4 (FR-027): the buffer length must be exactly `2^(n*n)`, which
     * bounds every write of the final pass by the caller's buffer size. */
    if (dim != expectedDim) {
        return QA_ERR_RANGE;
    }

    /* Step 5 (EC-021): `s * a * max|H_target|` must be finite. If `s * a`
     * already overflows, the product is infinite (or NaN when the maximum is
     * 0), which `isfinite` rejects; the state has not been written yet. */
    double maxAbs = 0.0;
    status = qaPrimitivesMaxTarget(n, dim, &maxAbs);
    if (status != QA_OK) {
        return status;
    }
    double sa = s * a;
    if (!isfinite(sa * maxAbs)) {
        return QA_ERR_DOMAIN;
    }

    /* Step 6: pass 2 writes. Bound: `k < dim == expectedDim`, the caller's
     * buffer length. The phase `sa * h` is finite because `|h| <= maxAbs`
     * and `sa * maxAbs` is finite (step 5). */
    for (size_t k = 0; k < dim; ++k) {
        double h = 0.0;
        status = qaHamiltonianTarget(n, k, &h);
        if (status != QA_OK) {
            /* Unreachable after step 5 (same inputs, same pure function);
             * kept so no status is ever ignored (ERR-1). Amplitudes already
             * rewritten would stay rewritten, which cannot happen because
             * `qaHamiltonianTarget` already succeeded on every `k` in
             * pass 1. */
            return status;
        }
        double phase = sa * h;
        psi[k] *= cos(phase) - sin(phase) * I;
    }

    return QA_OK;
}

/*
 * `qaEvolutionRx` (task T-008, FR-003, FR-022, FR-027, EC-014, EC-021).
 *
 * Implements the contract declared in `include/qa/evolution/primitives.h`:
 * the `N`-gate, NULL `psi`, non-finite or overflowing `dt`/`b`, `dim` check,
 * then one butterfly pass per cell; the first write follows every check.
 */
QaStatus qaEvolutionRx(unsigned int n, double dt, double b,
                       complex double *psi, size_t dim)
{
    size_t expectedDim = 0;

    /* Step 1 (FR-016): the gate precedes every size computation. */
    QaStatus status = qaPrimitivesResolveDim(n, &expectedDim);
    if (status != QA_OK) {
        return status;
    }

    /* Step 2 (FR-027): a NULL state is a domain error. */
    if (psi == NULL) {
        return QA_ERR_DOMAIN;
    }

    /* Step 3 (FR-027, EC-021): non-finite inputs, then `theta = dt * b` must
     * be finite; a large finite `theta` is accepted (EC-014). */
    if (!isfinite(dt) || !isfinite(b)) {
        return QA_ERR_DOMAIN;
    }
    double theta = dt * b;
    if (!isfinite(theta)) {
        return QA_ERR_DOMAIN;
    }

    /* Step 4 (FR-027): the buffer length must be exactly `2^(n*n)`, which
     * bounds every butterfly index below by the caller's buffer size. */
    if (dim != expectedDim) {
        return QA_ERR_RANGE;
    }

    /* Step 5: `theta` is finite, so `cos` and `sin` are defined. */
    double cs = cos(theta);
    double sn = sin(theta);
    complex double offDiag = -sn * I;
    unsigned int numCells = n * n; /* `n` is 2, 3 or 4: at most 16. */

    for (unsigned int c = 0; c < numCells; ++c) {
        /* Shift bound: `c < numCells <= 16`, below the width of `size_t`. */
        size_t bit = (size_t)1u << c;
        for (size_t i = 0; i < dim; ++i) {
            if ((i & bit) != 0u) {
                continue;
            }
            /* Index bound: `i < dim` and bit `c` of `i` is clear, so
             * `i + bit = i | bit < dim` (`bit < dim`): both reads and both
             * writes stay inside the caller's buffer. */
            complex double lo = psi[i];
            complex double hi = psi[i + bit];
            psi[i] = cs * lo + offDiag * hi;
            psi[i + bit] = offDiag * lo + cs * hi;
        }
    }

    return QA_OK;
}
