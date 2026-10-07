/*
 * 004-driver unit tests, Phase 3 (tasks T-004..T-009: `n-gate`, `shape`,
 * `initial-state`, `domain`, `vectors`, `properties` and `contract`).
 *
 * Purpose: assert the specified initial-state behavior group by group, each
 * group selectable as `test-004-driver <group>` so every tasks.md
 * `ctest -R 004-driver-<group>` pattern matches exactly one CTest entry.
 * Failing checks print the `TEST-004-driver-FR0XX` / `TEST-004-driver-EC0XX`
 * identifier required by the plan. The apply-side checks live in the
 * `domain`, `vectors`, `properties` and `contract` groups.
 *
 * Groups: `n-gate` (EC-001, EC-002, EC-017), `shape` (EC-003, EC-004,
 * EC-017), `initial-state` (FR-010, EC-015), `domain`, `vectors`,
 * `properties` and `contract` (apply-side FR/EC are named in each check).
 * No arguments runs every group.
 * Numerical assumptions: tolerances are 1e-12 on `complex double` results,
 * and the EC-026 reference norm is accumulated in `long double`.
 *
 * Ownership: stack buffers only, nothing to release. Errors: any failed
 * check is printed and the process exits non-zero; an unknown group exits 2
 * with usage. Determinism: fixed vectors only, no clock, RNG, environment or
 * filesystem access.
 */

#include "qa/hamiltonian/driver.h"

#include <complex.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Largest accepted state length, 2^16 for N = 4. */
#define MAX_DIM 65536u

/* Canary bit pattern: a quiet NaN payload no computation produces. */
#define CANARY_RE 0x7ff8dead00000001ull
#define CANARY_IM 0x7ff8beef00000002ull

/* Static so the 1 MiB buffers stay off the stack. */
static complex double gOut[MAX_DIM];
static complex double gRef[MAX_DIM];
static complex double gPhi[MAX_DIM];
static complex double gX[MAX_DIM];
static complex double gY[MAX_DIM];
static complex double gHx[MAX_DIM];
static complex double gHy[MAX_DIM];
/* One extra element so `gBig + 1` still has `MAX_DIM` valid elements. */
static complex double gBig[MAX_DIM + 1u];
static complex double gBigRef[MAX_DIM + 1u];

/**
 * @brief Record one check; the run stays allocation-free.
 *
 * @param[in] condition Non-zero when the check passed.
 * @param[in] what Description printed on failure; valid NUL-terminated string.
 * @return 0 when `condition` holds, 1 after printing a failure line.
 *
 * @owner Nothing is allocated; `what` stays with the caller.
 * @assumes No numerical assumptions.
 */
static int checkFailed(int condition, const char *what)
{
    if (condition) {
        return 0;
    }
    printf("FAIL: %s\n", what);
    return 1;
}

/**
 * @brief Fill `buf[0..len-1]` with the canary and keep a bit-exact copy.
 *
 * @param[out] buf Caller-owned buffer of `len` elements.
 * @param[out] ref Caller-owned buffer of `len` elements, bit copy of `buf`.
 * @param[in]  len Element count, at most `MAX_DIM`.
 *
 * @owner No allocation; both buffers stay with the caller.
 * @assumes `complex double` is two adjacent `double`s, so `memcpy` of the
 *          bit patterns builds the canary without a signalling conversion.
 */
static void fillCanary(complex double *buf, complex double *ref, size_t len)
{
    uint64_t re = CANARY_RE;
    uint64_t im = CANARY_IM;
    double parts[2];

    memcpy(&parts[0], &re, sizeof re);
    memcpy(&parts[1], &im, sizeof im);
    for (size_t k = 0u; k < len; ++k) {
        memcpy(&buf[k], parts, sizeof parts);
        memcpy(&ref[k], parts, sizeof parts);
    }
}

/**
 * @brief Check `buf` is bit-identical to `ref` over `len` elements.
 *
 * @param[in] buf Buffer under test, `len` elements.
 * @param[in] ref Bit-exact reference, `len` elements.
 * @param[in] len Element count, at most `MAX_DIM`.
 *
 * @return 1 when every byte matches, else 0.
 *
 * @owner No allocation.
 * @assumes `memcmp` compares bit patterns, so NaN canaries compare equal.
 */
static int untouched(const complex double *buf, const complex double *ref,
                     size_t len)
{
    return memcmp(buf, ref, len * sizeof *buf) == 0;
}

/**
 * @brief Apply-side N-gate checks (task T-005): EC-001, EC-002, EC-014.
 *
 * `N = 0, 1, 6, UINT_MAX` give `QA_ERR_RANGE` and `N = 5` gives
 * `QA_ERR_UNSUPPORTED` for valid, NULL and mismatched arguments; `outPsi`
 * keeps its canary.
 *
 * @return Failure count.
 *
 * @owner Static buffers; nothing to release.
 * @assumes N-gate runs before `dim`, NULL and overlap.
 */
