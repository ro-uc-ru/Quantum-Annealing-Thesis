# 001 - Classical Board States (compact grid vectors)

- Status: Approved (2026-09-29). Explicit user approval recorded per
  constitution §4 (user authorized T01 scaffold and spec approval on 2026-09-29).
- Active pointer: `specs/active-spec.md` points to 001-states.
- Source: `docs/TFG_UCEDA_RUIZ_ROGER.pdf` §II.C (grid notation to binary to `|k⟩`).
- Scope: classical N-Queens board representation only. No amplitudes, no operators,
  no evolution, no file or text IO.
- Target module: `core` (compact grid vectors + indexing).

## 1. Definitions

- `N`: board edge, `unsigned int`, strictly positive. This spec supports `N in [2, 4]`.
  Negative values are impossible by type.
- `numCells = N * N`: 4, 9, or 16.
- `QaGridId`: `uint32_t` bit-packed board, one bit per cell (`1` = queen).
  Smallest possible encoding: 4/9/16 bits used for 2x2/3x3/4x4.
  `N = 5` needs 25 bits and is out of scope here (see RF-002).
- `pos = i * N + j`, row-major, `0 <= pos < numCells`.
- `shift = numCells - 1 - pos`, MSB-first: `(0,0)` is the MSB, `(N-1,N-1)` is the LSB.
- `id = sum grid[i][j] * 2^shift`. Example 2x2 `[1 0; 0 1] = 0b1001 = |9⟩`.
- All helpers are pure and immutable: inputs pass by value, the original
  vector never mutates. `H |v⟩` later MUST also return a new value.
- No helper in this spec allocates.
- Error channel: every fallible helper returns `QaStatus`. Outputs travel via
  non-NULL out-params. A NULL out-param yields `QA_ERR_RANGE`. On failure the
  out-param is left untouched and the by-value input is unchanged by construction.
- Fixed validation order, before any shift or mask: validate `N`, then
  `i < N` and `j < N`, then `id < ((uint32_t)1u << numCells)`, then `bit <= 1`.

## 2. Functional requirements (EARS)

- RF-001 (ubiquitous): The system shall represent every board as a `QaGridId`
  with only the low `numCells` bits significant.
  What: compact bit-packed vector. Why: minimal memory, direct `|k⟩` identity,
  zero allocation and leak surface.
- RF-002 (event): When `N == 5` is requested, the system shall return
  `QA_ERR_UNSUPPORTED` and change nothing.
  What: reserve 5x5. Why: staged scope (2x2+3x3 mandatory, 4x4 expected,
  5x5 stretch) without blocking this spec; requires `QA_ERR_UNSUPPORTED`
  in the shared `QaStatus` (see `docs/codestyle.md`).
- RF-003 (event): When `N < 2` or `N > 5` is requested, the system shall return
  `QA_ERR_RANGE` and change nothing.
  What: strict range gate on a positive `unsigned int`. Why: prevent invalid
  shifts, masks, and counts (§8).
- RF-004 (ubiquitous): The system shall convert `id <-> (i, j)` with
  `pos = i * N + j` and `shift = numCells - 1 - pos` (MSB-first row-major),
  after the fixed validation order.
  What: canonical indexing. Why: deterministic mapping matching the TFG
  (`|9⟩`, `|11⟩`), shared by future tests and IO.
- RF-005 (ubiquitous): The system shall provide pure cell access:
  `QaStatus qaGridGetBit(QaGridId id, unsigned int n, unsigned int i,
  unsigned int j, unsigned int *outBit)` outputs only `0` or `1`;
  `QaStatus qaGridWithBit(QaGridId id, unsigned int n, unsigned int i,
  unsigned int j, unsigned int bit, QaGridId *outId)` outputs a new `QaGridId`.
  Neither shall mutate the input.
  What: minimal read/write via `QaStatus`. Why: testable indexing without
  amplitudes or heap ownership; `0/1` is the only legal cell output.
