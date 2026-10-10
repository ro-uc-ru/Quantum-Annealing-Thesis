#ifndef QA_IO_RUN_H
#define QA_IO_RUN_H

/*
 * Run records of the evolution CLI: the `--out` check, the streamed trace and
 * snapshot files, and `config.json` written last (spec 005-evolution, T-004).
 *
 * Scope: FR-013 (streamed `trace.csv` and `snapshots/snap_<j>.csv` during a run,
 * in the formats of section 4), FR-014 (`config.json` written last; its presence
 * marks a valid run), FR-019 (`QA_ERR_NOMEM` and `QA_ERR_IO` on failure, every
 * resource released, no `config.json`), FR-025 (write and allocation failures
 * injected through the private ops table of `src/io/run-internal.h`) and EC-017
 * (the `--out` rules, checked without creating anything). Declarations only;
 * behavior lives in `src/io/run.c`. The writers allocate only the run handle and
 * its own path strings; the open trace stream is the only other resource.
 *
 * Definitions fixed by spec §1 and §4 and used by every function below:
 *   run dir       the path given to `--out`; every record lives under it
 *   lazy          the run dir, `snapshots/` and `trace.csv` are created by the
 *                 first `qaRunAppendTrace` (`j = 0`), never earlier
 *   trace.csv     header `j,t,a,b,norm,energy`, then one row per `j = 0..steps`
 *   snapshots/    holds `snap_<j>.csv`, `j` zero-padded to the digit count of
 *                 `steps`
 *   snap_<j>.csv  header `k,p`, then `dim` rows with `k = 0..dim-1`
 *   config.json   one JSON object with the keys `format_version` (1), `spec`
 *                 (`"005-evolution"`), `N`, `family`, `T`, `steps`, `dt`, `r`,
 *                 `M`, `seed` (`"N/A"`), `git_sha`, `clang_version` and
 *                 `cmake_flags`, in that order; one key per line with a two-space
 *                 indent; strings escaped (`"` and `\` with a backslash, bytes
 *                 below 0x20 as `\u00XX`)
 *   CSV           numbers with `%.17g`, separator `,`, line end `\n`, header row
 *                 first; no quoting is needed because every field is a number
 *
 * Numerical bounds: the run dir path has at most `QA_IO_RUN_PATH_MAX` bytes,
 * excluding the NUL, and each `config.json` string has at most
 * `QA_IO_RUN_FIELD_MAX` bytes. Every path is formed in a buffer of
 * `QA_IO_RUN_PATH_MAX + 64` bytes, so it is never truncated after the bound
 * check. A `%.17g` number has at most 24 characters, so every formatted row
 * fits its stack buffer (see `src/io/run.c`).
 */

#include <stddef.h>

#include "qa/core/status.h"

/** Maximum byte length of the run dir path, excluding the NUL (FR-015, EC-017). */
#define QA_IO_RUN_PATH_MAX 1024u

/** Maximum byte length of each `config.json` string, excluding the NUL (FR-014). */
#define QA_IO_RUN_FIELD_MAX 4096u

/** Value written as `format_version` in `config.json` (spec §4, now 1). */
#define QA_IO_RUN_FORMAT_VERSION 1u

/** Value written as `spec` in `config.json` (spec §4). */
#define QA_IO_RUN_SPEC_NAME "005-evolution"

/**
 * @brief Caller-supplied fields of `config.json` (FR-014, TST-4).
 *
 * Fields are listed in the key order of section 4. The keys `format_version`,
 * `spec` and `seed` are fixed by the writer and are not fields here. The string
 * fields are NUL-terminated; the writer measures and escapes them.
 *
 * @owner The caller owns the record and every string it points to; the writer
 *        only reads them during the call and keeps no pointer after returning.
 * @assumes `n` is `2..4`, `T`, `dt` and `r` are finite, and `steps` equals the
 *          `steps` given to `qaRunOpen`, which the writer checks (FR-014).
 */
typedef struct QaRunConfig {
    unsigned int n;           /* key N, board side 2..4 */
    const char *family;       /* key family, 003 schedule name */
    double T;                 /* key T, total time */
    size_t steps;             /* key steps, equal to the run's steps */
    double dt;                /* key dt, T / steps */
    double r;                 /* key r, the constant 0.5 */
    size_t snapshotInterval;  /* key M, snapshot interval, 0 = final only */
    const char *gitSha;       /* key git_sha */
    const char *clangVersion; /* key clang_version */
    const char *cmakeFlags;   /* key cmake_flags */
} QaRunConfig;

