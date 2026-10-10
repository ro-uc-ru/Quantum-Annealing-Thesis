#ifndef QA_IO_RUN_INTERNAL_H
#define QA_IO_RUN_INTERNAL_H

/*
 * Private seam of the run writers for failure injection (spec 005-evolution,
 * T-004).
 *
 * Scope: FR-019 (every failure path releases what it acquired) and FR-025 (write,
 * create and close failures are provoked through this table, not through the
 * disk), plus EC-009 (run dir unwritable or a write fails) and EC-017 (the lazy
 * creation of the run layout). Not installed and not a public header: only
 * `src/io/run.c` and the io and cli tests include it, always after
 * `qa/io/run.h`. Declarations only; the libc-backed table and the writers live in
 * `src/io/run.c`.
 *
 * Definitions:
 *   ops table   the file and memory operations the run writers perform, each taking the
 *               table's `ctx` as its first argument, so a test can count calls
 *               or fail the k-th call without a global
 *   makeDir     creates one directory. The library calls it only for a path that
 *               is absent or is an existing empty directory (EC-017), so an
 *               existing directory is success; any other failure is `-1`
 *   injection   a test copies `*qaRunDefaultOps()`, replaces one entry and passes
 *               the copy to `qaRunOpenWith`
 *
 * Numerical bounds: none; the argument bounds of `qa/io/run.h` apply.
 */

#include <stddef.h>
#include <stdio.h>

/* qa/io/run.h must be included first (QaStatus, QaRun, QaRunConfig). */

/**
 * @brief File operations used by the run writers, replaceable by tests (FR-025).
 *
 * Each entry has the libc contract of the function it mirrors: `makeDir` is
 * `mkdir` (0 on success, with an existing directory counted as success), `open`
 * is `fopen`, `write` is `fwrite`, `close` is `fclose`, `rename` is `rename` and
 * `remove` is `remove`; `alloc` is `malloc` (size in bytes) and `release` is
 * `free`, used only for the run handle. The writers treat a NULL `open`, an `alloc`
 * returning NULL (`QA_ERR_NOMEM`), a `write` returning less
 * than `count`, and a non-zero `makeDir`, `close`, `rename` or `remove` as failure.
 *
 * @owner The table is owned by the caller and must outlive every run handle built
 *        from it (`qaRunDestroy` releases the handle through `release`); the writers never copy or free it. Every function entry must be
 *        non-NULL. `ctx` is opaque to the writers and passed back unchanged.
 * @assumes Entries behave like their libc counterparts unless a test injects a
 *          failure. A `FILE *` returned by `open` is released by `close` exactly
 *          once, on every path, including the failure paths.
 */
typedef struct QaRunOps {
    void *ctx;  /* passed as the first argument of every entry */
    int (*makeDir)(void *ctx, const char *path);
    FILE *(*open)(void *ctx, const char *path, const char *mode);
    size_t (*write)(void *ctx, const void *buf, size_t size, size_t count,
                    FILE *stream);
    int (*close)(void *ctx, FILE *stream);
    int (*rename)(void *ctx, const char *from, const char *to);
    int (*remove)(void *ctx, const char *path);
    void *(*alloc)(void *ctx, size_t size);   /* run handle only */
    void (*release)(void *ctx, void *ptr);    /* frees a block of `alloc` */
} QaRunOps;

/**
 * @brief Return the libc-backed operations table of the run writers (FR-025).
 *
 * The table is a static, immutable object of the library, with `ctx` NULL. Tests
 * copy it by value and change one entry; the public writers use it through
 * `qaRunOpen`. `makeDir` is the libc `mkdir` with the existing-directory rule
 * above.
 *
 * @return Non-NULL pointer to the static table; never NULL.
 *
 * @owner The table is owned by the library and never freed by the caller; the
 *        pointer stays valid for the process lifetime and must not be written.
 * @assumes The table is read-only and deterministic; it holds no state.
 */
const QaRunOps *qaRunDefaultOps(void);

/**
 * @brief Create the handle of one run with an explicit operations table
 * (FR-025, FR-019).
 *
 * Same contract, validation order and return codes as `qaRunOpen` (`qa/io/run.h`),
 * with one extra step first:
 *   0. `ops == NULL`, or any function entry of `ops` is NULL -> `QA_ERR_RANGE`.
 * `qaRunOpen` is this function called with `qaRunDefaultOps()`. The handle keeps
 * the pointer `ops`, so the table must outlive the handle.
 *
 * @param[in]  dir    Same as `qaRunOpen`.
 * @param[in]  steps  Same as `qaRunOpen`.
 * @param[in]  ops    Non-NULL caller-owned table with eight non-NULL entries.
 * @param[out] outRun Same as `qaRunOpen`.
 *
 * @return Same codes as `qaRunOpen`, plus `QA_ERR_RANGE` for an invalid `ops`.
 *         `*outRun` is NULL on every failure.
 *
 * @owner Same as `qaRunOpen`: on `QA_OK` the caller owns `*outRun` and must pass
 *        it to `qaRunDestroy`; `dir`, `ops` and `outRun` stay owned by the caller.
 * @assumes Same as `qaRunOpen`, and `ops` stays valid until `qaRunDestroy`.
 */
QaStatus qaRunOpenWith(const char *dir, size_t steps, const QaRunOps *ops,
                       QaRun **outRun);

#endif /* QA_IO_RUN_INTERNAL_H */
