/*
 * 005-evolution CLI library `qa_cli_evolve` (spec 005-evolution, task T-014;
 * FR-006, FR-013, FR-014, FR-015, EC-007, EC-009, EC-016, EC-017, EC-020).
 *
 * Purpose: parse the command line strictly, check `--out` without creating
 * anything, run `qaEvolveWith` with callbacks that stream the records to a
 * `QaRun` handle, write `config.json` last, print the summary and map every
 * failure to its exit status. `main` of `qa-005-evolve` is a thin wrapper, so
 * the tests drive the same path with an injected hook table (EC-020).
 *
 * Ownership: the run handle and the two state buffers (`initial`, `final`) are
 * owned by `qaCliEvolveRun` and released at its single `cleanup` label; the
 * buffers go through `ops -> alloc` and `ops -> release`. The family name is a
 * static library string and the build metadata are string literals; none is
 * freed. Callback contexts live on the stack of `qaCliEvolveRun`.
 * Errors: usage errors exit 64 and write nothing; every other failure returns
 * the numeric `QaStatus`, prints one line on `err` and leaves any partial file
 * in place, without `config.json` (FR-013, FR-019).
 * Numerical assumptions: `dim = 2^(N N)` is computed only for `N` in 2..4
 * (`N N <= 16`), so the shift and the byte count (`dim * 16 <= 2^20`) cannot
 * overflow; `dt = T / steps` is computed only after the library accepted `T`
 * and `steps`.
 * Build metadata: `QA_GIT_SHA`, `QA_CLANG_VERSION` and `QA_CMAKE_FLAGS` are
 * compile definitions set by CMake; a missing one falls back to `unavailable`.
 */

#include "qa-005-evolve-cli.h"

#include <complex.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/status.h"
#include "qa/evolution/schedules.h"
#include "qa/hamiltonian/driver.h"
#include "qa/hamiltonian/target.h"
#include "qa/io/run.h"

#ifndef QA_GIT_SHA
#define QA_GIT_SHA "unavailable"
#endif
#ifndef QA_CLANG_VERSION
#define QA_CLANG_VERSION "unavailable"
#endif
#ifndef QA_CMAKE_FLAGS
#define QA_CMAKE_FLAGS "unavailable"
#endif

/* Words of a valid command line: program, N, family, T, steps, M, `--out`, dir. */
#define QA_CLI_ARGC 8
#define QA_CLI_OUT_FLAG "--out"

/**
 * @brief Parsed command line of one run (FR-015).
 *
 * @owner Value type on the stack of `qaCliEvolveRun`; `dir` points into `argv`,
 *        which the caller owns, and is never freed.
 * @assumes Filled only by `qaCliParseArgs`; every field is valid after it
 *          returns 1.
 */
typedef struct QaCliArgs {
    unsigned int n;            /* N, any value of `unsigned int` (library gates it) */
    QaScheduleKind family;     /* one of the four 003 families */
    double T;                  /* total time, any `strtod` result */
    size_t steps;              /* Strang steps, any `size_t` (library bounds it) */
    size_t snapshotInterval;   /* M, any `size_t` */
    const char *dir;           /* run directory, NUL-terminated, caller-owned */
} QaCliArgs;

/**
 * @brief Streaming context shared by both callbacks (FR-013).
 *
 * @owner Value type on the stack of `qaCliEvolveRun`; `run` is owned by it.
 * @assumes `run` stays valid until the evolution returns.
 */
typedef struct QaCliSink {
    QaRun *run;                /* open handle, written by the callbacks */
} QaCliSink;

/**
 * @brief Parse an unsigned decimal integer strictly (EC-016).
 *
 * Accepts only a non-empty run of digits that fits `unsigned long long`: no
 * sign, no space, no `0x` prefix (base 10), no trailing text, no overflow.
 *
 * @param[in]  text   NUL-terminated word, non-NULL.
 * @param[out] outVal Receiver, written only when the function returns 1.
 *
 * @return 1 when `text` is a valid integer, 0 otherwise.
 *
 * @owner No allocation; both pointers stay owned by the caller.
 * @assumes `strtoull` reports overflow through `ERANGE`; `errno` is cleared
 *          first so a stale value cannot reject a valid word.
 */
