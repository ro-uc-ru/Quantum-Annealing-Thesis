/*
 * 005-evolution run record writer tests, Phase 5 (task T-013).
 *
 * Purpose: assert FR-013 (streamed `trace.csv` and zero-padded snapshots in the
 * formats of section 4), FR-014 (`config.json` written last, escaped, with the
 * keys of section 4 in order), FR-019 and FR-025 (every injected create, write,
 * close, rename or remove failure returns `QA_ERR_IO`, releases every stream and
 * leaves no `config.json`), EC-009 and EC-017 (the `--out` rules: missing,
 * empty, non-empty, regular file, symlink, dangling symlink, missing parent).
 * Each group is selectable as `test-005-run <group>` so a tasks.md pattern
 * `ctest -R 005-evolution-io-<group>` matches one CTest entry; no argument runs
 * every group. Failing checks print their FR/EC identifier.
 *
 * Independence: expected bytes are literals in this file; read-back goes through
 * `parseCsv` (fields converted with `strtod`) and `parseJson` (a flat-object
 * parser with its own unescaping), which share no code with the writer.
 *
 * Ownership: run handles are destroyed by `qaRunDestroy` on every path; scratch
 * trees live in the working directory (the build tree under CTest) and are
 * removed by `removeTree`. The failure sweep counts the streams opened and closed
 * through the ops table and requires both counts to be equal after each run.
 * Errors: any failed check is printed and the process exits non-zero; an unknown
 * group exits 2 with usage. Determinism: fixed inputs only, no clock or RNG.
 * Numerical assumptions: probabilities and times are compared after a `%.17g`
 * round trip, which is exact, so equality is tested with `==`.
 */

#define _POSIX_C_SOURCE 200809L /* mkdir, symlink, lstat, opendir */

#include "qa/io/run.h"

#include "run-internal.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCRATCH "t005-run-scratch"
#define BUF_CAP (64u * 1024u)
#define MAX_ROWS 64u
#define MAX_COLS 8u
#define MAX_KEYS 16u

/* Number of failed checks; incremented by `check` and `freshScratch`, read by
 * `main` for the exit status. Single-threaded, never reset. */
static int gFailures = 0;

/* Content of the last file read by `readFile`, NUL-terminated; BUF_CAP bytes,
 * so a file of at most `BUF_CAP - 1` bytes fits. Overwritten by every read. */
static char gFile[BUF_CAP];

/**
 * @brief Record one check and print the identifier of a failure.
 *
 * @param[in] condition Non-zero when the check passed.
 * @param[in] id        Traceability identifier printed on failure.
 * @param[in] what      Description printed on failure.
 *
 * @return Nothing. A failed check prints a line and increments `gFailures`; the
 *         test continues so that one run reports every miss.
 *
 * @owner Nothing is allocated. @assumes No numerical assumptions.
 */
static void check(int condition, const char *id, const char *what)
{
    if (!condition) {
        printf("FAIL TEST-005-%s: %s\n", id, what);
        gFailures++;
    }
}

/**
 * @brief Read a whole file into `gFile`, NUL-terminated.
 *
 * @param[in]  path   File to read.
 * @param[out] outLen Byte count on success.
 *
 * @return 1 on success, 0 when the file cannot be opened.
 *
 * @owner The stream is opened and closed inside the call.
 * @assumes The file fits `BUF_CAP - 1` bytes, enforced by the read bound.
 */
static int readFile(const char *path, size_t *outLen)
{
    FILE *f = fopen(path, "rb");
    size_t len;

    if (f == NULL) {
        return 0;
    }
    /* Bound: at most `BUF_CAP - 1` bytes are read, so `gFile[len]` is inside. */
    len = fread(gFile, 1, BUF_CAP - 1u, f);
    (void)fclose(f);
    gFile[len] = '\0';
    *outLen = len;
    return 1;
}

/**
 * @brief Report whether `path` exists (symbolic links not followed).
 *
 * @param[in] path Path to probe.
 *
 * @return 1 when `lstat` succeeds, else 0.
 *
 * @owner Nothing is allocated. @assumes No numerical assumptions.
 */
static int exists(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}

/**
 * @brief Remove a path recursively (files, links and directories).
 *
 * @param[in] path Path to remove; a missing path is a no-op.
 *
 * @return Nothing. Errors of `rmdir`, `unlink` and `closedir` are ignored on
 *         purpose: it is best-effort cleanup and the next `check` of the tree
 *         reports any leftover.
 *
 * @owner The `DIR *` of each level is closed before the level is removed.
 * @assumes The tree is shallow (three levels), so the recursion depth and the
 *          `child` buffer are small and bounded by the fixed names used here.
 */
static void removeTree(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        struct dirent *entry;

        if (dir != NULL) {
            while ((entry = readdir(dir)) != NULL) {
                char child[512];

                if (strcmp(entry -> d_name, ".") == 0 ||
                    strcmp(entry -> d_name, "..") == 0) {
                    continue;
                }
                /* Bound: `snprintf` truncates inside `child`; the names used by
                 * these tests are far below its size. */
                (void)snprintf(child, sizeof child, "%s/%s", path,
                               entry -> d_name);
                removeTree(child);
            }
            (void)closedir(dir);
        }
        (void)rmdir(path);
        return;
    }
    (void)unlink(path);
}

/**
 * @brief Remove and recreate the empty scratch directory (the run dir's parent).
 *
 * @return Nothing. When `mkdir` fails it prints a line and increments
 *         `gFailures`; later checks then fail on the missing directory.
 *
 * @owner Nothing allocated. @assumes `mkdir` succeeds in the working directory.
 */
static void freshScratch(void)
{
    removeTree(SCRATCH);
    if (mkdir(SCRATCH, 0777) != 0) {
        printf("FAIL: cannot create %s\n", SCRATCH);
        gFailures++;
    }
}

/**
 * @brief Create or overwrite `path` with `content` (test fixture).
 *
 * @param[in] path    File to create.
 * @param[in] content NUL-terminated bytes.
 *
 * @return 1 on success, 0 on any I/O failure.
 *
 * @owner The stream is closed on every path. @assumes No numerics.
 */
static int writeFixture(const char *path, const char *content)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL) {
        return 0;
    }
    ok = fputs(content, f) >= 0;
    return fclose(f) == 0 && ok;
}

/**
 * @brief Split a CSV text into rows of numeric cells (independent reader).
 *
 * The first line is the header and is returned verbatim in `header`; each later
 * line is split on `,` and every cell converted with `strtod`.
 *
 * @param[in]  text      NUL-terminated file content ending with `\n`.
 * @param[out] header    Buffer of `hcap` bytes for the header line (no `\n`).
 * @param[in]  hcap      Capacity of `header`.
 * @param[out] cells     `MAX_ROWS x MAX_COLS` parsed values.
 * @param[out] outCols   Cells per row (equal for every row), 0 when ragged.
 *
 * @return The number of data rows, or `(size_t)-1` on a malformed file.
 *
 * @owner Static storage only. @assumes The file has at most `MAX_ROWS` rows and
 *         `MAX_COLS` columns; larger inputs are reported malformed.
 */
