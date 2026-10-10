/*
 * 005-evolution header test (task T-005, FR-016, FR-021).
 *
 * Purpose: verify that the four public headers of this spec
 * (`qa/hamiltonian/target.h`, `qa/evolution/primitives.h`, `qa/evolution/evolve.h`,
 * `qa/io/run.h`) and the two private seams (`evolution/evolve-internal.h`,
 * `io/run-internal.h`) declare exactly the constants, types, struct members and
 * function signatures of the spec, and that every guard holds on a second include.
 *
 * Every signature is compared at compile time with
 * `__builtin_types_compatible_p(__typeof__(f), ...)`. `__typeof__` is unevaluated,
 * so the test references no symbol and links without any definition; the
 * behavior of the functions is covered by the later groups (T-006 onward). The
 * self-containment of each public header as the first include is checked by its
 * `standalone` CTest entry in `CMakeLists.txt`, not here: this file includes the
 * public headers in the order the spec needs.
 *
 * Ownership: no allocation, nothing to release. Errors: a failed compile-time check
 * stops the build through `_Static_assert`; at run time `main` returns 1 only if the
 * spec name string differs. Numerical assumptions: the constants are compared as
 * exact literals, no value is computed.
 * The checks use the compiler extensions `__typeof__` and
 * `__builtin_types_compatible_p` (clang, the project compiler per STK-1, and GCC),
 * as `tests/evolution/test-schedules-header.c` does.
 */

#include <complex.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "qa/hamiltonian/target.h"
#include "qa/evolution/primitives.h"
#include "qa/evolution/evolve.h"
#include "qa/io/run.h"

/* Second include of every public header: each guard must hold. */
#include "qa/hamiltonian/target.h"
#include "qa/evolution/primitives.h"
#include "qa/evolution/evolve.h"
#include "qa/io/run.h"

/* Private seams, included after their public headers (the precedent of 004). */
#include "evolve-internal.h"
#include "run-internal.h"

#if defined(QA_HAMILTONIAN_TARGET_H) && defined(QA_EVOLUTION_PRIMITIVES_H) && \
    defined(QA_EVOLUTION_EVOLVE_H) && defined(QA_IO_RUN_H) && \
    defined(QA_EVOLUTION_EVOLVE_INTERNAL_H) && defined(QA_IO_RUN_INTERNAL_H)
#define QA_005_GUARDS_HELD 1
#else
#define QA_005_GUARDS_HELD 0
#endif

/* Compares the type of member `member` of struct `Type` with `Expected`. */
#define QA_005_MEMBER_IS(Type, member, Expected) \
    __builtin_types_compatible_p(__typeof__(((Type *)0) -> member), Expected)

_Static_assert(QA_005_GUARDS_HELD,
               "guards of the 005-evolution headers must be defined");

/* Status codes: the numeric values are the CLI exit codes of FR-015. */
_Static_assert(QA_OK == 0 && QA_ERR_RANGE == 1 && QA_ERR_NOMEM == 2 &&
                   QA_ERR_OVERFLOW == 3 && QA_ERR_IO == 4 &&
                   QA_ERR_DOMAIN == 5 && QA_ERR_UNSUPPORTED == 6,
               "QaStatus must keep the values 0..6 of the spec");

/* Constants of FR-001, FR-005, FR-024, FR-015 and section 4. */
/* The two floating constants (`r`, `NORM_TOL`) are compared in `main`: clang
 * rejects floating comparisons in `_Static_assert` under -Werror. */
_Static_assert(QA_EVOLVE_STEPS_MAX == 1000000u,
               "QA_EVOLVE_STEPS_MAX must be 10^6 (FR-024)");
_Static_assert(QA_EVOLVE_SNAPSHOTS_MAX == 10000u,
               "QA_EVOLVE_SNAPSHOTS_MAX must be 10000 (EC-015)");
_Static_assert(QA_EVOLVE_SNAPSHOT_ROWS_MAX == ((size_t)1 << 24),
               "QA_EVOLVE_SNAPSHOT_ROWS_MAX must be 2^24 (EC-015)");
_Static_assert(QA_IO_RUN_PATH_MAX == 1024u,
               "QA_IO_RUN_PATH_MAX must be 1024 (FR-015)");
_Static_assert(QA_IO_RUN_FIELD_MAX == 4096u,
               "QA_IO_RUN_FIELD_MAX must be 4096 (FR-014)");
_Static_assert(QA_IO_RUN_FORMAT_VERSION == 1u,
               "format_version must be 1 (section 4)");

