current active spec: 005-evolution

## 005-evolution measurements (task T-017, recorded 2026-10-10, Debug build, Apple Silicon)

Representative run (3x3, linear, `T = 100`, `steps = 10^4`, `M = 1000`, 11 snapshots):

    ./build/qa-005-evolve 3 linear 100 10000 1000 --out <dir>

gives `final_energy -0.99327478938501756`, `max_norm_drift 2.93e-14`.

Norm drift (FR-005, `NORM_TOL = 1e-12`), `max |norm - 1|` over every step, from the run summary:

| Board | Command (`--out <dir>`)                          | max_norm_drift | final_energy        |
|-------|--------------------------------------------------|----------------|---------------------|
| 3x3   | `qa-005-evolve 3 linear 100 1000000 0` (STEPS_MAX) | 3.22e-13       | -0.99327476387374   |
| 3x3   | `qa-005-evolve 3 linear 100 10000 1000`          | 2.93e-14       | -0.99327478938502   |
| 2x2   | `qa-005-evolve 2 linear 100 10000 100`           | 1.42e-14       | -0.48905162505466   |

Both bounds hold: `|norm - 1| <= 1e-12` at every step for 2x2 and 3x3, also at
`STEPS_MAX`, so `STEPS_MAX = 10^6` stays unchanged.

4x4 run (EC-004, non-blocking): `qa-005-evolve 4 linear 100 10000 1000 --out <dir>`
completed with exit 0 in about 81 s (Debug, one thread): `final_energy -1.8341681802552`,
`max_norm_drift 6.24e-14` (norm bound kept). `P_gs` (ground set = the two 4x4
solutions, ids 16770 and 10260, `H_target = -2`) = 0.67492313 (0.33746157 each);
not asserted, `T = 100` is not adiabatic for 4x4.

Independent reference script: `tests/reference/proto.py` (moved from
`specs/005-evolution/reference/`; not run by CTest).

## 005-evolution MEM-4 exceptions (task T-019, recorded 2026-10-11, user-approved)

Constitution MEM-4 asks for a failure-path test on every failure path. The
following branches have none; each routes through `cleanup` and releases every
resource, and the public signature `qaCliEvolveRun` stays unchanged (no run-ops
seam is added to the CLI entry point). Rationale per group:

- Unreachable after validation (defensive, kept for ERR-2): `qaHamiltonianInitialState`
  in `src/cli/qa-005-evolve-cli.c`; `qaHamiltonianTarget` and `qaEvolutionRx` in
  `src/evolution/evolve.c`; `qaHamiltonianTarget` in `qaPrimitivesMaxTarget` and
  `qaEvolutionDz` (`src/evolution/primitives.c`); `qaScheduleName` in the CLI
  (the family was already resolved by the argument parser); the status of
  `qaRunDestroy` in the CLI (the trace is already closed by `qaRunWriteConfig`).
- Reachable only through the run writers, not through the CLI entry point: the
  `qaRunOpen` NOMEM branch and the `qaRunWriteConfig` failure branch of
  `qaCliEvolveRun`. The CLI always uses the libc `QaRunOps`; the same failures
  are covered at the library level by `tests/io/test-run.c` (`testHandleAlloc`
  for NOMEM, and the `testFailure` sweep over `makeDir`, `open`, `write`,
  `close`, `rename`, `remove`).
