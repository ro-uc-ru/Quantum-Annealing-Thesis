/*
 * 005-evolution Strang driver `qaEvolve` / `qaEvolveWith`: validation and the
 * pairwise norm (spec 005-evolution, task T-010; FR-016, FR-017, FR-018,
 * FR-020, FR-024, EC-002, EC-007, EC-008, EC-011, EC-015) and the Strang loop
 * (task T-011; FR-002, FR-005, FR-006, FR-008..FR-012, FR-021, FR-028).
 *
 * Purpose: runs the whole FR-020 validation in its fixed order before any
 * allocation, callback or write, then evolves a library-owned copy of the
 * initial state by exactly `steps` Strang steps, checks FR-006 after each
 * one, feeds the observer and the snapshot sink, and copies the result out
 * only on success. The pairwise norm serves the FR-018 entry check, the
 * FR-006 check and `<H>`.
 *
 * Ownership: the run owns three buffers allocated through `ops -> alloc`
 * after validation (work state, diagonal table of `H_target`, probability
 * vector when a sink is given), all released at the single `cleanup` label.
 * Every pointer argument stays owned by the caller; `outFinal` and
 * `outSummary` are written only after the last step succeeded.
 * Errors: `QA_ERR_UNSUPPORTED` (`n == 5`), `QA_ERR_RANGE`, `QA_ERR_DOMAIN`,
 * `QA_ERR_OVERFLOW` as documented in `qa/evolution/evolve.h`; combined faults
 * resolve by the FR-020 order.
 * Numerical assumptions: IEEE-754 `double`. The norm sums `|phi_k|^2` over a
 * fixed halving tree with sequential leaves, so the summation order depends on
 * `dim` only (FR-008). `steps <= 10^6` and `count <= 10^4` once checked, so no
 * product below overflows a 64-bit `size_t`. No renormalization is applied:
 * unitarity of `Dz` and `Rx` keeps the norm within `NORM_TOL` (FR-005) and a
 * violation stops the run (FR-006).
 */

#include "qa/evolution/evolve.h"
#include "qa/evolution/primitives.h"
#include "qa/hamiltonian/target.h"
#include "evolve-internal.h"

#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Leaf block of the pairwise norm tree: blocks of at most this many amplitudes
 * are summed sequentially (FR-005, FR-008). */
#define QA_EVOLVE_NORM_LEAF 16u

/**
 * @brief Sum `|psi_k|^2` over `count` amplitudes by recursive halving.
 *
 * @param[in] psi   Amplitudes, at least `count` readable elements.
 * @param[in] count Number of amplitudes, `>= 1`.
 *
 * @return The sum, possibly non-finite when an amplitude is non-finite.
 *
 * @owner No allocation, nothing is written.
 * @assumes The recursion depth is `log2(count / leaf) + 1 <= 16` for
 *          `count <= 2^25`; the split `count / 2`, `count - count / 2` is a
 *          pure function of `count`, so the order is fixed (FR-008).
 */
static double qaEvolveSumSq(const complex double *psi, size_t count)
{
    if (count <= QA_EVOLVE_NORM_LEAF) {
        double sum = 0.0;
        /* Bound: `k < count` and the caller guarantees `psi` holds `count` elements, so `psi[k]` is in range. */
        for (size_t k = 0; k < count; ++k) {
            double re = creal(psi[k]);
            double im = cimag(psi[k]);
            sum += re * re + im * im;
        }
        return sum;
    }
    size_t half = count / 2u;
    /* Bound: `half = count / 2 < count`, so the left call reads `psi[0 .. half)` and the right call reads `psi + half` with length `count - half`; together they cover exactly `count` elements. */
    return qaEvolveSumSq(psi, half) + qaEvolveSumSq(psi + half, count - half);
}

/*
 * `qaEvolveNorm` (task T-010, FR-005, FR-018).
 *
 * Implements the internal contract declared in `src/evolution/evolve-internal.h`.
 */
