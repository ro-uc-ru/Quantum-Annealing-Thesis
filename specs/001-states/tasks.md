# 001-states - Tasks

- Parent: `spec.md` (Approved 2026-09-29), `plan.md` (Phases 0-4).
- Each task is <30 min, ordered by dependency. Do not skip ahead.
- Checkbox protocol: check only when its `Hecho cuando:` line is fully verified.

## Phase 0 - Build scaffold

- [x] T01 CMake+CTest scaffold (`CMakeLists.txt`, `qa_core` lib, `test-001-states` target, C17/clang).
  RFs: unblocks RF-001..RF-007 (no behavior itself).
  Hecho cuando: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build` passes with the empty targets registered.
  Done (2026-09-29): configure+build pass (AppleClang 21.0.0, C17);
  targets `qa_core` and `test-001-states` registered; CTest
  `001-states-scaffold` green; ASan/UBSan and `leaks` clean.

## Phase 1 - Header

- [x] T02 Write `include/qa/core/grid.h` (`QaGridId`, `QaStatus` reuse, `getBit`/`withBit` declarations, §6 docs, guard `QA_CORE_GRID_H`, `f -> a` style).
  RFs: RF-001, RF-004, RF-005.
  Hecho cuando: header compiles standalone (`clang -std=c17 -fsyntax-only`) and every declaration carries purpose/inputs/outputs/errors.
  Done (2026-09-29): standalone `clang -std=c17 -fsyntax-only -I include
  include/qa/core/grid.h` clean; `001-states-header`,
  `001-states-include-qa-core-grid-h-standalone`,
  `001-states-include-qa-core-grid-h-docs` green
  (3 declarations documented, signatures oracle-checked); shared
  `include/qa/core/status.h` added as the "QaStatus reuse" precondition
  (`001-states-include-qa-core-status-h-standalone` and
  `001-states-include-qa-core-status-h-docs` green, 1 declaration documented);
  ASan/UBSan and `leaks` clean. Mutation checks: removed `@owner` ->
  docs test failed; `unsigned int *outBit` -> `unsigned long *outBit`
  -> build failed on the signature assert (a `QaGridId` swap is
  type-identical to `unsigned int` on arm64, so it is undetectable by
  design).

## Phase 2 - Implementation (in order)

- [x] T03 Implement N-gate + `numCells`/`limit` computation in `src/core/grid.c` (no shifts before validation).
  RFs: RF-002, RF-003.
  Hecho cuando: `N==5` yields `QA_ERR_UNSUPPORTED`, `N=0,1,6,UINT_MAX` yields `QA_ERR_RANGE`, `N=2,3,4` proceeds.
- [x] T04 Implement `qaGridGetBit` (fixed order `N` → `i,j` → `id` → out-param; output only `0/1`).
  RFs: RF-004, RF-005, RF-006, RF-007.
  Hecho cuando: `|9⟩`/`|11⟩` cells read back exact and every out-of-range input yields `RANGE` with `*outBit` untouched.
- [x] T05 Implement `qaGridWithBit` (same order + `bit<=1`; returns new id, input unchanged).
  RFs: RF-005, RF-006, RF-007.
  Hecho cuando: set/clear flips exactly one bit, `bit>1` yields `RANGE`, input `id` is bit-identical after the call.

## Phase 3 - Unit tests (depend on T01-T05)

- [x] T06 Tests T-01 N-gate (`N=0,1,2,3,4,5,6,UINT_MAX`).
  RFs: RF-002, RF-003. EC: EC-01, EC-02.
  Hecho cuando: `ctest -R 001-states-n-gate` is green with `UNSUPPORTED` asserted only for `N==5`.
- [x] T07 Tests T-02 id range (one-past-max 16/512/65536, `0xFFFFFFFF`, empty/full round-trips).
  RFs: RF-001, RF-006, RF-007. EC: EC-03, EC-06, EC-07.
  Hecho cuando: `ctest -R 001-states-id-range` is green and no high-bit input is ever masked.
- [x] T08 Tests T-03 cell access (all `(i,j)` incl. `i>=N`/`j>=N`, `bit` in `{0,1,2,UINT_MAX}`, NULL out-params).
  RFs: RF-005, RF-006. EC: EC-04, EC-05, EC-09.
  Hecho cuando: `ctest -R 001-states-cell-access` is green with untouched-out asserted on every failure.
- [x] T09 Tests T-04 vectors both directions (`|9⟩`, `|11⟩`, 3x3 `0/511/256/1`, 4x4 `0/65535/32768/1/0x8421`).
  RFs: RF-004. EC: EC-07.
  Hecho cuando: `ctest -R 001-states-vectors` is green for `id->grid` and `grid->id`.
- [x] T10 Tests T-05 immutability (input bit-identical after every `get`/`with`, success and failure).
  RFs: RF-005. EC: EC-08.
  Hecho cuando: `ctest -R 001-states-immutability` is green with before/after `id` comparison on each case.

## Phase 4 - Verification

- [x] T11 Full verification: Debug build + full `ctest` + ASan/UBSan + `leaks`, config recorded (`N`, vectors, git sha, clang, CMake flags).
  RFs: RF-001..RF-007 (acceptance).
  Hecho cuando: `cmake --build build`, `ctest --test-dir build --output-on-failure`, sanitizer and `leaks` runs are all clean and their exact commands plus results are reported.
