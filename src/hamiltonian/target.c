/*
 * 005-evolution Phase 2 implementation (task T-006): the diagonal target
 * Hamiltonian entry `H_target(k) = E(k) - r q(k)` (FR-001, FR-016, FR-027).
 *
 * Purpose: gate the board edge `n`, validate the receiver and the board id
 * `k`, then recompute `E(k)` (attacking queen-pair count) and `q(k)` (queen
 * count) from the bit pattern of `k` on every call. The 001-states mapping is
 * MSB-first row-major: cell `(i, j)` is bit `numCells - 1 - (i * n + j)`. The
 * module keeps its own static `E(k)` and `q(k)` so it depends on no other
 * module's private code; the 002 `qaHamiltonianApplyProblem` is the
 * independent oracle the tests compare against.
 *
 * Ownership: no allocation, no globals, no state across calls; the receiver
 * is caller-owned and written exactly once, only on `QA_OK`.
 * Errors: `QA_ERR_UNSUPPORTED` for `n == 5`, `QA_ERR_RANGE` for `n` outside
 * `[2, 5]` or `k >= dim`, `QA_ERR_DOMAIN` for a NULL receiver; failures write
 * nothing. No dense `dim x dim` matrix exists anywhere (ARC-1).
 * Numerical assumptions: accepted `n` is 2, 3 or 4, so `numCells <= 16`,
 * `dim <= 65536`, `q(k) <= 16` and `E(k) <= 120`; `r = 0.5` and every
 * product `r * q(k)` are exact in `double`, so the result is exact.
 */

#include "qa/hamiltonian/target.h"

#include <stddef.h>

/* Largest queen roster on any accepted board: `numCells <= 16` by the
 * N-gate, so every board's queens fit this fixed roster. */
#define QA_TARGET_MAX_QUEENS 16u

/**
 * @brief Apply the `N`-gate and derive `numCells = n * n` (FR-016, FR-027
 * step 1).
 *
 * Fixed order: `n == 5` -> `QA_ERR_UNSUPPORTED`, then `n < 2` or `n > 5` ->
 * `QA_ERR_RANGE`. It runs before any `numCells` or `dim` arithmetic and
 * before any pointer or `k` is looked at.
 *
 * @param[in]  n           Board edge: `2..4` accepted, `5` reserved, else
 *                         out of range.
 * @param[out] outNumCells Non-NULL caller-owned receiver of `n * n`; written
 *                         only on `QA_OK`.
 *
 * @return `QA_OK`; `QA_ERR_UNSUPPORTED` (`n == 5`); `QA_ERR_RANGE` (`n < 2`
 *         or `n > 5`). On failure the receiver is untouched.
 *
 * @owner No allocation; the receiver stays owned by the caller.
 * @assumes The multiply runs only for `n` in `[2, 4]`, so it cannot overflow.
 */
static QaStatus qaTargetResolveN(unsigned int n, unsigned int *outNumCells)
{
    if (n == 5u) {
        return QA_ERR_UNSUPPORTED;
    }
    if (n < 2u || n > 5u) {
        return QA_ERR_RANGE;
    }

    /* Checked arithmetic: `n` is 2, 3 or 4 here, so `n * n` is 4, 9 or 16. */
    *outNumCells = n * n;
    return QA_OK;
}

/**
 * @brief Count the queens `q(k)` of board `k`, the set bits of `k`.
 *
 * @param[in] numCells Gated cell count, 4, 9 or 16.
 * @param[in] k        Board id, already checked `< 2^numCells`.
 *
 * @return The number of set bits among the low `numCells` bits of `k`.
 *
 * @owner No allocation, no pointers.
 * @assumes `numCells <= 16`, so the loop bound is below the width of
 *          `size_t` and every shift is defined; the count is at most 16.
 */
static unsigned int qaTargetQueenCount(unsigned int numCells, size_t k)
{
    unsigned int count = 0;

    /* Shift bound: `bit < numCells <= 16`, below the width of `size_t`. */
    for (unsigned int bit = 0; bit < numCells; ++bit) {
        count += (unsigned int)((k >> bit) & 1u);
    }
    return count;
}