static size_t parseCsv(const char *text, char *header, size_t hcap,
                       double cells[MAX_ROWS][MAX_COLS], size_t *outCols)
{
    const char *p = strchr(text, '\n');
    size_t rows = 0;

    *outCols = 0;
    if (p == NULL || (size_t)(p - text) >= hcap) {
        return (size_t)-1;
    }
    /* Bound: the test above guarantees `p - text < hcap`, so the copy and the
     * terminator at `header[p - text]` stay inside the `hcap`-byte buffer. */
    memcpy(header, text, (size_t)(p - text));
    header[p - text] = '\0';
    p++;
    while (*p != '\0') {
        size_t col = 0;

        /* Bound: `cells[rows]` is written below, so reject when `rows` already
         * reached `MAX_ROWS` (checked before every row, not after). */
        if (rows >= MAX_ROWS) {
            return (size_t)-1;
        }
        for (;;) {
            char *end;

            /* Bound: `cells[rows][col]` is written next, so `col` must be
             * below `MAX_COLS`; a wider row is reported malformed. */
            if (col >= MAX_COLS) {
                return (size_t)-1;
            }
            cells[rows][col++] = strtod(p, &end);
            if (end == p) {
                return (size_t)-1;
            }
            p = end;
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == '\n') {
                p++;
                break;
            }
            return (size_t)-1;
        }
        if (rows == 0) {
            *outCols = col;
        } else if (col != *outCols) {
            *outCols = 0;
            return (size_t)-1;
        }
        rows++;
    }
    return rows;
}

/**
 * @brief One member of the flat JSON object read by `parseJson`.
 *
 * Both texts are NUL-terminated copies, so a member outlives the file buffer.
 * `parseJson` rejects any key of 32 bytes or more and any value of 512 bytes or
 * more, so the terminators always fit.
 *
 * @owner Array of `MAX_KEYS` members on the caller's stack; nothing is allocated.
 * @assumes Single-threaded; members past the returned count are uninitialized.
 */
typedef struct JsonMember {
    char key[32];    /* member name, without the quotes */
    char value[512]; /* unescaped string, or the raw number text */
    int isString;    /* 1 when the value was a quoted string, 0 for a number */
} JsonMember;

/**
 * @brief Parse a flat JSON object with a strict reader (independent of the writer).
 *
 * Accepts `{`, then members `"key": value` separated by commas (value: string with
 * the escapes `\"`, `\\`, `\uXXXX` for code points below 0x100, or a bare number
 * token), then `}` and a final newline. Whitespace is only the newlines and
 * two-space indent the writer promises, so formatting drift is detected.
 *
 * @param[in]  text    NUL-terminated file content.
 * @param[out] members Parsed members, in file order.
 *
 * @return The member count, or `(size_t)-1` when the text is malformed.
 *
 * @owner Static storage only. @assumes At most `MAX_KEYS` members; keys below 32
 *         bytes and values below 512 bytes, else the text is reported malformed.
 */
static size_t parseJson(const char *text, JsonMember members[MAX_KEYS])
{
    const char *p = text;
    size_t count = 0;

    if (strncmp(p, "{\n", 2) != 0) {
        return (size_t)-1;
    }
    p += 2;
    while (strncmp(p, "}\n", 2) != 0) {
        JsonMember *m;
        size_t n = 0;

        /* Bound: `members[count]` is written below, so `count` must be below
         * `MAX_KEYS`; the prefix test also fixes the two-space indent. */
        if (count >= MAX_KEYS || strncmp(p, "  \"", 3) != 0) {
            return (size_t)-1;
        }
        m = &members[count];
        p += 3;
        while (*p != '"') {
            /* Bound: `n + 1 < sizeof key` keeps room for the terminator written
             * after the loop; the NUL test stops at an unterminated key. */
            if (*p == '\0' || n + 1 >= sizeof m -> key) {
                return (size_t)-1;
            }
            m -> key[n++] = *p++;
        }
        m -> key[n] = '\0';
        p++;
        if (strncmp(p, ": ", 2) != 0) {
            return (size_t)-1;
        }
        p += 2;
        n = 0;
        if (*p == '"') {
            m -> isString = 1;
            p++;
            while (*p != '"') {
                unsigned int code;
                char c = *p;

                /* Bound: `n + 1 < sizeof value` keeps room for the terminator
                 * written after the value; raw control bytes are malformed. */
                if (c == '\0' || (unsigned char)c < 0x20u ||
                    n + 1 >= sizeof m -> value) {
                    return (size_t)-1;
                }
                if (c == '\\') {
                    p++;
                    if (*p == '"' || *p == '\\') {
                        c = *p++;
                    } else if (*p == 'u' && isxdigit((unsigned char)p[1]) &&
                               isxdigit((unsigned char)p[2]) &&
                               isxdigit((unsigned char)p[3]) &&
                               isxdigit((unsigned char)p[4]) &&
                               sscanf(p + 1, "%4x", &code) == 1 && code < 0x100u) {
                        /* Bound: the four `isxdigit` tests (each stops at the
                         * NUL) prove `p[1..4]` are digits inside the string,
                         * so `p += 5` lands on or before the terminator. */
                        c = (char)code;
                        p += 5;
                    } else {
                        return (size_t)-1;
                    }
                    m -> value[n++] = c;
                    continue;
                }
                m -> value[n++] = c;
                p++;
            }
            p++;
        } else {
            m -> isString = 0;
            while (*p != ',' && *p != '\n') {
                /* Same bound as the string case: the terminator must fit. */
                if (*p == '\0' || n + 1 >= sizeof m -> value) {
                    return (size_t)-1;
                }
                m -> value[n++] = *p++;
            }
        }
        m -> value[n] = '\0';
        count++;
        if (*p == ',') {
            p++;
            if (*p != '\n') {
                return (size_t)-1;
            }
            p++;
        } else if (*p == '\n') {
            p++;
            if (strncmp(p, "}\n", 2) != 0) {
                return (size_t)-1;
            }
        } else {
            return (size_t)-1;
        }
    }
    /* Bound: the loop ended on `"}\n"`, so `p[0..1]` are valid and `p[2]` is at
     * worst the terminator of `text`; anything after the object is malformed. */
    return p[2] == '\0' ? count : (size_t)-1;
}

/**
 * @brief Build the fixed configuration record used by the groups.
 *
 * @param[in] steps Value of the `steps` key; must equal the run's steps.
 *
 * @return The record by value; string members point to literals.
 *
 * @owner Nothing is allocated. @assumes `steps >= 1`.
 */