static int runApplyNGate(void)
{
    static const unsigned int badN[] = {0u, 1u, 6u, UINT_MAX};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 4u; ++i) {
        unsigned int n = badN[i];
        fillCanary(gOut, gRef, 16u);
        snprintf(what, sizeof what, "TEST-004-driver-EC001: apply n=%u RANGE",
                 n);
        failures += checkFailed(
            qaHamiltonianApplyDriver(n, gPhi, gOut, 16u) == QA_ERR_RANGE, what);
        failures += checkFailed(
            qaHamiltonianApplyDriver(n, NULL, NULL, 0u) == QA_ERR_RANGE,
            "TEST-004-driver-EC001: apply bad n with NULLs RANGE");
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC014: apply n=%u leaves canary", n);
        failures += checkFailed(untouched(gOut, gRef, 16u), what);
    }

    fillCanary(gOut, gRef, 16u);
    failures += checkFailed(
        qaHamiltonianApplyDriver(5u, gPhi, gOut, 16u) == QA_ERR_UNSUPPORTED,
        "TEST-004-driver-EC002: apply n=5 UNSUPPORTED");
    failures += checkFailed(
        qaHamiltonianApplyDriver(5u, NULL, NULL, 0u) == QA_ERR_UNSUPPORTED,
        "TEST-004-driver-EC002: apply n=5 with NULLs UNSUPPORTED");
    failures += checkFailed(
        qaHamiltonianApplyDriver(5u, gOut, gOut, SIZE_MAX) == QA_ERR_UNSUPPORTED,
        "TEST-004-driver-EC002: apply n=5 overlap and SIZE_MAX UNSUPPORTED");
    failures += checkFailed(untouched(gOut, gRef, 16u),
                            "TEST-004-driver-EC014: apply n=5 leaves canary");
    return failures;
}

/**
 * @brief Apply-side shape checks (task T-005): EC-003, EC-004, EC-005,
 * EC-014.
 *
 * Wrong `dim` is rejected first, also with NULL or overlapping buffers; NULL
 * `phi` or `outPsi` and the overlaps `outPsi == phi`, `outPsi == phi + 1`
 * and `phi == outPsi + 1` give `QA_ERR_RANGE`. Buffers stay bit-identical.
 *
 * @return Failure count.
 *
 * @owner Static buffers; nothing to release.
 * @assumes `gBig` holds `MAX_DIM + 1` elements.
 */
static int runApplyShape(void)
{
    static const unsigned int ns[] = {2u, 3u, 4u};
    static const size_t goodDim[] = {16u, 512u, 65536u};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 3u; ++i) {
        unsigned int n = ns[i];
        size_t dim = goodDim[i];
        /* Bound: `bad` holds 5 entries (`j < 5` below); `dim` is 16, 512 or
           65536, so `dim - 1u` cannot underflow. */
        size_t bad[5] = {dim - 1u, dim + 1u, 0u, SIZE_MAX, 1u};

        for (unsigned int j = 0u; j < 5u; ++j) {
            fillCanary(gBig, gBigRef, MAX_DIM + 1u);
            snprintf(what, sizeof what,
                     "TEST-004-driver-EC003: apply n=%u dim=%zu RANGE", n,
                     bad[j]);
            failures += checkFailed(
                qaHamiltonianApplyDriver(n, gBig, gBig + 1, bad[j])
                        == QA_ERR_RANGE
                    && qaHamiltonianApplyDriver(n, NULL, gBig, bad[j])
                           == QA_ERR_RANGE
                    && qaHamiltonianApplyDriver(n, gBig, NULL, bad[j])
                           == QA_ERR_RANGE,
                what);
            snprintf(what, sizeof what,
                     "TEST-004-driver-EC014: apply n=%u dim=%zu canary", n,
                     bad[j]);
            failures += checkFailed(
                untouched(gBig, gBigRef, MAX_DIM + 1u), what);
        }

        fillCanary(gBig, gBigRef, MAX_DIM + 1u);
        snprintf(what, sizeof what, "TEST-004-driver-EC004: apply n=%u NULL",
                 n);
        failures += checkFailed(
            qaHamiltonianApplyDriver(n, NULL, gBig, dim) == QA_ERR_RANGE
                && qaHamiltonianApplyDriver(n, gBig, NULL, dim) == QA_ERR_RANGE
                && qaHamiltonianApplyDriver(n, NULL, NULL, dim)
                       == QA_ERR_RANGE,
            what);
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC005: apply n=%u overlaps RANGE", n);
        failures += checkFailed(
            qaHamiltonianApplyDriver(n, gBig, gBig, dim) == QA_ERR_RANGE
                && qaHamiltonianApplyDriver(n, gBig, gBig + 1, dim)
                       == QA_ERR_RANGE
                && qaHamiltonianApplyDriver(n, gBig + 1, gBig, dim)
                       == QA_ERR_RANGE,
            what);
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC014: apply n=%u shape canary", n);
        failures += checkFailed(untouched(gBig, gBigRef, MAX_DIM + 1u), what);
    }
    return failures;
}

/**
 * @brief N-gate group (task T-004): EC-001, EC-002, EC-017.
 *
 * Calls the initial state with `N = 0, 1, 6, UINT_MAX` (`QA_ERR_RANGE`) and
 * `N = 5` (`QA_ERR_UNSUPPORTED`), each with a valid pointer and with `NULL`
 * and with a wrong `dim`, proving the N-gate runs first. `outPsi` holds a
 * canary and must be bit-identical afterwards.
 *
 * @return Failure count, 0 when every assert holds.
 *
 * @owner Static buffers; nothing to release.
 * @assumes The documented validation order (N-gate before `dim` and NULL).
 */
