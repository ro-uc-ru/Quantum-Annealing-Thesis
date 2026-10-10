/*
 * 005-evolution driver validation tests (task T-010; FR-016, FR-017, FR-018,
 * FR-020, FR-024, EC-002, EC-007, EC-008, EC-011, EC-015).
 *
 * Purpose: groups `n-gate`, `validation-order`, `bounds` and `initial-state`
 * exercise `qaEvolve` and `qaEvolveWith` up to the end of the FR-020
 * validation: every code, the order in which combined faults resolve, the
 * `STEPS_MAX`, snapshot and overlap bounds, and the initial-state finiteness
 * and norm boundary. Every failing call must leave the output buffer and the
 * summary untouched and call no callback.
 *
 * Task T-011 adds groups `steps1`, `norm`, `snapshots`, `callbacks` and
 * `repeat` for the Strang loop (FR-002, FR-005, FR-006, FR-008..FR-012,
 * FR-028, EC-001, EC-003, EC-005, EC-006): one step against the public
 * primitives bit for bit, the norm bound on 2x2 and 3x3, snapshot counts and
 * rows, callback order and error propagation, and bit-identical repeats.
 *
 * Task T-012 adds groups `alloc`, `schedule` and `perturb` (failure paths
 * through the private ops table; FR-006, FR-019, FR-025, EC-010, EC-012,
 * EC-020): one failure per allocation point, a schedule failure at `tm_j` and
 * at a trace node, and an injected norm or finiteness failure at step `j`;
 * each checks that every acquired buffer was released, that the output and
 * the summary are unchanged and that the failing step reaches no callback.
 *
 * Ownership: buffers are heap-allocated by the test and freed on every path.
 * Errors: any failed check is printed with its spec tag; exit 1.
 * Numerical assumptions: the norm boundary uses `1 +- 0.5e-12` amplitudes on
 * one basis state, far from the rounding noise of the pairwise sum
 * (`~1e-16`). Determinism: fixed inputs, no clock, RNG or filesystem (TST-3).
 */

#include "qa/evolution/evolve.h"
#include "qa/evolution/primitives.h"
#include "evolve-internal.h"

#include <complex.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sentinel written into every output to prove the no-write contract. */
#define QA_EVOLVE_TEST_CANARY 7.25

/* Counts callback invocations to prove none runs on a validation failure. */
static int gCalls;

/**
 * @brief Observer that only counts its calls.
 *
 * @param[in] ctx    Unused. @param[in] sample Unused.
 * @return QA_OK always. @owner No allocation; touches only `gCalls`.
 * @assumes Single-threaded test, so the global counter needs no locking.
 */
static QaStatus countObserver(void *ctx, const QaEvolveSample *sample)
{
    (void)ctx;
    (void)sample;
    ++gCalls;
    return QA_OK;
}

/**
 * @brief Sink that only counts its calls.
 *
 * @param[in] ctx   Unused. @param[in] j Unused. @param[in] dim Unused.
 * @param[in] probs Unused.
 * @return QA_OK always. @owner No allocation; touches only `gCalls`.
 * @assumes Single-threaded test, so the global counter needs no locking.
 */
static QaStatus countSink(void *ctx, size_t j, size_t dim, const double *probs)
{
    (void)ctx;
    (void)j;
    (void)dim;
    (void)probs;
    ++gCalls;
    return QA_OK;
}

/**
 * @brief Schedule hook that fails at once, proving a call got past the
 * validation without running the evolution (which would take seconds).
 *
 * @param[in]  ctx  Unused. @param[in] kind Unused. @param[in] t Unused.
 * @param[in]  T    Unused.
 * @param[out] outA Unused, left untouched. @param[out] outB Unused, untouched.
 * @return QA_ERR_IO always. @owner No allocation.
 * @assumes Only the status matters; no output is ever read after it.
 */
static QaStatus failingSchedule(void *ctx, QaScheduleKind kind, double t, double T,
                                double *outA, double *outB)
{
    (void)ctx;
    (void)kind;
    (void)t;
    (void)T;
    (void)outA;
    (void)outB;
    return QA_ERR_IO;
}

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
 * @brief Test fixture: buffers, parameters and callbacks of one call.
 *
 * @owner `initial` and `out` are owned by the fixture and freed by
 *        `fixtureFree`. @assumes `dim = 2^(n*n)` for the `n` it was built for.
 */
typedef struct Fixture {
    QaEvolveParams params;
    complex double *initial;
    complex double *out;
    size_t dim;
    QaEvolveCallbacks callbacks;
    QaEvolveSummary summary;
} Fixture;

/**
 * @brief Build a valid fixture for board edge `n` (basis state 0, normalized).
 *
 * @param[out] f Fixture to fill. @param[in] n Board edge, `2..4`.
 * @return `0` on success, `1` when allocation fails. @owner The caller frees
 *         with `fixtureFree` on success. @assumes `n` in `2..4`.
 */
static int fixtureInit(Fixture *f, unsigned int n)
{
    memset(f, 0, sizeof *f);
    /* Bound: `n` is in 2..4, so `n * n <= 16` and the shift cannot overflow `size_t`. */
    f -> dim = (size_t)1 << (n * n);
    /* Bound: `dim <= 2^16`, so both allocations are small and the element-size product cannot overflow. */
    f -> initial = calloc(f -> dim, sizeof (complex double));
    f -> out = calloc(f -> dim, sizeof (complex double));
    if (f -> initial == NULL || f -> out == NULL) {
        free(f -> initial);
        free(f -> out);
        return 1;
    }
    /* Bound: `dim >= 16` (`n >= 2`), so index 0 is in range for both buffers. */
    f -> initial[0] = 1.0;
    f -> out[0] = QA_EVOLVE_TEST_CANARY;
    f -> params.n = n;
    f -> params.family = QA_SCHEDULE_LINEAR;
    f -> params.T = 1.0;
    f -> params.steps = 10u;
    f -> params.snapshotInterval = 0u;
    f -> callbacks.observer = countObserver;
    f -> callbacks.sink = countSink;
    f -> summary.finalEnergy = QA_EVOLVE_TEST_CANARY;
    f -> summary.maxNormDrift = QA_EVOLVE_TEST_CANARY;
    return 0;
}

/**
 * @brief Release the fixture buffers.
 *
 * @param[in,out] f Fixture whose `initial` and `out` are freed; non-NULL.
 * @return Nothing. @owner Frees both buffers exactly once.
 * @assumes `f` came from a fixture builder; `free(NULL)` is a no-op, so a
 *          partially built fixture is also safe. The pointers are not reset.
 */
static void fixtureFree(Fixture *f)
{
    free(f -> initial);
    free(f -> out);
}

/**
 * @brief Call `qaEvolve` on the fixture and check code, no callback and the
 * untouched outputs.
 *
 * @param[in] f Fixture. @param[in] want Expected code. @param[in] what Tag.
 * @return Failure count. @owner No allocation.
 * @assumes The fixture outputs still hold the canary values.
 */