static int qaCliParseUnsigned(const char *text, unsigned long long *outVal)
{
    char *end = NULL;
    unsigned long long value;

    /* The first character must be a digit: this rejects "", " 5", "-1", "+1". */
    if (!isdigit((unsigned char)text[0])) {
        return 0;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    *outVal = value;
    return 1;
}

/**
 * @brief Parse a real strictly (EC-016).
 *
 * Accepts what `strtod` fully consumes, with no leading space and no trailing
 * text; `nan`, `inf` and negative or zero values parse and are rejected later
 * by the library with `QA_ERR_DOMAIN` (EC-016).
 *
 * @param[in]  text   NUL-terminated word, non-NULL.
 * @param[out] outVal Receiver, written only when the function returns 1.
 *
 * @return 1 when `text` is fully a real, 0 otherwise.
 *
 * @owner No allocation; both pointers stay owned by the caller.
 * @assumes `ERANGE` from `strtod` (overflow to infinity, underflow) is not an
 *          error here: the value is passed on and the library judges it.
 */
static int qaCliParseReal(const char *text, double *outVal)
{
    char *end = NULL;
    double value;

    /* A leading space would be skipped by `strtod`; the word must start at once. */
    if (text[0] == '\0' || isspace((unsigned char)text[0])) {
        return 0;
    }
    value = strtod(text, &end);
    if (end == text || *end != '\0') {
        return 0;
    }
    *outVal = value;
    return 1;
}

/**
 * @brief Find the 003 family with a given name (EC-016).
 *
 * @param[in]  name    NUL-terminated word, non-NULL.
 * @param[out] outKind Receiver, written only when the function returns 1.
 *
 * @return 1 when `name` equals the stable name of one family, 0 otherwise.
 *
 * @owner No allocation; the library names are static and never freed.
 * @assumes The four families are valued `0..3` (`QaScheduleKind`).
 */
static int qaCliParseFamily(const char *name, QaScheduleKind *outKind)
{
    /* Bound: kind runs over the four enumerators, 0..QA_SCHEDULE_EXPONENTIAL. */
    for (int kind = QA_SCHEDULE_LINEAR; kind <= QA_SCHEDULE_EXPONENTIAL; kind++) {
        const char *candidate = NULL;

        if (qaScheduleName((QaScheduleKind)kind, &candidate) == QA_OK &&
            strcmp(candidate, name) == 0) {
            *outKind = (QaScheduleKind)kind;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Parse the whole command line (FR-015, EC-016).
 *
 * @param[in]  argc    Word count, including the program name.
 * @param[in]  argv    `argc` NUL-terminated words, read only.
 * @param[out] outArgs Receiver, fully written only when the function returns 1.
 *
 * @return 1 when every word is valid, 0 on any usage error.
 *
 * @owner No allocation; `outArgs -> dir` borrows `argv[7]`.
 * @assumes `argv[0..argc-1]` are valid strings when `argc == QA_CLI_ARGC`; the
 *          word count is checked before any index is read.
 */
static int qaCliParseArgs(int argc, char **argv, QaCliArgs *outArgs)
{
    unsigned long long n;
    unsigned long long steps;
    unsigned long long interval;
    QaCliArgs parsed;

    /* Bound: argv[1..7] are read only after argc == 8 is established. */
    if (argc != QA_CLI_ARGC || argv == NULL) {
        return 0;
    }
    if (strcmp(argv[6], QA_CLI_OUT_FLAG) != 0) {
        return 0;
    }
    if (!qaCliParseUnsigned(argv[1], &n) || n > UINT_MAX) {
        return 0;
    }
    if (!qaCliParseFamily(argv[2], &parsed.family)) {
        return 0;
    }
    if (!qaCliParseReal(argv[3], &parsed.T)) {
        return 0;
    }
    if (!qaCliParseUnsigned(argv[4], &steps) || steps > SIZE_MAX) {
        return 0;
    }
    if (!qaCliParseUnsigned(argv[5], &interval) || interval > SIZE_MAX) {
        return 0;
    }
    /* Bounds: `n <= UINT_MAX`, `steps <= SIZE_MAX` and `interval <= SIZE_MAX`
     * were checked above, so the casts below cannot truncate. */
    parsed.n = (unsigned int)n;
    parsed.steps = (size_t)steps;
    parsed.snapshotInterval = (size_t)interval;
    parsed.dir = argv[7];
    *outArgs = parsed;
    return 1;
}

/**
 * @brief Observer: stream one `trace.csv` row (FR-011, FR-013).
 *
 * @param[in] ctx    `QaCliSink *`, non-NULL.
 * @param[in] sample Observables of step `j`, valid during the call.
 *
 * @return The status of `qaRunAppendTrace`, propagated by the library (FR-019).
 *
 * @owner No allocation; neither pointer is retained.
 * @assumes The library calls it for `j = 0..steps` in order (FR-028), which is
 *          the order `qaRunAppendTrace` requires.
 */
static QaStatus qaCliObserve(void *ctx, const QaEvolveSample *sample)
{
    const QaCliSink *sink = ctx;

    return qaRunAppendTrace(sink -> run, sample -> j, sample -> t, sample -> a,
                            sample -> b, sample -> norm, sample -> energy);
}

/**
 * @brief Snapshot sink: stream one `snapshots/snap_<j>.csv` file (FR-013).
 *
 * @param[in] ctx   `QaCliSink *`, non-NULL.
 * @param[in] j     Snapshot step.
 * @param[in] dim   Length of `probs`.
 * @param[in] probs Probabilities `p_k`, valid during the call.
 *
 * @return The status of `qaRunWriteSnapshot`, propagated by the library.
 *
 * @owner No allocation; neither pointer is retained.
 * @assumes The observer of the same `j` ran first, so the layout exists (FR-028).
 */
static QaStatus qaCliSnapshot(void *ctx, size_t j, size_t dim, const double *probs)
{
    const QaCliSink *sink = ctx;

    return qaRunWriteSnapshot(sink -> run, j, dim, probs);
}

/**
 * @brief Run the evolution CLI and return its exit status (FR-015).
 *
 * Implements the contract of `qaCliEvolveRun` in `qa-005-evolve-cli.h`.
 *
 * @param[in]  argc Word count of `argv`.
 * @param[in]  argv Words, read only.
 * @param[out] out  Summary stream.
 * @param[out] err  Usage and error stream.
 * @param[in]  ops  Hook table or NULL for the production table; a non-NULL
 *                  table needs non-NULL `alloc` and `release` (else exit 1).
 *
 * @return 0, 64 or the numeric `QaStatus` of the failing step.
 *
 * @owner Owns `run`, `initial` and `final`; all released at `cleanup`.
 * @assumes `out` and `err` are non-NULL open streams (checked by the caller of
 *          this private function, `main` or a test).
 */
int qaCliEvolveRun(int argc, char **argv, FILE *out, FILE *err,
                   const QaEvolveOps *ops)
{
    int exitCode = QA_CLI_EVOLVE_EXIT_USAGE;
    QaCliArgs args;
    QaRun *run = NULL;
    complex double *initial = NULL;
    complex double *final = NULL;
    size_t dim = 0;
    const char *familyName = NULL;
    QaCliSink sink;
    QaEvolveParams params;
    QaEvolveCallbacks callbacks;
    QaEvolveSummary summary;
    QaRunConfig config;
    QaStatus status;

    if (ops == NULL) {
        ops = qaEvolveDefaultOps();
    }
    /* Step 0: the CLI calls `alloc` and `release` itself, before the library
     * validates the table (`qaEvolveWith` step 0), so a NULL entry is rejected
     * here and nothing has been acquired yet. */
    if (ops -> alloc == NULL || ops -> release == NULL) {
        fprintf(err, "qa-005-evolve: invalid operations table\n");
        exitCode = (int)QA_ERR_RANGE;
        goto cleanup;
    }

    /* Step 1: strict parsing; nothing has been touched yet (EC-016). */
    if (!qaCliParseArgs(argc, argv, &args)) {
        fprintf(err, "usage: qa-005-evolve <N> <family> <T> <steps> <M> --out <dir>\n"
                     "  N, steps, M: unsigned integers; family: linear |"
                     " trigonometric | quadratic | exponential; T: real\n");
        goto cleanup;
    }

    /* Step 2: `--out` rules, no file-system change (EC-017). */
    status = qaRunCheckOutDir(args.dir);
    if (status != QA_OK) {
        fprintf(err, "qa-005-evolve: --out %s rejected (%d)\n", args.dir, (int)status);
        exitCode = (int)status;
        goto cleanup;
    }

    /* Step 3: the handle is opened with `steps` clamped to 1 when it is 0:
     * `qaEvolveWith` rejects `steps == 0` (FR-017) before any callback, so the
     * handle is never used in that case and `steps == 0` still exits 5. */
    status = qaRunOpen(args.dir, args.steps == 0 ? 1 : args.steps, &run);
    if (status != QA_OK) {
        fprintf(err, "qa-005-evolve: cannot open the run (%d)\n", (int)status);
        exitCode = (int)status;
        goto cleanup;
    }
    if (args.n >= 2 && args.n <= 4) {
        /* Bound: n * n <= 16, so the shift and `dim * sizeof` stay below 2^21. */
        dim = (size_t)1 << (args.n * args.n);
        initial = ops -> alloc(ops -> ctx, dim, sizeof *initial);
        final = ops -> alloc(ops -> ctx, dim, sizeof *final);
        if (initial == NULL || final == NULL) {
            fprintf(err, "qa-005-evolve: out of memory\n");
            exitCode = (int)QA_ERR_NOMEM;
            goto cleanup;
        }
        /* Defensive: `args.n` is in 2..4 and `dim == 2^(n*n)` exactly, so the
         * 001 call cannot fail here; the branch keeps ERR-2 (no ignored
         * status) and releases both buffers through `cleanup`. */
        status = qaHamiltonianInitialState(args.n, initial, dim);
        if (status != QA_OK) {
            fprintf(err, "qa-005-evolve: initial state failed (%d)\n", (int)status);
            exitCode = (int)status;
            goto cleanup;
        }
    }
    /* Any other `n` reaches the library with NULL buffers: its N-gate (FR-016)
     * precedes the pointer check (FR-020 (1) before (2)) and answers 1 or 6. */

    sink.run = run;
    params.n = args.n;
    params.family = args.family;
    params.T = args.T;
    params.steps = args.steps;
    params.snapshotInterval = args.snapshotInterval;
    callbacks.observer = qaCliObserve;
    callbacks.observerCtx = &sink;
    callbacks.sink = qaCliSnapshot;
    callbacks.sinkCtx = &sink;

    /* Step 4: the library validates before the first callback (EC-007). */
    status = qaEvolveWith(&params, initial, final, dim, &callbacks, &summary, ops);
    if (status != QA_OK) {
        fprintf(err, "qa-005-evolve: evolution failed (%d)\n", (int)status);
        exitCode = (int)status;
        goto cleanup;
    }

    /* Step 5: `config.json` last (FR-014). `dt` is finite and non-zero here
     * because the library accepted `T` and `steps`. */
    status = qaScheduleName(args.family, &familyName);
    if (status != QA_OK) {
        fprintf(err, "qa-005-evolve: family name failed (%d)\n", (int)status);
        exitCode = (int)status;
        goto cleanup;
    }
    config.n = args.n;
    config.family = familyName;
    config.T = args.T;
    config.steps = args.steps;
    config.dt = args.T / (double)args.steps;
    config.r = QA_HAMILTONIAN_TARGET_R;
    config.snapshotInterval = args.snapshotInterval;
    config.gitSha = QA_GIT_SHA;
    config.clangVersion = QA_CLANG_VERSION;
    config.cmakeFlags = QA_CMAKE_FLAGS;
    status = qaRunWriteConfig(run, &config);
    if (status != QA_OK) {
        fprintf(err, "qa-005-evolve: cannot write config.json (%d)\n", (int)status);
        exitCode = (int)status;
        goto cleanup;
    }
    /* The handle is destroyed here; `qaRunWriteConfig` already closed the trace,
     * so a non-OK status is defensive and not expected (the trace-close error is
     * reported by `qaRunWriteConfig`). `run` is cleared so `cleanup` does not
     * destroy it twice. */
    status = qaRunDestroy(run);
    run = NULL;
    if (status != QA_OK) {
        fprintf(err, "qa-005-evolve: cannot close the run (%d)\n", (int)status);
        exitCode = (int)status;
        goto cleanup;
    }

    fprintf(out, "final_energy %.17g\nmax_norm_drift %.17g\nrun_dir %s\n",
            summary.finalEnergy, summary.maxNormDrift, args.dir);
    exitCode = 0;

cleanup:
    if (run != NULL) {
        /* Failure path: the close result adds nothing, the run is already invalid. */
        (void)qaRunDestroy(run);
    }
    if (final != NULL) {
        ops -> release(ops -> ctx, final);
    }
    if (initial != NULL) {
        ops -> release(ops -> ctx, initial);
    }
    return exitCode;
}
