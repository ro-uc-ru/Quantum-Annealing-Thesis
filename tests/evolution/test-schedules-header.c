/*
 * 003-schedules header test (task T-002, FR-002, FR-013).
 *
 * Purpose: verify `include/qa/evolution/schedules.h` compiles as the first
 * include (self-contained, codestyle §3), survives a double include (guard
 * `QA_EVOLUTION_SCHEDULES_H`), declares exactly four families, and declares
 * the exact `qaScheduleEval` and `qaScheduleName` signatures.
 *
 * The declaration-level documentation gate (constitution §6) lives in
 * `scripts/check-header-docs.sh` and is registered per header by
 * `CMakeLists.txt`. No schedule behavior is exercised here: it is covered by
 * `tests/evolution/test-schedules.c`.
 *
 * Ownership: no allocation, nothing to release. Errors: any failed check
 * fails a compile-time assert (build error); at run time `main` only prints
 * and returns 0. Numerical assumptions: no floating-point value is computed.
 * The signature checks use the compiler extensions `__typeof__` and
 * `__builtin_types_compatible_p` (clang, the project compiler per STK-1, and
 * GCC), so the file is not portable to a compiler without them.
 */

#include "qa/evolution/schedules.h" /* must stand alone: self-contained header */

#include "qa/evolution/schedules.h" /* second include: guard holds */
#include "qa/core/status.h"

#include <stdio.h>

#if defined(QA_EVOLUTION_SCHEDULES_H)
#define QA_SCHEDULES_GUARD_HELD 1
#else
#define QA_SCHEDULES_GUARD_HELD 0
#endif

_Static_assert(QA_SCHEDULES_GUARD_HELD,
               "guard QA_EVOLUTION_SCHEDULES_H must be defined");

_Static_assert(QA_SCHEDULE_LINEAR == 0 && QA_SCHEDULE_TRIGONOMETRIC == 1 &&
                   QA_SCHEDULE_QUADRATIC == 2 && QA_SCHEDULE_EXPONENTIAL == 3,
               "QaScheduleKind must hold exactly the four families, 0..3");

/* Signature oracles: `__typeof__` is unevaluated, so this file only needs the
 * declarations; the test still links `qa_schedules` through CMake. */
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaScheduleEval),
                   QaStatus (QaScheduleKind, double, double, double *,
                             double *)),
               "qaScheduleEval signature must match the spec contract");

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaScheduleName),
                   QaStatus (QaScheduleKind, const char **)),
               "qaScheduleName signature must match the spec contract");

/**
 * @brief Entry point: all checks are compile-time, so it only reports success.
 *
 * @return 0 always; a failed check never reaches run time because the
 *         `_Static_assert`s above stop the build.
 *
 * @owner No allocation, nothing to release.
 * @assumes The translation unit compiled, which is the whole test.
 */
int main(void)
{
    printf("003-schedules header: OK (FR-002, FR-013 contract declared)\n");
    return 0;
}
