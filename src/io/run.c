/*
 * 005-evolution Phase 5 implementation (task T-013): run record writers.
 *
 * Purpose: check the `--out` directory without creating anything (EC-017), then
 * stream `trace.csv` and `snapshots/snap_<j>.csv` row by row and write
 * `config.json` last (FR-013, FR-014). The contract is declared in
 * `include/qa/io/run.h` (public) and `src/io/run-internal.h` (operations table
 * and `qaRunOpenWith`).
 *
 * Ownership: the only heap block is the run handle, one `malloc` that also holds
 * the copy of the run dir path; `qaRunDestroy` is its single release path. The
 * only other resource is the open trace `FILE *`, owned by the handle while its
 * state is `TRACE_OPEN` and released exactly once through `ops -> close` by
 * `qaRunWriteConfig`, `qaRunDestroy` or the failure path of the lazy creation.
 * Snapshot and `config.json.tmp` streams are opened and closed inside one call
 * through the single `cleanup` label of each function. Paths and rows live in
 * stack buffers sized from the validated bounds.
 *
 * Errors: arguments are validated, in the order documented in the header, before
 * any file is created. Every file operation goes through the ops table and is
 * checked; a failure returns `QA_ERR_IO` after releasing what the call acquired.
 * Partial files stay (FR-013); `config.json` is only ever produced by the final
 * `rename`, so its absence marks an invalid run (FR-014, EC-009).
 *
 * Numerical assumptions: values are formatted with `%.17g`, which round-trips a
 * `double` and has at most 24 characters for a finite value (sign, 17 digits,
 * point, `e-308`). Every formatted row is bounded by constants below that cover
 * `size_t` (at most 20 digits) plus the numbers of the row, so `snprintf` never
 * truncates; its result is nevertheless checked. No arithmetic overflows: the
 * only sums are over the path bounds of `QA_IO_RUN_PATH_MAX`.
 */

#define _POSIX_C_SOURCE 200809L /* mkdir, stat, lstat, opendir, readdir */

#include "qa/io/run.h"

#include "run-internal.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Room added to the path bound for the longest fixed suffix (`/snapshots/snap_`
 * + 20 digits + `.csv` is 40 bytes; `/config.json.tmp` is 16) plus the NUL. */
#define PATH_BUF (QA_IO_RUN_PATH_MAX + 64u)

/* Largest formatted trace row: `%zu` (20) + five `%.17g` (24 each) + five commas
 * + newline + NUL = 147; rounded up. */
#define ROW_CAP 192u

/* Largest formatted snapshot row: `%zu` (20) + comma + `%.17g` (24) + newline +
 * NUL = 47; rounded up. */
#define SNAP_ROW_CAP 64u

/* Largest `%.17g` or `%zu` number text, with the NUL. */
#define NUM_CAP 32u

/* Fixed header lines, newline included. */
static const char kTraceHeader[] = "j,t,a,b,norm,energy\n";
static const char kSnapHeader[] = "k,p\n";

/* State of the trace stream: not yet created, open, or closed for good. */
typedef enum TraceState {
    TRACE_NEW = 0,
    TRACE_OPEN = 1,
    TRACE_CLOSED = 2
} TraceState;

/* Run handle, see `qa/io/run.h`. Owner: the caller of `qaRunOpen`. */
struct QaRun {
    const QaRunOps *ops; /* borrowed, must outlive the handle */
    size_t steps;        /* total Strang steps, >= 1 */
    size_t next;         /* next expected trace index, 0..steps + 1 */
    int width;           /* digit count of `steps`, snapshot zero padding */
    TraceState state;    /* life cycle of `trace` */
    FILE *trace;         /* owned while state == TRACE_OPEN, else NULL */
    size_t dirLen;       /* strlen of `dir`, 1..QA_IO_RUN_PATH_MAX */
    char dir[];          /* owned copy of the run dir path, NUL-terminated */
};

/**
 * @brief Default `makeDir`: `mkdir` that accepts an existing directory.
 *
 * @param[in] ctx  Unused.
 * @param[in] path Non-NULL NUL-terminated directory path.
 *
 * @return 0 when the directory was created or already is a directory, -1 else.
 *
 * @owner No allocation; `path` stays with the caller.
 * @assumes The existence race with another creator is outside the contract
 *          (EC-017); the library calls this only for an absent or empty path.
 */
static int defaultMakeDir(void *ctx, const char *path)
{
    struct stat st;

    (void)ctx;
    if (mkdir(path, 0777) == 0) {
        return 0;
    }
    /* Only an existing directory counts as success; any other errno or an
     * existing non-directory is a failure. */
    if (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 0;
    }
    return -1;
}

/**
 * @brief Default `open`: `fopen`.
 *
 * @param[in] ctx  Unused.
 * @param[in] path Non-NULL NUL-terminated file path.
 * @param[in] mode Non-NULL `fopen` mode.
 *
 * @return The stream, or NULL on failure.
 *
 * @owner The caller releases the stream through `defaultClose`.
 * @assumes `mode` is valid for `fopen`; the writers pass `"wx"`.
 */
