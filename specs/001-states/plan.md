# 001-states - Plan

- Parent: `specs/001-states/spec.md` (Approved 2026-09-29 per constitution §4).
- Active pointer: `specs/active-spec.md` points to 001-states.
- Goal: implement classical compact board vectors + MSB-first indexing only.
  No amplitudes, no operators, no evolution, no IO.

## 1. Data models

- `QaGridId`: `uint32_t` bit-packed board. Only the low `numCells` bits are
  significant; canonical inputs carry zeros above `numCells` (strict, never masked).
- `N`: `unsigned int` board edge, `N in [2, 4]` accepted, `N == 5` reserved,
  anything else rejected. Negatives are impossible by type.
- Derived: `numCells = N * N` (4/9/16), `pos = i * N + j`,
  `shift = numCells - 1 - pos`, `limit = ((uint32_t)1u << numCells)`.
- Error channel: every fallible helper returns `QaStatus`
  (`QA_OK`, `QA_ERR_RANGE`, `QA_ERR_UNSUPPORTED`); results go through
  non-NULL out-params (`unsigned int *outBit`, `QaGridId *outId`).
  NULL out-param yields `QA_ERR_RANGE`; on failure out-params stay untouched.
- API shape (behavioral, exact names live in implementation):
  `getBit(id, n, i, j, outBit)` with `*outBit in {0, 1}`;
  `withBit(id, n, i, j, bit, outId)` returning a new id.
  All inputs by value; nothing mutates; nothing allocates.

## 2. Modules and phases

- Phase 0 - Build scaffold (prerequisite): add `CMakeLists.txt` (C17, clang,
  CTest) wiring `qa_core` library + `test-001-states` target. Done (2026-09-29):
  the repo had no `CMakeLists.txt` (only a placeholder `Makefile`); constitution
  §1 mandates CMake, and `README.md` states targets arrive with the first
  implementation spec.
- Phase 1 - Header: `include/qa/core/grid.h` declaring `QaGridId`, `QaStatus`
  reuse, and the pure helpers with §6 documentation.
- Phase 2 - Implementation: `src/core/grid.c` with the fixed validation order
  (`N` → `i,j` → `id` → `bit`) before any shift or mask.
- Phase 3 - Unit tests: `tests/core/test-grid.c` (CTest) covering §4 below.
- Phase 4 - Verification: Debug build, full `ctest`, ASan+UBSan clean,
  `leaks` clean, exact commands reported per `AGENTS.md`.

## 3. Decisions (chosen + discarded alternative)

- D-01 Bit-packed `uint32_t` (chosen): 4/9/16 bits used, `|k⟩` is the value,
  zero allocs. Why: smallest encoding per user requirement, direct TFG identity,
  trivial leak review. Discarded: byte-per-cell array — readable but 8x memory
  and heap ownership for zero benefit at this scale.
- D-02 `QaStatus` return + out-params (chosen): matches `codestyle.md` §5/§7,
  carries `RANGE`/`UNSUPPORTED` with untouched-out guarantee. Why: the only
  channel the constitution's ownership rules already cover. Discarded: struct
  `{id, status}` return — workable but invents a new project-wide pattern for
  one spec; bare-`QaGridId` return — cannot report errors at all.
- D-03 `N` as `unsigned int` (chosen): positivity by construction, matches spec.
  Why: removes the negative-`N` class entirely. Discarded: `size_t` — wider
  than needed and invites `SIZE_MAX` overflow games; `int` — reintroduces
  negatives the spec explicitly excludes.
- D-04 Strict reject of non-canonical ids (chosen): any bit at/above `numCells`
  yields `RANGE`. Why: user ruling, deterministic asserts, no hidden
  normalization. Discarded: mask-and-accept — hides caller bugs and contradicts
  RF-007 as now written.
- D-05 Pure by-value immutability (chosen): inputs never mutate; `withBit`
  returns a new id; precedent for future `H |v⟩`. Why: user ruling, assertable
  (`input == before` after every call), no aliasing. Discarded: in-place
  mutation via pointer — faster by nanoseconds, far harder to test and review.
- D-06 Fixed validation order (chosen): `N` → `i,j` → `id` → `bit` before any
  shift. Why: makes `EC-01..EC-06` order-independent of caller and eliminates
  shift-before-check UB. Discarded: per-function ad-hoc ordering — same checks,
  unreviewable interleavings.
- D-07 Scaffold CMake now (chosen): Phase 0 in this spec. Why: constitution §1
  and missing `CMakeLists.txt` block every later phase; README anticipates it.
  Discarded: deferring the build — leaves implementation untestable.

## 4. Test strategy (unit only; integration N/A for 001)

No integration tests: constitution §11 targets scientific outcomes, and 001 has
none by scope. All groups are deterministic CTest cases parameterized by
`(N, id, i, j, bit)`; recorded config per spec §5 is `N`, vectors, git sha,
clang version, CMake flags (`seed`/`schedule`/`dt` N/A).

| Group | Covers | Cases |
|-------|--------|-------|
| T-01 N-gate | RF-002, RF-003, EC-01, EC-02 | `N = 0,1,2,3,4,5,6,UINT_MAX`; expect `UNSUPPORTED` only for 5 |
| T-02 id range | RF-006, RF-007, EC-03, EC-06 | one-past-max per N (16/512/65536), `0xFFFFFFFF`, full/empty round-trips |
| T-03 cell access | RF-005, EC-04, EC-05, EC-09 | all `(i,j)` incl. `i>=N`, `bit in {0,1,2,UINT_MAX}`, NULL out-params |
| T-04 vectors | RF-004, §4 vectors | `\|9⟩`, `\|11⟩`, 3x3 `0/511/256/1`, 4x4 `0/65535/32768/1/0x8421`, both directions |
| T-05 immutability | RF-005, EC-08 | input bit-identical after every `withBit`/`getBit`, success and failure |

## 5. Traceability

RF-001 → Phase 1/2 (`QaGridId`) → T-02/T-04. RF-002 → Phase 2 (N-gate) → T-01.
RF-003 → Phase 2 → T-01. RF-004 → Phase 1/2 (pos/shift) → T-04. RF-005 →
Phase 1/2 (get/with) → T-03/T-05. RF-006 → Phase 2 (validation order) →
T-01/T-02/T-03. RF-007 → Phase 2 (strict canonical) → T-02.
