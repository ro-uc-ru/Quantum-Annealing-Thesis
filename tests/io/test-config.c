/*
 * 004-driver CSV writer tests, Phase 5 (task T-010: `contract`, `range` and
 * `escape`; task T-011: `io-failure`).
 *
 * Purpose: assert FR-014 and FR-015 (exact header and row, replace-on-success,
 * argument and bound validation) and EC-024, EC-027, EC-028, EC-029, group by
 * group, each selectable as `test-004-driver-io <group>` so every tasks.md
 * `ctest -R 004-driver-io-<group>` pattern matches exactly one CTest entry.
 * Failing checks print the `TEST-004-driver-FR0XX` / `TEST-004-driver-EC0XX`
 * identifier required by the plan. No arguments runs every group.
 *
 * Independence: the expected bytes are literals written in this file and the
 * read-back goes through `parseCsv`, an RFC 4180 parser that shares no code
 * with the writer.
 *
 * Ownership: static buffers only, nothing to release; scratch files are
 * created in the working directory (the build tree under CTest) and removed
 * by each group. The path-bound test uses fake operations so it does not
 * depend on the platform `PATH_MAX`. Errors: any failed check is printed and
 * the process exits non-zero; an unknown group exits 2 with usage.
 * Determinism: fixed inputs only, no clock, RNG or environment access.
 * Numerical assumptions: none; every comparison is on bytes or lengths, never
 * on floating point values. Sizes are compared against the library limits
 * `QA_IO_CONFIG_FIELD_MAX` (4096) and `QA_IO_CONFIG_PATH_MAX` (1024).
 */

#define _POSIX_C_SOURCE 200809L /* mkdir, chmod, access, rmdir */

#include "qa/io/config.h"

#include "config-internal.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Capacity of the file read-back and parser storage buffers: ten fields of
 * 4096 bytes, fully quote-doubled and framed, twice, with margin. */
#define BUF_CAP (256u * 1024u)
#define MAX_RECORDS 4u
#define NUM_COLS 10u

/* Expected FR-014 header line, newline included. */
#define EXPECTED_HEADER \
    "spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed," \
    "schedule,dt_steps\n"

static char gFile[BUF_CAP];
static char gStorage[BUF_CAP];
static const char *gCells[MAX_RECORDS][NUM_COLS];
static char gLongField[QA_IO_CONFIG_FIELD_MAX + 2u];
static char gLongPath[QA_IO_CONFIG_PATH_MAX + 2u];

/**
 * @brief Record one check; the run stays allocation-free.
 *
 * @param[in] condition Non-zero when the check passed.
 * @param[in] what Description printed on failure (valid string).
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
 * @brief Read a whole file into `gFile` and NUL-terminate it.
 *
 * @param[in]  path   File to read.
 * @param[out] outLen Receives the byte count on success.
 *
 * @return 1 on success (a longer file is truncated to `BUF_CAP - 1` bytes,
 *         which the callers' exact comparisons then reject), 0 when the file
 *         cannot be opened.
 *
 * @owner The stream is opened and closed inside the call.
 * @assumes The content fits `BUF_CAP - 1` bytes, checked by the read bound.
 */
static int readFile(const char *path, size_t *outLen)
{
    FILE *f = fopen(path, "rb");
    size_t len;

    if (f == NULL) {
        return 0;
    }
    /* Bound: `fread` reads at most `BUF_CAP - 1` bytes, so `len <= BUF_CAP - 1`
     * and `gFile[len]` is inside `gFile`. */
    len = fread(gFile, 1, BUF_CAP - 1u, f);
    (void)fclose(f);
    gFile[len] = '\0';
    *outLen = len;
    return 1;
}

/**
 * @brief Report whether `path` can be opened for reading.
 *
 * @param[in] path File name to probe (valid string).
 * @return 1 when `fopen(path, "rb")` succeeds, else 0.
 *
 * @owner The probe stream is closed before returning.
 * @assumes No numerical assumptions.
 */
static int fileExists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    (void)fclose(f);
    return 1;
}

/**
 * @brief Create or overwrite `path` with `content` (test fixture).
 *
 * @param[in] path File to create (valid string).
 * @param[in] content Bytes to write, NUL-terminated.
 * @return 1 on success, 0 on any I/O failure.
 *
 * @owner The stream is closed on every path; the file stays with the caller.
 * @assumes `content` is shorter than the file system limits; no numerics.
 */
