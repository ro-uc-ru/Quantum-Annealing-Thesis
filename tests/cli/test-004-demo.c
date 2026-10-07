/*
 * 004-driver demo tests (spec 004-driver, task T-013: EC-020, EC-025).
 *
 * Purpose: three checks around `qa-004-demo`, selected by the first argument.
 *   `readback <csv>`            parse the CSV the demo wrote and require
 *                               exactly the FR-014 header and one row with
 *                               `v1`, `4`, `65536`, `4:psi0`, `N/A` seed,
 *                               schedule and dt_steps, and non-empty
 *                               git_sha, clang_version and cmake_flags.
 *   `exit-status <demo> <path>` run the demo with a CSV path in a directory
 *                               that does not exist and require a normal
 *                               exit with a non-zero code and no file.
 *   `usage-status <demo>`       run the demo with two path arguments (the
 *                               usage-error path) and require a normal exit
 *                               with a non-zero code.
 *
 * Independence: `parseCsv` is a small RFC 4180 reader that shares no code
 * with the writer. Ownership: static buffers only. Errors: a failed check is
 * printed with its EC tag and the process exits 1; a usage error exits 2.
 * Determinism: no clock or RNG; the only inputs are the named files.
 * Numerical assumptions: none; every comparison is on text, never on floating
 * point values.
 */

#define _POSIX_C_SOURCE 200809L /* WIFEXITED / WEXITSTATUS */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define BUF_CAP (64u * 1024u)
#define NUM_COLS 10u
#define MAX_RECORDS 4u

#define EXPECTED_HEADER \
    "spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed," \
    "schedule,dt_steps"

static char gFile[BUF_CAP];
static char gStorage[BUF_CAP];
static const char *gCells[MAX_RECORDS][NUM_COLS];

/**
 * @brief Split `buf` into records of exactly `NUM_COLS` RFC 4180 cells.
 *
 * Quoted cells may hold commas, CR, LF and doubled quotes; cell text goes to
 * `gStorage`, pointers to `gCells`.
 *
 * @param[in] buf NUL-terminated CSV text, shorter than `BUF_CAP`.
 * @return Record count, or -1 when the input is malformed or too large.
 *
 * @owner Writes only the static buffers. @assumes `buf` is NUL-terminated and
 *        shorter than `BUF_CAP`; every copy is checked against the storage.
 */
static int parseCsv(const char *buf)
{
    size_t used = 0;
    size_t rec = 0;
    size_t col = 0;
    const char *p = buf;

    while (*p != '\0') {
        const char *start = &gStorage[used];

        if (rec >= MAX_RECORDS || col >= NUM_COLS) {
            return -1;
        }
        if (*p == '"') {
            p++;
            for (;;) {
                /* Bound: `used + 2 < BUF_CAP` leaves room for this byte and
                 * the cell's closing NUL in `gStorage`. */
                if (*p == '\0' || used + 2u >= BUF_CAP) {
                    return -1;
                }
                if (*p == '"') {
                    /* Read bound: `*p == '"'` here, so `p[1]` is at worst the
                     * terminating NUL. */
                    if (p[1] != '"') {
                        p++;
                        break;
                    }
                    p++;
                }
                gStorage[used++] = *p++;
            }
        } else {
            while (*p != '\0' && *p != ',' && *p != '\n') {
                /* Bound: same `used + 2 < BUF_CAP` guard as above. */
                if (*p == '"' || *p == '\r' || used + 2u >= BUF_CAP) {
                    return -1;
                }
                gStorage[used++] = *p++;
            }
        }
        /* Write bounds: `used + 2 < BUF_CAP` (re-checked here, since an empty
         * cell runs no copy guard) leaves room for the NUL in `gStorage`;
         * `rec < MAX_RECORDS` and `col < NUM_COLS` were checked at the top of
         * this iteration, so `gCells[rec][col]` is in range. */
        if (used + 2u >= BUF_CAP) {
            return -1;
        }
        gStorage[used++] = '\0';
        gCells[rec][col++] = start;
        if (*p == ',') {
            p++;
        } else if (*p == '\n') {
            if (col != NUM_COLS) {
                return -1;
            }
            p++;
            rec++;
            col = 0;
        } else {
            return -1;
        }
    }
    /* Cast bound: `rec <= MAX_RECORDS == 4`, so the conversion is lossless. */
    return col == 0 ? (int)rec : -1;
}

/**
 * @brief Report one failed check; returns 1 for easy accumulation.
 *
 * @param[in] what Description printed after `FAIL:`; must be a valid string.
 * @return Always 1.
 *
 * @owner Nothing is allocated; `what` stays with the caller.
 * @assumes `what` is NUL-terminated; no numerical assumptions.
 */
static int failed(const char *what)
{
    printf("FAIL: %s\n", what);
    return 1;
}

/**
 * @brief `readback` mode (EC-020).
 *
 * @param[in] path CSV file written by the demo.
 * @return Number of failed checks.
 *
 * @owner Opens and closes the file inside the call.
 * @assumes The file is shorter than `BUF_CAP`; a longer one is truncated and
 *          then fails the header or record checks.
 */
