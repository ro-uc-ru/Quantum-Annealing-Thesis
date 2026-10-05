# Tasks: 003-schedules

- Status: approved
- Parent: `spec.md` (approved) + `plan.md` (approved). Test IDs use the
  `TEST-003-schedules-FR-0XX` / `-EC-0XX` scheme; CTest entries are `003-schedules-<group>`.

## Phase 1 - Header and build wiring

- [x] T-001 Write `include/qa/evolution/schedules.h` (guard
  `QA_EVOLUTION_SCHEDULES_H`, `QaScheduleKind` with exactly the four families,
  exact `qaScheduleEval` and `qaScheduleName` prototypes, codestyle §6 docs for
  purpose/inputs/outputs/ownership/errors/numerical assumptions, name string
  documented as static and never freed). Covers: FR-002, FR-013. Done when:
  `clang -std=c17 -fsyntax-only -I include include/qa/evolution/schedules.h`
  passes and `scripts/check-header-docs.sh` passes for the new header.
- [x] T-002 Wire CMake: static library `qa_schedules` (sources under
  `src/evolution/schedules/`, links `qa_core` and libm, `qa_configure_target`),
  the `003-schedules` public-header gate entries (standalone + docs), and a
  header test binary `test-003-schedules-header`. Covers: FR-002, FR-013. Done
  when: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
  passes and `ctest --test-dir build -R 003-schedules` lists the header gate
  entries passing. After: T-001

## Phase 2 - Library

- [x] T-003 Implement `src/evolution/schedules/schedules.c` validation of
  `qaScheduleEval` (`kind`, `NULL` pointers, `T`, `t` in the fixed order, all
  returning `QA_ERR_DOMAIN` before any write; `-0.0` accepted) and
  `qaScheduleName` with its static name table and errors. Covers: FR-009,
  FR-010, FR-011, FR-012, FR-013, FR-014, EC-006. Done when: the build is
  strict-clean and a throwaway call (not committed) returns the four names and
  `QA_ERR_DOMAIN` for each invalid input class. After: T-002
- [x] T-004 Implement the four closed forms in `qaScheduleEval` with
  `s = t / T`, clamp of final `a` and `b` to `[0, 1]`, and the single write
  at the end. Covers: FR-001, FR-002, FR-003, FR-004, FR-005, FR-008, EC-002,
  EC-003, EC-007. Done when: the build is strict-clean and `qaScheduleEval`
  returns `QA_OK` with `a = 0, b = 1` at `t = 0` and `a = 1, b = 0` at
  `t = T` for each family (checked by a throwaway call, not committed).
  After: T-003

## Phase 3 - Unit tests

- [x] T-005 Create `tests/evolution/test-schedules.c` harness (group dispatch
  by argv, assertion macro printing `TEST-003-schedules-FR-xxx` / `-EC-xxx`, sentinel
  helper) with the `eval` and `boundary` groups, and register
  `003-schedules-eval` and `003-schedules-boundary` in CMake. Covers: FR-001,
  FR-002, FR-003, EC-001, EC-002, EC-005. Done when:
  `ctest --test-dir build -R 003-schedules-(eval|boundary) --output-on-failure`
  passes. After: T-004
- [x] T-006 Add the `properties` group: sweep of `s` over `[0, 1]` for each
  family checking sum within `SCHED_TOL`, exact range `[0, 1]`, monotonicity
  within `SCHED_TOL`, and scale invariance for several `c`. Covers: FR-003,
  FR-004, FR-005, FR-006, FR-007. Done when:
  `ctest --test-dir build -R 003-schedules-properties --output-on-failure`
  passes. After: T-005
- [x] T-007 Add the `limits` group: `T = 1e300`, `T = 1e-300`, positive
  subnormal `T`, `T = 1` with `t = 1e-16` and `t = nextafter(1, 0)`, the same two
  neighbours of `s` (`1e-16`, `nextafter(1, 0)`) for `T = 10`, `t = -0.0`, all
  four families. Covers: FR-005, EC-003, EC-004, EC-006, EC-007. Done when:
  `ctest --test-dir build -R 003-schedules-limits --output-on-failure` passes.
  After: T-005
- [x] T-008 Add the `errors` group: NaN, `+-inf` and `T <= 0`; NaN, negative
  and `t > T`; invalid `kind` (out-of-range cast); `NULL` for each output
  pointer; each case asserts `QA_ERR_DOMAIN` and untouched sentinel outputs.
  Covers: FR-009, FR-010, FR-011, FR-012, EC-006, EC-007. Done when:
  `ctest --test-dir build -R 003-schedules-errors --output-on-failure` passes.
  After: T-005
- [x] T-009 Add the `names` group (four exact names, invalid `kind`, `NULL`
  receiver, `*name` untouched on error) and the `determinism` group (repeated
  calls bit-identical, call order independent). Covers: FR-008, FR-013,
  FR-014. Done when:
  `ctest --test-dir build -R 003-schedules-(names|determinism) --output-on-failure`
  passes. After: T-005

## Phase 4 - Gates and DoD

- [x] T-010 Run the full gate: strict debug build, all tests (001, 002 and
  003), ASan/UBSan build (`-DQA_SANITIZE=address,undefined`) over the 003
  tests, and `leaks --atExit` over the 003 test binary. Covers: FR-008,
  FR-009, FR-010, FR-011, FR-012, FR-014. Done when: every command exits 0
  with zero leaks and no sanitizer report, and the report states that the
  representative run is the test binary (no CLI in this spec). After: T-006,
  T-007, T-008, T-009
- [x] T-011 Run DoD check against `spec.md` section 5 and tick its items.
  Covers: all. Done when: every DoD item in `spec.md` is ticked and every
  FR and EC maps to a passing 003 test. After: T-010
