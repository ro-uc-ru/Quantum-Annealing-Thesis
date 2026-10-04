/*
 * 002-hamiltonian behavior tests, Phase 2-3 (tasks T-003-T-007, one group
 * per task).
 *
 * Purpose: assert the specified operator behavior group by group, each group
 * selectable as `test-002-hamiltonian-problem <group>` so every tasks.md
 * `ctest -R 002-hamiltonian-<group>` pattern matches exactly one CTest
 * entry. Failing checks print the `TEST-002-hamiltonian-FR0XX` identifier
 * (the CTest name carries the tasks.md pattern, the printed tag carries the
 * spec identifier).
 *
 * Groups: `n-gate` (task T-003), `shape` (task T-004), `vectors`
 * (task T-005), `domain` (task T-006), `linearity` (task T-007). No
 * arguments runs every group implemented so far.
 *
 * Ownership: no allocation; every buffer is function-local storage, nothing
 * to release. Errors: any failed check is printed with its FR tag and the
 * process exits non-zero; an unknown group name exits 2 with usage.
 * Numerical assumptions: `unsigned int` is 32-bit (asserted by the 001
 * header test), so `UINT_MAX` exercises the overflowing-square path.
 * Determinism: fixed vectors only, no clock, no RNG, no environment or
 * filesystem access, so repeated runs are byte-identical (§13).
 */

#include "qa/hamiltonian/problem.h"

#include <complex.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Smallest exercised state: dim = 2^(2*2) = 16. Larger dims only arrive with
 * the vector groups (T-005); the N-gate and shape checks never read a
 * buffer, so small buffers prove the no-touch contract for any `dim`. */
#define QA_PROBLEM_TEST_DIM 16u

/* Scratch block for overlap probes: two adjacent `QA_PROBLEM_TEST_DIM`
 * windows fit inside with room for partial-overlap offsets. */
#define QA_PROBLEM_TEST_BLOCK 32u

/* Records one failing check; the run stays allocation-free. */
static int checkFailed(int condition, const char *what)
{
    if (condition) {
        return 0;
    }
    printf("FAIL: %s\n", what);
    return 1;
}

/* Fills both buffers with a recognizable pattern; callers snapshot them to
 * prove the no-write contract on every failure path. */
static void fillCanary(complex double *phi, complex double *outPsi)
{
    for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
        phi[k] = (double)k + (double)(k + 1) * I;
        outPsi[k] = (double)(k + 2) + (double)(k + 3) * I;
    }
}

/* Fills `count` cells from `base` with a recognizable pattern, for probes
 * that own a single buffer or a scratch block instead of the canary pair. */
static void fillWindow(complex double *buf, size_t count, double base)
{
    for (size_t k = 0; k < count; ++k) {
        buf[k] = (base + (double)k) + (base + (double)(k + 1)) * I;
    }
}

/**
 * @brief N-gate group (task T-003): FR-002, FR-003, EC-001, EC-002, EC-013.
 *
 * Feeds bad `N` with NULL buffers (a dereference would crash, so a clean
 * return proves EC-001 "without dereferencing NULL") and with canary
 * buffers (proving "write nothing"), asserts the `UNSUPPORTED`-before-all
 * precedence for `N == 5` (EC-013: NULL, wrong dim, and non-finite `phi`
 * still yield `UNSUPPORTED`), and pins the `RANGE`/`OVERFLOW` split for
 * `N > 5` (EC-001 parenthetical: `65535` fits, `65536` and `UINT_MAX`
 * overflow the `unsigned int` square). Valid `N` with NULL or mismatched
 * input yields the fail-closed `RANGE` tail until T-004 lands the real
 * checks; no `QA_OK` is asserted here because the apply core is T-005.
 *
 * @return Failure count, 0 when every N-gate assert holds.
 *
 * @owner No allocation; all buffers are function-local storage.
 * @assumes `qaHamiltonianApplyProblem` runs the `N`-gate before any pointer,
 *          `dim`, or float work (spec §1 validation order).
 */
