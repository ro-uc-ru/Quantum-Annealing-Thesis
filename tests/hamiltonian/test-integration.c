/*
 * 001<->002 integration test (spec 002-hamiltonian, tasks T-011/T-012,
 * FR-012).
 *
 * Purpose: prove the implemented 001-states grid helpers compose with the
 * `H_problem` apply. For every EC-018 board (N in {2, 3, 4}: empty, full,
 * and the 4x4 solution 16770) the test builds the id cell by cell with the
 * 001 `qaGridWithBit` helper, requires the built id to equal the expected
 * literal, sets the basis input `phi[id] = 1` with zeros elsewhere, calls
 * apply, and requires `QA_OK`, `outPsi[id]` equal to the §7 oracle
 * bit-exact, zeros elsewhere (`+0.0` vs `-0.0` count as equal), and `phi`
 * unchanged. The `build-failure` mode covers EC-019: a `withBit` call with
 * an invalid bit fails the board build, the failing step is reported, the
 * receiver is untouched, and apply is provably never called on the
 * partially built id (every apply in this binary runs through the counted
 * wrapper below). The `--record <csv>` mode covers EC-020: the run first
 * requires the FR-011 record at `<csv>` to authorize its exact parameter
 * set (same `N`, `dim`, vectors; `seed`/`schedule`/`dt_steps` `N/A`) and
 * only then executes the EC-018 cases, so the run is reproducible from
 * `results/002-config.csv`.
 *
 * Ownership: per-case heap buffers (`phi`, `phiBefore`, `outPsi`) owned by
 * the case runner and freed on every path via `cleanup`. Errors: any
 * failed check is printed with its FR tag and the process exits non-zero.
 * Numerical assumptions: accepted `n` keeps `numCells` and `dim` exact;
 * basis outputs scale exact small-integer energies without rounding, so
 * oracle comparisons are bit-exact.
 * Determinism: fixed boards and fixed amplitudes only; no clock, no RNG,
 * no environment or filesystem access (constitution §13).
 */

#include <complex.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/grid.h"
#include "qa/core/status.h"
#include "qa/hamiltonian/problem.h"

/* One EC-018 board: queen cells as a row-major mask (`pos = i * n + j`),
 * the expected literal id, and the §7 oracle energy. */
typedef struct QaIntegrationCase {
    unsigned int n;
    size_t dim;
    unsigned int queenMask;
    QaGridId wantId;
    double wantEnergy;
    const char *name;
} QaIntegrationCase;

static const QaIntegrationCase qaIntegrationCases[] = {
    {2, 16, 0x0, 0, 0.0, "n=2 empty"},
    {2, 16, 0xF, 15, 6.0, "n=2 full"},
    {3, 512, 0x0, 0, 0.0, "n=3 empty"},
    {3, 512, 0x1FF, 511, 28.0, "n=3 full"},
    {4, 65536, 0x0, 0, 0.0, "n=4 empty"},
    {4, 65536, 0xFFFF, 65535, 76.0, "n=4 full"},
    /* Solution queens at (0,1),(1,3),(2,0),(3,2): pos bits {1,7,8,14}. */
    {4, 65536, 0x4182, 16770, 0.0, "n=4 solution 16770"},
};

/* Counts every `H_problem` apply call in this binary, so the EC-019 probe
 * can prove apply is never reached on a failed board build. */
static unsigned int qaApplyCalls = 0;

/**
 * @brief Counted wrapper around `qaHamiltonianApplyProblem`.
 *
 * Every apply in this binary runs through this wrapper; the EC-019 probe
 * snapshots the counter around the failed build to prove the no-apply
 * path (FR-012).
 *
 * @param[in]  n      Board edge, gated by the library `N`-gate.
 * @param[in]  phi    Immutable normalized input state of length `dim`.
 * @param[out] outPsi Separate mutable caller-owned receiver of length `dim`.
 * @param[in]  dim    Expected state length, `2^(n*n)`.
 *
 * @return Whatever `qaHamiltonianApplyProblem` returns; `phi` immutable and
 *         `outPsi` written only on success per the library contract.
 *
 * @owner Allocates nothing; both buffers stay caller-owned on every path.
 * @assumes Same numerical assumptions as `qaHamiltonianApplyProblem`.
 */
