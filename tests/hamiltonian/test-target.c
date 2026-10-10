/*
 * 005-evolution H_target tests (task T-006, FR-001, FR-016, FR-027).
 *
 * Purpose: assert `qaHamiltonianTarget` against hand-computed values, against
 * the independent 002 operator `qaHamiltonianApplyProblem` on every basis
 * state (`E(k)` oracle, so `H_target(k) = E(k) - 0.5 q(k)` is rebuilt from
 * two separate implementations), against the reference ground sets (2x2: 4
 * boards at -0.5; 3x3: 8 at -1; 4x4: 2 at -2) and against the fixed
 * validation order, including the untouched receiver on every failure.
 *
 * Ownership: the basis-state buffers are heap-allocated by `checkOracle` with
 * one `calloc` each and released on every path through `goto cleanup`.
 * Errors: any failed check is printed with its spec tag and the process
 * exits 1. Numerical assumptions: every value is a multiple of 0.5, exact in
 * `double`, so comparisons are exact (`==`). Determinism: fixed inputs, no
 * clock, RNG, environment or filesystem access (TST-3).
 */

#include "qa/hamiltonian/problem.h"
#include "qa/hamiltonian/target.h"

#include <complex.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Sentinel written into receivers to prove the no-write contract. */
#define QA_TARGET_TEST_CANARY 123.456

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
 * @brief Check the fixed values of small boards (FR-001).
 *
 * Board ids use the MSB-first row-major mapping: on 2x2, `0b1001` is the main
 * diagonal pair (attacking, `E = 1`, `q = 2`, `H = 0`), `0b1010` is one
 * column pair (`E = 1`).
 *
 * @return Failure count, 0 when every value matches.
 * @owner No allocation. @assumes Exact multiples of 0.5, compared with `==`.
 */
static int runValues(void)
{
    int failures = 0;
    double v = QA_TARGET_TEST_CANARY;

    failures += checkFailed(qaHamiltonianTarget(2u, 0u, &v) == QA_OK && v == 0.0,
                            "TEST-005-evolution-FR001 2x2 empty board is 0");
    failures += checkFailed(qaHamiltonianTarget(2u, 1u, &v) == QA_OK && v == -0.5,
                            "TEST-005-evolution-FR001 2x2 one queen is -0.5");
    failures += checkFailed(qaHamiltonianTarget(2u, 9u, &v) == QA_OK && v == 0.0,
                            "TEST-005-evolution-FR001 2x2 diagonal pair is 1 - 1 = 0");
    failures += checkFailed(qaHamiltonianTarget(2u, 10u, &v) == QA_OK && v == 0.0,
                            "TEST-005-evolution-FR001 2x2 column pair is 1 - 1 = 0");
    failures += checkFailed(qaHamiltonianTarget(2u, 15u, &v) == QA_OK && v == 4.0,
                            "TEST-005-evolution-FR001 2x2 full board is 6 - 2 = 4");
    failures += checkFailed(qaHamiltonianTarget(3u, 0x1FFu, &v) == QA_OK && v == 23.5,
                            "TEST-005-evolution-FR001 3x3 full board is 28 - 4.5");
    return failures;
}

/**
 * @brief Cross-check `H_target(k)` against `qaHamiltonianApplyProblem` on
 * every board of every accepted `n`, and collect the ground sets.
 *
 * One 002 apply of the uniform state gives `E(k) = outPsi[k] / s` for all `k`; the
 * queen count comes from an independent popcount; `H_target(k)` must equal
 * `E(k) - 0.5 q(k)` exactly. The minimum and the number of boards attaining
 * it are compared with the reference ground sets.
 *
 * @param[in] n         Board side, 2..4.
 * @param[in] wantMin   Expected minimum of `E(k)` over all `k`.
 * @param[in] wantCount Expected number of `k` attaining it.
 * @return Failure count, 0 when every board and ground set matches.
 * @owner Allocates two `dim`-sized buffers (`calloc`), released at `cleanup`
 *        on every path. @assumes `dim <= 65536`, so `dim * sizeof` cannot
 *        overflow; `E(k)` is exact in the real part of `complex double`.
 */