static FILE *defaultOpen(void *ctx, const char *path, const char *mode)
{
    (void)ctx;
    return fopen(path, mode);
}

/**
 * @brief Default `write`: `fwrite`.
 *
 * @param[in]     ctx    Unused.
 * @param[in]     buf    `size * count` readable bytes.
 * @param[in]     size   Element size.
 * @param[in]     count  Element count.
 * @param[in,out] stream Open stream.
 *
 * @return The number of elements written.
 *
 * @owner No allocation; `stream` stays with the caller.
 * @assumes `buf` holds `size * count` bytes, guaranteed by the writers.
 */
static size_t defaultWrite(void *ctx, const void *buf, size_t size, size_t count,
                           FILE *stream)
{
    (void)ctx;
    return fwrite(buf, size, count, stream);
}

/**
 * @brief Default `close`: `fclose`.
 *
 * @param[in]     ctx    Unused.
 * @param[in,out] stream Open stream, released by this call.
 *
 * @return 0 on success, non-zero on failure; the stream is released either way.
 *
 * @owner Releases `stream`.
 * @assumes `stream` was opened by `defaultOpen` and is closed once.
 */
static int defaultClose(void *ctx, FILE *stream)
{
    (void)ctx;
    return fclose(stream);
}

/**
 * @brief Default `rename`: `rename`.
 *
 * @param[in] ctx  Unused.
 * @param[in] from Existing path.
 * @param[in] to   Target path, replaced atomically (LIM-1).
 *
 * @return 0 on success, non-zero on failure.
 *
 * @owner No allocation.
 * @assumes Both paths are NUL-terminated and on the same file system.
 */
static int defaultRename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    return rename(from, to);
}

/**
 * @brief Default `remove`: `remove`.
 *
 * @param[in] ctx  Unused.
 * @param[in] path Path to delete.
 *
 * @return 0 on success, non-zero on failure.
 *
 * @owner No allocation.
 * @assumes `path` is NUL-terminated.
 */
static int defaultRemove(void *ctx, const char *path)
{
    (void)ctx;
    return remove(path);
}

/**
 * @brief Default `alloc`: libc `malloc` (FR-025 seam for the run handle).
 *
 * @param[in] ctx  Unused.
 * @param[in] size Block size in bytes, already bounded by the caller.
 *
 * @return Block of `size` bytes, or NULL when memory is exhausted.
 *
 * @owner The caller owns the block and frees it through `defaultRelease`.
 * @assumes `size` was bounded by `QA_IO_RUN_PATH_MAX` before the call.
 */
static void *defaultAlloc(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

/**
 * @brief Default `release`: libc `free`.
 *
 * @param[in] ctx Unused.
 * @param[in] ptr Block from `defaultAlloc`, or NULL (ignored).
 *
 * @return Nothing.
 *
 * @owner Takes ownership of `ptr` and frees it exactly once.
 * @assumes `ptr` came from `defaultAlloc` and is not used afterwards.
 */
static void defaultRelease(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

/** libc-backed operations table (constant data, `ctx` NULL). */
static const QaRunOps kLibcOps = {NULL,         defaultMakeDir, defaultOpen,
                                  defaultWrite, defaultClose,   defaultRename,
                                  defaultRemove, defaultAlloc,  defaultRelease};

/*
 * `qaRunDefaultOps` (task T-013, FR-025).
 *
 * Implements the contract declared in `src/io/run-internal.h`.
 */
const QaRunOps *qaRunDefaultOps(void)
{
    return &kLibcOps;
}

/**
 * @brief Report whether `s` has at most `maxLen` bytes before its NUL.
 *
 * Scans at most `maxLen + 1` bytes, so an unterminated or huge string is never
 * read beyond the first byte past the bound.
 *
 * @param[in] s      Non-NULL caller-owned NUL-terminated string.
 * @param[in] maxLen Largest accepted length, excluding the NUL.
 *
 * @return 1 when `strlen(s) <= maxLen`, 0 otherwise.
 *
 * @owner No allocation; `s` stays with the caller.
 * @assumes `s` is readable up to its NUL or `maxLen + 1` bytes; `maxLen + 1`
 *          cannot overflow for the project bounds.
 */
static int fitsWithin(const char *s, size_t maxLen)
{
    size_t i = 0;

    /* Bound: the loop never reads past index `maxLen`. */
    while (i <= maxLen && s[i] != '\0') {
        i++;
    }
    return i <= maxLen;
}

/**
 * @brief Validate a run dir path: non-empty, no trailing `/`, within the bound.
 *
 * @param[in] dir Non-NULL NUL-terminated path.
 *
 * @return 1 when valid, 0 otherwise.
 *
 * @owner No allocation; `dir` stays with the caller.
 * @assumes `dir` is readable up to its NUL or `QA_IO_RUN_PATH_MAX + 1` bytes.
 */
static int dirPathValid(const char *dir)
{
    size_t len;

    if (!fitsWithin(dir, QA_IO_RUN_PATH_MAX)) {
        return 0;
    }
    /* Bound: `fitsWithin` guarantees `len <= QA_IO_RUN_PATH_MAX`, and `len > 0`
     * is checked before `dir[len - 1]` is read, so the index cannot underflow. */
    len = strlen(dir);
    return len > 0 && dir[len - 1] != '/';
}

/**
 * @brief Report whether `path` is an existing directory (symlinks followed).
 *
 * @param[in] path Non-NULL NUL-terminated path.
 *
 * @return 1 when `stat` succeeds and the target is a directory, 0 otherwise.
 *
 * @owner No allocation.
 * @assumes Read-only inspection; nothing is created.
 */
static int isDirectory(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @brief Report the emptiness of an existing directory.
 *
 * Reads the entries with `readdir`; `.` and `..` are ignored.
 *
 * @param[in]  path Non-NULL NUL-terminated directory path.
 * @param[out] outEmpty Set to 1 when only `.` and `..` exist, else 0; written
 *                      only on `QA_OK`.
 *
 * @return `QA_OK` on a completed scan, `QA_ERR_IO` when the directory cannot be
 *         opened, read or closed (ERR-1).
 *
 * @owner The `DIR *` is opened and closed inside the call.
 * @assumes `errno` is cleared before each `readdir` so that a NULL return can be
 *          told apart from the end of the directory.
 */
static QaStatus scanDirectory(const char *path, int *outEmpty)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int empty = 1;

    if (dir == NULL) {
        return QA_ERR_IO;
    }
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            break;
        }
        if (strcmp(entry -> d_name, ".") != 0 &&
            strcmp(entry -> d_name, "..") != 0) {
            empty = 0;
            break;
        }
    }
    /* `readdir` returns NULL both at the end and on error; only a non-zero errno
     * separates them. */
    if (entry == NULL && errno != 0) {
        (void)closedir(dir);
        return QA_ERR_IO;
    }
    if (closedir(dir) != 0) {
        return QA_ERR_IO;
    }
    *outEmpty = empty;
    return QA_OK;
}