/**
 * @brief Opaque handle that streams the records of one run (FR-013, FR-019).
 *
 * The handle holds a copy of the run dir path, the open trace
 * stream while it is open, the next expected trace index and the file-operation
 * table used for every write. Its layout is private to `src/io/run.c`.
 *
 * @owner Created by `qaRunOpen` and owned by the caller until `qaRunDestroy`,
 *        which releases every resource the handle holds. Passed by pointer to
 *        the writers and never freed by them.
 * @assumes One thread drives the handle at a time, and nothing else writes into
 *          the run dir during the run (EC-017: the check and the lazy creation are
 *          not atomic).
 */
typedef struct QaRun QaRun;

/**
 * @brief Check the `--out` directory without creating anything (FR-015, EC-017).
 *
 * Fixed validation order, completed before any file-system call:
 *   1. `dir == NULL` -> `QA_ERR_DOMAIN`.
 *   2. `dir` is empty, ends with `/` or has more than `QA_IO_RUN_PATH_MAX` bytes
 *      -> `QA_ERR_RANGE`.
 * Then the path is inspected with `stat` and `readdir` only; a symbolic link is
 * followed for steps 3 to 5:
 *   3. The path is absent: its parent (the path before the last `/`, or `.` when
 *      there is none) must exist and be a directory, else `QA_ERR_IO` (EC-017).
 *   4. The path is a regular file, or a link that does not resolve to a
 *      directory -> `QA_ERR_IO` (EC-017).
 *   5. The path is a directory with any entry other than `.` and `..` ->
 *      `QA_ERR_IO` (EC-017). An empty directory is accepted.
 *   6. Any other `stat`, `readdir` or `closedir` failure -> `QA_ERR_IO` (ERR-1).
 *
 * @param[in] dir Non-NULL caller-owned NUL-terminated run dir path.
 *
 * @return `QA_OK` (the path may be created, or it is an empty directory);
 *         `QA_ERR_DOMAIN` (`dir` is NULL); `QA_ERR_RANGE` (empty, ends with `/`
 *         or is longer than `QA_IO_RUN_PATH_MAX`); `QA_ERR_IO` (any rejection of
 *         steps 3 to 6). Nothing is created or written on any path.
 *
 * @owner No allocation. `dir` stays owned by the caller on every path.
 * @assumes The check is not atomic with the later lazy creation; a concurrent
 *          creator is outside the contract (EC-017). The path is compared as
 *          given, with no normalization.
 */
QaStatus qaRunCheckOutDir(const char *dir);

/**
 * @brief Create the handle of one run; the file system is not touched (FR-013,
 * FR-019).
 *
 * The handle copies the run dir path and derives the snapshot names from `steps`,
 * so the caller's `dir` may be freed after the call. Nothing is created here: the
 * directories and `trace.csv` are created lazily by the first `qaRunAppendTrace`
 * (FR-013). Fixed validation order, completed before any allocation:
 *   1. `outRun == NULL` -> `QA_ERR_DOMAIN`. Otherwise `*outRun` is set to NULL.
 *   2. `dir == NULL` -> `QA_ERR_DOMAIN`.
 *   3. `dir` is empty, ends with `/` or is longer than `QA_IO_RUN_PATH_MAX`
 *      -> `QA_ERR_RANGE`.
 *   4. `steps == 0` -> `QA_ERR_RANGE` (the evolution has at least one step, FR-020).
 *   5. The handle or its copies cannot be allocated -> `QA_ERR_NOMEM` (FR-019),
 *      nothing kept.
 *
 * @param[in]  dir    Non-NULL caller-owned NUL-terminated run dir path, at most
 *                    `QA_IO_RUN_PATH_MAX` bytes and not ending with `/`.
 * @param[in]  steps  Number of Strang steps, `>= 1`; fixes the zero-padded width
 *                    of the snapshot names.
 * @param[out] outRun Non-NULL receiver of the handle. Set to NULL on entry and
 *                    written only on `QA_OK`.
 *
 * @return `QA_OK` (`*outRun` holds the handle); `QA_ERR_DOMAIN` (NULL `dir` or
 *         `outRun`); `QA_ERR_RANGE` (bad `dir` or `steps == 0`); `QA_ERR_NOMEM`
 *         (allocation failed). `*outRun` is NULL on every failure.
 *
 * @owner On `QA_OK` the caller owns `*outRun` and must pass it to `qaRunDestroy`
 *        exactly once. Nothing is owned after a failure.
 * @assumes `steps` is the total the caller later passes in `QaRunConfig`. The
 *          digit count of `steps` is computed by repeated unsigned division, so
 *          no overflow occurs for any `size_t`.
 */