static QaStatus callApply(unsigned int n, const complex double *phi, complex double *outPsi, size_t dim)
{
    ++qaApplyCalls;
    return qaHamiltonianApplyProblem(n, phi, outPsi, dim);
}

/**
 * @brief Run one EC-018 case through the fixed FR-012 observable sequence.
 *
 * Builds the board id with `qaGridWithBit` over every cell (a helper
 * failure fails the case with the step reported and no apply call),
 * requires the built id to equal the expected literal, then applies
 * `H_problem` to the matching basis input and requires the §7 oracle
 * bit-exact, zeros elsewhere, and `phi` unchanged.
 *
 * @param[in]  kase  Case descriptor (N, dim, queen mask, id, energy).
 *
 * @return Failure count, 0 when the full sequence holds.
 *
 * @owner Owns three per-case heap buffers, all freed at `cleanup` on every
 *        path; the caller owns `kase`.
 * @assumes `kase` is one of the table entries above, so `dim` is the gated
 *          `2^(n*n)` and the queen mask fits the board.
 */
static int runCase(const QaIntegrationCase *kase)
{
    int failures = 0;
    char what[192];
    QaGridId id = 0;
    unsigned int numCells = kase -> n * kase -> n;
    size_t expectedDim = 0;
    complex double *phi = NULL;
    complex double *phiBefore = NULL;
    complex double *outPsi = NULL;

    if (numCells >= sizeof (size_t) * (size_t)CHAR_BIT) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s width overflow", kase -> name);
        printf("FAIL: %s\n", what);
        return 1;
    }
    expectedDim = (size_t)1 << numCells;
    if (expectedDim != kase -> dim) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s dim mismatch", kase -> name);
        printf("FAIL: %s\n", what);
        return 1;
    }
    if ((size_t)kase -> wantId >= kase -> dim) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s id out of range", kase -> name);
        printf("FAIL: %s\n", what);
        return 1;
    }
    if (kase -> dim > SIZE_MAX / sizeof *phi) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s state too large", kase -> name);
        printf("FAIL: %s\n", what);
        return 1;
    }

    /* Build the id with the 001 write helper, every cell, in row-major
     * order; any failure reports the step and skips apply entirely. */
    for (unsigned int i = 0; i < kase -> n; ++i) {
        for (unsigned int j = 0; j < kase -> n; ++j) {
            unsigned int pos = i * kase -> n + j;
            unsigned int bit = (kase -> queenMask >> pos) & 1u;
            QaGridId next = 0;
            QaStatus build = qaGridWithBit(id, kase -> n, i, j, bit, &next);
            if (build != QA_OK) {
                snprintf(what, sizeof what,
                         "TEST-002-hamiltonian-FR012: %s build fails at (%u,%u)",
                         kase -> name, i, j);
                printf("FAIL: %s\n", what);
                ++failures;
                goto cleanup;
            }
            id = next;
        }
    }

    if (id != kase -> wantId) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s built id %u != %u",
                 kase -> name, (unsigned int)id, (unsigned int)kase -> wantId);
        printf("FAIL: %s\n", what);
        ++failures;
        goto cleanup;
    }

    phi = calloc(kase -> dim, sizeof *phi);
    phiBefore = calloc(kase -> dim, sizeof *phiBefore);
    outPsi = calloc(kase -> dim, sizeof *outPsi);
    if (phi == NULL || phiBefore == NULL || outPsi == NULL) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s no memory", kase -> name);
        printf("FAIL: %s\n", what);
        ++failures;
        goto cleanup;
    }

    phi[id] = 1.0 + 0.0 * I;
    memcpy(phiBefore, phi, kase -> dim * sizeof *phi);
    for (size_t k = 0; k < kase -> dim; ++k) {
        outPsi[k] = 9.0 + 9.0 * I;
    }

    if (callApply(kase -> n, phi, outPsi, kase -> dim) != QA_OK) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s apply yields QA_OK", kase -> name);
        printf("FAIL: %s\n", what);
        ++failures;
        goto cleanup;
    }
    if (creal(outPsi[id]) != kase -> wantEnergy || cimag(outPsi[id]) != 0.0) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s E(%u) == %g bit-exact",
                 kase -> name, (unsigned int)id, kase -> wantEnergy);
        printf("FAIL: %s\n", what);
        ++failures;
    }
    for (size_t k = 0; k < kase -> dim; ++k) {
        if (k != id && (creal(outPsi[k]) != 0.0 || cimag(outPsi[k]) != 0.0)) {
            snprintf(what, sizeof what,
                     "TEST-002-hamiltonian-FR012: %s zeroes elsewhere", kase -> name);
            printf("FAIL: %s\n", what);
            ++failures;
            break;
        }
    }
    if (memcmp(phi, phiBefore, kase -> dim * sizeof *phi) != 0) {
        snprintf(what, sizeof what,
                 "TEST-002-hamiltonian-FR012: %s keeps phi", kase -> name);
        printf("FAIL: %s\n", what);
        ++failures;
    }

