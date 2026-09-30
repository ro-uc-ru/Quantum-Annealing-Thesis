# 002 - Problem Hamiltonian (diagonal H_problem application)

- Status: Approved (2026-09-30). Explicit user approval recorded per
  constitution §4: user approved writing the spec, the 24-item QA review
  corrections, the norm gate, and activation (all on 2026-09-30).
- Active pointer: `specs/active-spec.md` points to 002-hamiltonian.
- Source: elicited 2026-09-30 (6 questions: scope, penalties, N-range,
  numerical errors, API contract, exclusions) plus 24-item QA review with
  per-item user rulings (2026-09-30).
- Scope: matrix-free application of the diagonal N-Queens problem Hamiltonian
  `out = H_problem |phi>`, plus a minimal demo CLI for the representative
  `leaks`/determinism run and a versioned CSV config record. No driver, no
  schedule, no evolution, no expectation value, no measurement beyond §4 probes.
- Target module: `hamiltonian` (problem operator), reusing `core` grid indexing
  (`QaGridId`, `pos = i * N + j`, `shift = numCells - 1 - pos`, MSB-first)
  from 001-states; plus a `qa-002-demo` CLI owned by this spec.
- Constitution binding: §3 (matrix-free), §6/§7/§8 (documented functions, single
  owner, checked errors incl. `uintptr_t` overlap and checked arithmetic), §10
  (unit tests incl. normalization gate and failure paths), §12
  (`NORM_TOL = 1e-12` applies to the norm gate and irrational scalings),
  §13 (CSV v1 record with explicit `N/A`).

## 1. Definitions

- `N`: board edge, `unsigned int`, strictly positive. This spec accepts
  `N in [2, 4]`. Negative values are impossible by type.
- `numCells`: `size_t`, `N * N` computed with checked arithmetic only after the
  `N`-gate passes; 4, 9, or 16. `N = 5` needs 25 bits / `dim = 33,554,432`
  and is reserved out of scope here (see RF-002).
- `dim`: `size_t`, `2^numCells` computed with checked shifts only after the
  `N`-gate passes; 16, 512, or 65536. Loop indices `k` are `size_t`,
  `0 <= k < dim`. The Hilbert-space index `k` is the `QaGridId` value from
  001-states; basis state `|k>` is the board whose bit-packed id is `k`.
- `queens(k)`: set of cells `(i, j)` with bit `1` in `k` under the 001-states
  MSB-first row-major mapping.
- Attack: distinct cells `a = (i1, j1)`, `b = (i2, j2)` attack iff they share a
  row (`i1 == i2`) or a column (`j1 == j2`) or a diagonal
  (`|i1 - i2| == |j1 - j2|`). One uniform rule for all `N`; it covers every
  diagonal (main, secondary, and short diagonals).
- Energy: `E(k) =` number of unordered attacking queen pairs in `queens(k)`.
  Simple `+1` count per pair, equal weight for row/column/diagonal. `E(k) = 0`
  iff the board has no attacking pair (includes the empty board, single queens,
  and valid N-Queens solutions). `E(k)` is a small exact integer:
  `0 <= E(k) <= C(Q,2)` (`Q` = queen count; 4x4 full board `E = 76`; verified
  vectors in §4). `E(k)` itself is exactly representable in `double`; products
  `E(k) * phi[k]` follow the exactness rules in §4 (dyadic cases bit-exact,
  irrational scalings within `NORM_TOL`).
- Operator: `H_problem = sum_k E(k) |k><k|`, diagonal in the computational basis.
  Application: `out[k] = E(k) * phi[k]` for all `k`, with `phi`, `out` arrays of
  `complex double` and length `dim`.
- Normative prototype (fixing finding 1/24):
  `QaStatus qaHamiltonianApplyProblem(unsigned int n, const complex double *phi, complex double *outPsi, size_t dim);`
  `n` is the board edge, `phi` the immutable input, `outPsi` the mutable output,
  `dim` the caller-declared length of both arrays.