/**
 * @brief Check that the parent of an absent `dir` exists and is a directory.
 *
 * The parent is the path before the last `/`, `/` for a child of the root, or
 * `.` when `dir` has no `/` (EC-017).
 *
 * @param[in] dir Non-NULL validated run dir path, at most `QA_IO_RUN_PATH_MAX`.
 *
 * @return `QA_OK` when the parent is a directory, `QA_ERR_IO` otherwise.
 *
 * @owner No allocation; the parent copy lives on the stack.
 * @assumes `dir` passed `dirPathValid`, so its length is within the bound.
 */
static QaStatus checkParent(const char *dir)
{
    char parent[QA_IO_RUN_PATH_MAX + 1u];
    const char *slash = strrchr(dir, '/');
    size_t len;

    if (slash == NULL) {
        return isDirectory(".") ? QA_OK : QA_ERR_IO;
    }
    len = (size_t)(slash - dir);
    if (len == 0) {
        return isDirectory("/") ? QA_OK : QA_ERR_IO;
    }
    /* Bound: `len < strlen(dir) <= QA_IO_RUN_PATH_MAX`, so the copy and the NUL
     * fit `parent` (QA_IO_RUN_PATH_MAX + 1 bytes). */
    memcpy(parent, dir, len);
    parent[len] = '\0';
    return isDirectory(parent) ? QA_OK : QA_ERR_IO;
}

/*
 * `qaRunCheckOutDir` (task T-013, FR-015, EC-017).
 *
 * Implements the contract declared in `include/qa/io/run.h`: arguments first,
 * then read-only inspection (`stat`, `lstat`, `readdir`); nothing is created.
 */
QaStatus qaRunCheckOutDir(const char *dir)
{
    struct stat st;
    int empty = 0;
    QaStatus status;

    /* Step 1: NULL path. */
    if (dir == NULL) {
        return QA_ERR_DOMAIN;
    }
    /* Step 2: empty, trailing slash or over the bound, before any file call. */
    if (!dirPathValid(dir)) {
        return QA_ERR_RANGE;
    }

    if (stat(dir, &st) != 0) {
        /* Step 6: only "does not exist" leads to the parent check. */
        if (errno != ENOENT) {
            return QA_ERR_IO;
        }
        /* Step 4: a link that exists but does not resolve is not a directory. */
        if (lstat(dir, &st) == 0) {
            return QA_ERR_IO;
        }
        /* Step 3: absent path, the parent must be a directory. */
        return checkParent(dir);
    }
    /* Step 4: a regular file or any other non-directory. */
    if (!S_ISDIR(st.st_mode)) {
        return QA_ERR_IO;
    }
    /* Steps 5 and 6: only an empty directory is accepted. */
    status = scanDirectory(dir, &empty);
    if (status != QA_OK) {
        return status;
    }
    return empty ? QA_OK : QA_ERR_IO;
}

/**
 * @brief Count the decimal digits of `value` by repeated division.
 *
 * @param[in] value Any `size_t`.
 *
 * @return The digit count, 1 for zero, at most 20 on 64-bit targets.
 *
 * @owner No allocation.
 * @assumes Unsigned division never overflows, so any `size_t` is safe.
 */
