/*
 * 005-evolution time grid tests (task T-009, FR-017, FR-026, EC-018, EC-019).
 *
 * Purpose: assert `qaEvolutionTimeNode` and `qaEvolutionTimeMid` on the exact
 * end node and clamping, on a `T` that is not a multiple of `dt` (`T = 0.1`,
 * `steps = 3`), on extreme `T` (`1e300` and the smallest normal) with
 * `steps = 10^6` (finite, non-decreasing, `t_j <= tm_j <= t_{j+1}`), on a
 * subnormal `T` (`QA_ERR_DOMAIN`), and on every error code with the receiver
 * left untouched.
 *
 * Ownership: no allocation; every receiver is a local. Errors: any failed
 * check is printed with its spec tag and the process exits 1.
 * Numerical assumptions: `T = 0.1, steps = 3` is compared against the same
 * double expressions the spec defines; the extreme-`T` sweeps compare only
 * order and finiteness. Determinism: fixed inputs, no clock, RNG, environment
 * or filesystem access (TST-3).
 */

#include "qa/evolution/evolve.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Sentinel written into receivers to prove the no-write contract. */
#define QA_TIMEGRID_TEST_CANARY 123.456

/**
 * @brief Record one check: print a diagnostic when it does not hold.
 *
 * @param[in] condition Non-zero when the check passed.
 * @param[in] what      Check label printed on failure; non-NULL, NUL-terminated.
 * @return 0 when the check passed, 1 when it failed (the failures it adds).
 * @owner No allocation; `what` stays owned by the caller.
 * @assumes `what` is a string literal or otherwise outlives the call.
 */
static int checkFailed(int condition, const char *what)
{
    if (condition) {
        return 0;
    }
    printf("FAIL: %s\n", what);
    return 1;
}

/**
 * @brief Check the exact end node, the start node and the `T = 0.1`,
 * `steps = 3` grid (EC-018, FR-026).
 *
 * @return Failure count. @owner No allocation.
 * @assumes Expected values use the same double expressions as the contract.
 */
static int runExact(void)
{
    int failures = 0;
    double t = 0.0;
    const double T = 0.1;

    failures += checkFailed(qaEvolutionTimeNode(T, 3u, 3u, &t) == QA_OK && t == T,
                            "TEST-005-evolution-EC018 time_node(T, 3, 3) == T exactly");
    failures += checkFailed(qaEvolutionTimeNode(T, 3u, 0u, &t) == QA_OK && t == 0.0,
                            "TEST-005-evolution-FR026 time_node(.., 0) == 0");
    failures += checkFailed(qaEvolutionTimeNode(7.5, 1u, 1u, &t) == QA_OK && t == 7.5,
                            "TEST-005-evolution-FR026 time_node(T, 1, 1) == T");
    for (size_t j = 0; j <= 3u; ++j) {
        failures += checkFailed(qaEvolutionTimeNode(T, 3u, j, &t) == QA_OK
                                    && t >= 0.0 && t <= T,
                                "TEST-005-evolution-EC018 nodes stay in [0, T]");
    }
    for (size_t j = 0; j < 3u; ++j) {
        /* Bound: state `j < 3`, so `2u * j + 1u <= 5` cannot overflow. */
        double want = T * ((double)(2u * j + 1u) / 6.0);
        failures += checkFailed(qaEvolutionTimeMid(T, 3u, j, &t) == QA_OK
                                    && t == want && t <= T,
                                "TEST-005-evolution-EC018 midpoints match T (2j+1)/(2 steps)");
    }
    failures += checkFailed(qaEvolutionTimeNode(10.0, 4u, 1u, &t) == QA_OK && t == 2.5,
                            "TEST-005-evolution-FR026 time_node(10, 4, 1) == 2.5");
    failures += checkFailed(qaEvolutionTimeMid(10.0, 4u, 1u, &t) == QA_OK && t == 3.75,
                            "TEST-005-evolution-FR026 time_mid(10, 4, 1) == 3.75");
    return failures;
}

