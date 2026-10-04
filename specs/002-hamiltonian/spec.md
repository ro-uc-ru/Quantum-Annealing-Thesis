# 002-hamiltonian: Problem Hamiltonian application

- Status: approved
- Owner: Roger
- Date: 2026-10-03

## 1. Context

### Goal (required)
Provide matrix-free application of the diagonal N-Queens problem Hamiltonian
(`out = H_problem |phi>`) for exact-state studies on 2x2 through 4x4, accepting
only normalized inputs within `NORM_TOL = 1e-12`.

### Why (required)
Reliable problem energies are the prerequisite for every later annealing step:
without a tested `H_problem`, no schedule, evolution, or ground-state claim can
be trusted.

### Optional
- Users / Beneficiaries: the future evolution / split-operator spec, which
  consumes trusted `H_problem` energies; the thesis, which needs reproducible
  2x2-4x4 exact-state results.
- Definitions:
  - `N`: board edge, `unsigned int`, strictly positive; accepted `N in [2, 4]`.
  - `numCells`: `size_t`, `N * N` via checked arithmetic only after the `N`-gate;
    4, 9, or 16. `N = 5` (`dim = 33,554,432`) is reserved.
  - `dim`: `size_t`, `2^numCells` via checked shifts only after the `N`-gate;
    16, 512, or 65536. Indices `k` are `size_t`, `0 <= k < dim`. Index `k` is the
    `QaGridId` of 001-states; `|k>` is the board with bit-packed id `k`.
  - `queens(k)`: cells `(i, j)` holding bit `1` in `k` (001-states MSB-first
    row-major mapping).
  - Attack: distinct cells attack iff same row, same column, or
    `|i1 - i2| == |j1 - j2|` (all diagonals, uniformly for every `N`).
  - `E(k)`: unordered attacking queen-pair count (`+1` per pair, equal weights);
    `E(k) = 0` iff no attacking pair. Small exact integer
    (`0 <= E(k) <= C(Q,2)`; 4x4 full board `E = 76`).
  - `H_problem = sum_k E(k) |k><k|`; application `out[k] = E(k) * phi[k]` on
    `complex double` arrays of length `dim`. Matrix-free: no `dim x dim` matrix
    (dense or sparse) is ever allocated or materialized.
  - Contract: `QaStatus qaHamiltonianApplyProblem(unsigned int n, const complex double *phi, complex double *outPsi, size_t dim);`
    `phi` is immutable `const` input, `outPsi` a separate mutable caller-owned
    output of identical length; the function allocates nothing. Overlap means
    intersection of `[phi, phi + dim)` with `[outPsi, outPsi + dim)` compared via
    `uintptr_t` (`phi == outPsi` subsumed).
  - Error channel: `QaStatus` (`QA_OK`, `QA_ERR_RANGE`, `QA_ERR_UNSUPPORTED` for
    `N == 5`, `QA_ERR_DOMAIN` for non-finite amplitudes or norm-gate failure,
    `QA_ERR_OVERFLOW` for bounds-unsafe width arithmetic, always checked).
  - Validation order (before any dereference, shift, or float op): `N`-gate (no
    `numCells`/`dim` arithmetic before it passes), then pointer preconditions,
    then `dim == 2^numCells`, then finiteness of every `phi[k]`, then the norm
    gate (`norm = sqrt(sum_k |phi[k]|^2)`; `|norm - 1| <= 1e-12` else
    `QA_ERR_DOMAIN`). Combined faults resolve by this order. All checks complete
    before the first write; on failure, existing non-NULL buffers are unchanged.
  - Determinism: pure function of (`n`, `phi`, `dim`); no hidden state, no RNG.

## 2. Functional Requirements

- FR-001 (ubiquitous) The system shall define `E(k)` as the attacking-pair count
  (`+1` per pair sharing a row, column, or diagonal), uniformly for all `N`.
- FR-002 (event-driven) WHEN `N == 5` is requested, the system shall return
  `QA_ERR_UNSUPPORTED` and write nothing, regardless of pointers or dim.
- FR-003 (event-driven) WHEN `N < 2` or `N > 5` is requested, the system shall
  return `QA_ERR_RANGE` (or `QA_ERR_OVERFLOW` for a genuinely overflowing width
  computation) and write nothing.
- FR-004 (ubiquitous) The system shall apply `H_problem` matrix-free as
  `outPsi[k] = E(k) * phi[k]` for `k = 0..dim-1` with `dim = 2^(N*N)`, recomputing
  `E(k)` pointwise from the 001-states bit pattern of each `k`.
