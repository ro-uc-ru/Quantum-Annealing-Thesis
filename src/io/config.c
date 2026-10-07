/*
 * 004-driver Phase 4 implementation (task T-010): CSV configuration writer.
 *
 * Purpose: write the `v1` configuration record (FR-014) as one header line and
 * one RFC 4180 row, replacing the target only on success. The contract is
 * declared in `include/qa/io/config.h` (public) and `src/io/config-internal.h`
 * (operations table and `qaIoWriteConfigWith`).
 *
 * Ownership: no heap allocation, no globals other than the constant libc
 * operations table. The only resource is the temporary `FILE *`, opened and
 * closed inside `qaIoWriteConfigWith` through `ops -> open` / `ops -> close`;
 * the single `cleanup` label is its only release path. The temporary path
 * lives in a stack buffer sized from the validated `path` bound.
 *
 * Errors: arguments and bounds are validated, in the order documented in the
 * header, before any file is created (`QA_ERR_RANGE`, nothing touched). Every
 * file operation is checked and any failure returns `QA_ERR_IO` after closing
 * and removing the temporary file; the target is only ever touched by the
 * final `rename`, so it stays unchanged on every failure.
 *
 * Numerical assumptions: byte counts are `size_t` values bounded by
 * `QA_IO_CONFIG_PATH_MAX` and `QA_IO_CONFIG_FIELD_MAX` before any copy or
 * write, so no size arithmetic can overflow.
 */

#include "qa/io/config.h"

#include "config-internal.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Suffix of the temporary sibling file (`path + ".tmp"`). */
#define TMP_SUFFIX ".tmp"
#define TMP_SUFFIX_LEN 4u

/* Fixed FR-014 header line, newline included. */
static const char kHeader[] =
    "spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed,"
    "schedule,dt_steps\n";

/** libc-backed operations table used by `qaIoWriteConfig` (constant data). */
static const QaIoOps kLibcOps = {fopen, fwrite, fclose, rename, remove};

/**
 * @brief Report whether `s` has at most `maxLen` bytes before its NUL.
 *
 * Scans at most `maxLen + 1` bytes, so an unterminated or huge string is
 * never read beyond the first byte past the bound.
 *
 * @param[in] s      Non-NULL caller-owned NUL-terminated string.
 * @param[in] maxLen Largest accepted length, excluding the NUL.
 *
 * @return 1 when `strlen(s) <= maxLen`, 0 otherwise.
 *
 * @owner No allocation; `s` stays owned by the caller.
 * @assumes `s` is readable up to its NUL or `maxLen + 1` bytes, whichever
 *          comes first; `maxLen + 1` cannot overflow for the project bounds.
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
 * @brief Write `len` bytes through `ops -> write`, checking the full count.
 *
 * @param[in]     ops  Non-NULL operations table with a non-NULL `write`.
 * @param[in,out] file Open caller-owned stream.
 * @param[in]     buf  `len` readable bytes.
 * @param[in]     len  Byte count; zero is a successful no-op.
 *
 * @return `QA_OK` when all bytes were written, `QA_ERR_IO` on a short write.
 *
 * @owner No allocation; `file` stays owned by the caller.
 * @assumes `buf` holds at least `len` bytes, guaranteed by the callers, which
 *          pass string slices measured with `strlen` or `sizeof`.
 */
static QaStatus writeBytes(const QaIoOps *ops, FILE *file, const char *buf,
                           size_t len)
{
    if (len == 0) {
        return QA_OK;
    }
    /* Checked write: a count below `len` means a failed or partial write. */
    if (ops -> write(buf, 1, len, file) != len) {
        return QA_ERR_IO;
    }
    return QA_OK;
}