/**
 * @brief Sweep the whole grid for one `T` and `steps` and check finiteness,
 * monotonicity and `t_j <= tm_j <= t_{j+1}` (EC-019).
 *
 * @param[in] T     Total time. @param[in] steps Step count.
 * @return Failure count. @owner No allocation.
 * @assumes `steps <= QA_EVOLVE_STEPS_MAX`; the sweep is `O(steps)`.
 */
static int checkSweep(double T, size_t steps)
{
    int failures = 0;
    double prev = 0.0;

    for (size_t j = 0; j < steps; ++j) {
        double lo = -1.0, mid = -1.0, hi = -1.0;
        QaStatus s1 = qaEvolutionTimeNode(T, steps, j, &lo);
        QaStatus s2 = qaEvolutionTimeMid(T, steps, j, &mid);
        /* Bound: `j + 1 <= steps`, so the sum cannot overflow and the node index is valid. */
        QaStatus s3 = qaEvolutionTimeNode(T, steps, j + 1u, &hi);
        int ok = s1 == QA_OK && s2 == QA_OK && s3 == QA_OK
                 && isfinite(lo) && isfinite(mid) && isfinite(hi)
                 && lo >= prev && lo <= mid && mid <= hi && hi <= T;
        if (!ok) {
            failures += checkFailed(0,
                "TEST-005-evolution-EC019 finite, non-decreasing, t_j <= tm_j <= t_{j+1}");
            return failures;
        }
        prev = lo;
    }
    failures += checkFailed(prev >= 0.0, "TEST-005-evolution-EC019 sweep completed");
    return failures;
}

/**
 * @brief Extreme-`T` group (EC-019): `T = 1e300` and the smallest normal with
 * `steps = 10^6`, plus a subnormal `T` giving `QA_ERR_DOMAIN` (FR-017).
 *
 * @return Failure count. @owner No allocation.
 * @assumes `DBL_MIN / 10^6` is a nonzero subnormal, accepted; `DBL_TRUE_MIN`
 *          over `10^6` rounds to 0 and is rejected.
 */