QaStatus qaEvolveNorm(const complex double *psi, size_t dim, double *outNorm)
{
    /* Validation precedes the single write below. */
    if (psi == NULL || outNorm == NULL || dim == 0u) {
        return QA_ERR_DOMAIN;
    }
    *outNorm = sqrt(qaEvolveSumSq(psi, dim));
    return QA_OK;
}

/**
 * @brief Count the snapshots of FR-012 for `steps` and `M`, without overflow.
 *
 * @param[in] steps `1..QA_EVOLVE_STEPS_MAX`.
 * @param[in] interval `M`; `0` selects only `j = steps`.
 *
 * @return `1` for `M = 0`; otherwise `1 + floor(steps / M) + [steps mod M != 0]`.
 *
 * @owner No allocation. @assumes `steps <= 10^6` (checked by the caller), so
 *        `1 + steps / M + 1 <= 10^6 + 2` cannot overflow `size_t`.
 */
static size_t qaEvolveSnapshotCount(size_t steps, size_t interval)
{
    if (interval == 0u) {
        return 1u;
    }
    /* Bound: `steps <= 10^6` (QA_EVOLVE_STEPS_MAX, validated by the caller),
     * so the sum is at most `10^6 + 2` and cannot wrap `size_t`. */
    return 1u + steps / interval + ((steps % interval != 0u) ? 1u : 0u);
}

/**
 * @brief Tell whether two buffers of `dim` amplitudes overlap by address range.
 *
 * @param[in] a First buffer, non-NULL. @param[in] b Second buffer, non-NULL.
 * @param[in] dim Amplitudes in each, `<= 2^16`.
 *
 * @return `1` when the byte ranges intersect, `0` otherwise.
 *
 * @owner No allocation, nothing is dereferenced.
 * @assumes `dim * sizeof (complex double) <= 2^20`, so `start + bytes` does not
 *          wrap `uintptr_t` for any valid object; comparing integers avoids
 *          undefined relational comparison of unrelated pointers.
 */
static int qaEvolveOverlaps(const complex double *a, const complex double *b,
                            size_t dim)
{
    uintptr_t pa = (uintptr_t)a;
    uintptr_t pb = (uintptr_t)b;
    /* Bound: `dim <= 2^16` (n <= 4, validated by the caller), so the product
     * is at most 2^20 bytes and cannot wrap `size_t` or `uintptr_t`. */
    uintptr_t bytes = (uintptr_t)(dim * sizeof (complex double));

    return pa < pb + bytes && pb < pa + bytes;
}

/**
 * @brief Run FR-020 steps 1 to 6 for one call; nothing is allocated or written.
 *
 * @param[in] params    Run configuration, may be NULL (step 2 rejects it).
 * @param[in] initial   Initial state, may be NULL (step 2 rejects it).
 * @param[in] outFinal  Receiver, only its address is examined.
 * @param[in] dim       Claimed length of both buffers.
 * @param[in] callbacks Callback pair; members are not examined here.
 * @param[in] outSummary Receiver, only tested against NULL.
 *
 * @return `QA_OK` when every step passes; otherwise the first failing step's
 *         code (`QA_ERR_UNSUPPORTED`, `QA_ERR_RANGE`, `QA_ERR_DOMAIN`,
 *         `QA_ERR_OVERFLOW`).
 *
 * @owner No allocation; every pointer stays caller-owned.
 * @assumes Steps run strictly in the FR-020 order so combined faults resolve
 *          by it; `params -> n` is read only after `params != NULL`.
 */
