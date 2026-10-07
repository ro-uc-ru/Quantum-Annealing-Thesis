/*
 * 004-driver header test (task T-003, FR-005, FR-014).
 *
 * Purpose: verify that `include/qa/hamiltonian/driver.h` and
 * `include/qa/io/config.h` compile as the first include (self-contained,
 * codestyle §3), survive a double include (guards `QA_HAMILTONIAN_DRIVER_H`
 * and `QA_IO_CONFIG_H`), and declare the exact signatures of FR-005 and
 * FR-014: `qaHamiltonianApplyDriver`, `qaHamiltonianInitialState`,
 * `qaIoWriteConfig`, the ten-field `QaConfigRecord`, the bound constants 1024
 * and 4096, and the private `QaIoOps` table with `qaIoWriteConfigWith`.
 *
 * The declaration-level documentation gate (constitution ARC-6) lives in
 * `scripts/check-header-docs.sh` and is registered per header by
 * `CMakeLists.txt`. No behavior is exercised here: the functions are only
 * named inside unevaluated `__typeof__`, so the test links against the
 * placeholder libraries.
 *
 * Ownership: no allocation, nothing to release. Errors: any failed check
 * fails a compile-time assert (build error) or the process exit code.
 * Numerical assumptions: none.
 */

#include "qa/hamiltonian/driver.h" /* must stand alone: self-contained header */

#include "qa/hamiltonian/driver.h" /* second include: guard holds */
#include "qa/io/config.h"          /* must stand alone as well */
#include "qa/io/config.h"          /* second include: guard holds */

#include "config-internal.h" /* private seam, only valid after config.h */

#include <complex.h>
#include <stddef.h>
#include <stdio.h>

/* A guard that fails is a missing guard: the second includes above must be
 * no-ops, and that is only observable after the preprocessor has run. */
#if defined(QA_HAMILTONIAN_DRIVER_H)
#define QA_DRIVER_GUARD_HELD 1
#else
#define QA_DRIVER_GUARD_HELD 0
#endif

#if defined(QA_IO_CONFIG_H)
#define QA_CONFIG_GUARD_HELD 1
#else
#define QA_CONFIG_GUARD_HELD 0
#endif

_Static_assert(QA_DRIVER_GUARD_HELD,
               "guard QA_HAMILTONIAN_DRIVER_H must be defined");
_Static_assert(QA_CONFIG_GUARD_HELD, "guard QA_IO_CONFIG_H must be defined");

/* Signature oracles: `__typeof__` is unevaluated, so nothing references the
 * later definitions. A missing declaration is a compile error and a changed
 * signature fails the static assertion. */
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaHamiltonianApplyDriver),
                   QaStatus (unsigned int, const complex double *,
                             complex double *, size_t)),
               "qaHamiltonianApplyDriver signature must match FR-005");

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaHamiltonianInitialState),
                   QaStatus (unsigned int, complex double *, size_t)),
               "qaHamiltonianInitialState signature must match FR-005");

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaIoWriteConfig),
                   QaStatus (const char *, const QaConfigRecord *)),
               "qaIoWriteConfig signature must match spec §1");

_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaIoWriteConfigWith),
                   QaStatus (const char *, const QaConfigRecord *,
                             const QaIoOps *)),
               "qaIoWriteConfigWith signature must match the plan");

/* Bound constants fixed by spec §1 (FR-015). */
_Static_assert(QA_IO_CONFIG_PATH_MAX == 1024u, "path bound is 1024 bytes");
_Static_assert(QA_IO_CONFIG_FIELD_MAX == 4096u, "field bound is 4096 bytes");

/* `QaConfigRecord` holds exactly ten `const char *` fields (FR-014): ten
 * pointers and no padding or extra member. */
_Static_assert(sizeof(QaConfigRecord) == 10u * sizeof(const char *),
               "QaConfigRecord must hold exactly ten const char * fields");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(((QaConfigRecord *) 0) -> dtSteps),
                   const char *),
               "QaConfigRecord fields must be const char *");

/* The operations table holds exactly the five entries of the plan. */
_Static_assert(sizeof(QaIoOps) == 5u * sizeof(void (*)(void)),
               "QaIoOps must hold exactly five function pointers");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(((QaIoOps *) 0) -> open),
                   FILE *(*)(const char *, const char *)),
               "QaIoOps.open must mirror fopen");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(((QaIoOps *) 0) -> write),
                   size_t (*)(const void *, size_t, size_t, FILE *)),
               "QaIoOps.write must mirror fwrite");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(((QaIoOps *) 0) -> close),
                   int (*)(FILE *)),
               "QaIoOps.close must mirror fclose");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(((QaIoOps *) 0) -> rename),
                   int (*)(const char *, const char *)),
               "QaIoOps.rename must mirror rename");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(((QaIoOps *) 0) -> remove),
                   int (*)(const char *)),
               "QaIoOps.remove must mirror remove");

/**
 * @brief Report that the compile-time contract assertions above held.
 *
 * @return 0; a violated assertion already stopped the compilation.
 *
 * @owner No resource is kept.
 * @assumes Nothing at run time; every check is a `_Static_assert`.
 */
int main(void)
{
    printf("004-driver header: OK (FR-005, FR-014 contracts declared)\n");
    return 0;
}
