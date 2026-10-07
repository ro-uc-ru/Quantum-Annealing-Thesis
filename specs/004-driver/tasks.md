# Tasks: 004-driver

- Status: approved
- Parent: `spec.md` (approved) + `plan.md` (approved). Test identifiers use
  `TEST-004-driver-FR0XX` / `TEST-004-driver-EC0XX`; CTest entries are named
  `004-driver-<group>`.

## Phase 1 - Headers and wiring

- [x] T-001 Write `include/qa/hamiltonian/driver.h` (guard
  `QA_HAMILTONIAN_DRIVER_H`, scope comment, exact signatures of
  `qaHamiltonianApplyDriver` and `qaHamiltonianInitialState`, numbered
  validation order, `@owner`, `@assumes`, only the includes it uses). Covers:
  FR-005, FR-002, FR-003, FR-006. Done when: `clang -std=c17 -fsyntax-only
  -I include include/qa/hamiltonian/driver.h` passes and
  `scripts/check-header-docs.sh` passes on it.
- [x] T-002 Write `include/qa/io/config.h` (`QaConfigRecord` with ten
  `const char *` fields, `qaIoWriteConfig`, bound constants 1024 and 4096) and
  the private `src/io/config-internal.h` (operations table and
  `qaIoWriteConfigWith`). Covers: FR-014, FR-015. Done when: the public header
  passes `-fsyntax-only` and `check-header-docs.sh`, and the private header
  compiles when included after `config.h`.
- [x] T-003 Wire CMake: `driver.c` placeholder inside `qa_hamiltonian`, new
  `qa_io` static library with a placeholder `src/io/config.c`,
  `file(MAKE_DIRECTORY results)` at configure time, the 004 public headers in
  the standalone and docs gate (`004-driver-` prefix), and a header test
  binary checking guard, self-containment and exact signatures. Covers:
  FR-005, FR-014. Done when: the Debug configure and build pass with zero
  warnings, `ctest -R 004-driver-header` passes and `results/` exists after
  configure. After: T-001, T-002

## Phase 2-3 - Driver library and unit tests

- [x] T-004 Implement in `src/hamiltonian/driver.c` the static `N`-gate,
  `dim == 2^numCells` check, pointer check and `qaHamiltonianInitialState`
  (`(-1)^popcount(k) * s`, imaginary part `+0.0`), with tests `n-gate`,
  `shape` and `initial-state` for it in `tests/hamiltonian/test-driver.c`.
  Covers: FR-002, FR-003, FR-006, FR-010, EC-001, EC-002, EC-003, EC-004,
  EC-015, EC-017. Done when: `ctest -R 004-driver-n-gate`,
  `004-driver-shape` and `004-driver-initial-state` pass, including the
  canary-untouched checks on failure. After: T-003
- [x] T-005 Add to `qaHamiltonianApplyDriver` the `N`-gate, `dim`, pointer and
  `uintptr_t` overlap checks (validation skeleton; the compute loop lands in
  T-007), with tests for the apply side. Covers: FR-002, FR-003, FR-006,
  EC-001, EC-002, EC-003, EC-004, EC-005, EC-014. Done when:
  `ctest -R 004-driver-n-gate` and `004-driver-shape` pass for both
  functions, including `outPsi == phi`, `outPsi == phi + 1` and `dim` checked
  before NULL and overlap. After: T-004
- [x] T-006 Add the finiteness scan (full scan before the norm gate) and the
  sequential norm gate `| ||phi|| - 1 | <= 1e-12` to apply, with the `domain`
  tests. Covers: FR-007, FR-008, EC-006, EC-007, EC-014, EC-026. Done when:
  `ctest -R 004-driver-domain` passes: poisoned index and canary untouched,
  zero vector, `2.5 * |k>`, overflowing squares, and the `dim = 65536`
  non-power-of-two vector built with the `long double` reference norm is
  accepted. After: T-005
- [x] T-007 Implement the apply loop `outPsi[k] = sum_c phi[k XOR m_c]` in
  ascending `c` with `m_c = 1 << (numCells - 1 - c)`, with the `vectors`
  tests. Covers: FR-001, FR-004, EC-008. Done when:
  `ctest -R 004-driver-vectors` passes: `H|0>` at `N = 2` has ones at indices
  1, 2, 4, 8, and `H|k>` has ones at exactly `k XOR m_c` for sampled `k` at
  `N = 2, 3, 4`. After: T-006
- [x] T-008 Add the `properties` tests: `|+>^numCells` eigenvalue, operator
  norm bound, Hermiticity, linearity, and the ground-state checks of the
  initial state (`H psi0 = -numCells psi0` per amplitude, energy, bit-exact at
  `N = 2`). Covers: FR-011, FR-016, EC-009, EC-010, EC-011, EC-012, EC-016.
  Done when: `ctest -R 004-driver-properties` passes with the section 7
  vectors for `N = 2, 3, 4` within `1e-12`. After: T-007