static int runNGate(void)
{
    static const unsigned int badN[] = {0u, 1u, 6u, UINT_MAX};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 4u; ++i) {
        unsigned int n = badN[i];
        fillCanary(gOut, gRef, 16u);
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC001: n=%u yields RANGE", n);
        failures += checkFailed(
            qaHamiltonianInitialState(n, gOut, 16u) == QA_ERR_RANGE, what);
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC017: n=%u leaves canary", n);
        failures += checkFailed(untouched(gOut, gRef, 16u), what);
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC001: n=%u NULL yields RANGE", n);
        failures += checkFailed(
            qaHamiltonianInitialState(n, NULL, 16u) == QA_ERR_RANGE, what);
    }

    fillCanary(gOut, gRef, 16u);
    failures += checkFailed(
        qaHamiltonianInitialState(5u, gOut, 16u) == QA_ERR_UNSUPPORTED,
        "TEST-004-driver-EC002: n=5 yields UNSUPPORTED");
    failures += checkFailed(
        qaHamiltonianInitialState(5u, NULL, 16u) == QA_ERR_UNSUPPORTED,
        "TEST-004-driver-EC002: n=5 with NULL yields UNSUPPORTED");
    failures += checkFailed(
        qaHamiltonianInitialState(5u, gOut, 0u) == QA_ERR_UNSUPPORTED,
        "TEST-004-driver-EC002: n=5 with dim 0 yields UNSUPPORTED");
    failures += checkFailed(
        qaHamiltonianInitialState(5u, gOut, SIZE_MAX) == QA_ERR_UNSUPPORTED,
        "TEST-004-driver-EC002: n=5 with dim SIZE_MAX yields UNSUPPORTED");
    failures += checkFailed(untouched(gOut, gRef, 16u),
                            "TEST-004-driver-EC017: n=5 leaves canary");
    failures += runApplyNGate();
    return failures;
}

/**
 * @brief Shape group (task T-004): EC-003, EC-004, EC-017.
 *
 * Wrong `dim` values (15/17, 511/513, 65535/65537, 0, `SIZE_MAX`) return
 * `QA_ERR_RANGE`, also with a NULL pointer (`dim` is checked first), and a
 * NULL `outPsi` with valid `N` and `dim` returns `QA_ERR_RANGE`. The canary
 * must survive every failure. Each `N` is also checked against the `dim` of
 * the other boards.
 *
 * @return Failure count, 0 when every assert holds.
 *
 * @owner Static buffers; nothing to release.
 * @assumes The documented validation order (`dim` before NULL).
 */
static int runShape(void)
{
    static const unsigned int ns[] = {2u, 3u, 4u};
    static const size_t goodDim[] = {16u, 512u, 65536u};
    static const size_t badDim[][2] = {
        {15u, 17u}, {511u, 513u}, {65535u, 65537u}};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 3u; ++i) {
        /* Bound: `bad` holds 7 entries (`j < 7` below); `(i + 1) % 3` and
           `(i + 2) % 3` stay inside the 3-entry `goodDim` table. */
        size_t bad[7] = {badDim[i][0], badDim[i][1], 0u, SIZE_MAX,
                         goodDim[(i + 1u) % 3u], goodDim[(i + 2u) % 3u], 1u};
        for (unsigned int j = 0u; j < 7u; ++j) {
            fillCanary(gOut, gRef, MAX_DIM);
            snprintf(what, sizeof what,
                     "TEST-004-driver-EC003: n=%u dim=%zu yields RANGE",
                     ns[i], bad[j]);
            failures += checkFailed(
                qaHamiltonianInitialState(ns[i], gOut, bad[j]) == QA_ERR_RANGE,
                what);
            snprintf(what, sizeof what,
                     "TEST-004-driver-EC003: n=%u dim=%zu NULL yields RANGE",
                     ns[i], bad[j]);
            failures += checkFailed(
                qaHamiltonianInitialState(ns[i], NULL, bad[j]) == QA_ERR_RANGE,
                what);
            snprintf(what, sizeof what,
                     "TEST-004-driver-EC017: n=%u dim=%zu leaves canary",
                     ns[i], bad[j]);
            failures += checkFailed(untouched(gOut, gRef, MAX_DIM), what);
        }
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC004: n=%u NULL outPsi yields RANGE",
                 ns[i]);
        failures += checkFailed(
            qaHamiltonianInitialState(ns[i], NULL, goodDim[i]) == QA_ERR_RANGE,
            what);
    }
    failures += runApplyShape();
    return failures;
}

/**
 * @brief Initial-state group (task T-004): FR-010, EC-015.
 *
 * For `N = 2, 3, 4`: returns `QA_OK`, writes `(-1)^popcount(k) * s` for every
 * `k` with imaginary part bit-exactly `+0.0`, `psi0[0] = +s`,
 * `psi0[dim-1] = (-1)^numCells * s`, `| ||psi0|| - 1 | <= 1e-12`, nothing is
 * written past `dim` (guard band of canaries), and a second call is
 * bit-identical.
 *
 * @return Failure count, 0 when every assert holds.
 *
 * @owner Static buffers; nothing to release.
 * @assumes `s = 1.0 / sqrt((double) dim)`, recomputed here independently
 *          with a loop-based popcount.
 */