cleanup:
    free(outPsi);
    free(phiBefore);
    free(phi);
    if (failures == 0) {
        printf("002-hamiltonian integration: OK (%s, id=%u, E=%g)\n",
               kase -> name, (unsigned int)kase -> wantId, kase -> wantEnergy);
    }
    return failures;
}

/**
 * @brief EC-019 probe (task T-012): a failed helper build never reaches
 * apply (FR-012).
 *
 * Builds an N=2 board up to a `withBit` call with the invalid bit `2` at
 * cell (0,1): the helper must reject it with `QA_ERR_RANGE`, the receiver
 * must stay bit-identical to its sentinel (the 001 untouched-on-failure
 * contract), the case is treated as failed with the step reported, and
 * the apply counter must prove no apply ran on the partially built id.
 *
 * @return Failure count, 0 when the no-apply path holds exactly.
 *
 * @owner No allocation; nothing to release.
 * @assumes `bit > 1` is rejected with `QA_ERR_RANGE` (001 header step 6)
 *          and the receiver is left untouched on failure.
 */
static int runBuildFailure(void)
{
    int failures = 0;
    QaGridId id = 0;
    QaGridId next = 0;
    QaStatus build;
    unsigned int callsBefore = qaApplyCalls;

    build = qaGridWithBit(id, 2, 0, 0, 1, &next);
    if (build != QA_OK) {
        printf("FAIL: TEST-002-hamiltonian-FR012: build-failure setup fails at (0,0)\n");
        return 1;
    }
    id = next;

    next = 0xDEADBEEFu;
    build = qaGridWithBit(id, 2, 0, 1, 2u, &next);
    if (build == QA_OK) {
        printf("FAIL: TEST-002-hamiltonian-FR012: invalid bit=2 unexpectedly succeeds at (0,1)\n");
        return 1;
    }
    printf("002-hamiltonian integration-build-failure: expected build failure at (0,1) bit=2 -> status %d\n",
           (int)build);
    if (build != QA_ERR_RANGE) {
        printf("FAIL: TEST-002-hamiltonian-FR012: invalid bit yields RANGE\n");
        ++failures;
    }
    if (next != 0xDEADBEEFu) {
        printf("FAIL: TEST-002-hamiltonian-FR012: failed build keeps receiver\n");
        ++failures;
    }
    if (qaApplyCalls != callsBefore) {
        printf("FAIL: TEST-002-hamiltonian-FR012: failed build never calls apply\n");
        ++failures;
    }

    if (failures == 0) {
        printf("002-hamiltonian integration-build-failure: OK (RANGE at (0,1); receiver kept; apply never called)\n");
    }
    return failures;
}

