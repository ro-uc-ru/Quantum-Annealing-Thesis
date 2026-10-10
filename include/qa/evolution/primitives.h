#ifndef QA_EVOLUTION_PRIMITIVES_H
#define QA_EVOLUTION_PRIMITIVES_H

/*
 * Split-operator primitives: diagonal phase `Dz` and exact driver rotation `Rx`
 * (spec 005-evolution, T-002).
 *
 * Scope: FR-003 (`Rx(dt, b)` as the exact product over cells, matrix-free, no
 * splitting error inside the driver term), FR-004 (`Dz(s, a)` as the pointwise
 * phase on every amplitude, matrix-free), FR-027 (validation order of `Dz` and
 * `Rx`, state unchanged on any error), EC-014 (large finite `theta = dt b` is
 * accepted; `Rx` is periodic up to a global phase) and EC-021 (phase arguments
 * that overflow are rejected before any write). The closed forms of FR-022 are
 * the reference values of the tests, listed below. Declarations only; behavior
 * lives in `src/evolution/primitives.c`. No allocation, no IO, no state.
 *
 * Definitions fixed by spec §1 and used by the declarations below:
 *   n            board edge, `2..4` accepted; `5` is reserved (QA_ERR_UNSUPPORTED)
 *   numCells     n * n                          (4, 9, 16 for n = 2, 3, 4)
 *   dim          2^numCells                     (16, 512, 65536)
 *   psi          caller-owned state, exactly `dim` complex amplitudes
 *   H_target(k)  diagonal target operator, `E(k) - r q(k)` (see
 *                `qa/hamiltonian/target.h`); `|H_target(k)| <= 120` for every
 *                accepted `n`, so `max|H_target| <= 120`
 *   Dz(s, a)     `exp(-i s a H_target)`, the pointwise phase
 *                `exp(-i s a H_target(k))` on every amplitude `k`
 *   Rx(dt, b)    `exp(-i dt b H_driver) = prod_c (cos(theta) I - i sin(theta) sigma^x_c)`,
 *                `theta = dt b`, a product over the `numCells` cells that is
 *                exact because the `sigma^x_c` commute
 *
 * Closed forms of FR-022 (reference values for the closed-form tests):
 *   - `Rx` on `|->^n` multiplies the state by `exp(+i numCells theta)`.
 *   - `Dz` on `|k>` multiplies it by `exp(-i s a H_target(k))`.
 *   - `Rx` on `|0...0>` gives amplitude `cos(theta)^numCells` on `|0...0>`.
 *
 * Numerical bounds: both operators are unitary, so they preserve the norm up
 * to rounding; the driver checks the norm against `NORM_TOL` (FR-005), not
 * these primitives. The phase pre-check `s * a * max|H_target|` and `dt * b`
 * are the only products that can overflow; both are checked before any write
 * (EC-021). `numCells <= 16` once the `N`-gate has passed, so `2^numCells`
 * cannot reach the `size_t` width.
 */

#include <complex.h>
#include <stddef.h>

#include "qa/core/status.h"