/**
 * @brief Count the unordered attacking queen pairs `E(k)` of board `k`.
 *
 * Uniform rule for every `n`: `+1` per queen pair sharing a row, a column or
 * a diagonal (`|i1 - i2| == |j1 - j2|`), so `E(k) = 0` iff no pair attacks.
 * Cell `(i, j)` holds a queen iff bit `numCells - 1 - (i * n + j)` of `k` is
 * set (001-states MSB-first row-major mapping).
 *
 * @param[in] n        Gated board edge, 2, 3 or 4.
 * @param[in] numCells Gated `n * n`.
 * @param[in] k        Board id, already checked `< 2^numCells`.
 *
 * @return The attacking pair count, at most `C(16, 2) = 120`.
 *
 * @owner No allocation; the fixed roster lives on the stack.
 * @assumes Gated `n`, `numCells` and `k`: at most `numCells <= 16` queens
 *          fit the roster, every shift is below 16, and the pair count fits
 *          `unsigned int` exactly.
 */
static unsigned int qaTargetPairEnergy(unsigned int n, unsigned int numCells,
                                       size_t k)
{
    unsigned int rows[QA_TARGET_MAX_QUEENS];
    unsigned int cols[QA_TARGET_MAX_QUEENS];
    unsigned int queens = 0;

    for (unsigned int i = 0; i < n; ++i) {
        for (unsigned int j = 0; j < n; ++j) {
            /* Checked arithmetic: `i, j < n <= 4`, so `pos <= numCells - 1`
             * and `shift = numCells - 1 - pos` cannot underflow; it is at
             * most 15, below the width of `size_t`. */
            unsigned int pos = i * n + j;
            unsigned int shift = numCells - 1u - pos;
            if (((k >> shift) & 1u) != 0u) {
                /* Bound: `queens < numCells <= 16` before this increment,
                 * so the roster index is inside both arrays. */
                rows[queens] = i;
                cols[queens] = j;
                ++queens;
            }
        }
    }

    unsigned int energy = 0;
    for (unsigned int a = 0; a < queens; ++a) {
        for (unsigned int b = a + 1u; b < queens; ++b) {
            /* Bound: `a < b < queens <= 16` (roster built above, checked
             * against the arrays' capacity), so all four reads are inside
             * `rows` and `cols`. */
            unsigned int rowGap = (rows[a] > rows[b]) ? rows[a] - rows[b]
                                                      : rows[b] - rows[a];
            unsigned int colGap = (cols[a] > cols[b]) ? cols[a] - cols[b]
                                                      : cols[b] - cols[a];
            if (rows[a] == rows[b] || cols[a] == cols[b] || rowGap == colGap) {
                ++energy;
            }
        }
    }
    return energy;
}

/*
 * `qaHamiltonianTarget` (task T-006, FR-001, FR-016, FR-027).
 *
 * Implements the contract declared in `include/qa/hamiltonian/target.h`: the
 * `N`-gate first, then the NULL receiver, then `k >= dim`; the single write
 * happens after every check.
 */
QaStatus qaHamiltonianTarget(unsigned int n, size_t k, double *outValue)
{
    unsigned int numCells = 0;

    /* Step 1 (FR-016): the gate precedes every size computation. */
    QaStatus status = qaTargetResolveN(n, &numCells);
    if (status != QA_OK) {
        return status;
    }

    /* Step 2 (FR-027): NULL receiver is a domain error. */
    if (outValue == NULL) {
        return QA_ERR_DOMAIN;
    }

    /* Step 3 (FR-027): `numCells <= 16` after the gate, so the shift is
     * below the width of `size_t` and `dim <= 65536` cannot overflow. */
    size_t dim = (size_t)1u << numCells;
    if (k >= dim) {
        return QA_ERR_RANGE;
    }

    /* Exact arithmetic: `E <= 120`, `q <= 16` and `r = 0.5`, so `r * q` is a
     * multiple of 0.5 and the subtraction never rounds. The write is the
     * only one and follows every check above. */
    unsigned int energy = qaTargetPairEnergy(n, numCells, k);
    unsigned int queens = qaTargetQueenCount(numCells, k);
    *outValue = (double)energy - QA_HAMILTONIAN_TARGET_R * (double)queens;
    return QA_OK;
}
