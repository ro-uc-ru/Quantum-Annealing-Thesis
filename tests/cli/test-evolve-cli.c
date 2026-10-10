/*
 * 005-evolution CLI tests (spec 005-evolution, task T-014).
 *
 * Purpose: drive `qaCliEvolveRun` (the logic of `qa-005-evolve`) through four
 * groups selected by the first argument; the second argument is an existing
 * directory under which a scratch directory is created and removed.
 *   usage    EC-016: every malformed word gives exit 64, prints the usage text and
 *            creates nothing.
 *   codes    FR-015, FR-016, FR-017, FR-020, EC-007, EC-015, EC-017: the library
 *            and `--out` codes 1..6, each with nothing written (a rejected
 *            `--out` leaves its content untouched).
 *   layout   FR-013, FR-014, FR-015, section 4: a valid 2x2 run (with `M = 5` and
 *            with `M = 0`): exit 0, summary, files, header rows, invariants.
 *   perturb  EC-020, FR-006: the test-only hook breaks the norm at step 7; exit 5,
 *            trace rows `0..6`, snapshots `< 7` only, no `config.json`.
 *
 * Independence: the readers below are small hand-written parsers that share no
 * code with the writers of `src/io/run.c`.
 * Ownership: every buffer is static or released on the path that allocates it;
 * the scratch directory is removed at the end of each group. Errors: a failed
 * check prints its tag and the process exits 1; a usage error exits 2.
 * Determinism: no clock and no RNG; the scratch name comes from `mkdtemp`, never
 * from a record value.
 * Numerical assumptions: the norm bound is `1e-12` (ARC-4); `energy(0) = -4` for
 * 2x2 is compared with `1e-9`, looser than the spec's `4 SCHED_TOL + 1e-12`.
 */

#define _XOPEN_SOURCE 700 /* nftw */
#define _POSIX_C_SOURCE 200809L /* mkdtemp, strtok_r */
#define _DARWIN_C_SOURCE /* macOS only: mkdtemp stays visible with the macros above */

#include <dirent.h>
#include <ftw.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa-005-evolve-cli.h"

#define PATH_CAP 1024u
#define BUF_CAP (64u * 1024u)
#define NORM_BOUND 1e-12

/* Scratch directory of the running group; removed by `cleanScratch`. */
static char gScratch[PATH_CAP];
/* Output captured from the last `runCli` call. */
static char gOut[BUF_CAP];
static char gErr[BUF_CAP];

/**
 * @brief Report a failed check and exit 1.
 *
 * @param[in] tag  Short check name with its spec tag, non-NULL.
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
    do { if (!(cond)) { failCheck((tag), (what)); } } while (0)

/**
 * @brief Remove one tree entry; callback of `nftw` (cleanup of the scratch tree).
 *
 * @param[in] path Entry path. @param[in] sb Unused. @param[in] type Unused.
 * @param[in] ftwbuf Unused.
 *
 * @return 0 always, so the walk continues; a leftover is reported by the caller.
 *
 * @owner No allocation. @assumes The walk is depth first (`FTW_DEPTH`), so a
 *        directory is empty when it is removed.
 */
static int removeEntry(const char *path, const struct stat *sb, int type,
                       struct FTW *ftwbuf)
{
    (void)sb;
    (void)type;
    (void)ftwbuf;
    (void)remove(path);
    return 0;
}

/**
 * @brief Create the scratch directory under `base` (fixture setup).
 *
 * @param[in] base Existing directory, non-NULL.
 *
 * @return Nothing; `gScratch` holds the new directory, or the process exits 1.
 *
 * @owner The directory is removed by `cleanScratch`.
 * @assumes `base` is short enough for `PATH_CAP`; checked with `snprintf`.
 */