static int runNGate(void)
{
    static const unsigned int rangeN[] = {0, 1, 6, 100, 65535};
    static const unsigned int overflowN[] = {65536, UINT_MAX};
    static const unsigned int goodN[] = {2, 3, 4};
    int failures = 0;
    char what[160];

    for (unsigned int k = 0; k < 5; ++k) {
        unsigned int n = rangeN[k];
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR003: n=%u NULL yields RANGE", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, NULL, NULL, 0)
                                    == QA_ERR_RANGE,
                                what);

        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        complex double outBefore[QA_PROBLEM_TEST_DIM];
        fillCanary(phi, outPsi);
        memcpy(phiBefore, phi, sizeof phi);
        memcpy(outBefore, outPsi, sizeof outPsi);
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR003: n=%u canary yields RANGE", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_RANGE,
                                what);
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR003: n=%u leaves buffers", n);
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                    memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                what);
    }

    for (unsigned int k = 0; k < 2; ++k) {
        unsigned int n = overflowN[k];
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR003: n=%u NULL yields OVERFLOW", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, NULL, NULL, 0)
                                    == QA_ERR_OVERFLOW,
                                what);

        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        complex double outBefore[QA_PROBLEM_TEST_DIM];
        fillCanary(phi, outPsi);
        memcpy(phiBefore, phi, sizeof phi);
        memcpy(outBefore, outPsi, sizeof outPsi);
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR003: n=%u canary yields OVERFLOW", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_OVERFLOW,
                                what);
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR003: n=%u leaves buffers", n);
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                    memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                what);
    }

    failures += checkFailed(qaHamiltonianApplyProblem(5, NULL, NULL, 0)
                                == QA_ERR_UNSUPPORTED,
                            "TEST-002-hamiltonian-FR002: n=5 NULL yields UNSUPPORTED");
    failures += checkFailed(qaHamiltonianApplyProblem(5, NULL, NULL, 12345)
                                == QA_ERR_UNSUPPORTED,
                            "TEST-002-hamiltonian-FR002: n=5 NULL wrong-dim yields UNSUPPORTED");

    {
        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        complex double outBefore[QA_PROBLEM_TEST_DIM];
        fillCanary(phi, outPsi);
        memcpy(phiBefore, phi, sizeof phi);
        memcpy(outBefore, outPsi, sizeof outPsi);
        failures += checkFailed(qaHamiltonianApplyProblem(5, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_UNSUPPORTED,
                                "TEST-002-hamiltonian-FR002: n=5 canary yields UNSUPPORTED");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                    memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                "TEST-002-hamiltonian-FR002: n=5 leaves buffers");
        failures += checkFailed(qaHamiltonianApplyProblem(5, phi, outPsi, 17)
                                    == QA_ERR_UNSUPPORTED,
                                "TEST-002-hamiltonian-FR002: n=5 wrong-dim yields UNSUPPORTED");
        failures += checkFailed(qaHamiltonianApplyProblem(5, NULL, NULL, SIZE_MAX)
                                    == QA_ERR_UNSUPPORTED,
                                "TEST-002-hamiltonian-FR002: n=5 NULL SIZE_MAX-dim yields UNSUPPORTED");

        /* EC-013: a non-finite amplitude must not shadow the N-gate. */
        phi[3] = INFINITY + 0.0 * I;
        failures += checkFailed(qaHamiltonianApplyProblem(5, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_UNSUPPORTED,
                                "TEST-002-hamiltonian-FR002: n=5 non-finite phi yields UNSUPPORTED");
    }

    for (unsigned int k = 0; k < 3; ++k) {
        unsigned int n = goodN[k];
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR003: n=%u NULL fails closed RANGE", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, NULL, NULL,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_RANGE,
                                what);
    }

    {
        /* The gate passes valid N: n=2 with a normalized basis input runs
         * the T-005 apply core to QA_OK (|0> carries E=0, asserted exactly
         * by the vectors group); n=3,4 keep dim=16 mismatched, still RANGE
         * via the dim check. The basis input stays normalized, so this
         * probe is stable under the T-006 norm gate. */
        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phi[k] = 0.0 + 0.0 * I;
            outPsi[k] = 0.0 + 0.0 * I;
        }
        phi[0] = 1.0 + 0.0 * I;
        memcpy(phiBefore, phi, sizeof phi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR003: n=2 basis proceeds to QA_OK");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0,
                                "TEST-002-hamiltonian-FR003: n=2 basis keeps phi");

        for (unsigned int m = 3; m <= 4; ++m) {
            complex double phi2[QA_PROBLEM_TEST_DIM];
            complex double out2[QA_PROBLEM_TEST_DIM];
            complex double phi2Before[QA_PROBLEM_TEST_DIM];
            complex double out2Before[QA_PROBLEM_TEST_DIM];
            fillCanary(phi2, out2);
            memcpy(phi2Before, phi2, sizeof phi2);
            memcpy(out2Before, out2, sizeof out2);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR003: n=%u dim mismatch yields RANGE", m);
            failures += checkFailed(qaHamiltonianApplyProblem(m, phi2, out2,
                                                              QA_PROBLEM_TEST_DIM)
                                        == QA_ERR_RANGE,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR003: n=%u dim mismatch leaves buffers", m);
            failures += checkFailed(memcmp(phi2, phi2Before, sizeof phi2) == 0 &&
                                        memcmp(out2, out2Before, sizeof out2) == 0,
                                    what);
        }
    }

    if (failures == 0) {
        printf("002-hamiltonian n-gate: OK (5 unsupported; 0,1,6,100,65535 range; 65536,UINT_MAX overflow; 2,3,4 pass gate)\n");
    }
    return failures;
}

/**
 * @brief Shape group (task T-004): FR-006, EC-003, EC-004, EC-005, EC-014,
 * EC-015.
 *
 * Asserts the two-pass shape contract with buffers proven untouched via
 * `memcmp` (`phi` bit-identical per EC-010, `outPsi` per EC-011; the checks
 * never read a buffer, so 16-element buffers prove no-touch for any `dim`,
 * including `SIZE_MAX`): NULL receivers (EC-004), every EC-003 `dim`
 * mismatch for each valid `N`, identical buffers (EC-005), partial overlaps
 * with matching and mismatched `dim` (EC-015), and wrong-`dim` plus
 * non-finite `phi` resolving to `RANGE` (EC-014: `dim` precedes finiteness).
 * Exact adjacency (`outPsi == phi + dim`) passes shape and reaches the
 * fail-closed tail, still `RANGE` with buffers unchanged; T-005 promotes
 * that case once the apply core lands.
 *
 * @return Failure count, 0 when every shape assert holds.
 *
 * @owner No allocation; all buffers are function-local storage.
 * @assumes `qaHamiltonianApplyProblem` validates NULL, then `dim`, then
 *          `uintptr_t` overlap before any float work or write (spec §1).
 */