static int checkOracle(unsigned int n, double wantMin, size_t wantCount)
{
    int failures = 0;
    /* Bound: `n <= 4`, so `numCells <= 16`; the product and the shift below cannot overflow `size_t`. */
    size_t numCells = (size_t)n * n;
    size_t dim = (size_t)1u << numCells;
    complex double *phi = calloc(dim, sizeof *phi);
    complex double *out = calloc(dim, sizeof *out);
    double minimum = 1e9;
    size_t atMinimum = 0;

    if (phi == NULL || out == NULL) {
        failures += checkFailed(0, "allocation of the basis-state buffers");
        goto cleanup;
    }

    /* One apply of the uniform state `phi[k] = s` gives `out[k] = E(k) * s`
     * for every board at once (a per-basis-state apply would cost
     * `dim^2` work on 4x4). `s = 1 / sqrt(dim)` keeps the norm gate happy. */
    double s = 1.0 / sqrt((double)dim);
    for (size_t k = 0; k < dim; ++k) {
        /* Bound: `k < dim`, and `phi` was allocated with `dim` elements. */
        phi[k] = s;
    }
    if (qaHamiltonianApplyProblem(n, phi, out, dim) != QA_OK) {
        failures += checkFailed(0, "qaHamiltonianApplyProblem accepts the uniform state");
        goto cleanup;
    }

    for (size_t k = 0; k < dim; ++k) {
        unsigned int queens = 0;
        for (size_t bit = 0; bit < numCells; ++bit) {
            /* Bound: `bit < numCells <= 16`, far below the width of `size_t`, so the shift is defined. */
            queens += (unsigned int)((k >> bit) & 1u);
        }
        /* `E(k)` is a small integer, so rounding removes the `s` rescale error. */
        double want = round(creal(out[k]) / s) - 0.5 * (double)queens;
        double got = QA_TARGET_TEST_CANARY;
        if (qaHamiltonianTarget(n, k, &got) != QA_OK || got != want) {
            printf("FAIL: TEST-005-evolution-FR001 n=%u k=%zu got %g want %g\n",
                   n, k, got, want);
            ++failures;
            goto cleanup;
        }
        if (got < minimum) {
            minimum = got;
            atMinimum = 1;
        } else if (got == minimum) {
            ++atMinimum;
        }
    }

    failures += checkFailed(minimum == wantMin,
                            "TEST-005-evolution-FR001 ground energy");
    failures += checkFailed(atMinimum == wantCount,
                            "TEST-005-evolution-FR001 ground set size");

cleanup:
    free(phi);
    free(out);
    return failures;
}

/**
 * @brief Check the validation order of FR-016 and FR-027 and the untouched
 * receiver on every failure.
 *
 * Order: the `N`-gate beats a NULL receiver and an out-of-range `k`; the NULL
 * receiver beats `k >= dim`; `k = dim - 1` is the last accepted id.
 *
 * @return Failure count, 0 when every code and canary matches.
 * @owner No allocation. @assumes `dim = 2^(n*n)` for `n` in `[2, 4]`.
 */
static int runValidation(void)
{
    int failures = 0;
    double v = QA_TARGET_TEST_CANARY;

    failures += checkFailed(qaHamiltonianTarget(5u, 0u, &v) == QA_ERR_UNSUPPORTED,
                            "TEST-005-evolution-FR016 n=5 is UNSUPPORTED");
    failures += checkFailed(qaHamiltonianTarget(0u, 0u, &v) == QA_ERR_RANGE &&
                            qaHamiltonianTarget(1u, 0u, &v) == QA_ERR_RANGE &&
                            qaHamiltonianTarget(6u, 0u, &v) == QA_ERR_RANGE &&
                            qaHamiltonianTarget(UINT_MAX, 0u, &v) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR016 n outside [2,5] is RANGE");
    failures += checkFailed(qaHamiltonianTarget(5u, SIZE_MAX, NULL) == QA_ERR_UNSUPPORTED,
                            "TEST-005-evolution-FR027 gate beats NULL and k");
    failures += checkFailed(qaHamiltonianTarget(7u, SIZE_MAX, NULL) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 gate RANGE beats NULL and k");
    failures += checkFailed(qaHamiltonianTarget(2u, 0u, NULL) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 NULL receiver is DOMAIN");
    failures += checkFailed(qaHamiltonianTarget(2u, SIZE_MAX, NULL) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 NULL beats k >= dim");
    failures += checkFailed(qaHamiltonianTarget(2u, 16u, &v) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 2x2 k = 16 is RANGE");
    failures += checkFailed(qaHamiltonianTarget(3u, 512u, &v) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 3x3 k = 512 is RANGE");
    failures += checkFailed(qaHamiltonianTarget(4u, 65536u, &v) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 4x4 k = 65536 is RANGE");
    failures += checkFailed(qaHamiltonianTarget(4u, SIZE_MAX, &v) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 SIZE_MAX is RANGE");
    failures += checkFailed(v == QA_TARGET_TEST_CANARY,
                            "TEST-005-evolution-FR027 receiver untouched on failure");
    failures += checkFailed(qaHamiltonianTarget(4u, 65535u, &v) == QA_OK && v == 68.0,
                            "TEST-005-evolution-FR027 4x4 full board is 76 - 8");
    return failures;
}

/**
 * @brief Run every target-energy group; exit 0 on success, 1 on any failure.
 *
 * @return Process exit code (0 or 1). @owner No allocation of its own.
 * @assumes The groups are deterministic and independent of argv and time.
 */
int main(void)
{
    int failures = 0;

    failures += runValues();
    failures += checkOracle(2u, -0.5, 4u);
    failures += checkOracle(3u, -1.0, 8u);
    failures += checkOracle(4u, -2.0, 2u);
    failures += runValidation();

    if (failures != 0) {
        printf("005-evolution target: %d failure(s)\n", failures);
        return 1;
    }
    printf("005-evolution target: OK (values, oracle on 2x2/3x3/4x4, ground sets, validation order)\n");
    return 0;
}
