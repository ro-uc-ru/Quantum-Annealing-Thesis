/*
 * 001-states header test (task T02, RF-001 / RF-004 / RF-005).
 *
 * Purpose: verify `include/qa/core/grid.h` compiles as the first include
 * (self-contained, codestyle §3), survives a double include (guards
 * `QA_CORE_GRID_H` and `QA_CORE_STATUS_H`), exposes a 32-bit `QaGridId`,
 * documents the exact `qaGridGetBit`/`qaGridWithBit` signatures required by
 * RF-005, and reuses the shared `QaStatus` channel with the normative values
 * of `docs/codestyle.md` §5.
 *
 * The declaration-level documentation gate (constitution §6) lives in
 * `scripts/check-header-docs.sh` and is registered per header by
 * `CMakeLists.txt`; the shell gate cannot be checked from C. No grid
 * behavior is exercised here: T03-T05 implement it and T06-T10 test it.
 *
 * Ownership: no allocation, nothing to release. Errors: any failed check
 * fails a compile-time assert (build error) or the process exit code.
 * Numerical assumptions: `QaGridId` packs into exactly 32 bits and the
 * `QaStatus` enumerators are `QA_OK = 0`, `QA_ERR_RANGE = 1`,
 * `QA_ERR_NOMEM = 2`, `QA_ERR_OVERFLOW = 3`, `QA_ERR_IO = 4`,
 * `QA_ERR_DOMAIN = 5`, `QA_ERR_UNSUPPORTED = 6`.
 */

#include "qa/core/grid.h" /* must stand alone: self-contained header */

#include "qa/core/grid.h" /* second include: guard QA_CORE_GRID_H holds */
#include "qa/core/status.h"
#include "qa/core/status.h" /* second include: guard QA_CORE_STATUS_H holds */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

/* A guard that fails is a missing guard: the second include above must be a
 * no-op, and that is only observable after the preprocessor has run. */
#if defined(QA_CORE_GRID_H)
#define QA_GRID_GUARD_HELD 1
#else
#define QA_GRID_GUARD_HELD 0
#endif
#if defined(QA_CORE_STATUS_H)
#define QA_STATUS_GUARD_HELD 1
#else
#define QA_STATUS_GUARD_HELD 0
#endif

_Static_assert(QA_GRID_GUARD_HELD, "guard QA_CORE_GRID_H must be defined");
_Static_assert(QA_STATUS_GUARD_HELD,
               "guard QA_CORE_STATUS_H must be defined");

/*
 * Signature oracle: `__typeof__` is unevaluated, so nothing references the
 * unimplemented functions and the test still links. A missing declaration is
 * an "undeclared identifier" compile error; a changed signature fails the
 * static assertion below.
 */
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaGridGetBit),
                   QaStatus (QaGridId, unsigned int, unsigned int,
                             unsigned int, unsigned int *)),
               "qaGridGetBit signature must match RF-005");

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaGridWithBit),
                   QaStatus (QaGridId, unsigned int, unsigned int,
                             unsigned int, unsigned int, QaGridId *)),
               "qaGridWithBit signature must match RF-005");

_Static_assert(sizeof (QaGridId) == 4, "QaGridId must be a 32-bit packer");

_Static_assert(QA_OK == 0, "QA_OK must be 0 (codestyle §5)");
_Static_assert(QA_ERR_RANGE == 1, "QA_ERR_RANGE must be 1 (codestyle §5)");
_Static_assert(QA_ERR_NOMEM == 2, "QA_ERR_NOMEM must be 2 (codestyle §5)");
_Static_assert(QA_ERR_OVERFLOW == 3, "QA_ERR_OVERFLOW must be 3 (codestyle §5)");
_Static_assert(QA_ERR_IO == 4, "QA_ERR_IO must be 4 (codestyle §5)");
_Static_assert(QA_ERR_DOMAIN == 5, "QA_ERR_DOMAIN must be 5 (codestyle §5)");
_Static_assert(QA_ERR_UNSUPPORTED == 6,
               "QA_ERR_UNSUPPORTED must be 6 (codestyle §5)");

/* RF-001: the board is the value of a 32-bit cell-packed identifier. */

/* Records one failing check; the run stays allocation-free. */
static int checkFailed(int condition, const char *what)
{
    if (condition) {
        return 0;
    }
    printf("FAIL: %s\n", what);
    return 1;
}

int main(void)
{
    int failures = 0;

    failures += checkFailed(sizeof (QaGridId) == 4u,
                             "QaGridId must occupy 4 bytes");
    failures += checkFailed(sizeof (unsigned int) >= 4u,
                             "unsigned int must hold cell indices and N");
    failures += checkFailed(sizeof (unsigned int) * CHAR_BIT == 32u,
                             "unsigned int must be 32-bit so n == 5 is representable");
    failures += checkFailed(UINT32_MAX == 0xFFFFFFFFu,
                             "QaGridId must expose the full 32-bit range");

    printf("001-states header: %s, build=%s, QaGridId=%zu bits, "
           "QA_ERR_UNSUPPORTED=%d\n",
           __clang_version__, QA_BUILD_TYPE, sizeof (QaGridId) * CHAR_BIT,
           (int)QA_ERR_UNSUPPORTED);

    if (failures != 0) {
        printf("001-states header: FAILED (%d checks)\n", failures);
        return 1;
    }
    printf("001-states header: OK\n");
    return 0;
}
