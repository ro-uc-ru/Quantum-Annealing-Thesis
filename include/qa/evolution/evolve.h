#ifndef QA_EVOLUTION_EVOLVE_H
#define QA_EVOLUTION_EVOLVE_H

/*
 * Strang split-operator driver and time grid of the annealing evolution
 * (spec 005-evolution, T-003).
 *
 * Scope: FR-002 (exactly `steps` Strang steps with midpoint schedule values),
 * FR-006 (per-step norm and finiteness check, stop at once), FR-010 (final
 * state and summary), FR-011 (observer for `j = 0..steps`), FR-012 (snapshot
 * selection and bounds), FR-017 and FR-018 (domain of `steps`, `T`, family and
 * the initial state), FR-020 (numbered validation order of `qaEvolve`), FR-024
 * (`STEPS_MAX`), FR-026 (`qaEvolutionTimeNode` and `qaEvolutionTimeMid`), FR-028
 * (callback order and propagation; `evolution` does not depend on `io`). FR-019
 * governs the allocation and the callback error propagation. Declarations only;
 * behavior lives in `src/evolution/evolve.c` and `src/evolution/timegrid.c`.
 * The driver allocates its own work buffers and frees them on every path; the
 * caller owns every buffer it passes in.
 *
 * Definitions fixed by spec §1 and used by every declaration below:
 *   n            board edge, `2..4` accepted; `5` is reserved (QA_ERR_UNSUPPORTED)
 *   numCells     n * n                          (4, 9, 16 for n = 2, 3, 4)
 *   dim          2^numCells                     (16, 512, 65536)
 *   T            total annealing time, finite and strictly positive
 *   steps        number of Strang steps, `1..STEPS_MAX`
 *   dt           `T / steps`, finite and non-zero
 *   t_j          `T (j / steps)`, `j = 0..steps`, with `t_steps = T` exactly
 *   tm_j         `T ((2j + 1) / (2 steps))`, `j = 0..steps-1` (midpoint)
 *   a_j, b_j     schedule weights `a(tm_j)`, `b(tm_j)` of the requested family
 *   M            snapshot interval (`size_t`); `M = 0` selects only `j = steps`
 *   Strang step  `U_j = Dz(dt/2, a_j) Rx(dt, b_j) Dz(dt/2, a_j)` (FR-002)
 *   norm         `sqrt(sum_k |phi_k|^2)`, pairwise summation (FR-005)
 *
 * Numerical bounds (spec §1, ARC-4):
 *   - `QA_EVOLVE_STEPS_MAX = 10^6`: the drift bound of FR-005/FR-006 is measured
 *     up to this value; `2 steps` and `2j + 1` are exact below it.
 *   - `QA_EVOLVE_NORM_TOL = 1e-12`: absolute tolerance on `|norm - 1|`, both for
 *     the initial state (FR-018) and after every step (FR-006).
 *   - At most `QA_EVOLVE_SNAPSHOTS_MAX = 10000` snapshot files per run and at
 *     most `QA_EVOLVE_SNAPSHOT_ROWS_MAX = 2^24` total snapshot rows
 *     (`count * dim`), FR-012 and EC-015.
 *   - `|H_target(k)| <= 120` for every accepted `n` (see
 *     `qa/hamiltonian/target.h`), so `<H>` is bounded by `120` in magnitude.
 */

#include <complex.h>
#include <stddef.h>

#include "qa/core/status.h"
#include "qa/evolution/schedules.h"

/**
 * Maximum number of Strang steps (FR-024, spec §1). Above this value the
 * driver returns `QA_ERR_RANGE` before any allocation or write.
 */
#define QA_EVOLVE_STEPS_MAX 1000000u

/**
 * Absolute tolerance on `|norm - 1|` (ARC-4 default, spec §1). Used by the
 * initial-state check (FR-018) and the per-step check (FR-005, FR-006).
 */
#define QA_EVOLVE_NORM_TOL 1e-12

/**
 * Maximum number of snapshot files of one run (FR-012, FR-020 (4), EC-015).
 * Counted by the snapshot count formula, before any write.
 */
#define QA_EVOLVE_SNAPSHOTS_MAX 10000u

