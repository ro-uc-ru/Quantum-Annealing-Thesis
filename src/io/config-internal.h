#ifndef QA_IO_CONFIG_INTERNAL_H
#define QA_IO_CONFIG_INTERNAL_H

/*
 * Private seam of the CSV writer for failure injection (spec 004-driver,
 * T-002).
 *
 * Scope: FR-015 and EC-021 to EC-023. Not installed and not a public header:
 * only `src/io/config.c` and the writer tests include it, always after
 * `qa/io/config.h`. Declarations only; the libc-backed table and
 * `qaIoWriteConfigWith` live in `src/io/config.c`. The reason for this seam is
 * that a write or close failure after the file is open cannot be provoked
 * portably through the operating system on both platforms of LIM-1 (plan,
 * "Failure injection").
 *
 * Definitions:
 *   ops table  the five file operations the writer performs, in call order:
 *              open, write, close, rename, remove
 *   injection  a test passes a table whose chosen entry fails
 *
 * Numerical bounds: none; the argument bounds of `qa/io/config.h` apply.
 */

#include <stdio.h>

/* qa/io/config.h must be included first (QaStatus, QaConfigRecord). */

/**
 * @brief File operations used by the writer, replaceable by tests (FR-015).
 *
 * Each entry has the libc contract of the function it mirrors: `open` is
 * `fopen`, `write` is `fwrite`, `close` is `fclose`, `rename` is `rename` and
 * `remove` is `remove`. The writer treats `open` returning NULL, `write`
 * returning less than `count`, and any non-zero `close`, `rename` or `remove`
 * result as failure.
 *
 * @owner The table is owned by the caller and must outlive the call; the
 *        writer never copies or frees it. Every entry must be non-NULL.
 * @assumes Entries behave like their libc counterparts unless a test injects
 *          a failure; a `FILE *` returned by `open` is released by `close`
 *          exactly once, on every path.
 */
typedef struct QaIoOps {
    FILE *(*open)(const char *path, const char *mode);
    size_t (*write)(const void *buf, size_t size, size_t count, FILE *stream);
    int (*close)(FILE *stream);
    int (*rename)(const char *from, const char *to);
    int (*remove)(const char *path);
} QaIoOps;

/**
 * @brief Write the configuration record through an explicit operations table
 * (FR-014, FR-015).
 *
 * Same contract, validation order and return codes as `qaIoWriteConfig`
 * (`qa/io/config.h`), with one extra step first:
 *   0. `ops == NULL` or any entry of `ops` NULL -> `QA_ERR_RANGE`.
 * `qaIoWriteConfig` is this function called with the libc-backed table.
 *
 * @param[in] path   Same as `qaIoWriteConfig`.
 * @param[in] record Same as `qaIoWriteConfig`.
 * @param[in] ops    Non-NULL caller-owned table with five non-NULL entries.
 *
 * @return Same codes as `qaIoWriteConfig`, plus `QA_ERR_RANGE` for an invalid
 *         `ops`. On failure the target is left unchanged and the temporary
 *         file is removed.
 *
 * @owner No heap allocation; `path`, `record` and `ops` stay owned by the
 *        caller. The temporary `FILE *` is closed through `ops -> close` on
 *        every path.
 * @assumes Same as `qaIoWriteConfig`.
 */
QaStatus qaIoWriteConfigWith(const char *path, const QaConfigRecord *record,
                             const QaIoOps *ops);

#endif /* QA_IO_CONFIG_INTERNAL_H */