/**
 * @brief Write one field per RFC 4180, quoting it only when required.
 *
 * A field containing a comma, double quote, CR or LF is wrapped in double
 * quotes with every embedded double quote doubled (EC-027); any other field
 * is written verbatim. Quote doubling is done in runs: each run ending at a
 * double quote is written once and the quote is written a second time.
 *
 * @param[in]     ops   Non-NULL operations table.
 * @param[in,out] file  Open caller-owned stream.
 * @param[in]     field Non-NULL NUL-terminated field, already bounded by
 *                      `QA_IO_CONFIG_FIELD_MAX`.
 *
 * @return `QA_OK` on success, `QA_ERR_IO` on the first failed write.
 *
 * @owner No allocation; `file` and `field` stay owned by the caller.
 * @assumes `field` was validated by `fitsWithin`, so every offset below stays
 *          within `QA_IO_CONFIG_FIELD_MAX` and cannot overflow.
 */
static QaStatus writeField(const QaIoOps *ops, FILE *file, const char *field)
{
    size_t len = strlen(field);
    int needsQuotes = strpbrk(field, ",\"\r\n") != NULL;
    size_t runStart = 0;
    QaStatus status;

    if (!needsQuotes) {
        return writeBytes(ops, file, field, len);
    }

    status = writeBytes(ops, file, "\"", 1);
    if (status != QA_OK) {
        return status;
    }
    for (size_t i = 0; i < len; i++) {
        if (field[i] == '"') {
            /* Write the run including this quote, then its doubling quote.
             * Bound: `i < len`, so `i + 1 - runStart` stays within `len`. */
            status = writeBytes(ops, file, field + runStart, i + 1 - runStart);
            if (status != QA_OK) {
                return status;
            }
            status = writeBytes(ops, file, "\"", 1);
            if (status != QA_OK) {
                return status;
            }
            runStart = i + 1;
        }
    }
    /* Tail run: `runStart <= len`, so the length below cannot underflow. */
    status = writeBytes(ops, file, field + runStart, len - runStart);
    if (status != QA_OK) {
        return status;
    }
    return writeBytes(ops, file, "\"", 1);
}

/**
 * @brief Write the data row: ten fields separated by commas, then `\n`.
 *
 * @param[in]     ops    Non-NULL operations table.
 * @param[in,out] file   Open caller-owned stream.
 * @param[in]     fields Ten non-NULL bounded strings in column order.
 *
 * @return `QA_OK` on success, `QA_ERR_IO` on the first failed write.
 *
 * @owner No allocation; `file` and `fields` stay owned by the caller.
 * @assumes `fields` has exactly ten valid entries, built by the caller.
 */
static QaStatus writeRow(const QaIoOps *ops, FILE *file,
                         const char *const fields[10])
{
    /* Index bound: `fields` has exactly ten entries, so `i < 10` keeps
     * `fields[i]` in range. */
    for (size_t i = 0; i < 10; i++) {
        QaStatus status;

        if (i > 0) {
            status = writeBytes(ops, file, ",", 1);
            if (status != QA_OK) {
                return status;
            }
        }
        status = writeField(ops, file, fields[i]);
        if (status != QA_OK) {
            return status;
        }
    }
    return writeBytes(ops, file, "\n", 1);
}

/*
 * `qaIoWriteConfigWith` (task T-010, FR-014, FR-015, EC-021..EC-024,
 * EC-027..EC-029).
 *
 * Implements the contract declared in `src/io/config-internal.h`, with the
 * validation order of `qa/io/config.h` after the extra `ops` step. All
 * validation finishes before `ops -> open`, so a rejected call creates no
 * file. The temporary file is opened with `"wx"` so an existing sibling is
 * never overwritten; the target changes only through the final `rename`.
 */