static int digitCount(size_t value)
{
    int digits = 1;

    while (value >= 10u) {
        value /= 10u;
        digits++;
    }
    return digits;
}

/**
 * @brief Report whether every function entry of `ops` is set.
 *
 * @param[in] ops Possibly NULL table.
 *
 * @return 1 when `ops` and its eight function entries are non-NULL, else 0.
 *
 * @owner No allocation.
 * @assumes Read-only.
 */
static int opsValid(const QaRunOps *ops)
{
    return ops != NULL && ops -> makeDir != NULL && ops -> open != NULL &&
           ops -> write != NULL && ops -> close != NULL &&
           ops -> rename != NULL && ops -> remove != NULL &&
           ops -> alloc != NULL && ops -> release != NULL;
}

/*
 * `qaRunOpenWith` (task T-013, FR-013, FR-019, FR-025).
 *
 * Implements the contract declared in `src/io/run-internal.h` and, through
 * `qaRunOpen`, `include/qa/io/run.h`. One `ops -> alloc` block holds the handle and the path
 * copy; nothing touches the file system.
 */
QaStatus qaRunOpenWith(const char *dir, size_t steps, const QaRunOps *ops,
                       QaRun **outRun)
{
    QaRun *run;
    size_t len;

    /* Step 1 (header order): the out-param is cleared before anything fails. */
    if (outRun != NULL) {
        *outRun = NULL;
    }
    /* Step 0: invalid operations table. */
    if (!opsValid(ops)) {
        return QA_ERR_RANGE;
    }
    /* Step 2: NULL out-param or path. */
    if (outRun == NULL || dir == NULL) {
        return QA_ERR_DOMAIN;
    }
    /* Step 3: path shape and bound, before `strlen` or any copy. */
    if (!dirPathValid(dir)) {
        return QA_ERR_RANGE;
    }
    /* Step 4: at least one step (FR-020). */
    if (steps == 0) {
        return QA_ERR_RANGE;
    }

    /* Bound: `dirPathValid` limits `len` to QA_IO_RUN_PATH_MAX, so
     * `sizeof *run + len + 1` cannot overflow `size_t`. */
    len = strlen(dir);
    run = ops -> alloc(ops -> ctx, sizeof *run + len + 1u);
    if (run == NULL) {
        return QA_ERR_NOMEM;
    }
    run -> ops = ops;
    run -> steps = steps;
    run -> next = 0;
    run -> width = digitCount(steps);
    run -> state = TRACE_NEW;
    run -> trace = NULL;
    run -> dirLen = len;
    /* Copy bound: the block holds `len + 1` bytes after the struct. */
    memcpy(run -> dir, dir, len + 1u);
    *outRun = run;
    return QA_OK;
}

/*
 * `qaRunOpen` (task T-013, FR-013).
 *
 * Implements the contract declared in `include/qa/io/run.h`: the same writer
 * with the libc-backed operations table.
 */
QaStatus qaRunOpen(const char *dir, size_t steps, QaRun **outRun)
{
    return qaRunOpenWith(dir, steps, &kLibcOps, outRun);
}

/**
 * @brief Build `<run dir>/<name>` into `out`.
 *
 * @param[in]  run  Non-NULL handle.
 * @param[in]  name Non-NULL relative path under the run dir, at most 63 bytes.
 * @param[out] out  Buffer of `PATH_BUF` bytes, written on `QA_OK`.
 *
 * @return `QA_OK`, or `QA_ERR_IO` when the formatted path would not fit.
 *
 * @owner No allocation; `out` stays with the caller.
 * @assumes `run -> dirLen <= QA_IO_RUN_PATH_MAX`, so with a name of at most 63
 *          bytes plus `/` and the NUL the path fits `PATH_BUF`; the result of
 *          `snprintf` is nevertheless checked.
 */
static QaStatus buildPath(const QaRun *run, const char *name, char *out)
{
    int n = snprintf(out, PATH_BUF, "%s/%s", run -> dir, name);

    /* A negative result or one at least `PATH_BUF` means truncation. */
    if (n < 0 || (size_t)n >= PATH_BUF) {
        return QA_ERR_IO;
    }
    return QA_OK;
}

/**
 * @brief Write `len` bytes through `ops -> write`, checking the full count.
 *
 * @param[in]     ops  Non-NULL valid operations table.
 * @param[in,out] file Open caller-owned stream.
 * @param[in]     buf  `len` readable bytes.
 * @param[in]     len  Byte count; zero is a successful no-op.
 *
 * @return `QA_OK` when all bytes were written, `QA_ERR_IO` on a short write.
 *
 * @owner No allocation; `file` stays with the caller.
 * @assumes `buf` holds at least `len` bytes, guaranteed by the callers.
 */
static QaStatus writeBytes(const QaRunOps *ops, FILE *file, const char *buf,
                           size_t len)
{
    if (len == 0) {
        return QA_OK;
    }
    /* Checked write: a count below `len` means a failed or partial write. */
    if (ops -> write(ops -> ctx, buf, 1, len, file) != len) {
        return QA_ERR_IO;
    }
    return QA_OK;
}

