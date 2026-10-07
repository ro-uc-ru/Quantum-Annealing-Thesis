# 004-driver: Transverse (driver) Hamiltonian at t = 0 and initial state

- Status: approved
- Owner: Roger
- Date: 2026-10-06

## 1. Context

### Goal (required)
Provide matrix-free application of the transverse-field (driver) Hamiltonian
`H_driver = sum_c sigma^x_c` (`out = H_driver |phi>`) and construction of the
annealing initial state, the ground state of `H(0)`, for exact-state studies on
2x2 through 4x4, accepting only normalized inputs within `NORM_TOL = 1e-12`.

### Why (required)
At `t = 0` the schedules of 003 give `b(0) = 1`, `a(0) = 0` (within `SCHED_TOL`),
so `H(0) = H_driver`
and the anneal starts from its ground state. Without a tested `H_driver` and a
verified initial state, no evolution step of the future split-operator spec can
be validated.

### Optional
- Users / Beneficiaries: the future evolution / split-operator spec (consumes
  `H_driver` and the initial state); the thesis, which needs reproducible
  2x2-4x4 exact-state results.
- Definitions:
  - `N`, `numCells`, `dim`, index `k`, `|k>`, `QaStatus` codes and the
    `N`-gate: exactly as in 002-hamiltonian (`N in [2, 4]` accepted, `N = 5`
    reserved, `dim = 2^numCells`).
  - Cell and bit mapping (001-states, MSB-first row-major): cell `c = i * N + j`
    (`0 <= c < numCells`) is bit position `numCells - 1 - c` of `k`. The
    single-bit mask of cell `c` is `m_c = 1 << (numCells - 1 - c)`.
  - `sigma^x_c`: Pauli-X on the qubit of cell `c`; flips the bit selected by
    `m_c`.
  - `H_driver = sum_{c=0}^{numCells-1} sigma^x_c` (sign `+`, no `1/2` factor).
    It is the transverse term `delta * sum sigma^x` of TFG Eq. (6),
    `H(t) = a(t) H_target + b(t) delta sum sigma^x`, with `delta = 1`: `delta`
    only fixes the energy units and is not a parameter of this spec. Only the
    schedule method of Eq. (6) is used; the `Gamma(t)` method (Eq. 5) is not.
    Action on the basis: `H_driver |k> = sum_c |k XOR m_c>`. Application on
    amplitudes: `out[k] = sum_c phi[k XOR m_c]`, summed in ascending `c` (that
    is, from the most significant bit down). Matrix-free: no `dim x dim` matrix
    (dense or sparse) is ever allocated or materialized.
  - Sign convention: `+sum sigma^x` with ground state `|->^n` (confirmed against
    the TFG). It is equivalent to `-sum sigma^x` with `|+>^n` through the
    unitary `Z^(x n)`, which commutes with the diagonal `H_problem`, so
    computational-basis outcome probabilities are identical.
  - Spectrum: eigenvalues `numCells - 2 p` (`p = 0..numCells`) on the Hadamard
    basis, so `||H_driver|| = numCells` and `H_driver` is Hermitian.
  - `|psi0> = |->^numCells`: initial state, `psi0[k] = (-1)^popcount(k) * s`
    with `s = 1.0 / sqrt((double) dim)`. It is the ground state of `H_driver`:
    `H_driver |psi0> = -numCells |psi0>`. (`|+>^numCells` is the maximum-energy
    state, `+numCells`.)
  - Norm: `||phi|| = sqrt(<phi|phi>) = sqrt(sum_k (re(phi[k])^2 + im(phi[k])^2))`
    in `double`, as in 002. The sum is accumulated sequentially in ascending
    `k`, with no OpenMP and no parallel reduction in this spec. The gate is
    `| ||phi|| - 1 | <= NORM_TOL`. (`<phi|phi> = ||phi||^2` is the squared norm
    and is not used as the gate.)
    Plain sequential summation is the decision for now; IF EC-026 fails, THEN
    the sum moves to pairwise summation (fixed base block, ascending `k`) through
    a spec change, never a silent implementation change.
  - Contracts (same conventions as 002: `phi` immutable `const` input, `outPsi`
    a separate mutable caller-owned output of identical length, nothing
    allocated, overlap compared via `uintptr_t`):
    `QaStatus qaHamiltonianApplyDriver(unsigned int n, const complex double *phi, complex double *outPsi, size_t dim);`
    `QaStatus qaHamiltonianInitialState(unsigned int n, complex double *outPsi, size_t dim);`
  - Validation order (apply): `N`-gate, `dim == 2^numCells` (before any pointer
    arithmetic, so every later address computation is bounded by `dim <= 65536`),
    pointer and overlap preconditions, finiteness of every `phi[k]`, norm gate.
    Initial state: `N`-gate, `dim`, pointer. All checks complete before the
    first write; on failure existing non-NULL buffers are unchanged.
    `QA_ERR_OVERFLOW` is unreachable in both functions because the `N`-gate
    runs before any width arithmetic.
  - Configuration record: `results/004-config.csv` is written by a C writer in
    `src/io`, called by the demo CLI:
    `QaStatus qaIoWriteConfig(const char *path, const QaConfigRecord *record);`
    `QaConfigRecord` holds ten NUL-terminated, non-NULL strings, one per CSV
    column (`specVersion`, `n`, `dim`, `vectors`, `gitSha`, `clangVersion`,
    `cmakeFlags`, `seed`, `schedule`, `dtSteps`); the caller owns them and the
    writer only reads them. The demo takes `gitSha`, `clangVersion` and
    `cmakeFlags` from build-time compile definitions supplied by CMake.
    - The writer only writes: it never creates directories. The directory of
      `path` must exist; `results/` under the project root is created by the
      build setup (CMake configure), never by the writer or the demo.
    - Bounds: `path` at most 1024 bytes and every field at most 4096 bytes
      (excluding the NUL); all are validated before any file is created.
    - Format: RFC 4180. A field is quoted when it contains a comma, a double
      quote, CR or LF, and embedded double quotes are doubled. Rows end with
      `\n`.
    - The writer allocates no heap memory (fields are streamed to the file), so
      it has no `QA_ERR_NOMEM` path. Codes: `QA_OK`, `QA_ERR_RANGE` (NULL
      argument, or path or field over its bound), `QA_ERR_IO` (any file
      failure). On success an existing target is replaced entirely.
  - Determinism: pure functions of their inputs; no hidden state, no RNG.

