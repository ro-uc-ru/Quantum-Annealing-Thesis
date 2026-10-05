/*
 * 003-schedules unit tests, Phase 3 (tasks T-005 to T-009): one harness with
 * seven groups, `eval`, `boundary`, `properties`, `limits`, `errors`, `names`
 * and `determinism`, selected through one dispatch table.
 *
 * Purpose: assert the specified schedule behavior group by group, each group
 * selectable as `test-003-schedules <group>` so every tasks.md
 * `ctest -R 003-schedules-<group>` pattern matches exactly one CTest entry.
 * Failing checks print the `TEST-003-schedules-FR-xxx` / `-EC-xxx` identifier
 * of the requirement they cover (the CTest name carries the tasks.md pattern,
 * the printed tag carries the spec identifier).
 *
 * Groups: `eval` (FR-001, FR-002, EC-005), `boundary` (FR-003, EC-001,
 * EC-002), `properties` (FR-003..FR-007), `limits` (FR-005, EC-003,
 * EC-004, EC-006, EC-007), `errors` (FR-009..FR-012, EC-006, EC-007),
 * `names` (FR-013, FR-014), `determinism` (FR-008). No arguments runs every
 * group.
 *
 * Ownership: no allocation; nothing to release. Every diagnostic message is
 * built with `snprintf` into a fixed 160-byte local buffer bounded by
 * `sizeof`, so it can never overflow; a message too long is truncated, which
 * only shortens the text of a failure and never changes a verdict. Errors:
 * any failed check is printed with its tag and the process exits 1; an
 * unknown group name or too many arguments exits 2 with usage. Numerical
 * assumptions: expected values are computed here from the closed forms of
 * spec §1 with libm, and compared within `SCHED_TOL` (1e-12) as the spec
 * fixes. The banner uses the clang-only macro `__clang_version__` (STK-1: clang
 * is the project compiler). `QaScheduleKind` casts of out-of-set integers are
 * deliberate (FR-011, FR-014); under clang the enum is `unsigned int`, so a
 * negative cast value wraps to a large one and must be rejected all the same.
 * Determinism: fixed vectors only, no clock, no RNG, no environment or
 * filesystem access.
 */

#include "qa/evolution/schedules.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* SCHED_TOL of spec §1: absolute tolerance of every comparison below. */
#define SCHED_TOL 1e-12

/* Number of families, fixed by FR-002 (kinds are contiguous from 0). */
#define KIND_COUNT 4

/* Receiver sentinel: outside [0, 1], so any write is detectable. */
#define SENTINEL (-7.0)

/* Pi is not guaranteed by C17 <math.h>; fixed here to full double precision. */
static const double kPi = 3.14159265358979323846;

/* Test table row: one named group and its entry point. */
typedef struct QaScheduleTestGroup {
    const char *name;    /* group name as given on the command line */
    int (*run)(void);    /* returns the failure count of the group */
} QaScheduleTestGroup;

/**
 * @brief Record one check; prints the tagged message when it fails.
 *
 * @param condition Non-zero when the check holds.
 * @param tag       Spec identifier printed on failure (e.g. `FR-003`).
 * @param what      Human-readable description of the check.
 * @return 0 when the check holds, 1 when it failed.
 *
 * @owner No allocation; arguments are borrowed for the call.
 * @assumes `tag` and `what` are NUL-terminated strings.
 */
static int check(int condition, const char *tag, const char *what)
{
    if (condition) {
        return 0;
    }
    printf("FAIL: TEST-003-schedules-%s: %s\n", tag, what);
    return 1;
}

/**
 * @brief Poison both receivers with the sentinel before a call.
 *
 * @param[out] outA Receiver of `a`, set to `SENTINEL`.
 * @param[out] outB Receiver of `b`, set to `SENTINEL`.
 *
 * @return Nothing (`void`). No failure path: two plain stores through
 *         pointers the caller guarantees valid.
 *
 * @owner Receivers are caller-owned locals; nothing is allocated.
 * @assumes Both pointers are non-NULL (test-local storage).
 */
static void poison(double *outA, double *outB)
{
    *outA = SENTINEL;
    *outB = SENTINEL;
}

/**
 * @brief Reference value of `a(s)` for one family, from spec §1 closed forms.
 *
 * @param kind Family, one of the four `QaScheduleKind`.
 * @param s    Normalized time in `[0, 1]`.
 * @return Unclamped `a(s)`; NaN for an out-of-set kind (never used by tests).
 *
 * @owner Pure; nothing to release.
 * @assumes Independent of the implementation under test: it re-states the
 *          formulas so the comparison is a real cross-check.
 */
static double refA(QaScheduleKind kind, double s)
{
    switch (kind) {
    case QA_SCHEDULE_LINEAR:
        return s;
    case QA_SCHEDULE_TRIGONOMETRIC: {
        double v = sin(kPi * s / 2.0);
        return v * v;
    }
    case QA_SCHEDULE_QUADRATIC:
        return s * s;
    case QA_SCHEDULE_EXPONENTIAL:
        return pow(2.0, s) - 1.0;
    }
    return NAN;
}

