/*
 * 005-evolution time grid `qaEvolutionTimeNode` and `qaEvolutionTimeMid`
 * (spec 005-evolution, task T-009, FR-017, FR-026, EC-018, EC-019).
 *
 * Purpose: the node times `t_j = T (j / steps)` and the midpoint times
 * `tm_j = T ((2j + 1) / (2 steps))` of the Strang grid. The evolution and the
 * trace use exactly these two functions, so no other code derives a time. The
 * fraction is formed first and then multiplied by `T`: it lies in `[0, 1]`, so
 * the product cannot overflow for any finite `T` (EC-019), and `t_steps = T`
 * exactly because that node is returned without a division (EC-018).
 *
 * Ownership: no allocation, no globals, no state. The receiver is caller-owned
 * and written exactly once, only on `QA_OK`.
 * Errors: `QA_ERR_DOMAIN` (NULL receiver, `T` not finite or `<= 0`,
 * `steps == 0`, `dt = T / steps` zero or not finite), `QA_ERR_OVERFLOW` (the
 * size arithmetic of the function would overflow), `QA_ERR_RANGE` (`steps`
 * above `QA_EVOLVE_STEPS_MAX`, or `j` outside its range); failures write
 * nothing.
 * Numerical assumptions: IEEE-754 `double`. Division and multiplication are
 * correctly rounded and monotone, and the exact fractions satisfy
 * `j/steps <= (2j+1)/(2 steps) <= (j+1)/steps`, so the rounded values keep that
 * order weakly and so do the products with `T`. `steps <= 10^6` makes `j`,
 * `steps`, `2j + 1` and `2 steps` exact in `double`.
 */

#include "qa/evolution/evolve.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Validate `outT`, `T` and `steps` for the time grid, steps 1 to 3, the
 * size step given by the caller and steps 5 to 6 of the documented order.
 *
 * `maxSteps` carries the size limit of the caller's step 4: `SIZE_MAX - 1`
 * for `time_node` (`steps + 1` must not overflow) and `SIZE_MAX / 2` for
 * `time_mid` (`2 * steps` must not overflow).
 *
 * @param[in] T        Total time, finite and `> 0`.
 * @param[in] steps    Step count, `1..QA_EVOLVE_STEPS_MAX`.
 * @param[in] maxSteps Largest `steps` whose size arithmetic does not overflow.
 * @param[in] outT     Receiver pointer, only tested against NULL.
 *
 * @return `QA_OK`; `QA_ERR_DOMAIN`; `QA_ERR_OVERFLOW`; `QA_ERR_RANGE`.
 *
 * @owner No allocation; nothing is written.
 * @assumes `T / steps` is evaluated only after `steps >= 1` and
 *          `steps <= QA_EVOLVE_STEPS_MAX`, so the divisor is a small positive
 *          exact double.
 */
static QaStatus qaTimegridValidate(double T, size_t steps, size_t maxSteps,
                                   const double *outT)
{
    if (outT == NULL) {
        return QA_ERR_DOMAIN;
    }
    if (!isfinite(T) || T <= 0.0) {
        return QA_ERR_DOMAIN;
    }
    if (steps == 0u) {
        return QA_ERR_DOMAIN;
    }
    /* Checked arithmetic: the caller's size expression overflows iff
     * `steps > maxSteps`. */
    if (steps > maxSteps) {
        return QA_ERR_OVERFLOW;
    }
    if (steps > (size_t)QA_EVOLVE_STEPS_MAX) {
        return QA_ERR_RANGE;
    }

    /* `steps` is in `[1, 10^6]`, exact in `double`: a zero `dt` means `T` is
     * subnormal and underflows (FR-017, EC-019). */
    double dt = T / (double)steps;
    if (dt == 0.0 || !isfinite(dt)) {
        return QA_ERR_DOMAIN;
    }
    return QA_OK;
}

/**
 * @brief Clamp a time to `[0, T]`.
 *
 * @param[in] t Candidate time, finite and `>= 0`.
 * @param[in] T Total time, finite and `> 0`.
 * @return `min(t, T)`.
 * @owner No allocation. @assumes Both arguments are finite, so `<` is exact.
 */
static double qaTimegridClamp(double t, double T)
{
    return (t < T) ? t : T;
}

/*
 * `qaEvolutionTimeNode` (task T-009, FR-026, EC-018, EC-019).
 *
 * Implements the contract declared in `include/qa/evolution/evolve.h`.
 */
QaStatus qaEvolutionTimeNode(double T, size_t steps, size_t j, double *outT)
{
    /* Steps 1 to 6; `steps + 1` overflows iff `steps == SIZE_MAX`. */
    QaStatus status = qaTimegridValidate(T, steps, SIZE_MAX - 1u, outT);
    if (status != QA_OK) {
        return status;
    }

    /* Step 7: `j` ranges over `0..steps`. */
    if (j > steps) {
        return QA_ERR_RANGE;
    }

    /* Exact end node: no division at `j = steps` (EC-018). Otherwise
     * `j <= steps <= 10^6` is exact in `double`, the fraction is in `[0, 1]`
     * and the product cannot overflow (EC-019). The single write follows
     * every check. */
    if (j == steps) {
        *outT = T;
    } else {
        /* Clamp: only guards rounding of a fraction in `[0, 1]`; it never hides an out-of-range `j`. */
        *outT = qaTimegridClamp(T * ((double)j / (double)steps), T);
    }
    return QA_OK;
}

/*
 * `qaEvolutionTimeMid` (task T-009, FR-026, EC-018, EC-019).
 *
 * Implements the contract declared in `include/qa/evolution/evolve.h`.
 */
QaStatus qaEvolutionTimeMid(double T, size_t steps, size_t j, double *outT)
{
    /* Steps 1 to 6; `2 * steps` overflows iff `steps > SIZE_MAX / 2`. */
    QaStatus status = qaTimegridValidate(T, steps, SIZE_MAX / 2u, outT);
    if (status != QA_OK) {
        return status;
    }

    /* Step 7: `j` ranges over `0..steps-1`. */
    if (j >= steps) {
        return QA_ERR_RANGE;
    }

    /* Bound: `j < steps <= 10^6`, so `2j + 1 <= 2 steps - 1 <= 2 * 10^6` are
     * exact `size_t` and `double` values; the fraction is in `(0, 1)`, so the
     * product cannot overflow. The single write follows every check. */
    double frac = (double)(2u * j + 1u) / (double)(2u * steps);
    /* Clamp: only guards rounding of a fraction in `(0, 1)`; it never hides an out-of-range `j`. */
    *outT = qaTimegridClamp(T * frac, T);
    return QA_OK;
}