static void makeScratch(const char *base)
{
    int len = snprintf(gScratch, sizeof gScratch, "%s/005-cli-XXXXXX", base);

    /* Bound: the template and its terminator fit `gScratch`. */
    REQUIRE(len > 0 && (size_t)len < sizeof gScratch, "setup", "base too long");
    REQUIRE(mkdtemp(gScratch) != NULL, "setup", "mkdtemp failed");
}

/**
 * @brief Remove the scratch directory and everything below it.
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner No allocation. @assumes `gScratch` was set by `makeScratch`.
 */
static void cleanScratch(void)
{
    (void)nftw(gScratch, removeEntry, 16, FTW_DEPTH | FTW_PHYS);
}

/**
 * @brief Format `<scratch>/<name>` into `outPath`.
 *
 * @param[out] outPath Buffer of `PATH_CAP` bytes. @param[in] name Relative name.
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner No allocation. @assumes The result fits; checked, else exit 1.
 */
static void scratchPath(char *outPath, const char *name)
{
    int len = snprintf(outPath, PATH_CAP, "%s/%s", gScratch, name);

    /* Bound: the formatted path fits `PATH_CAP`. */
    REQUIRE(len > 0 && (size_t)len < PATH_CAP, "setup", "path too long");
}

/**
 * @brief Whether a path exists (any type, symlinks not followed).
 *
 * @param[in] path Path, non-NULL. @return 1 if `lstat` succeeds, else 0.
 *
 * @owner No allocation. @assumes None.
 */
static int pathExists(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}

/**
 * @brief Count the entries of a directory other than `.` and `..`.
 *
 * @param[in] path Directory, non-NULL. @return The count, or -1 if unreadable.
 *
 * @owner The `DIR` is closed on every path. @assumes None.
 */