/**
 * @brief Reference value of `b(s)` for one family, from spec §1 closed forms.
 *
 * @param kind Family, one of the four `QaScheduleKind`.
 * @param s    Normalized time in `[0, 1]`.
 * @return Unclamped `b(s)`; NaN for an out-of-set kind (never used by tests).
 *
 * @owner Pure; nothing to release.
 * @assumes Same independence from the implementation as `refA`.
 */
static double refB(QaScheduleKind kind, double s)
{
    switch (kind) {
    case QA_SCHEDULE_LINEAR:
        return 1.0 - s;
    case QA_SCHEDULE_TRIGONOMETRIC: {
        double v = cos(kPi * s / 2.0);
        return v * v;
    }
    case QA_SCHEDULE_QUADRATIC:
        return 1.0 - s * s;
    case QA_SCHEDULE_EXPONENTIAL:
        return 2.0 - pow(2.0, s);
    }
    return NAN;
}

/**
 * @brief Stable label of a family for failure messages.
 *
 * @param kind Family, one of the four `QaScheduleKind`.
 * @return Static string; `"?"` for an out-of-set kind.
 *
 * @owner Returns static storage; never freed.
 * @assumes Labels are for diagnostics only, independent of `qaScheduleName`.
 */
static const char *label(QaScheduleKind kind)
{
    switch (kind) {
    case QA_SCHEDULE_LINEAR:
        return "linear";
    case QA_SCHEDULE_TRIGONOMETRIC:
        return "trigonometric";
    case QA_SCHEDULE_QUADRATIC:
        return "quadratic";
    case QA_SCHEDULE_EXPONENTIAL:
        return "exponential";
    }
    return "?";
}

/**
 * @brief Evaluate one point and compare it with the reference closed forms.
 *
 * Checks `QA_OK`, that both receivers were written and that `a` and `b` equal
 * `refA(s)` / `refB(s)` within `SCHED_TOL`, with `s = t / T`.
 *
 * @param kind Family under test.
 * @param t    Time passed to `qaScheduleEval`, `0 <= t <= T`.
 * @param T    Total time, finite and `> 0`.
 * @param tag  Spec identifier printed on failure.
 * @return Failure count (0..3).
 *
 * @owner Receivers are function-local; nothing to release.
 * @assumes `t` and `T` are valid, so `s = t / T` is in `[0, 1]`.
 */
static int checkPoint(QaScheduleKind kind, double t, double T, const char *tag)
{
    double a;
    double b;
    double s = t / T; /* in [0, 1] because 0 <= t <= T */
    char what[160];
    int failures = 0;

    poison(&a, &b);
    snprintf(what, sizeof what, "%s t=%g T=%g returns QA_OK", label(kind), t, T);
    failures += check(qaScheduleEval(kind, t, T, &a, &b) == QA_OK, tag, what);
    snprintf(what, sizeof what, "%s t=%g T=%g a=%.17g expected %.17g",
             label(kind), t, T, a, refA(kind, s));
    failures += check(fabs(a - refA(kind, s)) <= SCHED_TOL, tag, what);
    snprintf(what, sizeof what, "%s t=%g T=%g b=%.17g expected %.17g",
             label(kind), t, T, b, refB(kind, s));
    failures += check(fabs(b - refB(kind, s)) <= SCHED_TOL, tag, what);
    return failures;
}

/**
 * @brief `eval` group: FR-001, FR-002, EC-005.
 *
 * FR-002: kinds 0..3 are all accepted and evaluate to four pairwise distinct
 * curves at `s = 0.25` (exactly four families, none aliased). FR-001: each
 * family matches its closed form at `s = 0, 0.25, 0.5, 0.75, 1` for several
 * `(t, T)` pairs sharing those `s`. EC-005: at `s = 0.5` the exact values
 * 0.5/0.5, 0.25/0.75, 0.5/0.5 and `sqrt(2) - 1` / `2 - sqrt(2)`.
 *
 * @return Failure count, 0 when every assert of the group holds.
 *
 * @owner No allocation; all receivers are function-local.
 * @assumes `qaScheduleEval` follows the header contract for valid inputs.
 */