static QaRunConfig makeConfig(size_t steps)
{
    QaRunConfig c;

    c.n = 2;
    c.family = "linear";
    c.T = 1.0;
    c.steps = steps;
    c.dt = 0.25;
    c.r = 0.5;
    c.snapshotInterval = 2;
    c.gitSha = "abc123";
    c.clangVersion = "clang 17";
    c.cmakeFlags = "-O2";
    return c;
}

/* ---- group: outdir ---------------------------------------------------- */

/**
 * @brief EC-017 and FR-015: every `--out` rule, with nothing created.
 *
 * @return Nothing; a failed check increments `gFailures` and the group continues.
 *
 * @owner Scratch tree removed at the end. @assumes `symlink` is available (LIM-1).
 */
static void testOutDir(void)
{
    char longPath[QA_IO_RUN_PATH_MAX + 2u];
    struct stat st;

    removeTree(SCRATCH);
    check(mkdir(SCRATCH, 0777) == 0, "EC017", "scratch created");

    check(qaRunCheckOutDir(NULL) == QA_ERR_DOMAIN, "EC017", "NULL path");
    check(qaRunCheckOutDir("") == QA_ERR_RANGE, "EC017", "empty path");
    check(qaRunCheckOutDir(SCRATCH "/x/") == QA_ERR_RANGE, "EC017",
          "trailing slash");
    /* Bound: `longPath` holds `QA_IO_RUN_PATH_MAX + 2u` bytes, so `PATH_MAX + 1u`
     * filler bytes plus the terminator at index `PATH_MAX + 1u` fit exactly. */
    memset(longPath, 'a', QA_IO_RUN_PATH_MAX + 1u);
    longPath[QA_IO_RUN_PATH_MAX + 1u] = '\0';
    check(qaRunCheckOutDir(longPath) == QA_ERR_RANGE, "EC017", "too long");

    /* Absent path with an existing parent, and with no slash at all. */
    check(qaRunCheckOutDir(SCRATCH "/new") == QA_OK, "EC017", "absent ok");
    check(!exists(SCRATCH "/new"), "EC017", "absent path is not created");
    check(qaRunCheckOutDir("t005-run-absent-here") == QA_OK, "EC017",
          "absent, parent is cwd");
    check(!exists("t005-run-absent-here"), "EC017", "cwd child not created");

    /* Missing parent, parent that is a file. */
    check(qaRunCheckOutDir(SCRATCH "/nope/new") == QA_ERR_IO, "EC017",
          "missing parent");
    check(writeFixture(SCRATCH "/file", "x\n"), "EC017", "fixture file");
    check(qaRunCheckOutDir(SCRATCH "/file") == QA_ERR_IO, "EC017",
          "regular file");
    check(qaRunCheckOutDir(SCRATCH "/file/new") == QA_ERR_IO, "EC017",
          "parent is a file");

    /* Empty directory accepted, non-empty rejected and left untouched. */
    check(mkdir(SCRATCH "/empty", 0777) == 0, "EC017", "empty dir fixture");
    check(qaRunCheckOutDir(SCRATCH "/empty") == QA_OK, "EC017", "empty dir ok");
    check(mkdir(SCRATCH "/full", 0777) == 0, "EC017", "full dir fixture");
    check(writeFixture(SCRATCH "/full/keep", "keep\n"), "EC017", "full content");
    check(qaRunCheckOutDir(SCRATCH "/full") == QA_ERR_IO, "EC017",
          "non-empty dir");
    check(readFile(SCRATCH "/full/keep", &(size_t){0}) &&
              strcmp(gFile, "keep\n") == 0,
          "EC017", "non-empty dir untouched");

    /* Symbolic links: to a directory (followed), to a file, dangling. */
    check(symlink("empty", SCRATCH "/lnk-dir") == 0, "EC017", "link fixture");
    check(qaRunCheckOutDir(SCRATCH "/lnk-dir") == QA_OK, "EC017",
          "link to empty dir");
    check(symlink("full", SCRATCH "/lnk-full") == 0, "EC017", "link fixture");
    check(qaRunCheckOutDir(SCRATCH "/lnk-full") == QA_ERR_IO, "EC017",
          "link to non-empty dir");
    check(symlink("file", SCRATCH "/lnk-file") == 0, "EC017", "link fixture");
    check(qaRunCheckOutDir(SCRATCH "/lnk-file") == QA_ERR_IO, "EC017",
          "link to file");
    check(symlink("missing-target", SCRATCH "/lnk-dangling") == 0, "EC017",
          "link fixture");
    check(qaRunCheckOutDir(SCRATCH "/lnk-dangling") == QA_ERR_IO, "EC017",
          "dangling link");
    check(lstat(SCRATCH "/lnk-dangling", &st) == 0 && S_ISLNK(st.st_mode),
          "EC017", "dangling link untouched");

    /* The open does not touch the file system either. */
    {
        QaRun *run = NULL;

        check(qaRunOpen(SCRATCH "/lazy", 3, &run) == QA_OK && run != NULL,
              "FR013", "open ok");
        check(!exists(SCRATCH "/lazy"), "FR013", "open creates nothing");
        check(qaRunDestroy(run) == QA_OK, "FR019", "destroy ok");
        check(!exists(SCRATCH "/lazy"), "FR013", "destroy creates nothing");
    }
    removeTree(SCRATCH);
}

/* ---- group: layout ---------------------------------------------------- */

/**
 * @brief FR-013 and FR-014: a complete run with the exact layout and contents.
 *
 * @return Nothing; a failed check increments `gFailures` and the group continues.
 *
 * @owner Handle destroyed; scratch tree removed. @assumes `%.17g` round trips.
 */
