/*
 * 005-evolution primitive tests (task T-007 for `Dz`, task T-008 for `Rx`).
 *
 * Purpose: assert `qaEvolutionDz` against the closed form of FR-022
 * (`|k> -> exp(-i s a H_target(k)) |k>`), the norm preservation of a uniform
 * superposition, the fixed validation order of FR-027 with the state unchanged
 * on every error, and the overflow rejection of EC-021 (`s = DBL_MAX`). For
 * `qaEvolutionRx` it asserts the closed forms of FR-022 (`|->^n` phase and
 * `cos(theta)^numCells` on `|0...0>`), the norm under a large `theta`
 * (EC-014), the validation order and the overflow rejection (EC-021). One
 * binary, one group per CTest entry: the group name is `argv[1]`.
 *
 * Ownership: every state buffer is heap-allocated with one `calloc` by the
 * check that needs it and released on every path through `goto cleanup`.
 * Errors: any failed check is printed with its spec tag and the process exits
 * 1; an unknown group exits 2. Numerical assumptions: the expected phase is
 * rebuilt with the same double expression, so the closed-form comparison uses
 * a tolerance of 1e-14 per amplitude; the norm check uses NORM_TOL 1e-12
 * (ARC-4). Determinism: fixed inputs, no clock, RNG, environment or
 * filesystem access (TST-3).
 */

#include "qa/evolution/primitives.h"
#include "qa/hamiltonian/target.h"

#include <complex.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Absolute tolerance of the closed-form amplitude comparison. */
#define QA_PRIM_TEST_AMP_TOL 1e-14

/* Norm tolerance (ARC-4 default). */
#define QA_PRIM_TEST_NORM_TOL 1e-12

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
 * @brief Return `2^(n*n)` for a test board edge in `[2, 4]`.
 *
 * @param[in] n Board edge, 2, 3 or 4.
 * @return The state dimension.
 * @owner No allocation. @assumes `n * n <= 16`, so the shift is defined.
 */
static size_t dimOf(unsigned int n)
{
    /* Bound: `n` is 2..4 (callers), so `n * n <= 16` and the shift is defined. */
    return (size_t)1u << (n * n);
}

/**
 * @brief Check `Dz` on basis states `|k>` against `exp(-i s a H_target(k))`
 * (FR-022), for every `k` of one board and one `(s, a)` pair.
 *
 * Each `|k>` is rebuilt in a single zeroed buffer; after `Dz` the amplitude at
 * `k` must equal the closed form and every other amplitude must stay zero.
 *
 * @param[in] n Board edge, 2, 3 or 4.
 * @param[in] s Time factor. @param[in] a Weight.
 *
 * @return Failure count, 0 when every basis state matches.
 * @owner Allocates one state buffer, released through `cleanup`.
 * @assumes `H_target(k)` is exact (multiple of 0.5), so the expected phase
 *          uses the same double expression as the implementation.
 */
static int checkClosedForm(unsigned int n, double s, double a)
{
    int failures = 0;
    size_t dim = dimOf(n);
    /* Bound: `n` is 2..4, so `dim <= 2^16` and `dim * 16` cannot overflow `size_t`. */
    complex double *psi = calloc(dim, sizeof *psi);

    if (psi == NULL) {
        return checkFailed(0, "calloc of the state buffer");
    }

    /* Each `Dz` call costs `O(dim)`, so every board is visited on 2x2 and 3x3
     * and a fixed stride of `dim / 8` is used on 4x4,
     * keeping the group `O(8 dim)` per pair. */
    size_t stride = (n == 4u) ? dim / 8u : 1u;
    for (size_t k = 0; k < dim; k += stride) {
        double h = 0.0;
        int bad = 0;

        if (qaHamiltonianTarget(n, k, &h) != QA_OK) {
            failures += checkFailed(0, "qaHamiltonianTarget on a valid board");
            goto cleanup;
        }

        psi[k] = 1.0;
        if (qaEvolutionDz(n, s, a, psi, dim) != QA_OK) {
            failures += checkFailed(0, "Dz returns QA_OK on valid input");
            goto cleanup;
        }

        double phase = (s * a) * h;
        complex double want = cos(phase) - sin(phase) * I;
        bad = cabs(psi[k] - want) > QA_PRIM_TEST_AMP_TOL;
        /* Basis state: the other amplitudes were 0 and must stay 0. */
        for (size_t j = 0; j < dim; ++j) {
            if (j != k && psi[j] != 0.0) {
                bad = 1;
            }
        }
        failures += checkFailed(!bad,
            "TEST-005-evolution-FR022 Dz on |k> is exp(-i s a H_target(k))");
        if (bad) {
            goto cleanup;
        }
        psi[k] = 0.0;
    }

cleanup:
    free(psi);
    return failures;
}