- Matrix-free: the implementation MUST NOT allocate or materialize any
  `dim x dim` matrix (dense or sparse). `E(k)` is recomputed from the bit
  pattern of `k` (via 001-states indexing) and applied pointwise. Complexity
  `O(dim * numCells^2)` time / `O(1)` extra memory is an informative note, not
  an acceptance criterion (finding 5/24).
- Input/output contract (Schrodinger shape): `phi` is read-only (`const`, never
  mutated); `outPsi` is a separate mutable caller-owned buffer of identical
  length. The apply function allocates nothing. Overlap is defined as
  intersection of `[phi, phi + dim)` with `[outPsi, outPsi + dim)` compared via
  `uintptr_t` (finding 2/24); `phi == outPsi` is subsumed. Any overlap yields
  `QA_ERR_RANGE` (see RF-006).
- Ownership: caller owns both buffers and their release paths. On success only
  `outPsi[0..dim-1]` is written; on failure nothing is written: Buffers that
  exist and are non-NULL are bit-identical to pre-call (finding 3/24); NULL
  pointers are never dereferenced. The implementation MUST complete all checks
  (two-pass validate-then-compute) before the first write to `outPsi`.
- Error channel: every fallible function returns `QaStatus`. Reachable codes:
  `QA_OK`, `QA_ERR_RANGE` (bad `N`, NULL, aliasing/overlap, dim mismatch),
  `QA_ERR_UNSUPPORTED` (`N == 5`), `QA_ERR_DOMAIN` (non-finite amplitude or
  norm-gate failure), `QA_ERR_OVERFLOW` (any bounds-unsafe `N * N`,
  `2^numCells`, or `dim * sizeof` computation; checked always per finding
  14/24, defensive for valid `N`).
- Fixed validation order, before any dereference, shift, or float operation:
  `N`-gate (finding 14/24: no `numCells`/`dim` arithmetic before it passes),
  then pointer preconditions (`phi != NULL`, `outPsi != NULL`, `uintptr_t`
  non-overlap), then `dim == 2^numCells`, then finiteness of every `phi[k]`,
  then the norm gate below. Only then compute. Combined faults resolve by this
  order (finding 13/24): `N == 5` beats NULL/dim/finiteness (`UNSUPPORTED`);
  NULL beats dim/finiteness (`RANGE`); dim beats finiteness/norm (`RANGE`);
  finiteness beats norm (`DOMAIN`).
- Norm gate (finding 20/24, user ruling): `|phi>` MUST be normalized before use
  so that `<phi|H|phi>` reads as the energy. `norm = sqrt(sum_k |phi[k]|^2)`;
  if `norm` is non-finite or `|norm - 1| > 1e-12` (`NORM_TOL` per §12), return
  `QA_ERR_DOMAIN` and write nothing. Linearity probes therefore use normalized
  inputs only; unnormalized scalings (e.g. `2.5 * |k>`, the zero vector) are
  `DOMAIN` errors, not valid apply inputs.
- Determinism (finding 19/24): the apply is pure — two calls with identical
  (`n`, `phi`, `dim`) produce identical `outPsi`; no hidden state, no RNG, no
  globals.

## 2. Functional requirements (EARS)

- RF-001 (ubiquitous): The system shall define `E(k)` as the simple attacking-pair
  count (`+1` per pair sharing a row, column, or any diagonal with
  `|i1 - i2| == |j1 - j2|`), uniformly for all `N`.
  What: single penalty rule. Why: one testable energy; secondary/short diagonals
  included by construction.
- RF-002 (event): When `N == 5` is requested, the system shall return
  `QA_ERR_UNSUPPORTED` and write nothing, regardless of pointers/dim (N-gate first).
  What: reserve 5x5. Why: staged scope (2x2+3x3 mandatory, 4x4 expected, 5x5
  stretch); `dim = 33M` is a future-spec concern.
- RF-003 (event): When `N < 2` or `N > 5` is requested, the system shall return
  `QA_ERR_RANGE` (`QA_ERR_OVERFLOW` only for a genuinely overflowing width
  computation) and write nothing.
  What: strict range gate on a positive `unsigned int`, evaluated before any
  `numCells`/`dim` arithmetic. Why: prevent invalid shifts, masks, and counts (§8).
