#ifndef QA_HAMILTONIAN_DRIVER_H
#define QA_HAMILTONIAN_DRIVER_H

/*
 * Transverse (driver) Hamiltonian `H_driver` applied matrix-free, and the
 * annealing initial state (spec 004-driver, T-001).
 *
 * Scope: FR-005 (exactly these two signatures, `phi` never modified, writes
 * only within `outPsi[0..dim-1]`, no heap allocation, no state across calls),
 * FR-002 (`N == 5` -> `QA_ERR_UNSUPPORTED`), FR-003 (`N < 2` or `N > 5` ->
 * `QA_ERR_RANGE`) and FR-006 (`dim`, NULL and overlap preconditions), plus the
 * apply-only domain gates FR-007 and FR-008 and the edge cases EC-001..EC-003
 * that exercise them. Declarations only; behavior lives in
 * `src/hamiltonian/driver.c`. No dense or sparse matrix is ever built.
 *
 * Definitions fixed by spec §1 and used by every function below:
 *   N          board side; accepted range `[2, 4]`, `N == 5` reserved
 *   numCells   `N * N`
 *   dim        `2^numCells`, at most 65536 for the accepted `N`
 *   k          basis index in `[0, dim)`; cell `c = i * N + j` is bit
 *              `numCells - 1 - c` of `k` (MSB-first, row-major)
 *   m_c        `1 << (numCells - 1 - c)`, single-bit mask of cell `c`
 *   H_driver   `sum_{c=0}^{numCells-1} sigma^x_c`, sign `+`, no scale factor
 *   NORM_TOL   1e-12, absolute tolerance of the input norm gate
 *
 * Formulas:
 *   apply          outPsi[k] = sum_c phi[k XOR m_c], ascending `c`
 *   initial state  psi0[k]   = (-1)^popcount(k) * s, imaginary part `+0.0`,
 *                  `s = 1.0 / sqrt((double) dim)`; ground state of `H_driver`
 *                  with energy `-numCells`
 *   norm           ||phi|| = sqrt(sum_k (re^2 + im^2)), sequential in
 *                  ascending `k`, no OpenMP
 *
 * Numerical bounds: `||H_driver|| = numCells`, so every output amplitude has
 * modulus at most `numCells * max|phi[k]|`; accepted inputs satisfy
 * `| ||phi|| - 1 | <= NORM_TOL`. `QA_ERR_OVERFLOW` is unreachable because the
 * `N`-gate runs before any width arithmetic.
 */

#include <complex.h>
#include <stddef.h>

#include "qa/core/status.h"