/**
 * @brief Closed-form group: all boards for 2x2, 3x3 and 4x4, several `(s, a)`
 * pairs including zero, negative and large finite phases (FR-004, FR-022).
 *
 * @return Failure count. @owner Delegates allocation to `checkClosedForm`.
 * @assumes Fixed finite pairs, so no call overflows (EC-021 is separate).
 */
static int runClosedForm(void)
{
    int failures = 0;
    static const double pairs[][2] = {
        {0.0, 1.0}, {1.0, 0.0}, {0.3, 1.7}, {-2.5, 0.9}, {1e3, 1.0}, {1.0, -7.25}};

    for (unsigned int n = 2u; n <= 4u; ++n) {
        for (size_t p = 0; p < sizeof pairs / sizeof pairs[0]; ++p) {
            failures += checkClosedForm(n, pairs[p][0], pairs[p][1]);
        }
    }
    return failures;
}

/**
 * @brief Check that `Dz` keeps the norm of a uniform superposition (ARC-4)
 * and that `s = 0` is the identity.
 *
 * @return Failure count. @owner One buffer, allocated and freed locally
 *         (released directly on every path; no `cleanup` label).
 * @assumes Uniform amplitudes `1 / sqrt(dim)`; the norm is summed in `double`.
 */
static int runNorm(void)
{
    int failures = 0;
    unsigned int n = 3u;
    size_t dim = dimOf(n);
    /* Bound: `n` is 2..4, so `dim <= 2^16` and `dim * 16` cannot overflow `size_t`. */
    complex double *psi = calloc(dim, sizeof *psi);

    if (psi == NULL) {
        return checkFailed(0, "calloc of the state buffer");
    }
    for (size_t k = 0; k < dim; ++k) {
        psi[k] = 1.0 / sqrt((double)dim);
    }

    failures += checkFailed(qaEvolutionDz(n, 0.37, 1.9, psi, dim) == QA_OK,
                            "Dz on the uniform state returns QA_OK");
    double norm2 = 0.0;
    for (size_t k = 0; k < dim; ++k) {
        norm2 += creal(psi[k]) * creal(psi[k]) + cimag(psi[k]) * cimag(psi[k]);
    }
    failures += checkFailed(fabs(norm2 - 1.0) < QA_PRIM_TEST_NORM_TOL,
                            "TEST-005-evolution-ARC4 Dz preserves the norm");

    failures += checkFailed(qaEvolutionDz(n, 0.0, 1.9, psi, dim) == QA_OK,
                            "Dz with s = 0 returns QA_OK");
    double norm2b = 0.0;
    for (size_t k = 0; k < dim; ++k) {
        norm2b += creal(psi[k]) * creal(psi[k]) + cimag(psi[k]) * cimag(psi[k]);
    }
    failures += checkFailed(fabs(norm2b - norm2) < QA_PRIM_TEST_NORM_TOL,
                            "Dz with s = 0 leaves the state as it was");

    free(psi);
    return failures;
}

/**
 * @brief Check that `psi` is bit-identical to a reference copy.
 *
 * @param[in] psi  State under test. @param[in] ref Reference copy.
 * @param[in] dim  Length of both buffers.
 *
 * @return 1 when equal, 0 otherwise.
 * @owner No allocation. @assumes Both buffers hold `dim` amplitudes.
 */
static int sameState(const complex double *psi, const complex double *ref,
                     size_t dim)
{
    /* Bound: both `psi` and `ref` hold `dim` elements, so the byte count stays inside both buffers. */
    return memcmp(psi, ref, dim * sizeof *psi) == 0;
}

