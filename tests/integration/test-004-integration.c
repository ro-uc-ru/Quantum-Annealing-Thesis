/*
 * 001<->004 integration test (spec 004-driver, task T-012, FR-012).
 *
 * Purpose: prove the implemented 001-states grid helpers compose with the
 * driver Hamiltonian. For every `N` in {2, 3, 4} and a fixed set of boards
 * (empty, full, checkerboard, and the 4x4 solution 16770) the test builds the
 * id cell by cell with `qaGridWithBit`, requires it to equal the expected
 * literal, and checks:
 *   (a) `psi0[id] = (-1)^queens * s`, `s = 1 / sqrt(dim)`, with the queen
 *       count taken from the test's own mask, never from the library;
 *   (b) `H_driver |id>` is `1` exactly at the ids obtained by reading each
 *       cell with `qaGridGetBit` and writing the opposite bit with
 *       `qaGridWithBit`, and `0` everywhere else (ties `m_c` to the 001 cell
 *       mapping);
 *   (c) FR-011: `H_driver |psi0> = -numCells |psi0>` per amplitude and
 *       `<psi0| H_driver |psi0> = -numCells`, both within 1e-12.
 * A failing helper call fails the case, reports the failing step, and the
 * Hamiltonian functions are not called on the partially built id: every apply
 * in this binary runs through the counted wrapper below. The `build-failure`
 * mode injects an invalid bit at one build step and proves exactly that.
 *
 * Modes: no argument runs the boards; `build-failure` runs the injection.
 *
 * Ownership: per-`N` heap buffers (`psi0`, `hpsi0`, `phi`, `out`, `mark`)
 * owned by `runForN` and freed on every path through its single `cleanup`
 * label. Errors: any failed check is printed with its FR tag and the process
 * exits non-zero. Numerical assumptions: basis inputs are exactly normalized,
 * so (b) is compared bit-exactly (`+0.0` and `-0.0` count as equal); (a) and
 * (c) use the tolerances stated at each check.
 * Determinism: fixed boards only; no clock, RNG, environment or file access.
 */

#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/grid.h"
#include "qa/core/status.h"
#include "qa/hamiltonian/driver.h"

/* Per-amplitude and energy tolerance of FR-011. */
#define TOL 1e-12
/* Tolerance of check (a): both sides are one correctly rounded expression. */
#define TOL_AMPLITUDE 1e-15

/* One board: edge, row-major queen mask (`pos = i * n + j`), expected id. */
typedef struct QaIntegrationCase {
    unsigned int n;
    unsigned int queenMask;
    QaGridId wantId;
    const char *name;
} QaIntegrationCase;

static const QaIntegrationCase kCases[] = {
    {2, 0x0, 0, "n=2 empty"},
    {2, 0xF, 15, "n=2 full"},
    {2, 0x9, 9, "n=2 diagonal"},
    {3, 0x0, 0, "n=3 empty"},
    {3, 0x1FF, 511, "n=3 full"},
    {3, 0x145, 325, "n=3 checkerboard"},
    {4, 0x0, 0, "n=4 empty"},
    {4, 0xFFFF, 65535, "n=4 full"},
    {4, 0xA5A5, 42405, "n=4 pattern"},
    /* Solution queens at (0,1),(1,3),(2,0),(3,2). */
    {4, 0x4182, 16770, "n=4 solution 16770"},
};

/* Counts every apply call in this binary so the build-failure mode can prove
 * apply is never reached on a partially built id. */
static unsigned int gApplyCalls = 0;

/**
 * @brief Counted wrapper around `qaHamiltonianApplyDriver`.
 *
 * @param[in]  n      Board edge.
 * @param[in]  phi    Immutable normalized input of length `dim`.
 * @param[out] outPsi Separate caller-owned receiver of length `dim`.
 * @param[in]  dim    State length `2^(n*n)`.
 *
 * @return The library status, unchanged.
 *
 * @owner No allocation; buffers stay owned by the caller.
 * @assumes Same as `qaHamiltonianApplyDriver`.
 */
static QaStatus countedApply(unsigned int n, const complex double *phi,
                             complex double *outPsi, size_t dim)
{
    gApplyCalls++;
    return qaHamiltonianApplyDriver(n, phi, outPsi, dim);
}