## 2. Functional Requirements

- FR-001 (ubiquitous) The system shall define `H_driver = sum_c sigma^x_c` with
  sign `+` and no scale factor, so that `H_driver |k> = sum_c |k XOR m_c>`,
  uniformly for all `N`.
- FR-002 (event-driven) WHEN `N == 5` is requested, the system shall return
  `QA_ERR_UNSUPPORTED` from both functions and write nothing, regardless of
  pointers or dim.
- FR-003 (event-driven) WHEN `N < 2` or `N > 5` is requested, the system shall
  return `QA_ERR_RANGE` from both functions and write nothing.
- FR-004 (ubiquitous) The system shall apply `H_driver` matrix-free as
  `outPsi[k] = sum_c phi[k XOR m_c]` for `k = 0..dim-1`, with a fixed summation
  order (ascending `c`).
- FR-005 (ubiquitous) The system shall expose exactly the two signatures of
  section 1; shall not modify `phi`; shall write only within
  `outPsi[0..dim-1]`; and shall hold no heap allocation and no state across
  calls. (How each is verified is decided in the plan and tasks.)
- FR-006 (unwanted) IF `dim != 2^numCells`, or a required pointer is `NULL`, or
  the `uintptr_t` ranges of `phi` and `outPsi` overlap, THEN the system shall
  return `QA_ERR_RANGE` and write nothing; `dim` is checked first (section 1).
- FR-007 (unwanted) IF any `phi[k]` is non-finite in real or imaginary part,
  THEN `qaHamiltonianApplyDriver` shall return `QA_ERR_DOMAIN` and write nothing,
  completing the full scan before the norm gate and before any write.
- FR-008 (unwanted) IF `||phi||` is non-finite or `| ||phi|| - 1 | > 1e-12`,
  THEN `qaHamiltonianApplyDriver` shall return `QA_ERR_DOMAIN` and write
  nothing.
- FR-009 (ubiquitous) The system shall be deterministic: identical inputs shall
  yield bit-identical outputs from both functions.
- FR-010 (ubiquitous) `qaHamiltonianInitialState` shall write
  `psi0[k] = (-1)^popcount(k) * s` (`s` as in section 1) with imaginary part
  `+0.0` for `k = 0..dim-1`, and `| ||psi0|| - 1 | <= 1e-12`.
- FR-011 (ubiquitous) The initial state shall be the ground state of `H(0)`:
  `H_driver |psi0> = -numCells |psi0>` within `1e-12` per amplitude, and the
  energy `<psi0| H_driver |psi0> = -numCells` within `1e-12`.