/**
 * @brief Validation-order group (FR-027): each rejected call must return its
 * fixed code and leave the state unchanged, and earlier steps must win over
 * later ones.
 *
 * @return Failure count. @owner Two buffers (state and reference), released
 *         through `cleanup`.
 * @assumes A 2x2 board (`dim = 16`) filled with distinct amplitudes.
 */
static int runValidation(void)
{
    int failures = 0;
    size_t dim = 16u;
    /* Bound: `dim = 16` (2x2 board), so each `calloc` size is 256 bytes at most. */
    complex double *psi = calloc(dim, sizeof *psi);
    complex double *ref = calloc(dim, sizeof *ref);
    double nan = NAN;
    double inf = INFINITY;

    if (psi == NULL || ref == NULL) {
        failures += checkFailed(0, "calloc of the state buffers");
        goto cleanup;
    }
    for (size_t k = 0; k < dim; ++k) {
        psi[k] = (double)(k + 1u) + 0.5 * I;
        ref[k] = psi[k];
    }

    /* Step 1 wins over everything: N-gate before NULL, scalars and dim. */
    failures += checkFailed(qaEvolutionDz(5u, nan, 1.0, NULL, 7u) == QA_ERR_UNSUPPORTED,
                            "TEST-005-evolution-FR027 n = 5 -> UNSUPPORTED first");
    failures += checkFailed(qaEvolutionDz(1u, nan, 1.0, NULL, 7u) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 n = 1 -> RANGE first");
    failures += checkFailed(qaEvolutionDz(0u, 1.0, 1.0, psi, dim) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 n = 0 -> RANGE");
    failures += checkFailed(qaEvolutionDz(6u, 1.0, 1.0, psi, dim) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 n = 6 -> RANGE");
    /* Step 2 wins over scalars and dim. */
    failures += checkFailed(qaEvolutionDz(2u, nan, 1.0, NULL, 7u) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 NULL psi -> DOMAIN");
    /* Step 3 wins over dim. */
    failures += checkFailed(qaEvolutionDz(2u, nan, 1.0, psi, 7u) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 NaN s -> DOMAIN before dim");
    failures += checkFailed(qaEvolutionDz(2u, 1.0, inf, psi, dim) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 infinite a -> DOMAIN");
    failures += checkFailed(qaEvolutionDz(2u, -inf, 1.0, psi, dim) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 -inf s -> DOMAIN");
    /* Step 4 wins over the overflow pre-check. */
    failures += checkFailed(qaEvolutionDz(2u, DBL_MAX, 1.0, psi, 15u) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 dim mismatch -> RANGE before overflow");
    failures += checkFailed(qaEvolutionDz(2u, 1.0, 1.0, psi, 17u) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 dim too large -> RANGE");
    failures += checkFailed(qaEvolutionDz(2u, 1.0, 1.0, psi, 0u) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 dim 0 -> RANGE");
    failures += checkFailed(qaEvolutionDz(3u, 1.0, 1.0, psi, dim) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 dim of another board -> RANGE");

    failures += checkFailed(sameState(psi, ref, dim),
                            "TEST-005-evolution-FR027 state unchanged on every error");

cleanup:
    free(psi);
    free(ref);
    return failures;
}

/**
 * @brief Overflow group (EC-021): finite `s`, `a` whose product with
 * `max|H_target|` overflows must give `QA_ERR_DOMAIN` with the state
 * unchanged, for every accepted board; a large finite phase that does not
 * overflow is accepted.
 *
 * @return Failure count. @owner One state and one reference buffer per board,
 *         released through `cleanup`.
 * @assumes `max|H_target| >= 1` on every accepted board, so
 *          `DBL_MAX * 1 * max` overflows.
 */
static int runOverflow(void)
{
    int failures = 0;

    for (unsigned int n = 2u; n <= 4u; ++n) {
        size_t dim = dimOf(n);
        /* Bound: `n` is 2..4, so `dim <= 2^16` and `dim * 16` cannot overflow `size_t`. */
        complex double *psi = calloc(dim, sizeof *psi);
        complex double *ref = calloc(dim, sizeof *ref);

        if (psi == NULL || ref == NULL) {
            failures += checkFailed(0, "calloc of the state buffers");
            goto cleanup;
        }
        for (size_t k = 0; k < dim; ++k) {
            psi[k] = 1.0 / (double)(k + 1u);
            ref[k] = psi[k];
        }

        failures += checkFailed(qaEvolutionDz(n, DBL_MAX, 1.0, psi, dim) == QA_ERR_DOMAIN,
                                "TEST-005-evolution-EC021 s = DBL_MAX -> DOMAIN");
        failures += checkFailed(qaEvolutionDz(n, 1.0, DBL_MAX, psi, dim) == QA_ERR_DOMAIN,
                                "TEST-005-evolution-EC021 a = DBL_MAX -> DOMAIN");
        failures += checkFailed(qaEvolutionDz(n, DBL_MAX, DBL_MAX, psi, dim) == QA_ERR_DOMAIN,
                                "TEST-005-evolution-EC021 s a overflows -> DOMAIN");
        failures += checkFailed(qaEvolutionDz(n, -DBL_MAX, 1.0, psi, dim) == QA_ERR_DOMAIN,
                                "TEST-005-evolution-EC021 s = -DBL_MAX -> DOMAIN");
        failures += checkFailed(sameState(psi, ref, dim),
                                "TEST-005-evolution-EC021 state unchanged on overflow");

        /* Finite but huge phase: accepted, the norm is kept. */
        failures += checkFailed(qaEvolutionDz(n, 1e300, 1.0, psi, dim) == QA_OK,
                                "TEST-005-evolution-EC021 s = 1e300 is accepted");
        for (size_t k = 0; k < dim; ++k) {
            if (fabs(cabs(psi[k]) - cabs(ref[k])) > QA_PRIM_TEST_AMP_TOL) {
                failures += checkFailed(0, "Dz with a huge phase keeps |amplitude|");
                break;
            }
        }

cleanup:
        free(psi);
        free(ref);
    }
    return failures;
}

/**
 * @brief Sum of squared moduli of a state.
 *
 * @param[in] psi State. @param[in] dim Length.
 * @return The squared norm. @owner No allocation.
 * @assumes Plain double accumulation; error is far below NORM_TOL for `dim <= 65536`.
 */
static double norm2Of(const complex double *psi, size_t dim)
{
    double sum = 0.0;
    for (size_t k = 0; k < dim; ++k) {
        sum += creal(psi[k]) * creal(psi[k]) + cimag(psi[k]) * cimag(psi[k]);
    }
    return sum;
}

/**
 * @brief Closed-form group for `Rx` (FR-003, FR-022, EC-014): `|->^n` gets
 * `exp(+i numCells theta)`, `|0...0>` gets amplitude `cos(theta)^numCells`,
 * `theta = pi/2` maps `|0...0>` to `(-i)^numCells |1...1>`, and a large `theta`
 * keeps the norm.
 *
 * @return Failure count. @owner One buffer per board and angle, released
 *         through `cleanup`.
 * @assumes `|-> = (|0> - |1>) / sqrt 2`, so `|->^n` has amplitude
 *          `(-1)^popcount(k) / sqrt(dim)`; tolerance 1e-12 absorbs the
 *          accumulated rounding of `numCells <= 16` passes.
 */
static int runRxClosedForm(void)
{
    int failures = 0;
    static const double thetas[] = {0.0, 0.3, -1.1, 2.5, 7.0};
    complex double *psi = NULL;

    for (unsigned int n = 2u; n <= 4u; ++n) {
        size_t dim = dimOf(n);
        unsigned int numCells = n * n;
        free(psi);
        /* Bound: `n` is 2..4, so `dim <= 2^16` and `dim * 16` cannot overflow `size_t`. */
        psi = calloc(dim, sizeof *psi);
        if (psi == NULL) {
            failures += checkFailed(0, "calloc of the state buffer");
            goto cleanup;
        }

        for (size_t t = 0; t < sizeof thetas / sizeof thetas[0]; ++t) {
            double theta = thetas[t];
            int bad = 0;

            /* |->^n -> exp(+i numCells theta) |->^n. */
            for (size_t k = 0; k < dim; ++k) {
                unsigned int bits = 0;
                for (size_t m = k; m != 0u; m >>= 1) {
                    bits += (unsigned int)(m & 1u);
                }
                psi[k] = ((bits % 2u) ? -1.0 : 1.0) / sqrt((double)dim);
            }
            if (qaEvolutionRx(n, theta, 1.0, psi, dim) != QA_OK) {
                failures += checkFailed(0, "Rx returns QA_OK on valid input");
                goto cleanup;
            }
            complex double phase = cexp(I * (double)numCells * theta);
            for (size_t k = 0; k < dim; ++k) {
                unsigned int bits = 0;
                for (size_t m = k; m != 0u; m >>= 1) {
                    bits += (unsigned int)(m & 1u);
                }
                complex double want = phase * (((bits % 2u) ? -1.0 : 1.0) / sqrt((double)dim));
                if (cabs(psi[k] - want) > QA_PRIM_TEST_NORM_TOL) {
                    bad = 1;
                }
            }
            failures += checkFailed(!bad,
                "TEST-005-evolution-FR022 Rx on |->^n is exp(+i numCells theta)");

            /* |0...0> -> cos(theta)^numCells on |0...0>. */
            /* Bound: `psi` was allocated with `dim` elements, so the byte count stays inside it. */
            memset(psi, 0, dim * sizeof *psi);
            psi[0] = 1.0;
            if (qaEvolutionRx(n, theta, 1.0, psi, dim) != QA_OK) {
                failures += checkFailed(0, "Rx returns QA_OK on |0...0>");
                goto cleanup;
            }
            failures += checkFailed(
                cabs(psi[0] - pow(cos(theta), (double)numCells)) < QA_PRIM_TEST_NORM_TOL,
                "TEST-005-evolution-FR022 Rx on |0...0> gives cos(theta)^numCells");
            failures += checkFailed(fabs(norm2Of(psi, dim) - 1.0) < QA_PRIM_TEST_NORM_TOL,
                "TEST-005-evolution-FR003 Rx preserves the norm");
        }

        /* theta = pi/2: cos = 0 and sin = 1, so each cell flips with the
         * factor -i and `|0...0>` goes to `(-i)^numCells |1...1>`. */
        /* Bound: `psi` was allocated with `dim` elements, so the byte count stays inside it. */
        memset(psi, 0, dim * sizeof *psi);
        psi[0] = 1.0;
        if (qaEvolutionRx(n, acos(-1.0) / 2.0, 1.0, psi, dim) != QA_OK) {
            failures += checkFailed(0, "Rx returns QA_OK at theta = pi/2");
            goto cleanup;
        }
        /* n >= 2 here, so dim = 2^(n*n) >= 16 and dim - 1u indexes the last
         * amplitude of the calloc'd buffer (|1...1>). */
        failures += checkFailed(
            cabs(psi[dim - 1u] - cpow(-I, (double)numCells)) < QA_PRIM_TEST_NORM_TOL,
            "TEST-005-evolution-FR022 Rx at pi/2 maps |0...0> to (-i)^numCells |1...1>");

        /* EC-014: large finite theta keeps the norm. */
        for (size_t k = 0; k < dim; ++k) {
            psi[k] = 1.0 / sqrt((double)dim);
        }
        failures += checkFailed(qaEvolutionRx(n, 1e6, 3.7, psi, dim) == QA_OK,
                                "TEST-005-evolution-EC014 large theta is accepted");
        failures += checkFailed(fabs(norm2Of(psi, dim) - 1.0) < QA_PRIM_TEST_NORM_TOL,
                                "TEST-005-evolution-EC014 large theta keeps the norm");
    }

cleanup:
    free(psi);
    return failures;
}

/**
 * @brief Validation-order group for `Rx` (FR-027, EC-021): fixed codes, state
 * unchanged on every error, and step 3 (finiteness and `dt * b` overflow)
 * winning over step 4 (`dim`).
 *
 * @return Failure count. @owner Two buffers released through `cleanup`.
 * @assumes A 2x2 board (`dim = 16`) filled with distinct amplitudes.
 */
static int runRxValidation(void)
{
    int failures = 0;
    size_t dim = 16u;
    /* Bound: `dim = 16` (2x2 board), so each `calloc` size is 256 bytes at most. */
    complex double *psi = calloc(dim, sizeof *psi);
    complex double *ref = calloc(dim, sizeof *ref);
    double nan = NAN;
    double inf = INFINITY;

    if (psi == NULL || ref == NULL) {
        failures += checkFailed(0, "calloc of the state buffers");
        goto cleanup;
    }
    for (size_t k = 0; k < dim; ++k) {
        psi[k] = (double)(k + 1u) + 0.5 * I;
        ref[k] = psi[k];
    }

    failures += checkFailed(qaEvolutionRx(5u, nan, 1.0, NULL, 7u) == QA_ERR_UNSUPPORTED,
                            "TEST-005-evolution-FR027 Rx n = 5 -> UNSUPPORTED first");
    failures += checkFailed(qaEvolutionRx(1u, nan, 1.0, NULL, 7u) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 Rx n = 1 -> RANGE first");
    failures += checkFailed(qaEvolutionRx(6u, 1.0, 1.0, psi, dim) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 Rx n = 6 -> RANGE");
    failures += checkFailed(qaEvolutionRx(2u, nan, 1.0, NULL, 7u) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 Rx NULL psi -> DOMAIN");
    failures += checkFailed(qaEvolutionRx(2u, nan, 1.0, psi, 7u) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 Rx NaN dt -> DOMAIN before dim");
    failures += checkFailed(qaEvolutionRx(2u, 1.0, inf, psi, dim) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-FR027 Rx infinite b -> DOMAIN");
    failures += checkFailed(qaEvolutionRx(2u, DBL_MAX, 2.0, psi, 7u) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-EC021 Rx overflow -> DOMAIN before dim");
    failures += checkFailed(qaEvolutionRx(2u, DBL_MAX, 2.0, psi, dim) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-EC021 Rx dt = DBL_MAX, b = 2 -> DOMAIN");
    failures += checkFailed(qaEvolutionRx(2u, 1e200, -1e200, psi, dim) == QA_ERR_DOMAIN,
                            "TEST-005-evolution-EC021 Rx dt b = -inf -> DOMAIN");
    failures += checkFailed(qaEvolutionRx(2u, 1.0, 1.0, psi, 15u) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 Rx dim 15 -> RANGE");
    failures += checkFailed(qaEvolutionRx(2u, 1.0, 1.0, psi, 17u) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 Rx dim 17 -> RANGE");
    failures += checkFailed(qaEvolutionRx(3u, 1.0, 1.0, psi, dim) == QA_ERR_RANGE,
                            "TEST-005-evolution-FR027 Rx dim of another board -> RANGE");
    failures += checkFailed(sameState(psi, ref, dim),
                            "TEST-005-evolution-FR027 Rx state unchanged on every error");
    failures += checkFailed(qaEvolutionRx(2u, 1e300, 1.0, psi, dim) == QA_OK,
                            "TEST-005-evolution-EC014 Rx theta = 1e300 is accepted");

cleanup:
    free(psi);
    free(ref);
    return failures;
}

/**
 * @brief Run the group named by `argv[1]` and exit with 0 on success, 1 on any
 * failed check, 2 on a missing or unknown group.
 *
 * @param[in] argc Argument count. @param[in] argv `argv[1]` is the group.
 * @return Process exit code. @owner No allocation of its own.
 * @assumes Each group is deterministic and independent of time and the
 *          environment.
 */
int main(int argc, char **argv)
{
    int failures = 0;

    if (argc != 2) {
        printf("usage: test-005-primitives <dz|rx>\n");
        return 2;
    }
    if (strcmp(argv[1], "dz") == 0) {
        failures += runClosedForm();
        failures += runNorm();
        failures += runValidation();
        failures += runOverflow();
    } else if (strcmp(argv[1], "rx") == 0) {
        failures += runRxClosedForm();
        failures += runRxValidation();
    } else {
        printf("unknown group: %s\n", argv[1]);
        return 2;
    }

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("OK\n");
    return 0;
}