static int runEval(void)
{
    static const double sValues[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    static const double totals[] = {1.0, 4.0, 10.0, 123.5};
    /* EC-005 expected (a, b) at s = 0.5, in QaScheduleKind order. */
    const double ec005[KIND_COUNT][2] = {
        {0.5, 0.5},
        {0.5, 0.5},
        {0.25, 0.75},
        {sqrt(2.0) - 1.0, 2.0 - sqrt(2.0)},
    };
    double quarter[KIND_COUNT];
    char what[160];
    int failures = 0;

    for (int k = 0; k < KIND_COUNT; ++k) {
        /* Bound: `k < KIND_COUNT` keeps the cast inside the four families and
         * every `ec005[k]` / `quarter[k]` access inside its array. */
        QaScheduleKind kind = (QaScheduleKind)k;
        double a;
        double b;

        /* FR-001: closed form for every (s, T) pair; t = s * T stays in [0, T]. */
        for (size_t i = 0; i < sizeof sValues / sizeof sValues[0]; ++i) {
            for (size_t j = 0; j < sizeof totals / sizeof totals[0]; ++j) {
                failures += checkPoint(kind, sValues[i] * totals[j],
                                       totals[j], "FR-001");
            }
        }

        /* EC-005: documented values at s = 0.5. */
        poison(&a, &b);
        snprintf(what, sizeof what, "%s s=0.5 returns QA_OK", label(kind));
        failures += check(qaScheduleEval(kind, 5.0, 10.0, &a, &b) == QA_OK,
                          "EC-005", what);
        snprintf(what, sizeof what, "%s s=0.5 a=%.17g expected %.17g",
                 label(kind), a, ec005[k][0]);
        failures += check(fabs(a - ec005[k][0]) <= SCHED_TOL, "EC-005", what);
        snprintf(what, sizeof what, "%s s=0.5 b=%.17g expected %.17g",
                 label(kind), b, ec005[k][1]);
        failures += check(fabs(b - ec005[k][1]) <= SCHED_TOL, "EC-005", what);

        /* FR-002: remember a(0.25) to prove the four curves are distinct;
         * the write is bounded by `k < KIND_COUNT` (loop bound above). */
        poison(&a, &b);
        failures += check(qaScheduleEval(kind, 0.25, 1.0, &a, &b) == QA_OK,
                          "FR-002", "kind in 0..3 is accepted");
        quarter[k] = a;
    }

    /* FR-002: exactly four distinct families (pairwise distinct a(0.25));
     * `i < j < KIND_COUNT` keeps both `quarter[]` reads and both kind casts
     * inside the four families. */
    for (int i = 0; i < KIND_COUNT; ++i) {
        for (int j = i + 1; j < KIND_COUNT; ++j) {
            snprintf(what, sizeof what,
                     "%s and %s differ at s=0.25", label((QaScheduleKind)i),
                     label((QaScheduleKind)j));
            failures += check(fabs(quarter[i] - quarter[j]) > SCHED_TOL,
                              "FR-002", what);
        }
    }
    return failures;
}

/**
 * @brief `boundary` group: FR-003, EC-001, EC-002.
 *
 * For every family and a spread of totals (`T = 1`, `0.1`, `3`, `1e6`, and a
 * non-dyadic `7.3`): EC-001 `t = 0` gives `a = 0`, `b = 1` and `t = -0.0`
 * does the same; EC-002 `t = T` gives `a = 1`, `b = 0` with no `s > 1`
 * rounding (the result stays inside `[0, 1]` exactly); FR-003 holds on both
 * ends within `SCHED_TOL`.
 *
 * @return Failure count, 0 when every assert of the group holds.
 *
 * @owner No allocation; all receivers are function-local.
 * @assumes `qaScheduleEval` follows the header contract for valid inputs.
 */
static int runBoundary(void)
{
    static const double totals[] = {1.0, 0.1, 3.0, 1e6, 7.3};
    char what[160];
    int failures = 0;

    for (int k = 0; k < KIND_COUNT; ++k) {
        /* Bound: `k < KIND_COUNT` (loop bound) keeps the cast inside the four
         * families. */
        QaScheduleKind kind = (QaScheduleKind)k;

        for (size_t j = 0; j < sizeof totals / sizeof totals[0]; ++j) {
            double T = totals[j];
            double a;
            double b;

            /* EC-001 / FR-003: t = 0 gives (0, 1). */
            poison(&a, &b);
            snprintf(what, sizeof what, "%s T=%g t=0 returns QA_OK",
                     label(kind), T);
            failures += check(qaScheduleEval(kind, 0.0, T, &a, &b) == QA_OK,
                              "EC-001", what);
            snprintf(what, sizeof what, "%s T=%g a(0)=%.17g", label(kind), T, a);
            failures += check(fabs(a - 0.0) <= SCHED_TOL, "EC-001", what);
            snprintf(what, sizeof what, "%s T=%g b(0)=%.17g", label(kind), T, b);
            failures += check(fabs(b - 1.0) <= SCHED_TOL, "EC-001", what);

            /* EC-001 / EC-006: t = -0.0 is accepted as 0. */
            poison(&a, &b);
            snprintf(what, sizeof what, "%s T=%g t=-0.0 treated as 0",
                     label(kind), T);
            failures += check(qaScheduleEval(kind, -0.0, T, &a, &b) == QA_OK
                                  && fabs(a) <= SCHED_TOL
                                  && fabs(b - 1.0) <= SCHED_TOL,
                              "EC-001", what);

            /* EC-002 / FR-003: t = T gives (1, 0), exactly inside [0, 1]. */
            poison(&a, &b);
            snprintf(what, sizeof what, "%s T=%g t=T returns QA_OK",
                     label(kind), T);
            failures += check(qaScheduleEval(kind, T, T, &a, &b) == QA_OK,
                              "EC-002", what);
            snprintf(what, sizeof what, "%s T=%g a(T)=%.17g", label(kind), T, a);
            failures += check(fabs(a - 1.0) <= SCHED_TOL, "EC-002", what);
            snprintf(what, sizeof what, "%s T=%g b(T)=%.17g", label(kind), T, b);
            failures += check(fabs(b - 0.0) <= SCHED_TOL, "EC-002", what);
            snprintf(what, sizeof what, "%s T=%g a(T), b(T) inside [0, 1]",
                     label(kind), T);
            failures += check(a >= 0.0 && a <= 1.0 && b >= 0.0 && b <= 1.0,
                              "EC-002", what);

            /* FR-003: the same four boundary values, restated for the FR. */
            failures += check(checkPoint(kind, 0.0, T, "FR-003") == 0
                                  && checkPoint(kind, T, T, "FR-003") == 0,
                              "FR-003", "boundaries match the closed forms");
        }
    }
    return failures;
}

/**
 * @brief `properties` group: FR-003, FR-004, FR-005, FR-006, FR-007.
 *
 * For every family sweeps `t = i * T / 1000`, `i = 0..1000`, with `T = 10`
 * (`i = 1000` gives exactly `t = T`): FR-004 `|a + b - 1| <= SCHED_TOL`;
 * FR-005 exact `0 <= a, b <= 1` with no tolerance; FR-006 `a` non-decreasing
 * and `b` non-increasing between consecutive samples within `SCHED_TOL`;
 * FR-003 endpoints of the sweep; FR-007 `(c*t, c*T)` reproduces `(a, b)`
 * within `SCHED_TOL` for `c` in `{0.5, 2, 3, 1e3, 1e-3}`.
 *
 * @return Failure count, 0 when every assert of the group holds.
 *
 * @owner No allocation; all receivers are function-local.
 * @assumes `qaScheduleEval` follows the header contract for valid inputs;
 *          `c * t <= c * T` holds because rounding is monotonic.
 */
static int runProperties(void)
{
    enum { STEPS = 1000 };
    static const double scales[] = {0.5, 2.0, 3.0, 1e3, 1e-3};
    const double T = 10.0;
    char what[160];
    int failures = 0;

    for (int k = 0; k < KIND_COUNT; ++k) {
        /* Bound: `k < KIND_COUNT` (loop bound) keeps the cast inside the four
         * families. */
        QaScheduleKind kind = (QaScheduleKind)k;
        double prevA = 0.0;
        double prevB = 1.0;

        for (int i = 0; i <= STEPS; ++i) {
            double t = (double)i * T / (double)STEPS; /* i = STEPS gives T */
            double a;
            double b;

            poison(&a, &b);
            snprintf(what, sizeof what, "%s i=%d returns QA_OK", label(kind), i);
            failures += check(qaScheduleEval(kind, t, T, &a, &b) == QA_OK,
                              "FR-004", what);

            snprintf(what, sizeof what, "%s i=%d a+b-1=%.17g", label(kind), i,
                     a + b - 1.0);
            failures += check(fabs(a + b - 1.0) <= SCHED_TOL, "FR-004", what);

            snprintf(what, sizeof what, "%s i=%d a=%.17g b=%.17g outside [0,1]",
                     label(kind), i, a, b);
            failures += check(a >= 0.0 && a <= 1.0 && b >= 0.0 && b <= 1.0,
                              "FR-005", what);

            if (i > 0) {
                snprintf(what, sizeof what, "%s i=%d not monotone", label(kind), i);
                failures += check(a >= prevA - SCHED_TOL
                                      && b <= prevB + SCHED_TOL,
                                  "FR-006", what);
            }
            prevA = a;
            prevB = b;

            /* FR-007: rescaling (t, T) by c leaves (a, b) unchanged. */
            for (size_t j = 0; j < sizeof scales / sizeof scales[0]; ++j) {
                double c = scales[j];
                double a2;
                double b2;

                /* Bound: `c <= 1e3` and `T = 10` give `c * T <= 1e4`, far from
                 * overflow, and monotonic rounding keeps `c * t <= c * T`, so
                 * both arguments stay valid inputs (`0 <= c*t <= c*T`). */
                poison(&a2, &b2);
                snprintf(what, sizeof what, "%s i=%d c=%g differs from c=1",
                         label(kind), i, c);
                failures += check(qaScheduleEval(kind, c * t, c * T, &a2, &b2)
                                          == QA_OK
                                      && fabs(a2 - a) <= SCHED_TOL
                                      && fabs(b2 - b) <= SCHED_TOL,
                                  "FR-007", what);
            }
        }

        /* FR-003: the sweep ends are the boundary values. */
        snprintf(what, sizeof what, "%s sweep ends at (a, b) = (1, 0)", label(kind));
        failures += check(fabs(prevA - 1.0) <= SCHED_TOL
                              && fabs(prevB) <= SCHED_TOL,
                          "FR-003", what);
    }
    return failures;
}

/**
 * @brief Evaluate one extreme point and check finiteness, exact range, sum.
 *
 * Checks `QA_OK`, finite `a` and `b`, exact `0 <= a, b <= 1` (FR-005) and
 * `|a + b - 1| <= SCHED_TOL` (FR-004).
 *
 * @param kind Family under test.
 * @param t    Time, `0 <= t <= T` (may be `-0.0`).
 * @param T    Total time, finite and `> 0` (may be subnormal).
 * @param tag  Spec identifier printed on failure.
 * @return Failure count (0..4).
 *
 * @owner Receivers are function-local; nothing to release.
 * @assumes `t` and `T` are valid inputs.
 */
static int checkExtreme(QaScheduleKind kind, double t, double T, const char *tag)
{
    double a;
    double b;
    char what[160];
    int failures = 0;

    poison(&a, &b);
    snprintf(what, sizeof what, "%s t=%.17g T=%.17g returns QA_OK",
             label(kind), t, T);
    failures += check(qaScheduleEval(kind, t, T, &a, &b) == QA_OK, tag, what);
    snprintf(what, sizeof what, "%s t=%.17g T=%.17g a=%g b=%g not finite",
             label(kind), t, T, a, b);
    failures += check(isfinite(a) && isfinite(b), tag, what);
    snprintf(what, sizeof what, "%s t=%.17g T=%.17g a=%.17g b=%.17g outside [0,1]",
             label(kind), t, T, a, b);
    failures += check(a >= 0.0 && a <= 1.0 && b >= 0.0 && b <= 1.0, "FR-005", what);
    snprintf(what, sizeof what, "%s t=%.17g T=%.17g a+b-1=%.17g",
             label(kind), t, T, a + b - 1.0);
    failures += check(fabs(a + b - 1.0) <= SCHED_TOL, "FR-004", what);
    return failures;
}

/**
 * @brief `limits` group: FR-005, EC-003, EC-004, EC-006, EC-007.
 *
 * EC-003: `T = 1e300` and `T = 1e-300` with `t` in `{0, T/2, nextafter(T, 0),
 * T}`. EC-007: the smallest positive subnormal and another subnormal as `T`,
 * with `t` in `{0, T, nextafter(T, 0)}`. EC-004: `T = 1` with `t = 1e-16` and
 * `t = nextafter(1, 0)`, and the same two neighbours of `s` with `T = 10`.
 * EC-006: `t = -0.0` with `T = 1`. Every case must
 * return `QA_OK`, finite values, exact `[0, 1]` range and `a + b = 1`; the
 * endpoints `t = T` also give `(1, 0)` (FR-003).
 *
 * @return Failure count, 0 when every assert of the group holds.
 *
 * @owner No allocation; all receivers are function-local.
 * @assumes `qaScheduleEval` follows the header contract for valid inputs.
 */
static int runLimits(void)
{
    static const double bigSmall[] = {1e300, 1e-300};
    static const double subnormals[] = {4.9406564584124654e-324, 1e-310};
    char what[160];
    int failures = 0;

    for (int k = 0; k < KIND_COUNT; ++k) {
        /* Bound: `k < KIND_COUNT` (loop bound) keeps the cast inside the four
         * families. */
        QaScheduleKind kind = (QaScheduleKind)k;
        double a;
        double b;

        for (size_t j = 0; j < sizeof bigSmall / sizeof bigSmall[0]; ++j) {
            double T = bigSmall[j];
            failures += checkExtreme(kind, 0.0, T, "EC-003");
            failures += checkExtreme(kind, T / 2.0, T, "EC-003");
            failures += checkExtreme(kind, nextafter(T, 0.0), T, "EC-003");
            failures += checkExtreme(kind, T, T, "EC-003");
            poison(&a, &b);
            snprintf(what, sizeof what, "%s T=%g t=T gives (1, 0)", label(kind), T);
            failures += check(qaScheduleEval(kind, T, T, &a, &b) == QA_OK
                                  && fabs(a - 1.0) <= SCHED_TOL
                                  && fabs(b) <= SCHED_TOL,
                              "EC-003", what);
        }

        for (size_t j = 0; j < sizeof subnormals / sizeof subnormals[0]; ++j) {
            double T = subnormals[j];
            snprintf(what, sizeof what, "T=%g is subnormal", T);
            failures += check(T > 0.0 && T < DBL_MIN, "EC-007", what);
            failures += checkExtreme(kind, 0.0, T, "EC-007");
            failures += checkExtreme(kind, nextafter(T, 0.0), T, "EC-007");
            failures += checkExtreme(kind, T, T, "EC-007");
            poison(&a, &b);
            snprintf(what, sizeof what, "%s T=%g t=T gives (1, 0)", label(kind), T);
            failures += check(qaScheduleEval(kind, T, T, &a, &b) == QA_OK
                                  && fabs(a - 1.0) <= SCHED_TOL
                                  && fabs(b) <= SCHED_TOL,
                              "EC-007", what);
        }

        /* EC-004: neighbours of the ends of [0, 1] for T = 1. */
        failures += checkExtreme(kind, 1e-16, 1.0, "EC-004");
        failures += checkExtreme(kind, nextafter(1.0, 0.0), 1.0, "EC-004");

        /* EC-004 / FR-005: same neighbours of s = 0 and s = 1 with T = 10
         * (t = 1e-16 * T, t = nextafter(T, 0)), so the bound is not tied to
         * the T = 1 case. */
        failures += checkExtreme(kind, 10.0 * 1e-16, 10.0, "EC-004");
        failures += checkExtreme(kind, nextafter(10.0, 0.0), 10.0, "EC-004");

        /* EC-006: -0.0 is accepted and behaves as 0. */
        poison(&a, &b);
        snprintf(what, sizeof what, "%s t=-0.0 gives (0, 1)", label(kind));
        failures += check(qaScheduleEval(kind, -0.0, 1.0, &a, &b) == QA_OK
                              && fabs(a) <= SCHED_TOL
                              && fabs(b - 1.0) <= SCHED_TOL,
                          "EC-006", what);
    }
    return failures;
}

/**
 * @brief Expect `QA_ERR_DOMAIN` and untouched receivers for one bad input.
 *
 * @param kind Family passed to `qaScheduleEval` (may be out of set).
 * @param t    Time passed to `qaScheduleEval`.
 * @param T    Total time passed to `qaScheduleEval`.
 * @param tag  Spec identifier printed on failure.
 * @param what Description of the bad input class.
 * @return Failure count (0..2).
 *
 * @owner Receivers are function-local; nothing to release.
 * @assumes Receivers are valid here; NULL receivers are tested separately.
 */
static int checkReject(QaScheduleKind kind, double t, double T,
                       const char *tag, const char *what)
{
    double a;
    double b;
    char msg[160];
    int failures = 0;

    poison(&a, &b);
    snprintf(msg, sizeof msg, "%s returns QA_ERR_DOMAIN", what);
    failures += check(qaScheduleEval(kind, t, T, &a, &b) == QA_ERR_DOMAIN,
                      tag, msg);
    snprintf(msg, sizeof msg, "%s leaves outputs untouched", what);
    failures += check(a == SENTINEL && b == SENTINEL, tag, msg);
    return failures;
}

/**
 * @brief `errors` group: FR-009, FR-010, FR-011, FR-012, EC-006, EC-007.
 *
 * Every invalid class returns `QA_ERR_DOMAIN` and leaves both sentinel
 * receivers untouched, in all four families: FR-009 `T` NaN, `+-inf`, `0`,
 * `-0.0`, negative and negative subnormal; FR-010 `t` NaN, `-inf`, negative,
 * `-1e-300`, `t > T`, `nextafter(T, inf)` and `+inf`; FR-011 out-of-set kinds
 * (`-1`, `4`, `100`, `INT_MAX`, `INT_MIN`); FR-012 NULL for each receiver and for
 * both. EC-006/EC-007 boundary: `-0.0` as `t` and a positive subnormal `T`
 * are still accepted, so the rejection checks are not over-broad.
 *
 * @return Failure count, 0 when every assert of the group holds.
 *
 * @owner No allocation; all receivers are function-local.
 * @assumes `qaScheduleEval` follows the documented validation order.
 */
static int runErrors(void)
{
    static const double badT[] = {NAN, INFINITY, -INFINITY, 0.0, -0.0, -1.0,
                                  -4.9406564584124654e-324};
    static const int badKinds[] = {-1, KIND_COUNT, 100, INT_MAX, INT_MIN};
    double a;
    double b;
    char what[160];
    int failures = 0;

    for (int k = 0; k < KIND_COUNT; ++k) {
        /* Bound: `k < KIND_COUNT` (loop bound) keeps the cast inside the four
         * families. */
        QaScheduleKind kind = (QaScheduleKind)k;
        const double T = 10.0;
        const double badTimes[] = {NAN, -INFINITY, -1.0, -1e-300, 10.5,
                                 nextafter(T, INFINITY), INFINITY};

        for (size_t j = 0; j < sizeof badT / sizeof badT[0]; ++j) {
            snprintf(what, sizeof what, "%s T=%g", label(kind), badT[j]);
            failures += checkReject(kind, 1.0, badT[j], "FR-009", what);
        }
        for (size_t j = 0; j < sizeof badTimes / sizeof badTimes[0]; ++j) {
            snprintf(what, sizeof what, "%s t=%.17g T=10", label(kind), badTimes[j]);
            failures += checkReject(kind, badTimes[j], T, "FR-010", what);
        }

        /* FR-012: NULL receivers, each alone and both; the other stays put. */
        poison(&a, &b);
        snprintf(what, sizeof what, "%s NULL outA", label(kind));
        failures += check(qaScheduleEval(kind, 1.0, T, NULL, &b) == QA_ERR_DOMAIN
                              && b == SENTINEL,
                          "FR-012", what);
        poison(&a, &b);
        snprintf(what, sizeof what, "%s NULL outB", label(kind));
        failures += check(qaScheduleEval(kind, 1.0, T, &a, NULL) == QA_ERR_DOMAIN
                              && a == SENTINEL,
                          "FR-012", what);
        snprintf(what, sizeof what, "%s NULL both", label(kind));
        failures += check(qaScheduleEval(kind, 1.0, T, NULL, NULL)
                              == QA_ERR_DOMAIN,
                          "FR-012", what);

        /* Not over-broad: -0.0 as t (EC-006) and subnormal T (EC-007) pass. */
        poison(&a, &b);
        snprintf(what, sizeof what, "%s t=-0.0 still accepted", label(kind));
        failures += check(qaScheduleEval(kind, -0.0, T, &a, &b) == QA_OK,
                          "EC-006", what);
        poison(&a, &b);
        snprintf(what, sizeof what, "%s subnormal T still accepted", label(kind));
        failures += check(qaScheduleEval(kind, 0.0, 4.9406564584124654e-324,
                                         &a, &b) == QA_OK,
                          "EC-007", what);
    }

    /* FR-011: out-of-set kinds via an out-of-range cast; the cast is the only
     * way to build such a value in C, and `qaScheduleEval` must reject it
     * before using it (FR-011). */
    for (size_t j = 0; j < sizeof badKinds / sizeof badKinds[0]; ++j) {
        snprintf(what, sizeof what, "kind=%d", badKinds[j]);
        failures += checkReject((QaScheduleKind)badKinds[j], 1.0, 10.0,
                                "FR-011", what);
    }
    return failures;
}

/**
 * @brief `names` group: FR-013, FR-014.
 *
 * FR-013: kinds 0..3 give exactly `linear`, `trigonometric`, `quadratic`,
 * `exponential` with `QA_OK`, and repeated calls return the same pointer.
 * FR-014: out-of-set kinds (`-1`, `4`, `INT_MAX`, `INT_MIN`) and a `NULL`
 * receiver (with a valid and with an invalid kind) return `QA_ERR_DOMAIN`;
 * `*name` keeps its sentinel pointer on every error.
 *
 * @return Failure count, 0 when every assert of the group holds.
 *
 * @owner Returned strings are static library storage, never freed here.
 * @assumes `qaScheduleName` follows the documented validation order.
 */
static int runNames(void)
{
    static const char *const expected[KIND_COUNT] = {
        "linear", "trigonometric", "quadratic", "exponential"};
    static const int badKinds[] = {-1, KIND_COUNT, INT_MAX, INT_MIN};
    static const char sentinelText[] = "sentinel";
    const char *sentinel = sentinelText;
    const char *name;
    const char *again;
    char what[160];
    int failures = 0;

    /* Bound: `k < KIND_COUNT` keeps the kind casts inside the four families
     * and `expected[k]` inside its `KIND_COUNT` entries. */
    for (int k = 0; k < KIND_COUNT; ++k) {
        name = sentinel;
        again = sentinel;
        snprintf(what, sizeof what, "kind %d returns QA_OK", k);
        failures += check(qaScheduleName((QaScheduleKind)k, &name) == QA_OK,
                          "FR-013", what);
        snprintf(what, sizeof what, "kind %d name is \"%s\"", k, expected[k]);
        failures += check(name != sentinel && name != NULL
                              && strcmp(name, expected[k]) == 0,
                          "FR-013", what);
        snprintf(what, sizeof what, "kind %d pointer is stable", k);
        failures += check(qaScheduleName((QaScheduleKind)k, &again) == QA_OK
                              && again == name,
                          "FR-013", what);
    }

    /* FR-014: out-of-set kinds built with an out-of-range cast (the only way
     * to make one in C); `qaScheduleName` must reject each before indexing
     * its table, leaving `*name` untouched. */
    for (size_t j = 0; j < sizeof badKinds / sizeof badKinds[0]; ++j) {
        name = sentinel;
        snprintf(what, sizeof what, "kind=%d is rejected, name untouched",
                 badKinds[j]);
        failures += check(qaScheduleName((QaScheduleKind)badKinds[j], &name)
                                  == QA_ERR_DOMAIN
                              && name == sentinel,
                          "FR-014", what);
    }
    failures += check(qaScheduleName(QA_SCHEDULE_LINEAR, NULL) == QA_ERR_DOMAIN,
                      "FR-014", "NULL name with valid kind is rejected");
    /* Out-of-range cast on purpose: both the kind and the receiver are bad,
     * and the call must still return `QA_ERR_DOMAIN` without writing. */
    failures += check(qaScheduleName((QaScheduleKind)-1, NULL) == QA_ERR_DOMAIN,
                      "FR-014", "NULL name with invalid kind is rejected");
    return failures;
}

/**
 * @brief Evaluate one point and store the raw result bits.
 *
 * @param kind Family under test.
 * @param t    Time, `0 <= t <= T`.
 * @param T    Total time, finite and `> 0`.
 * @param[out] outPair Receives `a` then `b`, caller-owned two-element array.
 * @return `QA_OK` on success, otherwise the failing `QaStatus`.
 *
 * @owner `outPair` is caller-owned; nothing is allocated.
 * @assumes Inputs are valid.
 */
static QaStatus evalPair(QaScheduleKind kind, double t, double T,
                         double outPair[2])
{
    /* Bound: the two writes go to elements 0 and 1, the whole two-element
     * array every caller (`ref[idx]`, `got`) provides. */
    return qaScheduleEval(kind, t, T, &outPair[0], &outPair[1]);
}

/**
 * @brief `determinism` group: FR-008.
 *
 * Evaluates 4 families x 101 points (`T = 7.3`, `t = i * T / 100`) once in
 * forward order into a reference table, then three more times: again forward,
 * in reverse order, and interleaved across families in a strided order. Every
 * `(a, b)` must be bit-identical (`memcmp`) to the reference, so results
 * depend on neither repetition nor call order nor other families' calls.
 *
 * @return Failure count, 0 when every assert of the group holds.
 *
 * @owner The tables are function-local arrays; nothing to release.
 * @assumes `qaScheduleEval` is pure (no state), as FR-008 requires.
 */
static int runDeterminism(void)
{
    enum { POINTS = 101, TOTAL = KIND_COUNT * POINTS };
    const double T = 7.3;
    double ref[TOTAL][2];
    double got[2];
    char what[160];
    int failures = 0;

    /* Bounds for every use of `idx` below: `0 <= idx < TOTAL` makes
     * `idx / POINTS` a family in 0..3 (so the kind cast is in range) and keeps
     * `ref[idx]` inside its `TOTAL` rows. The time `t` of a point is
     * `(idx % POINTS) * T / (POINTS - 1)`; the last point of a family is
     * `(POINTS - 1) * T / (POINTS - 1)`, which rounds back to exactly `T`
     * (`100 * 7.3 / 100 == 7.3` in IEEE double), so `t <= T` always holds and
     * `qaScheduleEval` never rejects a sample with `QA_ERR_DOMAIN`. */
    for (int idx = 0; idx < TOTAL; ++idx) {
        QaScheduleKind kind = (QaScheduleKind)(idx / POINTS);
        double t = (double)(idx % POINTS) * T / (double)(POINTS - 1);
        failures += check(evalPair(kind, t, T, ref[idx]) == QA_OK, "FR-008",
                          "reference evaluation returns QA_OK");
    }

    for (int pass = 0; pass < 3; ++pass) {
        for (int n = 0; n < TOTAL; ++n) {
            int idx;
            /* Bound: each branch keeps `0 <= idx < TOTAL` for `0 <= n < TOTAL`:
             * `n` itself; `TOTAL - 1 - n`; and `(n * 7) % TOTAL`, which is
             * below `TOTAL` by the modulo, cannot overflow (`n * 7 < 3000`)
             * and, because 7 is coprime with `TOTAL` (404 = 4 * 101), visits
             * every index exactly once while interleaving the families. */
            if (pass == 0) {
                idx = n;                          /* repeat, same order */
            } else if (pass == 1) {
                idx = TOTAL - 1 - n;              /* reverse order */
            } else {
                idx = (n * 7) % TOTAL;            /* strided permutation */
            }
            QaScheduleKind kind = (QaScheduleKind)(idx / POINTS);
            double t = (double)(idx % POINTS) * T / (double)(POINTS - 1);
            snprintf(what, sizeof what, "pass %d %s idx=%d bit-identical",
                     pass, label(kind), idx);
            failures += check(evalPair(kind, t, T, got) == QA_OK
                                  && memcmp(got, ref[idx], sizeof got) == 0,
                              "FR-008", what);
        }
    }
    return failures;
}

/* Dispatch table: one entry per group; adding a group means one entry here
 * and one `add_test` in `CMakeLists.txt`. */
static const QaScheduleTestGroup groups[] = {
    {"eval", runEval},
    {"boundary", runBoundary},
    {"properties", runProperties},
    {"limits", runLimits},
    {"errors", runErrors},
    {"names", runNames},
    {"determinism", runDeterminism},
};

/**
 * @brief Print the usage line to stdout.
 *
 * @param prog Program name (`argv[0]`).
 *
 * @return Nothing (`void`). No failure path is reported: the `printf` results
 *         are deliberately ignored, because a usage line that cannot be
 *         written leaves no better channel to report it, and the caller exits
 *         2 regardless.
 *
 * @owner No allocation; `prog` is borrowed.
 * @assumes `prog` is NUL-terminated.
 */
static void printUsage(const char *prog)
{
    printf("usage: %s [", prog);
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; ++i) {
        printf("%s%s", i == 0 ? "" : "|", groups[i].name);
    }
    printf("]\n");
}

