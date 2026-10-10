#ifndef QA_CLI_EVOLVE_CLI_H
#define QA_CLI_EVOLVE_CLI_H

/*
 * Logic of the evolution CLI `qa-005-evolve` (spec 005-evolution, T-014).
 *
 * Scope: FR-006 (a failing norm check exits 5 with no `config.json`), FR-013
 * (callbacks stream `trace.csv` and the snapshot files to the run handle),
 * FR-014 (`config.json` written last), FR-015 (arguments, summary, exit codes),
 * EC-007, EC-009, EC-016 (strict parsing, exit 64), EC-017 (`--out` rules) and
 * EC-020 (norm failure through the test-only perturb hook). Private header:
 * it is not installed and is included only by `src/cli/qa-005-evolve.c`, by
 * `src/cli/qa-005-evolve-cli.c` and by the CLI tests, which add
 * `src/evolution` to their include path for the hook table.
 *
 * Command line (exactly eight words, no option may be omitted or reordered):
 *   qa-005-evolve <N> <family> <T> <steps> <M> --out <dir>
 *   N, steps, M   decimal unsigned integers, no sign, space or trailing text
 *   family        one of the 003 names: linear, trigonometric, quadratic,
 *                 exponential
 *   T             a real parsed by `strtod`, no leading space, no trailing text;
 *                 `nan`, `inf` and values `<= 0` parse and are rejected later by
 *                 the library (exit 5, EC-016)
 *   <dir>         the run directory, checked by `qaRunCheckOutDir` (EC-017)
 *
 * Exit codes: 0 success; 1..6 the numeric `QaStatus` of the first failing step;
 * 64 usage error (EC-016), distinct from every `QaStatus`.
 *
 * Numerical bounds: `N` above 4 is passed to the library, which owns the
 * `N`-gate; for `N` in 2..4 the state length is `dim = 2^(N N)`, at most 65536.
 */

#include <stdio.h>

#include "qa/evolution/evolve.h"
#include "evolve-internal.h"

/** Exit status of every usage error (EC-016, sysexits `EX_USAGE`). */
#define QA_CLI_EVOLVE_EXIT_USAGE 64

/**
 * @brief Run the evolution CLI and return its exit status (FR-015).
 *
 * Fixed order, nothing is created before step 4:
 *   0. A non-NULL `ops` with a NULL `alloc` or `release` -> 1 (QA_ERR_RANGE),
 *      checked first because the CLI calls them itself (step 3).
 *   1. Argument shape and strict parsing -> usage text on `err`, 64 (EC-016).
 *   2. `--out` rules, `qaRunCheckOutDir` -> its code (EC-017, FR-015).
 *   3. Run handle and, for `N` in 2..4, the two state buffers and the 004
 *      initial state; an invalid `N` reaches the library with no buffers so its
 *      `N`-gate answers (FR-016).
 *   4. `qaEvolveWith`: the library validates (FR-020) and only then calls the
 *      observer at `j = 0`, which creates the run layout (EC-007, EC-015).
 *   5. `config.json` last, then the summary on `out` (FR-014, FR-015).
 * Every failure after step 1 prints one line on `err`, releases everything and
 * returns the numeric `QaStatus`; partial record files stay and the missing
 * `config.json` marks the run invalid (FR-013).
 *
 * @param[in]  argc Word count of `argv`, including the program name.
 * @param[in]  argv `argc` NUL-terminated words; read only.
 * @param[out] out  Non-NULL stream for the summary of a successful run.
 * @param[out] err  Non-NULL stream for the usage text and error lines.
 * @param[in]  ops  Hook table for the library and for the CLI's own buffers,
 *                  or NULL for the production table (libc, 003, no perturb).
 *
 * @return 0 on success; `64` on a usage error; otherwise the `QaStatus` value
 *         (1..6) of the failing step.
 *
 * @owner The CLI owns the run handle and the two state buffers and releases them
 *        on its single cleanup path; `argv`, `out`, `err` and `ops` stay owned by
 *        the caller.
 * @assumes Deterministic: equal arguments give byte-identical records on the same
 *          build (EC-013). One thread.
 */
int qaCliEvolveRun(int argc, char **argv, FILE *out, FILE *err,
                   const QaEvolveOps *ops);

#endif /* QA_CLI_EVOLVE_CLI_H */