/**
 * @brief Apply `H_driver` to a state, matrix-free: `outPsi = H_driver |phi>`
 * (FR-002, FR-003, FR-005..FR-008).
 *
 * Pure function: no state, no allocation, no IO, no matrix. Fixed validation
 * order, completed before the first write (spec §1, §2):
 *   1. `n == 5` -> `QA_ERR_UNSUPPORTED` (FR-002), regardless of pointers or
 *      `dim`.
 *   2. `n < 2` or `n > 5` -> `QA_ERR_RANGE` (FR-003).
 *   3. `dim != 2^(n*n)` -> `QA_ERR_RANGE` (FR-006); checked before any pointer
 *      arithmetic, so later address computations are bounded by
 *      `dim <= 65536`.
 *   4. `phi == NULL` or `outPsi == NULL` -> `QA_ERR_RANGE` (FR-006).
 *   5. The `uintptr_t` byte ranges of `phi` and `outPsi` overlap ->
 *      `QA_ERR_RANGE` (FR-006).
 *   6. Any `phi[k]` has a non-finite real or imaginary part ->
 *      `QA_ERR_DOMAIN` (FR-007); the full scan completes before step 7.
 *   7. `||phi||` is non-finite or `| ||phi|| - 1 | > NORM_TOL` ->
 *      `QA_ERR_DOMAIN` (FR-008).
 * On success it computes `outPsi[k] = sum_c phi[k XOR m_c]` for every `k`,
 * summing in ascending `c`, and writes only `outPsi[0..dim-1]`.
 *
 * @param[in]  n      Board side, `2 <= n <= 4` (`5` reserved).
 * @param[in]  phi    Non-NULL caller-owned input of `dim` amplitudes, finite
 *                    and normalized within `NORM_TOL`; never modified.
 * @param[out] outPsi Non-NULL caller-owned output of `dim` amplitudes,
 *                    disjoint from `phi`.
 * @param[in]  dim    Length of both buffers, exactly `2^(n*n)`.
 *
 * @return `QA_OK` (`outPsi[0..dim-1]` written); `QA_ERR_UNSUPPORTED` (`n == 5`);
 *         `QA_ERR_RANGE` (`n` outside `[2, 5]`, wrong `dim`, NULL pointer or
 *         overlapping buffers); `QA_ERR_DOMAIN` (non-finite amplitude or norm
 *         gate failure). On failure `outPsi` and `phi` are left untouched.
 *
 * @owner No allocation. `phi` and `outPsi` stay owned by the caller on every
 *        path; the function keeps no pointer after returning.
 * @assumes Deterministic: equal inputs give bit-identical outputs on the same
 *          build and platform (FR-009). The norm is accumulated sequentially in
 *          `double`, ascending `k`. `H_driver` is Hermitian with spectrum
 *          `numCells - 2p`, so the output norm is at most `numCells * ||phi||`.
 */
QaStatus qaHamiltonianApplyDriver(unsigned int n, const complex double *phi,
                                  complex double *outPsi, size_t dim);

/**
 * @brief Write the annealing initial state `|psi0> = |->^numCells`, the ground
 * state of `H_driver` (FR-002, FR-003, FR-005, FR-006, FR-010).
 *
 * Pure function: no state, no allocation, no IO. Fixed validation order,
 * completed before the first write:
 *   1. `n == 5` -> `QA_ERR_UNSUPPORTED` (FR-002), regardless of pointer or
 *      `dim`.
 *   2. `n < 2` or `n > 5` -> `QA_ERR_RANGE` (FR-003).
 *   3. `dim != 2^(n*n)` -> `QA_ERR_RANGE` (FR-006).
 *   4. `outPsi == NULL` -> `QA_ERR_RANGE` (FR-006).
 * On success it writes `outPsi[k] = (-1)^popcount(k) * s` with imaginary part
 * `+0.0` for `k = 0..dim-1`, where `s = 1.0 / sqrt((double) dim)`, and nothing
 * outside `outPsi[0..dim-1]`.
 *
 * @param[in]  n      Board side, `2 <= n <= 4` (`5` reserved).
 * @param[out] outPsi Non-NULL caller-owned output of `dim` amplitudes.
 * @param[in]  dim    Length of `outPsi`, exactly `2^(n*n)`.
 *
 * @return `QA_OK` (`outPsi[0..dim-1]` written); `QA_ERR_UNSUPPORTED` (`n == 5`);
 *         `QA_ERR_RANGE` (`n` outside `[2, 5]`, wrong `dim` or NULL `outPsi`).
 *         On failure `outPsi` is left untouched.
 *
 * @owner No allocation. `outPsi` stays owned by the caller on every path; the
 *        function keeps no pointer after returning.
 * @assumes Deterministic and bit-identical across calls (FR-009). `dim` is a
 *          power of two, so `s` is exact in `double` for even `numCells` and
 *          correctly rounded otherwise; `| ||psi0|| - 1 | <= NORM_TOL` and
 *          `H_driver |psi0> = -numCells |psi0>` within `1e-12` (FR-010,
 *          FR-011).
 */
QaStatus qaHamiltonianInitialState(unsigned int n, complex double *outPsi,
                                   size_t dim);

#endif /* QA_HAMILTONIAN_DRIVER_H */
