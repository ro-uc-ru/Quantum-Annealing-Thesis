/*
 * 002-hamiltonian header test (task T-002, FR-005).
 *
 * Purpose: verify `include/qa/hamiltonian/problem.h` compiles as the first
 * include (self-contained, codestyle §3), survives a double include (guard
 * `QA_HAMILTONIAN_PROBLEM_H`), and declares the exact
 * `qaHamiltonianApplyProblem` signature required by FR-005.
 *
 * The declaration-level documentation gate (constitution §6) lives in
 * `scripts/check-header-docs.sh` and is registered per header by
 * `CMakeLists.txt`; the shell gate cannot be checked from C. No operator
 * behavior is exercised here: T-003-T-006 implement it and T-003-T-007 test
 * it.
 *
 * Ownership: no allocation, nothing to release. Errors: any failed check
 * fails a compile-time assert (build error) or the process exit code.
 * Numerical assumptions: none beyond the `complex double` state vectors of
 * spec 002-hamiltonian §1.
 */

#include "qa/hamiltonian/problem.h" /* must stand alone: self-contained header */

#include "qa/hamiltonian/problem.h" /* second include: guard holds */
#include "qa/core/status.h"
#include "qa/core/status.h" /* second include: guard QA_CORE_STATUS_H holds */

#include <complex.h>
#include <stddef.h>
#include <stdio.h>

/* A guard that fails is a missing guard: the second include above must be a
 * no-op, and that is only observable after the preprocessor has run. */
#if defined(QA_HAMILTONIAN_PROBLEM_H)
#define QA_PROBLEM_GUARD_HELD 1
#else
#define QA_PROBLEM_GUARD_HELD 0
#endif

_Static_assert(QA_PROBLEM_GUARD_HELD,
               "guard QA_HAMILTONIAN_PROBLEM_H must be defined");

/*
 * Signature oracle: `__typeof__` is unevaluated, so nothing references the
 * Phase 2 definition and the test still links against the T-002 placeholder
 * library. A missing declaration is an "undeclared identifier" compile
 * error; a changed signature fails the static assertion below.
 */
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaHamiltonianApplyProblem),
                   QaStatus (unsigned int, const complex double *,
                             complex double *, size_t)),
               "qaHamiltonianApplyProblem signature must match FR-005");

int main(void)
{
    printf("002-hamiltonian header: OK (FR-005 contract declared)\n");
    return 0;
}