static int runShape(void)
{
    static const unsigned int goodN[] = {2, 3, 4};
    static const size_t badDims[] = {0, 15, 17, 511, 513, 65535, 65537, SIZE_MAX};
    int failures = 0;
    char what[160];

    for (unsigned int k = 0; k < 3; ++k) {
        unsigned int n = goodN[k];
        complex double buf[QA_PROBLEM_TEST_DIM];
        complex double before[QA_PROBLEM_TEST_DIM];
        fillWindow(buf, QA_PROBLEM_TEST_DIM, 0.0);
        memcpy(before, buf, sizeof buf);

        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR006: n=%u NULL phi yields RANGE", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, NULL, buf,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_RANGE,
                                what);
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR006: n=%u NULL phi keeps out", n);
        failures += checkFailed(memcmp(buf, before, sizeof buf) == 0, what);

        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR006: n=%u NULL out yields RANGE", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, buf, NULL,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_RANGE,
                                what);
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR006: n=%u NULL out keeps phi", n);
        failures += checkFailed(memcmp(buf, before, sizeof buf) == 0, what);

        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR006: n=%u NULL pair yields RANGE", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, NULL, NULL,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_RANGE,
                                what);
    }

    for (unsigned int k = 0; k < 3; ++k) {
        unsigned int n = goodN[k];
        for (unsigned int d = 0; d < 8; ++d) {
            size_t dim = badDims[d];
            complex double phi[QA_PROBLEM_TEST_DIM];
            complex double outPsi[QA_PROBLEM_TEST_DIM];
            complex double phiBefore[QA_PROBLEM_TEST_DIM];
            complex double outBefore[QA_PROBLEM_TEST_DIM];
            fillCanary(phi, outPsi);
            memcpy(phiBefore, phi, sizeof phi);
            memcpy(outBefore, outPsi, sizeof outPsi);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR006: n=%u dim=%zu yields RANGE",
                     n, dim);
            failures += checkFailed(qaHamiltonianApplyProblem(n, phi, outPsi,
                                                              dim)
                                        == QA_ERR_RANGE,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR006: n=%u dim=%zu leaves buffers",
                     n, dim);
            failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                        memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                    what);
        }
    }

    for (unsigned int k = 0; k < 3; ++k) {
        unsigned int n = goodN[k];
        complex double buf[QA_PROBLEM_TEST_DIM];
        complex double before[QA_PROBLEM_TEST_DIM];
        fillWindow(buf, QA_PROBLEM_TEST_DIM, 4.0);
        memcpy(before, buf, sizeof buf);
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR006: n=%u same buffer yields RANGE", n);
        failures += checkFailed(qaHamiltonianApplyProblem(n, buf, buf,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_RANGE,
                                what);
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR006: n=%u same buffer unchanged", n);
        failures += checkFailed(memcmp(buf, before, sizeof buf) == 0, what);
    }

    {
        static const size_t offsets[] = {1, 15};
        for (unsigned int o = 0; o < 2; ++o) {
            complex double block[QA_PROBLEM_TEST_BLOCK];
            complex double before[QA_PROBLEM_TEST_BLOCK];
            fillWindow(block, QA_PROBLEM_TEST_BLOCK, 8.0);
            memcpy(before, block, sizeof block);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR006: offset=%zu overlap yields RANGE",
                     offsets[o]);
            failures += checkFailed(qaHamiltonianApplyProblem(2, block, block + offsets[o],
                                                              QA_PROBLEM_TEST_DIM)
                                        == QA_ERR_RANGE,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR006: offset=%zu overlap keeps block",
                     offsets[o]);
            failures += checkFailed(memcmp(block, before, sizeof block) == 0,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR006: offset=%zu reversed overlap yields RANGE",
                     offsets[o]);
            failures += checkFailed(qaHamiltonianApplyProblem(2, block + offsets[o], block,
                                                              QA_PROBLEM_TEST_DIM)
                                        == QA_ERR_RANGE,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR006: offset=%zu reversed keeps block",
                     offsets[o]);
            failures += checkFailed(memcmp(block, before, sizeof block) == 0,
                                    what);
        }

        complex double block[QA_PROBLEM_TEST_BLOCK];
        complex double before[QA_PROBLEM_TEST_BLOCK];
        fillWindow(block, QA_PROBLEM_TEST_BLOCK, 8.0);
        memcpy(before, block, sizeof block);
        failures += checkFailed(qaHamiltonianApplyProblem(2, block, block + 1, 17)
                                    == QA_ERR_RANGE,
                                "TEST-002-hamiltonian-FR006: overlap with wrong dim yields RANGE");
        failures += checkFailed(memcmp(block, before, sizeof block) == 0,
                                "TEST-002-hamiltonian-FR006: overlap with wrong dim keeps block");
    }

    {
        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        complex double outBefore[QA_PROBLEM_TEST_DIM];
        fillCanary(phi, outPsi);
        phi[5] = INFINITY + 0.0 * I;
        memcpy(phiBefore, phi, sizeof phi);
        memcpy(outBefore, outPsi, sizeof outPsi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi, 17)
                                    == QA_ERR_RANGE,
                                "TEST-002-hamiltonian-FR006: wrong dim with non-finite phi yields RANGE");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                    memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                "TEST-002-hamiltonian-FR006: wrong dim with non-finite phi leaves buffers");
    }

    {
        /* Adjacent windows pass shape, so the T-005 core runs: a normalized
         * |0> input yields QA_OK with the phi window unchanged and the out
         * window zeroed (E(0) = 0, exactly as the vectors group asserts for
         * every basis input). Normalized input keeps this probe stable
         * under the T-006 norm gate. */
        complex double block[QA_PROBLEM_TEST_BLOCK];
        complex double before[QA_PROBLEM_TEST_BLOCK];
        fillWindow(block, QA_PROBLEM_TEST_BLOCK, 12.0);
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            block[k] = 0.0 + 0.0 * I;
        }
        block[0] = 1.0 + 0.0 * I;
        memcpy(before, block, sizeof block);
        failures += checkFailed(qaHamiltonianApplyProblem(2, block, block + QA_PROBLEM_TEST_DIM,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR006: adjacency runs apply to QA_OK");
        failures += checkFailed(memcmp(block, before, QA_PROBLEM_TEST_DIM * sizeof *block) == 0,
                                "TEST-002-hamiltonian-FR006: adjacency keeps phi window");
        int outZero = 1;
        for (size_t k = QA_PROBLEM_TEST_DIM; k < QA_PROBLEM_TEST_BLOCK; ++k) {
            if (creal(block[k]) != 0.0 || cimag(block[k]) != 0.0) {
                outZero = 0;
            }
        }
        failures += checkFailed(outZero,
                                "TEST-002-hamiltonian-FR006: adjacency zeroes out window");
    }

    if (failures == 0) {
        printf("002-hamiltonian shape: OK (NULL/dim/overlap range; partial overlap range; adjacency passes shape)\n");
    }
    return failures;
}