/**
 * @brief Write a NUL-terminated string literal or bounded buffer.
 *
 * @param[in]     ops  Non-NULL valid operations table.
 * @param[in,out] file Open caller-owned stream.
 * @param[in]     s    Non-NULL NUL-terminated string.
 *
 * @return `QA_OK` or `QA_ERR_IO`.
 *
 * @owner No allocation; `file` stays with the caller.
 * @assumes `s` is terminated, so `strlen` reads only its bytes.
 */
static QaStatus writeText(const QaRunOps *ops, FILE *file, const char *s)
{
    return writeBytes(ops, file, s, strlen(s));
}

/**
 * @brief Close a stream through `ops -> close` and report the result.
 *
 * @param[in]     ops  Non-NULL valid operations table.
 * @param[in,out] file Open stream, released by this call.
 *
 * @return `QA_OK`, or `QA_ERR_IO` when the close (flush) failed.
 *
 * @owner Releases `file` whatever the result; the caller must not reuse it.
 * @assumes `file` was opened through `ops -> open` and is closed once.
 */
static QaStatus closeStream(const QaRunOps *ops, FILE *file)
{
    return ops -> close(ops -> ctx, file) == 0 ? QA_OK : QA_ERR_IO;
}

/**
 * @brief Create the run dir, `snapshots/` and `trace.csv`, write the header.
 *
 * Called by the first `qaRunAppendTrace` (`j = 0`). On success the trace stream
 * is open and the state is `TRACE_OPEN`. On failure the stream is closed, the
 * partial files stay (FR-013) and the state is `TRACE_CLOSED`.
 *
 * @param[in,out] run Non-NULL handle in state `TRACE_NEW`.
 *
 * @return `QA_OK`, or `QA_ERR_IO` for any directory, open or write failure.
 *
 * @owner The trace stream is owned by `run` on success and released here on
 *        failure (single `cleanup` label).
 * @assumes The run dir was checked with `qaRunCheckOutDir` and is absent or an
 *          empty directory; `trace.csv` is opened exclusively so an existing file
 *          is never overwritten (EC-017).
 */
static QaStatus createLayout(QaRun *run)
{
    char path[PATH_BUF];
    FILE *file = NULL;
    QaStatus status = QA_ERR_IO;

    run -> state = TRACE_CLOSED;
    if (run -> ops -> makeDir(run -> ops -> ctx, run -> dir) != 0) {
        goto cleanup;
    }
    if (buildPath(run, "snapshots", path) != QA_OK ||
        run -> ops -> makeDir(run -> ops -> ctx, path) != 0) {
        goto cleanup;
    }
    if (buildPath(run, "trace.csv", path) != QA_OK) {
        goto cleanup;
    }
    file = run -> ops -> open(run -> ops -> ctx, path, "wx");
    if (file == NULL) {
        goto cleanup;
    }
    /* Size bound: `sizeof kTraceHeader - 1u` is the header length without its
     * NUL (non-empty string literal), so the write stays inside the array. */
    status = writeBytes(run -> ops, file, kTraceHeader, sizeof kTraceHeader - 1u);
    if (status != QA_OK) {
        goto cleanup;
    }
    run -> trace = file;
    run -> state = TRACE_OPEN;
    return QA_OK;

cleanup:
    if (file != NULL) {
        (void)closeStream(run -> ops, file); /* already failing; keep status */
    }
    return status;
}

/*
 * `qaRunAppendTrace` (task T-013, FR-013, FR-019, EC-009).
 *
 * Implements the contract declared in `include/qa/io/run.h`: validation in the
 * documented order, then the lazy layout on `j = 0`, then one `%.17g` row.
 */
QaStatus qaRunAppendTrace(QaRun *run, size_t j, double t, double a, double b,
                          double norm, double energy)
{
    char row[ROW_CAP];
    int n;
    QaStatus status;

    /* Step 1. */
    if (run == NULL) {
        return QA_ERR_DOMAIN;
    }
    /* Step 2: the stream was closed by `qaRunWriteConfig` or a failure. */
    if (run -> state == TRACE_CLOSED) {
        return QA_ERR_DOMAIN;
    }
    /* Step 3: rows arrive in order; `j > steps` also stops `next` at steps + 1. */
    if (j != run -> next || j > run -> steps) {
        return QA_ERR_RANGE;
    }
    /* Step 4: every value must be finite (ERR-1). */
    if (!isfinite(t) || !isfinite(a) || !isfinite(b) || !isfinite(norm) ||
        !isfinite(energy)) {
        return QA_ERR_DOMAIN;
    }
    /* Step 5: `j = 0` is the only call in state `TRACE_NEW`. */
    if (run -> state == TRACE_NEW) {
        status = createLayout(run);
        if (status != QA_OK) {
            return status;
        }
    }
    /* Step 6: the row fits `row` by the `ROW_CAP` bound; the result is checked. */
    n = snprintf(row, sizeof row, "%zu,%.17g,%.17g,%.17g,%.17g,%.17g\n", j, t, a,
                 b, norm, energy);
    if (n < 0 || (size_t)n >= sizeof row) {
        return QA_ERR_IO;
    }
    status = writeBytes(run -> ops, run -> trace, row, (size_t)n);
    if (status != QA_OK) {
        return status;
    }
    /* The index advances only after the row was written. */
    run -> next++;
    return QA_OK;
}

