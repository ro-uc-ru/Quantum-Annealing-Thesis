# 003-schedules: Annealing schedules a(t) and b(t)

- Status: approved
- Owner: Roger
- Date: 2026-10-05 (approved 2026-10-05)

## 1. Context

### Goal (required)
Provide evaluation of the four annealing schedules (linear, trigonometric,
degree-2 polynomial and exponential) as pure functions of time `a(t)` and `b(t)`
for an arbitrary total time `T`, returning explicit errors for out-of-domain inputs.

### Why (required)
The annealing evolution (future spec) combines the initial and problem Hamiltonians
with weights `b(t)` and `a(t)` at every step `dt`; without verified schedules no
evolution can be validated or reproduced.

### Optional
- Users / Beneficiaries: the future split-operator evolution spec, which will
  evaluate `a(t)` and `b(t)` at every step; the thesis, which compares the four
  schedules.
- Definitions:
  - `T`: total annealing time, `double`, finite and strictly positive. No fixed
    upper bound (1, 100, 10,000, ...).
  - `t`: current time, `double`, `0 <= t <= T`.
  - `s = t / T`: normalized time, `s in [0, 1]`. Every schedule depends on `t`
    and `T` only through `s`.
  - `a(t)`: weight that grows from 0 to 1 (`a(0) = 0`, `a(T) = 1`). Multiplies
    the problem Hamiltonian.
  - `b(t)`: weight that decays from 1 to 0 (`b(0) = 1`, `b(T) = 0`). Multiplies
    the initial (driver) Hamiltonian.
  - `SCHED_TOL`: `1e-12`, absolute tolerance of this spec for boundary
    conditions, `a + b = 1`, monotonicity and scale invariance. The range
    `[0, 1]` (FR-005) is exact, not toleranced.
  - Families (formulas, with `s = t / T`):

    | Family (`kind`)    | `a(s)`            | `b(s)`            |
    |--------------------|-------------------|-------------------|
    | linear             | `s`               | `1 - s`           |
    | trigonometric      | `sin^2(pi*s/2)`   | `cos^2(pi*s/2)`   |
    | degree-2 polynomial| `s^2`             | `1 - s^2`         |
    | exponential        | `2^s - 1`         | `2 - 2^s`         |

    The formulas coincide with those of the original Python TFG (scientific baseline).

  - Contract: `QaStatus qaScheduleEval(QaScheduleKind kind, double t, double T, double *a, double *b);`
    pure function: no state, no allocations, no I/O.
  - Contract: `QaStatus qaScheduleName(QaScheduleKind kind, const char **name);`
    pure function; `*name` points to a static, immutable, NUL-terminated string
    owned by the library and never freed by the caller.
  - Error codes (existing `QaStatus`, none added): every invalid input
    (`T`, `t`, `kind`, `NULL` pointer) -> `QA_ERR_DOMAIN`.
- Note for the plan: the step `dt` and the discretization belong to the evolution
  spec; here only `(a, b)` is evaluated at a given `t`. The caller (evolution) is
  responsible for `t <= T`: when it computes `t = k*dt`, rounding may exceed `T`
  at the last step, so it must use `t = min(k*dt, T)` (evaluation never saturates,
  see FR-010).
- Note for the plan: location is `src/evolution/schedules/` (depends on core and
  libm only); the time-stepping evolution code goes in a sibling folder in a later spec.

## 2. Functional Requirements

- FR-001 (event-driven)  WHEN evaluation is invoked with a valid `kind`, finite `T > 0`, and `t` with `0 <= t <= T`, the system shall write to `*a` and `*b` the values from the Definitions table for `s = t / T` and return `QA_OK`.
- FR-002 (ubiquitous)    The system shall offer exactly four families: linear, trigonometric, degree-2 polynomial and exponential, with the formulas in Definitions.
- FR-003 (ubiquitous)    For every family, `a(0) = 0`, `a(T) = 1`, `b(0) = 1` and `b(T) = 0` within `SCHED_TOL`.
- FR-004 (ubiquitous)    For every family and every valid `t`, `|a + b - 1| <= SCHED_TOL`.
- FR-005 (ubiquitous)    For every family and every valid `t`, the returned `a` and `b` satisfy exactly `0 <= a <= 1` and `0 <= b <= 1` (no tolerance; results are clamped to this range).
- FR-006 (ubiquitous)    For every family and valid `t1 < t2` in `[0, T]`, `a(t2) >= a(t1) - SCHED_TOL` and `b(t2) <= b(t1) + SCHED_TOL`.
- FR-007 (ubiquitous)    The result depends on `(t, T)` only through `s = t / T`: for `c > 0` such that `c*t` and `c*T` are valid, `a(c*t, c*T)` and `a(t, T)` differ by `<= SCHED_TOL` (same for `b`).
- FR-008 (ubiquitous)    Evaluation shall be deterministic: equal inputs give bit-identical results on the same build and platform, with no dependence on global state. Bit-identity across platforms is not guaranteed.
- FR-009 (unwanted)      IF `T` is not finite (NaN, +-inf) or `T <= 0`, THEN the system shall return `QA_ERR_DOMAIN` without writing `*a` or `*b`.
- FR-010 (unwanted)      IF `t` is NaN, `t < 0` or `t > T`, THEN the system shall return `QA_ERR_DOMAIN` without writing `*a` or `*b`; it never extrapolates or saturates.
- FR-011 (unwanted)      IF `kind` is not one of the four families, THEN the system shall return `QA_ERR_DOMAIN` without writing `*a` or `*b`.
- FR-012 (unwanted)      IF `a` or `b` is `NULL`, THEN the system shall return `QA_ERR_DOMAIN` and write through neither pointer.
- FR-013 (event-driven)  WHEN the name of a valid `kind` is requested, the system shall write to `*name` its stable text name (`linear`, `trigonometric`, `quadratic`, `exponential`) for the run configuration record (TST-4) and return `QA_OK`.
- FR-014 (unwanted)      IF `kind` is not one of the four families or `name` is `NULL`, THEN the name request shall return `QA_ERR_DOMAIN` without writing `*name`.

