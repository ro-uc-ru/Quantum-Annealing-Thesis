# Tasks: 002-hamiltonian

- Status: approved
- Parent: `spec.md` (approved) + `plan.md` (approved). Test IDs use the new
  `TEST-002-hamiltonian-FR0XX` scheme per the spec Changelog.

## Phase 1 - Header

- [x] T-001 Write `include/qa/hamiltonian/problem.h` (guard
  `QA_HAMILTONIAN_PROBLEM_H`, exact `qaHamiltonianApplyProblem` prototype, §6
  docs for purpose/inputs/outputs/ownership/errors). Covers: FR-005. Done when:
  `clang -std=c17 -fsyntax-only -I include include/qa/hamiltonian/problem.h`
  passes and the header-docs check passes for the new header.
- [x] T-002 Wire CMake for `qa_hamiltonian` (static lib linking `qa_core`),
  the public-header gate entries, and the `test-002-hamiltonian` binary plus a
  `qa-002-demo` placeholder target. Covers: FR-005. Done when:
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build` passes
  with the new targets registered. After: T-001

## Phase 2 - Library

- [x] T-003 Implement the `N`-gate with checked arithmetic (no `numCells`/`dim`
  math before the gate passes) returning `UNSUPPORTED` for 5 and
  `RANGE`/`OVERFLOW` otherwise, plus the combined-fault precedence. Covers:
  FR-002, FR-003, EC-001, EC-002, EC-013. Done when:
  `ctest -R 002-hamiltonian-n-gate` is green. After: T-002
- [x] T-004 Implement pointer/overlap/`dim` validation (`NULL`, `uintptr_t`
  range overlap incl. partial, `dim != 2^numCells`) with two-pass no-write
  semantics. Covers: FR-006, EC-003, EC-004, EC-005, EC-014, EC-015. Done when:
  `ctest -R 002-hamiltonian-shape` is green. After: T-003
- [x] T-005 Implement the `E(k)` energy core (uniform row/column/diagonal rule
  via `qa_core` indexing) and the pointwise `outPsi[k] = E(k) * phi[k]` apply.
  Covers: FR-001, FR-004, EC-007, EC-008. Done when:
  `ctest -R 002-hamiltonian-vectors` is green for every §7 oracle
  (2x2/3x3/4x4 incl. `E(16770) = 0`). After: T-004
- [x] T-006 Implement the finiteness scan and the `1e-12` norm gate (after the
  scan, before any write), including unnormalized rejection. Covers: FR-007,
  FR-008, EC-006, EC-009, EC-016. Done when:
  `ctest -R 002-hamiltonian-domain` is green (finite and non-finite canaries
  untouched). After: T-005

## Phase 3 - Unit tests (linearity and determinism)

- [x] T-007 Cover linearity, immutability, no-partial-write, and determinism
  probes (normalized superposition within `1e-12`, dyadic phases bit-exact,
  `±0.0` equality, repeated-call identity). Covers: FR-004, FR-009, EC-010,
  EC-011, EC-012, EC-017. Done when:
  `ctest -R 002-hamiltonian-linearity` is green. After: T-006

## Phase 4 - CLI and record

- [x] T-008 Implement `src/cli/qa-002-demo.c` (representative normalized N=4 run,
  exit code only; frees everything on every path). Covers: FR-010. Done when:
  `./build/qa-002-demo` exits 0 and
  `leaks --atExit -- ./build/qa-002-demo` is clean. After: T-005
- [x] T-009 Add `scripts/record-002-config.sh` writing versioned CSV `v1` with
  all FR-011 fields (`seed`/`schedule`/`dt_steps` as `N/A`) and wire the config
  CTest. Covers: FR-011. Done when: `ctest -R 002-hamiltonian-config` is green
  and `results/002-config.csv` exists with the `v1` header. After: T-005

## Phase 5 - 001↔002 integration

- [x] T-011 Add the 001↔002 integration test binary linking `qa_core` and
  `qa_hamiltonian`: build each EC-018 board (`N` in {2, 3, 4}: empty, full,
  and the 4x4 solution `16770`) with the 001 `withBit` helper, set
  `phi[id] = 1` with zeros elsewhere, call apply, and require `QA_OK`,
  `outPsi[id]` equal to the §7 oracle bit-exact, zeros elsewhere, and `phi`
  unchanged. Covers: FR-012, EC-018. Done when:
  `ctest -R 002-hamiltonian-integration` is green for every EC-018 case.
  After: T-005
- [x] T-012 Cover the integration build-failure path: a failing 001 helper
  call (e.g. `withBit` with an invalid bit) treats the case as failed,
  reports the failing build step, and never calls apply on the partially
  built id. Covers: FR-012, EC-019. Done when:
  `ctest -R 002-hamiltonian-integration-build-failure` is green (or the
  corresponding integration case asserting the no-apply path passes).
  After: T-011
- [x] T-013 Wire the integration run under the FR-011 record (same `N`,
  `dim`, vectors; `seed`/`schedule`/`dt_steps` `N/A`) so the run is
  reproducible from `results/002-config.csv`. Covers: FR-011, FR-012,
  EC-020. Done when: `ctest -R 002-hamiltonian-integration` passes using only
  the recorded configuration and the CSV contract test stays green.
  After: T-009, T-011

## Verification

- [x] T-010 Run the DoD check against `spec.md`. Covers: all. Done when: every
  DoD checkbox is ticked (debug build, full `ctest` incl. 001-states
  non-regression and the 002 integration entries, ASan/UBSan clean, `leaks`
  clean, numerical validation, CSV present, commands and results reported).
  After: T-007, T-008, T-009, T-011, T-012, T-013
