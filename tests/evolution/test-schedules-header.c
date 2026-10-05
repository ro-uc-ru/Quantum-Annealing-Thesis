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
 * `CMakeLists.txt`. No schedule behavior is exercised here: T-003 and T-004
 * implement it and T-005-T-009 test it.
 *
 * Ownership: no allocation, nothing to release. Errors: any failed check
 * fails a compile-time assert (build error). Numerical assumptions: none.
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

/* Signature oracles: `__typeof__` is unevaluated, so nothing references the
 * Phase 2 definitions and the test links against the placeholder library. */
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaScheduleEval),
                   QaStatus (QaScheduleKind, double, double, double *,
                             double *)),
               "qaScheduleEval signature must match the spec contract");

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaScheduleName),
                   QaStatus (QaScheduleKind, const char **)),
               "qaScheduleName signature must match the spec contract");

int main(void)
{
    printf("003-schedules header: OK (FR-002, FR-013 contract declared)\n");
    return 0;
}