/**
 * @brief Check one basis state against its §7 oracle energy (T-005 helper).
 *
 * Builds the normalized basis input `|at>` (`phi[at] = 1`, zeros elsewhere),
 * poisons `outPsi` so every cell must be overwritten, calls apply, and
 * requires `QA_OK`, `outPsi[at]` equal to `expected` bit-exact (real part;
 * imaginary part `+0.0`), zeros elsewhere, and `phi` unchanged. The 4x4
 * case scopes 3 MiB of function-local storage per call, released on return.
 *
 * @return Failure count, 0 when the oracle holds bit-exact.
 *
 * @owner No allocation; all buffers are function-local storage.
 * @assumes `at < dim` with `dim` the gated `2^(n*n)` for `n`.
 */
static int checkBasis(unsigned int n, size_t dim, size_t at, double expected)
{
    complex double phi[65536];
    complex double phiBefore[65536];
    complex double outPsi[65536];
    int failures = 0;
    char what[160];

    for (size_t k = 0; k < dim; ++k) {
        phi[k] = 0.0 + 0.0 * I;
    }
    phi[at] = 1.0 + 0.0 * I;
    memcpy(phiBefore, phi, dim * sizeof *phi);
    for (size_t k = 0; k < dim; ++k) {
        outPsi[k] = 9.0 + 9.0 * I;
    }

    snprintf(what, sizeof what,
             "TEST-002-hamiltonian-FR004: n=%u k=%zu yields QA_OK", n, at);
    failures += checkFailed(qaHamiltonianApplyProblem(n, phi, outPsi, dim)
                                == QA_OK,
                            what);

    snprintf(what, sizeof what,
             "TEST-002-hamiltonian-FR001: n=%u E(%zu) == %g bit-exact",
             n, at, expected);
    failures += checkFailed(creal(outPsi[at]) == expected &&
                                cimag(outPsi[at]) == 0.0,
                            what);

    int restZero = 1;
    for (size_t k = 0; k < dim; ++k) {
        if (k != at && (creal(outPsi[k]) != 0.0 || cimag(outPsi[k]) != 0.0)) {
            restZero = 0;
        }
    }
    snprintf(what, sizeof what,
             "TEST-002-hamiltonian-FR004: n=%u k=%zu zeroes elsewhere",
             n, at);
    failures += checkFailed(restZero, what);

    snprintf(what, sizeof what,
             "TEST-002-hamiltonian-FR004: n=%u k=%zu keeps phi", n, at);
    failures += checkFailed(memcmp(phi, phiBefore, dim * sizeof *phi) == 0,
                            what);
    return failures;
}

