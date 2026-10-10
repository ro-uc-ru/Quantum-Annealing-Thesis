/*
 * 005-evolution integration tests (task T-015; FR-007, FR-008, FR-023, EC-004,
 * EC-013).
 *
 * Purpose: validate the scientific outcomes and the reproducibility of the whole
 * evolution stack (002 `H_target`, 003 schedules, 004 initial state, 005 Strang
 * driver and CLI). Groups, one CTest entry each:
 *   convergence  FR-007: 2x2, linear, `T = 10`, steps 100/200/400/800 against the
 *                `steps = 12800` reference; the error is the L2 norm of the
 *                difference of the final states; every ratio of consecutive
 *                errors must lie in `[3.6, 4.4]` while the error exceeds `1e-10`,
 *                and at least two ratios must be checked.
 *   reference    FR-023: linear, `T = 100`, `steps = 10^4`; `P_gs` (computed here
 *                from `qaHamiltonianTarget`, never from the library) and
 *                `<H(T)>` agree with the four hard-coded constants of the spec
 *                within `1e-6` (3x3 and 2x2). No threshold on `P_gs` itself.
 *   repeat       FR-008, EC-013: two CLI runs with equal arguments give
 *                byte-identical `trace.csv`, snapshots and `config.json`, and two
 *                library runs give bit-identical states.
 *   n4           EC-004: a 4x4 run, non-blocking: its outcome and numbers are
 *                printed and recorded, never asserted.
 *
 * Independence: the reference constants come from `tests/reference/proto.py` (NumPy,
 * not run by CTest); the ground set and `P_gs` are computed here from the 002/005
 * `H_target`, and the byte comparison reads the files with plain `fread`.
 * Ownership: every heap buffer is allocated and freed by the function that owns
 * it through one `cleanup` path; the scratch directory made by `mkdtemp` is
 * removed at the end of the `repeat` group. Errors: a failed check prints its
 * spec tag and the process exits 1; a usage error exits 2.
 * Numerical assumptions: the agreement tolerance `1e-6` is that of FR-023; the
 * ratio band is that of FR-007. Determinism: fixed inputs, no clock, RNG or
 * environment (TST-3).
 */

#define _XOPEN_SOURCE 700 /* nftw */
#define _POSIX_C_SOURCE 200809L /* mkdtemp */
#define _DARWIN_C_SOURCE /* macOS only: mkdtemp stays visible with the macros above */

#include <complex.h>
#include <dirent.h>
#include <ftw.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/evolution/evolve.h"
#include "qa/hamiltonian/driver.h"
#include "qa/hamiltonian/target.h"
#include "qa-005-evolve-cli.h"

#define PATH_CAP 1024u
#define FILE_CAP (4u * 1024u * 1024u)

/* FR-023 agreement tolerance. */
#define REF_TOL 1e-6
/* FR-007 band of the ratio of consecutive errors and its error floor. */
#define RATIO_LO 3.6
#define RATIO_HI 4.4
#define ERR_FLOOR 1e-10

/* Scratch directory of the `repeat` group; removed by `cleanScratch`. */
static char gScratch[PATH_CAP];

/**
 * @brief Report a failed check and exit 1.
 *
 * @param[in] tag  Spec tag of the check, non-NULL.
 * @param[in] what Detail text, non-NULL.
 *
 * @return Does not return.
 *
 * @owner No allocation. @assumes `stderr` is open.
 */
static void failCheck(const char *tag, const char *what)
{
    fprintf(stderr, "FAIL [%s]: %s\n", tag, what);
    exit(1);
}

/** Require `cond`; otherwise `failCheck` with `tag` and `what`. */
#define REQUIRE(cond, tag, what) \
    do { \
        if (!(cond)) { \
            failCheck((tag), (what)); \
        } \
    } while (0)