- FR-005 (ubiquitous) The system shall expose exactly
  `QaStatus qaHamiltonianApplyProblem(unsigned int n, const complex double *phi, complex double *outPsi, size_t dim)`,
  keep `phi` immutable, write only to the separate caller-owned `outPsi`, and
  allocate nothing.
- FR-006 (unwanted) IF `phi == NULL` or `outPsi == NULL`, or the `uintptr_t`
  ranges overlap, or `dim != 2^numCells`, THEN the system shall return
  `QA_ERR_RANGE` and write nothing.
- FR-007 (unwanted) IF any `phi[k]` is non-finite in real or imaginary part,
  THEN the system shall return `QA_ERR_DOMAIN` and write nothing, completing the
  full scan before the norm gate and before any write.
- FR-008 (state-driven) WHILE applying `H_problem`, the system shall require
  `|norm(phi) - 1| <= 1e-12`; IF the norm is non-finite or outside tolerance,
  THEN the system shall return `QA_ERR_DOMAIN` and write nothing.
- FR-009 (ubiquitous) The system shall be deterministic: identical
  (`n`, `phi`, `dim`) inputs shall yield identical `outPsi`, with no hidden
  state, RNG, or globals.
- FR-010 (ubiquitous) The system shall provide the minimal demo CLI `qa-002-demo`
  that builds one representative normalized N=4 input, applies `H_problem`,
  checks the expected energy, and returns zero/non-zero.
- FR-011 (ubiquitous) The system shall record its configuration as versioned CSV
  `results/002-config.csv` (`v1`) with fields
  `spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed,schedule,dt_steps`,
  where `seed`, `schedule`, `dt_steps` are `N/A` (no evolution/sampling here).
- FR-012 (ubiquitous) The system shall provide an integration test composing the
  implemented 001-states grid helpers with `H_problem` energy and apply. For
  each (`N`, board) case of EC-018 the test SHALL build the id with 001
  `withBit`, set `phi[id] = 1` and zeros elsewhere, call apply, and require
  `QA_OK`, `outPsi[id]` equal to the §7 oracle bit-exact, zeros elsewhere, and
  `phi` unchanged.

## 3. Edge Cases

- EC-001 IF `N` is 0, 1, 6, or `UINT_MAX`, THEN the system shall return
  `QA_ERR_RANGE` (or `OVERFLOW` for a genuinely overflowing width op) without
  dereferencing NULL. (FR-003)
- EC-002 IF `N == 5` with any pointers or dim (including NULL), THEN the system
  shall return `QA_ERR_UNSUPPORTED` and write nothing. (FR-002)
- EC-003 IF `dim != 2^numCells` (15/17, 511/513, 65535/65537, 0, `SIZE_MAX`),
  THEN the system shall return `QA_ERR_RANGE` and leave `outPsi` untouched. (FR-006)
- EC-004 IF `phi == NULL` or `outPsi == NULL` with valid `N`, THEN the system
  shall return `QA_ERR_RANGE` and write nothing. (FR-006)
- EC-005 IF `phi == outPsi` or the `uintptr_t` ranges intersect, THEN the system
  shall return `QA_ERR_RANGE` with buffers unchanged. (FR-006)
- EC-006 IF any `phi[k]` is non-finite (including one poisoned index), THEN the
  system shall return `QA_ERR_DOMAIN` with `outPsi` unchanged. (FR-007)
- EC-007 IF `phi` is the basis state `|0>` or `|dim-1>`, THEN the system shall
  return `outPsi[k] = E_full` with `E_full = 6/28/76` for N=2/3/4, and `0` for
  `k = 0`. (FR-004)
- EC-008 IF `phi` is a single-queen basis state, THEN the system shall return
  `outPsi[k] = 0`. (FR-004)
- EC-009 IF `phi` is the zero vector or any unnormalized finite vector (e.g.
  `2.5 * |k>`), THEN the system shall return `QA_ERR_DOMAIN` with `outPsi`
  untouched. (FR-008)
- EC-010 IF any call succeeds or fails, THEN `phi` shall be bit-identical to
  pre-call. (FR-005)
- EC-011 IF any call fails, THEN the existing `outPsi` shall be unchanged
  (two-pass proof; `+0.0` vs `-0.0` in zero outputs count as equal). (FR-006, FR-007, FR-008)
- EC-012 IF `phi = (|x> + |y>) / sqrt(2)` is applied alongside separate `H|x>`,
  `H|y>`, THEN the results shall agree within `1e-12` (dyadic unit-norm scalings
  bit-exact). (FR-004, FR-009)
- EC-013 IF `N == 5` is combined with NULL, wrong dim, or non-finite `phi`,
  THEN the system shall return `QA_ERR_UNSUPPORTED` (precedence proof: the
  `N`-gate runs before pointer, dim, and finiteness checks). (FR-002)