- FR-012 (ubiquitous) The system shall provide an integration test that builds
  board ids with the 001-states `withBit` helper for `N` in {2, 3, 4} and
  checks, for each board: (a) `psi0[id] = (-1)^(number of queens) * s` (the test counts the queens
  itself); (b)
  `H_driver |id>` is `1` exactly at the ids obtained by toggling each single
  cell (reading it with `qaGridGetBit`, then `qaGridWithBit` with the opposite
  bit), and `0` elsewhere (this ties `m_c` to the 001 mapping);
  (c) FR-011. A failing helper call fails that case, reports the failing step
  and the Hamiltonian functions are not called on the partially built id.
- FR-013 (ubiquitous) The system shall provide the minimal demo CLI
  `qa-004-demo` (one optional argument, the CSV output path, default
  `results/004-config.csv` relative to the working directory; representative
  run `./build/qa-004-demo` from the project root) that
  builds the N=4 initial state, applies `H_driver`, checks that every amplitude
  of the result equals `-16 * psi0[k]` within `1e-12`, writes the configuration
  record of FR-014 and returns zero only if every step returned `QA_OK` and the
  check holds, non-zero otherwise.
- FR-014 (ubiquitous) The system shall record its configuration as versioned CSV
  `results/004-config.csv` (`v1`) with exactly the header
  `spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed,schedule,dt_steps`
  and one data row. For the demo: `spec_version = v1`, `N = 4`, `dim = 65536`,
  `vectors = 4:psi0`, `git_sha`, `clang_version` and `cmake_flags` of the build,
  and `seed`, `schedule`, `dt_steps` equal to `N/A`.
- FR-015 (unwanted) IF the CSV file cannot be created, written completely or
  closed, THEN the writer of FR-014 shall return `QA_ERR_IO`, release every
  resource it opened or allocated, and leave the target path unchanged (an
  existing file stays bit-identical; an absent file stays absent). IF the path
  or record argument is `NULL`, or the path or any field exceeds its bound
  (section 1), THEN it shall return `QA_ERR_RANGE` and touch nothing.
- FR-016 (ubiquitous) The system shall make `qaHamiltonianApplyDriver` satisfy,
  for normalized inputs and within `1e-12`: (a) `H_driver |+>^numCells =
  +numCells |+>^numCells`; (b) `||H phi|| <= numCells`; (c) Hermiticity
  `<x| H y> = <H x| y>`; (d) linearity, for orthonormal `x`, `y` (e.g. two distinct basis states),
  `H (|x> + |y>) / sqrt(2)` equals the sum of the separate results scaled by
  `1 / sqrt(2)`. EC-009..EC-012 are its tests.

## 3. Edge Cases

- EC-001 IF `N` is 0, 1, 6, or `UINT_MAX`, THEN both functions shall return
  `QA_ERR_RANGE` without dereferencing NULL. (FR-003)
- EC-002 IF `N == 5` with any pointers or dim (including NULL or non-finite
  `phi`), THEN both functions shall return `QA_ERR_UNSUPPORTED` and write
  nothing (the `N`-gate runs first). (FR-002)
- EC-003 IF `dim != 2^numCells` (15/17, 511/513, 65535/65537, 0, `SIZE_MAX`),
  THEN both functions shall return `QA_ERR_RANGE` and leave `outPsi` untouched,
  also when a pointer is `NULL` or the buffers overlap (`dim` is checked
  first). (FR-006)
- EC-004 IF `phi == NULL` or `outPsi == NULL` with valid `N` and `dim`, THEN the
  system shall return `QA_ERR_RANGE` and write nothing. (FR-006)
- EC-005 IF `phi == outPsi` or the ranges partially overlap (`outPsi == phi + 1`),
  THEN apply shall return `QA_ERR_RANGE` with buffers unchanged. (FR-006)
- EC-006 IF any `phi[k]` is non-finite (including one poisoned index) or `outPsi`
  is pre-filled with a non-finite canary, THEN apply shall return
  `QA_ERR_DOMAIN` with `outPsi` bit-identical to the canary. (FR-007)
- EC-007 IF `phi` is the zero vector or any unnormalized finite vector (e.g.
  `2.5 * |k>`, or a vector whose squares overflow to infinity), THEN apply
  shall return `QA_ERR_DOMAIN` with `outPsi` untouched. (FR-008)