/* Expected FR-011 header and data-row markers of `results/002-config.csv`. */
#define QA_RECORD_HEADER "spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed,schedule,dt_steps"
#define QA_RECORD_VERSION "v1,"
#define QA_RECORD_NA_TAIL ",N/A,N/A,N/A"

/* Largest record this probe accepts: the CSV is two short lines, so
 * anything bigger is not the FR-011 record. Bounds the single read. */
#define QA_RECORD_MAX_BYTES 65536u

/**
 * @brief Read a whole file into a NUL-terminated heap buffer (EC-020
 * helper).
 *
 * @param[in]  path    File to read (the FR-011 CSV).
 * @param[out] outText Non-NULL receiver of the buffer (NUL-terminated).
 * @param[out] outSize Non-NULL receiver of the byte count (without NUL).
 *
 * @return `QA_OK` (receivers written); `QA_ERR_RANGE` (NULL receiver,
 *         empty file, or file larger than `QA_RECORD_MAX_BYTES`);
 *         `QA_ERR_NOMEM` (allocation failed); `QA_ERR_IO` (open, seek,
 *         read, or close failed, or a short read). On failure both
 *         receivers are untouched except `*outText`, which is poisoned to
 *         NULL on entry per codestyle §8.
 *
 * @owner The buffer is owned by the caller on success and released with
 *        `free`; nothing is allocated on failure.
 * @assumes `long` holds the file size (`ftell` succeeds on a regular
 *          file); the record is far below the bound, so no truncation.
 */
static QaStatus readRecordFile(const char *path, char **outText, size_t *outSize)
{
    FILE *handle = NULL;
    long fileSize = 0;
    char *text = NULL;

    if (outText == NULL || outSize == NULL) {
        return QA_ERR_RANGE;
    }
    *outText = NULL;

    if (path == NULL) {
        return QA_ERR_RANGE;
    }
    handle = fopen(path, "r");
    if (handle == NULL) {
        return QA_ERR_IO;
    }
    if (fseek(handle, 0, SEEK_END) != 0) {
        fclose(handle);
        return QA_ERR_IO;
    }
    fileSize = ftell(handle);
    if (fileSize <= 0 || (unsigned long)fileSize > QA_RECORD_MAX_BYTES) {
        fclose(handle);
        return QA_ERR_RANGE;
    }
    if (fseek(handle, 0, SEEK_SET) != 0) {
        fclose(handle);
        return QA_ERR_IO;
    }
    text = malloc((size_t)fileSize + 1u);
    if (text == NULL) {
        fclose(handle);
        return QA_ERR_NOMEM;
    }
    if (fread(text, 1, (size_t)fileSize, handle) != (size_t)fileSize) {
        free(text);
        fclose(handle);
        return QA_ERR_IO;
    }
    if (fclose(handle) != 0) {
        free(text);
        return QA_ERR_IO;
    }
    text[fileSize] = '\0';
    *outText = text;
    *outSize = (size_t)fileSize;
    return QA_OK;
}

/**
 * @brief Parse unsigned values from a separator-delimited list (EC-020
 * helper).
 *
 * Splits `text` on any byte in `seps`, requires at least one value and at
 * most `cap`, and requires every token to be a fully consumed decimal
 * number (no signs, no spaces, no empty tokens).
 *
 * @param[in]  text  List text (one CSV cell without its quotes).
 * @param[in]  seps  Separator bytes (e.g. `","` or `";,"`).
 * @param[out] out   Non-NULL receiver of the parsed values.
 * @param[in]  cap   Capacity of `out`.
 * @param[out] outCount Non-NULL receiver of the value count.
 *
 * @return `QA_OK` (receivers written); `QA_ERR_RANGE` (NULL receiver,
 *         empty list, overflow of `cap`, or a malformed token).
 *
 * @owner No allocation; `out` and `outCount` stay caller-owned.
 * @assumes Values fit `unsigned long long` (record fields are tiny).
 */
