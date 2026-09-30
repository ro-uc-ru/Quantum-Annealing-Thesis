#ifndef QA_CORE_GRID_H
#define QA_CORE_GRID_H

/*
 * Classical board vectors: compact grid identifiers (spec 001-states, T02).
 *
 * Scope: RF-001 (bit-packed board), RF-004 (MSB-first `id` <-> `(i, j)`),
 * RF-005 (pure cell access). Declarations only; behavior arrives with
 * T03-T05 in `src/core/grid.c`. No allocation, no IO, no amplitudes.
 *
 * Derived quantities fixed by spec §1 and used by every helper below:
 *   numCells = n * n                 (4, 9, 16 for n = 2, 3, 4)
 *   pos      = i * n + j             (row-major, 0 <= pos < numCells)
 *   shift    = numCells - 1 - pos    (MSB-first: (0,0) is the top used bit)
 *   limit    = ((uint32_t)1u << numCells)   (exclusive bound of `id`)
 *
 * Numerical bounds for every accepted `n` (spec §3): `shift <= 15 < 32` and
 * `limit <= ((uint32_t)1u << 16)`, so no shift can reach the `QaGridId` width
 * and, because validation precedes every shift, no bad input can trap.
 */

#include <stdint.h>

#include "qa/core/status.h"

/**
 * @brief Bit-packed classical board (RF-001): the board *is* the value
 * `|k⟩`, so no conversion, copy, or allocation exists.
 *
 * Cell `(i, j)` is the bit at `shift = numCells - 1 - (i * n + j)`; only the
 * low `numCells` bits are significant, and canonical values keep every bit at
 * or above `numCells` clear (RF-007). A non-canonical value is rejected with
 * `QA_ERR_RANGE`, never masked.
 *
 * @owner Value type passed by value; nothing to allocate or release.
 * @assumes Accepted `n` is 2, 3, or 4 (`n == 5` is reserved by RF-002), so
 *          `numCells` is 4, 9, or 16 bits and the board always fits the
 *          32-bit pack. Canonical inputs are enforced by the helpers below,
 *          never assumed of the caller.
 */
typedef uint32_t QaGridId;

/**
 * @brief Read one cell of a board without mutating anything (RF-004, RF-005).
 *
 * Fixed validation order, completed before any shift or mask (codestyle §7,
 * then spec §1; D-06):
 *   1. `outBit == NULL` -> `QA_ERR_RANGE`. Checked first because the receiver
 *      is caller storage that must be proven writable before any work.
 *   2. `n == 5` -> `QA_ERR_UNSUPPORTED` (RF-002).
 *   3. `n < 2` or `n > 5` -> `QA_ERR_RANGE` (RF-003).
 *   4. `i >= n` or `j >= n` -> `QA_ERR_RANGE` (RF-006, EC-04).
 *   5. `id >= limit` -> `QA_ERR_RANGE` (RF-006, RF-007, EC-03, EC-06).
 * The cell is then read as `(id >> shift) & 1u`, which yields exactly `0` or
 * `1` because `shift < 32` and `id` is already canonical.
 *
 * @param[in]  id     Board to read; canonical form only (bits >= numCells clear).
 * @param[in]  n      Board edge: 2..4 accepted, 5 reserved, else out of range.
 * @param[in]  i      Row index, `0 <= i < n`.
 * @param[in]  j      Column index, `0 <= j < n`.
 * @param[out] outBit Non-NULL caller-owned receiver of the cell value `0` or `1`.
 *
 * @return `QA_OK` (cell written to `*outBit`); `QA_ERR_RANGE` (NULL receiver,
 *         `n < 2`, `n > 5`, `i >= n`, `j >= n`, or non-canonical `id`);
 *         `QA_ERR_UNSUPPORTED` (`n == 5`). On failure `*outBit` is left
 *         untouched, so this receiver is validated but deliberately never
 *         poisoned: spec §1 fixes the untouched-on-failure contract and the
 *         out-param is caller-owned, which overrides the generic poisoning
 *         rule of codestyle §8.
 *
 * @owner No allocation. `outBit` stays owned by the caller on every path.
 * @assumes Pure and immutable: no input mutates (EC-08) and no shift or mask
 *          runs before validation, so a bad input can neither trap nor alias.
 */
QaStatus qaGridGetBit(QaGridId id, unsigned int n, unsigned int i,
                      unsigned int j, unsigned int *outBit);

/**
 * @brief Return a new board with exactly one cell written to `bit`
 * (RF-004, RF-005).
 *
 * Same fixed order as `qaGridGetBit`, extended by the final cell check:
 *   6. `bit > 1` -> `QA_ERR_RANGE` (RF-006, EC-05).
 * On success the result has the single bit at
 * `shift = numCells - 1 - (i * n + j)` written to `bit` and every other cell
 * preserved, so setting and clearing are the same call with `bit` `1` or `0`.
 * The input id is never mutated: callers compare it before and after to assert
 * immutability (EC-08, D-05).
 *
 * @param[in]  id    Board to derive from; canonical form only.
 * @param[in]  n     Board edge: 2..4 accepted, 5 reserved, else out of range.
 * @param[in]  i     Row index, `0 <= i < n`.
 * @param[in]  j     Column index, `0 <= j < n`.
 * @param[in]  bit   New cell value, `0` or `1`; `> 1` is rejected.
 * @param[out] outId Non-NULL caller-owned receiver of the new board id.
 *
 * @return `QA_OK` (new id written to `*outId`); `QA_ERR_RANGE` (NULL
 *         receiver, `n < 2`, `n > 5`, `i >= n`, `j >= n`, non-canonical `id`,
 *         or `bit > 1`); `QA_ERR_UNSUPPORTED` (`n == 5`). On failure `*outId`
 *         is left untouched and is therefore validated but never poisoned,
 *         for the same spec §1 over codestyle §8 reason as `qaGridGetBit`.
 *
 * @owner No allocation. `outId` is a value written into caller storage that
 *        stays owned by the caller on every path.
 * @assumes Pure: `id` is unchanged and only the low `numCells` bits of
 *          `*outId` can ever be set; validation precedes every shift, and
 *          `shift < 32` bounds the single mask applied on success.
 */
QaStatus qaGridWithBit(QaGridId id, unsigned int n, unsigned int i,
                       unsigned int j, unsigned int bit, QaGridId *outId);

#endif /* QA_CORE_GRID_H */