/**
 * @brief Build a board id cell by cell with `qaGridWithBit`.
 *
 * Starts from id 0 and writes every queen of `queenMask` in row-major order.
 * When `injectStep` is a valid step index, that step is called with the
 * invalid bit `2` instead (build-failure mode).
 *
 * @param[in]  n          Board edge, 2..4.
 * @param[in]  queenMask  Row-major queen mask, bit `pos` for cell `pos`.
 * @param[in]  injectStep Step (cell position) to corrupt, or -1 for none.
 * @param[out] outId      Receives the id on success; untouched on failure.
 * @param[out] outStep    Receives the failing step on failure.
 *
 * @return `QA_OK`, or the first non-`QA_OK` helper status.
 *
 * @owner No allocation; out-params are caller-owned.
 * @assumes `n` in [2, 4], so `n * n` and every position fit `unsigned int`.
 */
static QaStatus buildBoard(unsigned int n, unsigned int queenMask,
                           int injectStep, QaGridId *outId,
                           unsigned int *outStep)
{
    QaGridId id = 0;

    for (unsigned int pos = 0; pos < n * n; pos++) {
        QaGridId next = 0;
        /* Shift bound: `pos < n * n <= 16`, below the 32-bit width of
         * `unsigned int`, so the shift is defined. */
        unsigned int bit = (queenMask >> pos) & 1u;
        QaStatus status;

        if (injectStep >= 0 && pos == (unsigned int)injectStep) {
            bit = 2u; /* invalid on purpose: must fail with QA_ERR_RANGE */
        }
        /* Bound: `n` is 2..4 (non-zero) and `pos < n * n`, so `pos / n` and
         * `pos % n` are valid row and column indices below `n`. */
        status = qaGridWithBit(id, n, pos / n, pos % n, bit, &next);
        if (status != QA_OK) {
            *outStep = pos;
            return status;
        }
        id = next;
    }
    *outId = id;
    return QA_OK;
}

/**
 * @brief Check (b): `H_driver |id>` is 1 exactly at the single-cell toggles.
 *
 * Marks the expected ids by reading each cell with `qaGridGetBit` and writing
 * the opposite bit with `qaGridWithBit`, then compares all `dim` amplitudes.
 *
 * @param[in]     n     Board edge.
 * @param[in]     id    Built board id.
 * @param[in]     dim   State length.
 * @param[in]     out   `H_driver |id>`, length `dim`.
 * @param[in,out] mark  Scratch of `dim` bytes, zeroed by this function.
 *
 * @return Number of failed checks (helper failures count as one).
 *
 * @owner No allocation; buffers stay owned by the caller.
 * @assumes `id < dim`; every toggled id is below `dim` by the grid contract.
 */
static int checkToggles(unsigned int n, QaGridId id, size_t dim,
                        const complex double *out, unsigned char *mark)
{
    int failures = 0;

    /* Bound: `mark` holds `dim` bytes (caller contract), so the memset stays
     * inside it. */
    memset(mark, 0, dim);
    for (unsigned int pos = 0; pos < n * n; pos++) {
        unsigned int bit = 0;
        QaGridId toggled = 0;

        /* Bound: as in `buildBoard`, `pos / n` and `pos % n` are below `n`;
         * a successful `qaGridGetBit` yields 0 or 1, so `bit ^ 1u` is a valid
         * bit value. */
        if (qaGridGetBit(id, n, pos / n, pos % n, &bit) != QA_OK ||
            qaGridWithBit(id, n, pos / n, pos % n, bit ^ 1u, &toggled) !=
                QA_OK ||
            toggled >= dim) {
            printf("FAIL: TEST-004-driver-FR012 toggle helper step %u\n", pos);
            return 1;
        }
        /* Write bound: the `toggled >= dim` test above returned before this
         * point, so `toggled < dim`, the size of `mark`. */
        mark[toggled] = 1;
    }
    for (size_t k = 0; k < dim; k++) {
        double want = mark[k] ? 1.0 : 0.0;

        if (creal(out[k]) != want || cimag(out[k]) != 0.0) {
            printf("FAIL: TEST-004-driver-FR012 (b) amplitude %zu\n", k);
            failures++;
            break;
        }
    }
    return failures;
}