- RF-004 (ubiquitous): The system shall apply `H_problem` matrix-free as
  `outPsi[k] = E(k) * phi[k]` for `k = 0..dim-1` with `dim = 2^(N*N)`, reading the
  001-states bit pattern of each `k` to recompute `E(k)` pointwise.
  What: `H|phi>` without any matrix allocation. Why: constitution §3; `dim = 65536`
  max here keeps the two-vector working set small.
- RF-005 (ubiquitous): The system shall expose exactly
  `QaStatus qaHamiltonianApplyProblem(unsigned int n, const complex double *phi, complex double *outPsi, size_t dim)`,
  treat `phi` as immutable `const` input and `outPsi` as a separate mutable output
  of identical length owned by the caller; the function shall allocate nothing.
  What: Schrodinger-shaped contract. Why: initial state never mutates (001-states
  precedent); aliasing bugs become assertable errors.
- RF-006 (unwanted): If `phi == NULL` or `outPsi == NULL`, or the `uintptr_t`
  ranges `[phi, phi + dim)` and `[outPsi, outPsi + dim)` intersect, or
  `dim != 2^numCells`, the system shall return `QA_ERR_RANGE` and write nothing
  (no partial writes).
  What: fail-safe API shape. Why: checked pointers and bounds (§8).
- RF-007 (unwanted): If any `phi[k]` is non-finite (`NaN` or `Inf` in real or
  imaginary part), the system shall return `QA_ERR_DOMAIN` and write nothing.
  The full finiteness scan MUST complete before the norm gate and before the first
  write to `outPsi`.
  What: two-pass validate-then-compute. Why: checked numerical-domain errors (§8).
- RF-008 (state): The system SHALL require `|norm(phi) - 1| <= 1e-12` with
  `norm = sqrt(sum_k |phi[k]|^2)`; otherwise it SHALL return `QA_ERR_DOMAIN`
  and write nothing. The norm gate runs after the finiteness scan and before any
  write.
  What: normalized-input gate (user ruling 2026-09-30). Why: `<phi|H|phi>` is the
  energy only for normalized `|phi>`; constitution §10 normalization coverage is
  met by these gate tests, and §12 `NORM_TOL` applies here. Exception rationale
  for §10/§12: linearity is preserved mathematically, but the API only serves
  normalized states; unnormalized service belongs to no staged spec.
- RF-009 (ubiquitous): The system shall be deterministic: identical
  (`n`, `phi`, `dim`) inputs yield identical `outPsi` across calls; no hidden
  state, no RNG, no globals.
  What: purity. Why: constitution §13 determinism; assertable reproducibility.
- RF-010 (ubiquitous): The system shall provide a minimal demo CLI `qa-002-demo`
  that builds one representative normalized N=4 input, applies `H_problem`,
  checks the expected energy, and returns zero/non-zero. It exists solely for the
  representative run and the `leaks` check (finding 6/24).
  What: specified run target. Why: `leaks --atExit --` needs a named executable;
  scope adds no physics beyond §4.
- RF-011 (ubiquitous): The system shall record its configuration as versioned CSV
  `results/002-config.csv` with header `v1` and fields
  `spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed,schedule,dt_steps`
  (finding 7/24, CSV chosen as dependency-free). `seed`, `schedule`, `dt_steps`
  are `N/A` with rationale "no evolution/sampling in this spec" (constitution §13
  made explicit per-spec).

Out of scope: `H_driver` (transverse field), any `H(s)` interpolation or schedule
`s(t)`, Trotter/split-operator steps, `<phi|H|phi>` expectation and variance as
library calls, eigensolvers, measurement/sampling, Metal/GPU backends. The demo CLI
and CSV writer are the only IO in scope.

## 3. Edge cases

