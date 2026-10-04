#ifndef QA_HAMILTONIAN_PROBLEM_H
#define QA_HAMILTONIAN_PROBLEM_H

/*
 * Problem Hamiltonian application: diagonal N-Queens operator
 * (spec 002-hamiltonian, T-001).
 *
 * Scope: FR-005 (exact `qaHamiltonianApplyProblem` contract). Declarations
 * only; behavior arrives with Phase 2 in the `qa_hamiltonian` module.
 * No allocation, no IO, no evolution.
 *
 * Operator fixed by spec §1 and used by the declaration below:
 *   numCells = n * n                 (4, 9, 16 for n = 2, 3, 4)
 *   dim      = 2 ^ numCells          (16, 512, 65536)
 *   E(k)     = unordered attacking queen-pair count of board id `k`
 *              (001-states MSB-first row-major mapping; +1 per pair sharing
 *              a row, column, or diagonal)
 *   H_problem = sum_k E(k) |k><k|    (diagonal, matrix-free)
 *   outPsi[k] = E(k) * phi[k]        (pointwise, recomputed per `k`)
 *
 * Numerical bounds for every accepted `n`: `dim <= 65536`, so the apply loop
 * is bounded and no shift can reach the `size_t` width once the `N`-gate has
 * passed; `NORM_TOL = 1e-12` gates the input norm.
 */

#include <complex.h>
#include <stddef.h>

#include "qa/core/status.h"

/**
 * @brief Apply the diagonal N-Queens problem Hamiltonian matrix-free
 * (FR-001, FR-004, FR-005).
 *
 * Computes `outPsi[k] = E(k) * phi[k]` for `k = 0..dim - 1` with
 * `dim = 2^(n*n)`, recomputing the attacking-pair energy `E(k)` pointwise
 * from the 001-states bit pattern of each `k`. Matrix-free: no `dim x dim`
 * matrix (dense or sparse) is ever allocated or materialized. The call is
 * deterministic: identical (`n`, `phi`, `dim`) inputs yield identical
 * `outPsi`, with no hidden state, RNG, or globals (FR-009).
 *
 * Fixed validation order, completed before any dereference, shift, or float
 * op and before the first write:
 *   1. `N`-gate: `n == 5` -> `QA_ERR_UNSUPPORTED` (FR-002, before pointer,
 *      dim, and finiteness checks); `n < 2` or `n > 5` -> `QA_ERR_RANGE`
 *      (`QA_ERR_OVERFLOW` for a genuinely overflowing width computation)
 *      (FR-003). No `numCells`/`dim` arithmetic runs before this gate passes.
 *   2. Pointer preconditions: `phi == NULL` or `outPsi == NULL`, or the
 *      `uintptr_t` ranges `[phi, phi + dim)` and `[outPsi, outPsi + dim)`
 *      intersect (`phi == outPsi` subsumed) -> `QA_ERR_RANGE` (FR-006).
 *   3. `dim != 2^numCells` -> `QA_ERR_RANGE` (FR-006).
 *   4. Finiteness scan of every `phi[k]` (real and imaginary parts);
 *      any non-finite amplitude -> `QA_ERR_DOMAIN` (FR-007). The full scan
 *      completes before the norm gate and before any write.
 *   5. Norm gate `norm = sqrt(sum_k |phi[k]|^2)`; `|norm - 1| <= 1e-12`
 *      required, non-finite or out-of-tolerance norm -> `QA_ERR_DOMAIN`
 *      (FR-008).
 * Combined faults resolve by this order. All checks complete before the
 * first write; on failure existing non-NULL buffers are unchanged and `phi`
 * is bit-identical to pre-call on every path.
 *
 * @param[in]  n      Board edge: 2..4 accepted, 5 reserved, else out of range.
 * @param[in]  phi    Immutable normalized input state of length `dim`;
 *                    every amplitude finite, `|norm(phi) - 1| <= 1e-12`.
 * @param[out] outPsi Separate mutable caller-owned receiver of length `dim`;
 *                    written only on success, untouched on any failure.
 * @param[in]  dim    Expected state length, `2^(n*n)` (16, 512, 65536).
 *
 * @return `QA_OK` (apply written to `outPsi`); `QA_ERR_UNSUPPORTED`
 *         (`n == 5`, writes nothing); `QA_ERR_RANGE` (bad `n`, NULL buffer,
 *         overlapping buffers, or `dim` mismatch; writes nothing);
 *         `QA_ERR_DOMAIN` (non-finite amplitude or norm-gate failure; writes
 *         nothing); `QA_ERR_OVERFLOW` (bounds-unsafe width arithmetic; writes
 *         nothing).
 *
 * @owner Allocates nothing. Both buffers stay caller-owned on every path;
 *        the function never takes ownership and the caller releases both.
 * @assumes Pure function of (`n`, `phi`, `dim`); `phi` is treated as
 *          read-only and `outPsi` as a disjoint receiver. Numerical work
 *          assumes finite inputs for the norm gate and exact small-integer
 *          energies `E(k)` scaled into `complex double` outputs.
 */
QaStatus qaHamiltonianApplyProblem(unsigned int n, const complex double *phi, complex double *outPsi, size_t dim);

#endif /* QA_HAMILTONIAN_PROBLEM_H */