/**
 * @brief Vectors group (task T-005): FR-001, FR-004, EC-007, EC-008.
 *
 * Every §7 oracle as a normalized basis input: 2x2 `E(0) = 0`, `E(9) = 1`,
 * `E(11) = 3`, `E(15) = 6`; 3x3 `E(0) = 0`, `E(511) = 28`, `E(256) = 0`,
 * `E(1) = 0`; 4x4 `E(0) = 0`, `E(65535) = 76`, `E(32768) = 0`, `E(1) = 0`,
 * `E(0x8421) = 6`, `E(16770) = 0`. Empty and full boards pin EC-007
 * (`E_full = 6/28/76`), lone queens pin EC-008 (`E = 0`), and every case
 * requires the oracle bit-exact, zeros elsewhere, and `phi` unchanged.
 *
 * @return Failure count, 0 when every §7 oracle holds.
 *
 * @owner No allocation; all buffers are function-local storage.
 * @assumes The apply core scales exact small-integer energies without
 *          rounding, so basis outputs are bit-exact.
 */
static int runVectors(void)
{
    int failures = 0;

    failures += checkBasis(2, 16, 0, 0.0);
    failures += checkBasis(2, 16, 1, 0.0);
    failures += checkBasis(2, 16, 8, 0.0);
    failures += checkBasis(2, 16, 9, 1.0);
    failures += checkBasis(2, 16, 11, 3.0);
    failures += checkBasis(2, 16, 15, 6.0);

    failures += checkBasis(3, 512, 0, 0.0);
    failures += checkBasis(3, 512, 1, 0.0);
    failures += checkBasis(3, 512, 256, 0.0);
    failures += checkBasis(3, 512, 511, 28.0);

    failures += checkBasis(4, 65536, 0, 0.0);
    failures += checkBasis(4, 65536, 1, 0.0);
    failures += checkBasis(4, 65536, 32768, 0.0);
    failures += checkBasis(4, 65536, 0x8421, 6.0);
    failures += checkBasis(4, 65536, 16770, 0.0);
    failures += checkBasis(4, 65536, 65535, 76.0);

    if (failures == 0) {
        printf("002-hamiltonian vectors: OK (every §7 oracle bit-exact, zeros elsewhere, phi unchanged)\n");
    }
    return failures;
}

/**
 * @brief Domain group (task T-006): FR-007, FR-008, EC-006, EC-009, EC-016.
 *
 * Asserts the scan-then-gate contract with buffers proven untouched via
 * `memcmp` (`phi` bit-identical per EC-010, `outPsi` per EC-011): the zero
 * vector and finite unnormalized scalings (`2.5`, `0.5`, and `1 + 1e-9`,
 * just outside the `1e-12` band) yield `DOMAIN` (EC-009, FR-008); a single
 * poisoned index (`+Inf`, `-Inf`, imaginary `Inf`, `NaN`), including one at
 * the last cell proving the full scan, yields `DOMAIN` (EC-006, FR-007);
 * and a non-finite `outPsi` canary survives a `DOMAIN` trigger
 * bit-identical (EC-016). Every earlier group feeds normalized or rejected
 * inputs, so none of its asserts change under the new gates.
 *
 * @return Failure count, 0 when every domain assert holds.
 *
 * @owner No allocation; all buffers are function-local storage.
 * @assumes `qaHamiltonianApplyProblem` scans finiteness fully, then gates
 *          `|norm - 1| <= 1e-12`, all before the first write (spec §1).
 */