static int runInitialState(void)
{
    static const unsigned int ns[] = {2u, 3u, 4u};
    static const size_t dims[] = {16u, 512u, 65536u};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 3u; ++i) {
        unsigned int n = ns[i];
        unsigned int numCells = n * n;
        size_t dim = dims[i];
        double s = 1.0 / sqrt((double)dim);
        int signsOk = 1;
        int imagOk = 1;
        double norm2 = 0.0;
        uint64_t zeroBits = 0u;
        double zero = 0.0;

        memcpy(&zeroBits, &zero, sizeof zero);
        /* Bound: `MAX_DIM` is the capacity of `gOut` and `gRef`. */
        fillCanary(gOut, gRef, MAX_DIM);
        snprintf(what, sizeof what, "TEST-004-driver-FR010: n=%u yields OK", n);
        failures += checkFailed(
            qaHamiltonianInitialState(n, gOut, dim) == QA_OK, what);

        for (size_t k = 0u; k < dim; ++k) {
            unsigned int pop = 0u;
            double expect;
            uint64_t imBits;
            double im = cimag(gOut[k]);

            for (size_t b = k; b != 0u; b >>= 1) {
                pop += (unsigned int)(b & 1u);
            }
            expect = (pop % 2u == 0u) ? s : -s;
            signsOk = signsOk && creal(gOut[k]) == expect;
            memcpy(&imBits, &im, sizeof im);
            imagOk = imagOk && imBits == zeroBits;
            norm2 += creal(gOut[k]) * creal(gOut[k]);
        }
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR010: n=%u amplitudes (-1)^popcount * s", n);
        failures += checkFailed(signsOk, what);
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC015: n=%u imaginary part is +0.0", n);
        failures += checkFailed(imagOk, what);
        snprintf(what, sizeof what, "TEST-004-driver-EC015: n=%u psi0[0] = +s",
                 n);
        failures += checkFailed(creal(gOut[0]) == s, what);
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC015: n=%u psi0[dim-1] = (-1)^numCells s",
                 n);
        /* Bound: `dim >= 16`, so `dim - 1u` cannot underflow and is the last
         * valid index of `gOut`. */
        failures += checkFailed(
            creal(gOut[dim - 1u]) == ((numCells % 2u == 0u) ? s : -s), what);
        snprintf(what, sizeof what, "TEST-004-driver-FR010: n=%u unit norm", n);
        failures += checkFailed(fabs(sqrt(norm2) - 1.0) <= 1e-12, what);

        /* Guard band: elements past `dim` must still hold the canary. */
        {
            int guardOk = 1;
            for (size_t k = dim; k < MAX_DIM; ++k) {
                guardOk = guardOk &&
                          memcmp(&gOut[k], &gRef[k], sizeof gOut[k]) == 0;
            }
            snprintf(what, sizeof what,
                     "TEST-004-driver-FR010: n=%u writes only within dim", n);
            failures += checkFailed(guardOk, what);
        }

        /* Determinism: a second call is bit-identical to the first. */
        /* Bound: `dim <= MAX_DIM`, the capacity of `gRef` and `gOut`, so both
           `dim * sizeof` byte counts stay inside the buffers. */
        memcpy(gRef, gOut, dim * sizeof gOut[0]);
        memset(gOut, 0, dim * sizeof gOut[0]);
        (void)qaHamiltonianInitialState(n, gOut, dim);
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR010: n=%u repeated call bit-identical", n);
        failures += checkFailed(untouched(gOut, gRef, dim), what);
    }
    return failures;
}

/**
 * @brief Domain group (task T-006): FR-007, FR-008, EC-006, EC-007, EC-014,
 * EC-026.
 *
 * With a canary in `outPsi` (bit-identical afterwards): a poisoned index
 * (NaN, +/-inf, real and imaginary, first, middle and last index) gives
 * `QA_ERR_DOMAIN`; so do the zero vector, `2.5 * |k>`, `|k>` scaled just
 * outside `1e-12`, and `1e200 * |k>` whose squares overflow. A normalized
 * `|k>` and the `dim = 65536` vector `x[k] = 1 + k` scaled by its
 * `long double` reference norm pass the gate (never `QA_ERR_DOMAIN`).
 *
 * @return Failure count.
 *
 * @owner Static buffers; nothing to release.
 * @assumes Inputs that pass the gate are computed; only
 *          `!= QA_ERR_DOMAIN` is asserted for them here (`vectors` checks
 *          values).
 */
static int runDomain(void)
{
    static const unsigned int ns[] = {2u, 3u, 4u};
    static const size_t dims[] = {16u, 512u, 65536u};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 3u; ++i) {
        unsigned int n = ns[i];
        size_t dim = dims[i];
        /* Bound: `dim >= 16`, so `dim - 1u` cannot underflow; all three
         * indices are below `dim`. */
        size_t idx[3] = {0u, dim / 2u, dim - 1u};
        double bad[3] = {NAN, INFINITY, -INFINITY};

        for (unsigned int a = 0u; a < 3u; ++a) {
            for (unsigned int b = 0u; b < 3u; ++b) {
                for (unsigned int part = 0u; part < 2u; ++part) {
                    /* Bound: `dim <= MAX_DIM` bounds the memset; `dim >= 16`,
                     * so index 3 is inside `gPhi`. */
                    memset(gPhi, 0, dim * sizeof gPhi[0]);
                    gPhi[3] = 1.0;
                    /* Bound: `idx[a] < dim` (table of valid indices for this `dim`). */
                    gPhi[idx[a]] = (part == 0u) ? CMPLX(bad[b], 0.0)
                                                : CMPLX(0.0, bad[b]);
                    fillCanary(gOut, gRef, dim);
                    snprintf(what, sizeof what,
                             "TEST-004-driver-EC006: n=%u poison %u/%u/%u",
                             n, a, b, part);
                    failures += checkFailed(
                        qaHamiltonianApplyDriver(n, gPhi, gOut, dim)
                                == QA_ERR_DOMAIN
                            && untouched(gOut, gRef, dim),
                        what);
                }
            }
        }

        /* Finite vectors outside the gate: zero, 2.5|k>, overflow, 1+2e-12. */
        {
            double scale[4] = {0.0, 2.5, 1e200, 1.0 + 2e-12};
            for (unsigned int c = 0u; c < 4u; ++c) {
                /* Bound: `dim <= MAX_DIM` bounds the memset; `dim >= 16`, so
                 * index 5 is inside `gPhi`. */
                memset(gPhi, 0, dim * sizeof gPhi[0]);
                gPhi[5] = scale[c];
                fillCanary(gOut, gRef, dim);
                snprintf(what, sizeof what,
                         "TEST-004-driver-EC007: n=%u scale case %u", n, c);
                failures += checkFailed(
                    qaHamiltonianApplyDriver(n, gPhi, gOut, dim)
                            == QA_ERR_DOMAIN
                        && untouched(gOut, gRef, dim),
                    what);
            }
        }

        /* A normalized |k> passes the gate. */
        /* Bound: `dim >= 16`, so index 5 is inside `gPhi`. */
        memset(gPhi, 0, dim * sizeof gPhi[0]);
        gPhi[5] = 1.0;
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR008: n=%u normalized |k> accepted", n);
        failures += checkFailed(
            qaHamiltonianApplyDriver(n, gPhi, gOut, dim) != QA_ERR_DOMAIN,
            what);
    }

    /* EC-026: dim = 65536, x[k] = 1 + k scaled by the long double norm. */
    {
        long double acc = 0.0L;
        for (size_t k = 0u; k < MAX_DIM; ++k) {
            long double x = 1.0L + (long double)k;
            acc += x * x;
        }
        long double ref = sqrtl(acc);
        /* Bound: `k < MAX_DIM`, the capacity of `gPhi`. */
        for (size_t k = 0u; k < MAX_DIM; ++k) {
            gPhi[k] = (double)((1.0L + (long double)k) / ref);
        }
        fillCanary(gOut, gRef, MAX_DIM);
        failures += checkFailed(
            qaHamiltonianApplyDriver(4u, gPhi, gOut, MAX_DIM) != QA_ERR_DOMAIN,
            "TEST-004-driver-EC026: non-power-of-two dim 65536 accepted");
    }
    return failures;
}

