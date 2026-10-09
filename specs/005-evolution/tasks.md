# Tasks: 005-evolution

- Status: approved
- Parent: `spec.md` (approved) + `plan.md` (approved). CTest entries are named
  `005-evolution-<group>`. "Done when" always includes a zero-warning strict
  build; implementation tasks also keep 001-004 tests green.

## Phase 1 - Headers and wiring

- [ ] T-001 Write `include/qa/hamiltonian/target.h` (scope comment with `r = 0.5`
  citing this spec, `H_target(k) = E(k) - r q(k)`, numbered validation order of
  FR-027, `@owner`, `@assumes`). Covers: FR-001, FR-016, FR-027. Done when:
  `clang -std=c17 -fsyntax-only -I include` passes on it and
  `scripts/check-header-docs.sh` passes.
- [ ] T-002 [P] Write `include/qa/evolution/primitives.h` (`Dz`, `Rx`, numbered
  validation order, closed forms of FR-022 in the scope comment). Covers:
  FR-003, FR-004, FR-027, EC-014, EC-021. Done when: `-fsyntax-only` and
  `check-header-docs.sh` pass on it.
- [ ] T-003 [P] Write `include/qa/evolution/evolve.h` (`time_node`, `time_mid`,
  `STEPS_MAX`, `NORM_TOL`, snapshot bounds, callback types, `qaEvolve`,
  FR-020 numbered order, callback order). Covers: FR-002, FR-006, FR-010,
  FR-011, FR-012, FR-017, FR-018, FR-020, FR-024, FR-026, FR-028. Done when:
  `-fsyntax-only` and `check-header-docs.sh` pass on it.
- [ ] T-004 [P] Write `include/qa/io/run.h` (run handle, `--out` check, trace,
  snapshot and `config.json` writers, section 4 formats in the scope comment)
  and the private `src/io/run-internal.h` and `src/evolution/evolve-internal.h`
  (ops tables). Covers: FR-013, FR-014, FR-019, FR-025, EC-017. Done when: the
  public header passes `-fsyntax-only` and `check-header-docs.sh`, and the
  private headers compile after their public ones.
- [ ] T-005 Wire CMake: `qa_evolution` library (placeholder sources), run
  writers placeholder in `qa_io`, `qa_cli_evolve` library and `qa-005-evolve`
  executable placeholders, the new headers in the standalone and docs gates
  (`005-evolution-` prefix), and a header test binary (guard,
  self-containment, exact signatures). Covers: FR-016, FR-021. Done when: Debug
  configure and build pass with zero warnings and `ctest -R 005-evolution-header`
  passes. After: T-001, T-002, T-003, T-004

## Phase 2 - H_target

- [ ] T-006 Implement `src/hamiltonian/target.c` (N-gate, NULL, `k >= dim`,
  own static `E(k)` and `q(k)`) with `tests/hamiltonian/test-target.c`: values,
  cross-check against `qaHamiltonianApplyProblem` on basis states, reference
  ground sets (2x2: 4 at -0.5; 3x3: 8 at -1; 4x4: 2 at -2), validation order.
  Covers: FR-001, FR-016, FR-027. Done when: `ctest -R 005-evolution-target`
  passes. After: T-005

## Phase 3 - Primitives and time grid

- [ ] T-007 Implement `Dz` in `src/evolution/primitives.c` (N-gate, NULL,
  finite `s`, `a`, overflow pre-check of `s a max|H_target|`, in-place phases,
  state unchanged on error) with its tests in
  `tests/evolution/test-primitives.c`: closed form `exp(-i s a H_target(k))` on
  `|k>`, validation order, EC-021 with `s = DBL_MAX`. Covers: FR-004, FR-022,
  FR-027, EC-021. Done when: `ctest -R 005-evolution-dz` passes. After: T-006
- [ ] T-008 Implement `Rx` in the same file (per-cell butterfly passes, ascending
  `c`) with tests: phase `exp(+i numCells theta)` on `|->^numCells`, amplitude
  `cos(theta)^numCells` on `|0...0>`, large `theta` keeps the norm, validation
  order, unchanged state on error. Covers: FR-003, FR-022, FR-027, EC-014,
  EC-021. Done when: `ctest -R 005-evolution-rx` passes. After: T-007
- [ ] T-009 [P] Implement `time_node` and `time_mid` in `src/evolution/timegrid.c`
  with `tests/evolution/test-timegrid.c`: exact `T` at `j = steps`, clamping,
  `T = 0.1, steps = 3`, `T = 1e300` and smallest normal with `steps = 10^6`
  (finite, non-decreasing, `t_j <= tm_j <= t_{j+1}`), subnormal `T` gives
  `QA_ERR_DOMAIN`, error codes and untouched out-param. Covers: FR-017, FR-026,
  EC-018, EC-019. Done when: `ctest -R 005-evolution-timegrid` passes.
  After: T-005

## Phase 4 - Integrator