static int runDomain(void)
{
    int failures = 0;
    char what[160];

    {
        /* EC-009: the zero vector has norm 0, outside tolerance. */
        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        complex double outBefore[QA_PROBLEM_TEST_DIM];
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phi[k] = 0.0 + 0.0 * I;
        }
        fillWindow(outPsi, QA_PROBLEM_TEST_DIM, 20.0);
        memcpy(phiBefore, phi, sizeof phi);
        memcpy(outBefore, outPsi, sizeof outPsi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_DOMAIN,
                                "TEST-002-hamiltonian-FR008: zero vector yields DOMAIN");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                    memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                "TEST-002-hamiltonian-FR008: zero vector leaves buffers");
    }

    {
        /* EC-009: finite but unnormalized scalings, at and just outside
         * the 1e-12 band. */
        static const double scales[] = {2.5, 0.5, 1.0 + 1e-9};
        for (unsigned int s = 0; s < 3; ++s) {
            complex double phi[QA_PROBLEM_TEST_DIM];
            complex double outPsi[QA_PROBLEM_TEST_DIM];
            complex double phiBefore[QA_PROBLEM_TEST_DIM];
            complex double outBefore[QA_PROBLEM_TEST_DIM];
            for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
                phi[k] = 0.0 + 0.0 * I;
            }
            phi[3] = scales[s] + 0.0 * I;
            fillWindow(outPsi, QA_PROBLEM_TEST_DIM, 24.0);
            memcpy(phiBefore, phi, sizeof phi);
            memcpy(outBefore, outPsi, sizeof outPsi);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR008: scale %g yields DOMAIN",
                     scales[s]);
            failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                              QA_PROBLEM_TEST_DIM)
                                        == QA_ERR_DOMAIN,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR008: scale %g leaves buffers",
                     scales[s]);
            failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                        memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                    what);
        }
    }

    {
        /* EC-006: one poisoned index among finite amplitudes; the last-cell
         * poison proves the scan runs to completion. */
        static const size_t poisonAt[] = {5, 6, 7, 15};
        for (unsigned int p = 0; p < 4; ++p) {
            complex double phi[QA_PROBLEM_TEST_DIM];
            complex double outPsi[QA_PROBLEM_TEST_DIM];
            complex double phiBefore[QA_PROBLEM_TEST_DIM];
            complex double outBefore[QA_PROBLEM_TEST_DIM];
            for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
                phi[k] = 0.0 + 0.0 * I;
            }
            phi[0] = 1.0 + 0.0 * I;
            if (p == 0) {
                phi[poisonAt[p]] = INFINITY + 0.0 * I;
            } else if (p == 1) {
                phi[poisonAt[p]] = -INFINITY + 0.0 * I;
            } else if (p == 2) {
                phi[poisonAt[p]] = 0.0 + INFINITY * I;
            } else {
                phi[poisonAt[p]] = NAN + 0.0 * I;
            }
            fillWindow(outPsi, QA_PROBLEM_TEST_DIM, 28.0);
            memcpy(phiBefore, phi, sizeof phi);
            memcpy(outBefore, outPsi, sizeof outPsi);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR007: poison %u yields DOMAIN", p);
            failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                              QA_PROBLEM_TEST_DIM)
                                        == QA_ERR_DOMAIN,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR007: poison %u leaves buffers", p);
            failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                        memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                    what);
        }
    }

    {
        /* EC-016: a DOMAIN trigger meets a non-finite outPsi canary; the
         * canary must survive bit-identical. */
        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double outBefore[QA_PROBLEM_TEST_DIM];
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phi[k] = 0.0 + 0.0 * I;
            outPsi[k] = (k % 2u == 0u) ? INFINITY + 0.0 * I : NAN + 0.0 * I;
        }
        memcpy(outBefore, outPsi, sizeof outPsi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_DOMAIN,
                                "TEST-002-hamiltonian-FR007: canary meets zero phi yields DOMAIN");
        failures += checkFailed(memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                "TEST-002-hamiltonian-FR007: canary bit-identical on zero phi");

        phi[2] = 0.0 + NAN * I;
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_DOMAIN,
                                "TEST-002-hamiltonian-FR007: canary meets poisoned phi yields DOMAIN");
        failures += checkFailed(memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                "TEST-002-hamiltonian-FR007: canary bit-identical on phi poison");
    }

    if (failures == 0) {
        printf("002-hamiltonian domain: OK (non-finite rejected; norm gate enforced; canaries untouched)\n");
    }
    return failures;
}

/**
 * @brief Linearity group (task T-007): FR-004, FR-009, EC-010, EC-011,
 * EC-012, EC-017.
 *
 * All inputs are normalized, so the T-006 norm gate passes and every apply
 * below must yield `QA_OK` unless a `DOMAIN` trigger is intended. Probes on
 * N=2 (`dim = 16`, `E(9) = 1`, `E(11) = 3` per §7):
 *   - real superposition `(|9> + |11>) / sqrt(2)` agrees with the separate
 *     `H|9>`, `H|11>` runs within `1e-12` per cell (EC-012, FR-004);
 *   - complex superposition `(|9> + i|11>) / sqrt(2)` agrees the same way;
 *   - dyadic phases (`-|9>`, `i|11>`) are bit-exact (FR-004, FR-009);
 *   - zero cells compare with `== 0.0`, so `+0.0` vs `-0.0` count as equal
 *     (EC-011 parenthetical);
 *   - repeated identical calls into distinct outputs are bit-identical
 *     (`memcmp`, EC-017, FR-009);
 *   - `phi` is bit-identical after success and after failure (EC-010) and a
 *     `DOMAIN` failure leaves `outPsi` bit-identical (EC-011, two-pass).
 *
 * @return Failure count, 0 when every linearity assert holds.
 *
 * @owner No allocation; all buffers are function-local storage.
 * @assumes `1/sqrt(2)` is irrational in binary, so superposition peaks agree
 *          only within `1e-12`, while dyadic scalings (`-1`, `i`) are exact.
 */