/**
 * @brief Vectors group (task T-007): FR-001, FR-004, EC-008.
 *
 * `H|0>` at `N = 2` has value 1 at indices 1, 2, 4, 8 and 0 elsewhere. For
 * sampled `k` at `N = 2, 3, 4`, `H|k>` has value 1 at exactly the `numCells`
 * indices `k XOR m_c` (`m_c = 1 << (numCells - 1 - c)`) and 0 elsewhere;
 * `phi` is unchanged by the call. Values are compared exactly, `+0.0` and
 * `-0.0` being equal.
 *
 * @return Failure count.
 *
 * @owner Static buffers; nothing to release.
 * @assumes The expected support is rebuilt here from the mask definition.
 */
static int runVectors(void)
{
    static const unsigned int ns[] = {2u, 3u, 4u};
    static const size_t dims[] = {16u, 512u, 65536u};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 3u; ++i) {
        unsigned int n = ns[i];
        unsigned int numCells = n * n;
        size_t dim = dims[i];
        /* Bound: `dim >= 16`, so every sample (including `dim - 1u`) is below
         * `dim`; `sIdx < 5u` below indexes this 5-entry table. */
        size_t samples[5] = {0u, 1u, 5u, dim / 2u, dim - 1u};

        for (unsigned int sIdx = 0u; sIdx < 5u; ++sIdx) {
            size_t k0 = samples[sIdx];
            int ok = 1;
            int phiOk;

            /* Bound: `dim <= MAX_DIM` bounds the memset/memcpy; `k0 < dim` (taken from
               the sample table for this `dim`) bounds the `gPhi[k0]` write. */
            memset(gPhi, 0, dim * sizeof gPhi[0]);
            gPhi[k0] = 1.0;
            memcpy(gRef, gPhi, dim * sizeof gPhi[0]);
            memset(gOut, 0, dim * sizeof gOut[0]);
            snprintf(what, sizeof what,
                     "TEST-004-driver-EC008: n=%u k=%zu yields OK", n, k0);
            failures += checkFailed(
                qaHamiltonianApplyDriver(n, gPhi, gOut, dim) == QA_OK, what);

            for (size_t k = 0u; k < dim; ++k) {
                double expect = 0.0;
                for (unsigned int c = 0u; c < numCells; ++c) {
                    /* Shift bound: `c < numCells <= 16`, so the count
                     * `numCells - 1 - c` is 0..15 and cannot underflow. */
                    if (k == (k0 ^ ((size_t)1u << (numCells - 1u - c)))) {
                        expect = 1.0;
                    }
                }
                ok = ok && creal(gOut[k]) == expect && cimag(gOut[k]) == 0.0;
            }
            snprintf(what, sizeof what,
                     "TEST-004-driver-EC008: n=%u k=%zu support is k XOR m_c",
                     n, k0);
            failures += checkFailed(ok, what);
            phiOk = untouched(gPhi, gRef, dim);
            snprintf(what, sizeof what,
                     "TEST-004-driver-FR004: n=%u k=%zu phi unchanged", n, k0);
            failures += checkFailed(phiOk, what);
        }
    }

    /* EC-008 literal case: H|0> at N = 2 is ones at 1, 2, 4, 8. */
    /* Bound: 16 elements <= `MAX_DIM`, the capacity of `gPhi`; index 0 < 16. */
    memset(gPhi, 0, 16u * sizeof gPhi[0]);
    gPhi[0] = 1.0;
    (void)qaHamiltonianApplyDriver(2u, gPhi, gOut, 16u);
    for (size_t k = 0u; k < 16u; ++k) {
        int isOne = (k == 1u || k == 2u || k == 4u || k == 8u);
        failures += checkFailed(creal(gOut[k]) == (isOne ? 1.0 : 0.0),
                                "TEST-004-driver-EC008: H|0> at N=2 literal");
    }
    return failures;
}

/**
 * @brief Fill `v[0..dim-1]` with a deterministic normalized complex vector.
 *
 * Components come from a fixed 64-bit LCG seeded by `seed`, then are scaled
 * by the `long double` norm, independent of the code under test.
 *
 * @param[out] v    Caller-owned buffer of `dim` elements.
 * @param[in]  dim  Element count, at most `MAX_DIM`.
 * @param[in]  seed LCG seed; equal seeds give equal vectors.
 *
 * @owner No allocation; `v` stays with the caller.
 * @assumes `uint64_t` wraps modulo 2^64; the top 53 bits map to [-1, 1).
 */