/**
 * Maximum total snapshot rows of one run, `count * dim <= 2^24` (FR-012,
 * FR-020 (5), EC-015). Bounds the record output of a run before any write.
 */
#define QA_EVOLVE_SNAPSHOT_ROWS_MAX ((size_t)1 << 24)

/**
 * @brief Parameters of one evolution run (FR-002, FR-017, FR-020).
 *
 * Plain value type holding the run's configuration. Nothing here is read from
 * the environment (spec §1).
 *
 * @owner Value type passed by const pointer; nothing to allocate or release.
 * @assumes `n`, `family`, `T`, `steps` and `snapshotInterval` are validated by
 *          `qaEvolve` in the order of its header; no field is trusted before
 *          that validation.
 */
typedef struct QaEvolveParams {
    unsigned int n;             /* board edge, 2..4 accepted (FR-016) */
    QaScheduleKind family;      /* schedule family of 003 (FR-002) */
    double T;                   /* total annealing time, finite and > 0 */
    size_t steps;               /* Strang steps, 1..QA_EVOLVE_STEPS_MAX */
    size_t snapshotInterval;    /* M, snapshot interval; 0 means only j = steps */
} QaEvolveParams;

/**
 * @brief Observables of one step `j`, delivered to the observer (FR-011).
 *
 * `energy` is `<H(t_j)>` for `j = 0..steps`, `norm` is the pairwise norm of
 * the state at `j`. The sample is valid only for the duration of the callback.
 *
 * @owner Value type owned by the driver; the observer receives a const pointer
 *        to a sample it must not retain beyond the callback.
 * @assumes `t` lies in `[0, T]`, `a` and `b` in `[0, 1]`, and `|norm - 1|`
 *          is within `QA_EVOLVE_NORM_TOL` for every delivered step (FR-006).
 */
typedef struct QaEvolveSample {
    size_t j;                   /* step index, 0..steps */
    double t;                   /* t_j, in [0, T] */
    double a;                   /* a(t_j), in [0, 1] */
    double b;                   /* b(t_j), in [0, 1] */
    double norm;                /* sqrt(sum |phi_k|^2) at step j */
    double energy;              /* <H(t_j)> = Re <phi|H(t_j)|phi> */
} QaEvolveSample;

/**
 * @brief Observer callback, called for `j = 0..steps` in increasing order
 * (FR-011, FR-028).
 *
 * Returns `QA_OK` to continue. Any other value stops the evolution at once and
 * is propagated unchanged by `qaEvolve` (FR-019, FR-028). The observer may be
 * NULL.
 *
 * @param[in] ctx    Caller-owned context, passed back unchanged.
 * @param[in] sample Observables of the step, valid only during the call.
 *
 * @return `QA_OK` to continue; any other `QaStatus` stops the run and is
 *         returned by `qaEvolve`.
 *
 * @owner The context and the sample both stay owned by the caller and the
 *        driver respectively; the observer never frees either.
 * @assumes Called only after the whole FR-020 validation succeeded and the
 *          step's FR-006 check passed.
 */
typedef QaStatus (*QaEvolveObserver)(void *ctx, const QaEvolveSample *sample);

/**
 * @brief Snapshot sink callback, called with `p_k = |phi_k|^2` for the
 * snapshot steps of FR-012 (FR-012, FR-028).
 *
 * Returns `QA_OK` to continue; any other value stops the evolution at once and
 * is propagated unchanged. The sink may be NULL.
 *
 * @param[in] ctx    Caller-owned context, passed back unchanged.
 * @param[in] j      Snapshot step index, from the FR-012 set.
 * @param[in] dim    Number of probabilities in `probs`, equal to `2^(n*n)`.
 * @param[in] probs  Probability vector `p_k`, `dim` doubles, valid only during
 *                   the call.
 *
 * @return `QA_OK` to continue; any other `QaStatus` stops the run and is
 *         returned by `qaEvolve`.
 *
 * @owner `probs` is owned by the driver and valid only during the call; the
 *        sink never frees it.
 * @assumes Each `p_k` is finite and non-negative, and `sum p_k` is within
 *          `2 QA_EVOLVE_NORM_TOL + 1e-15` of 1 (FR-010).
 */