static int readBack(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t len;
    int records;
    int failures = 0;

    if (f == NULL) {
        return failed("TEST-004-driver-EC020 CSV missing");
    }
    /* Bound: `fread` reads at most `BUF_CAP - 1` bytes, so `len <= BUF_CAP - 1`
     * and the NUL below stays inside `gFile`. */
    len = fread(gFile, 1, BUF_CAP - 1u, f);
    (void)fclose(f);
    gFile[len] = '\0';

    /* `sizeof EXPECTED_HEADER` counts the literal's NUL, which covers the
     * concatenated `\n` of the comparand, so header and newline are checked. */
    if (strncmp(gFile, EXPECTED_HEADER "\n", sizeof EXPECTED_HEADER) != 0) {
        failures += failed("TEST-004-driver-EC020 exact header");
    }
    records = parseCsv(gFile);
    if (records != 2) {
        return failures + failed("TEST-004-driver-EC020 header plus one row");
    }
    /* Bound: `records == 2` (checked above) makes row 1 valid, and every
     * `c` below stays under `NUM_COLS`. */
    if (strcmp(gCells[1][0], "v1") != 0 || strcmp(gCells[1][1], "4") != 0 ||
        strcmp(gCells[1][2], "65536") != 0 ||
        strcmp(gCells[1][3], "4:psi0") != 0) {
        failures += failed("TEST-004-driver-EC020 spec_version, N, dim, vectors");
    }
    for (size_t c = 7; c < NUM_COLS; c++) {
        if (strcmp(gCells[1][c], "N/A") != 0) {
            failures += failed("TEST-004-driver-EC020 seed/schedule/dt_steps");
        }
    }
    for (size_t c = 4; c < 7; c++) {
        if (gCells[1][c][0] == '\0') {
            failures += failed("TEST-004-driver-EC020 build field empty");
        }
    }
    return failures;
}

/**
 * @brief `exit-status` mode (EC-025): the demo must exit non-zero when the
 * writer fails.
 *
 * @param[in] demo Path of the `qa-004-demo` executable.
 * @param[in] path CSV path handed to the demo; no file may exist afterwards.
 * @return Number of failed checks.
 *
 * @owner The shell command lives on the stack; no resource is kept.
 * @assumes `demo` and `path` contain no single quote.
 */
static int exitStatus(const char *demo, const char *path)
{
    char cmd[2048];
    int rc;
    FILE *f;

    /* Size bound: `snprintf` truncates at `sizeof cmd`; a result at or above
     * it is rejected before `system` runs. */
    if (snprintf(cmd, sizeof cmd, "'%s' '%s' 2>/dev/null", demo, path) >=
        (int)sizeof cmd) {
        return failed("TEST-004-driver-EC025 command too long");
    }
    rc = system(cmd);
    if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) == 0) {
        return failed("TEST-004-driver-EC025 demo must exit non-zero");
    }
    f = fopen(path, "rb");
    if (f != NULL) {
        (void)fclose(f);
        return failed("TEST-004-driver-EC025 no file may exist");
    }
    return 0;
}

/**
 * @brief Run the usage-error path of the demo (two path arguments).
 *
 * @param[in] demo Path of the `qa-004-demo` executable, without single quote.
 * @return Number of failed checks.
 *
 * @owner The shell command lives on the stack; no resource is kept.
 * @assumes `demo` fits the command buffer; the demo exits 1 on `argc > 2`
 *          before it writes any file.
 */
static int usageStatus(const char *demo)
{
    char cmd[2048];
    int rc;

    /* Size bound: `snprintf` truncates at `sizeof cmd`; a result at or above
     * it is rejected before `system` runs. */
    if (snprintf(cmd, sizeof cmd, "'%s' a b >/dev/null 2>&1", demo) >=
        (int)sizeof cmd) {
        return failed("TEST-004-driver-EC025 command too long");
    }
    rc = system(cmd);
    if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) == 0) {
        return failed("TEST-004-driver-EC025 usage error must exit non-zero");
    }
    return 0;
}

/**
 * @brief Select the check from the command line and report the result.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Arguments; see the file header for the accepted forms.
 * @return 0 when every check passes, 1 on a failed check, 2 on a usage error.
 *
 * @owner No resource is kept.
 * @assumes `argv[argc - 1]` strings are valid; no numerical assumptions.
 */
int main(int argc, char **argv)
{
    int failures;

    /* Bound: each branch reads only `argv[1..argc-1]`, and its `argc ==` test
     * guarantees those entries exist. */
    if (argc == 3 && strcmp(argv[1], "readback") == 0) {
        failures = readBack(argv[2]);
    } else if (argc == 4 && strcmp(argv[1], "exit-status") == 0) {
        failures = exitStatus(argv[2], argv[3]);
    } else if (argc == 3 && strcmp(argv[1], "usage-status") == 0) {
        failures = usageStatus(argv[2]);
    } else {
        printf("usage: test-004-demo readback <csv> | exit-status <demo> "
               "<path> | usage-status <demo>\n");
        return 2;
    }
    if (failures != 0) {
        return 1;
    }
    printf("OK\n");
    return 0;
}