| # | Input | Expected |
|---|-------|----------|
| EC-01 | `N = 0, 1, 6, UINT_MAX` (`unsigned int`) | `QA_ERR_RANGE` (or `OVERFLOW` for a genuinely overflowing width op), nothing written; NULL pointers never dereferenced |
| EC-02 | `N = 5` (any pointers/dim, incl. NULL) | `QA_ERR_UNSUPPORTED`, nothing written (N-gate first) |
| EC-03 | `dim != 2^numCells` (e.g. N=2 dim 15/17, N=3 dim 511/513, N=4 dim 65535/65537, dim 0, `SIZE_MAX`) | `QA_ERR_RANGE`, existing `outPsi` untouched |
| EC-04 | `phi == NULL` or `outPsi == NULL` (with valid `N`) | `QA_ERR_RANGE`, nothing written |
| EC-05 | `phi == outPsi` or intersecting `uintptr_t` ranges | `QA_ERR_RANGE`, buffers unchanged (no partial in-place update) |
| EC-06 | Any `phi[k]` with non-finite real or imaginary part (`NaN`, `Inf`, `-Inf`), including a single poisoned index | `QA_ERR_DOMAIN`, existing `outPsi` bit-identical to pre-call |
| EC-07 | Basis inputs `phi = |k>` with `k = 0` and `k = dim - 1` (unit amplitude at one index, zeros elsewhere) | `outPsi[k] = E_full * 1` with `E_full = 6/28/76` for N=2/3/4; `outPsi[0] = 0` for `k = 0` |
| EC-08 | Single-queen basis states (`k = 1 << (numCells-1)` queen at `(0,0)`; `k = 1` queen at `(N-1,N-1)`) | `outPsi[k] = 0` (no pair exists) |
| EC-09 | Zero vector and any unnormalized finite vector (e.g. `2.5 * |k>`, unnormalized `|9> + |11>` without `/sqrt(2)`) | `QA_ERR_DOMAIN` (norm gate), existing `outPsi` untouched |
| EC-10 | Input `phi` after a successful or failed call | Bit-identical to pre-call (immutability assert over the full `dim`) |
| EC-11 | Failed call (any error) | Existing `outPsi` bit-identical to pre-call (no-partial-write; forces two-pass). `+0.0` vs `-0.0` in zero outputs count as equal (finding 17/24) |
| EC-12 | Linearity probe with normalized inputs, e.g. `phi = (|x> + |y>) / sqrt(2)` vs separate `H|x>`, `H|y>` | Agreement within `1e-12` (irrational scaling); dyadic unit-norm scalings (`1.0`, `-1.0`, `I`) bit-exact |
| EC-13 | Combined `N = 5` with NULL pointers or wrong `dim` or non-finite `phi` | `QA_ERR_UNSUPPORTED` (precedence proof) |
| EC-14 | Valid `N`, valid pointers, wrong `dim`, non-finite `phi` present | `QA_ERR_RANGE` (dim before finiteness proof) |
| EC-15 | Partial overlaps: `outPsi == phi + 1`, one-element overlap, same block with mismatched `dim` | `QA_ERR_RANGE`, buffers unchanged |
| EC-16 | `DOMAIN`-triggering `phi` with `outPsi` pre-filled with non-finite canary (`NaN+Inf*I`) | `QA_ERR_DOMAIN`, `outPsi` bit-identical to canary |
| EC-17 | Two identical calls (`n`, `phi`, `dim` equal, distinct `outPsi` buffers) | Bit-identical outputs (determinism proof) |

Width note: after the `N`-gate, `numCells <= 16` and `dim <= 65536`, so
`1u << numCells` is safe; the `OVERFLOW` path stays mandatory-checked (finding
14/24) for future `N` and for `dim * sizeof (complex double)` sizing.

## 4. Test vectors (asserts, not ground truth)

Energies are fixed oracles (`E(k)` = attacking pairs). Valid apply vectors assert
`obtained == expected` elementwise on `outPsi[k] = E(k) * phi[k]`; all valid apply
inputs below are normalized (RF-008). Failure probes assert untouched-`outPsi`.

- 2x2 (`dim = 16`): `E(0) = 0` (empty); `E(9) = 1` (`0b1001`, diagonal pair);
  `E(11) = 3` (`0b1011`, row+column+diagonal); `E(15) = 6` (full, all 6 pairs attack).