static int runExtreme(void)
{
    int failures = 0;
    double t = QA_TIMEGRID_TEST_CANARY;

    failures += checkSweep(1e300, 1000000u);
    failures += checkSweep(DBL_MIN, 1000000u);
    failures += checkSweep(DBL_MAX, 1000000u);

    failures += checkFailed(qaEvolutionTimeNode(DBL_TRUE_MIN, 1000000u, 1u, &t) == QA_ERR_DOMAIN
                                && t == QA_TIMEGRID_TEST_CANARY,
                            "TEST-005-evolution-EC019 subnormal T, dt = 0 -> DOMAIN (node)");
    failures += checkFailed(qaEvolutionTimeMid(DBL_TRUE_MIN, 1000000u, 0u, &t) == QA_ERR_DOMAIN
                                && t == QA_TIMEGRID_TEST_CANARY,
                            "TEST-005-evolution-EC019 subnormal T, dt = 0 -> DOMAIN (mid)");
    failures += checkFailed(qaEvolutionTimeNode(DBL_TRUE_MIN, 2u, 0u, &t) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-EC019 smallest subnormal T -> DOMAIN");
    return failures;
}

/**
 * @brief Error-code group: the fixed validation order of both functions with
 * the receiver untouched on every failure (FR-017, FR-026).
 *
 * @return Failure count. @owner No allocation.
 * @assumes Each call below is built so only the named step can fire, or an
 *          earlier step wins over a later one as stated.
 */
static int runErrors(void)
{
    int failures = 0;
    double t = QA_TIMEGRID_TEST_CANARY;
    const double c = QA_TIMEGRID_TEST_CANARY;

    /* Step 1 wins over everything. */
    failures += checkFailed(qaEvolutionTimeNode(NAN, 0u, 99u, NULL) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR026 NULL outT -> DOMAIN (node)");
    failures += checkFailed(qaEvolutionTimeMid(NAN, 0u, 99u, NULL) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR026 NULL outT -> DOMAIN (mid)");
    /* Step 2. */
    failures += checkFailed(qaEvolutionTimeNode(NAN, 0u, 0u, &t) == QA_ERR_DOMAIN && t == c,
                            "TEST-005-evolution-FR017 NaN T -> DOMAIN");
    failures += checkFailed(qaEvolutionTimeNode(INFINITY, 4u, 0u, &t) == QA_ERR_DOMAIN && t == c,
                            "TEST-005-evolution-FR017 infinite T -> DOMAIN");
    failures += checkFailed(qaEvolutionTimeMid(0.0, 4u, 0u, &t) == QA_ERR_DOMAIN && t == c,
                            "TEST-005-evolution-FR017 T = 0 -> DOMAIN");
    failures += checkFailed(qaEvolutionTimeMid(-1.0, 4u, 0u, &t) == QA_ERR_DOMAIN && t == c,
                            "TEST-005-evolution-FR017 T < 0 -> DOMAIN");
    /* Step 3 wins over the size steps. */
    failures += checkFailed(qaEvolutionTimeNode(1.0, 0u, 0u, &t) == QA_ERR_DOMAIN && t == c,
                            "TEST-005-evolution-FR017 steps = 0 -> DOMAIN (node)");
    failures += checkFailed(qaEvolutionTimeMid(1.0, 0u, 0u, &t) == QA_ERR_DOMAIN && t == c,
                            "TEST-005-evolution-FR017 steps = 0 -> DOMAIN (mid)");
    /* Step 4 wins over step 5. */
    failures += checkFailed(qaEvolutionTimeNode(1.0, SIZE_MAX, 0u, &t) == QA_ERR_OVERFLOW && t == c,
                            "TEST-005-evolution-FR026 steps = SIZE_MAX -> OVERFLOW (node)");
    failures += checkFailed(qaEvolutionTimeMid(1.0, SIZE_MAX / 2u + 1u, 0u, &t) == QA_ERR_OVERFLOW && t == c,
                            "TEST-005-evolution-FR026 2 steps overflows -> OVERFLOW (mid)");
    failures += checkFailed(qaEvolutionTimeNode(1.0, SIZE_MAX - 1u, 0u, &t) == QA_ERR_RANGE && t == c,
                            "TEST-005-evolution-FR024 steps = SIZE_MAX - 1 -> RANGE (node)");
    failures += checkFailed(qaEvolutionTimeMid(1.0, SIZE_MAX / 2u, 0u, &t) == QA_ERR_RANGE && t == c,
                            "TEST-005-evolution-FR024 steps = SIZE_MAX / 2 -> RANGE (mid)");
    /* Step 5. */
    failures += checkFailed(qaEvolutionTimeNode(1.0, QA_EVOLVE_STEPS_MAX + 1u, 0u, &t) == QA_ERR_RANGE && t == c,
                            "TEST-005-evolution-FR024 steps > STEPS_MAX -> RANGE");
    /* Step 6 wins over step 7. */
    failures += checkFailed(qaEvolutionTimeNode(DBL_TRUE_MIN, 1000000u, 99999999u, &t) == QA_ERR_DOMAIN && t == c,
                            "TEST-005-evolution-FR017 dt = 0 -> DOMAIN before j");
    /* Step 7. */
    failures += checkFailed(qaEvolutionTimeNode(1.0, 5u, 6u, &t) == QA_ERR_RANGE && t == c,
                            "TEST-005-evolution-FR026 j > steps -> RANGE (node)");
    failures += checkFailed(qaEvolutionTimeNode(1.0, 5u, 5u, &t) == QA_OK && t == 1.0,
                            "TEST-005-evolution-FR026 j = steps is accepted (node)");
    t = c;
    failures += checkFailed(qaEvolutionTimeMid(1.0, 5u, 5u, &t) == QA_ERR_RANGE && t == c,
                            "TEST-005-evolution-FR026 j = steps -> RANGE (mid)");
    failures += checkFailed(qaEvolutionTimeMid(1.0, 5u, SIZE_MAX, &t) == QA_ERR_RANGE && t == c,
                            "TEST-005-evolution-FR026 j = SIZE_MAX -> RANGE (mid)");
    return failures;
}

/**
 * @brief Run every group; exit 0 on success, 1 on any failed check.
 *
 * @return Process exit code. @owner No allocation.
 * @assumes Every group is deterministic and independent of argv and time.
 */
int main(void)
{
    int failures = runExact() + runExtreme() + runErrors();

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("OK\n");
    return 0;
}