typedef QaStatus (*QaEvolveSnapshotSink)(void *ctx, size_t j, size_t dim,
                                         const double *probs);

/**
 * @brief Pair of optional callbacks of `qaEvolve`, each with its own context.
 *
 * `qaEvolve` takes this struct by const pointer; the pointer itself must not
 * be NULL, but both members may be NULL (pure evolution, no files).
 *
 * @owner Value type owned by the caller; the driver reads it during the call
 *        and retains nothing.
 * @assumes Both contexts outlive the call that receives this struct.
 */
typedef struct QaEvolveCallbacks {
    QaEvolveObserver observer;  /* may be NULL */
    void *observerCtx;          /* passed back to observer unchanged */
    QaEvolveSnapshotSink sink;  /* may be NULL */
    void *sinkCtx;              /* passed back to sink unchanged */
} QaEvolveCallbacks;

/**
 * @brief Run summary of one successful evolution (FR-010).
 *
 * Feeds the CLI summary (FR-015). Only written by `qaEvolve` on success.
 *
 * @owner Value type owned by the caller; the driver writes it once on success.
 * @assumes `finalEnergy` is within `120` in magnitude (see the header scope),
 *          `maxNormDrift` is the maximum of `|norm - 1|` over every step and
 *          is at most `QA_EVOLVE_NORM_TOL` on success (FR-006).
 */
typedef struct QaEvolveSummary {
    double finalEnergy;         /* <H(T)> of the final state */
    double maxNormDrift;        /* max over j of |norm - 1| */
} QaEvolveSummary;

/**
 * @brief Time node `t_j = T (j / steps)` of the Strang grid (FR-026).
 *
 * Pure function: no state, no allocation, no IO. `t_steps` is exactly `T`
 * (no floating-point division at `j = steps`) and every value is clamped to
 * `min(t, T)`. The evolution and the trace use exactly this function.
 * Fixed validation order, completed before the write:
 *   1. `outT == NULL` -> `QA_ERR_DOMAIN` (FR-020 (2), pointer rule).
 *   2. `T` not finite or `T <= 0` -> `QA_ERR_DOMAIN` (FR-017, FR-026).
 *   3. `steps == 0` -> `QA_ERR_DOMAIN` (FR-017).
 *   4. `steps + 1` overflows (`steps == SIZE_MAX`) -> `QA_ERR_OVERFLOW`
 *      (FR-020 (4)).
 *   5. `steps > QA_EVOLVE_STEPS_MAX` -> `QA_ERR_RANGE` (FR-024).
 *   6. `dt = T / steps` zero or not finite -> `QA_ERR_DOMAIN` (FR-017).
 *   7. `j > steps` -> `QA_ERR_RANGE` (FR-026).
 *
 * @param[in]  T     Total annealing time, finite and `> 0`.
 * @param[in]  steps Strang steps, `1..QA_EVOLVE_STEPS_MAX`.
 * @param[in]  j     Node index, `0..steps`.
 * @param[out] outT  Non-NULL caller-owned receiver of `t_j`, in `[0, T]`.
 *
 * @return `QA_OK` (`*outT` written); `QA_ERR_DOMAIN` (NULL `outT`, `T` not
 *         finite or `<= 0`, `steps == 0`, or `dt` zero or not finite);
 *         `QA_ERR_OVERFLOW` (`steps == SIZE_MAX`); `QA_ERR_RANGE` (`steps`
 *         above `QA_EVOLVE_STEPS_MAX` or `j > steps`). On failure `*outT` is
 *         unchanged.
 *
 * @owner No allocation. `outT` stays owned by the caller on every path.
 * @assumes Deterministic and pure. `j / steps` is correctly rounded and at most
 *          1, so `t_j` lies in `[0, T]`; `t_steps = T` exactly. `steps` is at
 *          most `10^6`, so no intermediate overflows for any finite `T`.
 */
QaStatus qaEvolutionTimeNode(double T, size_t steps, size_t j, double *outT);