- [x] T-009 Add the `contract` tests and the no-heap source gate
  (`scripts/check-no-heap.sh`, registered in CTest, scanning `driver.c` and
  `src/io/config.c`): guard bands around
  `outPsi`, bit-exact `phi` immutability, interleaved repeated calls
  bit-identical, canary checks of every failure class. Covers: FR-005, FR-009,
  EC-014, EC-017. Done when: `ctest -R 004-driver-contract`,
  `004-driver-no-heap` and `004-driver-no-heap-negative` pass (the latter runs
  the gate on a fixture with a `malloc` call and requires its failure report),
  and the initial state repeats bit-identically around an apply call (FR-009). After: T-008

## Phase 4-5 - CSV writer and tests

- [x] T-010 Implement `qaIoWriteConfig` / `qaIoWriteConfigWith` in
  `src/io/config.c`: argument and bound validation before any file is
  created, RFC 4180 quoting, header and row, temporary sibling file opened
  with `"wx"`, `rename` over the target, no heap. Tests `contract`, `range`
  and `escape` in `tests/io/test-config.c` with an independent RFC 4180
  parser. Covers: FR-014, FR-015, EC-024, EC-027, EC-028, EC-029. Done when:
  `ctest -R 004-driver-io-contract`, `004-driver-io-range` and
  `004-driver-io-escape` pass: exact header and row, a replaced file holds
  nothing of the old content, bounds accepted exactly at 1024 and 4096 and
  rejected one byte over with no file created, round trip of fields with
  comma, quote, CR and LF. After: T-003
- [x] T-011 Implement the failure paths of the writer (every open, write,
  flush, close and rename failure closes the file, removes the temporary file
  and leaves the target alone) and the `io-failure` tests using the
  operations table. Covers: FR-015, EC-021, EC-022, EC-023. Done when:
  `ctest -R 004-driver-io-failure` passes: missing directory, non-writable
  directory, injected write failure, injected close failure, and an existing
  valid CSV bit-identical afterwards, with no `.tmp` file left. After: T-010

## Phase 6 - Integration, demo and record

- [x] T-012 Write `tests/integration/test-004-integration.c`: for `N` in
  {2, 3, 4} build board ids with `qaGridWithBit` and check (a) `psi0[id]`
  with the test's own queen count, (b) `H_driver |id>` at exactly the ids
  from toggling each cell read with `qaGridGetBit`, (c) FR-011; a failing
  helper call fails the case, reports the step and skips the Hamiltonian
  calls. Covers: FR-012, FR-011, FR-001. Done when:
  `ctest -R 004-driver-integration` and
  `004-driver-integration-build-failure` pass. After: T-008
- [x] T-013 Write `src/cli/qa-004-demo.c` (one optional path argument,
  default `results/004-config.csv`; `N = 4` initial state, apply, check
  `-16 * psi0[k]` within `1e-12`, record filled from the CMake build
  definitions, `goto cleanup`) and its tests: a demo run followed by a
  read-back checker, and an exit-status test on a non-existent directory.
  Covers: FR-013, FR-014, FR-015, EC-020, EC-025. Done when:
  `./build/qa-004-demo` exits 0 from the project root,
  `ctest -R 004-driver-config` passes (exact header, one row, `v1`, `4`,
  `65536`, `4:psi0`, `N/A` fields, non-empty build fields), and
  `004-driver-demo-failure` sees a non-zero exit. After: T-011, T-012

## Phase 7 - Gates and closing

- [x] T-014 Run the sanitizer and leak gates on all 004 targets:
  ASan+UBSan build over the 004 tests and `qa-004-demo`, `leaks --atExit`
  (macOS) through `scripts/check-leaks.sh build ./build/qa-004-demo`.
  Covers: FR-005, FR-015, EC-021, EC-022. Done when: the sanitizer build runs
  all 004 CTest entries clean and `check-leaks.sh` reports 0 leaks for every
  004 test and the demo. After: T-013
- [x] T-015 Run `arc6-doc-auditor` and `memory-safety-reviewer` over the 004
  sources, tests and headers and fix every finding. Covers: FR-005, FR-015.
  Done when: both reports are clean and `scripts/check-header-docs.sh` passes
  on both new headers. After: T-014
- [x] T-016 Run the DoD check. Covers: all. Done when: every DoD item in
  `spec.md` section 5 is ticked, the full
  `ctest --test-dir build --output-on-failure` is green including 001..003,
  and `results/004-config.csv` is present after the representative run. After:
  T-015

## Coverage check

| Item | Task |
|------|------|
| FR-001 | T-007, T-012 |
| FR-002, FR-003 | T-004, T-005 |
| FR-004 | T-007 |
| FR-005 | T-001, T-003, T-009 |
| FR-006 | T-004, T-005 |
| FR-007, FR-008 | T-006 |
| FR-009 | T-009 |
| FR-010 | T-004 |
| FR-011 | T-008, T-012 |
| FR-012 | T-012 |
| FR-013 | T-013 |
| FR-014 | T-002, T-010, T-013 |
| FR-015 | T-011, T-013 |
| FR-016 | T-008 |
| EC-001..EC-005 | T-004, T-005 |
| EC-006, EC-007, EC-026 | T-006 |
| EC-008 | T-007 |
| EC-009..EC-012, EC-016 | T-008 |
| EC-014 | T-005, T-006, T-009 |
| EC-015 | T-004 |
| EC-017 | T-004, T-009 |
| EC-020, EC-025 | T-013 |
| EC-021..EC-023 | T-011 |
| EC-024, EC-027..EC-029 | T-010 |
