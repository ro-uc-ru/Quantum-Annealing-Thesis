/*
 * 003-schedules Phase 2 implementation (tasks T-003, T-004): validation, the
 * four closed forms and the name table.
 *
 * Purpose: validate every input of `qaScheduleEval` in the fixed order
 * `kind`, NULL out-params, `T`, `t` (spec §2, FR-009..FR-012), compute
 * `s = t / T`, evaluate the family's closed form (FR-001, FR-002), clamp both
 * weights to `[0, 1]` (FR-005) and only then write `*outA` and `*outB` once.
 * `qaScheduleName` answers from a static table (FR-013, FR-014).
 *
 * Ownership: no allocation and no mutable global state; every out-param is
 * caller-owned storage. The name table is static, immutable and never freed.
 * Errors: every fallible function returns `QaStatus`; every invalid input is
 * `QA_ERR_DOMAIN` and leaves out-params untouched (spec §1).
 * Numerical assumptions: `T` is finite and `> 0`, and `t` is compared with
 * `>= 0` and `<= T`, so NaN fails both and `-0.0` is accepted as `0`
 * (EC-006). `s = t / T` is correctly rounded IEEE division, so
 * `0 <= t <= T` gives `0 <= s <= 1` and `t = T` gives exactly `s = 1`
 * (EC-002); a subnormal `T` needs no special path (EC-007). The exponential
 * family uses `exp2`, exact at `s = 0` and `s = 1`. Clamping applies to the
 * final weights, never to `s`, so the range is exact (FR-005) while sum and
 * boundaries keep their `1e-12` margin (FR-003, FR-004).
 */

#include "qa/evolution/schedules.h"

#include <math.h>
#include <stddef.h>

/* Number of families; the name table below has exactly this many entries. */
#define QA_SCHEDULE_COUNT 4

/* `M_PI` is not guaranteed under strict C17 (extensions off). */
#define QA_SCHEDULE_PI 3.14159265358979323846

/* Indexed by QaScheduleKind; string literals, immutable, never freed. */
static const char *const qaScheduleNames[QA_SCHEDULE_COUNT] = {
    "linear",
    "trigonometric",
    "quadratic",
    "exponential"
};

/**
 * @brief Tell whether `kind` is one of the four families (FR-011, FR-014).
 *
 * @param[in] kind Value to test; a C enum may hold any integer, so this is
 *                 the only guard between a caller cast and the name table.
 *
 * @return `1` when `0 <= kind < QA_SCHEDULE_COUNT`, otherwise `0`. No
 *         failure path: every input, including a cast from any integer, gets
 *         a definite answer.
 *
 * @owner No allocation, no pointers, nothing to release.
 * @assumes Enumerator values are the contiguous range 0..3 fixed by
 *          `qa/evolution/schedules.h`, so the range test is exact and the
 *          result is a valid index into `qaScheduleNames`. The test runs in
 *          `unsigned int` because the underlying type of the enum is
 *          implementation-defined (unsigned under clang): converting a value
 *          above `INT_MAX` to `int` is implementation-defined, while
 *          converting to `unsigned int` is modular and therefore exact. A
 *          negative cast value becomes a huge unsigned value and fails the
 *          upper bound, so a single comparison covers both ends.
 */
static int qaScheduleKindIsValid(QaScheduleKind kind)
{
    /* Bound: `(unsigned int)kind < 4` accepts exactly 0..3; any other cast
     * integer, negative or large, is rejected before it can index the table. */
    return (unsigned int)kind < (unsigned int)QA_SCHEDULE_COUNT;
}

/**
 * @brief Clamp a computed weight into `[0, 1]` (FR-005).
 *
 * @param[in] x Finite weight computed from a closed form.
 *
 * @return `0.0` if `x < 0`, `1.0` if `x > 1`, otherwise `x` unchanged. No
 *         failure path: a NaN input would be returned unchanged (both
 *         comparisons are false), but callers never pass one (see below).
 *
 * @owner No allocation, nothing to release.
 * @assumes `x` is finite (it comes from finite `s` in `[0, 1]`). Rounding can
 *          push a mathematically exact bound a few ulp outside `[0, 1]`, e.g.
 *          `2.0 - exp2(s)` or `1.0 - s * s` near `s = 1`; clamping the final
 *          value makes the range exact without tolerance and never alters a
 *          value already inside the range.
 */
static double qaScheduleClamp01(double x)
{
    /* Lower bound of the range: rounding must not leave a weight below 0. */
    if (x < 0.0) {
        return 0.0;
    }
    /* Upper bound of the range: rounding must not leave a weight above 1. */
    if (x > 1.0) {
        return 1.0;
    }
    return x;
}