static QaStatus qaEvolveValidate(const QaEvolveParams *params,
                                 const complex double *initial,
                                 const complex double *outFinal, size_t dim,
                                 const QaEvolveCallbacks *callbacks,
                                 const QaEvolveSummary *outSummary)
{
    /* Step 1, N-gate (FR-016): precedes any size arithmetic. With `params`
     * NULL there is no `n` to gate; step 2 then rejects the call. */
    if (params != NULL) {
        if (params -> n == 5u) {
            return QA_ERR_UNSUPPORTED;
        }
        if (params -> n < 2u || params -> n > 5u) {
            return QA_ERR_RANGE;
        }
    }

    /* Step 2, pointers (FR-020 (2)); the two callbacks inside `callbacks` may
     * be NULL, the pair itself may not. */
    if (params == NULL || initial == NULL || outFinal == NULL
        || callbacks == NULL || outSummary == NULL) {
        return QA_ERR_DOMAIN;
    }

    /* Step 3, `T`, family, `steps`, `dt` (FR-017). `steps >= 1` is tested
     * before the division; `steps` is not yet bounded, so `dt` is formed from
     * the double conversion only to detect underflow of a subnormal `T`. */
    if (!isfinite(params -> T) || params -> T <= 0.0 || params -> steps == 0u) {
        return QA_ERR_DOMAIN;
    }
    const char *name = NULL;
    if (qaScheduleName(params -> family, &name) != QA_OK) {
        return QA_ERR_DOMAIN;
    }
    double dt = params -> T / (double)params -> steps;
    if (dt == 0.0 || !isfinite(dt)) {
        return QA_ERR_DOMAIN;
    }

    /* Step 4, size arithmetic (FR-020 (4), EC-002, FR-024, EC-015). */
    if (params -> steps == SIZE_MAX) {
        return QA_ERR_OVERFLOW; /* `steps + 1` would wrap */
    }
    if (params -> steps > (size_t)QA_EVOLVE_STEPS_MAX) {
        return QA_ERR_RANGE;
    }
    /* `steps <= 10^6` here, so the count helper cannot overflow. */
    size_t count = qaEvolveSnapshotCount(params -> steps, params -> snapshotInterval);
    if (count > (size_t)QA_EVOLVE_SNAPSHOTS_MAX) {
        return QA_ERR_RANGE;
    }

    /* Step 5, `dim`, rows, overlap (FR-020 (5), EC-011, EC-015). `n <= 4` after
     * the N-gate, so `n * n <= 16` and the shift is defined. */
    size_t expectedDim = (size_t)1 << (params -> n * params -> n);
    if (dim != expectedDim) {
        return QA_ERR_RANGE;
    }
    /* `count <= 10^4` and `dim <= 2^16`: the product fits `size_t`. */
    if (count * dim > QA_EVOLVE_SNAPSHOT_ROWS_MAX) {
        return QA_ERR_RANGE;
    }
    if (qaEvolveOverlaps(initial, outFinal, dim)) {
        return QA_ERR_DOMAIN;
    }

    /* Step 6, initial state (FR-018, EC-007, EC-008): every amplitude finite,
     * then `|norm - 1| <= NORM_TOL` (the boundary value is accepted). */
    /* Bound: `k < dim` and step 5 validated `dim`, so `initial[k]` is in range. */
    for (size_t k = 0; k < dim; ++k) {
        if (!isfinite(creal(initial[k])) || !isfinite(cimag(initial[k]))) {
            return QA_ERR_DOMAIN;
        }
    }
    double norm = 0.0;
    QaStatus status = qaEvolveNorm(initial, dim, &norm);
    if (status != QA_OK) {
        return status;
    }
    if (!(fabs(norm - 1.0) <= QA_EVOLVE_NORM_TOL)) {
        return QA_ERR_DOMAIN;
    }
    return QA_OK;
}

/**
 * @brief Default `alloc` hook: `calloc` ignoring the opaque context.
 *
 * @param[in] ctx Unused. @param[in] count Elements. @param[in] size Bytes each.
 * @return Zeroed block or NULL. @owner The caller releases it with
 *         `qaEvolveDefaultRelease`. @assumes `count * size` was checked.
 */
static void *qaEvolveDefaultAlloc(void *ctx, size_t count, size_t size)
{
    (void)ctx;
    return calloc(count, size);
}