/**
 * @brief Run one board of `runForN` (checks (a) and (b)).
 *
 * @param[in] c Case to run (board size `n`, queen mask).
 * @param[in] injectStep Build step at which a failure is injected, or -1.
 * @param[in] dim State-space length `2^(n*n)`; all buffers hold `dim` entries.
 * @param[in] psi0 Initial state of the board.
 * @param[in,out] phi All-zero basis scratch on entry; one entry is set during
 *            the call and restored, so it is all-zero again on return.
 * @param[out] out Output of apply, overwritten.
 * @param[out] mark Scratch marks, `dim` entries, overwritten.
 * @return 0 on success, 1 when a check failed (printed), 2 when the board
 *         build failed (printed with the step; apply is not called).
 *
 * @owner No allocation; all buffers are caller-owned and `dim` long.
 * @assumes `psi0` holds the initial state for `c->n`.
 */
static int runBoard(const QaIntegrationCase *c, int injectStep, size_t dim,
                    const complex double *psi0, complex double *phi,
                    complex double *out, unsigned char *mark)
{
    QaGridId id = 0;
    unsigned int step = 0;
    unsigned int queens = 0;
    double s = 1.0 / sqrt((double)dim);
    QaStatus status;
    int failures = 0;

    status = buildBoard(c -> n, c -> queenMask, injectStep, &id, &step);
    if (status != QA_OK) {
        printf("build step %u failed for %s with status %d\n", step, c -> name,
               (int)status);
        return 2; /* Hamiltonian functions are not called */
    }
    if (id != c -> wantId) {
        printf("FAIL: TEST-004-driver-FR012 %s id %u != %u\n", c -> name,
               (unsigned)id, (unsigned)c -> wantId);
        return 1;
    }

    /* (a) the test counts the queens itself. */
    for (unsigned int pos = 0; pos < c -> n * c -> n; pos++) {
        /* Shift bound: `pos < n * n <= 16`, below the 32-bit width. */
        queens += (c -> queenMask >> pos) & 1u;
    }
    /* Read bound: `id == c->wantId` (checked above) and every `wantId` in the
     * case table is below `2^(n*n) == dim`; `psi0` and `phi` hold `dim`
     * entries. */
    if (fabs(creal(psi0[id]) - ((queens & 1u) ? -s : s)) > TOL_AMPLITUDE ||
        cimag(psi0[id]) != 0.0) {
        printf("FAIL: TEST-004-driver-FR012 (a) %s psi0[id]\n", c -> name);
        failures++;
    }

    /* (b) basis input, exactly normalized; reset afterwards. */
    phi[id] = 1.0;
    status = countedApply(c -> n, phi, out, dim);
    phi[id] = 0.0;
    if (status != QA_OK) {
        printf("FAIL: TEST-004-driver-FR012 %s apply status %d\n", c -> name,
               (int)status);
        return 1;
    }
    failures += checkToggles(c -> n, id, dim, out, mark);
    return failures != 0;
}

/**
 * @brief Run every board of one `N`, plus check (c) once.
 *
 * @param[in] n Board edge, 2..4.
 *
 * @return Number of failed boards or checks.
 *
 * @owner Allocates five buffers; the single `cleanup` label frees them on
 *        every path.
 * @assumes `dim = 2^(n*n)` fits `size_t` for `n <= 4`.
 */
