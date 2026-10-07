#ifndef QA_IO_CONFIG_H
#define QA_IO_CONFIG_H

/*
 * Reproducible run-configuration record written as versioned CSV
 * (spec 004-driver, T-002).
 *
 * Scope: FR-014 (CSV `v1` with a fixed header and exactly one data row) and
 * FR-015 (failure behavior and argument bounds), plus the edge cases EC-021
 * to EC-024 and EC-027 to EC-029. Declarations only; behavior lives in
 * `src/io/config.c`. The writer allocates no heap memory.
 *
 * Definitions fixed by spec §1 and used by every function below:
 *   header     `spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,
 *              seed,schedule,dt_steps` (one line, no spaces), then one row
 *   field      one NUL-terminated string of `QaConfigRecord`, in header order
 *   RFC 4180   a field is quoted when it contains a comma, a double quote, CR
 *              or LF; embedded double quotes are doubled; rows end with `\n`
 *   replace    on success an existing target is replaced entirely, written
 *              through `path + ".tmp"` and `rename`
 *
 * Numerical bounds: `path` has at most `QA_IO_CONFIG_PATH_MAX` bytes and every
 * field at most `QA_IO_CONFIG_FIELD_MAX` bytes, both excluding the NUL; all
 * bounds are validated before any file is created.
 */

#include "qa/core/status.h"

/** Maximum byte length of `path`, excluding the NUL (FR-015). */
#define QA_IO_CONFIG_PATH_MAX 1024u

/** Maximum byte length of every record field, excluding the NUL (FR-015). */
#define QA_IO_CONFIG_FIELD_MAX 4096u

/**
 * @brief The ten configuration fields of one CSV row (FR-014).
 *
 * Fields are listed in CSV column order: `specVersion` -> `spec_version`,
 * `n` -> `N`, `dim`, `vectors`, `gitSha` -> `git_sha`, `clangVersion` ->
 * `clang_version`, `cmakeFlags` -> `cmake_flags`, `seed`, `schedule` and
 * `dtSteps` -> `dt_steps`. Every field must be non-NULL, NUL-terminated and at
 * most `QA_IO_CONFIG_FIELD_MAX` bytes; a NULL or over-bound field makes the
 * writer return `QA_ERR_RANGE` (EC-024, EC-028).
 *
 * @owner The caller owns the record and every string it points to; the writer
 *        only reads them and keeps no pointer after returning.
 * @assumes The strings stay valid and unmodified for the whole call; their
 *          lengths are measured by the writer, never trusted from the caller.
 */
typedef struct QaConfigRecord {
    const char *specVersion;  /* column spec_version */
    const char *n;            /* column N */
    const char *dim;          /* column dim */
    const char *vectors;      /* column vectors */
    const char *gitSha;       /* column git_sha */
    const char *clangVersion; /* column clang_version */
    const char *cmakeFlags;   /* column cmake_flags */
    const char *seed;         /* column seed */
    const char *schedule;     /* column schedule */
    const char *dtSteps;      /* column dt_steps */
} QaConfigRecord;

/**
 * @brief Write the configuration record as a `v1` CSV file (FR-014, FR-015).
 *
 * Never creates directories: the directory of `path` must already exist.
 * Fixed validation order, completed before any file is created:
 *   1. `path == NULL` or `record == NULL` -> `QA_ERR_RANGE` (FR-015, EC-024).
 *   2. Any of the ten fields is NULL -> `QA_ERR_RANGE` (EC-024).
 *   3. `path` longer than `QA_IO_CONFIG_PATH_MAX` bytes -> `QA_ERR_RANGE`
 *      (FR-015, EC-028).
 *   4. Any field longer than `QA_IO_CONFIG_FIELD_MAX` bytes -> `QA_ERR_RANGE`
 *      (FR-015, EC-028).
 * It then writes the header and one RFC 4180 row to `path + ".tmp"` (opened
 * exclusively), checks every stream call, flushes and closes, and renames the
 * temporary file over `path`. Any file failure (missing or non-writable
 * directory, write, close or rename error) -> `QA_ERR_IO` (FR-015, EC-021,
 * EC-022), after closing and removing the temporary file.
 *
 * @param[in] path   Non-NULL caller-owned NUL-terminated target path, at most
 *                   `QA_IO_CONFIG_PATH_MAX` bytes.
 * @param[in] record Non-NULL caller-owned record, ten non-NULL fields, each at
 *                   most `QA_IO_CONFIG_FIELD_MAX` bytes.
 *
 * @return `QA_OK` (target replaced entirely with header plus one row);
 *         `QA_ERR_RANGE` (NULL argument, NULL field, over-bound path or
 *         field; nothing touched); `QA_ERR_IO` (any file failure; an existing
 *         target stays bit-identical and an absent target stays absent,
 *         EC-023). There is no `QA_ERR_NOMEM` path because nothing is
 *         allocated.
 *
 * @owner No heap allocation. The only resource, the temporary `FILE *`, is
 *        opened and closed inside the call on every path; `path` and `record`
 *        stay owned by the caller.
 * @assumes The output is deterministic for equal inputs (TST-3). The temporary
 *          name fits a stack buffer of `QA_IO_CONFIG_PATH_MAX + 4 + 1` bytes
 *          because `path` was bounded first. `rename` is atomic on both
 *          supported platforms (LIM-1).
 */
QaStatus qaIoWriteConfig(const char *path, const QaConfigRecord *record);

#endif /* QA_IO_CONFIG_H */