static void testLayout(void)
{
    static const double probs[4] = {0.5, 0.25, 0.125, 0.125};
    double cells[MAX_ROWS][MAX_COLS];
    char header[64];
    size_t cols, len, rows;
    QaRun *run = NULL;
    QaRunConfig cfg = makeConfig(12);
    QaStatus st;

    freshScratch();
    check(qaRunOpen(SCRATCH "/run", 12, &run) == QA_OK, "FR013", "open");
    check(qaRunWriteSnapshot(run, 0, 4, probs) == QA_ERR_DOMAIN, "FR028",
          "snapshot before the layout exists");
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_DOMAIN, "FR014",
          "config before the layout exists");
    check(!exists(SCRATCH "/run"), "FR013", "failed calls create nothing");

    check(qaRunAppendTrace(run, 0, 0.0, 0.0, 1.0, 1.0, -4.0) == QA_OK, "FR013",
          "row 0");
    check(exists(SCRATCH "/run/trace.csv") && exists(SCRATCH "/run/snapshots"),
          "FR013", "lazy creation at j = 0");
    check(qaRunAppendTrace(run, 1, 0.1, 0.1, 0.9, 1.0, -3.5) == QA_OK, "FR013",
          "row 1");
    /* Bound: dim 4 equals the length of probs[4]. */
    check(qaRunWriteSnapshot(run, 0, 4, probs) == QA_OK, "FR013", "snap 0");
    check(qaRunWriteSnapshot(run, 12, 4, probs) == QA_OK, "FR013", "snap 12");
    for (size_t j = 2; j <= 12; j++) {
        check(qaRunAppendTrace(run, j, (double)j / 12.0, 0.5, 0.5, 1.0, -2.0) ==
                  QA_OK,
              "FR013", "row j");
    }
    check(qaRunAppendTrace(run, 13, 1.0, 1.0, 0.0, 1.0, 0.0) == QA_ERR_RANGE,
          "FR013", "row beyond steps");
    st = qaRunWriteConfig(run, &cfg);
    check(st == QA_OK, "FR014", "config written");
    check(qaRunAppendTrace(run, 13, 1.0, 1.0, 0.0, 1.0, 0.0) == QA_ERR_DOMAIN,
          "FR013", "append after the trace closed");
    check(qaRunDestroy(run) == QA_OK, "FR019", "destroy");

    /* trace.csv: exact leading bytes, then the independent reader. */
    check(readFile(SCRATCH "/run/trace.csv", &len), "FR013", "read trace");
    check(strncmp(gFile,
                  "j,t,a,b,norm,energy\n0,0,0,1,1,-4\n"
                  "1,0.10000000000000001,0.10000000000000001,0.90000000000000002,"
                  "1,-3.5\n",
                  strlen("j,t,a,b,norm,energy\n0,0,0,1,1,-4\n1,0.10000000000000001"
                         ",0.10000000000000001,0.90000000000000002,1,-3.5\n")) ==
              0,
          "FR013", "trace literal bytes");
    rows = parseCsv(gFile, header, sizeof header, cells, &cols);
    check(rows == 13 && cols == 6, "FR013", "13 rows x 6 columns");
    check(strcmp(header, "j,t,a,b,norm,energy") == 0, "FR013", "trace header");
    check(rows == 13 && cells[1][1] == 0.1 && cells[1][3] == 0.9 &&
              cells[12][0] == 12.0 && cells[12][1] == 1.0,
          "FR013", "trace round trip is exact");

    /* snapshots: zero-padded to the two digits of 12, nothing else. */
    check(exists(SCRATCH "/run/snapshots/snap_00.csv") &&
              exists(SCRATCH "/run/snapshots/snap_12.csv") &&
              !exists(SCRATCH "/run/snapshots/snap_0.csv") &&
              !exists(SCRATCH "/run/snapshots/snap_06.csv"),
          "FR013", "snapshot names");
    check(readFile(SCRATCH "/run/snapshots/snap_00.csv", &len) &&
              strcmp(gFile, "k,p\n0,0.5\n1,0.25\n2,0.125\n3,0.125\n") == 0,
          "FR013", "snapshot literal bytes");
    rows = parseCsv(gFile, header, sizeof header, cells, &cols);
    check(rows == 4 && cols == 2 && strcmp(header, "k,p") == 0 &&
              cells[2][0] == 2.0 && cells[3][1] == 0.125,
          "FR013", "snapshot rows");

    /* config.json: the independent parser and the key order. */
    check(readFile(SCRATCH "/run/config.json", &len), "FR014", "read config");
    {
        static const char *const keys[13] = {
            "format_version", "spec", "N", "family", "T", "steps", "dt", "r",
            "M", "seed", "git_sha", "clang_version", "cmake_flags"};
        JsonMember m[MAX_KEYS];
        size_t count = parseJson(gFile, m);

        /* Bound: every `m[i]` read below (i up to 12, and the fixed indices 0,
         * 1, 2, 4..10) is guarded by `count == 13`, so it is an initialized
         * member and below `MAX_KEYS` (16). */
        check(count == 13, "FR014", "13 members");
        for (size_t i = 0; count == 13 && i < 13; i++) {
            check(strcmp(m[i].key, keys[i]) == 0, "FR014", "key order");
        }
        check(count == 13 && strcmp(m[0].value, "1") == 0 && !m[0].isString,
              "FR014", "format_version");
        check(count == 13 && strcmp(m[1].value, "005-evolution") == 0 &&
                  m[1].isString,
              "FR014", "spec");
        check(count == 13 && strcmp(m[2].value, "2") == 0, "FR014", "N");
        check(count == 13 && atof(m[4].value) == 1.0 &&
                  atof(m[6].value) == 0.25 && atof(m[7].value) == 0.5,
              "FR014", "T, dt, r");
        check(count == 13 && strcmp(m[5].value, "12") == 0 &&
                  strcmp(m[8].value, "2") == 0,
              "FR014", "steps, M");
        check(count == 13 && strcmp(m[9].value, "N/A") == 0 && m[9].isString,
              "FR014", "seed");
        check(count == 13 && strcmp(m[10].value, "abc123") == 0, "FR014",
              "git_sha");
    }
    check(!exists(SCRATCH "/run/config.json.tmp"), "FR014",
          "temporary file is gone");
    freshScratch();

    /* Padding widths: 9 steps -> 1 digit, 100 steps -> 3 digits. */
    check(qaRunOpen(SCRATCH "/w1", 9, &run) == QA_OK, "FR013", "open w1");
    check(qaRunAppendTrace(run, 0, 0, 0, 1, 1, -4) == QA_OK, "FR013", "w1 row");
    /* Bound: dim 1 <= the length of probs[4]. */
    check(qaRunWriteSnapshot(run, 7, 1, probs) == QA_OK, "FR013", "w1 snap");
    check(exists(SCRATCH "/w1/snapshots/snap_7.csv"), "FR013", "1 digit");
    check(qaRunDestroy(run) == QA_OK, "FR019", "destroy w1");
    check(qaRunOpen(SCRATCH "/w3", 100, &run) == QA_OK, "FR013", "open w3");
    check(qaRunAppendTrace(run, 0, 0, 0, 1, 1, -4) == QA_OK, "FR013", "w3 row");
    check(qaRunWriteSnapshot(run, 7, 1, probs) == QA_OK, "FR013", "w3 snap");
    check(exists(SCRATCH "/w3/snapshots/snap_007.csv"), "FR013", "3 digits");
    check(qaRunDestroy(run) == QA_OK, "FR019", "destroy w3");

    /* Determinism: a second run gives byte-identical files (EC-013). */
    {
        char first[BUF_CAP];
        QaRun *again = NULL;

        check(readFile(SCRATCH "/w3/trace.csv", &len), "EC013", "read w3");
        /* Bound: `readFile` stored at most `BUF_CAP - 1` bytes, so
         * `len + 1u <= BUF_CAP` and the copy with its terminator fits `first`
         * (also `BUF_CAP` bytes). */
        memcpy(first, gFile, len + 1u);
        check(qaRunOpen(SCRATCH "/w3b", 100, &again) == QA_OK, "EC013", "open");
        check(qaRunAppendTrace(again, 0, 0, 0, 1, 1, -4) == QA_OK, "EC013",
              "row");
        check(qaRunDestroy(again) == QA_OK, "EC013", "destroy");
        check(readFile(SCRATCH "/w3b/trace.csv", &len) &&
                  strcmp(first, gFile) == 0,
              "EC013", "identical bytes");
    }
    removeTree(SCRATCH);
}

