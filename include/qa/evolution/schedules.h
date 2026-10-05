#ifndef QA_EVOLUTION_SCHEDULES_H
#define QA_EVOLUTION_SCHEDULES_H

/*
 * Annealing schedules a(t) and b(t): pure evaluation of the four families
 * (spec 003-schedules, T-001).
 *
 * Scope: FR-002 (exactly four families), FR-013 and FR-014 (stable names),
 * plus the `qaScheduleEval` contract (FR-001, FR-003..FR-012). Declarations
 * only; behavior lives in `src/evolution/schedules/schedules.c`. No
 * allocation, no IO, no state.
 *
 * Definitions fixed by spec §1 and used by every function below:
 *   T          total annealing time, finite and strictly positive
 *   t          current time, `0 <= t <= T`
 *   s          normalized time `t / T`, in [0, 1]; every family depends on
 *              `t` and `T` only through `s`
 *   a(t)       weight growing from 0 to 1, multiplies the problem Hamiltonian
 *   b(t)       weight decaying from 1 to 0, multiplies the driver Hamiltonian
 *   SCHED_TOL  1e-12, absolute tolerance for boundaries, `a + b = 1`,
 *              monotonicity and scale invariance
 *
 * Families (formulas of the original TFG, with `s = t / T`):
 *   linear          a = s               b = 1 - s
 *   trigonometric   a = sin^2(pi*s/2)   b = cos^2(pi*s/2)
 *   quadratic       a = s^2             b = 1 - s^2
 *   exponential     a = 2^s - 1         b = 2 - 2^s
 *
 * Numerical bounds: the range `[0, 1]` of `a` and `b` is exact because the
 * final values are clamped (FR-005), while boundaries, sum, monotonicity and
 * scale invariance hold within `SCHED_TOL`.
 */

#include "qa/core/status.h"

/**
 * @brief The four annealing schedule families (FR-002).
 *
 * Each family interpolates `(a, b)` from `(0, 1)` at `t = 0` to `(1, 0)` at
 * `t = T` with its own closed form (see the table above). The values are
 * contiguous from 0, in the order of the stable names of `qaScheduleName`.
 * A C enum accepts any integer, so every function that takes a kind validates
 * it and rejects anything outside this set with `QA_ERR_DOMAIN` (FR-011,
 * FR-014).
 *
 * @owner Value type passed by value; nothing to allocate or release.
 * @assumes Exactly four enumerators, valued 0..3; adding a family is a spec
 *          change (out of scope of 003-schedules) and MUST update every
 *          function that switches on the kind.
 */
typedef enum QaScheduleKind {
    QA_SCHEDULE_LINEAR = 0,        /* a = s, b = 1 - s */
    QA_SCHEDULE_TRIGONOMETRIC = 1, /* a = sin^2(pi*s/2), b = cos^2(pi*s/2) */
    QA_SCHEDULE_QUADRATIC = 2,     /* a = s^2, b = 1 - s^2 */
    QA_SCHEDULE_EXPONENTIAL = 3    /* a = 2^s - 1, b = 2 - 2^s */
} QaScheduleKind;

/**
 * @brief Evaluate the weights `a(t)` and `b(t)` of one schedule family
 * (FR-001..FR-012).
 *
 * Pure function: no state, no allocation, no IO. Fixed validation order,
 * completed before any arithmetic or write (spec §2):
 *   1. `kind` is not one of the four families -> `QA_ERR_DOMAIN` (FR-011).
 *   2. `outA == NULL` or `outB == NULL` -> `QA_ERR_DOMAIN` (FR-012).
 *   3. `T` is NaN, `+-inf` or `T <= 0` -> `QA_ERR_DOMAIN` (FR-009).
 *   4. `t` is NaN, `t < 0` or `t > T` -> `QA_ERR_DOMAIN` (FR-010); `t = -0.0`
 *      is treated as `0` and accepted (EC-006). The function never
 *      extrapolates or saturates `t`.
 * On success it computes `s = t / T`, evaluates the family's closed form,
 * clamps both weights to `[0, 1]` and only then writes `*outA` and `*outB`.
 *
 * @param[in]  kind Schedule family, one of the four `QaScheduleKind`.
 * @param[in]  t    Current time, `0 <= t <= T`, finite.
 * @param[in]  T    Total annealing time, finite and `> 0`.
 * @param[out] outA Non-NULL caller-owned receiver of `a(t)`, in `[0, 1]`.
 * @param[out] outB Non-NULL caller-owned receiver of `b(t)`, in `[0, 1]`.
 *
 * @return `QA_OK` (both receivers written); `QA_ERR_DOMAIN` (invalid `kind`,
 *         NULL receiver, `T` not finite or `<= 0`, or `t` NaN, negative or
 *         above `T`). On failure both receivers are left untouched (spec §1),
 *         so they are validated but never poisoned.
 *
 * @owner No allocation. Both receivers stay owned by the caller on every path.
 * @assumes Deterministic: equal inputs give bit-identical results on the same
 *          build and platform, with no dependence on global state (FR-008).
 *          `s = t / T` is correctly rounded, so `s` lies in `[0, 1]` and
 *          `t = T` gives exactly `s = 1`; no overflow occurs for any valid
 *          `T`, including `1e300`, `1e-300` and subnormals (EC-003, EC-007).
 *          Results satisfy `a(0) = 0`, `a(T) = 1`, `b(0) = 1`, `b(T) = 0`,
 *          `|a + b - 1| <= SCHED_TOL`, monotonicity within `SCHED_TOL` and
 *          scale invariance in `(t, T)` within `SCHED_TOL` (FR-003, FR-004,
 *          FR-006, FR-007); the range `[0, 1]` is exact (FR-005).
 */
QaStatus qaScheduleEval(QaScheduleKind kind, double t, double T,
                        double *outA, double *outB);

/**
 * @brief Return the stable text name of a schedule family (FR-013, FR-014).
 *
 * Pure function: no allocation, no IO. Fixed validation order, completed
 * before the write:
 *   1. `kind` is not one of the four families -> `QA_ERR_DOMAIN` (FR-014).
 *   2. `name == NULL` -> `QA_ERR_DOMAIN` (FR-014).
 * On success `*name` is one of `"linear"`, `"trigonometric"`, `"quadratic"`
 * or `"exponential"`, matching the order of `QaScheduleKind`, for the run
 * configuration record (TST-4).
 *
 * @param[in]  kind Schedule family, one of the four `QaScheduleKind`.
 * @param[out] name Non-NULL caller-owned receiver of the name pointer.
 *
 * @return `QA_OK` (`*name` written); `QA_ERR_DOMAIN` (invalid `kind` or NULL
 *         `name`). On failure `*name` is left untouched (spec §1).
 *
 * @owner The string is static, immutable and NUL-terminated, owned by the
 *        library and NEVER freed by the caller; it outlives every call. Only
 *        the pointer variable `name` refers to stays owned by the caller.
 * @assumes The returned pointer is stable for the whole process lifetime and
 *          identical across calls with the same `kind`; callers must not
 *          write through it.
 */
QaStatus qaScheduleName(QaScheduleKind kind, const char **name);

#endif /* QA_EVOLUTION_SCHEDULES_H */