/**
 * @brief Midpoint time `tm_j = T ((2j + 1) / (2 steps))` of the Strang grid
 * (FR-026).
 *
 * Pure function: no state, no allocation, no IO. The fraction lies in `[0, 1]`
 * for `j = 0..steps-1`, so no intermediate overflows for any finite `T`. The
 * value is clamped to `min(t, T)`. The evolution and the trace use exactly this
 * function. Fixed validation order, completed before the write:
 *   1. `outT == NULL` -> `QA_ERR_DOMAIN` (FR-020 (2), pointer rule).
 *   2. `T` not finite or `T <= 0` -> `QA_ERR_DOMAIN` (FR-017, FR-026).
 *   3. `steps == 0` -> `QA_ERR_DOMAIN` (FR-017).
 *   4. `2 * steps` overflows (`steps > SIZE_MAX / 2`) -> `QA_ERR_OVERFLOW`
 *      (FR-020 (4)).
 *   5. `steps > QA_EVOLVE_STEPS_MAX` -> `QA_ERR_RANGE` (FR-024).
 *   6. `dt = T / steps` zero or not finite -> `QA_ERR_DOMAIN` (FR-017).
 *   7. `j >= steps` -> `QA_ERR_RANGE` (FR-026; `j` ranges over `0..steps-1`).
 *
 * @param[in]  T     Total annealing time, finite and `> 0`.
 * @param[in]  steps Strang steps, `1..QA_EVOLVE_STEPS_MAX`.
 * @param[in]  j     Midpoint index, `0..steps-1`.
 * @param[out] outT  Non-NULL caller-owned receiver of `tm_j`, in `[0, T]`.
 *
 * @return `QA_OK` (`*outT` written); `QA_ERR_DOMAIN` (NULL `outT`, `T` not
 *         finite or `<= 0`, `steps == 0`, or `dt` zero or not finite);
 *         `QA_ERR_OVERFLOW` (`steps > SIZE_MAX / 2`); `QA_ERR_RANGE` (`steps`
 *         above `QA_EVOLVE_STEPS_MAX` or `j >= steps`). On failure `*outT` is
 *         unchanged.
 *
 * @owner No allocation. `outT` stays owned by the caller on every path.
 * @assumes Deterministic and pure. `2 steps` and `2j + 1` are exact because
 *          `steps <= QA_EVOLVE_STEPS_MAX` after step 5; the rounding of the
 *          fraction keeps `j/steps <= (2j+1)/(2 steps) <= (j+1)/steps` in
 *          floating point for any finite `T`.
 */
QaStatus qaEvolutionTimeMid(double T, size_t steps, size_t j, double *outT);