/* ---- group: config ---------------------------------------------------- */

/**
 * @brief FR-014: escaping, bounds and validation of `config.json`.
 *
 * @return Nothing; a failed check increments `gFailures` and the group continues.
 *
 * @owner Handles destroyed; scratch removed. @assumes Strings below the bound.
 */
static void testConfig(void)
{
    static char big[QA_IO_RUN_FIELD_MAX + 2u];
    QaRun *run = NULL;
    QaRunConfig cfg = makeConfig(4);
    JsonMember m[MAX_KEYS];
    size_t len;

    freshScratch();
    check(qaRunOpen(SCRATCH "/c", 4, &run) == QA_OK, "FR014", "open");
    check(qaRunAppendTrace(run, 0, 0, 0, 1, 1, -4) == QA_OK, "FR014", "row");

    /* Validation order: NULL, strings, n and steps, numbers. */
    check(qaRunWriteConfig(NULL, &cfg) == QA_ERR_DOMAIN, "FR014", "NULL run");
    check(qaRunWriteConfig(run, NULL) == QA_ERR_DOMAIN, "FR014", "NULL config");
    cfg.gitSha = NULL;
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_DOMAIN, "FR014", "NULL string");
    cfg = makeConfig(4);
    /* Bound: `big` holds `FIELD_MAX + 2u` bytes: `FIELD_MAX + 1u` filler bytes
     * (one over the limit) plus the terminator at index `FIELD_MAX + 1u`. */
    memset(big, 'x', QA_IO_RUN_FIELD_MAX + 1u);
    big[QA_IO_RUN_FIELD_MAX + 1u] = '\0';
    cfg.cmakeFlags = big;
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_RANGE, "FR014", "string too long");
    cfg = makeConfig(4);
    cfg.n = 5;
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_RANGE, "FR014", "n = 5");
    cfg.n = 1;
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_RANGE, "FR014", "n = 1");
    cfg = makeConfig(5);
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_RANGE, "FR014", "steps mismatch");
    cfg = makeConfig(4);
    cfg.dt = NAN;
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_DOMAIN, "FR014", "NaN dt");
    cfg = makeConfig(4);
    cfg.T = INFINITY;
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_DOMAIN, "FR014", "inf T");
    check(!exists(SCRATCH "/c/config.json") &&
              !exists(SCRATCH "/c/config.json.tmp"),
          "FR014", "rejected calls write nothing");

    /* Escaping: quote, backslash, tab, SOH and a UTF-8 byte pair. */
    cfg = makeConfig(4);
    cfg.cmakeFlags = "-DX=\"a\\b\"\tend\x01 \xc3\xa9";
    check(qaRunWriteConfig(run, &cfg) == QA_OK, "FR014", "config with escapes");
    check(readFile(SCRATCH "/c/config.json", &len), "FR014", "read");
    check(strstr(gFile, "\"cmake_flags\": \"-DX=\\\"a\\\\b\\\"\\u0009end\\u0001 "
                        "\xc3\xa9\"\n}\n") != NULL,
          "FR014", "escaped bytes");
    /* Bound: `m[12]` is read only when `parseJson` returned 13 members. */
    check(parseJson(gFile, m) == 13 && strcmp(m[12].value, cfg.cmakeFlags) == 0,
          "FR014", "independent parser recovers the string");
    check(qaRunDestroy(run) == QA_OK, "FR019", "destroy");

    /* A field of exactly the bound is accepted. Bound: index `FIELD_MAX` is
     * inside `big` (`FIELD_MAX + 2u` bytes); it shortens the string to exactly
     * `FIELD_MAX` characters. */
    big[QA_IO_RUN_FIELD_MAX] = '\0';
    cfg = makeConfig(4);
    cfg.family = big;
    check(qaRunOpen(SCRATCH "/c2", 4, &run) == QA_OK, "FR014", "open c2");
    check(qaRunAppendTrace(run, 0, 0, 0, 1, 1, -4) == QA_OK, "FR014", "row c2");
    check(qaRunWriteConfig(run, &cfg) == QA_OK, "FR014", "bound-sized field");
    check(qaRunWriteConfig(run, &cfg) == QA_ERR_DOMAIN, "FR014",
          "second config: trace already closed");
    check(qaRunDestroy(run) == QA_OK, "FR019", "destroy c2");
    removeTree(SCRATCH);
}

/* ---- group: order ----------------------------------------------------- */

/**
 * @brief FR-013 and FR-019: argument validation of open, append and snapshot.
 *
 * @return Nothing; a failed check increments `gFailures` and the group continues.
 *
 * @owner Handles destroyed; scratch removed. @assumes No numerics beyond NaN/inf.
 */
