#ifndef QA_EVOLUTION_EVOLVE_INTERNAL_H
#define QA_EVOLUTION_EVOLVE_INTERNAL_H

/*
 * Private seam of the Strang driver for failure injection (spec 005-evolution,
 * T-004).
 *
 * Scope: FR-019 (every failure path releases what it acquired), FR-025
 * (allocation failures, schedule-evaluation failures and the per-step norm
 * failure are provoked through this table, never through memory exhaustion or
 * `/dev/full`), FR-006 (the per-step norm check that the perturb hook exercises),
 * plus EC-010 (one failure test per allocation point), EC-012 (schedule
 * evaluation fails at `tm_j` or at a trace node) and EC-020 (norm failure at
 * step `j`). Not installed and not a public header: only `src/evolution/evolve.c`
 * and the evolution and cli tests include it, always after `qa/evolution/evolve.h`
 * and `qa/evolution/schedules.h`. Declarations only; the libc-backed table and the
 * driver live in `src/evolution/evolve.c`.
 *
 * Definitions:
 *   ops table   the four hooks the driver calls: `alloc`, `release`, `schedule`
 *               (003 evaluation at `tm_j` and at each trace node) and `perturb`
 *               (test-only, may be NULL); each takes the table's `ctx` first
 *   perturb     called once per step `j = 1..steps` after the Strang step and
 *               before the FR-006 check; it may change the working state, which
 *               is how EC-020 reaches a norm failure no valid input can reach
 *   injection   a test copies `*qaEvolveDefaultOps()`, replaces one hook and
 *               passes the copy to `qaEvolveWith`
 *
 * Numerical bounds: none beyond those of `qa/evolution/evolve.h`.
 */

#include <complex.h>
#include <stddef.h>

/* qa/evolution/evolve.h and qa/evolution/schedules.h must be included first
 * (QaStatus, QaScheduleKind, QaEvolveParams, QaEvolveCallbacks, QaEvolveSummary). */

/**
 * @brief Hooks used by the Strang driver, replaceable by tests (FR-025, EC-010,
 * EC-012, EC-020).
 *
 * `alloc` has the semantics of `calloc` for `count` elements of `size` bytes,
 * returns NULL on failure, and is called only after the driver has checked that
 * `count * size` does not overflow. `release` is `free` and is called on every
 * non-NULL buffer the driver acquired. `schedule` has the contract of
 * `qaScheduleEval` (`qa/evolution/schedules.h`). `perturb` is optional: when it
 * is NULL no hook runs, which is the production path.
 *
 * @owner The table is owned by the caller and must outlive the call; the driver
 *        never copies or frees it. `alloc`, `release` and `schedule` must be
 *        non-NULL; `perturb` may be NULL. `ctx` is opaque to the driver.
 * @assumes Entries behave like their libc or 003 counterparts unless a test
 *          injects a failure. A buffer returned by `alloc` is released by
 *          `release` exactly once, on every path, including the failure paths.
 */
typedef struct QaEvolveOps {
    void *ctx;  /* passed as the first argument of every hook */
    void *(*alloc)(void *ctx, size_t count, size_t size);
    void (*release)(void *ctx, void *ptr);
    QaStatus (*schedule)(void *ctx, QaScheduleKind kind, double t, double T,
                         double *outA, double *outB);
    QaStatus (*perturb)(void *ctx, size_t j, complex double *psi, size_t dim);
} QaEvolveOps;

/**
 * @brief Return the libc- and 003-backed hooks of the Strang driver (FR-025).
 *
 * The table is a static, immutable object of the library with `ctx` NULL and
 * `perturb` NULL. Tests copy it by value and replace one hook. `qaEvolve` uses it
 * through `qaEvolveWith`.
 *
 * @return Non-NULL pointer to the static table; never NULL.
 *
 * @owner The table is owned by the library and never freed by the caller; the
 *        pointer stays valid for the process lifetime and must not be written.
 * @assumes The table is read-only and holds no state; its `schedule` hook is
 *          `qaScheduleEval` (003).
 */
const QaEvolveOps *qaEvolveDefaultOps(void);

/**
 * @brief Run the Strang evolution with an explicit hook table (FR-025, FR-019,
 * FR-020).
 *
 * Same contract, validation order, callback order and return codes as `qaEvolve`
 * (`qa/evolution/evolve.h`), with one extra step first:
 *   0. `ops == NULL`, or `ops -> alloc`, `ops -> release` or `ops -> schedule`
 *      NULL -> `QA_ERR_RANGE`. `ops -> perturb` may be NULL.
 * The FR-006 check that follows each step fails with `QA_ERR_DOMAIN` when the
 * working state is not finite or `|norm - 1| > QA_EVOLVE_NORM_TOL`, and it runs
 * after `ops -> perturb` of the same step. Any non-`QA_OK` value from `ops ->
 * perturb` or `ops -> schedule` stops the run at once and is returned unchanged.
 * `qaEvolve` is this function called with `qaEvolveDefaultOps()`.
 *
 * @param[in]  params    Same as `qaEvolve`.
 * @param[in]  initial   Same as `qaEvolve`.
 * @param[out] outFinal  Same as `qaEvolve`.
 * @param[in]  dim       Same as `qaEvolve`.
 * @param[in]  callbacks Same as `qaEvolve`.
 * @param[out] outSummary Same as `qaEvolve`.
 * @param[in]  ops       Non-NULL caller-owned hook table, see step 0.
 *
 * @return Same codes as `qaEvolve`, plus `QA_ERR_RANGE` for an invalid `ops`.
 *         On every failure `outFinal` and `outSummary` are unchanged.
 *
 * @owner Same as `qaEvolve`: the driver owns its buffers and frees each on the
 *        single cleanup path. `ops` and every pointer argument stay owned by the
 *        caller.
 * @assumes Same as `qaEvolve`. The hooks are called on the driver's thread only,
 *          and `ops` stays valid for the whole call.
 */
QaStatus qaEvolveWith(const QaEvolveParams *params, const complex double *initial,
                      complex double *outFinal, size_t dim,
                      const QaEvolveCallbacks *callbacks,
                      QaEvolveSummary *outSummary, const QaEvolveOps *ops);

/**
 * @brief Pairwise Euclidean norm `sqrt(sum_k |psi_k|^2)` (FR-005, FR-018,
 * FR-008).
 *
 * Sums over a fixed halving tree of `[0, dim)` with sequential leaves of 16
 * amplitudes, so the order depends on `dim` only. Used by the FR-018 entry
 * check, and later by the FR-006 check and `<H>`. Validation, completed before
 * the write: `psi`, `outNorm` NULL or `dim == 0` -> `QA_ERR_DOMAIN`. A
 * non-finite amplitude is NOT an error here: it yields a non-finite norm, and
 * the caller tests finiteness itself.
 *
 * @param[in]  psi     Non-NULL amplitudes, `dim` readable elements.
 * @param[in]  dim     Number of amplitudes, `>= 1`.
 * @param[out] outNorm Non-NULL receiver of the norm, written only on success.
 *
 * @return `QA_OK` (`*outNorm` written); `QA_ERR_DOMAIN` (NULL pointer or
 *         `dim == 0`). On failure `*outNorm` is unchanged.
 *
 * @owner No allocation; both pointers stay owned by the caller.
 * @assumes `dim <= 2^25`, so the recursion depth is at most 16; IEEE-754
 *          `double`, deterministic for equal inputs (FR-008).
 */
QaStatus qaEvolveNorm(const complex double *psi, size_t dim, double *outNorm);

#endif /* QA_EVOLUTION_EVOLVE_INTERNAL_H */
