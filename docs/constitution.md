# Constitution

- Status: approved

> Basis of the project. Every spec, plan and task MUST comply with it.
> It overrides any spec/plan. Once approved it is NOT changed during normal work;
> any change needs team approval.
> Each statement is VERIFIABLE: a reviewer can look at the repo and say
> "complies" or "does not".
> Cite statements by ID (e.g. STK-1). IDs are never renumbered. A removed
> statement keeps its line with "(retired: <reason>)" appended.
> Any exception requires explicit user approval before implementation and a
> documented rationale in the active specification.

## 1. Stack (STK)
- STK-1 All production code is C17, built with CMake and clang.
- STK-2 The CPU implementation is the reference backend; accelerators (e.g. Metal) are optional and no build or test requires them.
- STK-3 Dependencies are minimal, documented and justified by a concrete project need. A new dependency needs explicit user approval. OpenMP is optional.
- STK-4 Generated build artifacts, logs and results (`build/`, `bin/`, `results/`, `*.log`) stay outside source directories and are covered by a versioned `.gitignore`.
- STK-5 Production code and tests compile with `-Wall -Wextra -Werror` and zero warnings; this strict build is the default and the one that gates task completion. A non-strict build (`-DQA_WERROR=OFF`) may exist for non-blocking development only.

## 2. Memory Safety (MEM)
- MEM-1 Every allocation has one documented owner and one unambiguous release path.
- MEM-2 Every test and every specified run reports zero leaks: `leaks --atExit` on macOS, LeakSanitizer on Linux.
- MEM-3 All tests and specified runs are clean under ASan and UBSan on every supported platform.
- MEM-4 Every failure path releases all resources, and each one has a failure-path test that exercises it.
- MEM-5 Every write into a buffer is bounded by that buffer's size (no `strcpy`, `strcat`, `sprintf`, `gets`; `read`/`fread`/`memcpy` lengths never exceed the destination). Sizes and indices are validated before any multiply, shift or allocation.
- MEM-6 An unresolved leak, memory error or undefined behavior blocks task completion.

## 3. Architecture and Boundaries (ARC)
- ARC-1 Hamiltonian operators are matrix-free; allocating a dense Hamiltonian matrix is forbidden.
- ARC-2 Every public function and every non-obvious internal function documents purpose, ownership, errors and numerical assumptions.
- ARC-3 Every behavior is traceable from specification to tests and implementation.
- ARC-4 Time evolution preserves its required invariants (state norm) within tolerance. Default `NORM_TOL` is 1e-12 for `complex double`; each specification may tighten or relax it with justification.

- ARC-6 Source files are documented at the density of `src/core/grid.c` and public headers at the density of `include/qa/evolution/schedules.h`. Every `.c` file opens with a header comment (purpose, ownership, errors, numerical assumptions); every function, including `static` helpers, has a doc block with purpose, inputs, outputs, ownership, errors and numerical assumptions; every public definition carries an implementation note pointing to its header contract; and every validation, checked arithmetic step, clamp or output write whose safety depends on a bound has an inline comment stating that bound. Every public header opens with a scope comment (spec, FR/EC covered, definitions, formulas and numerical bounds used by its functions); every public type and enumerator is documented with `@owner` and `@assumes`; every public function documents its numbered validation order with the FR/EC of each step, `@param` directions, every reachable `@return` code and what happens to out-params on failure, `@owner` and `@assumes`; and a header declares only what it uses (no unused includes).

## 4. Testing (TST)
- TST-1 Unit tests cover mathematical primitives, indexing, operators, normalization and failure paths.
- TST-2 Integration tests validate the specified scientific outcomes and reproducibility.
- TST-3 Tests and experiments are deterministic and parameterized.
- TST-4 Each run records its configuration in a versioned machine-readable record: seed, N, schedule, dt/steps, git sha, clang version and CMake flags.
- TST-5 A specification is approved (explicit user approval recorded as Status plus date in the active specification) before any related implementation begins.

## 5. Errors and Logging (ERR)
- ERR-1 All allocations, I/O, integer conversions and numerical-domain errors are checked.
- ERR-2 Errors are never ignored: they are returned as `QaStatus`. Resource release on failure paths is governed by MEM-4.

## 6. Scope (SCP)
- SCP-1 Exact-state studies target 2x2 through 5x5. 2x2 and 3x3 MUST be completed; 4x4 is the expected goal; 5x5 is the final stretch goal and the upper bound.
- SCP-2 The original Python TFG is a scientific baseline, not code to reproduce.
- SCP-3 Out of scope: execution on real quantum hardware, boards larger than 5x5, and mandatory GPU backends.
- SCP-4 The project demonstrates that carefully written C is memory-safe and reliable: the rules in section 2 are the central quality bar of the project.

## 7. Limits (LIM)
- LIM-1 Supported platforms: macOS on Apple Silicon and Linux, both with clang, both equally required to build and pass every test and every check in section 2.
