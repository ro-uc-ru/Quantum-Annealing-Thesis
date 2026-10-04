/*
 * qa-002-demo (spec 002-hamiltonian, task T-008, FR-010).
 *
 * Purpose: run one representative normalized N=4 input through the
 * matrix-free `H_problem` apply and check the expected §7 energies,
 * signalling only via the process exit code (0 success, 1 any failure).
 * The input is the superposition `(|16770> + |65535>) / sqrt(2)`: the 4x4
 * solution (`E = 0`) plus the full board (`E = 76`), so one run pins both
 * the zero-energy and a non-zero-energy oracle at `dim = 65536`.
 *
 * Ownership: `phi`, `phiBefore`, and `outPsi` are heap buffers owned by
 * `main`; every path (success or failure) releases all three via the
 * single `cleanup` label, so the `leaks` gate stays clean. Errors: any
 * allocation failure, bounds-unsafe width, non-`QA_OK` apply status, or
 * energy mismatch prints to stderr and exits 1; stdout stays silent.
 * Numerical assumptions: `E(16770) = 0` and `E(65535) = 76` per spec §7;
 * the `76 / sqrt(2)` peak agrees within `1e-12` (irrational scaling), zero
 * cells compare with `== 0.0` (`+0.0` vs `-0.0` count as equal).
 * Determinism: fixed board ids and fixed amplitudes only; no clock, no
 * RNG, no environment or filesystem access (constitution §13).
 */

#include <complex.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/hamiltonian/problem.h"

/* Representative N=4 boards (spec §7): solution and full board. */
#define QA_DEMO_N 4u
#define QA_DEMO_SOLUTION 16770u
#define QA_DEMO_FULL 65535u
#define QA_DEMO_TOL 1e-12

/**
 * @brief Run the representative N=4 apply and report via exit code.
 *
 * @return 0 when the apply yields `QA_OK` with both oracle peaks and zeros
 *         elsewhere and `phi` unchanged; 1 on any failure.
 *
 * @owner Owns three heap buffers (`phi`, `phiBefore`, `outPsi`), all freed
 *        at `cleanup` on every path.
 * @assumes Fixed N=4 keeps `numCells = 16` and `dim = 65536` exact.
 */
int main(void)
{
    int exitCode = 1;
    complex double *phi = NULL;
    complex double *phiBefore = NULL;
    complex double *outPsi = NULL;

    unsigned int numCells = QA_DEMO_N * QA_DEMO_N;
    size_t dim = 0;
    double invSqrt2 = 0.0;

    if (numCells >= sizeof (size_t) * (size_t)CHAR_BIT) {
        fprintf(stderr, "qa-002-demo: width overflow for N=%u\n", QA_DEMO_N);
        goto cleanup;
    }
    dim = (size_t)1 << numCells;
    if (dim != 65536u) {
        fprintf(stderr, "qa-002-demo: unexpected dim %zu\n", dim);
        goto cleanup;
    }
    if ((size_t)QA_DEMO_SOLUTION >= dim || (size_t)QA_DEMO_FULL >= dim) {
        fprintf(stderr, "qa-002-demo: board id out of range\n");
        goto cleanup;
    }
    if (dim > SIZE_MAX / sizeof *phi) {
        fprintf(stderr, "qa-002-demo: state too large\n");
        goto cleanup;
    }

    phi = calloc(dim, sizeof *phi);
    if (phi == NULL) {
        fprintf(stderr, "qa-002-demo: no memory for phi\n");
        goto cleanup;
    }
    phiBefore = calloc(dim, sizeof *phiBefore);
    if (phiBefore == NULL) {
        fprintf(stderr, "qa-002-demo: no memory for phi snapshot\n");
        goto cleanup;
    }
    outPsi = calloc(dim, sizeof *outPsi);
    if (outPsi == NULL) {
        fprintf(stderr, "qa-002-demo: no memory for outPsi\n");
        goto cleanup;
    }

    invSqrt2 = 1.0 / sqrt(2.0);
    phi[QA_DEMO_SOLUTION] = invSqrt2 + 0.0 * I;
    phi[QA_DEMO_FULL] = invSqrt2 + 0.0 * I;
    memcpy(phiBefore, phi, dim * sizeof *phi);
    for (size_t k = 0; k < dim; ++k) {
        outPsi[k] = 9.0 + 9.0 * I;
    }

    QaStatus status = qaHamiltonianApplyProblem(QA_DEMO_N, phi, outPsi, dim);
    if (status != QA_OK) {
        fprintf(stderr, "qa-002-demo: apply failed with status %d\n", (int)status);
        goto cleanup;
    }
    if (memcmp(phi, phiBefore, dim * sizeof *phi) != 0) {
        fprintf(stderr, "qa-002-demo: phi mutated\n");
        goto cleanup;
    }
    if (cabs(outPsi[QA_DEMO_SOLUTION] - 0.0) > QA_DEMO_TOL) {
        fprintf(stderr, "qa-002-demo: E(16770) mismatch\n");
        goto cleanup;
    }
    if (cabs(outPsi[QA_DEMO_FULL] - 76.0 * invSqrt2) > QA_DEMO_TOL) {
        fprintf(stderr, "qa-002-demo: E(65535) mismatch\n");
        goto cleanup;
    }
    for (size_t k = 0; k < dim; ++k) {
        if (k != QA_DEMO_SOLUTION && k != QA_DEMO_FULL &&
            (creal(outPsi[k]) != 0.0 || cimag(outPsi[k]) != 0.0)) {
            fprintf(stderr, "qa-002-demo: nonzero output at k=%zu\n", k);
            goto cleanup;
        }
    }

    exitCode = 0;

cleanup:
    free(outPsi);
    free(phiBefore);
    free(phi);
    return exitCode;
}