static int runForN(unsigned int n)
{
    /* Shift bound: `main` calls this only with n in 2..4, so `n * n <= 16`
     * and `dim <= 65536` fits `size_t` without overflow. */
    size_t dim = (size_t)1 << (n * n);
    complex double *psi0 = calloc(dim, sizeof *psi0);
    complex double *hpsi0 = calloc(dim, sizeof *hpsi0);
    complex double *phi = calloc(dim, sizeof *phi);
    complex double *out = calloc(dim, sizeof *out);
    unsigned char *mark = calloc(dim, 1);
    double energy = 0.0;
    double target = -(double)(n * n);
    int failures = 0;

    if (psi0 == NULL || hpsi0 == NULL || phi == NULL || out == NULL ||
        mark == NULL) {
        printf("FAIL: TEST-004-driver-FR012 allocation for n=%u\n", n);
        failures = 1;
        goto cleanup;
    }
    if (qaHamiltonianInitialState(n, psi0, dim) != QA_OK ||
        countedApply(n, psi0, hpsi0, dim) != QA_OK) {
        printf("FAIL: TEST-004-driver-FR011 psi0 or apply for n=%u\n", n);
        failures = 1;
        goto cleanup;
    }

    /* (c) FR-011: eigen-equation per amplitude, and the energy. */
    /* Bound: `k < dim`; `psi0` and `hpsi0` were allocated with `dim` entries
     * and checked non-NULL in `runForN`. */
    for (size_t k = 0; k < dim; k++) {
        if (cabs(hpsi0[k] - target * psi0[k]) > TOL) {
            printf("FAIL: TEST-004-driver-FR011 n=%u amplitude %zu\n", n, k);
            failures++;
            break;
        }
        energy += creal(conj(psi0[k]) * hpsi0[k]);
    }
    if (fabs(energy - target) > TOL) {
        printf("FAIL: TEST-004-driver-FR011 n=%u energy %.17g\n", n, energy);
        failures++;
    }

    for (size_t i = 0; i < sizeof kCases / sizeof kCases[0]; i++) {
        if (kCases[i].n == n &&
            runBoard(&kCases[i], -1, dim, psi0, phi, out, mark) != 0) {
            failures++;
        }
    }
    printf("004-driver integration n=%u: %s\n", n, failures ? "FAILED" : "OK");

cleanup:
    free(psi0);
    free(hpsi0);
    free(phi);
    free(out);
    free(mark);
    return failures;
}

/**
 * @brief `build-failure` mode: an invalid bit at one build step fails the
 * case, reports the step, leaves the receiver alone and never calls apply.
 *
 * @return Number of failed expectations.
 *
 * @owner Stack buffers only (a 16-element state for `N = 2`).
 * @assumes Step 1 of the `n = 2` build exists and the helper rejects bit 2.
 */
static int runBuildFailure(void)
{
    complex double psi0[16];
    complex double phi[16] = {0};
    complex double out[16];
    unsigned char mark[16];
    QaGridId id = 0xDEADBEEFu;
    unsigned int step = 99;
    unsigned int before = gApplyCalls;
    int failures = 0;

    if (qaHamiltonianInitialState(2, psi0, 16) != QA_OK) {
        printf("FAIL: TEST-004-driver-FR012 build-failure setup\n");
        return 1;
    }
    before = gApplyCalls;
    if (buildBoard(2, 0x9, 1, &id, &step) != QA_ERR_RANGE || step != 1u ||
        id != 0xDEADBEEFu) {
        printf("FAIL: TEST-004-driver-FR012 injected failure not reported\n");
        failures++;
    }
    /* `kCases[2]` is the n = 2 board, so its state has 2^4 = 16 entries; 16
     * is also the length of `psi0`, `phi`, `out` and `mark` here. */
    if (runBoard(&kCases[2], 1, 16, psi0, phi, out, mark) != 2) {
        printf("FAIL: TEST-004-driver-FR012 failed build must fail the case\n");
        failures++;
    }
    if (gApplyCalls != before) {
        printf("FAIL: TEST-004-driver-FR012 apply called on partial id\n");
        failures++;
    }
    if (failures == 0) {
        printf("004-driver integration-build-failure: OK (step 1 reported; "
               "apply never called)\n");
    }
    return failures;
}

/**
 * @brief Run the integration checks for N = 2..4.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Optional `build-failure` selector at `argv[1]`.
 * @return 0 when every check passes, 1 on failures, 2 on a usage error.
 *
 * @owner No resource is kept; `runForN` frees its buffers.
 * @assumes N is limited to 2..4 by this loop (SCP-1 for the exact study).
 */
int main(int argc, char **argv)
{
    int failures = 0;

    if (argc > 1) {
        if (strcmp(argv[1], "build-failure") != 0) {
            printf("usage: test-004-integration [build-failure]\n");
            return 2;
        }
        return runBuildFailure() != 0;
    }
    for (unsigned int n = 2; n <= 4; n++) {
        failures += runForN(n);
    }
    return failures != 0;
}