static int expectCode(Fixture *f, QaStatus want, const char *what)
{
    int failures = 0;

    gCalls = 0;
    QaStatus got = qaEvolve(&f -> params, f -> initial, f -> out, f -> dim,
                            &f -> callbacks, &f -> summary);
    failures += checkFailed(got == want, what);
    if (want != QA_OK) {
        failures += checkFailed(gCalls == 0, "no callback runs on a failing call");
        failures += checkFailed(f -> out[0] == QA_EVOLVE_TEST_CANARY
                                    && f -> summary.finalEnergy == QA_EVOLVE_TEST_CANARY
                                    && f -> summary.maxNormDrift == QA_EVOLVE_TEST_CANARY,
                                "outputs untouched on failure");
    } else {
        /* A success wrote the outputs: restore the canaries for the next call. */
        /* Bound: `dim >= 16`, so index 0 of `out` is in range. */
        f -> out[0] = QA_EVOLVE_TEST_CANARY;
        f -> summary.finalEnergy = QA_EVOLVE_TEST_CANARY;
        f -> summary.maxNormDrift = QA_EVOLVE_TEST_CANARY;
    }
    return failures;
}

/**
 * @brief Group `n-gate`: FR-016 codes, and the gate precedes size arithmetic.
 *
 * @return Failure count. @owner Frees its fixture.
 * @assumes The fixture for `n = 2` is valid.
 */