/**
 * @brief Apply the diagonal phase `Dz(s, a)` in place on every amplitude
 * (FR-004, FR-027, EC-021).
 *
 * Multiplies `psi[k]` by `exp(-i s a H_target(k))` for `k = 0..dim - 1`,
 * recomputing `H_target(k)` pointwise from `k` (matrix-free, ARC-1). Fixed
 * validation order, completed before the first write (FR-027):
 *   1. `N`-gate (FR-016): `n == 5` -> `QA_ERR_UNSUPPORTED`; `n < 2` or
 *      `n > 5` -> `QA_ERR_RANGE`. No size arithmetic runs before this gate.
 *   2. `psi == NULL` -> `QA_ERR_DOMAIN` (FR-027).
 *   3. `s` or `a` not finite -> `QA_ERR_DOMAIN` (FR-027).
 *   4. `dim` arithmetic (FR-027): `dim != 2^(n*n)` -> `QA_ERR_RANGE`. This
 *      bounds every write of step 6 by the caller's buffer length.
 *   5. Phase pre-check (EC-021): pass 1 computes `max|H_target|` over the
 *      `dim` board ids and `s * a * max|H_target|` must be finite; overflow
 *      -> `QA_ERR_DOMAIN`.
 *   6. Pass 2 writes the phases in place.
 * On any error `psi` is unchanged: steps 1 to 5 complete before the write.
 *
 * @param[in]     n   Board edge: `2..4` accepted, `5` reserved, else out of range.
 * @param[in]     s   Normalized time factor, finite.
 * @param[in]     a   Problem-Hamiltonian weight, finite.
 * @param[in,out] psi Caller-owned state of exactly `dim` amplitudes; rewritten
 *                    in place on success, unchanged on failure.
 * @param[in]     dim Length of `psi`, equal to `2^(n*n)` (16, 512, 65536).
 *
 * @return `QA_OK` (phases applied in place); `QA_ERR_UNSUPPORTED` (`n == 5`);
 *         `QA_ERR_RANGE` (`n` outside `[2, 5]` or `dim != 2^(n*n)`);
 *         `QA_ERR_DOMAIN` (NULL `psi`, non-finite `s` or `a`, or a phase
 *         argument that overflows). `psi` is unchanged on every failure.
 *
 * @owner Allocates nothing. `psi` stays owned by the caller on every path; the
 *        function never takes ownership and never resizes or frees it.
 * @assumes `psi` holds `dim` writable `complex double` values that do not
 *          alias any other argument. The phase is computed in `double`, with
 *          `|exp(-i phase)| = 1` up to rounding, so the operator is unitary and
 *          preserves the norm up to rounding; the result is deterministic and
 *          independent of global state (TST-3).
 */
QaStatus qaEvolutionDz(unsigned int n, double s, double a,
                       complex double *psi, size_t dim);

/**
 * @brief Apply the exact driver rotation `Rx(dt, b)` in place over all cells
 * (FR-003, FR-027, EC-014, EC-021).
 *
 * Applies `prod_c (cos(theta) I - i sin(theta) sigma^x_c)` with `theta = dt b`
 * to `psi` as one butterfly pass per cell, in place. The per-cell blocks are
 * exact, so there is no splitting error inside the driver term. Fixed
 * validation order, completed before the first write (FR-027):
 *   1. `N`-gate (FR-016): `n == 5` -> `QA_ERR_UNSUPPORTED`; `n < 2` or
 *      `n > 5` -> `QA_ERR_RANGE`. No size arithmetic runs before this gate.
 *   2. `psi == NULL` -> `QA_ERR_DOMAIN` (FR-027).
 *   3. `dt` or `b` not finite -> `QA_ERR_DOMAIN` (FR-027); `dt * b` overflows
 *      to infinity -> `QA_ERR_DOMAIN` (EC-021). A large finite `theta` is
 *      accepted (EC-014).
 *   4. `dim` arithmetic (FR-027): `dim != 2^(n*n)` -> `QA_ERR_RANGE`. This
 *      bounds every butterfly write by the caller's buffer length.
 *   5. Per-cell passes write in place.
 * On any error `psi` is unchanged: steps 1 to 4 complete before the write.
 *
 * @param[in]     n   Board edge: `2..4` accepted, `5` reserved, else out of range.
 * @param[in]     dt  Time step, finite.
 * @param[in]     b   Driver weight, finite.
 * @param[in,out] psi Caller-owned state of exactly `dim` amplitudes; rewritten
 *                    in place on success, unchanged on failure.
 * @param[in]     dim Length of `psi`, equal to `2^(n*n)` (16, 512, 65536).
 *
 * @return `QA_OK` (rotation applied in place); `QA_ERR_UNSUPPORTED` (`n == 5`);
 *         `QA_ERR_RANGE` (`n` outside `[2, 5]` or `dim != 2^(n*n)`);
 *         `QA_ERR_DOMAIN` (NULL `psi`, non-finite `dt` or `b`, or `dt * b`
 *         overflows). `psi` is unchanged on every failure.
 *
 * @owner Allocates nothing. `psi` stays owned by the caller on every path; the
 *        function never takes ownership and never resizes or frees it.
 * @assumes `psi` holds `dim` writable `complex double` values that do not
 *          alias any other argument. `cos(theta)` and `sin(theta)` are computed
 *          in `double` for any finite `theta`, periodic up to a global phase
 *          (EC-014); the norm is preserved up to rounding; no integration
 *          accuracy is claimed beyond the measured regime (FR-007).
 */
QaStatus qaEvolutionRx(unsigned int n, double dt, double b,
                       complex double *psi, size_t dim);

#endif /* QA_EVOLUTION_PRIMITIVES_H */