- RF-006 (unwanted): If `id >= ((uint32_t)1u << numCells)` or `i >= N`
  or `j >= N` or `bit > 1`, the system shall return `QA_ERR_RANGE`
  and change nothing.
  What: fail-safe out-of-range (e.g. `|9⟩` under an `N` whose `N*N` bits
  cannot spell `9` is rejected). Why: checked integer/domain errors (§8),
  no silent truncation or masking.
- RF-007 (state): While a board value is in use, callers SHALL pass canonical
  form with all bits at and above `numCells` set to zero; any other input is
  rejected per RF-006, never masked.
  What: strict canonical input. Why: deterministic asserts across N, no hidden
  normalization.

Out of scope: `complex double` amplitudes, norms, `1e-12` tolerance checks,
initial quantum states, operators, schedules, parsing/printing, files.

## 3. Edge cases

| # | Input | Expected |
|---|-------|----------|
| EC-01 | `N = 0, 1, 6, UINT_MAX` (`unsigned int`) | `QA_ERR_RANGE`, no change |
| EC-02 | `N = 5` | `QA_ERR_UNSUPPORTED`, no change |
| EC-03 | 2x2 `id = 16`, 3x3 `id = 512`, 4x4 `id = 65536` | `QA_ERR_RANGE` (one past max) |
| EC-04 | `i >= N` or `j >= N` in get/with/convert | `QA_ERR_RANGE`, input unchanged |
| EC-05 | `withBit` with `bit > 1` | `QA_ERR_RANGE`, input unchanged |
| EC-06 | `id` with any bit at or above `numCells` set (e.g. `0xFFFFFFFF` for N=2) | `QA_ERR_RANGE`, never masked |
| EC-07 | Empty board `id = 0`, full board `id = 2^numCells - 1` | Round-trip `id -> cells -> id` exact |
| EC-08 | Original `id` after `withBit` | Bit-identical to input (immutability assert) |
| EC-09 | NULL `outBit` / NULL `outId` | `QA_ERR_RANGE` |

Bit widths: validation runs before any shift. `numCells - 1 - pos` is at most
15 here, always `< 32`; `((uint32_t)1u << numCells)` is at most `1u << 16`.

## 4. Test vectors (asserts, not ground truth)

Vectors are fixed test oracles. Each test asserts `expected == obtained`
in both directions (`id -> grid` and `grid -> id`) plus immutability.

- 2x2 (`numCells = 4`): `|9⟩ = 0b1001 = [1 0; 0 1]`; `|11⟩ = 0b1011 = [1 0; 1 1]`.
- 3x3 (`numCells = 9`): `0 = empty`; `511 = 0b111111111 = full`;
  `256 = 0b100000000 = queen only at (0,0)`; `1 = queen only at (2,2)`.
- 4x4 (`numCells = 16`): `0 = empty`; `65535 = full`;
  `32768 = queen only at (0,0)`; `1 = queen only at (3,3)`;
  `0x8421 = diagonal queens at (0,0),(1,1),(2,2),(3,3)`.

Unit tests MUST cover all EC rows, all vectors above, and failure paths
(NULL out-params, bad N, bad id, bad `(i,j)`, bad bit).

## 5. Traceability and acceptance

- Each RF maps to unit tests named `TEST-001-states-RF00X` and to pure
  `core` helpers following `docs/codestyle.md` (`f -> a` spacing, `QaStatus`
  channel, by-value/const inputs, no allocation, no `exit()`).
- Function documentation follows constitution §6 (purpose, ownership, errors,
  numerical assumptions); the template itself lives in the constitution and
  `docs/codestyle.md` and is not duplicated here.
- Recorded configuration for 001 tests: `N`, grid vectors, git sha, clang
  version, CMake flags. `seed`, `schedule`, `dt/steps` are N/A for this spec
  (constitution §13 applies per-spec).
- Acceptance: Debug build + `ctest` green + ASan/UBSan clean + `leaks` clean
  on a representative run; exact commands reported per `AGENTS.md`.
- Approval: explicit user approval recorded here (Status + date) before any
  implementation (constitution §4).