static void fillRandom(complex double *v, size_t dim, uint64_t seed)
{
    uint64_t state = seed;
    long double acc = 0.0L;
    double parts[2];

    /* Bound: `k < dim <= MAX_DIM`, the capacity of every vector passed in;
     * `p < 2u` indexes the 2-entry `parts`. */
    for (size_t k = 0u; k < dim; ++k) {
        for (unsigned int p = 0u; p < 2u; ++p) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            parts[p] = (double)(state >> 11) / 4503599627370496.0 - 1.0;
        }
        v[k] = CMPLX(parts[0], parts[1]);
        acc += (long double)parts[0] * parts[0]
               + (long double)parts[1] * parts[1];
    }
    {
        /* `acc > 0`: a zero vector would need all `2 * dim` generated parts
         * to be exactly 0.0, which the fixed LCG streams never produce. */
        long double norm = sqrtl(acc);
        for (size_t k = 0u; k < dim; ++k) {
            v[k] = CMPLX((double)((long double)creal(v[k]) / norm),
                         (double)((long double)cimag(v[k]) / norm));
        }
    }
}

/**
 * @brief Properties group (task T-008): FR-011, FR-016, EC-009..EC-012,
 * EC-016.
 *
 * For `N = 2, 3, 4`: `H |+>^numCells = +numCells |+>^numCells` per amplitude
 * (EC-009); `||H phi|| <= numCells + 1e-12` for a random normalized `phi`
 * (EC-010); `<x|H y> = <H x|y>` for random normalized `x`, `y` (EC-011);
 * linearity for two distinct basis states (EC-012); and for the initial
 * state `H psi0 = -numCells psi0` per amplitude and energy `-numCells`, all
 * within `1e-12`, bit-exact for `H psi0` at `N = 2` (FR-011, EC-016).
 *
 * @return Failure count.
 *
 * @owner Static buffers; nothing to release.
 * @assumes `fillRandom` vectors are normalized to far better than `1e-12`.
 */
static int runProperties(void)
{
    static const unsigned int ns[] = {2u, 3u, 4u};
    static const size_t dims[] = {16u, 512u, 65536u};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 3u; ++i) {
        unsigned int n = ns[i];
        double numCells = (double)(n * n);
        size_t dim = dims[i];
        double s = 1.0 / sqrt((double)dim);
        double maxErr = 0.0;
        double sumSq = 0.0;
        complex double lhs = 0.0;
        complex double rhs = 0.0;
        complex double energy = 0.0;
        int exact = 1;

        /* EC-009: |+>^numCells is an eigenvector with eigenvalue +numCells. */
        /* Bound: `k < dim <= MAX_DIM`, the capacity of `gPhi`. */
        for (size_t k = 0u; k < dim; ++k) {
            gPhi[k] = s;
        }
        failures += checkFailed(
            qaHamiltonianApplyDriver(n, gPhi, gOut, dim) == QA_OK,
            "TEST-004-driver-EC009: apply |+> yields OK");
        for (size_t k = 0u; k < dim; ++k) {
            double e = cabs(gOut[k] - numCells * s);
            maxErr = (e > maxErr) ? e : maxErr;
        }
        snprintf(what, sizeof what, "TEST-004-driver-EC009: n=%u eigenvalue", n);
        failures += checkFailed(maxErr <= 1e-12, what);

        /* EC-010: operator norm bound on a random normalized vector. */
        /* Bound: `dim <= MAX_DIM`, the capacity of `gX`, `gY` and `gHx`. */
        fillRandom(gX, dim, 12345u + n);
        (void)qaHamiltonianApplyDriver(n, gX, gHx, dim);
        for (size_t k = 0u; k < dim; ++k) {
            sumSq += creal(gHx[k]) * creal(gHx[k])
                     + cimag(gHx[k]) * cimag(gHx[k]);
        }
        snprintf(what, sizeof what, "TEST-004-driver-EC010: n=%u norm bound", n);
        failures += checkFailed(sqrt(sumSq) <= numCells + 1e-12, what);

        /* EC-011: Hermiticity <x|H y> == <H x|y>. */
        /* Bound: `dim <= MAX_DIM`, the capacity of `gY` and `gHy`. */
        fillRandom(gY, dim, 98765u + n);
        (void)qaHamiltonianApplyDriver(n, gY, gHy, dim);
        for (size_t k = 0u; k < dim; ++k) {
            lhs += conj(gX[k]) * gHy[k];
            rhs += conj(gHx[k]) * gY[k];
        }
        snprintf(what, sizeof what, "TEST-004-driver-EC011: n=%u Hermitian", n);
        failures += checkFailed(cabs(lhs - rhs) <= 1e-12, what);

        /* EC-012: linearity on two distinct basis states. */
        /* Bound: `dim <= MAX_DIM` bounds both memsets; `dim >= 16`, so index
         * 1 is inside `gX`. */
        memset(gX, 0, dim * sizeof gX[0]);
        memset(gY, 0, dim * sizeof gY[0]);
        gX[1] = 1.0;
        /* Bound: `dim >= 16`, so `dim - 2u` cannot underflow and stays below `dim`. */
        gY[dim - 2u] = 1.0;
        /* Bound: `k < dim <= MAX_DIM`, the capacity of `gPhi`, `gX`, `gY`. */
        for (size_t k = 0u; k < dim; ++k) {
            gPhi[k] = (gX[k] + gY[k]) / sqrt(2.0);
        }
        (void)qaHamiltonianApplyDriver(n, gX, gHx, dim);
        (void)qaHamiltonianApplyDriver(n, gY, gHy, dim);
        failures += checkFailed(
            qaHamiltonianApplyDriver(n, gPhi, gOut, dim) == QA_OK,
            "TEST-004-driver-EC012: apply superposition yields OK");
        maxErr = 0.0;
        for (size_t k = 0u; k < dim; ++k) {
            double e = cabs(gOut[k] - (gHx[k] + gHy[k]) / sqrt(2.0));
            maxErr = (e > maxErr) ? e : maxErr;
        }
        snprintf(what, sizeof what, "TEST-004-driver-EC012: n=%u linearity", n);
        failures += checkFailed(maxErr <= 1e-12, what);

        /* FR-011 / EC-016: psi0 is the ground state, H psi0 = -numCells psi0. */
        failures += checkFailed(
            qaHamiltonianInitialState(n, gPhi, dim) == QA_OK
                && qaHamiltonianApplyDriver(n, gPhi, gOut, dim) == QA_OK,
            "TEST-004-driver-FR011: psi0 and apply yield OK");
        maxErr = 0.0;
        for (size_t k = 0u; k < dim; ++k) {
            double e = cabs(gOut[k] + numCells * gPhi[k]);
            maxErr = (e > maxErr) ? e : maxErr;
            exact = exact && creal(gOut[k]) == -numCells * creal(gPhi[k])
                    && cimag(gOut[k]) == 0.0;
            energy += conj(gPhi[k]) * gOut[k];
        }
        snprintf(what, sizeof what, "TEST-004-driver-FR011: n=%u H psi0", n);
        failures += checkFailed(maxErr <= 1e-12, what);
        snprintf(what, sizeof what, "TEST-004-driver-FR011: n=%u energy", n);
        failures += checkFailed(cabs(energy + numCells) <= 1e-12, what);
        if (n == 2u) {
            failures += checkFailed(
                exact, "TEST-004-driver-EC016: N=2 H psi0 bit-exact");
        }
    }
    return failures;
}

