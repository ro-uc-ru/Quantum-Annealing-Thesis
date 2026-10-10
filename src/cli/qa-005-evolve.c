/*
 * qa-005-evolve (spec 005-evolution, task T-014; FR-015, EC-016).
 *
 * Purpose: thin `main` of the evolution CLI. Usage:
 * `qa-005-evolve <N> <family> <T> <steps> <M> --out <dir>`; every decision
 * lives in `qaCliEvolveRun` (`src/cli/qa-005-evolve-cli.c`) so the tests drive
 * the same path. The representative run is documented in `specs/active-spec.md`.
 *
 * Ownership: no allocation here. Errors: the exit status is 0 on success, the
 * numeric `QaStatus` (1..6) on an evolution or I/O error and 64 on a usage error.
 * Numerical assumptions: none; no value is computed.
 */

#include <stdio.h>

#include "qa-005-evolve-cli.h"

/**
 * @brief Run the evolution CLI with the production hook table.
 *
 * @param[in] argc Word count, including the program name.
 * @param[in] argv Words, owned by the C runtime.
 *
 * @return The exit status of `qaCliEvolveRun`.
 *
 * @owner No allocation; nothing to release.
 * @assumes `stdout` and `stderr` are open, as the C runtime guarantees.
 */
int main(int argc, char **argv)
{
    return qaCliEvolveRun(argc, argv, stdout, stderr, NULL);
}