static void testOrder(void)
{
    static const double good[2] = {0.5, 0.5};
    static const double neg[2] = {0.5, -1e-9};
    static const double nan2[2] = {0.5, NAN};
    /* Sentinel: a non-NULL, never dereferenced pointer; every rejected open must
     * overwrite it with NULL, which proves `outRun` is cleared. */
    QaRun *run = (QaRun *)0x1;
    QaRunOps ops = *qaRunDefaultOps();

    freshScratch();
    check(qaRunOpen("x", 1, NULL) == QA_ERR_DOMAIN, "FR019", "NULL outRun");
    check(qaRunOpen(NULL, 1, &run) == QA_ERR_DOMAIN && run == NULL, "FR019",
          "NULL dir clears outRun");
    check(qaRunOpen("", 1, &run) == QA_ERR_RANGE && run == NULL, "FR019",
          "empty dir");
    check(qaRunOpen("a/", 1, &run) == QA_ERR_RANGE, "FR019", "trailing slash");
    check(qaRunOpen("a", 0, &run) == QA_ERR_RANGE && run == NULL, "FR019",
          "zero steps");
    ops.write = NULL;
    check(qaRunOpenWith("a", 1, &ops, &run) == QA_ERR_RANGE && run == NULL,
          "FR025", "ops entry NULL");
    check(qaRunOpenWith("a", 1, NULL, &run) == QA_ERR_RANGE, "FR025", "ops NULL");
    check(qaRunDestroy(NULL) == QA_OK, "FR019", "destroy NULL");

    check(qaRunOpen(SCRATCH "/o", 2, &run) == QA_OK, "FR013", "open");
    check(qaRunAppendTrace(NULL, 0, 0, 0, 1, 1, 0) == QA_ERR_DOMAIN, "FR013",
          "NULL run");
    check(qaRunAppendTrace(run, 1, 0, 0, 1, 1, 0) == QA_ERR_RANGE, "FR013",
          "first row must be 0");
    check(qaRunAppendTrace(run, 0, NAN, 0, 1, 1, 0) == QA_ERR_DOMAIN, "FR013",
          "NaN t");
    check(qaRunAppendTrace(run, 0, 0, INFINITY, 1, 1, 0) == QA_ERR_DOMAIN,
          "FR013", "inf a");
    check(!exists(SCRATCH "/o"), "FR013", "rejected rows create nothing");
    check(qaRunAppendTrace(run, 0, 0, 0, 1, 1, 0) == QA_OK, "FR013", "row 0");
    check(qaRunAppendTrace(run, 0, 0, 0, 1, 1, 0) == QA_ERR_RANGE, "FR013",
          "repeated row");
    check(qaRunAppendTrace(run, 2, 0, 0, 1, 1, 0) == QA_ERR_RANGE, "FR013",
          "skipped row");
    check(qaRunWriteSnapshot(run, 0, 2, NULL) == QA_ERR_DOMAIN, "FR013",
          "NULL probs");
    check(qaRunWriteSnapshot(run, 0, 0, good) == QA_ERR_RANGE, "FR013", "dim 0");
    check(qaRunWriteSnapshot(run, 3, 2, good) == QA_ERR_RANGE, "FR013",
          "j beyond steps");
    check(qaRunWriteSnapshot(run, 0, 2, neg) == QA_ERR_DOMAIN, "FR013",
          "negative probability");
    check(qaRunWriteSnapshot(run, 0, 2, nan2) == QA_ERR_DOMAIN, "FR013",
          "NaN probability");
    check(!exists(SCRATCH "/o/snapshots/snap_0.csv"), "FR013",
          "an invalid state produces no file");
    check(qaRunWriteSnapshot(run, 0, 2, good) == QA_OK, "FR013", "snap");
    check(qaRunWriteSnapshot(run, 0, 2, good) == QA_ERR_IO, "FR013",
          "exclusive creation: existing snapshot is never overwritten");
    check(qaRunDestroy(run) == QA_OK, "FR019", "destroy closes the open trace");
    check(!exists(SCRATCH "/o/config.json"), "FR014",
          "no config.json without a config call");
    removeTree(SCRATCH);

    /* `trace.csv` already present: the layout step refuses to overwrite. */
    check(mkdir(SCRATCH, 0777) == 0 && mkdir(SCRATCH "/p", 0777) == 0, "EC017",
          "fixture");
    check(writeFixture(SCRATCH "/p/trace.csv", "old\n"), "EC017", "old trace");
    check(qaRunOpen(SCRATCH "/p", 2, &run) == QA_OK, "EC017", "open");
    check(qaRunAppendTrace(run, 0, 0, 0, 1, 1, 0) == QA_ERR_IO, "EC017",
          "existing trace.csv is not overwritten");
    check(qaRunAppendTrace(run, 0, 0, 0, 1, 1, 0) == QA_ERR_DOMAIN, "EC017",
          "run is dead after a failed layout");
    check(readFile(SCRATCH "/p/trace.csv", &(size_t){0}) &&
              strcmp(gFile, "old\n") == 0,
          "EC017", "old content intact");
    check(qaRunDestroy(run) == QA_OK, "FR019", "destroy");
    removeTree(SCRATCH);
}

/* ---- group: failure --------------------------------------------------- */

/**
 * @brief Entries of `QaRunOps` that the fault sweep can fail, one per hook.
 *
 * `K_COUNT` is not an entry: it is the number of entries and sizes the
 * per-entry arrays (`Inject.calls`, `names`, `perKind`).
 *
 * @owner Plain values; nothing is allocated. @assumes `K_MAKEDIR` is 0 and the
 *        enumerators are contiguous, so they index arrays directly.
 */
typedef enum Kind {
    K_MAKEDIR = 0, /* `makeDir` */
    K_OPEN,        /* `open` */
    K_WRITE,       /* `write` (a failure is a short count of 0) */
    K_CLOSE,       /* `close` (the stream is still released) */
    K_RENAME,      /* `rename` */
    K_REMOVE,      /* `remove` (called only on failure paths) */
    K_COUNT        /* number of entries; not a valid `Kind` value */
} Kind;

/**
 * @brief Fault-injection context of the ops table (FR-025).
 *
 * Counts calls of each entry from 1 and fails the one whose index equals
 * `failAt` for the selected `kind` (`0` means never). `opened - closed` must be
 * 0 after any run: every stream the writer opened was closed.
 *
 * @owner Stack object of `testFailure`; the writer never frees it.
 * @assumes Single-threaded; `real` points at the default table, which outlives
 *          the test and is called with a NULL context.
 */
typedef struct Inject {
    const QaRunOps *real;
    Kind kind;           /* the entry to fail */
    unsigned failAt;     /* 1-based call number to fail, 0 = never */
    unsigned calls[K_COUNT];
    unsigned opened;     /* streams returned by `open` */
    unsigned closed;     /* streams passed to `close` */
    unsigned injected;   /* failures actually injected */
    int alsoFailRemove;  /* make every `remove` fail as well */
} Inject;

/**
 * @brief Count a call of `kind` and report whether it is the one to fail.
 *
 * @param[in,out] inj  Injection state.
 * @param[in]     kind Entry being called.
 *
 * @return 1 when this call must fail.
 *
 * @owner Nothing allocated. @assumes Single thread.
 */
static int shouldFail(Inject *inj, Kind kind)
{
    /* Bound: kind < K_COUNT, the length of calls[]. */
    inj -> calls[kind]++;
    if (inj -> kind == kind && inj -> failAt == inj -> calls[kind]) {
        inj -> injected++;
        return 1;
    }
    return 0;
}

/**
 * @brief Injecting `makeDir`: fails the selected call, else forwards.
 *
 * @param[in,out] ctx `Inject` state, non-NULL.
 * @param[in]     path Directory to create, as for `mkdir`.
 *
 * @return The libc result, or the failure value of the entry when injected.
 *
 * @owner Nothing is allocated; streams stay with the caller. @assumes Single thread.
 */
static int injMakeDir(void *ctx, const char *path)
{
    Inject *inj = ctx;

    return shouldFail(inj, K_MAKEDIR) ? -1 : inj -> real -> makeDir(NULL, path);
}

/**
 * @brief Injecting `open`: fails the selected call, else forwards and counts the stream.
 *
 * @param[in,out] ctx `Inject` state, non-NULL.
 * @param[in]     path File to open, as for `fopen`.
 * @param[in]     mode Open mode string, as for `fopen`.
 *
 * @return The libc result, or the failure value of the entry when injected.
 *
 * @owner Nothing is allocated; streams stay with the caller. @assumes Single thread.
 */
