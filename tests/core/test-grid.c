/*
 * 001-states behavior tests, Phase 3 (tasks T06-T10, one group per task).
 *
 * Purpose: assert the specified grid behavior group by group, each group
 * selectable as `test-001-states-grid <group>` so every tasks.md
 * `ctest -R 001-states-<group>` pattern matches exactly one CTest entry.
 * Failing checks print the `TEST-001-states-RF00X` identifier required by
 * spec §5 (the CTest name carries the tasks.md pattern, the printed tag
 * carries the spec §5 identifier).
 *
 * Groups: `n-gate` (T-01, task T06), `id-range` (T-02, task T07),
 * `cell-access` (T-03, task T08), `vectors` (T-04, task T09),
 * `immutability` (T-05, task T10). No arguments runs every group.
 *
 * Ownership: no allocation; nothing to release. Errors: any failed check
 * is printed with its RF tag and the process exits non-zero; an unknown
 * group name exits 2 with usage. Numerical assumptions: `QaGridId` is a
 * 32-bit pack and `unsigned int` is 32-bit (asserted by the header test).
 * Determinism: fixed vectors only, no clock, no RNG, no environment or
 * filesystem access, so repeated runs are byte-identical (§13).
 */

#include "qa/core/grid.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Records one failing check; the run stays allocation-free. */
static int checkFailed(int condition, const char *what)
{
    if (condition) {
        return 0;
    }
    printf("FAIL: %s\n", what);
    return 1;
}

/**
 * @brief N-gate group T-01 (task T06): RF-002, RF-003, EC-01, EC-02.
 *
 * Feeds `N = 0, 1, 2, 3, 4, 5, 6, UINT_MAX` through both public helpers
 * with otherwise-valid arguments, so any rejection is attributable to the
 * N-gate alone: `QA_ERR_UNSUPPORTED` only for `N == 5`, `QA_ERR_RANGE`
 * for `0, 1, 6, UINT_MAX`, `QA_OK` for `2, 3, 4` with exact derived
 * values. Failing calls must leave their receivers untouched (EC-01,
 * EC-02 "no change").
 *
 * @return Failure count, 0 when every N-gate assert holds.
 *
 * @owner No allocation; all receivers are function-local storage.
 * @assumes `qaGridGetBit`/`qaGridWithBit` implement the documented fixed
 *          validation order with the N-gate before any shift.
 */
static int runNGate(void)
{
    static const unsigned int badRangeN[] = {0, 1, 6, UINT_MAX};
    static const unsigned int goodN[] = {2, 3, 4};
    static const QaGridId cornerId[] = {8, 256, 32768}; /* (0,0) set */
    int failures = 0;
    char what[128];

    for (unsigned int k = 0; k < 4; ++k) {
        unsigned int n = badRangeN[k];
        unsigned int bit = 42u; /* sentinel: never 0/1, must survive */
        QaGridId id = (QaGridId)0xDEADu;
        snprintf(what, sizeof what,
                 "TEST-001-states-RF003: getBit n=%u yields RANGE", n);
        failures += checkFailed(qaGridGetBit(0u, n, 0, 0, &bit)
                                    == QA_ERR_RANGE,
                                what);
        snprintf(what, sizeof what,
                 "TEST-001-states-RF003: getBit n=%u leaves receiver", n);
        failures += checkFailed(bit == 42u, what);
        snprintf(what, sizeof what,
                 "TEST-001-states-RF003: withBit n=%u yields RANGE", n);
        failures += checkFailed(qaGridWithBit(0u, n, 0, 0, 0u, &id)
                                    == QA_ERR_RANGE,
                                what);
        snprintf(what, sizeof what,
                 "TEST-001-states-RF003: withBit n=%u leaves receiver", n);
        failures += checkFailed(id == (QaGridId)0xDEADu, what);
    }

    {
        unsigned int bit = 42u;
        QaGridId id = (QaGridId)0xDEADu;
        failures += checkFailed(qaGridGetBit(0u, 5, 0, 0, &bit)
                                    == QA_ERR_UNSUPPORTED,
                                "TEST-001-states-RF002: getBit n=5 yields UNSUPPORTED");
        failures += checkFailed(bit == 42u,
                                "TEST-001-states-RF002: getBit n=5 leaves receiver");
        failures += checkFailed(qaGridWithBit(0u, 5, 0, 0, 0u, &id)
                                    == QA_ERR_UNSUPPORTED,
                                "TEST-001-states-RF002: withBit n=5 yields UNSUPPORTED");
        failures += checkFailed(id == (QaGridId)0xDEADu,
                                "TEST-001-states-RF002: withBit n=5 leaves receiver");
    }

    for (unsigned int k = 0; k < 3; ++k) {
        unsigned int n = goodN[k];
        unsigned int bit = 42u;
        QaGridId id = (QaGridId)0xDEADu;
        snprintf(what, sizeof what,
                 "TEST-001-states-RF003: getBit n=%u proceeds", n);
        failures += checkFailed(qaGridGetBit(0u, n, 0, 0, &bit) == QA_OK,
                                what);
        snprintf(what, sizeof what,
                 "TEST-001-states-RF003: empty cell n=%u reads 0", n);
        failures += checkFailed(bit == 0u, what);
        snprintf(what, sizeof what,
                 "TEST-001-states-RF003: withBit n=%u proceeds", n);
        failures += checkFailed(qaGridWithBit(0u, n, 0, 0, 1u, &id) == QA_OK,
                                what);
        snprintf(what, sizeof what,
                 "TEST-001-states-RF003: corner id n=%u exact", n);
        failures += checkFailed(id == cornerId[k], what);
    }

    if (failures == 0) {
        printf("001-states n-gate: OK (5 unsupported; 0,1,6,UINT_MAX range; 2,3,4 proceed)\n");
    }
    return failures;
}