/**
 * @brief Evolve the 004 initial state of an `n x n` board with the linear
 *        family and no callbacks (pure evolution).
 *
 * @param[in]  n       Board edge, `2..4`.
 * @param[in]  T       Total time, finite and positive.
 * @param[in]  steps   Strang steps, `1..QA_EVOLVE_STEPS_MAX`.
 * @param[out] outPsi  Caller-owned receiver of `dim = 2^(n n)` amplitudes.
 * @param[out] outSummary Receiver of the run summary, non-NULL.
 *
 * @return The `QaStatus` of the first failing step (`QA_OK` on success).
 *
 * @owner The initial buffer is allocated and freed here; `outPsi` stays owned by
 *        the caller. @assumes `n` in `2..4`, so `dim <= 65536` and the shift
 *        cannot overflow `size_t`.
 */
static QaStatus evolveLinear(unsigned int n, double T, size_t steps,
                             complex double *outPsi, QaEvolveSummary *outSummary)
{
    QaEvolveParams params = {n, QA_SCHEDULE_LINEAR, T, steps, 0u};
    /* Pure evolution (FR-007): both callbacks NULL, the struct itself is not. */
    QaEvolveCallbacks none = {NULL, NULL, NULL, NULL};
    /* Bound: n is 2..4 at every caller, so n * n <= 16 and the shift and dim * sizeof cannot overflow. */
    size_t dim = (size_t)1 << (n * n);
    complex double *initial = malloc(dim * sizeof *initial);
    QaStatus status;

    if (initial == NULL) {
        return QA_ERR_NOMEM;
    }
    status = qaHamiltonianInitialState(n, initial, dim);
    if (status == QA_OK) {
        status = qaEvolve(&params, initial, outPsi, dim, &none, outSummary);
    }
    free(initial);
    return status;
}

/**
 * @brief Probability `P_gs` of the ground set of `H_target` in state `psi`.
 *
 * The ground set is every `k` whose `H_target(k)` equals the minimum (exact
 * comparison: the values are multiples of 0.5, see `target.h`). Two passes: the
 * minimum, then the sum of `|psi_k|^2` over the ground set.
 *
 * @param[in]  n      Board edge, `2..4`.
 * @param[in]  psi    State of `dim = 2^(n n)` amplitudes, read only.
 * @param[out] outP   Receiver of `P_gs`, non-NULL.
 *
 * @return `QA_OK`, or the `QaStatus` of `qaHamiltonianTarget`.
 *
 * @owner No allocation. @assumes `dim` matches `n`; `outP` is written only on
 *        success.
 */
static QaStatus groundProbability(unsigned int n, const complex double *psi,
                                  double *outP)
{
    /* Bound: n is 2..4 at every caller, so n * n <= 16 and the shift cannot overflow. */
    size_t dim = (size_t)1 << (n * n);
    double minValue = 0.0;
    double sum = 0.0;
    double value;
    QaStatus status;

    for (size_t k = 0; k < dim; k++) {
        status = qaHamiltonianTarget(n, k, &value);
        if (status != QA_OK) {
            return status;
        }
        if (k == 0 || value < minValue) {
            minValue = value;
        }
    }
    for (size_t k = 0; k < dim; k++) {
        status = qaHamiltonianTarget(n, k, &value);
        if (status != QA_OK) {
            return status;
        }
        if (value == minValue) {
            sum += creal(psi[k]) * creal(psi[k]) + cimag(psi[k]) * cimag(psi[k]);
        }
    }
    *outP = sum;
    return QA_OK;
}

/**
 * @brief L2 norm of the difference of two states of length `dim`.
 *
 * @param[in] a,b States, read only. @param[in] dim Their common length.
 *
 * @return `sqrt(sum |a_k - b_k|^2)`.
 *
 * @owner No allocation. @assumes Plain summation: the errors compared are above
 *        `1e-10` and the sum has at most 16 terms of rounding noise `~1e-16`.
 */
static double l2Difference(const complex double *a, const complex double *b,
                           size_t dim)
{
    double sum = 0.0;

    for (size_t k = 0; k < dim; k++) {
        double dr = creal(a[k]) - creal(b[k]);
        double di = cimag(a[k]) - cimag(b[k]);

        sum += dr * dr + di * di;
    }
    return sqrt(sum);
}