QaStatus qaRunOpen(const char *dir, size_t steps, QaRun **outRun);

/**
 * @brief Append one `trace.csv` row, creating the run layout on `j = 0`
 * (FR-013, FR-019).
 *
 * Rows arrive in order, one per `j`. On the first row (`j = 0`) the run dir,
 * `snapshots/` and `trace.csv` are created, the last one exclusively, and the
 * header is written. Fixed validation order, completed before any file write:
 *   1. `run == NULL` -> `QA_ERR_DOMAIN`.
 *   2. The trace stream is already closed (after `qaRunWriteConfig`) -> `QA_ERR_DOMAIN`.
 *   3. `j` is not the next expected index (0 first, then one more each call) or
 *      `j > steps` -> `QA_ERR_RANGE` (FR-013: one row per `j`, in order).
 *   4. `t`, `a`, `b`, `norm` or `energy` is not finite -> `QA_ERR_DOMAIN` (ERR-1).
 *   5. On `j = 0`: the run dir and `snapshots/` are made, `trace.csv` is opened
 *      exclusively and the header is written; any failure -> `QA_ERR_IO`
 *      (FR-019, EC-009, EC-017), the stream is closed and the partial files stay
 *      (FR-013).
 *   6. The row is formatted with `%.17g` and written; a short write or a failed
 *      format -> `QA_ERR_IO` (FR-019, EC-009).
 * The expected index advances only on `QA_OK`.
 *
 * @param[in,out] run    Non-NULL handle from `qaRunOpen`.
 * @param[in]     j      Step index, `0..steps`, equal to the next expected index.
 * @param[in]     t      Time `t_j`, finite.
 * @param[in]     a      Weight `a(t_j)`, finite.
 * @param[in]     b      Weight `b(t_j)`, finite.
 * @param[in]     norm   `sqrt(sum_k p_k)`, finite.
 * @param[in]     energy `<H(t_j)>`, finite.
 *
 * @return `QA_OK` (row written); `QA_ERR_DOMAIN` (NULL `run`, closed trace, or a
 *         non-finite value); `QA_ERR_RANGE` (out-of-order or over-range `j`);
 *         `QA_ERR_IO` (any file failure of steps 5 and 6).
 *
 * @owner No allocation. The trace stream is owned by `run` and is closed by
 *        `qaRunWriteConfig` or `qaRunDestroy`, never by this call on success.
 * @assumes Values are passed as the evolution computed them; the writer does not
 *          check their ranges (the evolution does, FR-005, FR-006). Output is
 *          deterministic: equal rows give byte-identical files (EC-013).
 */
QaStatus qaRunAppendTrace(QaRun *run, size_t j, double t, double a, double b,
                          double norm, double energy);

/**
 * @brief Write the snapshot file `snapshots/snap_<j>.csv` (FR-013, FR-019).
 *
 * Called for the steps of FR-012 only, after the observer call of the same `j`.
 * The file is created exclusively, its header `k,p` is written, then one row per
 * board id. Fixed validation order, completed before the file is created:
 *   1. `run == NULL` or `probs == NULL` -> `QA_ERR_DOMAIN`.
 *   2. `dim == 0` or `j > steps` -> `QA_ERR_RANGE`.
 *   3. The run layout does not exist yet (no `qaRunAppendTrace` with `j = 0`) or
 *      the trace is closed -> `QA_ERR_DOMAIN` (call order, FR-028).
 *   4. Any `probs[k]` is not finite or is negative -> `QA_ERR_DOMAIN`. All `dim`
 *      values are checked before the file is created, so an invalid state never
 *      produces a file.
 * Then, in the file system:
 *   5. The file cannot be created, a row cannot be written, or the close fails
 *      -> `QA_ERR_IO` (FR-019, EC-009). The partial file stays (FR-013).
 *
 * @param[in,out] run   Non-NULL handle from `qaRunOpen`.
 * @param[in]     j     Step index, `0..steps`.
 * @param[in]     dim   Number of board ids, `2^(n*n)`; at least one.
 * @param[in]     probs Non-NULL caller-owned array of `dim` probabilities
 *                      `p_k = |phi_k|^2`.
 *
 * @return `QA_OK` (file written); `QA_ERR_DOMAIN` (NULL argument, call order, or
 *         a non-finite or negative probability); `QA_ERR_RANGE` (`dim == 0` or
 *         `j > steps`); `QA_ERR_IO` (any file failure of step 5).
 *
 * @owner No allocation. `probs` stays owned by the caller; the file is closed
 *        before the call returns on every path.
 * @assumes `probs` holds `dim` readable values that do not alias the handle. The
 *          file is named from `j` with the zero-padded width fixed by `steps`.
 */
