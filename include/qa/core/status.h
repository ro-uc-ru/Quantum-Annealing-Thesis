#ifndef QA_CORE_STATUS_H
#define QA_CORE_STATUS_H

/*
 * Shared status channel for every module (docs/codestyle.md §5).
 *
 * Defined once and reused by every public header; spec 001-states T02
 * ("QaStatus reuse", decision D-02) introduced it because a per-module error
 * type would have invented a second project-wide pattern. The enumerator
 * values are fixed by codestyle §5 and asserted by
 * `tests/core/test-grid-header.c`, so existing callers never observe a
 * silent renumbering.
 *
 * The file uses the same include-guard shape as every other public header
 * (codestyle §3) and declares no function, no allocation and no IO.
 */

/**
 * @brief Result of every fallible project function (codestyle §5).
 *
 * Library code returns `QaStatus` and never calls `exit()`; CLI layers are
 * the only place allowed to translate a status into a process exit code. On
 * every failure each function documents whether its out-params are left
 * untouched or poisoned. The enumerator values are normative and MUST NOT be
 * renumbered:
 *   `QA_OK` 0, `QA_ERR_RANGE` 1, `QA_ERR_NOMEM` 2, `QA_ERR_OVERFLOW` 3,
 *   `QA_ERR_IO` 4, `QA_ERR_DOMAIN` 5, `QA_ERR_UNSUPPORTED` 6.
 *
 * @owner Plain enum, no resources: nothing to allocate or release.
 * @assumes No resource, arithmetic or numerical assumption is attached to a
 *          status value; the domain of a code is documented per function.
 */
typedef enum QaStatus {
    QA_OK = 0,            /* Success. Out-params hold the results. */
    QA_ERR_RANGE = 1,     /* Bad size or index. Out-params untouched. */
    QA_ERR_NOMEM = 2,     /* Allocation failed. Nothing leaked. */
    QA_ERR_OVERFLOW = 3,  /* Integer shift or multiply would overflow. */
    QA_ERR_IO = 4,        /* File open, read, or write failed. */
    QA_ERR_DOMAIN = 5,    /* Numerical input outside the valid domain. */
    QA_ERR_UNSUPPORTED = 6 /* Valid input outside the staged scope (e.g. future sizes). */
} QaStatus;

#endif /* QA_CORE_STATUS_H */