/* Callback types of FR-011 and FR-012. */
_Static_assert(__builtin_types_compatible_p(
                   QaEvolveObserver,
                   QaStatus (*)(void *, const QaEvolveSample *)),
               "QaEvolveObserver must match the FR-011 callback");
_Static_assert(__builtin_types_compatible_p(
                   QaEvolveSnapshotSink,
                   QaStatus (*)(void *, size_t, size_t, const double *)),
               "QaEvolveSnapshotSink must match the FR-012 callback");

/* Public function signatures: target, primitives, time grid, driver. */
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaHamiltonianTarget),
                   QaStatus (unsigned int, size_t, double *)),
               "qaHamiltonianTarget signature must match FR-001");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaEvolutionDz),
                   QaStatus (unsigned int, double, double, complex double *,
                             size_t)),
               "qaEvolutionDz signature must match FR-004");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaEvolutionRx),
                   QaStatus (unsigned int, double, double, complex double *,
                             size_t)),
               "qaEvolutionRx signature must match FR-003");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaEvolutionTimeNode),
                   QaStatus (double, size_t, size_t, double *)),
               "qaEvolutionTimeNode signature must match FR-026");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaEvolutionTimeMid),
                   QaStatus (double, size_t, size_t, double *)),
               "qaEvolutionTimeMid signature must match FR-017");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaEvolve),
                   QaStatus (const QaEvolveParams *, const complex double *,
                             complex double *, size_t,
                             const QaEvolveCallbacks *, QaEvolveSummary *)),
               "qaEvolve signature must match FR-020");

/* Public function signatures: run records (FR-013, FR-014, EC-017). */
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaRunCheckOutDir),
                   QaStatus (const char *)),
               "qaRunCheckOutDir signature must match EC-017");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaRunOpen),
                   QaStatus (const char *, size_t, QaRun **)),
               "qaRunOpen signature must match the run handle contract");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaRunAppendTrace),
                   QaStatus (QaRun *, size_t, double, double, double, double,
                             double)),
               "qaRunAppendTrace signature must match FR-013");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaRunWriteSnapshot),
                   QaStatus (QaRun *, size_t, size_t, const double *)),
               "qaRunWriteSnapshot signature must match FR-013");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaRunWriteConfig),
                   QaStatus (QaRun *, const QaRunConfig *)),
               "qaRunWriteConfig signature must match FR-014");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaRunDestroy),
                   QaStatus (QaRun *)),
               "qaRunDestroy signature must match FR-019");

/* Private seams: the ops tables and their constructors (FR-025). */
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaRunDefaultOps),
                   const QaRunOps *(void)),
               "qaRunDefaultOps signature must return the libc table");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaRunOpenWith),
                   QaStatus (const char *, size_t, const QaRunOps *, QaRun **)),
               "qaRunOpenWith signature must take the run ops table");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaEvolveDefaultOps),
                   const QaEvolveOps *(void)),
               "qaEvolveDefaultOps signature must return the driver table");
_Static_assert(__builtin_types_compatible_p(
                   __typeof__(qaEvolveWith),
                   QaStatus (const QaEvolveParams *, const complex double *,
                             complex double *, size_t,
                             const QaEvolveCallbacks *, QaEvolveSummary *,
                             const QaEvolveOps *)),
               "qaEvolveWith signature must take the driver ops table");

/* Struct members of the public types, in declaration order. */
_Static_assert(QA_005_MEMBER_IS(QaEvolveParams, n, unsigned int) &&
                   QA_005_MEMBER_IS(QaEvolveParams, family, QaScheduleKind) &&
                   QA_005_MEMBER_IS(QaEvolveParams, T, double) &&
                   QA_005_MEMBER_IS(QaEvolveParams, steps, size_t) &&
                   QA_005_MEMBER_IS(QaEvolveParams, snapshotInterval, size_t),
               "QaEvolveParams members must match FR-020 and section 4");
_Static_assert(QA_005_MEMBER_IS(QaEvolveSample, j, size_t) &&
                   QA_005_MEMBER_IS(QaEvolveSample, t, double) &&
                   QA_005_MEMBER_IS(QaEvolveSample, a, double) &&
                   QA_005_MEMBER_IS(QaEvolveSample, b, double) &&
                   QA_005_MEMBER_IS(QaEvolveSample, norm, double) &&
                   QA_005_MEMBER_IS(QaEvolveSample, energy, double),
               "QaEvolveSample members must match the trace columns (FR-011)");
