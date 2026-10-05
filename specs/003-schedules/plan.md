# Plan: 003-schedules

- Status: approved
- Parent: `specs/003-schedules/spec.md` (approved, Owner Roger, 2026-10-05).

## Approach

Meet the spec with a tiny pure library, `qa_schedules`, under
`src/evolution/schedules/`, depending only on `qa_core` (for `QaStatus`) and
libm. Evaluation is a strict validate-then-compute pipeline: validate `kind`,
pointers, `T` and `t` in a fixed order (all failures return `QA_ERR_DOMAIN`
before any write), compute `s = t / T`, evaluate the family, clamp both
weights to `[0, 1]` and only then write the two outputs. The name function is
a lookup over a static table of string literals, so it allocates nothing and
the caller never frees.

Decisions:
- Pointer and `kind` checks run before the numeric checks, and outputs are
  written only at the very end, so no error path leaves a partial write
  (FR-009..FR-012, FR-014).
- Clamping is applied to the final values, not to `s`, so FR-005 holds
  exactly while FR-003/FR-004 keep the `SCHED_TOL` margin. The exponential
  family uses the form that keeps absolute error small near `s = 0`.
- `s = t / T` is IEEE-exact for `0 <= t <= T`, so `t = T` gives exactly
  `s = 1` and no `s > 1` can occur (EC-002), and a subnormal `T` needs no
  special path (EC-007).
- The four families are selected by a `QaScheduleKind` enum, with `kind`
  validated explicitly because a C enum accepts any integer.
- Tests are deterministic and parameterized by family; a sweep over `s` plus
  reference values from the spec's formulas covers the properties.

Alternatives rejected: function-pointer table per family — more machinery than
four closed-form cases and one more thing to keep consistent with the enum;
placing the code in a new top-level module `src/schedules` — rejected by the
decision recorded in the spec (location is `src/evolution/schedules/`, the
time-stepping code arrives as a sibling folder later).

## Components / Phases

- Phase 1 - Header and build wiring: public header
  `include/qa/evolution/schedules.h` declaring `QaScheduleKind` (four
  families), `qaScheduleEval` and `qaScheduleName` with the per-function
  documentation required by ARC-3 (purpose, ownership, errors, numerical
  assumptions); new static library `qa_schedules` linking `qa_core`, libm,
  with the strict-warning helper; header registered in a `003-schedules`
  public-header gate (standalone compile plus docs check), using the same
  scripts as 001/002; a placeholder-free minimal translation unit so the
  header test links.
- Phase 2 - Library: `src/evolution/schedules/schedules.c` implementing
  validation order, the four closed forms, clamping, and the static name
  table; no allocations, no globals, no I/O.
- Phase 3 - Unit tests: `tests/evolution/test-schedules.c`, one CTest entry per
  group with the `003-schedules-<group>` naming used by 001/002 and
  `TEST-003-schedules-FR-0XX` / `TEST-003-schedules-EC-0XX` identifiers on failure. Groups: `eval`
  (formulas, `s = 0.5` values), `boundary` (t = 0, t = T), `properties`
  (sweep of `s` for sum, exact range, monotonicity within `SCHED_TOL`, scale
  invariance), `limits` (huge, tiny, subnormal `T`; `s` near 0 and 1 for `T = 1`
  and `T = 10`; `-0.0`),
  `errors` (every invalid input leaves outputs untouched, using sentinel
  values), `names` (four names, invalid kind, NULL), `determinism`
  (repeated calls bit-identical).
- Phase 4 - Gates: CTest labels `003-schedules;phase-N`; full-suite run
  including 001/002 non-regression; ASan/UBSan build and `leaks` check over
  the schedule tests. The spec defines no CLI or recorded configuration (the
  record is out of scope, only names are provided), so the "representative
  run" of AGENTS.md is the test binary itself; this is stated in the final
  report.

## Coverage

| FR / EC | Covered by |
|---------|------------|
| FR-001 | Phase 2 evaluation; Phase 3 `eval` |
| FR-002 | Phase 1 enum (exactly four); Phase 3 `eval`, `names` |
| FR-003 | Phase 2; Phase 3 `boundary`, `properties` |
| FR-004 | Phase 2; Phase 3 `properties` |
| FR-005 | Phase 2 clamping; Phase 3 `properties`, `limits` |
| FR-006 | Phase 3 `properties` (sweep, tolerance `SCHED_TOL`) |
| FR-007 | Phase 3 `properties` (scale invariance) |
| FR-008 | Phase 2 (pure, no state); Phase 3 `determinism` |
| FR-009 | Phase 2 validation; Phase 3 `errors` |
| FR-010 | Phase 2 validation; Phase 3 `errors` |
| FR-011 | Phase 2 validation; Phase 3 `errors` |
| FR-012 | Phase 2 validation; Phase 3 `errors` |
| FR-013 | Phase 2 name table; Phase 3 `names` |
| FR-014 | Phase 2 validation; Phase 3 `names` |
| EC-001 | Phase 3 `boundary` |
| EC-002 | Phase 2 (`s = t / T`); Phase 3 `boundary` |
| EC-003 | Phase 3 `limits` |
| EC-004 | Phase 3 `limits` (`T = 1`, `t = 1e-16`, `nextafter(1, 0)`) |
| EC-005 | Phase 3 `eval` |
| EC-006 | Phase 2 validation; Phase 3 `limits` |
| EC-007 | Phase 3 `limits` |

## Constitution check
- STK-1: C17, CMake, clang; the new library goes through the existing configuration.
- STK-3: no new dependency; libm only (already implied by the project).
- STK-5: `qa_configure_target` applies `-Wall -Wextra -Werror` (STK-5) plus `-Wpedantic` to library and tests.
- MEM-1/MEM-4/MEM-5: no allocation, no buffers; the name table is static and immutable, documented as never freed.
- MEM-2/MEM-3/MEM-6: schedule tests run under ASan/UBSan and `leaks` before the task is complete.
- ARC-3: header documents every public function (checked by the docs gate).
- ARC-6: `schedules.c` opens with a header comment, every function (including `static` helpers) has a doc block, each public definition carries an implementation note pointing to the header contract, and every validation, clamp and output write has an inline bound comment; `tests/evolution/test-schedules.c` follows the same density; the header follows `include/qa/evolution/schedules.h` (docs gate).
- ARC-4: test identifiers carry FR/EC IDs; the Coverage table maps each to a phase.
- TST-1: unit tests cover primitives and every failure path.
- TST-2: N/A in this spec: there is no scientific outcome or reproducible run to integrate; integration tests arrive with the evolution spec.
- TST-3: tests are deterministic and parameterized by family; no randomness.
- TST-4: only the family names are provided here; writing the record is out of scope per the spec.
- ERR-1/ERR-2: every domain error is returned as `QaStatus` (`QA_ERR_DOMAIN`), never ignored.
- LIM-1: libm closed forms only, no platform-specific code; cross-platform bit-identity is not claimed (FR-008).

## Handoff to the evolution spec
- The caller of `qaScheduleEval` owns the `t <= T` guarantee: when it computes
  `t = k*dt`, rounding may exceed `T` at the last step, so it MUST use
  `t = min(k*dt, T)`; evaluation never saturates (FR-010). The evolution spec
  must state this as a requirement and test it. (Moved here from the spec's
  "Note for the plan" so it is tracked; the spec text is unchanged.)