/* Guard elements on each side of `outPsi` in the contract group. */
#define GUARD 8u
static complex double gGuard[MAX_DIM + 2u * GUARD];
static complex double gGuardRef[MAX_DIM + 2u * GUARD];

/**
 * @brief Contract group (task T-009): FR-005, FR-009, EC-014, EC-017.
 *
 * (a) Guard bands: `outPsi` is placed inside a canary-filled buffer with 8
 * guard elements on each side; after success only `outPsi[0..dim-1]` changed
 * and after every failure class nothing changed (EC-014, EC-017). (b) `phi`
 * is bit-identical after success and failure. (c) Interleaved calls with two
 * different inputs repeat bit-identically (no state across calls, FR-009);
 * the initial state repeats bit-identically around an apply call.
 *
 * @return Failure count.
 *
 * @owner Static buffers; nothing to release.
 * @assumes `gGuard`/`gGuardRef` hold `MAX_DIM + 2 * GUARD` elements, enough
 *          for the largest `dim` plus both guard bands.
 */
static int runContract(void)
{
    static const unsigned int ns[] = {2u, 3u, 4u};
    static const size_t dims[] = {16u, 512u, 65536u};
    int failures = 0;
    char what[128];

    for (unsigned int i = 0u; i < 3u; ++i) {
        unsigned int n = ns[i];
        size_t dim = dims[i];
        /* Bound: `total = dim + 2 * GUARD <= MAX_DIM + 2 * GUARD`, the
         * capacity of `gGuard` and `gGuardRef`; `out` starts `GUARD`
         * elements in, so `out[0..dim-1]` and both bands lie inside. */
        size_t total = dim + 2u * GUARD;
        complex double *out = gGuard + GUARD;
        int bandOk = 1;
        QaStatus st;

        /* (a) success: only out[0..dim-1] may change; phi is immutable. */
        /* Bound: `dim <= MAX_DIM`, the capacity of `gPhi`. */
        fillRandom(gPhi, dim, 777u + n);
        /* Bound: `dim <= MAX_DIM` (capacity of `gRef`/`gPhi`). */
        memcpy(gRef, gPhi, dim * sizeof gPhi[0]);
        fillCanary(gGuard, gGuardRef, total);
        st = qaHamiltonianApplyDriver(n, gPhi, out, dim);
        snprintf(what, sizeof what, "TEST-004-driver-FR005: n=%u apply OK", n);
        failures += checkFailed(st == QA_OK, what);
        /* Bound: `k < GUARD`, so `GUARD + dim + k <= total - 1` stays inside
         * `gGuard` and `gGuardRef`. */
        for (size_t k = 0u; k < GUARD; ++k) {
            bandOk = bandOk
                     && memcmp(&gGuard[k], &gGuardRef[k], sizeof gGuard[k]) == 0
                     && memcmp(&gGuard[GUARD + dim + k],
                               &gGuardRef[GUARD + dim + k],
                               sizeof gGuard[k]) == 0;
        }
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR005: n=%u apply writes only within dim", n);
        failures += checkFailed(bandOk, what);
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR005: n=%u phi bit-identical", n);
        failures += checkFailed(untouched(gPhi, gRef, dim), what);

        /* Same guard-band check for the initial state. */
        fillCanary(gGuard, gGuardRef, total);
        st = qaHamiltonianInitialState(n, out, dim);
        bandOk = (st == QA_OK);
        for (size_t k = 0u; k < GUARD; ++k) {
            bandOk = bandOk
                     && memcmp(&gGuard[k], &gGuardRef[k], sizeof gGuard[k]) == 0
                     && memcmp(&gGuard[GUARD + dim + k],
                               &gGuardRef[GUARD + dim + k],
                               sizeof gGuard[k]) == 0;
        }
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR005: n=%u initial state within dim", n);
        failures += checkFailed(bandOk, what);

        /* (b) every failure class leaves out and phi bit-identical. */
        /* Bound: `dim <= MAX_DIM`, the capacity of `gPhi`. */
        fillRandom(gPhi, dim, 31u + n);
        /* Bound: `dim <= MAX_DIM` (capacity of `gRef`/`gPhi`). */
        memcpy(gRef, gPhi, dim * sizeof gPhi[0]);
        fillCanary(gGuard, gGuardRef, total);
        bandOk = 1;
        bandOk = bandOk && qaHamiltonianApplyDriver(0u, gPhi, out, dim)
                               == QA_ERR_RANGE;
        bandOk = bandOk && qaHamiltonianApplyDriver(5u, gPhi, out, dim)
                               == QA_ERR_UNSUPPORTED;
        /* Arithmetic bound: `dim` is 16..65536, so `dim + 1u` and `dim - 1u`
         * below cannot overflow or underflow; both are wrong lengths. */
        bandOk = bandOk && qaHamiltonianApplyDriver(n, gPhi, out, dim + 1u)
                               == QA_ERR_RANGE;
        bandOk = bandOk && qaHamiltonianApplyDriver(n, NULL, out, dim)
                               == QA_ERR_RANGE;
        bandOk = bandOk && qaHamiltonianApplyDriver(n, gPhi, NULL, dim)
                               == QA_ERR_RANGE;
        bandOk = bandOk && qaHamiltonianApplyDriver(n, out, out, dim)
                               == QA_ERR_RANGE;
        /* Bound: `dim >= 16`, so index 2 is inside `gPhi`; the original value
         * is restored from `gRef` before the `phi` check below. */
        gPhi[2] = CMPLX(NAN, 0.0);
        bandOk = bandOk && qaHamiltonianApplyDriver(n, gPhi, out, dim)
                               == QA_ERR_DOMAIN;
        gPhi[2] = 3.0;
        bandOk = bandOk && qaHamiltonianApplyDriver(n, gPhi, out, dim)
                               == QA_ERR_DOMAIN;
        bandOk = bandOk && qaHamiltonianInitialState(7u, out, dim)
                               == QA_ERR_RANGE;
        bandOk = bandOk && qaHamiltonianInitialState(n, out, dim - 1u)
                               == QA_ERR_RANGE;
        bandOk = bandOk && qaHamiltonianInitialState(n, NULL, dim)
                               == QA_ERR_RANGE;
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC014: n=%u failure statuses", n);
        failures += checkFailed(bandOk, what);
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC014: n=%u failures leave buffer canary", n);
        failures += checkFailed(untouched(gGuard, gGuardRef, total), what);
        gPhi[2] = gRef[2];
        snprintf(what, sizeof what,
                 "TEST-004-driver-EC014: n=%u failures leave phi intact", n);
        failures += checkFailed(untouched(gPhi, gRef, dim), what);

        /* (c) interleaved repeats are bit-identical (FR-009). */
        fillRandom(gX, dim, 1001u + n);
        fillRandom(gY, dim, 2002u + n);
        (void)qaHamiltonianApplyDriver(n, gX, gHx, dim);
        (void)qaHamiltonianApplyDriver(n, gY, gHy, dim);
        (void)qaHamiltonianApplyDriver(n, gX, gOut, dim);
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR009: n=%u x repeat bit-identical", n);
        failures += checkFailed(untouched(gOut, gHx, dim), what);
        (void)qaHamiltonianApplyDriver(n, gY, gOut, dim);
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR009: n=%u y repeat bit-identical", n);
        failures += checkFailed(untouched(gOut, gHy, dim), what);

        /* (d) the initial state is deterministic and stateless too (FR-009):
         * apply calls between two initial-state calls must not change it.
         * Bound: `dim <= MAX_DIM`, the capacity of `gHx`, `gHy` and `gOut`. */
        st = qaHamiltonianInitialState(n, gHx, dim);
        (void)qaHamiltonianApplyDriver(n, gY, gOut, dim);
        st = (st == QA_OK) ? qaHamiltonianInitialState(n, gHy, dim) : st;
        snprintf(what, sizeof what,
                 "TEST-004-driver-FR009: n=%u initial state repeat "
                 "bit-identical", n);
        failures += checkFailed(st == QA_OK && untouched(gHy, gHx, dim), what);
    }
    return failures;
}