- 3x3 (`dim = 512`): `E(0) = 0`; `E(511) = 28` (full); `E(256) = 0` (queen only
  at `(0,0)`); `E(1) = 0` (queen only at `(2,2)`).
- 4x4 (`dim = 65536`): `E(0) = 0`; `E(65535) = 76` (full); `E(32768) = 0`
  (queen only at `(0,0)`); `E(1) = 0` (queen only at `(3,3)`);
  `E(0x8421) = 6` (main-diagonal queens, all `C(4,2)` pairs attack);
  `E(16770) = 0` (`0x4182`, valid 4-queens solution with queens at
  `(0,1),(1,3),(2,0),(3,2)`).
- Application probes (per supported `N`, all inputs normalized):
  - Basis: `phi = |k>` gives `outPsi[k] = E(k)`, `outPsi[j != k] = 0` bit-exact,
    for each `k` above.
  - Phase: `phi = -|k>`, `phi = i|k>` give `outPsi[k] = -E(k)`, `iE(k)` bit-exact.
  - Superposition: `phi = (|9> + |11>) / sqrt(2)` (2x2, norm 1) gives
    `outPsi[9] = 1/sqrt(2)`, `outPsi[11] = 3/sqrt(2)`, zeros elsewhere, within
    `1e-12` (irrational scaling).
  - Zero-energy preservation: normalized `phi` supported only on `E = 0` boards
    (e.g. `phi = |16770>` on 4x4) gives `outPsi == 0` (`+0.0`/`-0.0` accepted).
- Failure probes MUST cover every EC-01..EC-06, EC-09, EC-13..EC-16 row with
  untouched-`outPsi` asserts (pre-fill with finite canary `0.25+0.75i` and with
  non-finite canary per EC-16, compare appropriately after each rejected call).

Unit tests MUST cover all EC rows, all vectors above, diagonality
(off-`k` outputs are `0` for basis inputs), the norm gate (normalized accept /
unnormalized `DOMAIN` incl. zero vector), determinism (EC-17), and failure paths
(NULL, aliasing/overlap, dim mismatch, `N`-gate, non-finite scan).

## 5. Traceability and acceptance

- Each RF maps to unit tests named `TEST-002-hamiltonian-RF00X` (RF-010 maps to a
  CLI test, RF-011 to a CSV-format test) and to a `hamiltonian` module following
  `docs/codestyle.md` (`f -> a` spacing, `QaStatus` channel, `const` input,
  caller-owned buffers, no allocation, no `exit()` in library code).
- Function documentation follows constitution §6 (purpose, ownership, errors,
  numerical assumptions); the template itself lives in the constitution and
  `docs/codestyle.md` and is not duplicated here.
- Recorded configuration: `results/002-config.csv` (`v1`) per RF-011.
  Integration tests: N/A in this spec with rationale — the only scientific
  outcomes here (`E(k)` table incl. 4x4 solution energy `0`) are asserted as
  unit oracles; annealing-level outcomes belong to future evolution specs
  (constitution §11 applied per-spec with this rationale).
- Completion criteria (all MUST hold):
  1. Debug build passes: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug &&
     cmake --build build`.
  2. Full `ctest --test-dir build --output-on-failure` green, including new
     `TEST-002-hamiltonian-*` plus non-regression of `TEST-001-states-*`.
  3. ASan+UBSan clean on the 002 tests
     (`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`).
  4. `leaks --atExit -- ./build/qa-002-demo` clean (representative N=4 run;
     apply allocates nothing, so zero heap growth attributable to `H`).
  5. Numerical validation: every §4 vector holds (`E(k)` exact; dyadic unit-norm
     applies bit-exact; irrational scalings/linearity within `1e-12`);
     norm gate rejects unnormalized with `DOMAIN`; immutability and
     no-partial-write asserts hold (`±0.0` equal).
  6. `results/002-config.csv` present, `v1`, fields per RF-011.
  7. Exact commands, git sha, clang version, and results reported per `AGENTS.md`;
     any criterion not run is listed with its reason.
- Approval: explicit user approval recorded here — Approved (2026-09-30) —
  before any implementation (constitution §4).
