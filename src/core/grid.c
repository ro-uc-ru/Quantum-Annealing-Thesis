/*
 * 001-states Phase 2 implementation (task T03): N-gate + derived quantities.
 *
 * Purpose: validate the board edge `n` and derive `numCells = n * n` and
 * `limit = ((uint32_t)1u << numCells)` before any shift or mask runs
 * (spec §1, RF-002, RF-003, D-06). Cell reads (`qaGridGetBit`) arrived with
 * T04 and single-cell writes (`qaGridWithBit`) arrive with T05 below.
 *
 * Ownership: no allocation; every out-param is caller-owned storage.
 * Errors: every fallible helper returns `QaStatus`; on failure out-params
 * are left untouched (spec §1). Numerical assumptions: `QaGridId` is a
 * `uint32_t`, so the target must provide an exact 32-bit type; the assert
 * below fails the build otherwise. Accepted `n` is 2, 3, or 4, hence
 * `numCells` is 4, 9, or 16 and the `limit` shift stays below the 32-bit
 * width by construction.
 */

#include "qa/core/grid.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof (uint32_t) == 4,
               "spec 001-states assumes uint32_t is exactly 32 bits");
_Static_assert(sizeof (unsigned int) == 4,
               "spec 001-states assumes unsigned int is exactly 32 bits");

/**
 * @brief Validate the board edge and derive `numCells` and `limit` (T03).
 *
 * Fixed order, completed before any shift (RF-002 before RF-003, D-06):
 *   1. `outNumCells == NULL` or `outLimit == NULL` -> `QA_ERR_RANGE`
 *      (codestyle §7: receivers are caller storage proven first).
 *   2. `n == 5` -> `QA_ERR_UNSUPPORTED` (RF-002, EC-02).
 *   3. `n < 2` or `n > 5` -> `QA_ERR_RANGE` (RF-003, EC-01).
 * On success `*outNumCells` is `n * n` (4, 9, or 16) and `*outLimit` is
 * `((uint32_t)1u << numCells)` (16, 512, or 65536), the exclusive bound
 * every board id is checked against (RF-006, RF-007).
 *
 * @param[in]  n           Board edge: 2..4 accepted, 5 reserved, else invalid.
 * @param[out] outNumCells Non-NULL caller-owned receiver of `n * n`.
 * @param[out] outLimit    Non-NULL caller-owned receiver of the id bound.
 *
 * @return `QA_OK` (both receivers written); `QA_ERR_UNSUPPORTED` (`n == 5`);
 *         `QA_ERR_RANGE` (NULL receiver, `n < 2`, or `n > 5`). On failure
 *         both receivers are left untouched (spec §1), so they are validated
 *         but never poisoned.
 *
 * @owner No allocation. Both receivers stay owned by the caller on every path.
 * @assumes The `n * n` multiply runs only for `n` in [2, 4], so it cannot
 *          overflow; the `limit` shift runs only with `numCells` in
 *          {4, 9, 16}, hence strictly below the 32-bit width, and the
 *          narrowing to `QaGridId` is value-preserving.
 */
static QaStatus qaGridResolveN(unsigned int n, unsigned int *outNumCells,
                               QaGridId *outLimit)
{
    if (outNumCells == NULL || outLimit == NULL) {
        return QA_ERR_RANGE;
    }
    if (n == 5) {
        return QA_ERR_UNSUPPORTED;
    }
    if (n < 2 || n > 5) {
        return QA_ERR_RANGE;
    }

    /* Checked arithmetic: `n` is 2, 3, or 4 here, so `n * n` is 4, 9, or 16
     * and the shift below stays strictly below the 32-bit width. */
    unsigned int numCells = n * n;
    *outNumCells = numCells;
    *outLimit = ((uint32_t)1u << numCells);
    return QA_OK;
}

/*
 * `qaGridGetBit` (task T04, RF-004, RF-005, RF-006, RF-007).
 *
 * Implements the contract declared in `include/qa/core/grid.h`: NULL
 * receiver first (codestyle §7), then the fixed domain order `n` → `i,j`
 * → `id` (spec §1, D-06), and no shift or mask before every check has
 * passed. The header carries the normative §6 documentation; what follows
 * states only the implementation-side arithmetic facts.
 */
QaStatus qaGridGetBit(QaGridId id, unsigned int n, unsigned int i,
                      unsigned int j, unsigned int *outBit)
{
    unsigned int numCells = 0u;
    QaGridId limit = (QaGridId)0u;
    QaStatus status;

    if (outBit == NULL) {
        return QA_ERR_RANGE;
    }

    status = qaGridResolveN(n, &numCells, &limit);
    if (status != QA_OK) {
        return status;
    }
    if (i >= n || j >= n) {
        return QA_ERR_RANGE;
    }
    if (id >= limit) {
        return QA_ERR_RANGE;
    }

    /* Checked arithmetic: `i` and `j` are below `n <= 4`, so
     * `pos = i * n + j <= numCells - 1` cannot overflow or underflow the
     * `shift = numCells - 1 - pos <= 15 < 32` below; `id` is canonical, so
     * `(id >> shift) & 1u` is exactly `0` or `1`. */
    unsigned int pos = i * n + j;
    unsigned int shift = numCells - 1u - pos;
    *outBit = (id >> shift) & 1u;
    return QA_OK;
}

/*
 * `qaGridWithBit` (task T05, RF-005, RF-006, RF-007).
 *
 * Implements the contract declared in `include/qa/core/grid.h`: NULL
 * receiver first (codestyle §7), then the fixed domain order `n` → `i,j`
 * → `id` → `bit` (spec §1, D-06), and no shift or mask before every check
 * has passed. The input id is passed by value, so immutability holds by
 * construction; callers still compare before/after (EC-08). The header
 * carries the normative §6 documentation; what follows states only the
 * implementation-side arithmetic facts.
 */
QaStatus qaGridWithBit(QaGridId id, unsigned int n, unsigned int i,
                       unsigned int j, unsigned int bit, QaGridId *outId)
{
    unsigned int numCells = 0u;
    QaGridId limit = (QaGridId)0u;
    QaStatus status;

    if (outId == NULL) {
        return QA_ERR_RANGE;
    }

    status = qaGridResolveN(n, &numCells, &limit);
    if (status != QA_OK) {
        return status;
    }
    if (i >= n || j >= n) {
        return QA_ERR_RANGE;
    }
    if (id >= limit) {
        return QA_ERR_RANGE;
    }
    if (bit > 1u) {
        return QA_ERR_RANGE;
    }

    /* Checked arithmetic: same `pos`/`shift` bounds as `qaGridGetBit`
     * (`shift <= 15 < 32`), so the single mask below is exact. Clearing
     * preserves the canonical form: `id` already has every bit at or above
     * `numCells` clear, and the mask only touches one bit below it. */
    unsigned int pos = i * n + j;
    unsigned int shift = numCells - 1u - pos;
    QaGridId mask = (QaGridId)1u << shift;
    if (bit == 1u) {
        *outId = id | mask;
    } else {
        *outId = id & ~mask;
    }
    return QA_OK;
}