static int countEntries(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int count = 0;

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
 * @brief Read a whole text file into `buf`.
 *
 * @param[in]  path Non-NULL. @param[out] buf Buffer. @param[in] cap Its size.
 *
 * @return Length read, or -1 when missing or over `cap - 1`.
 *
 * @owner The `FILE` is closed on every path. @assumes The file is small; a
 *        longer one is treated as a failure, never truncated silently.
 */
static long readFile(const char *path, char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t got;

    if (fp == NULL) {
        return -1;
    }
    got = fread(buf, 1, cap - 1, fp);
    /* Bound: `got <= cap - 1`, so the NUL fits; a longer file is rejected. */
    if (ferror(fp) || !feof(fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[got] = '\0';
    return (long)got;
}

/**
 * @brief Run the CLI with the given words and capture both streams.
 *
 * @param[in] argc  Word count. @param[in] argv Words.
 * @param[in] ops   Hook table or NULL.
 *
 * @return The exit status of `qaCliEvolveRun`; `gOut` and `gErr` hold the text.
 *
 * @owner The two temporary streams are closed on every path.
 * @assumes The captured text fits `BUF_CAP`; a longer one is cut at the cap.
 */
static int runCli(int argc, char **argv, const QaEvolveOps *ops)
{
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    int status;
    size_t got;

    REQUIRE(out != NULL && err != NULL, "setup", "tmpfile failed");
    status = qaCliEvolveRun(argc, argv, out, err, ops);
    rewind(out);
    rewind(err);
    got = fread(gOut, 1, sizeof gOut - 1, out);
    gOut[got] = '\0'; /* Bound: got <= sizeof gOut - 1. */
    got = fread(gErr, 1, sizeof gErr - 1, err);
    gErr[got] = '\0'; /* Bound: got <= sizeof gErr - 1. */
    fclose(out);
    fclose(err);
    return status;
}

/**
 * @brief Run `qa-005-evolve <n> <family> <T> <steps> <m> --out <dir>`.
 *
 * @param[in] n,family,T,steps,m The five words. @param[in] dir The `--out` path.
 * @param[in] ops Hook table or NULL.
 *
 * @return The exit status.
 *
 * @owner No allocation. @assumes All words are non-NULL.
 */
static int runWords(const char *n, const char *family, const char *T,
                    const char *steps, const char *m, const char *dir,
                    const QaEvolveOps *ops)
{
    char *argv[] = {"qa-005-evolve", (char *)n, (char *)family, (char *)T,
                    (char *)steps, (char *)m, "--out", (char *)dir};

    return runCli(8, argv, ops);
}

/**
 * @brief EC-016: malformed words exit 64 and create nothing.
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner Scratch removed by the caller. @assumes `gScratch` is set.
 */
static void testUsage(void)
{
    char dir[PATH_CAP];
    char *shortArgv[] = {"qa-005-evolve", "2", "linear", "10", "20", "5", "--out"};
    char *badFlag[] = {"qa-005-evolve", "2", "linear", "10", "20", "5", "-o", dir};
    const char *bad[][5] = {
        {"x", "linear", "10", "20", "5"},       /* N not a number */
        {"2x", "linear", "10", "20", "5"},      /* trailing text */
        {"-2", "linear", "10", "20", "5"},      /* negative N */
        {"+2", "linear", "10", "20", "5"},      /* explicit sign */
        {" 2", "linear", "10", "20", "5"},      /* leading space */
        {"", "linear", "10", "20", "5"},        /* empty */
        {"99999999999999999999999", "linear", "10", "20", "5"}, /* overflow */
        {"2", "bogus", "10", "20", "5"},        /* unknown family */
        {"2", "Linear", "10", "20", "5"},       /* names are exact */
        {"2", "linear", "abc", "20", "5"},      /* T not a real */
        {"2", "linear", "1e", "20", "5"},       /* incomplete exponent */
        {"2", "linear", "10 ", "20", "5"},      /* trailing space */
        {"2", "linear", " 10", "20", "5"},      /* leading space */
        {"2", "linear", "", "20", "5"},         /* empty T */
        {"2", "linear", "10", "1.5", "5"},      /* steps not an integer */
        {"2", "linear", "10", "-1", "5"},       /* negative steps */
        {"2", "linear", "10", "20x", "5"},      /* trailing text */
        {"2", "linear", "10", "20", "-1"},      /* negative M */
        {"2", "linear", "10", "20", "5x"},      /* trailing text */
    };

    scratchPath(dir, "run");
    REQUIRE(runCli(1, (char *[]){"qa-005-evolve"}, NULL) == 64, "EC-016", "no arguments");
    REQUIRE(runCli(7, shortArgv, NULL) == 64, "EC-016", "missing directory");
    REQUIRE(runCli(8, badFlag, NULL) == 64, "EC-016", "wrong flag");
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int status = runWords(bad[i][0], bad[i][1], bad[i][2], bad[i][3], bad[i][4],
                              dir, NULL);

        REQUIRE(status == 64, "EC-016", bad[i][0]);
        REQUIRE(strstr(gErr, "usage:") != NULL, "EC-016", "usage text missing");
        REQUIRE(gOut[0] == '\0', "EC-016", "stdout must stay empty");
        REQUIRE(!pathExists(dir), "EC-016", "usage error created the run dir");
    }
    REQUIRE(countEntries(gScratch) == 0, "EC-016", "usage error wrote into scratch");
}

/**
 * @brief Expect `status` from one run and no run directory afterwards.
 *
 * @param[in] tag Check name. @param[in] status Expected exit status.
 * @param[in] n,family,T,steps,m The five words. @param[in] ops Hook table or NULL.
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner No allocation. @assumes The run dir `<scratch>/run` does not exist before.
 */
static void expectRejected(const char *tag, int status, const char *n,
                           const char *family, const char *T, const char *steps,
                           const char *m, const QaEvolveOps *ops)
{
    char dir[PATH_CAP];

    scratchPath(dir, "run");
    REQUIRE(runWords(n, family, T, steps, m, dir, ops) == status, tag, gErr);
    REQUIRE(gOut[0] == '\0', tag, "stdout must stay empty on failure");
    REQUIRE(!pathExists(dir), tag, "a rejected run created the run dir");
}

/**
 * @brief Allocation hook that always fails (FR-025, NOMEM path of the CLI).
 *
 * @param[in] ctx,count,size Unused. @return NULL.
 *
 * @owner No allocation. @assumes None.
 */
static void *failAlloc(void *ctx, size_t count, size_t size)
{
    (void)ctx;
    (void)count;
    (void)size;
    return NULL;
}

/**
 * @brief State of the counting allocation hooks `countAlloc`/`countRelease`.
 *
 * @owner A stack variable of the test that passes `&st` as the hook `ctx`;
 *        nothing in it is allocated or freed.
 * @assumes Single-threaded use; the struct outlives every hook call.
 */
typedef struct {
    int calls;  /* Allocation calls seen so far; starts at 0. */
    int live;   /* Blocks allocated and not yet released; 0 when balanced. */
    int failAt; /* 1-based call number that returns NULL; 0 = never fail. */
} CountAlloc;

/**
 * @brief Counting allocation hook that fails only at the `failAt`-th call
 * (FR-025: reaches the "first buffer allocated, second failed" release path).
 *
 * @param[in,out] ctx `CountAlloc *`, non-NULL; counters are updated.
 * @param[in] count,size Element count and size, forwarded to `calloc`.
 * @return Zeroed block, or NULL on the injected call.
 * @owner The block is released by `countRelease`.
 * @assumes Single-threaded test; `count * size` is the CLI's bounded size.
 */
static void *countAlloc(void *ctx, size_t count, size_t size)
{
    CountAlloc *st = ctx;
    void *ptr;

    ++st -> calls;
    if (st -> calls == st -> failAt) {
        return NULL;
    }
    ptr = calloc(count, size);
    if (ptr != NULL) {
        ++st -> live;
    }
    return ptr;
}

/**
 * @brief Counting release hook paired with `countAlloc`.
 *
 * @param[in,out] ctx `CountAlloc *`, non-NULL; `live` is decremented.
 * @param[in] ptr Block from `countAlloc`, or NULL (ignored).
 * @return Nothing. @owner Takes ownership of `ptr` and frees it.
 * @assumes Every non-NULL `ptr` came from `countAlloc`, so `live` balances.
 */
static void countRelease(void *ctx, void *ptr)
{
    CountAlloc *st = ctx;

    if (ptr != NULL) {
        --st -> live;
    }
    free(ptr);
}

/**
 * @brief Either CLI buffer fails to allocate (first, then second): exit 2, the
 * other buffer is freed, nothing is written (FR-019, EC-010, FR-025).
 *
 * @return Nothing; a failed `REQUIRE` aborts with a diagnostic.
 * @owner Frees nothing itself; the hook counts the CLI's own releases.
 * @assumes `gScratch` is set; `n = 2` allocates `initial` then `final`, and
 *          the CLI makes both calls before checking either result.
 */
static void testSecondAllocFails(void)
{
    CountAlloc st = {0, 0, 2};
    QaEvolveOps ops = *qaEvolveDefaultOps();

    ops.alloc = countAlloc;
    ops.release = countRelease;
    ops.ctx = &st;
    expectRejected("FR-019 second alloc", 2, "2", "linear", "10", "20", "5", &ops);
    REQUIRE(st.calls == 2, "FR-019 second alloc", "expected exactly two allocations");
    REQUIRE(st.live == 0, "FR-019 second alloc", "first buffer leaked");

    ops.alloc = NULL;
    expectRejected("FR-025 NULL alloc entry", 1, "2", "linear", "10", "20", "5", &ops);
    ops.alloc = countAlloc;
    ops.release = NULL;
    expectRejected("FR-025 NULL release entry", 1, "2", "linear", "10", "20", "5", &ops);
    ops.release = countRelease;

    st.calls = 0;
    st.failAt = 1;
    expectRejected("FR-019 first alloc", 2, "2", "linear", "10", "20", "5", &ops);
    REQUIRE(st.calls == 2, "FR-019 first alloc", "expected exactly two allocations");
    REQUIRE(st.live == 0, "FR-019 first alloc", "second buffer leaked");
}

/**
 * @brief Library and `--out` codes 1..6 with nothing written (FR-015, EC-017).
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner Scratch removed by the caller. @assumes `gScratch` is set.
 */
static void testCodes(void)
{
    char dir[PATH_CAP];
    char file[PATH_CAP];
    char inner[PATH_CAP];
    char missing[PATH_CAP];
    QaEvolveOps failing = *qaEvolveDefaultOps();
    FILE *fp;

    expectRejected("FR-016 N=1", 1, "1", "linear", "10", "20", "5", NULL);
    expectRejected("FR-016 N=0", 1, "0", "linear", "10", "20", "5", NULL);
    expectRejected("FR-016 N=6", 1, "6", "linear", "10", "20", "5", NULL);
    expectRejected("FR-016 N=5", 6, "5", "linear", "10", "20", "5", NULL);
    expectRejected("FR-017 T=nan", 5, "2", "linear", "nan", "20", "5", NULL);
    expectRejected("FR-017 T=inf", 5, "2", "linear", "inf", "20", "5", NULL);
    expectRejected("FR-017 T=0", 5, "2", "linear", "0", "20", "5", NULL);
    expectRejected("FR-017 T=-1", 5, "2", "linear", "-1", "20", "5", NULL);
    expectRejected("FR-017 steps=0", 5, "2", "linear", "10", "0", "5", NULL);
    expectRejected("FR-024 steps>max", 1, "2", "linear", "10", "1000001", "5", NULL);
#if SIZE_MAX == 18446744073709551615ull
    expectRejected("FR-020 steps+1 overflow", 3, "2", "linear", "1", "18446744073709551615",
                   "0", NULL);
#endif
    expectRejected("EC-015 count", 1, "2", "linear", "10", "20000", "1", NULL);
    expectRejected("EC-015 rows", 1, "4", "linear", "10", "1000", "1", NULL);
    failing.alloc = failAlloc;
    expectRejected("FR-019 alloc", 2, "2", "linear", "10", "20", "5", &failing);

    /* EC-017: every rejected `--out` leaves its content untouched. */
    scratchPath(dir, "full");
    scratchPath(inner, "full/keep.txt");
    scratchPath(file, "file");
    scratchPath(missing, "nope/run");
    REQUIRE(mkdir(dir, 0700) == 0, "setup", "mkdir full");
    fp = fopen(inner, "w");
    REQUIRE(fp != NULL, "setup", "create keep.txt");
    fputs("keep", fp);
    fclose(fp);
    fp = fopen(file, "w");
    REQUIRE(fp != NULL, "setup", "create file");
    fclose(fp);
    REQUIRE(runWords("2", "linear", "10", "20", "5", dir, NULL) == 4, "EC-017",
            "non-empty directory");
    REQUIRE(countEntries(dir) == 1 && pathExists(inner), "EC-017",
            "non-empty directory was modified");
    REQUIRE(runWords("2", "linear", "10", "20", "5", file, NULL) == 4, "EC-017",
            "regular file");
    REQUIRE(runWords("2", "linear", "10", "20", "5", missing, NULL) == 4, "EC-017",
            "missing parent");
    REQUIRE(!pathExists(missing), "EC-017", "missing parent was created");
}

/**
 * @brief Parse one `trace.csv` data row (section 4).
 *
 * @param[in]  line Row text. @param[out] outJ,outT,outA,outB,outNorm,outEnergy
 *             Receivers.
 *
 * @return 1 when all six fields parsed, else 0.
 *
 * @owner No allocation. @assumes `line` is NUL-terminated.
 */
static int parseRow(const char *line, size_t *outJ, double *outT, double *outA,
                    double *outB, double *outNorm, double *outEnergy)
{
    unsigned long long j;

    if (sscanf(line, "%llu,%lf,%lf,%lf,%lf,%lf", &j, outT, outA, outB, outNorm,
               outEnergy) != 6) {
        return 0;
    }
    *outJ = (size_t)j;
    return 1;
}

/**
 * @brief Check `trace.csv` of a 2x2 linear `T = 10`, `steps = 20` run (section 4).
 *
 * @param[in] runDir Run directory. @param[in] rows Expected data rows.
 * @param[in] lastEnergyText Text of the energy field the summary printed.
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner Uses the static buffer `gFile`. @assumes The run completed.
 */
static void checkTrace(const char *runDir, size_t rows, const char *lastEnergyText)
{
    static char gFile[BUF_CAP];
    char path[PATH_CAP];
    char *line;
    char *save = NULL;
    size_t count = 0;
    char lastEnergy[64] = "";

    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/trace.csv", runDir);
    REQUIRE(readFile(path, gFile, sizeof gFile) > 0, "FR-013", "trace.csv unreadable");
    line = strtok_r(gFile, "\n", &save);
    REQUIRE(line != NULL && strcmp(line, "j,t,a,b,norm,energy") == 0, "FR-013",
            "trace.csv header");
    while ((line = strtok_r(NULL, "\n", &save)) != NULL) {
        size_t j;
        double t, a, b, norm, energy;
        const char *lastComma = strrchr(line, ',');

        REQUIRE(parseRow(line, &j, &t, &a, &b, &norm, &energy), "FR-013", "row parse");
        REQUIRE(j == count, "FR-013", "rows must be in order");
        REQUIRE(fabs(norm - 1.0) <= NORM_BOUND, "section 4", "norm within NORM_TOL");
        if (count == 0) {
            REQUIRE(t == 0.0 && fabs(a) <= 1e-12 && fabs(b - 1.0) <= 1e-12,
                    "section 4", "j = 0 invariants");
            REQUIRE(fabs(energy + 4.0) <= 1e-9, "section 4", "energy(0) = -numCells");
        }
        /* Bound: rows >= 1 (callers pass 21), so rows - 1 does not underflow. */
        if (count == rows - 1) {
            REQUIRE(t == 10.0 && fabs(a - 1.0) <= 1e-12 && fabs(b) <= 1e-12,
                    "section 4", "j = steps invariants");
            /* Bound: `lastEnergy` holds 64 bytes, longer than any `%.17g`-style energy
             * text; `lastComma` is non-NULL because `parseRow` just accepted the row,
             * which needs commas. A truncation would fail the summary comparison. */
            snprintf(lastEnergy, sizeof lastEnergy, "%s", lastComma + 1);
        }
        count++;
    }
    REQUIRE(count == rows, "FR-013", "trace row count");
    REQUIRE(strcmp(lastEnergy, lastEnergyText) == 0, "FR-015",
            "summary energy differs from the last trace row");
}

/**
 * @brief Check one snapshot file: header, 16 rows, `sum p` within tolerance.
 *
 * @param[in] runDir Run directory. @param[in] name File name below `snapshots/`.
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner Uses a static buffer. @assumes The file is small (16 rows).
 */
static void checkSnapshot(const char *runDir, const char *name)
{
    static char gSnap[BUF_CAP];
    char path[PATH_CAP];
    char *line;
    char *save = NULL;
    size_t rows = 0;
    double sum = 0.0;

    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/snapshots/%s", runDir, name);
    REQUIRE(readFile(path, gSnap, sizeof gSnap) > 0, "FR-013", name);
    line = strtok_r(gSnap, "\n", &save);
    REQUIRE(line != NULL && strcmp(line, "k,p") == 0, "FR-013", "snapshot header");
    while ((line = strtok_r(NULL, "\n", &save)) != NULL) {
        unsigned long long k;
        double p;

        REQUIRE(sscanf(line, "%llu,%lf", &k, &p) == 2 && k == rows && p >= 0.0,
                "FR-013", "snapshot row");
        sum += p;
        rows++;
    }
    REQUIRE(rows == 16, "EC-003", "a 2x2 snapshot has 16 rows");
    REQUIRE(fabs(sum - 1.0) <= 2.0 * NORM_BOUND + 1e-15, "section 4", "sum p");
}

/**
 * @brief Valid 2x2 runs: layout, records and summary (FR-013..FR-015).
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner Scratch removed by the caller. @assumes `gScratch` is set.
 */
static void testLayout(void)
{
    static const char *const keys[] = {
        "\"format_version\": 1", "\"spec\": \"005-evolution\"", "\"N\": 2",
        "\"family\": \"linear\"", "\"T\": 10", "\"steps\": 20", "\"r\": 0.5",
        "\"M\": 5", "\"seed\": \"N/A\"", "\"git_sha\"", "\"clang_version\"",
        "\"cmake_flags\""};
    static char gConf[BUF_CAP];
    char dir[PATH_CAP];
    char snaps[PATH_CAP];
    char path[PATH_CAP];
    const char *energy;
    char energyText[64];

    scratchPath(dir, "run");
    REQUIRE(runWords("2", "linear", "10", "20", "5", dir, NULL) == 0, "FR-015", gErr);
    REQUIRE(strstr(gOut, "final_energy ") != NULL, "FR-015", "summary energy");
    REQUIRE(strstr(gOut, "max_norm_drift ") != NULL, "FR-015", "summary drift");
    REQUIRE(strstr(gOut, dir) != NULL, "FR-015", "summary run dir");
    energy = strstr(gOut, "final_energy ") + strlen("final_energy ");
    /* Bound: `energyText` holds 64 bytes, longer than one energy value; the
     * `(int)` cast is safe because `strcspn` is at most the short summary line
     * length (far below INT_MAX). A truncation would fail `checkTrace`. */
    snprintf(energyText, sizeof energyText, "%.*s", (int)strcspn(energy, "\n"), energy);
    checkTrace(dir, 21, energyText);

    /* Bound: `snaps` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(snaps, sizeof snaps, "%s/snapshots", dir);
    REQUIRE(countEntries(snaps) == 5, "FR-012", "M = 5, steps = 20: five snapshots");
    checkSnapshot(dir, "snap_00.csv");
    checkSnapshot(dir, "snap_05.csv");
    checkSnapshot(dir, "snap_10.csv");
    checkSnapshot(dir, "snap_15.csv");
    checkSnapshot(dir, "snap_20.csv");

    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/config.json", dir);
    REQUIRE(readFile(path, gConf, sizeof gConf) > 0, "FR-014", "config.json missing");
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        REQUIRE(strstr(gConf, keys[i]) != NULL, "FR-014", keys[i]);
    }
    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/config.json.tmp", dir);
    REQUIRE(!pathExists(path), "FR-014", "temporary config left behind");

    /* An empty directory is accepted (EC-017) and `M = 0` gives only `j = steps`. */
    scratchPath(dir, "empty");
    REQUIRE(mkdir(dir, 0700) == 0, "setup", "mkdir empty");
    REQUIRE(runWords("2", "trigonometric", "10", "20", "0", dir, NULL) == 0, "EC-017", gErr);
    /* Bound: `snaps` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(snaps, sizeof snaps, "%s/snapshots", dir);
    REQUIRE(countEntries(snaps) == 1, "FR-012", "M = 0: one snapshot");
    checkSnapshot(dir, "snap_20.csv");
}

/**
 * @brief Test hook: break the norm after the Strang step `j = 7` (EC-020).
 *
 * @param[in] ctx Unused. @param[in] j Step just applied, `1..steps`.
 * @param[in,out] psi Working state. @param[in] dim Its length, at least 1.
 *
 * @return `QA_OK` always; only the state is changed.
 *
 * @owner No allocation. @assumes `dim >= 1`, so `psi[0]` is readable.
 */
static QaStatus breakNormAtSeven(void *ctx, size_t j, complex double *psi, size_t dim)
{
    (void)ctx;
    (void)dim;
    if (j == 7) {
        psi[0] *= 2.0; /* Bound: dim >= 1 for every accepted N. */
    }
    return QA_OK;
}

/**
 * @brief EC-020: a norm failure at step 7 leaves a partial but invalid run.
 *
 * @return Nothing; a failed check calls `failCheck`, which exits 1.
 *
 * @owner Scratch removed by the caller. @assumes `gScratch` is set.
 */
static void testPerturb(void)
{
    QaEvolveOps ops = *qaEvolveDefaultOps();
    static char gFile[BUF_CAP];
    char dir[PATH_CAP];
    char path[PATH_CAP];
    char snaps[PATH_CAP];
    size_t rows = 0;

    ops.perturb = breakNormAtSeven;
    scratchPath(dir, "run");
    REQUIRE(runWords("2", "linear", "10", "20", "3", dir, &ops) == 5, "EC-020", gErr);
    REQUIRE(gOut[0] == '\0', "EC-020", "no summary on failure");

    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/trace.csv", dir);
    REQUIRE(readFile(path, gFile, sizeof gFile) > 0, "EC-020", "trace.csv missing");
    for (const char *p = gFile; *p != '\0'; p++) {
        rows += *p == '\n';
    }
    REQUIRE(rows == 1 + 7, "EC-020", "trace must hold header and rows 0..6");
    REQUIRE(strstr(gFile, "\n7,") == NULL, "EC-020", "the failing step was delivered");

    /* Bound: `snaps` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(snaps, sizeof snaps, "%s/snapshots", dir);
    REQUIRE(countEntries(snaps) == 3, "EC-020", "snapshots below 7: j = 0, 3, 6");
    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/snap_06.csv", snaps);
    REQUIRE(pathExists(path), "EC-020", "snapshot 6 missing");
    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/snap_09.csv", snaps);
    REQUIRE(!pathExists(path), "EC-020", "a later snapshot exists");
    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/config.json", dir);
    REQUIRE(!pathExists(path), "EC-020", "config.json must not exist");
    /* Bound: `path` holds `PATH_CAP` bytes and the short suffix cannot overflow it
     * (`runDir` comes from `scratchPath`, already checked); a truncation would
     * fail the following read or existence check. */
    snprintf(path, sizeof path, "%s/config.json.tmp", dir);
    REQUIRE(!pathExists(path), "EC-020", "temporary config must not exist");
}

/**
 * @brief Select and run one test group.
 *
 * @param[in] argc Word count. @param[in] argv `group base-dir`.
 *
 * @return 0 when the group passes; 1 on a failed check (inside `failCheck`);
 *         2 on a usage error.
 *
 * @owner The scratch directory is created and removed here.
 * @assumes `argv[2]` is an existing writable directory.
 */
int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test-005-cli <usage|codes|layout|perturb> <base-dir>\n");
        return 2;
    }
    makeScratch(argv[2]);
    if (strcmp(argv[1], "usage") == 0) {
        testUsage();
    } else if (strcmp(argv[1], "codes") == 0) {
        testCodes();
        testSecondAllocFails();
    } else if (strcmp(argv[1], "layout") == 0) {
        testLayout();
    } else if (strcmp(argv[1], "perturb") == 0) {
        testPerturb();
    } else {
        cleanScratch();
        fprintf(stderr, "unknown group %s\n", argv[1]);
        return 2;
    }
    cleanScratch();
    printf("test-005-cli %s: OK\n", argv[1]);
    return 0;
}