static QaStatus parseUintList(const char *text, const char *seps, unsigned long long *out,
                              size_t cap, size_t *outCount)
{
    size_t count = 0;
    const char *cursor = NULL;

    if (text == NULL || seps == NULL || out == NULL || outCount == NULL) {
        return QA_ERR_RANGE;
    }
    *outCount = 0;
    if (*text == '\0') {
        return QA_ERR_RANGE;
    }

    cursor = text;
    while (*cursor != '\0') {
        char *end = NULL;
        unsigned long long value = 0;

        if (count >= cap) {
            return QA_ERR_RANGE;
        }
        errno = 0;
        value = strtoull(cursor, &end, 10);
        if (errno != 0 || end == cursor || (*end != '\0' && strchr(seps, *end) == NULL)) {
            return QA_ERR_RANGE;
        }
        out[count] = value;
        ++count;
        cursor = (*end == '\0') ? end : end + 1;
        if (*cursor == '\0') {
            break;
        }
        if (strchr(seps, cursor[-1]) == NULL) {
            return QA_ERR_RANGE;
        }
    }
    if (count == 0) {
        return QA_ERR_RANGE;
    }
    *outCount = count;
    return QA_OK;
}

/**
 * @brief Require the FR-011 record to authorize one EC-018 case (EC-020
 * helper).
 *
 * The case runs only if the record lists its `N` in the `N` cell, its
 * `dim` in the `dim` cell, and its board id among the `N`-grouped vector
 * ids (`N:id,...;...`, so the group prefixes can never mask a missing
 * id).
 *
 * @param[in]  kase      Case descriptor from the EC-018 table.
 * @param[in]  nValues   Parsed record `N` cell.
 * @param[in]  nCount    Length of `nValues`.
 * @param[in]  dimValues Parsed record `dim` cell.
 * @param[in]  dimCount  Length of `dimValues`.
 * @param[in]  vectors   Record `vectors` cell (with quotes stripped).
 *
 * @return Failure count, 0 when the record covers the case.
 *
 * @owner No allocation; nothing to release.
 * @assumes `vectors` groups look like `N:id,...` separated by `;`.
 */
static int requireRecordCovers(const QaIntegrationCase *kase, const unsigned long long *nValues,
                               size_t nCount, const unsigned long long *dimValues,
                               size_t dimCount, const char *vectors)
{
    int covered = 0;
    const char *group = vectors;

    for (size_t k = 0; k < nCount; ++k) {
        if (nValues[k] == kase -> n) {
            covered = 1;
        }
    }
    if (!covered) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record N misses %s\n", kase -> name);
        return 1;
    }
    covered = 0;
    for (size_t k = 0; k < dimCount; ++k) {
        if (dimValues[k] == kase -> dim) {
            covered = 1;
        }
    }
    if (!covered) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record dim misses %s\n", kase -> name);
        return 1;
    }

    while (*group != '\0') {
        const char *colon = strchr(group, ':');
        const char *end = strchr(group, ';');
        char ids[256];
        size_t len = 0;
        unsigned long long idValues[64];
        size_t idCount = 0;

        if (end == NULL) {
            end = group + strlen(group);
        }
        if (colon == NULL || colon > end) {
            printf("FAIL: TEST-002-hamiltonian-FR011: record vectors malformed\n");
            return 1;
        }
        len = (size_t)(end - (colon + 1));
        if (len >= sizeof ids) {
            printf("FAIL: TEST-002-hamiltonian-FR011: record vectors group too long\n");
            return 1;
        }
        memcpy(ids, colon + 1, len);
        ids[len] = '\0';
        if (parseUintList(ids, ",", idValues, sizeof idValues / sizeof *idValues, &idCount) != QA_OK) {
            printf("FAIL: TEST-002-hamiltonian-FR011: record vectors malformed\n");
            return 1;
        }
        for (size_t k = 0; k < idCount; ++k) {
            if (idValues[k] == (unsigned long long)kase -> wantId) {
                return 0;
            }
        }
        group = (*end == '\0') ? end : end + 1;
    }
    printf("FAIL: TEST-002-hamiltonian-FR011: record vectors miss id %u (%s)\n",
           (unsigned int)kase -> wantId, kase -> name);
    return 1;
}