/**
 * @brief Id-range group T-02 (task T07): RF-001, RF-006, RF-007, EC-03,
 * EC-06, EC-07.
 *
 * For every `n` in {2, 3, 4}: the one-past-max id (`limit = 1u << numCells`,
 * i.e. 16/512/65536) and `0xFFFFFFFF` are rejected by both helpers with
 * receivers untouched (RF-006, RF-007: rejected, never masked — a masking
 * implementation would return `QA_OK` here). The empty (`0`) and full
 * (`limit - 1`) boards round-trip `id -> cells -> id` exactly with outputs
 * restricted to `0`/`1` (RF-001, EC-07).
 *
 * @return Failure count, 0 when every id-range assert holds.
 *
 * @owner No allocation; all receivers are function-local storage.
 * @assumes `qaGridGetBit`/`qaGridWithBit` enforce the canonical id bound
 *          `id < limit` after the N-gate (spec §1, RF-006, RF-007).
 */
static int runIdRange(void)
{
    static const unsigned int ns[] = {2, 3, 4};
    static const unsigned int cells[] = {4, 9, 16};
    int failures = 0;
    char what[160];

    for (unsigned int k = 0; k < 3; ++k) {
        unsigned int n = ns[k];
        unsigned int numCells = cells[k];
        QaGridId limit = (QaGridId)((uint32_t)1u << numCells);
        QaGridId full = (QaGridId)(limit - 1u);
        QaGridId trials[2] = {0u, 0u};

        /* EC-03: one-past-max rejected, receivers untouched. */
        {
            unsigned int bit = 42u; /* sentinel: never 0/1, must survive */
            QaGridId id = (QaGridId)0xDEADu;
            snprintf(what, sizeof what,
                     "TEST-001-states-RF006: getBit id=limit n=%u RANGE", n);
            failures += checkFailed(qaGridGetBit(limit, n, 0, 0, &bit)
                                        == QA_ERR_RANGE,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-001-states-RF006: getBit id=limit n=%u untouched",
                     n);
            failures += checkFailed(bit == 42u, what);
            snprintf(what, sizeof what,
                     "TEST-001-states-RF006: withBit id=limit n=%u RANGE", n);
            failures += checkFailed(qaGridWithBit(limit, n, 0, 0, 0u, &id)
                                        == QA_ERR_RANGE,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-001-states-RF006: withBit id=limit n=%u untouched",
                     n);
            failures += checkFailed(id == (QaGridId)0xDEADu, what);
        }

        /* EC-06/RF-007: any bit at or above numCells set is rejected,
         * never masked. */
        {
            unsigned int bit = 42u;
            QaGridId id = (QaGridId)0xDEADu;
            failures += checkFailed(qaGridGetBit(0xFFFFFFFFu, n, 0, 0, &bit)
                                        == QA_ERR_RANGE,
                                    "TEST-001-states-RF007: getBit 0xFFFFFFFF RANGE, never masked");
            failures += checkFailed(bit == 42u,
                                    "TEST-001-states-RF007: getBit 0xFFFFFFFF untouched");
            failures += checkFailed(qaGridWithBit(0xFFFFFFFFu, n, 0, 0, 0u,
                                                  &id)
                                        == QA_ERR_RANGE,
                                    "TEST-001-states-RF007: withBit 0xFFFFFFFF RANGE, never masked");
            failures += checkFailed(id == (QaGridId)0xDEADu,
                                    "TEST-001-states-RF007: withBit 0xFFFFFFFF untouched");
        }

        /* EC-07/RF-001: empty and full round-trip id -> cells -> id exact. */
        trials[0] = 0u;
        trials[1] = full;
        for (unsigned int t = 0; t < 2; ++t) {
            QaGridId want = trials[t];
            QaGridId rebuilt = 0u;
            unsigned int wantBit = (t == 1u) ? 1u : 0u;
            for (unsigned int pos = 0; pos < numCells; ++pos) {
                unsigned int i = pos / n;
                unsigned int j = pos % n;
                unsigned int got = 0xDEADu;
                QaGridId next = (QaGridId)0xDEADu;
                snprintf(what, sizeof what,
                         "TEST-001-states-RF001: read id=%u n=%u(%u,%u)",
                         want, n, i, j);
                failures += checkFailed(qaGridGetBit(want, n, i, j, &got)
                                            == QA_OK,
                                        what);
                snprintf(what, sizeof what,
                         "TEST-001-states-RF001: cell id=%u n=%u(%u,%u)==%u",
                         want, n, i, j, wantBit);
                failures += checkFailed(got == wantBit, what);
                snprintf(what, sizeof what,
                         "TEST-001-states-RF001: rebuild id=%u n=%u(%u,%u)",
                         want, n, i, j);
                failures += checkFailed(qaGridWithBit(rebuilt, n, i, j, got,
                                                      &next)
                                            == QA_OK,
                                        what);
                rebuilt = next;
            }
            snprintf(what, sizeof what,
                     "TEST-001-states-RF001: round-trip id=%u n=%u exact",
                     want, n);
            failures += checkFailed(rebuilt == want, what);
        }
    }

    if (failures == 0) {
        printf("001-states id-range: OK (one-past-max and 0xFFFFFFFF rejected unmasked; empty/full round-trips exact)\n");
    }
    return failures;
}