QaStatus qaIoWriteConfigWith(const char *path, const QaConfigRecord *record,
                             const QaIoOps *ops)
{
    char tmpPath[QA_IO_CONFIG_PATH_MAX + TMP_SUFFIX_LEN + 1u];
    const char *fields[10];
    FILE *file = NULL;
    size_t pathLen = 0;
    int tmpCreated = 0;
    QaStatus status = QA_ERR_IO;

    /* Step 0: the operations table and every entry must exist. */
    if (ops == NULL || ops -> open == NULL || ops -> write == NULL ||
        ops -> close == NULL || ops -> rename == NULL ||
        ops -> remove == NULL) {
        return QA_ERR_RANGE;
    }
    /* Step 1: NULL arguments (EC-024). */
    if (path == NULL || record == NULL) {
        return QA_ERR_RANGE;
    }

    fields[0] = record -> specVersion;
    fields[1] = record -> n;
    fields[2] = record -> dim;
    fields[3] = record -> vectors;
    fields[4] = record -> gitSha;
    fields[5] = record -> clangVersion;
    fields[6] = record -> cmakeFlags;
    fields[7] = record -> seed;
    fields[8] = record -> schedule;
    fields[9] = record -> dtSteps;

    /* Step 2: NULL fields (EC-024). */
    for (size_t i = 0; i < 10; i++) {
        if (fields[i] == NULL) {
            return QA_ERR_RANGE;
        }
    }
    /* Step 3: path bound (EC-028). Bounded before the copy below. */
    if (!fitsWithin(path, QA_IO_CONFIG_PATH_MAX)) {
        return QA_ERR_RANGE;
    }
    /* Step 4: field bound (EC-028), every field before any file exists. */
    for (size_t i = 0; i < 10; i++) {
        if (!fitsWithin(fields[i], QA_IO_CONFIG_FIELD_MAX)) {
            return QA_ERR_RANGE;
        }
    }

    /* Build `path + ".tmp"`. Bound: `pathLen <= QA_IO_CONFIG_PATH_MAX`, so the
     * copy plus suffix plus NUL fits `tmpPath` exactly. */
    pathLen = strlen(path);
    memcpy(tmpPath, path, pathLen);
    memcpy(tmpPath + pathLen, TMP_SUFFIX, TMP_SUFFIX_LEN);
    tmpPath[pathLen + TMP_SUFFIX_LEN] = '\0';

    /* Exclusive open: fails (and creates nothing) if the sibling exists, a
     * directory is missing or not writable (EC-021). */
    file = ops -> open(tmpPath, "wx");
    if (file == NULL) {
        return QA_ERR_IO;
    }
    tmpCreated = 1;

    /* Size bound: `sizeof kHeader - 1u` is the header length without its
     * terminating NUL (the array is a non-empty string literal), so the
     * write stays inside `kHeader`. */
    status = writeBytes(ops, file, kHeader, sizeof kHeader - 1u);
    if (status != QA_OK) {
        goto cleanup;
    }
    status = writeRow(ops, file, fields);
    if (status != QA_OK) {
        goto cleanup;
    }

    /* Close flushes; a failure here (EC-022) is a write failure. The stream
     * is released by this call whatever it returns, so it is not reused. */
    if (ops -> close(file) != 0) {
        file = NULL;
        status = QA_ERR_IO;
        goto cleanup;
    }
    file = NULL;

    /* Atomic replace: the only step that touches the target (EC-023). */
    if (ops -> rename(tmpPath, path) != 0) {
        status = QA_ERR_IO;
        goto cleanup;
    }
    tmpCreated = 0;
    status = QA_OK;

cleanup:
    if (file != NULL) {
        (void)ops -> close(file); /* already failing; status is kept */
    }
    if (tmpCreated) {
        (void)ops -> remove(tmpPath); /* best effort; target is untouched */
    }
    return status;
}

/*
 * `qaIoWriteConfig` (task T-010, FR-014, FR-015).
 *
 * Implements the contract declared in `include/qa/io/config.h`: the same
 * writer with the libc-backed operations table.
 */
QaStatus qaIoWriteConfig(const char *path, const QaConfigRecord *record)
{
    return qaIoWriteConfigWith(path, record, &kLibcOps);
}