- EC-008 IF `phi = |k>`, THEN `outPsi` shall have value `1` at exactly the
  `numCells` indices `k XOR m_c` and `0` elsewhere (e.g. `|0>`, N=2: indices
  1, 2, 4, 8). (FR-004)
- EC-009 IF `phi = |+>^numCells` (all amplitudes `1/sqrt(dim)`), THEN `outPsi`
  shall equal `+numCells * phi` within `1e-12` per amplitude. (FR-016)
- EC-010 IF `phi` is any normalized vector, THEN `||outPsi|| <= numCells + 1e-12`
  (operator norm); the output is not required to be normalized. (FR-016)
- EC-011 IF two normalized vectors `x`, `y` are given, THEN
  `<x| H y> == <H x| y>` within `1e-12` (Hermiticity). (FR-016)
- EC-012 IF `x`, `y` are orthonormal and `phi = (|x> + |y>) / sqrt(2)` is applied alongside separate
  `H|x>`, `H|y>`, THEN the results shall agree within `1e-12` (linearity).
  (FR-016, FR-009)
- EC-014 IF any call fails, THEN the existing `outPsi` shall be unchanged
  (`+0.0` vs `-0.0` in zero outputs count as equal). (FR-002, FR-003, FR-006,
  FR-007, FR-008)
- EC-015 IF `qaHamiltonianInitialState` is called with `N = 2, 3, 4`, THEN
  `psi0[0] = +s`, `psi0[dim-1] = (-1)^numCells * s`, and `psi0[k]` has
  imaginary part exactly `+0.0`. (FR-010)
- EC-016 IF `N = 2` (`dim = 16`, amplitude `0.25` exact), THEN `psi0` shall be
  bit-exact and `H_driver |psi0> = -4 |psi0>` bit-exact; for `N = 3, 4` it
  holds within `1e-12`. (FR-010, FR-011)
- EC-017 IF `qaHamiltonianInitialState` is applied to `outPsi` pre-filled with a
  canary and a precondition fails (N, pointer or dim), THEN the canary shall
  be bit-identical afterwards. (FR-002, FR-003, FR-006, FR-010)
- EC-020 IF the demo has run, THEN reading back `results/004-config.csv` shall
  yield exactly the FR-014 header and one row whose `spec_version`, `N`, `dim`,
  `vectors`, `seed`, `schedule` and `dt_steps` equal the demo's parameters and
  whose `git_sha`, `clang_version` and `cmake_flags` are non-empty.
  (FR-012, FR-014)
- EC-021 IF the CSV directory does not exist (the writer does not create it) or
  is not writable, THEN the writer shall return `QA_ERR_IO`, create no file
  and leak nothing (the leak check of the DoD covers this test). (FR-015)
- EC-022 IF a write or close failure is injected after the file was opened
  (injection mechanism decided in the plan), THEN the writer shall return
  `QA_ERR_IO`, close the file and leave no partial file at the target path.
  (FR-015)
- EC-023 IF the target path already holds a valid CSV and a failure occurs
  (EC-021 or EC-022 conditions), THEN that file shall be bit-identical
  afterwards. (FR-015)
- EC-024 IF the path or the record argument is `NULL` (or a record string is
  `NULL`), THEN the writer shall return `QA_ERR_RANGE` and create no file.
  (FR-015)
- EC-025 IF the writer returns non-`QA_OK` during the demo, THEN the demo shall
  exit non-zero. (FR-013, FR-015)
- EC-026 IF `phi` is a normalized vector with `dim = 65536` and amplitudes that
  are not powers of two (deterministic, built in the test, e.g. `x[k] = 1 + k`
  scaled by `1 / ||x||`, with the reference norm computed in `long double`,
  independent of the code under test), THEN apply shall accept it (`QA_OK`): the rounding
  of the sequential norm sum must not reject a legitimately normalized input.
  (FR-008)
- EC-027 IF a record field contains a comma, a double quote, CR or LF, THEN the
  written CSV shall quote and escape it per RFC 4180 and parsing it back shall
  yield the original string. (FR-014)
- EC-028 IF the path exceeds 1024 bytes or any field exceeds 4096 bytes (and
  exactly at each bound it is accepted), THEN the writer shall return
  `QA_ERR_RANGE`, create no file and leave an existing target bit-identical.
  (FR-015)
- EC-029 IF the target path already holds a CSV and the write succeeds, THEN the
  file shall contain exactly the new header and row (nothing of the old
  content). (FR-014)

## 4. Scope