/**
 * @brief Cell-access group T-03 (task T08): RF-005, RF-006, EC-04, EC-05,
 * EC-09.
 *
 * For every `n` in {2, 3, 4}: every valid `(i, j)` reads `0` on the empty
 * board and `1` on the full board, and accepts a single-cell write of
 * `0`/`1` that reads back exact (RF-005). Coordinates swept `0..n`
 * inclusive hit the `i >= n` / `j >= n` boundary (EC-04, RF-006); `bit`
 * swept over {0, 1, 2, UINT_MAX} hits the `bit > 1` boundary (EC-05,
 * RF-006); NULL receivers hit EC-09. Every failing call must leave its
 * receiver untouched.
 *
 * @return Failure count, 0 when every cell-access assert holds.
 *
 * @owner No allocation; all receivers are function-local storage.
 * @assumes `qaGridGetBit`/`qaGridWithBit` validate `(i, j)` against `n`,
 *          `bit` against {0, 1}, and NULL receivers before any shift
 *          (spec §1, RF-005, RF-006).
 */
static int runCellAccess(void)
{
    static const unsigned int ns[] = {2, 3, 4};
    static const unsigned int cells[] = {4, 9, 16};
    static const unsigned int bits[] = {0, 1, 2, UINT_MAX};
    int failures = 0;
    char what[160];

    for (unsigned int k = 0; k < 3; ++k) {
        unsigned int n = ns[k];
        unsigned int numCells = cells[k];
        QaGridId limit = (QaGridId)((uint32_t)1u << numCells);
        QaGridId full = (QaGridId)(limit - 1u);

        /* RF-005: every valid cell reads 0 on empty, 1 on full. */
        for (unsigned int i = 0; i < n; ++i) {
            for (unsigned int j = 0; j < n; ++j) {
                unsigned int gotEmpty = 0xDEADu;
                unsigned int gotFull = 0xDEADu;
                snprintf(what, sizeof what,
                         "TEST-001-states-RF005: read empty n=%u(%u,%u)==0",
                         n, i, j);
                failures += checkFailed(qaGridGetBit(0u, n, i, j, &gotEmpty)
                                            == QA_OK && gotEmpty == 0u,
                                        what);
                snprintf(what, sizeof what,
                         "TEST-001-states-RF005: read full n=%u(%u,%u)==1",
                         n, i, j);
                failures += checkFailed(qaGridGetBit(full, n, i, j, &gotFull)
                                            == QA_OK && gotFull == 1u,
                                        what);
            }
        }

        /* EC-04: coordinates swept 0..n inclusive; n itself is rejected. */
        for (unsigned int i = 0; i <= n; ++i) {
            for (unsigned int j = 0; j <= n; ++j) {
                int valid = (i < n) && (j < n);
                unsigned int bit = 42u; /* sentinel: must survive failure */
                QaGridId id = (QaGridId)0xDEADu;
                if (valid) {
                    continue; /* covered by the valid sweep above */
                }
                snprintf(what, sizeof what,
                         "TEST-001-states-RF006: getBit n=%u(%u,%u) RANGE",
                         n, i, j);
                failures += checkFailed(qaGridGetBit(0u, n, i, j, &bit)
                                            == QA_ERR_RANGE,
                                        what);
                snprintf(what, sizeof what,
                         "TEST-001-states-RF006: getBit n=%u(%u,%u) untouched",
                         n, i, j);
                failures += checkFailed(bit == 42u, what);
                snprintf(what, sizeof what,
                         "TEST-001-states-RF006: withBit n=%u(%u,%u) RANGE",
                         n, i, j);
                failures += checkFailed(qaGridWithBit(0u, n, i, j, 0u, &id)
                                            == QA_ERR_RANGE,
                                        what);
                snprintf(what, sizeof what,
                         "TEST-001-states-RF006: withBit n=%u(%u,%u) untouched",
                         n, i, j);
                failures += checkFailed(id == (QaGridId)0xDEADu, what);
            }
        }

        /* RF-005/EC-05: bit swept over {0, 1, 2, UINT_MAX} on cell (0,0). */
        for (unsigned int b = 0; b < 4; ++b) {
            unsigned int bit = bits[b];
            QaGridId id = (QaGridId)0xDEADu;
            if (bit <= 1u) {
                unsigned int back = 0xDEADu;
                snprintf(what, sizeof what,
                         "TEST-001-states-RF005: withBit n=%u bit=%u proceeds",
                         n, bit);
                failures += checkFailed(qaGridWithBit(0u, n, 0, 0, bit, &id)
                                            == QA_OK,
                                        what);
                snprintf(what, sizeof what,
                         "TEST-001-states-RF005: withBit n=%u bit=%u reads back",
                         n, bit);
                failures += checkFailed(qaGridGetBit(id, n, 0, 0, &back)
                                            == QA_OK && back == bit,
                                        what);
            } else {
                snprintf(what, sizeof what,
                         "TEST-001-states-RF006: withBit n=%u bit=%u RANGE",
                         n, bit);
                failures += checkFailed(qaGridWithBit(0u, n, 0, 0, bit, &id)
                                            == QA_ERR_RANGE,
                                        what);
                snprintf(what, sizeof what,
                         "TEST-001-states-RF006: withBit n=%u bit=%u untouched",
                         n, bit);
                failures += checkFailed(id == (QaGridId)0xDEADu, what);
            }
        }

        /* EC-09: NULL receivers, on valid and invalid inputs alike. */
        failures += checkFailed(qaGridGetBit(0u, n, 0, 0, NULL) == QA_ERR_RANGE,
                                "TEST-001-states-RF006: getBit NULL receiver RANGE");
        failures += checkFailed(qaGridWithBit(0u, n, 0, 0, 0u, NULL)
                                    == QA_ERR_RANGE,
                                "TEST-001-states-RF006: withBit NULL receiver RANGE");
        failures += checkFailed(qaGridGetBit(0u, n, n, 0, NULL) == QA_ERR_RANGE,
                                "TEST-001-states-RF006: getBit NULL receiver RANGE on bad (i,j)");
        failures += checkFailed(qaGridWithBit(0u, 5, 0, 0, 0u, NULL)
                                    == QA_ERR_RANGE,
                                "TEST-001-states-RF006: withBit NULL receiver RANGE on n == 5");
    }

    if (failures == 0) {
        printf("001-states cell-access: OK (all (i,j) incl. boundary; bit {0,1,2,UINT_MAX}; NULL receivers; untouched on every failure)\n");
    }
    return failures;
}