QaStatus qaRunWriteSnapshot(QaRun *run, size_t j, size_t dim, const double *probs);

/**
 * @brief Close the trace and write `config.json` last (FR-014, FR-019).
 *
 * `config.json` is the record that marks a valid run, so it is written only after
 * every trace row and snapshot succeeded. Fixed validation order, completed
 * before any file is touched:
 *   1. `run == NULL` or `config == NULL` -> `QA_ERR_DOMAIN`.
 *   2. `family`, `gitSha`, `clangVersion` or `cmakeFlags` is NULL -> `QA_ERR_DOMAIN`;
 *      any of them longer than `QA_IO_RUN_FIELD_MAX` -> `QA_ERR_RANGE`.
 *   3. `n` outside `2..4`, or `steps` not equal to the run's steps -> `QA_ERR_RANGE`.
 *   4. `T`, `dt` or `r` is not finite -> `QA_ERR_DOMAIN`.
 *   5. The run layout does not exist yet, or the trace is already closed
 *      -> `QA_ERR_DOMAIN` (call order).
 * Then the file system, in this order:
 *   6. The trace stream is closed and its result checked -> `QA_ERR_IO` on failure
 *      (FR-019, EC-009). The stream is released in every case.
 *   7. `config.json.tmp` is created exclusively, the object is written with the
 *      keys of section 4 and the file is closed; any failure -> `QA_ERR_IO` after
 *      the temporary file is removed.
 *   8. The temporary file is renamed to `config.json`; a failure -> `QA_ERR_IO`
 *      after the temporary file is removed (FR-014, EC-013).
 * On any failure no `config.json` is written, so the run is not valid (EC-009,
 * EC-020).
 *
 * @param[in,out] run    Non-NULL handle from `qaRunOpen`; the trace is closed.
 * @param[in]     config Non-NULL caller-owned record, see `QaRunConfig`.
 *
 * @return `QA_OK` (`config.json` written; the temporary file no longer exists);
 *         `QA_ERR_DOMAIN` (NULL argument, NULL string, non-finite number, or call
 *         order); `QA_ERR_RANGE` (over-bound string, `n` outside `2..4` or steps
 *         mismatch); `QA_ERR_IO` (any file failure of steps 6 to 8).
 *
 * @owner No allocation. `config` stays owned by the caller. The trace stream is
 *        released on every path; the temporary file is removed on every failure
 *        after its creation.
 * @assumes Keys, order and layout are fixed, so equal inputs give byte-identical
 *          files on the same build (EC-013). `rename` replaces the target
 *          atomically on both supported platforms (LIM-1).
 */
QaStatus qaRunWriteConfig(QaRun *run, const QaRunConfig *config);

/**
 * @brief Release a run handle and any open trace stream (FR-019, MEM-1).
 *
 * Closes the trace stream if it is still open (a run that never reached
 * `qaRunWriteConfig`, or failed midway), then frees the handle and its strings.
 * `NULL` is accepted and does nothing. The pointer is invalid after the call, so
 * the caller must clear its copy.
 *
 * @param[in] run Handle from `qaRunOpen`, or NULL.
 *
 * @return `QA_OK` (handle released; also for NULL); `QA_ERR_IO` (the trace stream
 *         failed to close; the handle is released in every case and the run is
 *         invalid without `config.json`, FR-014).
 *
 * @owner Releases everything `qaRunOpen` acquired. Does not remove any partial
 *        file: the run dir keeps what was written (FR-013).
 * @assumes Called once per handle; a second call on the same pointer is a use
 *          after free and is not allowed.
 */
QaStatus qaRunDestroy(QaRun *run);

#endif /* QA_IO_RUN_H */