/*
 * `qaScheduleEval` (tasks T-003 and T-004, FR-001..FR-012).
 *
 * Implements the contract declared in `include/qa/evolution/schedules.h`:
 * `kind` first, then the NULL receivers, then `T`, then `t` (spec §2), and no
 * arithmetic before every check has passed, so no error path writes. The
 * header carries the normative §6 documentation; what follows states only
 * the implementation-side numerical facts.
 */
QaStatus qaScheduleEval(QaScheduleKind kind, double t, double T,
                        double *outA, double *outB)
{
    if (!qaScheduleKindIsValid(kind)) {
        return QA_ERR_DOMAIN;
    }
    if (outA == NULL || outB == NULL) {
        return QA_ERR_DOMAIN;
    }
    /* `isfinite` rejects NaN and +-inf; `!(T > 0.0)` also rejects NaN, 0 and
     * negatives, so the division below never sees a zero or non-finite `T`. */
    if (!isfinite(T) || !(T > 0.0)) {
        return QA_ERR_DOMAIN;
    }
    /* `!(t >= 0.0)` rejects NaN and negatives yet accepts `-0.0` (EC-006);
     * `t > T` rejects overshoot and `+inf` because `T` is finite here. */
    if (!(t >= 0.0) || t > T) {
        return QA_ERR_DOMAIN;
    }

    /* Checked arithmetic: `0 <= t <= T` and `T` finite and positive, so the
     * correctly rounded quotient lies in `[0, 1]`, equals exactly `1` when
     * `t = T` (EC-002), and cannot overflow or underflow to NaN, even for
     * `T = 1e300`, `T = 1e-300` or a subnormal `T` (EC-003, EC-007). */
    double s = t / T;
    double a = 0.0;
    double b = 0.0;

    switch (kind) {
    case QA_SCHEDULE_LINEAR:
        a = s;
        b = 1.0 - s;
        break;
    case QA_SCHEDULE_TRIGONOMETRIC: {
        /* `s` is in [0, 1], so the angle `pi * s / 2` lies in [0, pi/2], where
         * `sin` and `cos` are in [0, 1] and their squares too (up to rounding,
         * which the final clamp absorbs). `sin` and `cos` are evaluated
         * separately instead of using `1 - sin^2`, so `b` keeps its own
         * absolute accuracy near `s = 1`. */
        double sn = sin(QA_SCHEDULE_PI * s / 2.0);
        double cs = cos(QA_SCHEDULE_PI * s / 2.0);
        a = sn * sn;
        b = cs * cs;
        break;
    }
    case QA_SCHEDULE_QUADRATIC:
        a = s * s;
        b = 1.0 - s * s;
        break;
    case QA_SCHEDULE_EXPONENTIAL: {
        /* `exp2(s)` is in `[1, 2]`, exact at both ends, so `a` and `b` have
         * absolute error near one ulp of 1 and hit the boundaries exactly. */
        double e = exp2(s);
        a = e - 1.0;
        b = 2.0 - e;
        break;
    }
    default:
        /* Unreachable: `kind` was validated above. Kept so a future enumerator
         * added without a formula fails closed instead of writing garbage. */
        return QA_ERR_DOMAIN;
    }

    /* Single write point: both weights are clamped to the exact range [0, 1]
     * (FR-005) and final before either out-param is touched, and both
     * out-params were proven non-NULL above, so no failure path leaves a
     * partial result (FR-009..FR-012). */
    *outA = qaScheduleClamp01(a);
    *outB = qaScheduleClamp01(b);
    return QA_OK;
}

/*
 * `qaScheduleName` (task T-003, FR-013, FR-014).
 *
 * Implements the contract declared in `include/qa/evolution/schedules.h`:
 * `kind` first, then the NULL receiver, and `*outName` is written only on
 * success. The header carries the normative §6 documentation; what follows
 * states only the implementation-side facts.
 */
QaStatus qaScheduleName(QaScheduleKind kind, const char **outName)
{
    if (!qaScheduleKindIsValid(kind)) {
        return QA_ERR_DOMAIN;
    }
    if (outName == NULL) {
        return QA_ERR_DOMAIN;
    }

    /* Bounded index: `qaScheduleKindIsValid` proved `0 <= kind < 4`, which is
     * the size of the table, so the `unsigned int` conversion is exact and in
     * range. The pointer targets a static literal, so the caller never frees
     * it and it outlives every call. */
    *outName = qaScheduleNames[(unsigned int)kind];
    return QA_OK;
}