static int runLinearity(void)
{
    const double invSqrt2 = 1.0 / sqrt(2.0);
    const double tol = 1e-12;
    int failures = 0;

    {
        /* EC-012 real superposition vs separate applies. */
        complex double phiS[QA_PROBLEM_TEST_DIM];
        complex double phiSBefore[QA_PROBLEM_TEST_DIM];
        complex double outS[QA_PROBLEM_TEST_DIM];
        complex double phiX[QA_PROBLEM_TEST_DIM];
        complex double outX[QA_PROBLEM_TEST_DIM];
        complex double phiY[QA_PROBLEM_TEST_DIM];
        complex double outY[QA_PROBLEM_TEST_DIM];
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phiS[k] = 0.0 + 0.0 * I;
            phiX[k] = 0.0 + 0.0 * I;
            phiY[k] = 0.0 + 0.0 * I;
            outS[k] = 9.0 + 9.0 * I;
            outX[k] = 9.0 + 9.0 * I;
            outY[k] = 9.0 + 9.0 * I;
        }
        phiS[9] = invSqrt2 + 0.0 * I;
        phiS[11] = invSqrt2 + 0.0 * I;
        phiX[9] = 1.0 + 0.0 * I;
        phiY[11] = 1.0 + 0.0 * I;
        memcpy(phiSBefore, phiS, sizeof phiS);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phiS, outS,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR004: super (|9>+|11>)/sqrt2 yields QA_OK");
        failures += checkFailed(qaHamiltonianApplyProblem(2, phiX, outX,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR004: basis |9> yields QA_OK");
        failures += checkFailed(qaHamiltonianApplyProblem(2, phiY, outY,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR004: basis |11> yields QA_OK");
        failures += checkFailed(memcmp(phiS, phiSBefore, sizeof phiS) == 0,
                                "TEST-002-hamiltonian-FR004: super keeps phi");
        failures += checkFailed(cabs(outS[9] - invSqrt2) <= tol,
                                "TEST-002-hamiltonian-FR004: super peak k=9 within 1e-12");
        failures += checkFailed(cabs(outS[11] - 3.0 * invSqrt2) <= tol,
                                "TEST-002-hamiltonian-FR004: super peak k=11 within 1e-12");
        int linear = 1;
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            complex double expected = (outX[k] + outY[k]) * invSqrt2;
            if (cabs(outS[k] - expected) > tol) {
                linear = 0;
            }
        }
        failures += checkFailed(linear,
                                "TEST-002-hamiltonian-FR004: super agrees with H|x>,H|y> within 1e-12");
        int restZero = 1;
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            if (k != 9 && k != 11 &&
                (creal(outS[k]) != 0.0 || cimag(outS[k]) != 0.0)) {
                restZero = 0;
            }
        }
        failures += checkFailed(restZero,
                                "TEST-002-hamiltonian-FR004: super zeroes elsewhere (+0/-0 equal)");
    }

    {
        /* EC-012 complex superposition vs separate applies. */
        complex double phiC[QA_PROBLEM_TEST_DIM];
        complex double outC[QA_PROBLEM_TEST_DIM];
        complex double phiX[QA_PROBLEM_TEST_DIM];
        complex double outX[QA_PROBLEM_TEST_DIM];
        complex double phiY[QA_PROBLEM_TEST_DIM];
        complex double outY[QA_PROBLEM_TEST_DIM];
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phiC[k] = 0.0 + 0.0 * I;
            phiX[k] = 0.0 + 0.0 * I;
            phiY[k] = 0.0 + 0.0 * I;
            outC[k] = 9.0 + 9.0 * I;
            outX[k] = 9.0 + 9.0 * I;
            outY[k] = 9.0 + 9.0 * I;
        }
        phiC[9] = invSqrt2 + 0.0 * I;
        phiC[11] = 0.0 + invSqrt2 * I;
        phiX[9] = 1.0 + 0.0 * I;
        phiY[11] = 1.0 + 0.0 * I;
        failures += checkFailed(qaHamiltonianApplyProblem(2, phiC, outC,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR004: super (|9>+i|11>)/sqrt2 yields QA_OK");
        failures += checkFailed(qaHamiltonianApplyProblem(2, phiX, outX,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR004: complex basis |9> yields QA_OK");
        failures += checkFailed(qaHamiltonianApplyProblem(2, phiY, outY,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR004: complex basis |11> yields QA_OK");
        failures += checkFailed(cabs(outC[9] - invSqrt2) <= tol,
                                "TEST-002-hamiltonian-FR004: complex peak k=9 within 1e-12");
        failures += checkFailed(cabs(outC[11] - 3.0 * invSqrt2 * I) <= tol,
                                "TEST-002-hamiltonian-FR004: complex peak k=11 within 1e-12");
        int linear = 1;
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            complex double expected = (outX[k] + I * outY[k]) * invSqrt2;
            if (cabs(outC[k] - expected) > tol) {
                linear = 0;
            }
        }
        failures += checkFailed(linear,
                                "TEST-002-hamiltonian-FR004: complex super agrees within 1e-12");
    }

    {
        /* Dyadic phases are bit-exact: -|9> (E=1), i|11> (E=3). */
        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phi[k] = 0.0 + 0.0 * I;
            outPsi[k] = 9.0 + 9.0 * I;
        }
        phi[9] = -1.0 + 0.0 * I;
        memcpy(phiBefore, phi, sizeof phi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR009: phase -|9> yields QA_OK");
        failures += checkFailed(creal(outPsi[9]) == -1.0 && cimag(outPsi[9]) == 0.0,
                                "TEST-002-hamiltonian-FR009: phase -|9> bit-exact");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0,
                                "TEST-002-hamiltonian-FR009: phase -|9> keeps phi");

        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phi[k] = 0.0 + 0.0 * I;
            outPsi[k] = 9.0 + 9.0 * I;
        }
        phi[11] = 0.0 + 1.0 * I;
        memcpy(phiBefore, phi, sizeof phi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR009: phase i|11> yields QA_OK");
        failures += checkFailed(creal(outPsi[11]) == 0.0 && cimag(outPsi[11]) == 3.0,
                                "TEST-002-hamiltonian-FR009: phase i|11> bit-exact");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0,
                                "TEST-002-hamiltonian-FR009: phase i|11> keeps phi");
    }

    {
        /* EC-011 parenthetical: +0.0 vs -0.0 count as equal in zero outputs. */
        complex double plusZero = 0.0 + 0.0 * I;
        complex double minusZero = -0.0 + (-0.0) * I;
        failures += checkFailed(plusZero == minusZero,
                                "TEST-002-hamiltonian-FR004: +0.0 equals -0.0");
        failures += checkFailed(creal(minusZero) == 0.0 && cimag(minusZero) == 0.0,
                                "TEST-002-hamiltonian-FR004: -0.0 reads as zero");
    }

    {
        /* EC-017 determinism: identical inputs into distinct outputs agree. */
        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        complex double outA[QA_PROBLEM_TEST_DIM];
        complex double outB[QA_PROBLEM_TEST_DIM];
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phi[k] = 0.0 + 0.0 * I;
            outA[k] = 9.0 + 9.0 * I;
            outB[k] = -9.0 + (-9.0) * I;
        }
        phi[9] = invSqrt2 + 0.0 * I;
        phi[11] = invSqrt2 + 0.0 * I;
        memcpy(phiBefore, phi, sizeof phi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outA,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR009: determinism first call yields QA_OK");
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outB,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_OK,
                                "TEST-002-hamiltonian-FR009: determinism second call yields QA_OK");
        failures += checkFailed(memcmp(outA, outB, sizeof outA) == 0,
                                "TEST-002-hamiltonian-FR009: repeated calls bit-identical");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0,
                                "TEST-002-hamiltonian-FR009: determinism keeps phi");
    }

    {
        /* EC-010/EC-011 on failure: unnormalized and poisoned inputs leave
         * both buffers bit-identical (outPsi proven via memcmp: no write). */
        complex double phi[QA_PROBLEM_TEST_DIM];
        complex double outPsi[QA_PROBLEM_TEST_DIM];
        complex double phiBefore[QA_PROBLEM_TEST_DIM];
        complex double outBefore[QA_PROBLEM_TEST_DIM];
        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phi[k] = 0.0 + 0.0 * I;
        }
        phi[3] = 2.5 + 0.0 * I;
        fillWindow(outPsi, QA_PROBLEM_TEST_DIM, 32.0);
        memcpy(phiBefore, phi, sizeof phi);
        memcpy(outBefore, outPsi, sizeof outPsi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_DOMAIN,
                                "TEST-002-hamiltonian-FR008: unnormalized yields DOMAIN");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                    memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                "TEST-002-hamiltonian-FR008: unnormalized leaves buffers");

        for (size_t k = 0; k < QA_PROBLEM_TEST_DIM; ++k) {
            phi[k] = 0.0 + 0.0 * I;
        }
        phi[0] = 1.0 + 0.0 * I;
        phi[5] = INFINITY + 0.0 * I;
        fillWindow(outPsi, QA_PROBLEM_TEST_DIM, 36.0);
        memcpy(phiBefore, phi, sizeof phi);
        memcpy(outBefore, outPsi, sizeof outPsi);
        failures += checkFailed(qaHamiltonianApplyProblem(2, phi, outPsi,
                                                          QA_PROBLEM_TEST_DIM)
                                    == QA_ERR_DOMAIN,
                                "TEST-002-hamiltonian-FR007: poisoned yields DOMAIN");
        failures += checkFailed(memcmp(phi, phiBefore, sizeof phi) == 0 &&
                                    memcmp(outPsi, outBefore, sizeof outPsi) == 0,
                                "TEST-002-hamiltonian-FR007: poisoned leaves buffers");
    }

    if (failures == 0) {
        printf("002-hamiltonian linearity: OK (superpositions within 1e-12; phases bit-exact; determinism bit-identical; failures leave buffers)\n");
    }
    return failures;
}

/* Runs one named group; returns -1 for an unknown name. */
static int runGroup(const char *name)
{
    if (strcmp(name, "n-gate") == 0) {
        return runNGate();
    }
    if (strcmp(name, "shape") == 0) {
        return runShape();
    }
    if (strcmp(name, "vectors") == 0) {
        return runVectors();
    }
    if (strcmp(name, "domain") == 0) {
        return runDomain();
    }
    if (strcmp(name, "linearity") == 0) {
        return runLinearity();
    }
    return -1;
}

/* Lists all behavior groups implemented so far. */
static void printUsage(const char *program)
{
    printf("usage: %s [n-gate|shape|vectors|domain|linearity]\n", program);
}

int main(int argc, char *argv[])
{
    int failures = 0;

    printf("002-hamiltonian problem: %s, build=%s\n", __clang_version__, QA_BUILD_TYPE);

    if (argc == 1) {
        failures += runNGate();
        failures += runShape();
        failures += runVectors();
        failures += runDomain();
        failures += runLinearity();
    } else if (argc == 2) {
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
        printf("002-hamiltonian problem: FAILED (%d checks)\n", failures);
        return 1;
    }
    printf("002-hamiltonian problem: OK\n");
    return 0;
}