/**
 * @brief FR-007: order-2 convergence on 2x2, linear, `T = 10`.
 *
 * Errors of steps 100, 200, 400, 800 against the 12800-step state; ratios of
 * consecutive errors must lie in `[RATIO_LO, RATIO_HI]` while the error exceeds
 * `ERR_FLOOR`, and at least two ratios must have been checked.
 *
 * @return Nothing; a failed `REQUIRE` prints its spec tag and exits 1.
 *
 * @owner `ref` and `psi` are stack arrays of `DIM` amplitudes, so nothing is
 *        allocated or freed here.
 * @assumes `dim = 16`; the reference is the finest run, so the 800-step error is
 *          computed against a run 16 times finer.
 */
static void testConvergence(void)
{
    static const size_t kSteps[] = {100, 200, 400, 800};
    enum { COUNT = 4, DIM = 16 };
    complex double ref[DIM];
    complex double psi[DIM];
    QaEvolveSummary summary;
    double errors[COUNT];
    int checked = 0;

    /* Bound: `n = 2` gives `dim == 16 == DIM`, the length of `ref` and `psi`. */
    REQUIRE(evolveLinear(2, 10.0, 12800, ref, &summary) == QA_OK, "FR-007",
            "reference run failed");
    /* Bound: i < COUNT, the length of kSteps[] and errors[]. */
    for (size_t i = 0; i < COUNT; i++) {
        REQUIRE(evolveLinear(2, 10.0, kSteps[i], psi, &summary) == QA_OK, "FR-007",
                "coarse run failed");
        errors[i] = l2Difference(psi, ref, DIM);
        printf("convergence steps=%zu error=%.6e\n", kSteps[i], errors[i]);
    }
    for (size_t i = 1; i < COUNT; i++) {
        double ratio;

        /* Bound: the error floor guards the division and the noise regime. */
        /* Bound: i >= 1 from the loop start, so errors[i - 1] does not underflow. */
        if (errors[i - 1] <= ERR_FLOOR || errors[i] <= 0.0) {
            continue;
        }
        ratio = errors[i - 1] / errors[i];
        printf("convergence ratio[%zu]=%.4f\n", i, ratio);
        REQUIRE(ratio >= RATIO_LO && ratio <= RATIO_HI, "FR-007",
                "ratio outside [3.6, 4.4]");
        checked++;
    }
    REQUIRE(checked >= 2, "FR-007", "fewer than two ratios checked");
}

/**
 * @brief FR-023: one linear `T = 100`, `steps = 10^4` run against two constants.
 *
 * @param[in] n        Board edge, `2` or `3`.
 * @param[in] wantP    Reference `P_gs`. @param[in] wantE Reference `<H(T)>`.
 *
 * @return Nothing; a failed `REQUIRE` prints its spec tag and exits 1.
 *
 * @owner The state buffer is allocated here and freed on every path.
 * @assumes `dim <= 512`; the tolerance is `REF_TOL` (FR-023).
 */
static void checkReference(unsigned int n, double wantP, double wantE)
{
    /* Bound: n is 2..4 at every caller, so n * n <= 16 and the shift and dim * sizeof cannot overflow. */
    size_t dim = (size_t)1 << (n * n);
    complex double *psi = malloc(dim * sizeof *psi);
    QaEvolveSummary summary;
    double p = 0.0;

    REQUIRE(psi != NULL, "setup", "allocation failed");
    if (evolveLinear(n, 100.0, 10000, psi, &summary) != QA_OK
        || groundProbability(n, psi, &p) != QA_OK) {
        free(psi);
        failCheck("FR-023", "evolution or ground-set computation failed");
    }
    free(psi);
    printf("reference N=%u P_gs=%.7f (want %.7f) energy=%.7f (want %.7f)\n", n, p,
           wantP, summary.finalEnergy, wantE);
    REQUIRE(fabs(p - wantP) <= REF_TOL, "FR-023", "P_gs disagrees with reference");
    REQUIRE(fabs(summary.finalEnergy - wantE) <= REF_TOL, "FR-023",
            "<H(T)> disagrees with reference");
}