/* Fixed test oracle (spec §4): one id with its row-major cell matrix. */
typedef struct QaVectorCase {
    unsigned int n;
    QaGridId id;
    unsigned int cells[16]; /* only the first n * n entries are significant */
} QaVectorCase;

/* Every §4 vector, written out explicitly so the oracle is reviewable. */
static const QaVectorCase vectorCases[] = {
    {2, 9, {1, 0, 0, 1}}, /* |9> = 0b1001 = [1 0; 0 1] */
    {2, 11, {1, 0, 1, 1}}, /* |11> = 0b1011 = [1 0; 1 1] */
    {3, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0}}, /* empty */
    {3, 511, {1, 1, 1, 1, 1, 1, 1, 1, 1}}, /* full */
    {3, 256, {1, 0, 0, 0, 0, 0, 0, 0, 0}}, /* queen only at (0,0) */
    {3, 1, {0, 0, 0, 0, 0, 0, 0, 0, 1}}, /* queen only at (2,2) */
    {4, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, /* empty */
    {4, 65535, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}}, /* full */
    {4, 32768, {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, /* (0,0) */
    {4, 1, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}, /* (3,3) */
    {4, 0x8421, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}, /* diagonal */
};

/**
 * @brief Vectors group T-04 (task T09): RF-004, EC-07.
 *
 * Asserts every spec §4 vector in both directions: `id -> grid` reads each
 * cell with `qaGridGetBit` and compares against the oracle matrix, and
 * `grid -> id` rebuilds the id from empty with `qaGridWithBit` over the
 * set cells and compares the exact value. Each build step also asserts the
 * source id is unchanged (spec §4 "plus immutability").
 *
 * @return Failure count, 0 when every vector holds in both directions.
 *
 * @owner No allocation; all receivers are function-local storage.
 * @assumes `qaGridGetBit`/`qaGridWithBit` implement MSB-first row-major
 *          indexing `pos = i * n + j`, `shift = numCells - 1 - pos`
 *          (spec §1, RF-004).
 */
static int runVectors(void)
{
    int failures = 0;
    char what[160];

    for (unsigned int c = 0; c < sizeof vectorCases / sizeof vectorCases[0];
         ++c) {
        unsigned int n = vectorCases[c].n;
        QaGridId id = vectorCases[c].id;
        unsigned int numCells = n * n;
        QaGridId rebuilt = 0u;

        /* id -> grid direction. */
        for (unsigned int pos = 0; pos < numCells; ++pos) {
            unsigned int i = pos / n;
            unsigned int j = pos % n;
            unsigned int want = vectorCases[c].cells[pos];
            unsigned int got = 0xDEADu;
            snprintf(what, sizeof what,
                     "TEST-001-states-RF004: id->grid id=%u n=%u(%u,%u)==%u",
                     id, n, i, j, want);
            failures += checkFailed(qaGridGetBit(id, n, i, j, &got) == QA_OK
                                        && got == want,
                                    what);
        }

        /* grid -> id direction, built only from the set cells. */
        for (unsigned int pos = 0; pos < numCells; ++pos) {
            unsigned int i = pos / n;
            unsigned int j = pos % n;
            if (vectorCases[c].cells[pos] == 0u) {
                continue;
            }
            QaGridId before = rebuilt;
            QaGridId next = (QaGridId)0xDEADu;
            snprintf(what, sizeof what,
                     "TEST-001-states-RF004: grid->id id=%u n=%u set(%u,%u)",
                     id, n, i, j);
            failures += checkFailed(qaGridWithBit(rebuilt, n, i, j, 1u, &next)
                                        == QA_OK,
                                    what);
            snprintf(what, sizeof what,
                     "TEST-001-states-RF004: grid->id id=%u source kept",
                     id);
            failures += checkFailed(rebuilt == before, what);
            rebuilt = next;
        }
        snprintf(what, sizeof what,
                 "TEST-001-states-RF004: grid->id id=%u n=%u exact", id, n);
        failures += checkFailed(rebuilt == id, what);
    }

    if (failures == 0) {
        printf("001-states vectors: OK (|9>/|11>, 3x3 0/511/256/1, 4x4 0/65535/32768/1/0x8421, both directions)\n");
    }
    return failures;
}

/**
 * @brief Immutability group T-05 (task T10): RF-005, EC-08.
 *
 * Compares the input `id` before and after every `qaGridGetBit` /
 * `qaGridWithBit` call, on success paths (every cell of the empty, full,
 * and one mid board per N, both `bit` values) and on every failure path
 * (bad `n`, out-of-range `(i, j)`, non-canonical `id`, `bit > 1`, NULL
 * receiver). Inputs travel by value, so identity holds by construction;
 * this group asserts it explicitly as EC-08 demands.
 *
 * @return Failure count, 0 when every before/after comparison holds.
 *
 * @owner No allocation; all receivers are function-local storage.
 * @assumes Both helpers take the board by value and never mutate it
 *          (spec §1, D-05).
 */
static int runImmutability(void)
{
    static const unsigned int ns[] = {2, 3, 4};
    static const unsigned int cells[] = {4, 9, 16};
    static const QaGridId mids[] = {9, 256, 0x8421}; /* |9>, (0,0), diagonal */
    static const unsigned int badN[] = {0, 1, 5, 6, UINT_MAX};
    int failures = 0;
    char what[160];

    for (unsigned int k = 0; k < 3; ++k) {
        unsigned int n = ns[k];
        unsigned int numCells = cells[k];
        QaGridId limit = (QaGridId)((uint32_t)1u << numCells);
        QaGridId full = (QaGridId)(limit - 1u);
        QaGridId boards[3] = {0u, full, mids[k]};

        /* Success paths: every cell, both helpers, both bit values. */
        for (unsigned int t = 0; t < 3; ++t) {
            for (unsigned int pos = 0; pos < numCells; ++pos) {
                unsigned int i = pos / n;
                unsigned int j = pos % n;
                unsigned int bit = 0u;
                QaGridId next = (QaGridId)0xDEADu;
                QaGridId id = boards[t];
                snprintf(what, sizeof what,
                         "TEST-001-states-RF005: get keeps id=%u n=%u(%u,%u)",
                         id, n, i, j);
                failures += checkFailed(qaGridGetBit(id, n, i, j, &bit)
                                            == QA_OK && id == boards[t],
                                        what);
                for (unsigned int w = 0; w < 2; ++w) {
                    id = boards[t];
                    snprintf(what, sizeof what,
                             "TEST-001-states-RF005: with keeps id=%u n=%u(%u,%u) bit=%u",
                             id, n, i, j, w);
                    failures += checkFailed(qaGridWithBit(id, n, i, j, w,
                                                          &next)
                                                == QA_OK && id == boards[t],
                                            what);
                }
            }
        }

        /* Failure paths: bad n (EC-01, EC-02). */
        for (unsigned int b = 0; b < 5; ++b) {
            QaGridId id = boards[2];
            unsigned int bit = 42u;
            QaGridId next = (QaGridId)0xDEADu;
            snprintf(what, sizeof what,
                     "TEST-001-states-RF005: get keeps id on n=%u", badN[b]);
            failures += checkFailed(qaGridGetBit(id, badN[b], 0, 0, &bit)
                                        != QA_OK && id == boards[2],
                                    what);
            snprintf(what, sizeof what,
                     "TEST-001-states-RF005: with keeps id on n=%u", badN[b]);
            failures += checkFailed(qaGridWithBit(id, badN[b], 0, 0, 0u,
                                                  &next)
                                        != QA_OK && id == boards[2],
                                    what);
        }

        /* Failure paths: bad (i,j), bad id, bad bit, NULL (EC-04/06/05/09). */
        {
            QaGridId id = boards[2];
            unsigned int bit = 42u;
            QaGridId next = (QaGridId)0xDEADu;
            failures += checkFailed(qaGridGetBit(id, n, n, 0, &bit)
                                        == QA_ERR_RANGE && id == boards[2],
                                    "TEST-001-states-RF005: get keeps id on i >= n");
            failures += checkFailed(qaGridWithBit(id, n, 0, n, 0u, &next)
                                        == QA_ERR_RANGE && id == boards[2],
                                    "TEST-001-states-RF005: with keeps id on j >= n");
            id = limit;
            failures += checkFailed(qaGridGetBit(id, n, 0, 0, &bit)
                                        == QA_ERR_RANGE && id == limit,
                                    "TEST-001-states-RF005: get keeps id on one-past-max");
            id = 0xFFFFFFFFu;
            failures += checkFailed(qaGridWithBit(id, n, 0, 0, 0u, &next)
                                        == QA_ERR_RANGE && id == 0xFFFFFFFFu,
                                    "TEST-001-states-RF005: with keeps id on high bits");
            id = boards[2];
            failures += checkFailed(qaGridWithBit(id, n, 0, 0, 2u, &next)
                                        == QA_ERR_RANGE && id == boards[2],
                                    "TEST-001-states-RF005: with keeps id on bit > 1");
            failures += checkFailed(qaGridGetBit(id, n, 0, 0, NULL)
                                        == QA_ERR_RANGE && id == boards[2],
                                    "TEST-001-states-RF005: get keeps id on NULL receiver");
            failures += checkFailed(qaGridWithBit(id, n, 0, 0, 0u, NULL)
                                        == QA_ERR_RANGE && id == boards[2],
                                    "TEST-001-states-RF005: with keeps id on NULL receiver");
        }
    }

    if (failures == 0) {
        printf("001-states immutability: OK (input bit-identical after every get/with, success and failure)\n");
    }
    return failures;
}

/* Runs one named group; returns -1 for an unknown name. */
static int runGroup(const char *name)
{
    if (strcmp(name, "n-gate") == 0) {
        return runNGate();
    }
    if (strcmp(name, "id-range") == 0) {
        return runIdRange();
    }
    if (strcmp(name, "cell-access") == 0) {
        return runCellAccess();
    }
    if (strcmp(name, "vectors") == 0) {
        return runVectors();
    }
    if (strcmp(name, "immutability") == 0) {
        return runImmutability();
    }
    return -1;
}

/* Lists all behavior groups. */
static void printUsage(const char *program)
{
    printf("usage: %s [n-gate|id-range|cell-access|vectors|immutability]\n",
           program);
}

int main(int argc, char *argv[])
{
    int failures = 0;

    printf("001-states grid: %s, build=%s\n", __clang_version__, QA_BUILD_TYPE);

    if (argc == 1) {
        failures += runNGate();
        failures += runIdRange();
        failures += runCellAccess();
        failures += runVectors();
        failures += runImmutability();
    } else if (argc == 2) {
        int groupFailures = runGroup(argv[1]);
        if (groupFailures < 0) {
            printUsage(argv[0]);
            return 2;
        }
        failures += groupFailures;
    } else {
        printUsage(argv[0]);
        return 2;
    }

    if (failures != 0) {
        printf("001-states grid: FAILED (%d checks)\n", failures);
        return 1;
    }
    printf("001-states grid: OK\n");
    return 0;
}