- [ ] T-010 Implement in `src/evolution/evolve.c` the validation (FR-020 order,
  including `STEPS_MAX`, snapshot count and rows, overlap, initial-state
  finiteness and norm) and the internal pairwise norm helper, with tests
  `n-gate`, `validation-order`, `bounds` and `initial-state` in
  `tests/evolution/test-evolve.c` (no evolution yet; returns after validation).
  Covers: FR-016, FR-017, FR-018, FR-020, FR-024, EC-002, EC-007, EC-008,
  EC-011, EC-015. Done when: `ctest -R 005-evolution-validate` passes. After:
  T-003, T-006, T-009
- [ ] T-011 Implement the Strang loop, the three owned buffers with one
  `cleanup`, observer and sink calls, FR-006 check, max drift, final `<H>` and
  copy-out. Tests: `steps = 1`, norm bound on 2x2 and 3x3, snapshot counts for
  `M = 0`, `M > steps`, `M = 1`, 16 rows for 2x2, callback order and error
  propagation, output unchanged on failure, bit-identical repeats. Covers:
  FR-002, FR-005, FR-006, FR-008, FR-009, FR-010, FR-011, FR-012, FR-021,
  FR-028, EC-001, EC-003, EC-005, EC-006. Done when:
  `ctest -R 005-evolution-evolve` passes. After: T-008, T-010
- [ ] T-012 Failure-path tests through the private ops table: one test per
  allocation point, schedule-evaluation failure at `tm_j` and at a trace node,
  injected norm failure at step `j` (perturb hook), each checking release of
  acquired buffers, unchanged output and no callback for the failing step.
  Covers: FR-006, FR-019, FR-025, EC-010, EC-012, EC-020. Done when:
  `ctest -R 005-evolution-failure` passes. After: T-011

## Phase 5 - Records

- [ ] T-013 Implement `src/io/run.c`: `--out` check without creation, lazy
  creation, `%.17g` trace rows, zero-padded snapshot files, JSON escaping and
  `config.json` written last, through the ops table; tests in
  `tests/io/test-run.c` with an independent CSV/JSON reader. Covers: FR-013,
  FR-014, FR-019, FR-025, EC-009, EC-017. Done when: `ctest -R 005-evolution-io`
  passes, covering exists/non-empty/file/symlink/missing-parent and injected
  write failures. After: T-004, T-005

## Phase 6 - CLI

- [ ] T-014 Implement the CLI library and `src/cli/qa-005-evolve.c`: strict
  parsing, `--out` check, callbacks streaming to the run handle, summary,
  `QaStatus` exit codes, build-metadata definitions. Tests in
  `tests/cli/test-evolve-cli.c`: exit 64 cases, library codes 1..6,
  valid run layout and invariants of section 4, EC-020 through the perturb hook
  (trace ends at `j - 1`, no later snapshot, no `config.json`), nothing written
  on usage or validation errors. Covers: FR-006, FR-013, FR-014, FR-015, EC-007,
  EC-009, EC-016, EC-017, EC-020. Done when: `ctest -R 005-evolution-cli`
  passes. After: T-011, T-013

## Phase 7 - Integration and measurements

- [ ] T-015 Integration tests in `tests/integration/`: order-2 convergence
  (2x2, linear, `T = 10`, steps 100..800 vs 12800, at least two ratios in
  [3.6, 4.4]); agreement with the four hard-coded reference constants within
  `1e-6` (3x3 and 2x2, `T = 100`, `steps = 10^4`); byte-identical repeated CLI
  runs; 4x4 run (non-blocking, recorded). Covers: FR-007, FR-008, FR-023,
  EC-004, EC-013. Done when: `ctest -R 005-evolution-integration` passes. After:
  T-014
- [ ] T-016 Source gates in CTest: no dense matrix allocation and no `io`
  include under `src/evolution`, no `malloc` outside the documented owner.
  Covers: FR-001, FR-021, FR-028. Done when: `ctest -R 005-evolution-gate` passes
  and fails on a deliberately planted violation. After: T-014
- [ ] T-017 Move `reference/proto.py` to `tests/reference/` (needs explicit user
  permission: touches `specs/`), measure the norm drift at `STEPS_MAX` (3x3,
  linear), run the 4x4 representative run and record parameters and outcome in
  `specs/active-spec.md` together with the representative run command (needs
  explicit user permission). Covers: FR-005, FR-023, EC-004. Done when: the values
  appear in `specs/active-spec.md` and `|norm - 1| <= 1e-12` at every step for
  2x2 and 3x3 is shown by the run summary. After: T-015

## Phase 8 - Closing checks

- [ ] T-018 Sanitizer and leak gate: ASan+UBSan build, all 005 tests and the
  representative run; `leaks --atExit` on the representative run. Covers:
  FR-021, EC-010. Done when: zero sanitizer reports and zero leaks.
  After: T-015, T-016
- [ ] T-019 ARC-6 documentation audit and memory-safety review of the new files
  (agents `arc6-doc-auditor` and `memory-safety-reviewer`). Covers: FR-021,
  FR-027. Done when: both report no findings. After: T-018
- [ ] T-020 Run DoD check. Covers: all. Done when: every DoD item in `spec.md`
  is verified (full `ctest` green including 001-004 non-regression) and the
  final report lists commands, results and numbers. After: T-017, T-019