/**
 * @brief FR-023: compare the 3x3 and 2x2 runs with the reference constants.
 *
 * @return Nothing; a failed `REQUIRE` aborts the test with a diagnostic.
 * @owner No allocation of its own; `checkReference` owns its buffers.
 * @assumes The constants are the spec values to 7 decimals, compared with
 *          `REF_TOL`.
 */
static void testReference(void)
{
    checkReference(3, 0.9868132, -0.9932748);
    checkReference(2, 0.9781068, -0.4890516);
}

/**
 * @brief Remove one entry of the scratch tree (`nftw` callback).
 *
 * @param[in] path Entry path. @param[in] sb,type,ftw Unused.
 *
 * @return 0 always, so the walk continues after a failed removal.
 *
 * @owner No allocation. @assumes Called depth first (`FTW_DEPTH`).
 */
static int removeEntry(const char *path, const struct stat *sb, int type,
                       struct FTW *ftw)
{
    (void)sb;
    (void)type;
    (void)ftw;
    (void)remove(path);
    return 0;
}

/**
 * @brief Create the scratch directory under `base`.
 *
 * @param[in] base Existing directory, non-NULL.
 *
 * @return Nothing; a failed `REQUIRE` prints its spec tag and exits 1.
 *
 * @owner `gScratch` is static; `cleanScratch` removes the tree.
 * @assumes The template fits `PATH_CAP`, checked.
 */
static void makeScratch(const char *base)
{
    int len = snprintf(gScratch, sizeof gScratch, "%s/005-int-XXXXXX", base);

    /* Bound: the template and its terminator fit `gScratch`. */
    REQUIRE(len > 0 && (size_t)len < sizeof gScratch, "setup", "base too long");
    REQUIRE(mkdtemp(gScratch) != NULL, "setup", "mkdtemp failed");
}

/**
 * @brief Remove the scratch directory and everything below it.
 *
 * @return Nothing; removal errors are ignored because this is best-effort
 *         test cleanup after the checks already ran.
 * @owner Frees nothing; `gScratch` is a static buffer owned by this file.
 * @assumes `gScratch` holds the path made by the setup (or is empty, in which
 *          case `nftw` fails harmlessly) and `removeEntry` unlinks one entry.
 */
static void cleanScratch(void)
{
    (void)nftw(gScratch, removeEntry, 16, FTW_DEPTH | FTW_PHYS);
}

/**
 * @brief Read a whole file into `buf`.
 *
 * @param[in]  path File to read. @param[out] buf Receiver. @param[in] cap Its size.
 *
 * @return The byte count, or -1 if the file is missing, unreadable or longer
 *         than `cap`.
 *
 * @owner The `FILE` is closed on every path. @assumes A longer file is rejected,
 *        so `fread` never exceeds `cap` (MEM-5).
 */