/**
 * @brief Entry point: run the selected group, or every group.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Optional group name: `n-gate`, `shape`, `initial-state`, `domain`, `vectors`, `properties` or `contract`.
 *
 * @return 0 when every selected check passes, 1 on any failure, 2 on an
 *         unknown group name.
 *
 * @owner No allocation.
 * @assumes `argv[1]`, when present, is a NUL-terminated string.
 */
int main(int argc, char **argv)
{
    int failures = 0;
    const char *group = (argc > 1) ? argv[1] : NULL;
    int ran = 0;

    if (group == NULL || strcmp(group, "n-gate") == 0) {
        failures += runNGate();
        ran = 1;
    }
    if (group == NULL || strcmp(group, "shape") == 0) {
        failures += runShape();
        ran = 1;
    }
    if (group == NULL || strcmp(group, "initial-state") == 0) {
        failures += runInitialState();
        ran = 1;
    }
    if (group == NULL || strcmp(group, "domain") == 0) {
        failures += runDomain();
        ran = 1;
    }
    if (group == NULL || strcmp(group, "vectors") == 0) {
        failures += runVectors();
        ran = 1;
    }
    if (group == NULL || strcmp(group, "properties") == 0) {
        failures += runProperties();
        ran = 1;
    }
    if (group == NULL || strcmp(group, "contract") == 0) {
        failures += runContract();
        ran = 1;
    }
    if (!ran) {
        /* `argv[0]` is valid: `argc >= 1` for any hosted `main` call. */
        fprintf(stderr, "usage: %s [n-gate|shape|initial-state|domain|vectors|properties|contract]\n", argv[0]);
        return 2;
    }
    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