## 3. Edge Cases

- EC-001 WHEN `t = 0`, the system shall return `a = 0` and `b = 1` (within `SCHED_TOL`) in all four families. (FR-003)
- EC-002 WHEN `t = T`, the system shall return `a = 1` and `b = 0` (within `SCHED_TOL`) in all four families, with no rounding of `t / T` producing `s > 1`. (FR-003)
- EC-003 WHEN `T` is very large (e.g. `1e300`) or very small but positive (e.g. `1e-300`) and `t` is valid, the system shall return finite values satisfying FR-003..FR-005 with no overflow or NaN. (FR-001)
- EC-004 WHEN `T = 1` and `t = 1e-16` or `t = nextafter(1, 0)` (about `1 - 1.1e-16`, the neighbour of 1), the system shall return `a, b` exactly in `[0, 1]` and satisfying FR-004. (FR-005)
- EC-005 WHEN `s = 0.5`, the system shall return: linear `a = b = 0.5`; trigonometric `a = b = 0.5`; polynomial `a = 0.25`, `b = 0.75`; exponential `a = sqrt(2) - 1`, `b = 2 - sqrt(2)` (within `SCHED_TOL`). (FR-001)
- EC-006 IF `t` is `-0.0`, THEN the system shall treat it as `0` and not return an error. (FR-010)
- EC-007 WHEN `T` is a positive subnormal and `t` is valid, the system shall return `QA_OK` with finite `a, b` satisfying FR-003..FR-005. (FR-001)

## 4. Scope

### In scope
- Evaluation of `a(t)` and `b(t)` for the four families, with input validation.
- Stable family names for the configuration record.
- Unit tests of properties (boundary, sum, range, monotonicity, scale invariance), known values and failure paths.

### Out of scope
- Any schedule other than the four above, including user-parameterized ones: they require a later spec.
- Step `dt`, number of steps and the time loop: they belong to the evolution spec.
- Building `H(t) = b(t) H_driver + a(t) H_problem` and split-operator evolution: later specs.
- Initial (driver) Hamiltonian: future spec.
- Writing the configuration record (format/IO): IO/CLI spec; only the names are provided here.
- Parsing a name back into a `kind`: later spec (CLI) if needed.

## 5. Definition of Done
- [x] FR-001..FR-014 covered by tests
- [x] EC-001..EC-007 covered by tests
- [x] A sweep of `s` over `[0, 1]` for each family verifies FR-003..FR-006 (FR-005 exact)
- [x] Every error path returns the specified `QaStatus` and leaves the outputs untouched
- [x] Public functions document purpose, ownership, errors and numerical assumptions (ARC-3)
- [x] Strict build (`-Wall -Wextra -Werror`), all tests, ASan/UBSan and `leaks` clean

## 6. Changelog
- 2026-10-05 QA-1: no change; formulas confirmed equal to the TFG (noted in Definitions), boundary tests EC-001/EC-002 suffice.
- 2026-10-05 QA-2: EC-007 rewritten as a deterministic success case.
- 2026-10-05 QA-3: FR-006 monotonicity now within `SCHED_TOL`.
- 2026-10-05 QA-4: FR-005 range exact (clamped), tolerance removed.
- 2026-10-05 QA-5: Note for the plan: caller uses `t = min(k*dt, T)`.
- 2026-10-05 QA-6: added `qaScheduleName` contract (static string ownership), split FR-013/FR-014, parsing names out of scope, no schedules beyond the four.
- 2026-10-05 QA-7: mapped errors to existing `QaStatus` (`QA_ERR_DOMAIN`, `QA_ERR_DOMAIN`); no new codes.
- 2026-10-05 QA-8: EC-008 retired (duplicate of FR-009).
- 2026-10-05 QA-9: EC-004 uses concrete `t` values (`1e-16`, `nextafter(1, 0)`).
- 2026-10-05 QA-10: FR-008 clarified (same build and platform only).
- 2026-10-05 QA-11: location `src/evolution/schedules/` in Note for the plan; ARC-3 added to DoD.
- 2026-10-05 QA-7 (final): all invalid inputs return `QA_ERR_DOMAIN` (user decision). Spec approved 2026-10-05.