static long readFile(const char *path, char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t got;

    if (fp == NULL) {
        return -1;
    }
    got = fread(buf, 1, cap, fp);
    /* Bound: `got <= cap`; the file must end exactly there or sooner. */
    if (ferror(fp) || !feof(fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return (long)got;
}

/**
 * @brief Run the CLI with the given words, discarding its streams.
 *
 * @param[in] n,family,T,steps,m Argument words. @param[in] dir Run directory.
 *
 * @return The CLI exit status.
 *
 * @owner The two temporary streams are closed on every path.
 * @assumes `tmpfile` succeeds, checked.
 */
static int runCli(const char *n, const char *family, const char *T,
                  const char *steps, const char *m, const char *dir)
{
    char *argv[] = {"qa-005-evolve", (char *)n, (char *)family, (char *)T,
                    (char *)steps, (char *)m, "--out", (char *)dir};
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    int code;

    REQUIRE(out != NULL && err != NULL, "setup", "tmpfile failed");
    /* Bound: the literal 8 equals the number of entries in argv[] above. */
    code = qaCliEvolveRun(8, argv, out, err, NULL);
    fclose(out);
    fclose(err);
    return code;
}

/**
 * @brief Require the file `<dirA>/<rel>` to equal `<dirB>/<rel>` byte for byte.
 *
 * @param[in] dirA,dirB Run directories. @param[in] rel Relative file name.
 * @param[in] bufA,bufB Scratch buffers of `FILE_CAP` bytes.
 *
 * @return Nothing; a failed `REQUIRE` prints its spec tag and exits 1.
 *
 * @owner Buffers belong to the caller. @assumes Both paths fit `PATH_CAP`.
 */
static void requireSameFile(const char *dirA, const char *dirB, const char *rel,
                            char *bufA, char *bufB)
{
    char pathA[PATH_CAP];
    char pathB[PATH_CAP];
    long lenA;
    long lenB;
    int la = snprintf(pathA, sizeof pathA, "%s/%s", dirA, rel);
    int lb = snprintf(pathB, sizeof pathB, "%s/%s", dirB, rel);

    /* Bound: both formatted paths fit `PATH_CAP`. */
    REQUIRE(la > 0 && (size_t)la < sizeof pathA && lb > 0 && (size_t)lb < sizeof pathB,
            "setup", "path too long");
    lenA = readFile(pathA, bufA, FILE_CAP);
    lenB = readFile(pathB, bufB, FILE_CAP);
    REQUIRE(lenA >= 0 && lenB >= 0, "EC-013", rel);
    /* Bound: `lenA <= FILE_CAP` (`readFile` never reads more), the size of both buffers. */
    REQUIRE(lenA == lenB && memcmp(bufA, bufB, (size_t)lenA) == 0, "EC-013", rel);
}

/**
 * @brief Count the entries of a directory other than `.` and `..`.
 *
 * @param[in] path Directory. @return The count, or -1 if unreadable.
 *
 * @owner The `DIR` is closed on every path. @assumes None.
 */
static long countEntries(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    long count = 0;

    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry -> d_name, ".") != 0 && strcmp(entry -> d_name, "..") != 0) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

/**
 * @brief FR-008, EC-013: repeated CLI and library runs are identical.
 *
 * @param[in] base Existing directory for the scratch tree.
 *
 * @return Nothing; a failed `REQUIRE` prints its spec tag and exits 1.
 *
 * @owner The two comparison buffers are allocated here and freed on every path;
 *        the scratch tree is removed at the end.
 * @assumes `config.json` embeds only build-time values and the arguments (not
 *          the run directory), as EC-013 requires on the same build.
 */
static void testRepeat(const char *base)
{
    char dirA[PATH_CAP];
    char dirB[PATH_CAP];
    char snapsA[PATH_CAP];
    char snapsB[PATH_CAP];
    char *bufA = malloc(FILE_CAP);
    char *bufB = malloc(FILE_CAP);
    /* `evolveLinear(3, ...)` writes `dim = 2^9 = 512` amplitudes, exactly the
     * length of `psiA` and `psiB`. */
    complex double psiA[512];
    complex double psiB[512];
    QaEvolveSummary sumA;
    QaEvolveSummary sumB;
    DIR *dir;
    struct dirent *entry;

    REQUIRE(bufA != NULL && bufB != NULL, "setup", "allocation failed");
    makeScratch(base);
    /* Bound: `gScratch` plus `/a`, `/b` and `/snapshots` fit `PATH_CAP` because
     * `makeScratch` checked the scratch path and the suffixes are short; a
     * truncation would make `countEntries` or `requireSameFile` fail. */
    snprintf(dirA, sizeof dirA, "%s/a", gScratch);
    snprintf(dirB, sizeof dirB, "%s/b", gScratch);
    snprintf(snapsA, sizeof snapsA, "%s/snapshots", dirA);
    snprintf(snapsB, sizeof snapsB, "%s/snapshots", dirB);

    REQUIRE(runCli("3", "linear", "20", "400", "100", dirA) == 0, "EC-013", "run A");
    REQUIRE(runCli("3", "linear", "20", "400", "100", dirB) == 0, "EC-013", "run B");
    requireSameFile(dirA, dirB, "trace.csv", bufA, bufB);
    requireSameFile(dirA, dirB, "config.json", bufA, bufB);
    REQUIRE(countEntries(snapsA) == 5 && countEntries(snapsB) == 5, "FR-012",
            "M = 100, steps = 400: five snapshots");
    dir = opendir(snapsA);
    REQUIRE(dir != NULL, "setup", "opendir snapshots");
    while ((entry = readdir(dir)) != NULL) {
        char rel[PATH_CAP];

        if (strcmp(entry -> d_name, ".") == 0 || strcmp(entry -> d_name, "..") == 0) {
            continue;
        }
        /* Bound: the entry name is at most 255 bytes, far below `PATH_CAP`. */
        snprintf(rel, sizeof rel, "snapshots/%s", entry -> d_name);
        requireSameFile(dirA, dirB, rel, bufA, bufB);
    }
    closedir(dir);
    printf("repeat: trace, config and 5 snapshots byte-identical\n");

    /* FR-008: two library runs agree bit for bit on the final state. */
    REQUIRE(evolveLinear(3, 20.0, 400, psiA, &sumA) == QA_OK, "FR-008", "run A");
    REQUIRE(evolveLinear(3, 20.0, 400, psiB, &sumB) == QA_OK, "FR-008", "run B");
    REQUIRE(memcmp(psiA, psiB, sizeof psiA) == 0, "FR-008", "states differ");
    REQUIRE(memcmp(&sumA, &sumB, sizeof sumA) == 0, "FR-008", "summaries differ");

    cleanScratch();
    free(bufA);
    free(bufB);
}

/**
 * @brief EC-004: a 4x4 run, non-blocking; the outcome is recorded, not asserted.
 *
 * Linear, `T = 100`, `steps = 1000`. Prints the status, `P_gs`, `<H(T)>` and the
 * maximum norm drift; a non-`QA_OK` status is printed as a finding and does not
 * fail the test (the DoD marks this run non-blocking).
 *
 * @return Nothing; a failed `REQUIRE` prints its spec tag and exits 1.
 *
 * @owner The state buffer is allocated here and freed on every path.
 * @assumes `dim = 65536` (1 MiB of amplitudes).
 */
static void testN4(void)
{
    /* Bound: the shift is the literal 16 (n = 4), so dim = 65536 and dim * sizeof cannot overflow. */
    size_t dim = (size_t)1 << 16;
    complex double *psi = malloc(dim * sizeof *psi);
    QaEvolveSummary summary = {0.0, 0.0};
    double p = 0.0;
    QaStatus status;

    REQUIRE(psi != NULL, "setup", "allocation failed");
    status = evolveLinear(4, 100.0, 1000, psi, &summary);
    if (status == QA_OK && groundProbability(4, psi, &p) == QA_OK) {
        printf("n4 RECORDED: status=0 P_gs=%.7f energy=%.7f max_norm_drift=%.3e "
               "(norm bound %s)\n", p, summary.finalEnergy, summary.maxNormDrift,
               summary.maxNormDrift <= QA_EVOLVE_NORM_TOL ? "kept" : "EXCEEDED");
    } else {
        printf("n4 RECORDED: status=%d (non-blocking, EC-004)\n", (int)status);
    }
    free(psi);
}

/**
 * @brief Entry point: `test-005-integration <convergence|reference|repeat|n4> [dir]`.
 *
 * @param[in] argc,argv Group name and, for `repeat`, an existing base directory.
 *
 * @return 0 when the group passes, 2 on a usage error (a failed check exits 1).
 *
 * @owner No allocation. @assumes None.
 */
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test-005-integration <convergence|reference|repeat|n4> [dir]\n");
        return 2;
    }
    if (strcmp(argv[1], "convergence") == 0) {
        testConvergence();
    } else if (strcmp(argv[1], "reference") == 0) {
        testReference();
    } else if (strcmp(argv[1], "repeat") == 0 && argc == 3) {
        testRepeat(argv[2]);
    } else if (strcmp(argv[1], "n4") == 0) {
        testN4();
    } else {
        fprintf(stderr, "unknown group or missing directory: %s\n", argv[1]);
        return 2;
    }
    printf("test-005-integration %s: OK\n", argv[1]);
    return 0;
}