### In scope
- Matrix-free `H_driver` application with finiteness and norm gates.
- Initial state `|->^numCells` and its verification as ground state of `H(0)`.
- 001 / 004 integration test, demo CLI, and the versioned CSV record with its
  writer and I/O failure handling.

### Out of scope
- `H(t) = b(t) H_driver + a(t) H_problem` for general `t`: belongs to the
  evolution spec.
- Exponentials `exp(-i dt H_driver)` and split-operator steps: evolution spec.
- Choice of `|+>^n` as initial state or other signs of the driver: the
  convention is fixed here to `+sum sigma^x` with ground state `|->^n`.
- Integration with evolved states `|phi>` and with `H_problem` (combined `H(t)`,
  step-by-step norm preservation): depends on the integrator, so it belongs to
  the evolution spec.
- Parallel (OpenMP) execution of apply or of the norm reduction: future spec.
- Eigensolvers, measurement and sampling: future specs.
- `N = 5` execution (`dim = 2^25`): reserved, as in 002.
- Metal/GPU backends: optional future optimization, never required.

## 5. Definition of Done

- [x] FR-001..FR-016 covered by tests
- [x] EC-001..EC-029 (except removed IDs) covered by tests
- [x] Strict debug build passes, `-Wall -Wextra -Werror`, zero warnings
  (`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`)
- [x] Full `ctest --test-dir build --output-on-failure` green, with
  non-regression of 001, 002 and 003 tests
- [x] ASan+UBSan clean on the 004 tests and on `qa-004-demo`
- [x] Zero leaks on every 004 test and on `qa-004-demo` (representative N=4
  run): `leaks --atExit` on macOS, LeakSanitizer on Linux
- [x] Numerical validation holds (eigen-equation `H|psi0> = -numCells |psi0>`,
  `|+>^n` eigenvalue `+numCells`, Hermiticity, norm bound, norm gate enforced)
- [x] `results/004-config.csv` present after the representative run from the
  project root, `v1`, with fields per FR-014
- [x] ARC-2 / ARC-6 documentation audit (`arc6-doc-auditor`) clean on the 004
  sources, tests and headers

## 6. Changelog

- 2026-10-06 Edits after QA review round 1: added FR-015 and EC-021..EC-025
  (CSV writer I/O failure and memory handling, QA-1); DoD aligned with MEM-2,
  MEM-3, LIM-1 and STK-5 (QA-2); cell/bit mapping made explicit as `m_c` (QA-3);
  norm defined as `sqrt(<phi|phi>)` as in 002, sequential, no OpenMP, with
  EC-010 and new EC-026, plain summation kept with a pairwise fallback (QA-4); FR-014 header and row fixed, EC-020 made testable by
  read-back (QA-5); sign convention and `Z^(x n)` equivalence recorded (QA-6);
  FR-012 reworked, EC-019 removed (QA-7); `dim` validated before pointers and
  overlap, FR-006/EC-003 updated (QA-8); FR-008 reworded as unwanted behavior
  (QA-10); `QA_ERR_OVERFLOW` removed from FR-003/EC-001 (QA-11); FR-005
  reworded, verification deferred to plan (QA-12); demo arguments and tolerance
  fixed and `psi0` formula fixed (QA-13); EC-014/EC-017 now cite FR-002 and
  FR-003 (QA-14). Round 1, QA-9: added FR-016 (mathematical properties,
  tested by EC-009..EC-012, now citing it) and removed EC-013 and EC-018.
- 2026-10-06 Edits after QA review round 2: the writer only writes and the
  directory is created by the build setup, demo takes an optional output path
  (QA-15); writer contract completed with record, bounds, RFC 4180 escaping,
  no heap allocation and replace-on-success, plus EC-027..EC-029 (QA-16);
  linearity inputs made orthonormal in FR-016/EC-012 (QA-17); FR-012 toggling
  specified with `qaGridGetBit` and `qaGridWithBit` (QA-18); EC-026 reference
  norm in `long double` (QA-19); `SCHED_TOL` added to Why (QA-20); ARC-6 audit
  added to the DoD (QA-21).

## 7. Test vectors

- `N = 2` (`dim = 16`): `H|0>` has ones at indices 1, 2, 4, 8; `psi0[k] = (-1)^popcount(k) / 4`,
  `H psi0 = -4 psi0` exact.
- `N = 3` (`dim = 512`): `H psi0 = -9 psi0`, `H |+>^9 = +9 |+>^9` within `1e-12`.
- `N = 4` (`dim = 65536`): `H psi0 = -16 psi0`, `<psi0|H|psi0> = -16` within `1e-12`.