static int writeFixture(const char *path, const char *content)
{
    FILE *f = fopen(path, "wb");
    size_t len = strlen(content);
    int ok;

    if (f == NULL) {
        return 0;
    }
    ok = fwrite(content, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

/**
 * @brief Independent RFC 4180 parser for the read-back checks.
 *
 * Splits `buf` into records of exactly `NUM_COLS` cells ended by `\n`. A cell
 * starting with a double quote runs to the closing quote, `""` meaning one
 * literal quote, and may contain commas, CR and LF. Cell text is copied into
 * `gStorage` and referenced from `gCells`.
 *
 * @param[in]  buf        NUL-terminated input.
 * @param[out] outRecords Receives the record count on success.
 *
 * @return 1 when `buf` is well formed (every record has `NUM_COLS` cells, at
 *         most `MAX_RECORDS` records, storage not exhausted), 0 otherwise.
 *
 * @owner Writes only the static `gStorage` and `gCells`.
 * @assumes `buf` is shorter than `BUF_CAP`; every cell copy is bounded by the
 *          storage check before the write.
 */
static int parseCsv(const char *buf, size_t *outRecords)
{
    size_t used = 0;
    size_t rec = 0;
    size_t col = 0;
    const char *p = buf;

    while (*p != '\0') {
        const char *cellStart = &gStorage[used];

        if (rec >= MAX_RECORDS || col >= NUM_COLS) {
            return 0;
        }
        if (*p == '"') {
            p++;
            for (;;) {
                if (*p == '\0') {
                    return 0; /* unterminated quoted cell */
                }
                if (*p == '"') {
                    /* Read bound: `*p == '"'` here, so `p[1]` is at worst the
                     * terminating NUL. */
                    if (p[1] == '"') {
                        /* Bound: `used + 1 < BUF_CAP` keeps this one-byte write
                         * inside `gStorage`. */
                        if (used + 1u >= BUF_CAP) {
                            return 0;
                        }
                        gStorage[used++] = '"';
                        p += 2;
                        continue;
                    }
                    p++;
                    break;
                }
                /* Bound: same `used + 1 < BUF_CAP` guard as above. */
                if (used + 1u >= BUF_CAP) {
                    return 0;
                }
                gStorage[used++] = *p++;
            }
        } else {
            while (*p != '\0' && *p != ',' && *p != '\n') {
                if (*p == '"' || *p == '\r') {
                    return 0; /* must have been quoted */
                }
                /* Bound: same `used + 1 < BUF_CAP` guard as above. */
                if (used + 1u >= BUF_CAP) {
                    return 0;
                }
                gStorage[used++] = *p++;
            }
        }
        /* Write bounds: `used + 1 < BUF_CAP` leaves room for the NUL in
         * `gStorage`; `rec < MAX_RECORDS` and `col < NUM_COLS` were checked
         * at the top of this iteration, so `gCells[rec][col]` is in range. */
        if (used + 1u >= BUF_CAP) {
            return 0;
        }
        gStorage[used++] = '\0';
        gCells[rec][col++] = cellStart;

        if (*p == ',') {
            p++;
        } else if (*p == '\n') {
            if (col != NUM_COLS) {
                return 0;
            }
            p++;
            rec++;
            col = 0;
        } else {
            return 0; /* quoted cell followed by junk */
        }
    }
    if (col != 0) {
        return 0; /* last record lacks its newline */
    }
    *outRecords = rec;
    return 1;
}

/**
 * @brief Fill `rec` with the N = 4 demo values of FR-014.
 *
 * @param[out] rec Record to fill; every field points to a string literal.
 *
 * @owner The record borrows static literals; nothing is allocated.
 * @assumes `rec` is non-NULL; no numerical assumptions.
 */
static void demoRecord(QaConfigRecord *rec)
{
    rec -> specVersion = "v1";
    rec -> n = "4";
    rec -> dim = "65536";
    rec -> vectors = "4:psi0";
    rec -> gitSha = "abc1234";
    rec -> clangVersion = "clang 17.0.0";
    rec -> cmakeFlags = "-O0 -g";
    rec -> seed = "N/A";
    rec -> schedule = "N/A";
    rec -> dtSteps = "N/A";
}

/**
 * @brief `contract` group: exact header and row, replace-on-success, no
 * leftover temporary file (FR-014, EC-029).
 *
 * @return Number of failed checks.
 *
 * @owner Static buffers and stack only; files created here are removed.
 * @assumes Writes only below the build directory; no numerics.
 */
static int groupContract(void)
{
    static const char *const path = "t010-contract.csv";
    static const char *const tmp = "t010-contract.csv.tmp";
    static const char expected[] =
        EXPECTED_HEADER "v1,4,65536,4:psi0,abc1234,clang 17.0.0,-O0 -g,N/A,"
                        "N/A,N/A\n";
    QaConfigRecord rec;
    size_t len = 0;
    int failures = 0;

    demoRecord(&rec);
    (void)remove(path);
    (void)remove(tmp);

    /* Absent target: created with exactly header and row. */
    failures += checkFailed(qaIoWriteConfig(path, &rec) == QA_OK,
                            "TEST-004-driver-FR014 write to absent target");
    /* Bound: the `len == sizeof expected - 1u` test runs before `memcmp`, so
     * the compared range is exactly the bytes `readFile` stored. */
    failures += checkFailed(readFile(path, &len) && len == sizeof expected - 1u &&
                                memcmp(gFile, expected, len) == 0,
                            "TEST-004-driver-FR014 exact header and row");
    failures += checkFailed(!fileExists(tmp),
                            "TEST-004-driver-FR014 no .tmp file left");

    /* Existing target with longer old content: nothing of it survives. */
    failures += checkFailed(
        writeFixture(path, "OLD-HEADER-OLD-HEADER\nOLD,OLD,OLD,OLD,OLD,OLD,"
                           "OLD,OLD,OLD,OLD,EXTRA-EXTRA-EXTRA-EXTRA\n"),
        "TEST-004-driver-EC029 fixture written");
    failures += checkFailed(qaIoWriteConfig(path, &rec) == QA_OK,
                            "TEST-004-driver-EC029 write over existing CSV");
    failures += checkFailed(readFile(path, &len) && len == sizeof expected - 1u &&
                                memcmp(gFile, expected, len) == 0 &&
                                strstr(gFile, "OLD") == NULL,
                            "TEST-004-driver-EC029 replaced file holds nothing "
                            "of the old content");
    failures += checkFailed(!fileExists(tmp),
                            "TEST-004-driver-EC029 no .tmp file left");

    (void)remove(path);
    return failures;
}

/** Counters of the fake operations used by the path-bound checks. */
static size_t gFakeOpens;
static size_t gFakeOpenedPathLen;
static size_t gFakeRenameToLen;

/**
 * @brief Fake `open`: counts the call and returns an anonymous stream.
 *
 * @param[in] path Path the writer asked for; only its length is recorded.
 * @param[in] mode Ignored.
 * @return A `tmpfile()` stream, or NULL when it cannot be created.
 *
 * @owner The writer closes the stream through the real `fclose`.
 * @assumes `path` is a valid string.
 */
static FILE *fakeOpen(const char *path, const char *mode)
{
    (void)mode;
    gFakeOpens++;
    gFakeOpenedPathLen = strlen(path);
    return tmpfile();
}

/**
 * @brief Fake `rename`: records the target length and succeeds.
 *
 * @param[in] from Ignored.
 * @param[in] to Target path; only its length is recorded.
 * @return 0 (success).
 *
 * @owner Nothing is allocated or renamed.
 * @assumes `to` is a valid string.
 */
static int fakeRename(const char *from, const char *to)
{
    (void)from;
    gFakeRenameToLen = strlen(to);
    return 0;
}

/**
 * @brief Fake `remove`: succeeds without touching the file system.
 *
 * @param[in] path Ignored.
 * @return 0 (success).
 *
 * @owner Nothing is removed.
 * @assumes No numerical assumptions.
 */
static int fakeRemove(const char *path)
{
    (void)path;
    return 0;
}

/**
 * @brief `range` group: NULL arguments, exact bounds accepted, one byte over
 * rejected with no file created (EC-024, EC-028, FR-015).
 *
 * @return Number of failed checks.
 *
 * @owner Static buffers and stack only; files created here are removed.
 * @assumes Writes only below the build directory; no numerics.
 */
static int groupRange(void)
{
    static const char *const path = "t010-range.csv";
    static const char *const tmp = "t010-range.csv.tmp";
    static const char kOldContent[] = "OLD CONTENT\n";
    const QaIoOps fakeOps = {fakeOpen, fwrite, fclose, fakeRename, fakeRemove};
    QaConfigRecord rec;
    const char **slots[NUM_COLS];
    size_t len = 0;
    int failures = 0;

    demoRecord(&rec);
    (void)remove(path);
    (void)remove(tmp);

    /* EC-024: NULL path, NULL record, and each NULL field, no file made. */
    failures += checkFailed(qaIoWriteConfig(NULL, &rec) == QA_ERR_RANGE,
                            "TEST-004-driver-EC024 NULL path");
    failures += checkFailed(qaIoWriteConfig(path, NULL) == QA_ERR_RANGE,
                            "TEST-004-driver-EC024 NULL record");
    slots[0] = &rec.specVersion;
    slots[1] = &rec.n;
    slots[2] = &rec.dim;
    slots[3] = &rec.vectors;
    slots[4] = &rec.gitSha;
    slots[5] = &rec.clangVersion;
    slots[6] = &rec.cmakeFlags;
    slots[7] = &rec.seed;
    slots[8] = &rec.schedule;
    slots[9] = &rec.dtSteps;
    for (size_t i = 0; i < NUM_COLS; i++) {
        const char *saved = *slots[i];

        *slots[i] = NULL;
        failures += checkFailed(qaIoWriteConfig(path, &rec) == QA_ERR_RANGE,
                                "TEST-004-driver-EC024 NULL field");
        *slots[i] = saved;
    }
    failures += checkFailed(!fileExists(path) && !fileExists(tmp),
                            "TEST-004-driver-EC024 no file created");
    failures += checkFailed(
        qaIoWriteConfigWith(path, &rec, NULL) == QA_ERR_RANGE,
        "TEST-004-driver-FR015 NULL ops table");

    /* EC-028: every field exactly at its bound is accepted and round trips. */
    /* Bound: `gLongField` holds `FIELD_MAX + 2` bytes, so the memset of
     * `FIELD_MAX` bytes and the NUL at index `FIELD_MAX` fit. */
    memset(gLongField, 'x', QA_IO_CONFIG_FIELD_MAX);
    gLongField[QA_IO_CONFIG_FIELD_MAX] = '\0';
    for (size_t i = 0; i < NUM_COLS; i++) {
        const char *saved = *slots[i];
        size_t records = 0;

        *slots[i] = gLongField;
        failures += checkFailed(qaIoWriteConfig(path, &rec) == QA_OK,
                                "TEST-004-driver-EC028 field at 4096 accepted");
        /* Bound: a successful write produced at least the header, so skipping
         * `sizeof EXPECTED_HEADER - 1` bytes stays inside the file; `parseCsv`
         * rejects anything else. */
        /* Bound: `records == 1u` short-circuits first, so `gCells[0][i]` has
         * `i < NUM_COLS` and a parsed row 0. */
        failures += checkFailed(
            readFile(path, &len) && parseCsv(gFile + sizeof EXPECTED_HEADER - 1u,
                                             &records) &&
                records == 1u &&
                strlen(gCells[0][i]) == QA_IO_CONFIG_FIELD_MAX,
            "TEST-004-driver-EC028 field at 4096 round trips");
        *slots[i] = saved;
    }

    /* EC-028: one byte over, per field, leaves an existing target intact. */
    /* Bound: indices `FIELD_MAX` and `FIELD_MAX + 1` are the last two bytes
     * of the `FIELD_MAX + 2` array `gLongField`. */
    gLongField[QA_IO_CONFIG_FIELD_MAX] = 'x';
    gLongField[QA_IO_CONFIG_FIELD_MAX + 1u] = '\0';
    for (size_t i = 0; i < NUM_COLS; i++) {
        const char *saved = *slots[i];

        failures += checkFailed(writeFixture(path, kOldContent),
                                "TEST-004-driver-EC028 fixture written");
        *slots[i] = gLongField;
        failures += checkFailed(qaIoWriteConfig(path, &rec) == QA_ERR_RANGE,
                                "TEST-004-driver-EC028 field at 4097 rejected");
        failures += checkFailed(readFile(path, &len) &&
                                    len == sizeof kOldContent - 1u &&
                                    memcmp(gFile, kOldContent, len) == 0 &&
                                    !fileExists(tmp),
                                "TEST-004-driver-EC028 target bit-identical, "
                                "no file created");
        *slots[i] = saved;
    }
    (void)remove(path);

    /* EC-028: path bound, through fake operations (no PATH_MAX dependency).
     * Exactly 1024 bytes is accepted and the temporary name is 1028 bytes. */
    /* Bound: `gLongPath` holds `PATH_MAX + 2` bytes, so the memset of
     * `PATH_MAX` bytes and the NUL at index `PATH_MAX` fit. */
    memset(gLongPath, 'p', QA_IO_CONFIG_PATH_MAX);
    gLongPath[QA_IO_CONFIG_PATH_MAX] = '\0';
    gFakeOpens = 0;
    failures += checkFailed(qaIoWriteConfigWith(gLongPath, &rec, &fakeOps) ==
                                QA_OK,
                            "TEST-004-driver-EC028 path at 1024 accepted");
    failures += checkFailed(gFakeOpens == 1u && gFakeOpenedPathLen ==
                                QA_IO_CONFIG_PATH_MAX + 4u &&
                                gFakeRenameToLen == QA_IO_CONFIG_PATH_MAX,
                            "TEST-004-driver-EC028 tmp path is path + .tmp");

    /* One byte over: rejected before any open, with real and fake operations. */
    /* Bound: indices `PATH_MAX` and `PATH_MAX + 1` are the last two bytes of
     * the `PATH_MAX + 2` array `gLongPath`. */
    gLongPath[QA_IO_CONFIG_PATH_MAX] = 'p';
    gLongPath[QA_IO_CONFIG_PATH_MAX + 1u] = '\0';
    gFakeOpens = 0;
    failures += checkFailed(qaIoWriteConfigWith(gLongPath, &rec, &fakeOps) ==
                                QA_ERR_RANGE,
                            "TEST-004-driver-EC028 path at 1025 rejected");
    failures += checkFailed(gFakeOpens == 0u,
                            "TEST-004-driver-EC028 no file opened for 1025");
    failures += checkFailed(qaIoWriteConfig(gLongPath, &rec) == QA_ERR_RANGE,
                            "TEST-004-driver-EC028 path at 1025 rejected (libc)");

    return failures;
}

/**
 * @brief `escape` group: RFC 4180 quoting and round trip of fields with
 * comma, quote, CR and LF (EC-027).
 *
 * @return Number of failed checks.
 *
 * @owner Static buffers and stack only; files created here are removed.
 * @assumes Writes only below the build directory; no numerics.
 */
static int groupEscape(void)
{
    static const char *const path = "t010-escape.csv";
    static const char expectedTail[] =
        "plain,\"a,b\",\"say \"\"hi\"\"\",\"line1\r\nline2\",\"cr\rcr\","
        "\"lf\nlf\",\"\"\"\",\",\",,\"all,\"\"\r\n\"\n";
    const char *const values[NUM_COLS] = {
        "plain", "a,b", "say \"hi\"", "line1\r\nline2", "cr\rcr",
        "lf\nlf", "\"", ",", "", "all,\"\r\n"};
    QaConfigRecord rec;
    size_t len = 0;
    size_t records = 0;
    int failures = 0;

    rec.specVersion = values[0];
    rec.n = values[1];
    rec.dim = values[2];
    rec.vectors = values[3];
    rec.gitSha = values[4];
    rec.clangVersion = values[5];
    rec.cmakeFlags = values[6];
    rec.seed = values[7];
    rec.schedule = values[8];
    rec.dtSteps = values[9];
    (void)remove(path);

    failures += checkFailed(qaIoWriteConfig(path, &rec) == QA_OK,
                            "TEST-004-driver-EC027 write escaped record");
    failures += checkFailed(
        readFile(path, &len) &&
            len == sizeof EXPECTED_HEADER - 1u + sizeof expectedTail - 1u &&
            memcmp(gFile, EXPECTED_HEADER, sizeof EXPECTED_HEADER - 1u) == 0 &&
            memcmp(gFile + sizeof EXPECTED_HEADER - 1u, expectedTail,
                   sizeof expectedTail - 1u) == 0,
        "TEST-004-driver-EC027 exact quoted and escaped bytes");

    /* Round trip through the independent parser: header plus one record. */
    failures += checkFailed(parseCsv(gFile, &records) && records == 2u,
                            "TEST-004-driver-EC027 output parses as 2 records");
    /* Bound: `records == 2u` makes row 1 valid; `i < NUM_COLS`. */
    for (size_t i = 0; records == 2u && i < NUM_COLS; i++) {
        failures += checkFailed(strcmp(gCells[1][i], values[i]) == 0,
                                "TEST-004-driver-EC027 field round trips");
    }
    /* Bound: `records == 2u` is tested first; index 9 is `NUM_COLS - 1`. */
    failures += checkFailed(records == 2u &&
                                strcmp(gCells[0][0], "spec_version") == 0 &&
                                strcmp(gCells[0][9], "dt_steps") == 0,
                            "TEST-004-driver-EC027 header row parses");

    (void)remove(path);
    return failures;
}

/* Which operation the injected table makes fail (T-011). */
typedef enum FailKind {
    FAIL_NONE = 0,
    FAIL_OPEN,
    FAIL_WRITE,
    FAIL_CLOSE,
    FAIL_RENAME
} FailKind;

static FailKind gFailKind;
static size_t gFailWriteAt; /* 1-based write call that fails */
static size_t gWrites;
static size_t gCloses;
static size_t gRemoves;
static long gLiveStreams;   /* opened minus closed streams */

/**
 * @brief Reset the injection state and counters before one run.
 *
 * @param[in] kind Failure to inject.
 * @param[in] failWriteAt 1-based write call that fails (FAIL_WRITE only).
 *
 * @owner Writes the static counters only.
 * @assumes Called before each injected run.
 */
static void resetInjection(FailKind kind, size_t failWriteAt)
{
    gFailKind = kind;
    gFailWriteAt = failWriteAt;
    gWrites = 0;
    gCloses = 0;
    gRemoves = 0;
    gLiveStreams = 0;
}

/**
 * @brief Injected `open`: fails for FAIL_OPEN, else a real `fopen`.
 *
 * @param[in] path File to open.
 * @param[in] mode `fopen` mode.
 * @return The stream, or NULL for FAIL_OPEN or a real failure.
 *
 * @owner The returned stream is counted in `gLiveStreams` and released by `injClose`.
 * @assumes `path` and `mode` are valid strings.
 */
static FILE *injOpen(const char *path, const char *mode)
{
    FILE *f;

    if (gFailKind == FAIL_OPEN) {
        return NULL;
    }
    f = fopen(path, mode);
    if (f != NULL) {
        gLiveStreams++;
    }
    return f;
}

/**
 * @brief Injected `write`: short-writes (returns 0) on the chosen call.
 *
 * @param[in] buf Bytes to write.
 * @param[in] size Element size.
 * @param[in] count Element count.
 * @param[in,out] f Open stream.
 * @return Elements written; 0 on the injected call.
 *
 * @owner The stream stays with the caller.
 * @assumes `size * count` fits the buffer, as for `fwrite`.
 */
static size_t injWrite(const void *buf, size_t size, size_t count, FILE *f)
{
    gWrites++;
    if (gFailKind == FAIL_WRITE && gWrites == gFailWriteAt) {
        return 0;
    }
    return fwrite(buf, size, count, f);
}

/**
 * @brief Injected `close`: always releases the stream; reports EOF for
 * FAIL_CLOSE, like a failed flush at `fclose`.
 *
 * @param[in] f Stream opened by `injOpen`; invalid after the call.
 * @return The real `fclose` result, or EOF for FAIL_CLOSE.
 *
 * @owner Releases the stream and decrements `gLiveStreams`.
 * @assumes `f` was returned by `injOpen` and is closed once.
 */
static int injClose(FILE *f)
{
    int rc = fclose(f);

    gCloses++;
    gLiveStreams--;
    return gFailKind == FAIL_CLOSE ? EOF : rc;
}

/**
 * @brief Injected `rename`: fails for FAIL_RENAME, else a real `rename`.
 *
 * @param[in] from Source path.
 * @param[in] to Target path.
 * @return -1 for FAIL_RENAME, else the real result.
 *
 * @owner Moves a file only when it does not inject a failure.
 * @assumes Both paths are valid strings.
 */
static int injRename(const char *from, const char *to)
{
    if (gFailKind == FAIL_RENAME) {
        return -1;
    }
    return rename(from, to);
}

/**
 * @brief Injected `remove`: counts, then removes for real.
 *
 * @param[in] path File to remove.
 * @return The real `remove` result.
 *
 * @owner Deletes `path`; the count is kept in `gRemoves`.
 * @assumes `path` is a valid string.
 */
static int injRemove(const char *path)
{
    gRemoves++;
    return remove(path);
}

/**
 * @brief Run one injected failure and check every FR-015 postcondition.
 *
 * With `existing` the target holds a valid CSV before the call and must be
 * bit-identical afterwards (EC-023); otherwise it must stay absent. In both
 * cases the call returns `QA_ERR_IO`, no stream stays open, the stream is
 * closed exactly once when it was opened, and no `.tmp` file is left.
 *
 * @param[in] path Target CSV path.
 * @param[in] tmp Temporary path the writer uses (`path` + `.tmp`).
 * @param[in] existing Non-zero when the target holds a valid CSV before.
 * @param[in] kind Failure to inject.
 * @param[in] failWriteAt 1-based write call that fails (FAIL_WRITE only).
 * @param[in] what Label used in the failure messages.
 * @return Number of failed checks.
 *
 * @owner Creates and removes `path` and `tmp`; no heap is used.
 * @assumes The file system lets the build directory be written.
 */
static int checkInjected(const char *path, const char *tmp, int existing,
                         FailKind kind, size_t failWriteAt, const char *what)
{
    static const char kOld[] = "spec_version,N\nv1,2\n";
    const QaIoOps ops = {injOpen, injWrite, injClose, injRename, injRemove};
    QaConfigRecord rec;
    size_t len = 0;
    int failures = 0;

    demoRecord(&rec);
    (void)remove(path);
    (void)remove(tmp);
    if (existing) {
        failures += checkFailed(writeFixture(path, kOld),
                                "TEST-004-driver-EC023 fixture written");
    }
    resetInjection(kind, failWriteAt);
    failures += checkFailed(qaIoWriteConfigWith(path, &rec, &ops) == QA_ERR_IO,
                            what);
    failures += checkFailed(gLiveStreams == 0 &&
                                gCloses == (kind == FAIL_OPEN ? 0u : 1u),
                            "TEST-004-driver-EC022 stream closed exactly once");
    failures += checkFailed(!fileExists(tmp),
                            "TEST-004-driver-EC022 no .tmp file left");
    if (existing) {
        failures += checkFailed(readFile(path, &len) &&
                                    len == sizeof kOld - 1u &&
                                    memcmp(gFile, kOld, len) == 0,
                                "TEST-004-driver-EC023 target bit-identical");
    } else {
        failures += checkFailed(!fileExists(path),
                                "TEST-004-driver-EC022 absent target stays "
                                "absent");
    }
    (void)remove(path);
    return failures;
}

/**
 * @brief `io-failure` group: missing and non-writable directory (EC-021),
 * injected open, write, close and rename failures (EC-022) and an existing
 * target left bit-identical (EC-023).
 *
 * @return Number of failed checks.
 *
 * @owner Static buffers and stack only; files created here are removed.
 * @assumes The temporary read-only directory `roDir` is created and removed here; the injected operations never touch other files.
 */
static int groupIoFailure(void)
{
    static const char *const path = "t011-failure.csv";
    static const char *const tmp = "t011-failure.csv.tmp";
    static const char *const roDir = "t011-ro";
    static const char *const roPath = "t011-ro/out.csv";
    static const char *const roTmp = "t011-ro/out.csv.tmp";
    static const char *const noPath = "t011-missing-dir/out.csv";
    const QaIoOps countOps = {injOpen, injWrite, injClose, injRename,
                              injRemove};
    QaConfigRecord rec;
    size_t totalWrites;
    int failures = 0;

    demoRecord(&rec);

    /* EC-021: missing directory -> QA_ERR_IO, nothing created. */
    failures += checkFailed(qaIoWriteConfig(noPath, &rec) == QA_ERR_IO,
                            "TEST-004-driver-EC021 missing directory");
    failures += checkFailed(!fileExists(noPath) &&
                                access("t011-missing-dir", F_OK) != 0,
                            "TEST-004-driver-EC021 directory not created");

    /* EC-021: non-writable directory (skipped when running with rights that
     * ignore the mode bits, e.g. root). */
    (void)rmdir(roDir);
    if (mkdir(roDir, 0555) == 0) {
        (void)chmod(roDir, 0555);
        if (access(roDir, W_OK) != 0) {
            failures += checkFailed(qaIoWriteConfig(roPath, &rec) == QA_ERR_IO,
                                    "TEST-004-driver-EC021 non-writable "
                                    "directory");
            failures += checkFailed(!fileExists(roPath) && !fileExists(roTmp),
                                    "TEST-004-driver-EC021 no file created");
        } else {
            printf("note: directory mode not enforced, skipping read-only "
                   "case\n");
        }
        (void)rmdir(roDir);
    } else {
        failures += checkFailed(0, "TEST-004-driver-EC021 mkdir read-only dir");
    }

    /* Stale temporary sibling: "wx" refuses it, the target stays untouched. */
    (void)remove(path);
    failures += checkFailed(writeFixture(tmp, "stale"),
                            "TEST-004-driver-EC022 stale tmp fixture");
    failures += checkFailed(qaIoWriteConfig(path, &rec) == QA_ERR_IO &&
                                !fileExists(path),
                            "TEST-004-driver-EC022 stale .tmp refused");
    (void)remove(tmp);

    /* Count the writes of a successful run, then fail each one in turn. */
    resetInjection(FAIL_NONE, 0);
    failures += checkFailed(qaIoWriteConfigWith(path, &rec, &countOps) == QA_OK,
                            "TEST-004-driver-EC022 counting run succeeds");
    totalWrites = gWrites;
    (void)remove(path);
    failures += checkFailed(totalWrites > 2u,
                            "TEST-004-driver-EC022 several write calls");

    for (int existing = 0; existing <= 1; existing++) {
        failures += checkInjected(path, tmp, existing, FAIL_OPEN, 0,
                                  "TEST-004-driver-EC022 open failure");
        for (size_t k = 1; k <= totalWrites; k++) {
            failures += checkInjected(path, tmp, existing, FAIL_WRITE, k,
                                      "TEST-004-driver-EC022 write failure");
        }
        failures += checkInjected(path, tmp, existing, FAIL_CLOSE, 0,
                                  "TEST-004-driver-EC022 close failure");
        failures += checkInjected(path, tmp, existing, FAIL_RENAME, 0,
                                  "TEST-004-driver-EC022 rename failure");
    }

    /* The temporary file is removed (not just closed) after a write failure. */
    resetInjection(FAIL_WRITE, 2);
    {
        const QaIoOps ops = {injOpen, injWrite, injClose, injRename,
                             injRemove};

        (void)qaIoWriteConfigWith(path, &rec, &ops);
    }
    failures += checkFailed(gRemoves == 1u,
                            "TEST-004-driver-EC022 .tmp removed after failure");
    (void)remove(path);
    return failures;
}

/**
 * @brief Run one named group, or all when `name` is NULL.
 *
 * @param[in] name Group name or NULL for all groups.
 * @return Number of failed checks, or -1 for an unknown name (usage printed).
 *
 * @owner Static buffers only.
 * @assumes `name`, when given, is a valid string.
 */
static int runGroup(const char *name)
{
    int failures = 0;
    int ran = 0;

    if (name == NULL || strcmp(name, "contract") == 0) {
        failures += groupContract();
        ran = 1;
    }
    if (name == NULL || strcmp(name, "range") == 0) {
        failures += groupRange();
        ran = 1;
    }
    if (name == NULL || strcmp(name, "escape") == 0) {
        failures += groupEscape();
        ran = 1;
    }
    if (name == NULL || strcmp(name, "io-failure") == 0) {
        failures += groupIoFailure();
        ran = 1;
    }
    if (!ran) {
        printf("usage: test-004-driver-io [contract|range|escape|io-failure]\n");
        return -1;
    }
    return failures;
}

/**
 * @brief Run the requested group (or all) and report the result.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Optional group name at `argv[1]`.
 * @return 0 on success, 1 on failed checks, 2 on a usage error.
 *
 * @owner No resource is kept.
 * @assumes `argv[1]`, when present, is a valid string.
 */
int main(int argc, char **argv)
{
    int failures = runGroup(argc > 1 ? argv[1] : NULL);

    if (failures < 0) {
        return 2;
    }
    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("OK\n");
    return 0;
}