static FILE *injOpen(void *ctx, const char *path, const char *mode)
{
    Inject *inj = ctx;
    FILE *f;

    if (shouldFail(inj, K_OPEN)) {
        return NULL;
    }
    f = inj -> real -> open(NULL, path, mode);
    if (f != NULL) {
        inj -> opened++;
    }
    return f;
}

/**
 * @brief Injecting `write`: returns a short count (0) on the selected call, else forwards.
 *
 * @param[in,out] ctx `Inject` state, non-NULL.
 * @param[in]     buf    Source bytes, as for `fwrite`.
 * @param[in]     size   Element size in bytes.
 * @param[in]     count  Number of elements.
 * @param[in,out] stream Open output stream.
 *
 * @return The libc result, or the failure value of the entry when injected.
 *
 * @owner Nothing is allocated; streams stay with the caller. @assumes Single thread.
 */
static size_t injWrite(void *ctx, const void *buf, size_t size, size_t count,
                       FILE *stream)
{
    Inject *inj = ctx;

    if (shouldFail(inj, K_WRITE)) {
        return 0;
    }
    return inj -> real -> write(NULL, buf, size, count, stream);
}

/**
 * @brief Injecting `close`: always releases the stream, reports failure on the selected call.
 *
 * @param[in,out] ctx `Inject` state, non-NULL.
 * @param[in,out] stream Open stream, always closed by this call.
 *
 * @return The libc result, or the failure value of the entry when injected.
 *
 * @owner Nothing is allocated; streams stay with the caller. @assumes Single thread.
 */
static int injClose(void *ctx, FILE *stream)
{
    Inject *inj = ctx;
    int failed = shouldFail(inj, K_CLOSE);
    int rc = inj -> real -> close(NULL, stream); /* the stream is always released */

    inj -> closed++;
    return failed ? -1 : rc;
}

/**
 * @brief Injecting `rename`: fails the selected call, else forwards.
 *
 * @param[in,out] ctx `Inject` state, non-NULL.
 * @param[in]     from Existing path.
 * @param[in]     to   New path.
 *
 * @return The libc result, or the failure value of the entry when injected.
 *
 * @owner Nothing is allocated; streams stay with the caller. @assumes Single thread.
 */
static int injRename(void *ctx, const char *from, const char *to)
{
    Inject *inj = ctx;

    return shouldFail(inj, K_RENAME) ? -1 : inj -> real -> rename(NULL, from, to);
}

/**
 * @brief Injecting `remove`: fails the selected call (or every call), else forwards.
 *
 * @param[in,out] ctx `Inject` state, non-NULL.
 * @param[in]     path File to remove, as for `remove`.
 *
 * @return The libc result, or the failure value of the entry when injected.
 *
 * @owner Nothing is allocated; streams stay with the caller. @assumes Single thread.
 */
static int injRemove(void *ctx, const char *path)
{
    Inject *inj = ctx;

    if (shouldFail(inj, K_REMOVE) || inj -> alsoFailRemove) {
        return -1;
    }
    return inj -> real -> remove(NULL, path);
}

/**
 * @brief Drive a whole run through `ops`, stopping at the first failure.
 *
 * Sequence: open, rows 0..2, snapshots 0 and 2, config, destroy.
 *
 * @param[in]  ops       Operations table under test.
 * @param[out] outFailed Set to 1 when a writer call returned non-`QA_OK`.
 *
 * @return The status of the first failing call, or `QA_OK`.
 *
 * @owner The handle is destroyed before returning on every path.
 * @assumes The scratch tree does not exist on entry.
 */
static QaStatus drive(const QaRunOps *ops, int *outFailed)
{
    static const double p[2] = {0.5, 0.5};
    QaRunConfig cfg = makeConfig(2);
    QaRun *run = NULL;
    QaStatus st = qaRunOpenWith(SCRATCH "/r", 2, ops, &run);

    *outFailed = 0;
    if (st != QA_OK) {
        *outFailed = 1;
        return st;
    }
    st = qaRunAppendTrace(run, 0, 0, 0, 1, 1, -4);
    if (st == QA_OK) {
        /* Bound: dim 2 equals the length of p[2]. */
        st = qaRunWriteSnapshot(run, 0, 2, p);
    }
    if (st == QA_OK) {
        st = qaRunAppendTrace(run, 1, 0.5, 0.5, 0.5, 1, -3);
    }
    if (st == QA_OK) {
        st = qaRunAppendTrace(run, 2, 1, 1, 0, 1, 0);
    }
    if (st == QA_OK) {
        /* Bound: dim 2 equals the length of p[2]. */
        st = qaRunWriteSnapshot(run, 2, 2, p);
    }
    if (st == QA_OK) {
        st = qaRunWriteConfig(run, &cfg);
    }
    *outFailed = st != QA_OK;
    (void)qaRunDestroy(run);
    return st;
}

/**
 * @brief Allocation-seam state shared by `countAlloc` and `countRelease`.
 *
 * @owner Stack object of `testHandleAlloc`; the run handle never frees it.
 * @assumes Single-threaded; `live` is 0 once every block was released.
 */
typedef struct AllocState {
    int calls;  /* `alloc` calls seen so far, counted from 1 */
    int live;   /* blocks handed out and not yet released */
    int failAt; /* 1-based call number that returns NULL, 0 = never */
} AllocState;

/**
 * @brief Counting `alloc` entry that fails at the `failAt`-th call (FR-025).
 *
 * @param[in,out] ctx  `AllocState *`, non-NULL; counters are updated.
 * @param[in]     size Block size in bytes.
 *
 * @return A block from `malloc`, or NULL on the injected call.
 *
 * @owner The block is released by `countRelease`. @assumes Single thread.
 */
static void *countAlloc(void *ctx, size_t size)
{
    AllocState *st = ctx;
    void *ptr;

    st -> calls++;
    if (st -> calls == st -> failAt) {
        return NULL;
    }
    ptr = malloc(size);
    if (ptr != NULL) {
        st -> live++;
    }
    return ptr;
}

/**
 * @brief Counting `release` entry paired with `countAlloc`.
 *
 * @param[in,out] ctx `AllocState *`, non-NULL; `live` is decremented.
 * @param[in]     ptr Block from `countAlloc`, or NULL (ignored).
 *
 * @return Nothing.
 *
 * @owner Takes ownership of `ptr` and frees it. @assumes Single thread.
 */
static void countRelease(void *ctx, void *ptr)
{
    AllocState *st = ctx;

    if (ptr != NULL) {
        st -> live--;
    }
    free(ptr);
}