/**
 * @brief Report whether every probability is finite and non-negative.
 *
 * @param[in] probs Non-NULL array of `dim` values.
 * @param[in] dim   Element count, at least one.
 *
 * @return 1 when valid, 0 at the first invalid value.
 *
 * @owner No allocation; `probs` stays with the caller.
 * @assumes `probs` holds `dim` readable values.
 */
static int probsValid(const double *probs, size_t dim)
{
    /* Index bound: `k < dim`, the caller-supplied element count. */
    for (size_t k = 0; k < dim; k++) {
        if (!isfinite(probs[k]) || probs[k] < 0.0) {
            return 0;
        }
    }
    return 1;
}

/*
 * `qaRunWriteSnapshot` (task T-013, FR-013, FR-019, EC-009).
 *
 * Implements the contract declared in `include/qa/io/run.h`: the whole state is
 * validated before the file is created; the file is opened exclusively and
 * closed on every path through the single `cleanup` label.
 */
QaStatus qaRunWriteSnapshot(QaRun *run, size_t j, size_t dim, const double *probs)
{
    char name[64];
    char path[PATH_BUF];
    char row[SNAP_ROW_CAP];
    FILE *file = NULL;
    QaStatus status;
    int n;

    /* Step 1. */
    if (run == NULL || probs == NULL) {
        return QA_ERR_DOMAIN;
    }
    /* Step 2. */
    if (dim == 0 || j > run -> steps) {
        return QA_ERR_RANGE;
    }
    /* Step 3: needs the open trace, which implies the layout exists. */
    if (run -> state != TRACE_OPEN) {
        return QA_ERR_DOMAIN;
    }
    /* Step 4: all values before the file exists. */
    if (!probsValid(probs, dim)) {
        return QA_ERR_DOMAIN;
    }

    /* Name bound: `snapshots/snap_` (15) + at most 20 digits + `.csv` (4) + NUL
     * is 40 bytes, inside `name`; the result is checked anyway. `width` is the
     * digit count of `steps`, and `j <= steps`, so `j` never exceeds it. */
    n = snprintf(name, sizeof name, "snapshots/snap_%0*zu.csv", run -> width, j);
    if (n < 0 || (size_t)n >= sizeof name) {
        return QA_ERR_IO;
    }
    status = buildPath(run, name, path);
    if (status != QA_OK) {
        return status;
    }
    file = run -> ops -> open(run -> ops -> ctx, path, "wx");
    if (file == NULL) {
        return QA_ERR_IO;
    }
    /* Size bound: header length without its NUL. */
    status = writeBytes(run -> ops, file, kSnapHeader, sizeof kSnapHeader - 1u);
    if (status != QA_OK) {
        goto cleanup;
    }
    /* Index bound: `k < dim`, the length of `probs`. */
    for (size_t k = 0; k < dim; k++) {
        n = snprintf(row, sizeof row, "%zu,%.17g\n", k, probs[k]);
        if (n < 0 || (size_t)n >= sizeof row) {
            status = QA_ERR_IO;
            goto cleanup;
        }
        status = writeBytes(run -> ops, file, row, (size_t)n);
        if (status != QA_OK) {
            goto cleanup;
        }
    }
    /* The close flushes; its failure is a write failure. The stream is released
     * by the call whatever it returns. */
    status = closeStream(run -> ops, file);
    file = NULL;

cleanup:
    if (file != NULL) {
        (void)closeStream(run -> ops, file); /* already failing; keep status */
    }
    return status;
}

/**
 * @brief Write `"<s>"` with JSON escaping.
 *
 * `"` and `\` get a backslash; bytes below 0x20 become `\u00XX`; every other
 * byte (including UTF-8 continuation bytes) is copied. Safe runs are written in
 * one call each.
 *
 * @param[in]     ops  Non-NULL valid operations table.
 * @param[in,out] file Open caller-owned stream.
 * @param[in]     s    Non-NULL NUL-terminated string, at most
 *                     `QA_IO_RUN_FIELD_MAX` bytes.
 *
 * @return `QA_OK` or `QA_ERR_IO` at the first failed write.
 *
 * @owner No allocation; `file` and `s` stay with the caller.
 * @assumes `s` was bounded by `fitsWithin`, so every offset stays within
 *          `QA_IO_RUN_FIELD_MAX`.
 */