/**
 * @brief Run the Strang split-operator evolution from `initial` into
 * `outFinal` (FR-002, FR-006, FR-010, FR-011, FR-012, FR-017..FR-020, FR-028).
 *
 * Performs exactly `steps` Strang steps `U_j = Dz(dt/2, a_j) Rx(dt, b_j)
 * Dz(dt/2, a_j)` for `j = 0..steps-1`, with `a_j`, `b_j` evaluated once per
 * step at `tm_j`. Fixed validation order of FR-020, completed before any
 * allocation, callback or write:
 *   1. `N`-gate (FR-016): `n == 5` -> `QA_ERR_UNSUPPORTED`; `n < 2` or
 *      `n > 5` -> `QA_ERR_RANGE`. No size arithmetic runs before this gate.
 *   2. Pointers (FR-020 (2)): `params`, `initial`, `outFinal`, `callbacks` and
 *      `outSummary` NULL -> `QA_ERR_DOMAIN`. The two callbacks may be NULL.
 *   3. `steps`, `T`, family (FR-017): `steps == 0`, `T` not finite or `T <= 0`,
 *      an invalid family, or `dt = T / steps` zero or not finite ->
 *      `QA_ERR_DOMAIN`.
 *   4. Size arithmetic (FR-020 (4)): `steps + 1` overflows (`steps == SIZE_MAX`)
 *      -> `QA_ERR_OVERFLOW`; `steps > QA_EVOLVE_STEPS_MAX` (FR-024) and the
 *      snapshot count `count > QA_EVOLVE_SNAPSHOTS_MAX` -> `QA_ERR_RANGE`. The
 *      count is `1` for `M = 0` and `1 + floor(steps / M) + [steps mod M != 0]`
 *      for `M > 0` (FR-012).
 *   5. `dim` (FR-020 (5)): `dim != 2^(n*n)` -> `QA_ERR_RANGE`; the snapshot row
 *      bound `count * dim > QA_EVOLVE_SNAPSHOT_ROWS_MAX` -> `QA_ERR_RANGE`
 *      (EC-015); `outFinal` overlapping `initial` by address range ->
 *      `QA_ERR_DOMAIN`. Both buffers hold exactly `dim` amplitudes.
 *   6. Initial state (FR-018): any non-finite amplitude or `|norm - 1| >
 *      QA_EVOLVE_NORM_TOL` -> `QA_ERR_DOMAIN`.
 *   7. Allocation (FR-019): every work buffer is allocated here; failure ->
 *      `QA_ERR_NOMEM`, every acquired resource released, no callback called.
 *
 * Callback order (FR-028, spec §1): observer `j = 0` and the sink if `0` is a
 * snapshot step; then for each `j = 1..steps`: step, FR-006 check, observer,
 * sink if `j` is a snapshot step. When both fire at one `j`, the observer runs
 * first. A failing step is never delivered. Any non-`QA_OK` value from a
 * callback stops the run at once and is returned unchanged (FR-019).
 *
 * On success `outFinal` holds `|phi(T)>`, `*outSummary` holds `<H(T)>` and the
 * maximum `|norm - 1|`, and `outSummary` is written only then. On failure
 * `outFinal` and `*outSummary` are unchanged, and the driver has released every
 * buffer it acquired (FR-019).
 *
 * @param[in]  params    Run configuration, non-NULL, validated by steps 1 to 5.
 * @param[in]  initial   Initial state, non-NULL, `dim` amplitudes, finite and
 *                       normalized within `QA_EVOLVE_NORM_TOL`; read only.
 * @param[out] outFinal  Non-NULL caller-owned receiver of `dim` amplitudes,
 *                       written only on success; must not overlap `initial`.
 * @param[in]  dim       Length of `initial` and `outFinal`, equal to
 *                       `2^(n*n)` (16, 512, 65536).
 * @param[in]  callbacks Non-NULL pair of optional callbacks with their contexts.
 * @param[out] outSummary Non-NULL caller-owned receiver of the run summary,
 *                       written only on success.
 *
 * @return `QA_OK` (run completed, `outFinal` and `outSummary` written);
 *         `QA_ERR_UNSUPPORTED` (`n == 5`); `QA_ERR_RANGE` (`n` outside
 *         `[2, 5]`, `steps > QA_EVOLVE_STEPS_MAX`, snapshot count or rows over
 *         their bounds, or `dim != 2^(n*n)`); `QA_ERR_DOMAIN` (NULL pointer,
 *         invalid `T`, `steps`, family or `dt`, overlapping buffers, or an
 *         invalid initial state, or an FR-006 norm or finiteness failure of a
 *         step); `QA_ERR_OVERFLOW` (`steps == SIZE_MAX`); `QA_ERR_NOMEM`
 *         (allocation failed); the value returned by a callback, propagated
 *         unchanged. On every failure `outFinal` and `outSummary` are unchanged.
 *
 * @owner `params`, `initial`, `outFinal`, `callbacks` and `outSummary` stay
 *        owned by the caller on every path. The driver owns its work state, its
 *        diagonal table of `H_target(k)` and its probability vector, and frees
 *        each on the single cleanup path (MEM-1, MEM-4).
 * @assumes `initial` and `outFinal` do not overlap. The evolution is
 *          deterministic: equal inputs and callbacks give bit-identical output on
 *          the same build (TST-3, FR-008). Norm and `<H>` are computed by the
 *          pairwise summation of FR-005. Unitarity of `Dz` and `Rx` keeps the
 *          norm within `QA_EVOLVE_NORM_TOL` for every step where FR-006 passes;
 *          no renormalization is applied.
 */
QaStatus qaEvolve(const QaEvolveParams *params, const complex double *initial,
                  complex double *outFinal, size_t dim,
                  const QaEvolveCallbacks *callbacks,
                  QaEvolveSummary *outSummary);

#endif /* QA_EVOLUTION_EVOLVE_H */