/**
 * @brief Run the group named `name`.
 *
 * @param name Group name from the command line.
 * @return Failure count of the group, or -1 when the name is unknown.
 *
 * @owner No allocation; `name` is borrowed.
 * @assumes `name` is NUL-terminated.
 */
static int runGroup(const char *name)
{
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; ++i) {
        if (strcmp(groups[i].name, name) == 0) {
            return groups[i].run();
        }
    }
    return -1;
}

/**
 * @brief Entry point: run one group (argv[1]) or all groups (no argument).
 *
 * @param argc Argument count; 1 runs every group, 2 runs the group in
 *             `argv[1]`, anything else is a usage error (exit 2).
 * @param argv Argument vector; `argv[0]` is the program name and `argv[1]`
 *             is read only when `argc == 2`.
 *
 * @return 0 when every check passed, 1 when any failed, 2 on bad usage.
 *
 * @owner No allocation; `argv` strings are borrowed from the C runtime.
 * @assumes The banner uses the clang-only `__clang_version__` (see the file
 *          header); the remaining assumptions are those of the file header.
 */
int main(int argc, char *argv[])
{
    int failures = 0;

    printf("003-schedules: %s\n", __clang_version__);

    if (argc == 1) {
        for (size_t i = 0; i < sizeof groups / sizeof groups[0]; ++i) {
            failures += groups[i].run();
        }
    } else if (argc == 2) {
        /* Bound: `argv[1]` exists and is NUL-terminated only because
         * `argc == 2` (the C runtime guarantees `argv[argc] == NULL`). */
        int groupFailures = runGroup(argv[1]);
        if (groupFailures < 0) {
            printUsage(argv[0]);
            return 2;
        }
        failures += groupFailures;
    } else {
        printUsage(argv[0]);
        return 2;
    }

    if (failures != 0) {
        printf("003-schedules: FAILED (%d checks)\n", failures);
        return 1;
    }
    printf("003-schedules: OK\n");
    return 0;
}