static QaStatus writeJsonString(const QaRunOps *ops, FILE *file, const char *s)
{
    size_t runStart = 0;
    size_t i;
    QaStatus status = writeText(ops, file, "\"");

    if (status != QA_OK) {
        return status;
    }
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        char esc[8];

        if (c != '"' && c != '\\' && c >= 0x20u) {
            continue;
        }
        /* Flush the safe run before the byte; `runStart <= i` by construction. */
        status = writeBytes(ops, file, s + runStart, i - runStart);
        if (status != QA_OK) {
            return status;
        }
        if (c == '"' || c == '\\') {
            /* Bound: `esc` holds 8 bytes; these three writes use indices 0..2. */
            esc[0] = '\\';
            esc[1] = (char)c;
            esc[2] = '\0';
        } else {
            /* `\u00XX` is 6 characters plus the NUL, inside `esc`. */
            (void)snprintf(esc, sizeof esc, "\\u00%02X", (unsigned int)c);
        }
        status = writeText(ops, file, esc);
        if (status != QA_OK) {
            return status;
        }
        runStart = i + 1u;
    }
    /* Tail run: `runStart <= i`, so the length cannot underflow. */
    status = writeBytes(ops, file, s + runStart, i - runStart);
    if (status != QA_OK) {
        return status;
    }
    return writeText(ops, file, "\"");
}

/**
 * @brief Write one member line `  "key": <value>` and its terminator.
 *
 * @param[in]     ops   Non-NULL valid operations table.
 * @param[in,out] file  Open caller-owned stream.
 * @param[in]     key   Non-NULL fixed key (no escaping needed).
 * @param[in]     text  Non-NULL value text, or NULL when `str` is used.
 * @param[in]     str   String value written with escaping, or NULL when `text`
 *                      is used.
 * @param[in]     last  Non-zero for the final member (no comma).
 *
 * @return `QA_OK` or `QA_ERR_IO` at the first failed write.
 *
 * @owner No allocation; everything stays with the caller.
 * @assumes Exactly one of `text` and `str` is non-NULL.
 */
static QaStatus writeMember(const QaRunOps *ops, FILE *file, const char *key,
                            const char *text, const char *str, int last)
{
    QaStatus status = writeText(ops, file, "  \"");

    if (status == QA_OK) {
        status = writeText(ops, file, key);
    }
    if (status == QA_OK) {
        status = writeText(ops, file, "\": ");
    }
    if (status == QA_OK) {
        status = text != NULL ? writeText(ops, file, text)
                              : writeJsonString(ops, file, str);
    }
    if (status == QA_OK) {
        status = writeText(ops, file, last ? "\n" : ",\n");
    }
    return status;
}

/**
 * @brief Write the whole `config.json` object (keys of spec §4, in order).
 *
 * @param[in]     ops    Non-NULL valid operations table.
 * @param[in,out] file   Open caller-owned stream.
 * @param[in]     config Non-NULL validated record.
 *
 * @return `QA_OK` or `QA_ERR_IO` at the first failed write.
 *
 * @owner No allocation; everything stays with the caller.
 * @assumes `config` passed the validation of `qaRunWriteConfig`: strings within
 *          `QA_IO_RUN_FIELD_MAX`, numbers finite; each number text fits
 *          `NUM_CAP` (`%.17g` is at most 24 characters, `%zu` at most 20).
 */
static QaStatus writeConfigBody(const QaRunOps *ops, FILE *file,
                                const QaRunConfig *config)
{
    char n[NUM_CAP];
    char steps[NUM_CAP];
    char big[NUM_CAP];
    char snap[NUM_CAP];
    char buf[NUM_CAP];
    QaStatus status = writeText(ops, file, "{\n");

    /* Each number is at most 24 characters, inside `NUM_CAP`. */
    (void)snprintf(n, sizeof n, "%u", config -> n);
    (void)snprintf(steps, sizeof steps, "%zu", config -> steps);
    (void)snprintf(snap, sizeof snap, "%zu", config -> snapshotInterval);
    (void)snprintf(big, sizeof big, "%u", QA_IO_RUN_FORMAT_VERSION);

    if (status == QA_OK) {
        status = writeMember(ops, file, "format_version", big, NULL, 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "spec", NULL, QA_IO_RUN_SPEC_NAME, 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "N", n, NULL, 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "family", NULL, config -> family, 0);
    }
    if (status == QA_OK) {
        (void)snprintf(buf, sizeof buf, "%.17g", config -> T);
        status = writeMember(ops, file, "T", buf, NULL, 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "steps", steps, NULL, 0);
    }
    if (status == QA_OK) {
        (void)snprintf(buf, sizeof buf, "%.17g", config -> dt);
        status = writeMember(ops, file, "dt", buf, NULL, 0);
    }
    if (status == QA_OK) {
        (void)snprintf(buf, sizeof buf, "%.17g", config -> r);
        status = writeMember(ops, file, "r", buf, NULL, 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "M", snap, NULL, 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "seed", NULL, "N/A", 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "git_sha", NULL, config -> gitSha, 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "clang_version", NULL,
                             config -> clangVersion, 0);
    }
    if (status == QA_OK) {
        status = writeMember(ops, file, "cmake_flags", NULL, config -> cmakeFlags,
                             1);
    }
    if (status == QA_OK) {
        status = writeText(ops, file, "}\n");
    }
    return status;
}