/**
 * @brief EC-020 check (task T-013): require the run to sit under the
 * FR-011 record (FR-011, FR-012).
 *
 * Reads the CSV at `path`, requires the exact FR-011 header, the `v1`
 * version, the `N/A` tail, ten cells, and requires the record to cover
 * every EC-018 case of the table (same `N`, `dim`, vectors). Any
 * violation fails before any physics runs.
 *
 * @param[in]  path  Path to `results/002-config.csv`.
 *
 * @return Failure count, 0 when the record authorizes the full run.
 *
 * @owner Owns one heap file buffer, freed on every path via `cleanup`.
 * @assumes The record has the T-009 shape: header line, one `v1` data
 *          row, `;`-grouped vectors, `N/A` seed/schedule/`dt_steps`.
 */
static int runRecordCheck(const char *path)
{
    int failures = 0;
    char *text = NULL;
    size_t textSize = 0;
    QaStatus status = QA_OK;
    char *newline = NULL;
    char *row = NULL;
    char *rowEnd = NULL;
    char *cells[10];
    size_t cellCount = 0;
    char *cursor = NULL;
    int inQuotes = 0;
    unsigned long long nValues[8];
    unsigned long long dimValues[8];
    size_t nCount = 0;
    size_t dimCount = 0;

    status = readRecordFile(path, &text, &textSize);
    if (status != QA_OK) {
        printf("FAIL: TEST-002-hamiltonian-FR011: cannot read record %s (status %d)\n",
               (path == NULL) ? "(null)" : path, (int)status);
        return 1;
    }

    newline = strchr(text, '\n');
    if (newline == NULL) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record has no header line\n");
        failures = 1;
        goto cleanup;
    }
    *newline = '\0';
    if (strcmp(text, QA_RECORD_HEADER) != 0) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record header mismatch\n");
        failures = 1;
        goto cleanup;
    }

    row = newline + 1;
    rowEnd = strchr(row, '\n');
    if (rowEnd == NULL) {
        rowEnd = row + strlen(row);
        if (*row == '\0') {
            printf("FAIL: TEST-002-hamiltonian-FR011: record has no v1 row\n");
            failures = 1;
            goto cleanup;
        }
    } else if (*(rowEnd + 1) != '\0') {
        printf("FAIL: TEST-002-hamiltonian-FR011: record has extra rows\n");
        failures = 1;
        goto cleanup;
    }
    *rowEnd = '\0';
    if (strncmp(row, QA_RECORD_VERSION, strlen(QA_RECORD_VERSION)) != 0) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record version is not v1\n");
        failures = 1;
        goto cleanup;
    }
    {
        size_t rowLen = strlen(row);
        size_t tailLen = strlen(QA_RECORD_NA_TAIL);
        if (rowLen < tailLen ||
            strcmp(row + rowLen - tailLen, QA_RECORD_NA_TAIL) != 0) {
            printf("FAIL: TEST-002-hamiltonian-FR011: seed/schedule/dt_steps are not N/A\n");
            failures = 1;
            goto cleanup;
        }
    }

    /* Split the row on commas outside quotes; strip one quote pair per
     * cell and unescape `""`. */
    cursor = row;
    cells[0] = cursor;
    cellCount = 1;
    inQuotes = 0;
    while (*cursor != '\0') {
        if (*cursor == '"') {
            inQuotes = !inQuotes;
        } else if (*cursor == ',' && !inQuotes) {
            *cursor = '\0';
            if (cellCount >= sizeof cells / sizeof *cells) {
                printf("FAIL: TEST-002-hamiltonian-FR011: record has extra cells\n");
                failures = 1;
                goto cleanup;
            }
            cells[cellCount] = cursor + 1;
            ++cellCount;
        }
        ++cursor;
    }
    if (inQuotes) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record has unbalanced quotes\n");
        failures = 1;
        goto cleanup;
    }
    if (cellCount != sizeof cells / sizeof *cells) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record has %zu cells, want 10\n", cellCount);
        failures = 1;
        goto cleanup;
    }
    for (size_t k = 0; k < cellCount; ++k) {
        size_t len = strlen(cells[k]);
        if (len >= 2 && cells[k][0] == '"' && cells[k][len - 1] == '"') {
            char *read = cells[k] + 1;
            char *write = cells[k];
            while (read < cells[k] + len - 1) {
                if (read[0] == '"' && read[1] == '"') {
                    ++read;
                }
                *write = *read;
                ++write;
                ++read;
            }
            *write = '\0';
        }
    }

    if (strcmp(cells[0], "v1") != 0) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record version is not v1\n");
        failures = 1;
        goto cleanup;
    }
    if (strcmp(cells[7], "N/A") != 0 || strcmp(cells[8], "N/A") != 0 ||
        strcmp(cells[9], "N/A") != 0) {
        printf("FAIL: TEST-002-hamiltonian-FR011: seed/schedule/dt_steps are not N/A\n");
        failures = 1;
        goto cleanup;
    }
    if (parseUintList(cells[1], ",", nValues, sizeof nValues / sizeof *nValues, &nCount) != QA_OK) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record N malformed\n");
        failures = 1;
        goto cleanup;
    }
    if (parseUintList(cells[2], ",", dimValues, sizeof dimValues / sizeof *dimValues,
                      &dimCount) != QA_OK) {
        printf("FAIL: TEST-002-hamiltonian-FR011: record dim malformed\n");
        failures = 1;
        goto cleanup;
    }
    for (size_t c = 0; c < sizeof qaIntegrationCases / sizeof *qaIntegrationCases; ++c) {
        failures += requireRecordCovers(&qaIntegrationCases[c], nValues, nCount, dimValues,
                                        dimCount, cells[3]);
    }
    if (failures == 0) {
        printf("002-hamiltonian integration: OK (record v1 authorizes N, dim, vectors; seed/schedule/dt_steps N/A)\n");
    }