- EC-014 IF `N` and pointers are valid but dim is wrong and `phi` is non-finite,
  THEN the system shall return `QA_ERR_RANGE`. (FR-006)
- EC-015 IF the overlap is partial (`outPsi == phi + 1`, one element, same block
  with mismatched dim), THEN the system shall return `QA_ERR_RANGE` with buffers
  unchanged. (FR-006)
- EC-016 IF a `DOMAIN`-triggering `phi` meets `outPsi` pre-filled with non-finite
  canary, THEN the system shall return `QA_ERR_DOMAIN` with `outPsi` bit-identical
  to the canary. (FR-007)
- EC-017 IF two identical (`n`, `phi`, `dim`) calls run into distinct outputs,
  THEN the outputs shall be bit-identical. (FR-009)
- EC-018 IF the board is built through the 001-states helpers for each `N` in
  {2, 3, 4} (empty, full, and the 4x4 solution `16770`), THEN the integration
  energies shall match the §7 oracles. (FR-012)
- EC-019 IF a 001-states helper call fails while building the integration board
  (e.g. `withBit` with an invalid bit), THEN the integration test shall treat
  the case as failed, report the failing build step, and SHALL NOT call apply
  on the partially built id. (FR-012)
- EC-020 IF the integration test runs, THEN it SHALL run under the configuration
  recorded per FR-011 (same `N`, `dim`, vectors; `seed`/`schedule`/`dt_steps`
  `N/A`), so the run is reproducible from `results/002-config.csv`.
  (FR-012, FR-011)

## 4. Scope

### In scope
- Diagonal `H_problem` application with norm gate and matrix-free guarantee.
- 001↔002 integration test over the implemented grid helpers.
- Demo CLI for the representative `leaks`/determinism run.
- Versioned CSV configuration record.

### Out of scope
- `H_driver` (transverse field): belongs to the driver spec.
- `H(s)` interpolation, schedules, Trotter/split-operator steps: belong to the
  evolution spec.
- `<phi|H|phi>` as a library call, eigensolvers, measurement/sampling: future specs.
- Metal/GPU backends: optional future optimization, never required.

## 5. Definition of Done

- [x] FR-001..FR-012 covered by tests
- [x] EC-001..EC-020 covered by tests
- [x] Debug build passes (`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`)
- [x] Full `ctest --test-dir build --output-on-failure` green, including
  `TEST-002-hamiltonian-*` plus non-regression of `TEST-001-states-*`
- [x] ASan+UBSan clean on the 002 tests
- [x] `leaks --atExit -- ./build/qa-002-demo` clean (representative N=4 run)
- [x] Numerical validation holds (`E(k)` exact; dyadic unit-norm applies bit-exact;
  irrational scalings/linearity within `1e-12`; norm gate enforced; `±0.0` equal)
- [x] `results/002-config.csv` present, `v1`, with fields per FR-011

## 6. Changelog

- 2026-09-30 Approved content baseline (11 RFs incl. norm gate, demo CLI, CSV v1).
- 2026-10-03 Rewritten to sdd-spec template: IDs migrated 1:1 (`RF-001..RF-011` to
  `FR-001..FR-011`, `EC-01..EC-17` to `EC-001..EC-017`) with no behavior change;
  status back to draft pending re-approval. Test names `TEST-002-hamiltonian-RF00X`
  must be re-derived to the new IDs.
- 2026-10-03 Added FR-012 + EC-018: explicit 001↔002 integration test over the
  implemented grid helpers; status back to draft pending re-approval.
- 2026-10-03 Added EC-019 after QA-2 (integration build-failure path) and EC-020
  after QA-3 (integration runs under the FR-011 record); clarified EC-013 as
  precedence proof   after QA-4.
- 2026-10-03 Clarified FR-012 after QA-1: fixed observable integration sequence
  (example B: build id, basis input, apply, oracle + zeros + immutability).

## 7. Test vectors

- 2x2 (`dim = 16`): `E(0) = 0`; `E(9) = 1`; `E(11) = 3`; `E(15) = 6`.
- 3x3 (`dim = 512`): `E(0) = 0`; `E(511) = 28`; `E(256) = 0`; `E(1) = 0`.
- 4x4 (`dim = 65536`): `E(0) = 0`; `E(65535) = 76`; `E(32768) = 0`; `E(1) = 0`;
  `E(0x8421) = 6`; `E(16770) = 0` (`0x4182`, queens at `(0,1),(1,3),(2,0),(3,2)`).
- Probes (all normalized): basis `|k>` bit-exact; phase `-1`/`i` bit-exact;
  `( |9> + |11> ) / sqrt(2)` within `1e-12`; `|16770>` gives zero.