/**
 * @brief FR-019, FR-025: the handle allocation fails (`QA_ERR_NOMEM`), and a
 * successful handle is released through the table by `qaRunDestroy`.
 *
 * @return Nothing; failures are counted by `check`.
 *
 * @owner Handles destroyed before returning. @assumes Single thread; no file is
 *        created because `qaRunOpenWith` touches no file system.
 */
static void testHandleAlloc(void)
{
    AllocState st = {0, 0, 1};
    QaRunOps ops = *qaRunDefaultOps();
    /* Sentinel: never dereferenced; `run == NULL` after a failed open proves
     * that `outRun` was cleared. */
    QaRun *run = (QaRun *)0x1;

    ops.ctx = &st;
    ops.alloc = countAlloc;
    ops.release = countRelease;
    check(qaRunOpenWith("a", 1, &ops, &run) == QA_ERR_NOMEM && run == NULL,
          "FR019", "handle alloc failure returns NOMEM and clears outRun");
    check(st.calls == 1 && st.live == 0, "FR019", "failed alloc holds nothing");

    st.failAt = 0;
    check(qaRunOpenWith("a", 1, &ops, &run) == QA_OK && run != NULL, "FR025",
          "open through the alloc seam");
    check(st.live == 1, "FR019", "one live block while the handle exists");
    check(qaRunDestroy(run) == QA_OK && st.live == 0, "FR019",
          "destroy releases the handle through the seam");

    ops.alloc = NULL;
    check(qaRunOpenWith("a", 1, &ops, &run) == QA_ERR_RANGE && run == NULL,
          "FR025", "NULL alloc entry");
    ops.alloc = countAlloc;
    ops.release = NULL;
    check(qaRunOpenWith("a", 1, &ops, &run) == QA_ERR_RANGE && run == NULL,
          "FR025", "NULL release entry");
}

/**
 * @brief FR-019, FR-025, EC-009: sweep a failure over every call of each entry.
 *
 * For each entry kind and each call number k the k-th call fails. The run must
 * return `QA_ERR_IO`, close every stream it opened, and leave no `config.json`.
 * The sweep stops for a kind when k exceeds the number of calls (a clean run).
 *
 * @return Nothing; a failed check increments `gFailures` and the group continues. `drive` and the
 *         injected sweep never exit early.
 *
 * @owner Scratch tree removed after every run. @assumes The clean run of the
 *        scenario makes fewer than 1000 calls per kind.
 */
static void testFailure(void)
{
    static const char *const names[K_COUNT] = {"makeDir", "open", "write",
                                               "close",   "rename", "remove"};
    unsigned total = 0;
    unsigned perKind[K_COUNT] = {0};

    for (unsigned kind = 0; kind < K_COUNT; kind++) {
        for (unsigned k = 1; k < 1000u; k++) {
            Inject inj;
            QaRunOps ops = *qaRunDefaultOps();
            int failed = 0;
            QaStatus st;

            memset(&inj, 0, sizeof inj);
            inj.real = qaRunDefaultOps();
            inj.kind = (Kind)kind;
            inj.failAt = k;
            /* `remove` is only called on failure paths, so it is swept together
             * with a failing `rename`, which is the path that calls it. */
            if (kind == K_REMOVE) {
                inj.kind = K_RENAME;
                inj.failAt = 1;
                inj.alsoFailRemove = 1;
            }
            ops.ctx = &inj;
            ops.makeDir = injMakeDir;
            ops.open = injOpen;
            ops.write = injWrite;
            ops.close = injClose;
            ops.rename = injRename;
            ops.remove = injRemove;

            freshScratch();
            st = drive(&ops, &failed);

            check(inj.opened == inj.closed, "FR019",
                  "every opened stream is closed exactly once");
            if (kind == K_REMOVE) {
                check(failed && st == QA_ERR_IO, "FR019",
                      "rename failure with failing remove");
                check(!exists(SCRATCH "/r/config.json"), "FR014",
                      "no config.json when remove also fails");
                perKind[kind]++;
                total++;
                break;
            }
            if (inj.injected == 0) {
                /* Clean run: the sweep for this entry is complete. */
                check(!failed && st == QA_OK, "FR014", "clean run succeeds");
                check(exists(SCRATCH "/r/config.json"), "FR014",
                      "clean run has config.json");
                check(!exists(SCRATCH "/r/config.json.tmp"), "FR014",
                      "clean run leaves no temporary file");
                if (k == 1 && kind != K_REMOVE) {
                    /* Bound: kind < K_COUNT, the length of names[]. */
                    printf("note: %s is never called\n", names[kind]);
                }
                break;
            }
            perKind[kind]++;
            total++;
            check(failed && st == QA_ERR_IO, "EC009",
                  "an injected failure returns QA_ERR_IO");
            check(!exists(SCRATCH "/r/config.json"), "EC009",
                  "no config.json after a failure");
            if (kind != K_REMOVE) {
                check(!exists(SCRATCH "/r/config.json.tmp") ||
                          inj.kind == K_REMOVE,
                      "FR019", "temporary file removed after a failure");
            }
        }
    }
    removeTree(SCRATCH);
    printf("failure sweep: %u injected failures (makeDir %u, open %u, write %u, "
           "close %u, rename %u, remove %u)\n",
           total, perKind[0], perKind[1], perKind[2], perKind[3], perKind[4],
           perKind[5]);
    check(perKind[K_MAKEDIR] == 2 && perKind[K_OPEN] == 4 &&
              perKind[K_WRITE] >= 8 && perKind[K_CLOSE] == 4 &&
              perKind[K_RENAME] == 1 && perKind[K_REMOVE] == 1,
          "FR025", "every entry was exercised");
    testHandleAlloc();
}

/**
 * @brief One selectable test group of `main`.
 *
 * @owner Static table in `main`; nothing is allocated.
 * @assumes `name` is a literal and `run` is non-NULL.
 */
typedef struct Group {
    const char *name; /* group name accepted on the command line */
    void (*run)(void); /* group entry point; failures counted in `gFailures` */
} Group;

/**
 * @brief Run the selected group, or all groups without an argument.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Optional group name.
 *
 * @return 0 when every check passed, 1 on any failure, 2 for an unknown group.
 *
 * @owner Nothing allocated. @assumes `argv` has `argc` entries.
 */
int main(int argc, char **argv)
{
    static const Group groups[] = {{"outdir", testOutDir},
                                   {"layout", testLayout},
                                   {"config", testConfig},
                                   {"order", testOrder},
                                   {"failure", testFailure}};
    int ran = 0;

    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        if (argc < 2 || strcmp(argv[1], groups[i].name) == 0) {
            groups[i].run();
            ran = 1;
        }
    }
    if (!ran) {
        fprintf(stderr, "usage: %s [outdir|layout|config|order|failure]\n",
                argv[0]);
        return 2;
    }
    if (gFailures != 0) {
        printf("%d check(s) failed\n", gFailures);
        return 1;
    }
    printf("OK\n");
    return 0;
}