cleanup:
    free(text);
    return failures;
}

int main(int argc, char *argv[])
{
    int failures = 0;

    printf("002-hamiltonian integration: %s, build=%s\n", __clang_version__, QA_BUILD_TYPE);

    if (argc == 1) {
        for (size_t c = 0; c < sizeof qaIntegrationCases / sizeof *qaIntegrationCases; ++c) {
            failures += runCase(&qaIntegrationCases[c]);
        }
    } else if (argc == 2 && strcmp(argv[1], "build-failure") == 0) {
        failures += runBuildFailure();
    } else if (argc == 3 && strcmp(argv[1], "--record") == 0) {
        failures += runRecordCheck(argv[2]);
        if (failures == 0) {
            for (size_t c = 0; c < sizeof qaIntegrationCases / sizeof *qaIntegrationCases; ++c) {
                failures += runCase(&qaIntegrationCases[c]);
            }
        }
    } else {
        printf("usage: %s [build-failure|--record <csv>]\n", argv[0]);
        return 2;
    }

    if (failures != 0) {
        printf("002-hamiltonian integration: FAILED (%d checks)\n", failures);
        return 1;
    }
    if (argc == 1) {
        printf("002-hamiltonian integration: OK (7 EC-018 boards via 001 helpers)\n");
    } else if (argc == 2) {
        printf("002-hamiltonian integration: OK (EC-019 no-apply path)\n");
    } else {
        printf("002-hamiltonian integration: OK (EC-020 run under FR-011 record)\n");
    }
    return 0;
}