_Static_assert(QA_005_MEMBER_IS(QaEvolveCallbacks, observer, QaEvolveObserver) &&
                   QA_005_MEMBER_IS(QaEvolveCallbacks, observerCtx, void *) &&
                   QA_005_MEMBER_IS(QaEvolveCallbacks, sink,
                                    QaEvolveSnapshotSink) &&
                   QA_005_MEMBER_IS(QaEvolveCallbacks, sinkCtx, void *),
               "QaEvolveCallbacks members must match FR-011 and FR-012");
_Static_assert(QA_005_MEMBER_IS(QaEvolveSummary, finalEnergy, double) &&
                   QA_005_MEMBER_IS(QaEvolveSummary, maxNormDrift, double),
               "QaEvolveSummary members must match FR-010");
_Static_assert(QA_005_MEMBER_IS(QaRunConfig, n, unsigned int) &&
                   QA_005_MEMBER_IS(QaRunConfig, family, const char *) &&
                   QA_005_MEMBER_IS(QaRunConfig, T, double) &&
                   QA_005_MEMBER_IS(QaRunConfig, steps, size_t) &&
                   QA_005_MEMBER_IS(QaRunConfig, dt, double) &&
                   QA_005_MEMBER_IS(QaRunConfig, r, double) &&
                   QA_005_MEMBER_IS(QaRunConfig, snapshotInterval, size_t) &&
                   QA_005_MEMBER_IS(QaRunConfig, gitSha, const char *) &&
                   QA_005_MEMBER_IS(QaRunConfig, clangVersion, const char *) &&
                   QA_005_MEMBER_IS(QaRunConfig, cmakeFlags, const char *),
               "QaRunConfig members must match the section 4 keys");

/* Private ops tables: each hook keeps the exact signature of its header. */
_Static_assert(QA_005_MEMBER_IS(QaRunOps, ctx, void *) &&
                   QA_005_MEMBER_IS(QaRunOps, makeDir,
                                    int (*)(void *, const char *)) &&
                   QA_005_MEMBER_IS(QaRunOps, open,
                                    FILE *(*)(void *, const char *,
                                              const char *)) &&
                   QA_005_MEMBER_IS(QaRunOps, write,
                                    size_t (*)(void *, const void *, size_t,
                                               size_t, FILE *)) &&
                   QA_005_MEMBER_IS(QaRunOps, close,
                                    int (*)(void *, FILE *)) &&
                   QA_005_MEMBER_IS(QaRunOps, rename,
                                    int (*)(void *, const char *,
                                            const char *)) &&
                   QA_005_MEMBER_IS(QaRunOps, remove,
                                    int (*)(void *, const char *)),
               "QaRunOps entries must match the io seam of FR-025");
_Static_assert(QA_005_MEMBER_IS(QaEvolveOps, ctx, void *) &&
                   QA_005_MEMBER_IS(QaEvolveOps, alloc,
                                    void *(*)(void *, size_t, size_t)) &&
                   QA_005_MEMBER_IS(QaEvolveOps, release,
                                    void (*)(void *, void *)) &&
                   QA_005_MEMBER_IS(QaEvolveOps, schedule,
                                    QaStatus (*)(void *, QaScheduleKind, double,
                                                 double, double *, double *)) &&
                   QA_005_MEMBER_IS(QaEvolveOps, perturb,
                                    QaStatus (*)(void *, size_t,
                                                 complex double *, size_t)),
               "QaEvolveOps hooks must match the driver seam of FR-025");

/**
 * @brief Entry point: the compile-time checks above do most of the work; `main`
 * checks the two floating constants and the spec name written into `config.json`.
 *
 * @return 0 when `r == 0.5`, `NORM_TOL == 1e-12` and the spec name is
 *         `"005-evolution"` (section 4); 1 otherwise.
 *
 * @owner No allocation, nothing to release.
 * @assumes The translation unit compiled, which already proves the signatures;
 *          the two literals are exact or identical to their macro text, so the
 *          equality is exact.
 */
int main(void)
{
    if (QA_HAMILTONIAN_TARGET_R != 0.5) {
        fprintf(stderr, "005-evolution header: r must be 0.5\n");
        return 1;
    }
    if (QA_EVOLVE_NORM_TOL != 1e-12) {
        fprintf(stderr, "005-evolution header: NORM_TOL must be 1e-12\n");
        return 1;
    }
    if (strcmp(QA_IO_RUN_SPEC_NAME, "005-evolution") != 0) {
        fprintf(stderr, "005-evolution header: wrong spec name\n");
        return 1;
    }
    printf("005-evolution header: OK (guards, constants, signatures declared)\n");
    return 0;
}