/**
 * @brief Default `release` hook: `free` ignoring the opaque context.
 *
 * @param[in] ctx Unused. @param[in] ptr Block from the default alloc or NULL.
 * @return Nothing. @owner Takes ownership of `ptr` and frees it once.
 * @assumes `ptr` came from `qaEvolveDefaultAlloc` and is not used afterwards.
 */
static void qaEvolveDefaultRelease(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

/**
 * @brief Default `schedule` hook: `qaScheduleEval` ignoring the context.
 *
 * @param[in] ctx Unused. @param[in] kind Family. @param[in] t Time.
 * @param[in] T Total time. @param[out] outA Receiver of `a`.
 * @param[out] outB Receiver of `b`.
 * @return The code of `qaScheduleEval`. @owner Receivers stay caller-owned.
 * @assumes Arguments are validated as the 003 contract requires.
 */
static QaStatus qaEvolveDefaultSchedule(void *ctx, QaScheduleKind kind, double t,
                                        double T, double *outA, double *outB)
{
    (void)ctx;
    return qaScheduleEval(kind, t, T, outA, outB);
}

/* Immutable libc- and 003-backed table; `ctx` and `perturb` are NULL. */
static const QaEvolveOps qaEvolveDefault = {
    NULL, qaEvolveDefaultAlloc, qaEvolveDefaultRelease, qaEvolveDefaultSchedule,
    NULL
};

/*
 * `qaEvolveDefaultOps` (task T-010, FR-025).
 *
 * Implements the contract declared in `src/evolution/evolve-internal.h`.
 */
const QaEvolveOps *qaEvolveDefaultOps(void)
{
    return &qaEvolveDefault;
}

/* Pairwise partial sums of one subtree: `sq = sum |psi_k|^2`,
 * `diag = sum diag[k] |psi_k|^2`, `drv = sum_c sum_k Re(conj(psi_k) psi_{k^m_c})`. */
typedef struct QaEvolveSums {
    double sq;
    double diag;
    double drv;
} QaEvolveSums;

/**
 * @brief Pairwise sums of norm, target energy and driver energy over
 * `[base, base + count)` of the full state, on the same tree as `qaEvolveSumSq`.
 *
 * @param[in]  psi      Full state of `dim` amplitudes.
 * @param[in]  diag     Table `H_target(k)` of `dim` doubles.
 * @param[in]  numCells `n * n`, `<= 16`; bit `c` of `k` is cell `c`.
 * @param[in]  base     First index of the subtree. @param[in] count Its length.
 * @param[out] outSums  Non-NULL receiver of the three sums.
 *
 * @return Nothing. @owner No allocation; `outSums` stays caller-owned.
 * @assumes `base + count <= dim` and `k ^ (1 << c) < dim` for `c < numCells`
 *          (dim is `2^numCells`), so every read is in bounds. The split is
 *          `count / 2` exactly as `qaEvolveSumSq`, so `sq` is bit-identical to
 *          the squared `qaEvolveNorm` and the order depends on `dim` only.
 */
static void qaEvolveSums3(const complex double *psi, const double *diag,
                          unsigned int numCells, size_t base, size_t count,
                          QaEvolveSums *outSums)
{
    if (count <= QA_EVOLVE_NORM_LEAF) {
        QaEvolveSums leaf = {0.0, 0.0, 0.0};
        for (size_t k = base; k < base + count; ++k) {
            /* Bound: the leaf spans `[base, base + count)` inside `[0, dim)`
             * by the recursion, so `k < dim` indexes `psi` and `diag`. */
            double re = creal(psi[k]);
            double im = cimag(psi[k]);
            double p = re * re + im * im;
            leaf.sq += p;
            leaf.diag += diag[k] * p;
            for (unsigned int c = 0; c < numCells; ++c) {
                /* Bound: `k < dim` and `c < numCells`, so the partner index
                 * is below `dim`. */
                complex double other = psi[k ^ ((size_t)1 << c)];
                leaf.drv += re * creal(other) + im * cimag(other);
            }
        }
        *outSums = leaf;
        return;
    }
    size_t half = count / 2u;
    QaEvolveSums left;
    QaEvolveSums right;
    qaEvolveSums3(psi, diag, numCells, base, half, &left);
    qaEvolveSums3(psi, diag, numCells, base + half, count - half, &right);
    outSums -> sq = left.sq + right.sq;
    outSums -> diag = left.diag + right.diag;
    outSums -> drv = left.drv + right.drv;
}

/**
 * @brief Tell whether step `j` is a snapshot step (FR-012).
 *
 * @param[in] j Step, `0..steps`. @param[in] steps Total steps.
 * @param[in] interval `M`; `0` selects only `j = steps`.
 * @return `1` for `j = steps` or (`M > 0` and `j` a multiple of `M`), else `0`.
 * @owner No allocation. @assumes `interval == 0` is tested before `%`.
 */
static int qaEvolveIsSnapshot(size_t j, size_t steps, size_t interval)
{
    return j == steps || (interval != 0u && j % interval == 0u);
}

/**
 * @brief Apply `Dz(s, a)` with the precomputed table, bit-identical to
 * `qaEvolutionDz` (same phase expression, same order of `k`).
 *
 * @param[in]     diag Table `H_target(k)`, `dim` doubles.
 * @param[in]     s    Time factor, finite. @param[in] a Weight, finite.
 * @param[in,out] psi  Work state of `dim` amplitudes.
 * @param[in]     dim  Length of both arrays.
 * @return Nothing. @owner No allocation; `psi` stays owned by the run.
 * @assumes `k < dim` bounds every access; a phase that overflows yields a
 *          non-finite amplitude that the FR-006 check then rejects.
 */
static void qaEvolveApplyDz(const double *diag, double s, double a,
                            complex double *psi, size_t dim)
{
    double sa = s * a;
    for (size_t k = 0; k < dim; ++k) {
        /* Bound: the loop condition gives `k < dim`, the length of both
         * `diag` and `psi`, so the read and the write stay inside them. */
        double phase = sa * diag[k];
        psi[k] *= cos(phase) - sin(phase) * I;
    }
}

/**
 * @brief Measure norm and `<H> = a <H_target> + b <H_driver>` of the state.
 *
 * @param[in]  psi  Work state. @param[in] diag Table. @param[in] n Board edge.
 * @param[in]  dim  Length. @param[in] a Weight of `H_target`.
 * @param[in]  b    Weight of `H_driver`.
 * @param[out] outNorm   Receiver of `sqrt(sum |psi_k|^2)`.
 * @param[out] outEnergy Receiver of `Re <psi|H|psi>`.
 * @return Nothing. @owner No allocation; receivers stay caller-owned.
 * @assumes `dim == 2^(n*n)`. `<H_driver> = sum_c <sigma^x_c>` is real for a
 *          Hermitian operator; only its real part is accumulated.
 */
static void qaEvolveObserve(const complex double *psi, const double *diag,
                            unsigned int n, size_t dim, double a, double b,
                            double *outNorm, double *outEnergy)
{
    QaEvolveSums sums;

    /* Bound: `n <= 4` (validated by the caller), so `n * n <= 16` cannot wrap
     * and `dim == 2^(n*n)`; the walk covers `[0, dim)` exactly once. */
    qaEvolveSums3(psi, diag, n * n, 0u, dim, &sums);
    *outNorm = sqrt(sums.sq);
    *outEnergy = a * sums.diag + b * sums.drv;
}

/**
 * @brief Run the allocation, the Strang loop and the copy-out after a
 * successful validation (FR-002, FR-006, FR-010..FR-012, FR-019, FR-021,
 * FR-028).
 *
 * @param[in]  params Validated run configuration. @param[in] initial Validated
 *             initial state. @param[out] outFinal Receiver, written on success
 *             only. @param[in] dim `2^(n*n)`. @param[in] callbacks Non-NULL.
 * @param[out] outSummary Receiver, written on success only.
 * @param[in]  ops Validated hook table.
 *
 * @return `QA_OK`; `QA_ERR_NOMEM`; `QA_ERR_DOMAIN` (FR-006); a schedule,
 *         perturb or callback code unchanged. On failure `outFinal` and
 *         `outSummary` are unchanged.
 *
 * @owner Allocates work, diag and (with a sink) probs through `ops -> alloc`;
 *        every one is released by `ops -> release` at `cleanup` exactly once.
 * @assumes Validation passed, so `steps` is in `[1, 10^6]`, `T / steps` is a
 *          finite non-zero `dt`, and `dim <= 2^16`, hence `dim * 16` bytes
 *          cannot overflow `size_t`. The caller's time functions cannot fail
 *          after validation; any code is still propagated (ERR-1).
 */
static QaStatus qaEvolveRun(const QaEvolveParams *params,
                            const complex double *initial,
                            complex double *outFinal, size_t dim,
                            const QaEvolveCallbacks *callbacks,
                            QaEvolveSummary *outSummary, const QaEvolveOps *ops)
{
    QaStatus status = QA_OK;
    complex double *work = NULL;
    double *diag = NULL;
    double *probs = NULL;
    const unsigned int n = params -> n;
    const size_t steps = params -> steps;
    const double dt = params -> T / (double)steps;
    double maxDrift = 0.0;
    double energy = 0.0;

    /* Step 7 (FR-019): every buffer is acquired here, in a fixed order.
     * Bound: `dim <= 2^16` (n <= 4), so each `count * size` is at most 2^19
     * bytes and cannot overflow `size_t`. */
    work = ops -> alloc(ops -> ctx, dim, sizeof (complex double));
    diag = ops -> alloc(ops -> ctx, dim, sizeof (double));
    if (work == NULL || diag == NULL) {
        status = QA_ERR_NOMEM;
        goto cleanup;
    }
    if (callbacks -> sink != NULL) {
        probs = ops -> alloc(ops -> ctx, dim, sizeof (double));
        if (probs == NULL) {
            status = QA_ERR_NOMEM;
            goto cleanup;
        }
    }

    /* Table of `H_target(k)`: a vector of `dim` doubles, not a matrix (ARC-1);
     * `k < dim` bounds every write. */
    for (size_t k = 0; k < dim; ++k) {
        /* Defensive: `n` is 2..4 and `k < dim == 2^(n*n)` by validation, so
         * the call cannot fail; the check keeps ERR-2 and frees via `cleanup`. */
        status = qaHamiltonianTarget(n, k, &diag[k]);
        if (status != QA_OK) {
            goto cleanup;
        }
    }
    /* `dim` amplitudes fit both buffers (validated and allocated above). */
    memcpy(work, initial, dim * sizeof (complex double));

    /* `j = 0 .. steps`: sample, observer, sink; for `j >= 1` the step and the
     * FR-006 check come first so a failing step is never delivered. */
    for (size_t j = 0; j <= steps; ++j) {
        double t = 0.0;
        double a = 0.0;
        double b = 0.0;
        double norm = 0.0;

        if (j >= 1u) {
            double tm = 0.0;
            status = qaEvolutionTimeMid(params -> T, steps, j - 1u, &tm);
            if (status != QA_OK) {
                goto cleanup;
            }
            status = ops -> schedule(ops -> ctx, params -> family, tm,
                                     params -> T, &a, &b);
            if (status != QA_OK) {
                goto cleanup;
            }
            /* Strang step `Dz(dt/2, a) Rx(dt, b) Dz(dt/2, a)` (FR-002). */
            qaEvolveApplyDz(diag, dt / 2.0, a, work, dim);
            /* Defensive: `n`, `dt` and `dim` are validated and `b` is the
             * schedule output checked above, so a failure is not expected; the
             * check keeps ERR-2 and any status is propagated via `cleanup`. */
            status = qaEvolutionRx(n, dt, b, work, dim);
            if (status != QA_OK) {
                goto cleanup;
            }
            qaEvolveApplyDz(diag, dt / 2.0, a, work, dim);
            if (ops -> perturb != NULL) {
                status = ops -> perturb(ops -> ctx, j, work, dim);
                if (status != QA_OK) {
                    goto cleanup;
                }
            }
        }

        status = qaEvolutionTimeNode(params -> T, steps, j, &t);
        if (status != QA_OK) {
            goto cleanup;
        }
        status = ops -> schedule(ops -> ctx, params -> family, t, params -> T,
                                 &a, &b);
        if (status != QA_OK) {
            goto cleanup;
        }
        qaEvolveObserve(work, diag, n, dim, a, b, &norm, &energy);

        /* FR-006: a non-finite amplitude makes `norm` NaN or infinite, so the
         * negated comparison rejects it together with a drift above tolerance. */
        double drift = fabs(norm - 1.0);
        if (!(drift <= QA_EVOLVE_NORM_TOL) || !isfinite(energy)) {
            status = QA_ERR_DOMAIN;
            goto cleanup;
        }
        if (drift > maxDrift) {
            maxDrift = drift;
        }

        if (callbacks -> observer != NULL) {
            QaEvolveSample sample = {j, t, a, b, norm, energy};
            status = callbacks -> observer(callbacks -> observerCtx, &sample);
            if (status != QA_OK) {
                goto cleanup;
            }
        }
        if (callbacks -> sink != NULL
            && qaEvolveIsSnapshot(j, steps, params -> snapshotInterval)) {
            /* `k < dim` bounds the write into `probs` (`dim` doubles). */
            for (size_t k = 0; k < dim; ++k) {
                double re = creal(work[k]);
                double im = cimag(work[k]);
                probs[k] = re * re + im * im;
            }
            status = callbacks -> sink(callbacks -> sinkCtx, j, dim, probs);
            if (status != QA_OK) {
                goto cleanup;
            }
        }
    }

    /* Copy-out and summary only after the last check passed (FR-010);
     * `dim` amplitudes fit `outFinal` (validated, FR-020 (5)). */
    memcpy(outFinal, work, dim * sizeof (complex double));
    outSummary -> finalEnergy = energy;
    outSummary -> maxNormDrift = maxDrift;

cleanup:
    if (probs != NULL) {
        ops -> release(ops -> ctx, probs);
    }
    if (diag != NULL) {
        ops -> release(ops -> ctx, diag);
    }
    if (work != NULL) {
        ops -> release(ops -> ctx, work);
    }
    return status;
}

/*
 * `qaEvolveWith` (tasks T-010 and T-011).
 *
 * Implements the contract declared in `src/evolution/evolve-internal.h`.
 */
QaStatus qaEvolveWith(const QaEvolveParams *params, const complex double *initial,
                      complex double *outFinal, size_t dim,
                      const QaEvolveCallbacks *callbacks,
                      QaEvolveSummary *outSummary, const QaEvolveOps *ops)
{
    /* Step 0: an invalid table is rejected before anything is read. */
    if (ops == NULL || ops -> alloc == NULL || ops -> release == NULL
        || ops -> schedule == NULL) {
        return QA_ERR_RANGE;
    }

    QaStatus status = qaEvolveValidate(params, initial, outFinal, dim, callbacks,
                                       outSummary);
    if (status != QA_OK) {
        return status;
    }

    return qaEvolveRun(params, initial, outFinal, dim, callbacks, outSummary, ops);
}

/*
 * `qaEvolve` (task T-010).
 *
 * Implements the contract declared in `include/qa/evolution/evolve.h`.
 */
QaStatus qaEvolve(const QaEvolveParams *params, const complex double *initial,
                  complex double *outFinal, size_t dim,
                  const QaEvolveCallbacks *callbacks,
                  QaEvolveSummary *outSummary)
{
    return qaEvolveWith(params, initial, outFinal, dim, callbacks, outSummary,
                        qaEvolveDefaultOps());
}
