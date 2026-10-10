#ifndef QA_HAMILTONIAN_TARGET_H
#define QA_HAMILTONIAN_TARGET_H

/*
 * Target Hamiltonian: diagonal queen-reward operator H_target(k)
 * (spec 005-evolution, T-001).
 *
 * Scope: FR-001 (definition `H_target(k) = E(k) - r q(k)`, matrix-free, no
 * `dim x dim` matrix, ARC-1), FR-016 (`N`-gate codes `QA_ERR_UNSUPPORTED` for
 * `N = 5` and `QA_ERR_RANGE` for `N` outside `[2, 5]`) and FR-027 (validation
 * order of the public `H_target(k)` primitive). The constant `r` is the one
 * fixed by this spec (TFG II.A); it is recorded as `r` in `config.json`.
 * Declarations only; behavior lives in `src/hamiltonian/target.c`. No
 * allocation, no IO, no state, no evolution.
 *
 * Definitions fixed by spec §1 and used by the declaration below:
 *   n          board edge, `2..4` accepted; `5` is reserved (QA_ERR_UNSUPPORTED)
 *   numCells   n * n                          (4, 9, 16 for n = 2, 3, 4)
 *   dim        2^numCells                     (16, 512, 65536)
 *   k          board id, `0 <= k < dim`, 001-states MSB-first row-major mapping
 *   E(k)       attacking queen-pair count of board `k` (as in 002: +1 per pair
 *              sharing a row, column or diagonal)
 *   q(k)       number of queens (set bits) of board `k`
 *   r          queen-reward constant, `QA_HAMILTONIAN_TARGET_R = 0.5`
 *   H_target(k) = E(k) - r q(k)              (diagonal, matrix-free)
 *
 * Numerical bounds: `q(k) <= numCells <= 16` and `E(k) <= C(numCells, 2) <= 120`
 * for every accepted `n`, so `|H_target(k)| <= 120`. With `r = 0.5` every value
 * is a multiple of 0.5 and exact in `double`; `r * q(k)` never rounds. `dim`
 * is at most 65536 once the `N`-gate has passed, so `2^numCells` cannot reach
 * the `size_t` width. The ground set is the set of `k` with minimal
 * `H_target(k)` (spec §1).
 */

#include <stddef.h>

#include "qa/core/status.h"

/**
 * Queen-reward constant `r` of `H_target(k) = E(k) - r q(k)` (FR-001, TFG
 * II.A). Exact in binary floating point: `r * q(k)` is a multiple of 0.5 and
 * is never rounded.
 */
#define QA_HAMILTONIAN_TARGET_R 0.5

/**
 * @brief Evaluate the diagonal target Hamiltonian entry `H_target(k)` of one
 * board, matrix-free (FR-001, FR-016, FR-027).
 *
 * Computes `*outValue = E(k) - r * q(k)` for board `k` of an `n x n` board,
 * recomputing `E(k)` and `q(k)` from the bit pattern of `k` on every call. No
 * `dim x dim` matrix is ever allocated (ARC-1). Pure function: identical
 * inputs give identical output, with no hidden state or globals (TST-3).
 *
 * Fixed validation order, completed before any read of `k` and before the
 * single write (FR-027):
 *   1. `N`-gate (FR-016): `n == 5` -> `QA_ERR_UNSUPPORTED`; `n < 2` or
 *      `n > 5` -> `QA_ERR_RANGE`. No `numCells` or `dim` arithmetic runs before
 *      this gate passes.
 *   2. `outValue == NULL` -> `QA_ERR_DOMAIN` (FR-027).
 *   3. `k >= dim`, with `dim = 2^(n*n)` (computed after step 1; it cannot
 *      overflow once the gate has passed, see the scope comment) ->
 *      `QA_ERR_RANGE` (FR-027).
 * On success `*outValue` is written exactly once, after every check.
 *
 * @param[in]  n        Board edge: `2..4` accepted, `5` reserved, anything else
 *                      out of range.
 * @param[in]  k        Board id, `0 <= k < 2^(n*n)`.
 * @param[out] outValue Non-NULL caller-owned receiver of `H_target(k)`. Written
 *                      only on success; left untouched on every failure.
 *
 * @return `QA_OK` (`*outValue` written); `QA_ERR_UNSUPPORTED` (`n == 5`);
 *         `QA_ERR_RANGE` (`n` outside `[2, 5]` or `k >= dim`); `QA_ERR_DOMAIN`
 *         (`outValue` is NULL). On every failure `*outValue` is untouched and
 *         nothing is allocated.
 *
 * @owner Allocates nothing. `outValue` stays owned by the caller on every
 *        path; the function never takes ownership of any pointer.
 * @assumes `E(k)` and `q(k)` are exact small integers and `r = 0.5` is exact,
 *          so the result is exact (multiple of 0.5, `|H_target(k)| <= 120`
 *          for every accepted `n`). Deterministic, pure and independent of
 *          global state (TST-3).
 */
QaStatus qaHamiltonianTarget(unsigned int n, size_t k, double *outValue);

#endif /* QA_HAMILTONIAN_TARGET_H */
