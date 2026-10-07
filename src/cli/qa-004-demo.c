/*
 * qa-004-demo (spec 004-driver, task T-013, FR-013, FR-014, FR-015).
 *
 * Purpose: build the N = 4 initial state, apply `H_driver` matrix-free, check
 * that every amplitude of the result equals `-16 * psi0[k]` within 1e-12, and
 * write the configuration record of FR-014 with `qaIoWriteConfig`. Usage:
 * `qa-004-demo [csv-path]`; the default path is `results/004-config.csv`
 * relative to the working directory (the representative run is
 * `./build/qa-004-demo` from the project root). The exit code is 0 only if
 * every step returned `QA_OK` and the check held, 1 otherwise.
 *
 * Ownership: `psi0` and `hpsi0` are heap buffers owned by `main`; every path
 * releases both through the single `cleanup` label. The record strings are
 * string literals and configure-time definitions, never freed. Errors: any
 * allocation failure, non-`QA_OK` status or failed check prints one line to
 * stderr and exits 1; stdout stays silent. A failed check writes no record,
 * so a CSV on disk always belongs to a run that passed.
 * Numerical assumptions: `H_driver |psi0> = -16 |psi0>` for `N = 4`
 * (FR-011), compared per amplitude within `QA_DEMO_TOL`; `dim = 2^16` keeps
 * every size computation exact.
 * Build metadata: `QA_GIT_SHA`, `QA_CLANG_VERSION` and `QA_CMAKE_FLAGS` are
 * compile definitions set by CMake at configure time; a missing one falls
 * back to the non-empty text `unavailable` (EC-020).
 * Fault seam (MEM-4 tests): the shipped `qa-004-demo` defines no
 * `QA_DEMO_FAULT` (value 0, every fault branch compiled out). The test-only
 * executables `qa-004-demo-fault-1..5` rebuild this file with
 * `QA_DEMO_FAULT` = 1 (the `psi0` allocation returns NULL), 5 (the `hpsi0` allocation returns
 * NULL), 2 (initial state gets a
 * wrong `dim`), 3 (apply gets a wrong `dim`) or 4 (one amplitude of the
 * result is perturbed by 1.0 so the eigen-check fails); each must exit 1,
 * release both buffers and write no file.
 */

#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "qa/core/status.h"
#include "qa/hamiltonian/driver.h"
#include "qa/io/config.h"

#define QA_DEMO_N 4u
#define QA_DEMO_DIM 65536u
#define QA_DEMO_EIGENVALUE (-16.0)
#define QA_DEMO_TOL 1e-12
#define QA_DEMO_DEFAULT_PATH "results/004-config.csv"

/* Test seam selector: 0 in the shipped demo; 1..5 select one injected fault
 * (see the file header). `QA_DEMO_LEN(f)` is the length handed to the library
 * when fault `f` wants it to see a wrong, too-short `dim`. */
#ifndef QA_DEMO_FAULT
#define QA_DEMO_FAULT 0
#endif
#define QA_DEMO_LEN(f) \
    (QA_DEMO_FAULT == (f) ? QA_DEMO_DIM - 1u : QA_DEMO_DIM)

#ifndef QA_GIT_SHA
#define QA_GIT_SHA "unavailable"
#endif
#ifndef QA_CLANG_VERSION
#define QA_CLANG_VERSION "unavailable"
#endif
#ifndef QA_CMAKE_FLAGS
#define QA_CMAKE_FLAGS "unavailable"
#endif

/**
 * @brief Run the N = 4 demo and report through the exit code.
 *
 * @param[in] argc Argument count; at most one optional argument is accepted.
 * @param[in] argv `argv[1]`, when present, is the CSV output path.
 *
 * @return 0 when every step is `QA_OK` and the eigen-check holds; 1 on a
 *         usage error, allocation failure, library error or failed check.
 *
 * @owner Owns two heap buffers (`psi0`, `hpsi0`), freed at `cleanup` on every
 *        path; `argv` stays owned by the C runtime.
 * @assumes `dim = 2^16` fits `size_t`, and the writer bounds the path length
 *          itself (a longer path returns `QA_ERR_RANGE` and exits 1).
 */
int main(int argc, char **argv)
{
    int exitCode = 1;
    complex double *psi0 = NULL;
    complex double *hpsi0 = NULL;
    const char *path = QA_DEMO_DEFAULT_PATH;
    QaConfigRecord record;
    QaStatus status;

    if (argc > 2) {
        fprintf(stderr, "usage: qa-004-demo [csv-path]\n");
        goto cleanup;
    }
    if (argc == 2) {
        path = argv[1];
    }

    /* Fault 1 models a failed allocation of `psi0`; `hpsi0` is still
     * allocated so `cleanup` is exercised with one live buffer. */
    psi0 = QA_DEMO_FAULT == 1 ? NULL : calloc(QA_DEMO_DIM, sizeof *psi0);
    /* Fault 5 mirrors fault 1: `hpsi0` fails while `psi0` is live. */
    hpsi0 = QA_DEMO_FAULT == 5 ? NULL : calloc(QA_DEMO_DIM, sizeof *hpsi0);
    if (psi0 == NULL || hpsi0 == NULL) {
        fprintf(stderr, "qa-004-demo: out of memory\n");
        goto cleanup;
    }

    status = qaHamiltonianInitialState(QA_DEMO_N, psi0, QA_DEMO_LEN(2));
    if (status != QA_OK) {
        fprintf(stderr, "qa-004-demo: initial state failed (%d)\n", (int)status);
        goto cleanup;
    }
    status = qaHamiltonianApplyDriver(QA_DEMO_N, psi0, hpsi0, QA_DEMO_LEN(3));
    if (status != QA_OK) {
        fprintf(stderr, "qa-004-demo: apply failed (%d)\n", (int)status);
        goto cleanup;
    }

#if QA_DEMO_FAULT == 4
    /* Fault 4: index 0 < QA_DEMO_DIM; breaks H psi0 = -16 psi0 by 1.0. */
    hpsi0[0] += 1.0;
#endif

    /* Bound: k < QA_DEMO_DIM, the length of both buffers. */
    for (size_t k = 0; k < QA_DEMO_DIM; k++) {
        if (cabs(hpsi0[k] - QA_DEMO_EIGENVALUE * psi0[k]) > QA_DEMO_TOL) {
            fprintf(stderr, "qa-004-demo: H psi0 != -16 psi0 at k=%zu\n", k);
            goto cleanup;
        }
    }

    record.specVersion = "v1";
    record.n = "4";
    record.dim = "65536";
    record.vectors = "4:psi0";
    record.gitSha = QA_GIT_SHA;
    record.clangVersion = QA_CLANG_VERSION;
    record.cmakeFlags = QA_CMAKE_FLAGS;
    record.seed = "N/A";
    record.schedule = "N/A";
    record.dtSteps = "N/A";

    status = qaIoWriteConfig(path, &record);
    if (status != QA_OK) {
        fprintf(stderr, "qa-004-demo: cannot write %s (%d)\n", path,
                (int)status);
        goto cleanup;
    }

    exitCode = 0;

cleanup:
    free(hpsi0);
    free(psi0);
    return exitCode;
}