static int runNGate(void)
{
    int failures = 0;
    Fixture f;

    if (fixtureInit(&f, 2u) != 0) {
        return 1;
    }
    failures += expectCode(&f, QA_OK, "TEST-005-FR016 n = 2 passes validation");
    f.params.n = 3u;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-FR016 dim mismatch for n = 3");
    f.params.n = 5u;
    failures += expectCode(&f, QA_ERR_UNSUPPORTED, "TEST-005-FR016 n = 5 unsupported");
    f.params.n = 6u;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-FR016 n = 6 range");
    f.params.n = 0u;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-FR016 n = 0 range");
    f.params.n = 1u;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-FR016 n = 1 range");
    f.params.n = UINT_MAX;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-FR016 n = UINT_MAX range");

    /* The gate precedes every other check, including the pointer checks. */
    f.params.n = 5u;
    gCalls = 0;
    failures += checkFailed(qaEvolve(&f.params, NULL, f.out, f.dim, &f.callbacks,
                                     &f.summary) == QA_ERR_UNSUPPORTED,
                            "TEST-005-FR020 gate precedes NULL initial");
    f.params.n = 6u;
    f.params.steps = SIZE_MAX;
    failures += checkFailed(qaEvolve(&f.params, f.initial, f.out, f.dim,
                                     &f.callbacks, &f.summary) == QA_ERR_RANGE,
                            "TEST-005-FR020 gate precedes the steps arithmetic");
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Group `validation-order`: pointers, step 3 codes and combined faults
 * resolving by the FR-020 order, plus the ops table of step 0.
 *
 * @return Failure count. @owner Frees its fixture.
 * @assumes The fixture for `n = 2` is valid.
 */
static int runOrder(void)
{
    int failures = 0;
    Fixture f;

    if (fixtureInit(&f, 2u) != 0) {
        return 1;
    }
    failures += checkFailed(qaEvolve(NULL, f.initial, f.out, f.dim, &f.callbacks,
                                     &f.summary) == QA_ERR_DOMAIN,
                            "TEST-005-FR020 NULL params");
    failures += checkFailed(qaEvolve(&f.params, NULL, f.out, f.dim, &f.callbacks,
                                     &f.summary) == QA_ERR_DOMAIN,
                            "TEST-005-FR020 NULL initial");
    failures += checkFailed(qaEvolve(&f.params, f.initial, NULL, f.dim, &f.callbacks,
                                     &f.summary) == QA_ERR_DOMAIN,
                            "TEST-005-FR020 NULL outFinal");
    failures += checkFailed(qaEvolve(&f.params, f.initial, f.out, f.dim, NULL,
                                     &f.summary) == QA_ERR_DOMAIN,
                            "TEST-005-FR020 NULL callbacks");
    failures += checkFailed(qaEvolve(&f.params, f.initial, f.out, f.dim,
                                     &f.callbacks, NULL) == QA_ERR_DOMAIN,
                            "TEST-005-FR020 NULL outSummary");
    {
        QaEvolveCallbacks none = {NULL, NULL, NULL, NULL};
        failures += checkFailed(qaEvolve(&f.params, f.initial, f.out, f.dim, &none,
                                         &f.summary) == QA_OK,
                                "TEST-005-FR020 both callbacks may be NULL");
        /* Bound: the fixture is a 2x2..4x4 board, so `dim >= 16` and index 0 is valid. */
        f.out[0] = QA_EVOLVE_TEST_CANARY;
        f.summary.finalEnergy = QA_EVOLVE_TEST_CANARY;
        f.summary.maxNormDrift = QA_EVOLVE_TEST_CANARY;
    }

    /* Step 3 codes (FR-017). */
    f.params.steps = 0u;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR017 steps = 0");
    f.params.steps = 10u;
    f.params.T = 0.0;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR017 T = 0");
    f.params.T = -1.0;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR017 T < 0");
    f.params.T = NAN;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR017 T = NaN");
    f.params.T = INFINITY;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR017 T = inf");
    f.params.T = 4.9e-324;
    f.params.steps = 3u;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-EC019 subnormal T gives dt = 0");
    f.params.T = 1.0;
    f.params.steps = 10u;
    f.params.family = (QaScheduleKind)99;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR017 invalid family");
    f.params.family = QA_SCHEDULE_QUADRATIC;
    failures += expectCode(&f, QA_OK, "TEST-005-FR017 every family is valid");
    f.params.family = QA_SCHEDULE_EXPONENTIAL;
    failures += expectCode(&f, QA_OK, "TEST-005-FR017 exponential family is valid");

    /* Combined faults resolve by the order. */
    f.params.family = QA_SCHEDULE_LINEAR;
    f.params.steps = 0u;
    /* Bound: the fixture is a 2x2..4x4 board, so `dim >= 16` and index 0 is valid. */
    f.initial[0] = NAN;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR020 step 3 before step 6");
    f.params.steps = (size_t)QA_EVOLVE_STEPS_MAX + 1u;
    f.params.snapshotInterval = 1u;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-FR020 STEPS_MAX before initial state");
    f.params.steps = 10u;
    f.params.snapshotInterval = 1u;
    f.initial[0] = 1.0;
    failures += expectCode(&f, QA_OK, "TEST-005-FR020 M = 1 with 11 snapshots is accepted");
    f.params.steps = 5u;
    f.params.snapshotInterval = 0u;
    complex double *savedOut = f.out;
    f.out = f.initial;
    failures += checkFailed(qaEvolve(&f.params, f.initial, f.out, f.dim,
                                     &f.callbacks, &f.summary) == QA_ERR_DOMAIN,
                            "TEST-005-FR020 overlap before initial state");
    f.out = savedOut; /* restore so both buffers are freed exactly once */
    fixtureFree(&f);

    /* Step 0: an invalid ops table gives QA_ERR_RANGE. */
    if (fixtureInit(&f, 2u) != 0) {
        return failures + 1;
    }
    {
        QaEvolveOps ops = *qaEvolveDefaultOps();
        failures += checkFailed(qaEvolveWith(&f.params, f.initial, f.out, f.dim,
                                             &f.callbacks, &f.summary, NULL)
                                    == QA_ERR_RANGE,
                                "TEST-005-FR025 NULL ops");
        ops.alloc = NULL;
        failures += checkFailed(qaEvolveWith(&f.params, f.initial, f.out, f.dim,
                                             &f.callbacks, &f.summary, &ops)
                                    == QA_ERR_RANGE,
                                "TEST-005-FR025 NULL alloc hook");
        ops = *qaEvolveDefaultOps();
        ops.release = NULL;
        failures += checkFailed(qaEvolveWith(&f.params, f.initial, f.out, f.dim,
                                             &f.callbacks, &f.summary, &ops)
                                    == QA_ERR_RANGE,
                                "TEST-005-FR025 NULL release hook");
        ops = *qaEvolveDefaultOps();
        ops.schedule = NULL;
        failures += checkFailed(qaEvolveWith(&f.params, f.initial, f.out, f.dim,
                                             &f.callbacks, &f.summary, &ops)
                                    == QA_ERR_RANGE,
                                "TEST-005-FR025 NULL schedule hook");
        ops = *qaEvolveDefaultOps();
        failures += checkFailed(qaEvolveWith(&f.params, f.initial, f.out, f.dim,
                                             &f.callbacks, &f.summary, &ops)
                                    == QA_OK,
                                "TEST-005-FR025 NULL perturb is valid");
    }
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Group `bounds`: `STEPS_MAX`, `steps + 1` overflow, snapshot count and
 * rows, `dim` mismatch and buffer overlap (FR-024, EC-002, EC-011, EC-015).
 *
 * @return Failure count. @owner Frees its fixtures.
 * @assumes Fixtures for `n = 2` and `n = 4` are valid.
 */
static int runBounds(void)
{
    int failures = 0;
    Fixture f;

    if (fixtureInit(&f, 2u) != 0) {
        return 1;
    }
    f.params.steps = QA_EVOLVE_STEPS_MAX;
    {
        /* Accepted by validation: the first schedule call fails with IO. */
        QaEvolveOps ops = *qaEvolveDefaultOps();
        ops.schedule = failingSchedule;
        failures += checkFailed(qaEvolveWith(&f.params, f.initial, f.out, f.dim,
                                             &f.callbacks, &f.summary, &ops)
                                    == QA_ERR_IO,
                                "TEST-005-FR024 steps = STEPS_MAX passes validation");
    }
    f.params.steps = (size_t)QA_EVOLVE_STEPS_MAX + 1u;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-FR024 steps = STEPS_MAX + 1");
    f.params.steps = SIZE_MAX;
    failures += expectCode(&f, QA_ERR_OVERFLOW, "TEST-005-EC002 steps = SIZE_MAX overflows");
    f.params.steps = SIZE_MAX - 1u;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-EC002 steps = SIZE_MAX - 1 is RANGE");

    /* Snapshot count: exactly 10,000 accepted, 10,001 rejected (2x2 rows ok). */
    f.params.T = 1.0;
    f.params.steps = 9999u;
    f.params.snapshotInterval = 1u; /* count = 1 + 9999 = 10000 */
    failures += expectCode(&f, QA_OK, "TEST-005-EC015 count = 10000 accepted");
    f.params.steps = 10000u;
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-EC015 count = 10001 rejected");
    f.params.steps = 10u;
    f.params.snapshotInterval = 3u; /* 0,3,6,9,10 -> 5 */
    failures += expectCode(&f, QA_OK, "TEST-005-FR012 M not dividing steps accepted");
    f.params.snapshotInterval = 100u; /* M > steps: j = 0 and j = steps */
    failures += expectCode(&f, QA_OK, "TEST-005-EC006 M > steps accepted");

    /* dim must equal 2^(n*n). */
    f.params.snapshotInterval = 0u;
    failures += checkFailed(qaEvolve(&f.params, f.initial, f.out, f.dim - 1u,
                                     &f.callbacks, &f.summary) == QA_ERR_RANGE,
                            "TEST-005-FR020 dim - 1 rejected");
    failures += checkFailed(qaEvolve(&f.params, f.initial, f.out, 0u, &f.callbacks,
                                     &f.summary) == QA_ERR_RANGE,
                            "TEST-005-FR020 dim = 0 rejected");
    fixtureFree(&f);

    /* Rows: 4x4 dim = 65536, so 256 snapshots fit and 257 do not (2^24). */
    if (fixtureInit(&f, 4u) != 0) {
        return failures + 1;
    }
    f.params.steps = 255u;
    f.params.snapshotInterval = 1u; /* count = 256, rows = 2^24 */
    failures += expectCode(&f, QA_OK, "TEST-005-EC015 4x4 with 256 snapshots accepted");
    f.params.steps = 256u; /* count = 257, rows > 2^24 */
    failures += expectCode(&f, QA_ERR_RANGE, "TEST-005-EC015 4x4 with 257 snapshots rejected");
    f.params.snapshotInterval = 0u;
    failures += expectCode(&f, QA_OK, "TEST-005-EC015 4x4 with one snapshot accepted");

    /* Overlap by address range: partial overlap is also rejected. */
    {
        /* Bound: `f.dim <= 2^16` (n <= 4), so `2 * dim * 16` bytes cannot
         * overflow `size_t`. */
        complex double *pool = calloc(2u * f.dim, sizeof (complex double));
        if (pool == NULL) {
            fixtureFree(&f);
            return failures + 1;
        }
        /* pool holds 2u * f.dim amplitudes: pool + f.dim is the adjacent
         * second half, while pool + f.dim - 1u and pool + 1u keep the second
         * buffer's f.dim elements inside the pool (last index <= 2 f.dim - 1). */
        pool[0] = 1.0;
        failures += checkFailed(qaEvolve(&f.params, pool, pool + f.dim, f.dim,
                                         &f.callbacks, &f.summary) == QA_OK,
                                "TEST-005-EC011 adjacent buffers accepted");
        failures += checkFailed(qaEvolve(&f.params, pool, pool + f.dim - 1u, f.dim,
                                         &f.callbacks, &f.summary) == QA_ERR_DOMAIN,
                                "TEST-005-EC011 one-element overlap rejected");
        failures += checkFailed(qaEvolve(&f.params, pool + 1u, pool, f.dim,
                                         &f.callbacks, &f.summary) == QA_ERR_DOMAIN,
                                "TEST-005-EC011 reversed overlap rejected");
        failures += checkFailed(qaEvolve(&f.params, pool, pool, f.dim,
                                         &f.callbacks, &f.summary) == QA_ERR_DOMAIN,
                                "TEST-005-EC011 identical buffers rejected");
        free(pool);
    }
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Group `initial-state`: non-finite amplitudes, the `NORM_TOL`
 * boundary and the pairwise norm helper (FR-018, EC-007, EC-008).
 *
 * @return Failure count. @owner Frees its fixture.
 * @assumes `QA_EVOLVE_NORM_TOL = 1e-12`; the test amplitudes sit `0.5e-12`
 *          and `2e-12` from 1, well outside the `1e-16` rounding noise.
 */
static int runInitial(void)
{
    int failures = 0;
    Fixture f;

    if (fixtureInit(&f, 2u) != 0) {
        return 1;
    }
    /* Bound: the fixture is a 2x2..4x4 board, so `dim >= 16` and index 0 is valid. */
    f.initial[0] = NAN;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-EC007 NaN real part");
    f.initial[0] = 1.0 + NAN * I;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-EC007 NaN imaginary part");
    f.initial[0] = INFINITY;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-EC007 infinite amplitude");
    f.initial[0] = 1.0;
    /* Bound: `dim >= 16`, so `dim - 1` cannot underflow and indexes the last element. */
    f.initial[f.dim - 1u] = -INFINITY * I;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-EC007 non-finite last amplitude");
    f.initial[f.dim - 1u] = 0.0;

    f.initial[0] = 1.0 + 0.5e-12;
    failures += expectCode(&f, QA_OK, "TEST-005-EC008 norm just below tolerance accepted");
    f.initial[0] = 1.0 - 0.5e-12;
    failures += expectCode(&f, QA_OK, "TEST-005-EC008 norm just below tolerance (low) accepted");
    f.initial[0] = 1.0 + 2e-12;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-EC008 norm just above tolerance rejected");
    f.initial[0] = 1.0 - 2e-12;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-EC008 norm just above tolerance (low) rejected");
    f.initial[0] = 0.0;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR018 zero state rejected");
    f.initial[0] = 2.0;
    failures += expectCode(&f, QA_ERR_DOMAIN, "TEST-005-FR018 norm 2 rejected");

    /* A spread normalized state (all amplitudes equal) is accepted. */
    for (size_t k = 0; k < f.dim; ++k) {
        f.initial[k] = 1.0 / sqrt((double)f.dim) * (k % 2u == 0u ? 1.0 : I);
    }
    failures += expectCode(&f, QA_OK, "TEST-005-FR018 uniform complex state accepted");

    /* The pairwise helper: values, determinism and domain. */
    {
        double norm = QA_EVOLVE_TEST_CANARY;
        failures += checkFailed(qaEvolveNorm(f.initial, f.dim, &norm) == QA_OK
                                    && fabs(norm - 1.0) < 1e-15,
                                "TEST-005-FR005 uniform state has unit norm");
        norm = QA_EVOLVE_TEST_CANARY;
        failures += checkFailed(qaEvolveNorm(NULL, f.dim, &norm) == QA_ERR_DOMAIN
                                    && norm == QA_EVOLVE_TEST_CANARY,
                                "TEST-005-FR005 NULL psi");
        failures += checkFailed(qaEvolveNorm(f.initial, 0u, &norm) == QA_ERR_DOMAIN
                                    && norm == QA_EVOLVE_TEST_CANARY,
                                "TEST-005-FR005 dim = 0");
        failures += checkFailed(qaEvolveNorm(f.initial, f.dim, NULL) == QA_ERR_DOMAIN,
                                "TEST-005-FR005 NULL outNorm");
        /* Bound: `3 < dim` because `dim >= 16`. */
        f.initial[3] = NAN;
        failures += checkFailed(qaEvolveNorm(f.initial, f.dim, &norm) == QA_OK
                                    && isnan(norm),
                                "TEST-005-FR005 NaN amplitude gives NaN norm");
    }
    fixtureFree(&f);

    /* Non-leaf sizes (dim = 17 and 100) use the same tree and are exact for
     * amplitudes of modulus 1 / sqrt(dim) up to rounding. */
    {
        const size_t sizes[2] = {17u, 100u};
        /* The bound 2u mirrors the two entries of sizes[]. */
        for (size_t s = 0; s < 2u; ++s) {
            /* Bound: `sizes[s]` is 17 or 100, so the byte count is tiny and
             * the writes below stay at `k < sizes[s]`, the allocated length. */
            complex double *psi = calloc(sizes[s], sizeof (complex double));
            double norm = 0.0;
            if (psi == NULL) {
                return failures + 1;
            }
            for (size_t k = 0; k < sizes[s]; ++k) {
                psi[k] = 1.0 / sqrt((double)sizes[s]);
            }
            failures += checkFailed(qaEvolveNorm(psi, sizes[s], &norm) == QA_OK
                                        && fabs(norm - 1.0) < 1e-15,
                                    "TEST-005-FR005 non-power-of-two sizes");
            free(psi);
        }
    }
    return failures;
}

/**
 * @brief One logged callback invocation: who was called and at which step.
 *
 * @owner Plain value stored by value in the `gEvents` array; no resources.
 * @assumes `kind` is 'O' (observer) or 'S' (sink); `j` is the step index
 *          the driver passed to the callback.
 */
typedef struct Event {
    char kind;  /* 'O' observer, 'S' sink */
    size_t j;   /* step index reported by the driver */
} Event;

#define QA_EVOLVE_TEST_EVENTS 64u

/* Callback log, in call order; only the first gEventCount entries are valid. */
static Event gEvents[QA_EVOLVE_TEST_EVENTS];
/* Number of valid entries in gEvents/gNorms/gSinkSum (capped at the capacity). */
static size_t gEventCount;
/* Observer norm per event (0.0 for sink events), indexed like gEvents. */
static double gNorms[QA_EVOLVE_TEST_EVENTS];
/* Sink-received sum of |psi|^2 per event (0.0 for observer events). */
static double gSinkSum[QA_EVOLVE_TEST_EVENTS];
/* Dimension the sink last received; 0 until a sink call happens. */
static size_t gSinkDim;
static size_t gFailObserverAt;  /* observer returns QA_ERR_IO at this `j` */
static size_t gFailSinkAt;      /* sink returns QA_ERR_NOMEM at this `j` */

/**
 * @brief Append one event to the log (dropped past the log capacity).
 *
 * @param[in] kind 'O' for an observer call, 'S' for a sink call.
 * @param[in] j    Sample index of the event.
 * @param[in] norm Norm seen by the observer (0 for a sink event).
 * @param[in] sum  `sum p_k` seen by the sink (0 for an observer event).
 * @return Nothing. @owner No allocation; writes only the global log arrays.
 * @assumes Single-threaded test; the `gEventCount < QA_EVOLVE_TEST_EVENTS`
 *          check bounds every write into the log arrays.
 */
static void logEvent(char kind, size_t j, double norm, double sum)
{
    if (gEventCount < QA_EVOLVE_TEST_EVENTS) {
        gEvents[gEventCount].kind = kind;
        gEvents[gEventCount].j = j;
        gNorms[gEventCount] = norm;
        gSinkSum[gEventCount] = sum;
        ++gEventCount;
    }
}

/**
 * @brief Observer that logs each sample and fails at `gFailObserverAt`.
 *
 * @param[in] ctx    Unused. @param[in] sample Sample to log; non-NULL.
 * @return QA_ERR_IO when `sample -> j == gFailObserverAt`, else QA_OK.
 * @owner No allocation. @assumes Single-threaded test.
 */
static QaStatus logObserver(void *ctx, const QaEvolveSample *sample)
{
    (void)ctx;
    logEvent('O', sample -> j, sample -> norm, 0.0);
    return (sample -> j == gFailObserverAt) ? QA_ERR_IO : QA_OK;
}

/**
 * @brief Sink that logs `sum p_k` and fails at `gFailSinkAt`.
 *
 * @param[in] ctx   Unused. @param[in] j Snapshot sample index.
 * @param[in] dim   Length of `probs`. @param[in] probs `dim` probabilities.
 * @return QA_ERR_NOMEM when `j == gFailSinkAt`, else QA_OK.
 * @owner No allocation; reads `probs` only.
 * @assumes `probs` holds `dim` readable doubles (the driver's buffer); the
 *          test is single-threaded.
 */
static QaStatus logSink(void *ctx, size_t j, size_t dim, const double *probs)
{
    double sum = 0.0;

    (void)ctx;
    for (size_t k = 0; k < dim; ++k) {
        sum += probs[k];
    }
    gSinkDim = dim;
    logEvent('S', j, 0.0, sum);
    return (j == gFailSinkAt) ? QA_ERR_NOMEM : QA_OK;
}

/**
 * @brief Reset the log and the failure triggers.
 *
 * @return Nothing. @owner No allocation; writes only the globals.
 * @assumes Called before each logged run; `SIZE_MAX` means "never fail".
 */
static void resetLog(void)
{
    gEventCount = 0u;
    gSinkDim = 0u;
    gFailObserverAt = SIZE_MAX;
    gFailSinkAt = SIZE_MAX;
}

/**
 * @brief Fill `psi` with the normalized `|->^numCells` state (FR-009).
 *
 * @param[out] psi `dim` amplitudes. @param[in] dim `2^numCells`.
 * @return Nothing. @owner No allocation. @assumes `dim` is a power of two.
 */
static void fillMinus(complex double *psi, size_t dim)
{
    for (size_t k = 0; k < dim; ++k) {
        size_t bits = 0u;
        for (size_t v = k; v != 0u; v >>= 1) {
            bits += v & 1u;
        }
        psi[k] = ((bits % 2u == 0u) ? 1.0 : -1.0) / sqrt((double)dim);
    }
}

/**
 * @brief Run `qaEvolve` with the logging callbacks on a `|->` start.
 *
 * @param[in,out] f Fixture (its `initial` is overwritten with `|->`).
 * @return The status of `qaEvolve`. @owner No allocation.
 * @assumes The fixture is valid for `f -> params`.
 */
static QaStatus runLogged(Fixture *f)
{
    fillMinus(f -> initial, f -> dim);
    f -> callbacks.observer = logObserver;
    f -> callbacks.sink = logSink;
    return qaEvolve(&f -> params, f -> initial, f -> out, f -> dim, &f -> callbacks,
                    &f -> summary);
}

/**
 * @brief Group `steps1`: one Strang step equals `Dz Rx Dz` of the public
 * primitives at `tm_0 = T/2`, bit for bit, with rows `j = 0` and `j = 1`
 * (EC-001, FR-002, FR-008; the table-driven loop `Dz` equals the public one).
 *
 * @return Failure count. @owner Frees its fixture and reference.
 * @assumes `T = 0.7`, linear schedule, 2x2.
 */
static int runSteps1(void)
{
    int failures = 0;
    Fixture f;
    complex double ref[16];
    double a = 0.0;
    double b = 0.0;

    if (fixtureInit(&f, 2u) != 0) {
        return 1;
    }
    f.params.steps = 1u;
    f.params.T = 0.7;
    resetLog();
    failures += checkFailed(runLogged(&f) == QA_OK, "TEST-005-EC001 steps = 1 runs");
    /* gEventCount == 3u is evaluated first (short-circuit), so indices 0..2 are logged entries. */
    failures += checkFailed(gEventCount == 3u && gEvents[0].kind == 'O'
                                && gEvents[0].j == 0u && gEvents[1].kind == 'O'
                                && gEvents[1].j == 1u && gEvents[2].kind == 'S'
                                && gEvents[2].j == 1u,
                            "TEST-005-EC001 rows j = 0 and j = 1, sink at j = steps");

    /* Bound: 16 is `dim` for 2x2, so `ref[16]` and `f.out` hold the same number of elements and `sizeof ref` matches. */
    fillMinus(ref, 16u);
    failures += checkFailed(qaScheduleEval(QA_SCHEDULE_LINEAR, 0.35, 0.7, &a, &b) == QA_OK
                                && qaEvolutionDz(2u, 0.35, a, ref, 16u) == QA_OK
                                && qaEvolutionRx(2u, 0.7, b, ref, 16u) == QA_OK
                                && qaEvolutionDz(2u, 0.35, a, ref, 16u) == QA_OK,
                            "TEST-005-EC001 reference step with the public primitives");
    failures += checkFailed(memcmp(ref, f.out, sizeof ref) == 0,
                            "TEST-005-FR002 loop step is bit-identical to Dz Rx Dz");
    failures += checkFailed(f.summary.maxNormDrift <= QA_EVOLVE_NORM_TOL
                                && isfinite(f.summary.finalEnergy),
                            "TEST-005-FR010 summary written");
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Run one evolution and check the norm bound and the delivered norms.
 *
 * @param[in] n Board edge. @param[in] steps Steps. @param[in] T Total time.
 * @param[in] what Tag for failures.
 * @return Failure count. @owner Frees its fixture.
 * @assumes The case is inside the verified drift regime (FR-005).
 */
static int checkNormCase(unsigned int n, size_t steps, double T, const char *what)
{
    int failures = 0;
    Fixture f;
    double norm = 0.0;

    if (fixtureInit(&f, n) != 0) {
        return 1;
    }
    f.params.steps = steps;
    f.params.T = T;
    f.params.snapshotInterval = 0u;
    resetLog();
    failures += checkFailed(runLogged(&f) == QA_OK, what);
    failures += checkFailed(qaEvolveNorm(f.out, f.dim, &norm) == QA_OK
                                && fabs(norm - 1.0) <= QA_EVOLVE_NORM_TOL,
                            "TEST-005-FR005 final norm within NORM_TOL");
    failures += checkFailed(f.summary.maxNormDrift <= QA_EVOLVE_NORM_TOL,
                            "TEST-005-FR005 max drift within NORM_TOL");
    failures += checkFailed(fabs(norm - 1.0) <= f.summary.maxNormDrift + 1e-15,
                            "TEST-005-FR010 max drift covers the final drift");
    failures += checkFailed(fabs(f.summary.finalEnergy) <= 120.0,
                            "TEST-005-FR010 final energy bounded by 120");
    {
        size_t observers = 0u;
        int ok = 1;
        for (size_t e = 0; e < gEventCount; ++e) {
            if (gEvents[e].kind == 'O') {
                ok = ok && gEvents[e].j == observers
                     && fabs(gNorms[e] - 1.0) <= QA_EVOLVE_NORM_TOL;
                ++observers;
            }
        }
        /* The log holds 64 events only: steps + 1 observers are checked up to
         * that capacity. */
        failures += checkFailed(ok && (observers == steps + 1u
                                       || gEventCount == QA_EVOLVE_TEST_EVENTS),
                                "TEST-005-FR011 observer order and norms");
    }
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Group `norm`: 2x2 and 3x3 norm bound.
 *
 * @return Failure count, 0 when `|norm - 1| <= NORM_TOL` at every sample.
 * @owner Frees its fixtures on every path.
 * @assumes The runs are deterministic; the bound is ARC-4's default 1e-12.
 */
static int runNorm(void)
{
    int failures = 0;

    failures += checkNormCase(2u, 1000u, 10.0, "TEST-005-FR005 2x2 steps = 1000");
    failures += checkNormCase(3u, 100u, 5.0, "TEST-005-FR005 3x3 steps = 100");
    return failures;
}

/**
 * @brief Group `snapshots`: counts for `M = 0`, `M > steps`, `M = 1`, `M`
 * dividing and not dividing `steps`, 16 rows on 2x2 and `sum p_k = 1`
 * (FR-012, EC-003, EC-005, EC-006).
 *
 * @return Failure count. @owner Frees its fixture.
 * @assumes `steps = 10`, 2x2, `T = 1`.
 */
static int runSnapshots(void)
{
    static const size_t intervals[5] = {0u, 1u, 3u, 10u, 100u};
    static const size_t expected[5] = {1u, 11u, 5u, 2u, 2u};
    int failures = 0;
    Fixture f;

    if (fixtureInit(&f, 2u) != 0) {
        return 1;
    }
    f.params.steps = 10u;
    /* The bound 5u mirrors the number of cases this loop is written for. */
    for (size_t c = 0; c < 5u; ++c) {
        size_t sinks = 0u;
        size_t prev = 0u;
        int ok = 1;

        f.params.snapshotInterval = intervals[c];
        resetLog();
        failures += checkFailed(runLogged(&f) == QA_OK, "TEST-005-FR012 run succeeds");
        for (size_t e = 0; e < gEventCount; ++e) {
            if (gEvents[e].kind != 'S') {
                continue;
            }
            size_t j = gEvents[e].j;
            ok = ok && (sinks == 0u || j > prev)
                 && (j == 10u || (intervals[c] != 0u && j % intervals[c] == 0u))
                 && fabs(gSinkSum[e] - 1.0) <= 2.0 * QA_EVOLVE_NORM_TOL + 1e-15;
            prev = j;
            ++sinks;
        }
        failures += checkFailed(sinks == expected[c] && ok && prev == 10u,
                                "TEST-005-FR012 snapshot count, steps and probability sum");
        failures += checkFailed(gSinkDim == 16u,
                                "TEST-005-EC003 every snapshot has 16 rows");
    }
    /* M = 0 and M > steps: the first snapshot differs (EC-005): j = 0 only for M > 0. */
    f.params.snapshotInterval = 100u;
    resetLog();
    /* gEvents has QA_EVOLVE_TEST_EVENTS (64) slots, so index 1 is in range; a QA_OK run logs the observer then the sink. */
    failures += checkFailed(runLogged(&f) == QA_OK && gEvents[1].kind == 'S'
                                && gEvents[1].j == 0u,
                            "TEST-005-EC005 M > steps writes j = 0 first, observer first");
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Group `callbacks`: order (observer before sink at one `j`), error
 * propagation from each callback, no further call after a failure, and the
 * output unchanged on failure (FR-028, FR-019).
 *
 * @return Failure count. @owner Frees its fixture.
 * @assumes 2x2, `steps = 3`, `M = 1`.
 */
static int runCallbacks(void)
{
    int failures = 0;
    Fixture f;

    if (fixtureInit(&f, 2u) != 0) {
        return 1;
    }
    f.params.steps = 3u;
    f.params.snapshotInterval = 1u;
    resetLog();
    failures += checkFailed(runLogged(&f) == QA_OK && gEventCount == 8u,
                            "TEST-005-FR028 eight events for steps = 3, M = 1");
    for (size_t e = 0; e < gEventCount && e < 8u; ++e) {
        failures += checkFailed(gEvents[e].kind == ((e % 2u == 0u) ? 'O' : 'S')
                                    && gEvents[e].j == e / 2u,
                                "TEST-005-FR028 order O0 S0 O1 S1 O2 S2 O3 S3");
    }

    /* Bound: `dim >= 16`, so index 0 of `out` is in range. */
    f.out[0] = QA_EVOLVE_TEST_CANARY;
    f.summary.finalEnergy = QA_EVOLVE_TEST_CANARY;
    f.summary.maxNormDrift = QA_EVOLVE_TEST_CANARY;
    resetLog();
    gFailObserverAt = 2u;
    /* gEventCount == 5u is tested first, so index 4 is a logged entry. */
    failures += checkFailed(runLogged(&f) == QA_ERR_IO && gEventCount == 5u
                                && gEvents[4].kind == 'O' && gEvents[4].j == 2u,
                            "TEST-005-FR028 observer error propagated, run stops");
    failures += checkFailed(f.out[0] == QA_EVOLVE_TEST_CANARY
                                && f.summary.finalEnergy == QA_EVOLVE_TEST_CANARY
                                && f.summary.maxNormDrift == QA_EVOLVE_TEST_CANARY,
                            "TEST-005-FR019 outputs unchanged after observer error");
    resetLog();
    gFailSinkAt = 1u;
    /* gEventCount == 4u is tested first, so index 3 is a logged entry. */
    failures += checkFailed(runLogged(&f) == QA_ERR_NOMEM && gEventCount == 4u
                                && gEvents[3].kind == 'S' && gEvents[3].j == 1u,
                            "TEST-005-FR028 sink code propagated unchanged, run stops");
    failures += checkFailed(f.out[0] == QA_EVOLVE_TEST_CANARY
                                && f.summary.finalEnergy == QA_EVOLVE_TEST_CANARY,
                            "TEST-005-FR019 outputs unchanged after sink error");
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Group `repeat`: two runs with equal inputs give bit-identical states,
 * summaries and delivered norms (FR-008, TST-3).
 *
 * @return Failure count. @owner Frees its fixture and the copies.
 * @assumes 3x3, `steps = 50`, trigonometric schedule.
 */
static int runRepeat(void)
{
    int failures = 0;
    Fixture f;
    complex double *first = NULL;
    QaEvolveSummary firstSummary;
    double firstNorms[QA_EVOLVE_TEST_EVENTS];
    size_t firstCount;

    if (fixtureInit(&f, 3u) != 0) {
        return 1;
    }
    /* Bound: `first` holds `f.dim` elements, the same length as `f.out`, so the memcpy/memset/memcmp below stay inside both buffers. */
    first = malloc(f.dim * sizeof (complex double));
    if (first == NULL) {
        fixtureFree(&f);
        return 1;
    }
    f.params.steps = 50u;
    f.params.T = 20.0;
    f.params.family = QA_SCHEDULE_TRIGONOMETRIC;
    f.params.snapshotInterval = 10u;
    resetLog();
    failures += checkFailed(runLogged(&f) == QA_OK, "TEST-005-FR008 first run");
    memcpy(first, f.out, f.dim * sizeof (complex double));
    firstSummary = f.summary;
    firstCount = gEventCount;
    /* Both arrays hold QA_EVOLVE_TEST_EVENTS doubles, so the copy is exact. */
    memcpy(firstNorms, gNorms, sizeof firstNorms);

    memset(f.out, 0, f.dim * sizeof (complex double));
    resetLog();
    failures += checkFailed(runLogged(&f) == QA_OK, "TEST-005-FR008 second run");
    failures += checkFailed(memcmp(first, f.out, f.dim * sizeof (complex double)) == 0
                                && memcmp(&firstSummary, &f.summary, sizeof f.summary) == 0
                                && gEventCount == firstCount
                                && memcmp(firstNorms, gNorms, sizeof firstNorms) == 0,
                            "TEST-005-FR008 repeats are bit-identical");
    free(first);
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Fault-injection context of the ops table (FR-025).
 *
 * Counts hook calls from 1 and fails the one whose index equals the matching
 * `fail*` field (`0` means never). `live` is the number of buffers handed out
 * by `alloc` and not yet returned to `release`: it must be 0 after any call.
 *
 * @owner Stack object of the test; the driver never frees it.
 * @assumes Single-threaded test; `alloc` uses `calloc` and `release` `free`.
 */
typedef struct Faults {
    size_t allocCalls;
    size_t failAlloc;       /* alloc call index that returns NULL */
    long live;              /* buffers acquired minus buffers released */
    size_t scheduleCalls;
    size_t failSchedule;    /* schedule call index that returns QA_ERR_IO */
    size_t perturbStep;     /* step `j` at which `perturb` acts (0 = never) */
    int perturbMode;        /* 0 scale psi[0], 1 NaN, 2 return QA_ERR_RANGE */
} Faults;

/**
 * @brief Counting `calloc` that fails at the `failAlloc`-th call.
 *
 * @param[in,out] ctx   `Faults *`, non-NULL; counters are updated.
 * @param[in]     count Element count. @param[in] size Element size.
 * @return Zeroed block, or NULL on the injected failure or a real one.
 * @owner The block is released by `faultRelease`.
 * @assumes `count * size` is the driver's validated size, so `calloc` itself
 *          checks any overflow.
 */
static void *faultAlloc(void *ctx, size_t count, size_t size)
{
    Faults *fl = ctx;

    ++fl -> allocCalls;
    if (fl -> allocCalls == fl -> failAlloc) {
        return NULL;
    }
    void *ptr = calloc(count, size);
    if (ptr != NULL) {
        ++fl -> live;
    }
    return ptr;
}

/**
 * @brief Counting `free`, the release half of the fault seam.
 *
 * @param[in,out] ctx `Faults *`, non-NULL; `live` is decremented.
 * @param[in]     ptr Block from `faultAlloc`, or NULL (ignored).
 * @return Nothing. @owner Takes ownership of `ptr` and frees it.
 * @assumes Every non-NULL `ptr` came from `faultAlloc`, so `live` balances.
 */
static void faultRelease(void *ctx, void *ptr)
{
    Faults *fl = ctx;

    if (ptr != NULL) {
        --fl -> live;
    }
    free(ptr);
}

/**
 * @brief `qaScheduleEval` wrapper that fails at the `failSchedule`-th call.
 *
 * @param[in,out] ctx  `Faults *`, non-NULL; `scheduleCalls` is incremented.
 * @param[in]     kind Schedule family. @param[in] t Time. @param[in] T Total.
 * @param[out]    outA Receives `a(t)`. @param[out] outB Receives `b(t)`.
 * @return QA_ERR_IO on the injected call, else the 003 code.
 * @owner Receivers stay caller-owned; untouched on the injected failure.
 * @assumes Arguments are the driver's validated schedule inputs.
 */
static QaStatus faultSchedule(void *ctx, QaScheduleKind kind, double t, double T,
                              double *outA, double *outB)
{
    Faults *fl = ctx;

    ++fl -> scheduleCalls;
    if (fl -> scheduleCalls == fl -> failSchedule) {
        return QA_ERR_IO;
    }
    return qaScheduleEval(kind, t, T, outA, outB);
}

/**
 * @brief Perturb hook: breaks the norm (scale), the finiteness (NaN) or fails
 * with QA_ERR_RANGE at step `perturbStep`.
 *
 * @param[in]     ctx  `Faults *`, non-NULL. @param[in] j Current step.
 * @param[in,out] psi  Work state of `dim >= 1` amplitudes.
 * @param[in]     dim  Length of `psi`; unused beyond that guarantee.
 * @return QA_OK, or QA_ERR_RANGE in mode 2 at the chosen step.
 * @owner Rewrites `psi`, which belongs to the driver's work buffer.
 * @assumes `dim >= 1`, so `psi[0]` is a valid write.
 */
static QaStatus faultPerturb(void *ctx, size_t j, complex double *psi, size_t dim)
{
    Faults *fl = ctx;

    (void)dim;
    if (fl -> perturbStep == 0u || j != fl -> perturbStep) {
        return QA_OK;
    }
    if (fl -> perturbMode == 0) {
        /* Bound: `dim >= 1` (see @assumes), so `psi[0]` is a valid element. */
        psi[0] *= 1.1;
    } else if (fl -> perturbMode == 1) {
        psi[0] = NAN;
    } else {
        return QA_ERR_RANGE;
    }
    return QA_OK;
}

/**
 * @brief Run `qaEvolveWith` on a fixture with the fault table and check the
 * common failure contract (FR-019): the expected code, nothing live, outputs
 * unchanged.
 *
 * @param[in,out] f  Fixture (`|->` start, logging callbacks). @param[in] fl Faults.
 * @param[in] want Expected code. @param[in] what Tag.
 * @return Failure count. @owner No allocation of its own.
 * @assumes `resetLog` was called and the fixture outputs hold the canaries.
 */
static int expectFault(Fixture *f, Faults *fl, QaStatus want, const char *what)
{
    int failures = 0;
    QaEvolveOps ops = *qaEvolveDefaultOps();
    QaStatus got;

    ops.ctx = fl;
    ops.alloc = faultAlloc;
    ops.release = faultRelease;
    ops.schedule = faultSchedule;
    ops.perturb = faultPerturb;
    fillMinus(f -> initial, f -> dim);
    /* Bound: `dim >= 16`, so index 0 of `out` is in range. */
    f -> out[0] = QA_EVOLVE_TEST_CANARY;
    f -> summary.finalEnergy = QA_EVOLVE_TEST_CANARY;
    f -> summary.maxNormDrift = QA_EVOLVE_TEST_CANARY;
    got = qaEvolveWith(&f -> params, f -> initial, f -> out, f -> dim,
                       &f -> callbacks, &f -> summary, &ops);
    failures += checkFailed(got == want, what);
    failures += checkFailed(fl -> live == 0, "TEST-005-FR019 every buffer released");
    if (want != QA_OK) {
        failures += checkFailed(f -> out[0] == QA_EVOLVE_TEST_CANARY
                                    && f -> summary.finalEnergy == QA_EVOLVE_TEST_CANARY
                                    && f -> summary.maxNormDrift == QA_EVOLVE_TEST_CANARY,
                                "TEST-005-FR019 outputs unchanged on failure");
    }
    return failures;
}

/**
 * @brief Build a 2x2 fixture with logging callbacks, `steps = 4`, `M = 1`.
 *
 * @param[out] f Fixture. @return `0` on success, `1` on allocation failure.
 * @owner The caller frees it with `fixtureFree`.
 * @assumes The 2x2 board has `dim = 16`, so the fixture sizes cannot overflow.
 */
static int faultFixture(Fixture *f)
{
    if (fixtureInit(f, 2u) != 0) {
        return 1;
    }
    f -> params.steps = 4u;
    f -> params.snapshotInterval = 1u;
    f -> callbacks.observer = logObserver;
    f -> callbacks.sink = logSink;
    resetLog();
    return 0;
}

/**
 * @brief Group `alloc`: failure at each of the three allocation points
 * (work, table, probabilities), and no third allocation without a sink
 * (EC-010, FR-019, FR-021, FR-025).
 *
 * @return Failure count. @owner Frees its fixture.
 * @assumes The driver allocates work, table, then probabilities (sink only).
 */
static int runAlloc(void)
{
    int failures = 0;
    Fixture f;

    if (faultFixture(&f) != 0) {
        return 1;
    }
    /* 1..3u: the three allocation call indices that the driver makes. */
    for (size_t at = 1; at <= 3u; ++at) {
        Faults fl = {0};
        fl.failAlloc = at;
        resetLog();
        failures += expectFault(&f, &fl, QA_ERR_NOMEM,
                                "TEST-005-EC010 allocation failure gives QA_ERR_NOMEM");
        failures += checkFailed(gEventCount == 0u,
                                "TEST-005-EC010 no callback after an allocation failure");
        failures += checkFailed(fl.allocCalls == ((at == 1u) ? 2u : at),
                                "TEST-005-EC010 allocation order work, table, probs");
    }
    /* Without a sink there are only two allocation points. */
    {
        Faults fl = {0};
        fl.failAlloc = 3u;
        f.callbacks.sink = NULL;
        resetLog();
        failures += expectFault(&f, &fl, QA_OK, "TEST-005-EC010 no probs buffer without a sink");
        failures += checkFailed(fl.allocCalls == 2u, "TEST-005-EC010 two allocations without a sink");
    }
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Group `schedule`: an unexpected schedule failure at the trace node
 * `t_0`, at `tm_j` and at a later trace node `t_j` propagates its code, stops
 * the run and never delivers the failing step (EC-012).
 *
 * @return Failure count. @owner Frees its fixture.
 * @assumes Schedule call order per run: `t_0`, then `tm_{j-1}`, `t_j` for each `j`.
 */
static int runSchedule(void)
{
    /* call index -> events delivered before the failure (observer + sink) */
    static const size_t calls[4] = {1u, 2u, 4u, 5u};
    static const size_t events[4] = {0u, 2u, 4u, 4u};
    static const char *const tags[4] = {
        "TEST-005-EC012 failure at trace node t_0",
        "TEST-005-EC012 failure at tm_0",
        "TEST-005-EC012 failure at tm_1",
        "TEST-005-EC012 failure at trace node t_2"};
    int failures = 0;
    Fixture f;

    if (faultFixture(&f) != 0) {
        return 1;
    }
    /* The bound 4u mirrors the length of the calls[], tags[] and events[]
     * tables. */
    for (size_t c = 0; c < 4u; ++c) {
        Faults fl = {0};
        fl.failSchedule = calls[c];
        resetLog();
        failures += expectFault(&f, &fl, QA_ERR_IO, tags[c]);
        failures += checkFailed(gEventCount == events[c],
                                "TEST-005-EC012 no callback for the failing step");
    }
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Group `perturb`: a norm violation and a non-finite amplitude at step
 * `j` return QA_ERR_DOMAIN; the failing step is not delivered; an error code
 * of the hook is propagated unchanged (FR-006, EC-020, FR-019).
 *
 * @return Failure count. @owner Frees its fixture.
 * @assumes `steps = 4`, `M = 1`: step `j` is preceded by `2 j` events.
 */
static int runPerturb(void)
{
    int failures = 0;
    Fixture f;

    if (faultFixture(&f) != 0) {
        return 1;
    }
    /* 1..4u: step counts of the runs; matches the expected event counts below. */
    for (size_t j = 1; j <= 4u; ++j) {
        Faults fl = {0};
        fl.perturbStep = j;
        fl.perturbMode = 0;
        resetLog();
        failures += expectFault(&f, &fl, QA_ERR_DOMAIN,
                                "TEST-005-EC020 norm failure gives QA_ERR_DOMAIN");
        /* gEventCount == 2u * j (>= 2) is tested first, so gEventCount - 1u does not wrap and is the last logged entry. */
        failures += checkFailed(gEventCount == 2u * j
                                    && gEvents[gEventCount - 1u].kind == 'S'
                                    && gEvents[gEventCount - 1u].j == j - 1u,
                                "TEST-005-EC020 rows stop at j - 1, failing step not delivered");
    }
    {
        Faults fl = {0};
        fl.perturbStep = 2u;
        fl.perturbMode = 1;
        resetLog();
        failures += expectFault(&f, &fl, QA_ERR_DOMAIN,
                                "TEST-005-FR006 non-finite amplitude gives QA_ERR_DOMAIN");
        failures += checkFailed(gEventCount == 4u, "TEST-005-FR006 NaN step not delivered");
    }
    {
        Faults fl = {0};
        fl.perturbStep = 3u;
        fl.perturbMode = 2;
        resetLog();
        failures += expectFault(&f, &fl, QA_ERR_RANGE,
                                "TEST-005-FR019 perturb error propagated unchanged");
        failures += checkFailed(gEventCount == 6u, "TEST-005-FR019 hook failure step not delivered");
    }
    fixtureFree(&f);
    return failures;
}

/**
 * @brief Run the group named by `argv[1]`; exit 0 on success, 1 on a failed
 * check, 2 on a missing or unknown group.
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
        printf("usage: test-005-evolve <n-gate|validation-order|bounds|initial-state|steps1|norm|snapshots|callbacks|repeat|alloc|schedule|perturb>\n");
        return 2;
    }
    if (strcmp(argv[1], "n-gate") == 0) {
        failures += runNGate();
    } else if (strcmp(argv[1], "validation-order") == 0) {
        failures += runOrder();
    } else if (strcmp(argv[1], "bounds") == 0) {
        failures += runBounds();
    } else if (strcmp(argv[1], "initial-state") == 0) {
        failures += runInitial();
    } else if (strcmp(argv[1], "steps1") == 0) {
        failures += runSteps1();
    } else if (strcmp(argv[1], "norm") == 0) {
        failures += runNorm();
    } else if (strcmp(argv[1], "snapshots") == 0) {
        failures += runSnapshots();
    } else if (strcmp(argv[1], "callbacks") == 0) {
        failures += runCallbacks();
    } else if (strcmp(argv[1], "repeat") == 0) {
        failures += runRepeat();
    } else if (strcmp(argv[1], "alloc") == 0) {
        failures += runAlloc();
    } else if (strcmp(argv[1], "schedule") == 0) {
        failures += runSchedule();
    } else if (strcmp(argv[1], "perturb") == 0) {
        failures += runPerturb();
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