/**
 * @brief Validate the strings and numbers of a `QaRunConfig` (steps 2 to 4).
 *
 * @param[in] run    Non-NULL handle.
 * @param[in] config Non-NULL record.
 *
 * @return `QA_OK`, `QA_ERR_DOMAIN` (NULL string or non-finite number) or
 *         `QA_ERR_RANGE` (over-bound string, `n` outside `2..4`, steps mismatch).
 *
 * @owner No allocation; read-only.
 * @assumes Strings are NUL-terminated or readable for `QA_IO_RUN_FIELD_MAX + 1`
 *          bytes.
 */
static QaStatus validateConfig(const QaRun *run, const QaRunConfig *config)
{
    const char *strings[4];

    /* Bound: the four text fields of `QaRunConfig` fill indices 0..3 exactly. */
    strings[0] = config -> family;
    strings[1] = config -> gitSha;
    strings[2] = config -> clangVersion;
    strings[3] = config -> cmakeFlags;

    /* Step 2: NULL strings first, then the length bound of each (`i < 4`, the
     * length of `strings`). */
    for (size_t i = 0; i < 4; i++) {
        if (strings[i] == NULL) {
            return QA_ERR_DOMAIN;
        }
    }
    for (size_t i = 0; i < 4; i++) {
        if (!fitsWithin(strings[i], QA_IO_RUN_FIELD_MAX)) {
            return QA_ERR_RANGE;
        }
    }
    /* Step 3: `n` is a supported board (2..4) and `steps` equals the count the
     * handle was opened with, so the config matches the files written. */
    if (config -> n < 2u || config -> n > 4u || config -> steps != run -> steps) {
        return QA_ERR_RANGE;
    }
    /* Step 4: finite `T`, `dt` and `r`, so the `%.17g` formatting into a
     * `NUM_CAP` buffer never prints inf/nan and stays within its bound. */
    if (!isfinite(config -> T) || !isfinite(config -> dt) ||
        !isfinite(config -> r)) {
        return QA_ERR_DOMAIN;
    }
    return QA_OK;
}

/*
 * `qaRunWriteConfig` (task T-013, FR-014, FR-019, EC-009, EC-013).
 *
 * Implements the contract declared in `include/qa/io/run.h`: validation, close
 * of the trace, then `config.json.tmp` written exclusively and renamed. The
 * temporary file is removed on every failure after its creation.
 */
QaStatus qaRunWriteConfig(QaRun *run, const QaRunConfig *config)
{
    char tmpPath[PATH_BUF];
    char finalPath[PATH_BUF];
    FILE *file = NULL;
    int tmpCreated = 0;
    QaStatus status;

    /* Step 1. */
    if (run == NULL || config == NULL) {
        return QA_ERR_DOMAIN;
    }
    /* Steps 2 to 4. */
    status = validateConfig(run, config);
    if (status != QA_OK) {
        return status;
    }
    /* Step 5: the trace must be open (the layout exists, nothing closed yet). */
    if (run -> state != TRACE_OPEN) {
        return QA_ERR_DOMAIN;
    }

    /* Step 6: the stream is released in every case, so the state moves first. */
    file = run -> trace;
    run -> trace = NULL;
    run -> state = TRACE_CLOSED;
    status = closeStream(run -> ops, file);
    file = NULL;
    if (status != QA_OK) {
        return status;
    }

    status = buildPath(run, "config.json.tmp", tmpPath);
    if (status != QA_OK) {
        return status;
    }
    status = buildPath(run, "config.json", finalPath);
    if (status != QA_OK) {
        return status;
    }
    /* Step 7: exclusive creation, never overwriting an existing sibling. */
    file = run -> ops -> open(run -> ops -> ctx, tmpPath, "wx");
    if (file == NULL) {
        return QA_ERR_IO;
    }
    tmpCreated = 1;
    status = writeConfigBody(run -> ops, file, config);
    if (status != QA_OK) {
        goto cleanup;
    }
    status = closeStream(run -> ops, file);
    file = NULL;
    if (status != QA_OK) {
        goto cleanup;
    }
    /* Step 8: the only step that produces `config.json`. */
    if (run -> ops -> rename(run -> ops -> ctx, tmpPath, finalPath) != 0) {
        status = QA_ERR_IO;
        goto cleanup;
    }
    tmpCreated = 0;
    status = QA_OK;

cleanup:
    if (file != NULL) {
        (void)closeStream(run -> ops, file); /* already failing; keep status */
    }
    if (tmpCreated) {
        /* Best effort; `config.json` was never written, so the run is invalid. */
        (void)run -> ops -> remove(run -> ops -> ctx, tmpPath);
    }
    return status;
}

/*
 * `qaRunDestroy` (task T-013, FR-019, MEM-1).
 *
 * Implements the contract declared in `include/qa/io/run.h`: closes the trace if
 * it is still open, then frees the single block. Partial files stay.
 */
QaStatus qaRunDestroy(QaRun *run)
{
    QaStatus status = QA_OK;

    if (run == NULL) {
        return QA_OK;
    }
    if (run -> state == TRACE_OPEN) {
        status = closeStream(run -> ops, run -> trace);
        run -> trace = NULL;
    }
    /* The handle was acquired through the same table (kept in `run -> ops`). */
    run -> ops -> release(run -> ops -> ctx, run);
    return status;
}
