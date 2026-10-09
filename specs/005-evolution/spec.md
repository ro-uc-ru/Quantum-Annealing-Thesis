# 005-evolution: Time evolution of the annealing state (second-order split-operator)

- Status: approved
- Owner: Roger
- Date: 2026-10-09

## 1. Context

### Goal (required)
Solve the time-dependent Schrodinger equation `i d|phi>/dt = H(t)|phi>` for
`H(t) = a(t) H_target + b(t) H_driver` with a matrix-free, unitary, second-order
(Strang) split-operator integrator, starting from the ground state of `H(0)`
(004-driver), for exact-state studies (2x2 and 3x3 mandatory; 4x4 expected and
non-blocking; 5x5 reserved), and write the final probabilities, per-step
observables (norm, `<H(t)>`) and probability snapshots as machine-readable
records. The probability of measuring a ground state is NOT computed by the C
code: it is obtained by data analysis from those records.

### Why (required)
Specs 001-004 provide states, `H_problem`, schedules and `H_driver` with the
initial state, but nothing yet evolves the state. Without a validated
integrator no annealing result (the thesis' 2x2 and 3x3 solutions, and 4x4 as
the expected goal) can be produced.

### Optional
- Users / Beneficiaries: the thesis, which needs reproducible exact annealing
  results comparable with the TFG baseline (3x3, linear schedules, `T = 100 t0`:
  eight degenerate solutions; the TFG figure of about 0.998 total probability is
  NOT reproduced by this model, which gives about 0.987, see FR-023); later specs (OpenMP, spectrum, larger boards).
- Definitions:
  - `N`, `numCells`, `dim`, index `k`, `|k>`, `QaStatus`, cell/bit mapping and
    the `N`-gate: exactly as in 002/004 (`N in [2, 4]` accepted, `N = 5`
    reserved, `dim = 2^numCells`).
  - Units: `hbar = 1`, `delta = 1` (energy unit), time unit `t0 = hbar/delta = 1`.
  - `r = epsilon/delta = 0.5`: a FIXED CONSTANT of the problem, not a
    parameter. TFG II.A: it maximizes the gap between the ground and first
    excited energies. It shall be documented in the code (scope comment of
    the header that defines `H_target`, citing this spec) and recorded as
    `r` in `config.json`.
  - `q(k)`: number of queens (set bits) of board `k`. `E(k)`: attacking-pair
    count of 002.
  - `H_target(k) = E(k) - r q(k)`: diagonal, matrix-free (TFG Eq. 1-2 with
    `delta = 1`). It extends 002's `H_problem` with the queen-reward term; 002 is
    not modified. With `r = 0.5` its values are multiples of 0.5, exact in
    floating point. Its ground set is the set of `k` with minimal `H_target(k)`.
  - `H_driver = sum_c sigma^x_c`: as in 004. `H(t) = a(t) H_target + b(t) H_driver`
    (TFG Eq. 6) with `a`, `b` from 003 (four families, total time `T`). Contract
    of 003 assumed by this spec for ALL four families without exception, within
    `SCHED_TOL = 1e-12` (003 FR-003): `a(0) = 0`, `b(T) = 0`, `a` goes from 0 to 1
    and `b` from 1 to 0. Hence `H(0) = H_driver` and `H(T) = H_target` up to
    `SCHED_TOL`, and the ground state of `H(0)` is
    the 004 initial state `|->^numCells`.
  - Responsibilities: the `evolution` library evolves the state and reports through two optional caller-supplied callbacks, an observer (called in order for `j = 0..steps` with `t_j`, `a`, `b`, `norm`, `<H>`) and a snapshot sink (called with `p_k` for the `j` of FR-012); each returns a `QaStatus` and any non-`QA_OK` value stops the evolution and is propagated. Both may be NULL (pure evolution, no files). `evolution` does not depend on `io`. The CLI parses and validates its arguments, checks the `--out` rules (EC-017) without creating anything, and prints the summary. The run directory, `snapshots/` and the record files are created lazily by the CLI on the FIRST observer call (`j = 0`), which the library makes only after the whole FR-020 validation succeeded; so any validation failure writes nothing (EC-007, EC-015). Records are written through `io` from the callbacks.
  - Parameters come from the evolution CLI: `N`, schedule family, `T`, `steps`,
    `M` and the output directory (`--out`). Nothing is read from the environment.
  - Steps: `steps` is a positive integer, `dt = T / steps`, `t_j = T (j / steps)`
    for `j = 0..steps`, with `t_steps := T` by construction (no floating-point division at `j = steps`) and every evaluated time clamped to `min(t, T)`; midpoint
    `tm_j = T ((2j + 1) / (2 steps))` for `j = 0..steps-1` (the fraction lies in `[0, 1]`, so no intermediate overflows for any finite `T`); `a_j = a(tm_j)`,
    `b_j = b(tm_j)`. Every evaluated time lies in `[0, T]`, as 003 requires.
  - Strang step: `U_j = Dz(dt/2, a_j) Rx(dt, b_j) Dz(dt/2, a_j)` with
    `Dz(s, a) = exp(-i s a H_target)` (pointwise phase `exp(-i s a H_target(k))`)
    and `Rx(dt, b) = exp(-i dt b H_driver) = prod_c (cos(theta) I - i sin(theta) sigma^x_c)`,
    `theta = dt b` (exact, the `sigma^x_c` commute).
  - `P_gs(phi) = sum_{k in ground set} |phi_k|^2`: a quantity of the data
    analysis and of the tests, computed from the written probabilities and the
    rules of 002 and `H_target`; the C code neither computes nor stores it.
  - `<H(t)> = Re <phi| H(t) |phi>`.
  - Norm: `norm = sqrt(sum_k |phi_k|^2)`, summed by pairwise summation over a
    fixed tree that depends only on `dim` (deterministic, FR-008).
  - `NORM_TOL = 1e-12` (ARC-4 default), absolute on `|norm - 1|`. Its
    verification on 2x2 and 3x3 is a DoD item; a relaxation needs a documented
    justification (ARC-4) and a revision of this spec.
  - `STEPS_MAX = 10^6` is fixed by this spec. Rationale: the norm drift grows
    with `steps` (about 2.4e-13 at 10^6 steps in the prototype) and, without
    renormalization, FR-006 may stop a run below `STEPS_MAX` with
    `QA_ERR_DOMAIN` if the drift exceeds `NORM_TOL`; that is expected behavior,
    at the user's risk, not a defect. If the DoD measurement with the C
    integrator shows the drift already exceeding `NORM_TOL` below `STEPS_MAX`,
    the value is changed only through a revision of this spec.
    Memory does not grow with `steps` because the trace is streamed.
  - Snapshot: the vector `p_k = |phi_k|^2`, `k = 0..dim-1`, at step `j`.
  - Reference ground sets (`r = 0.5`, for tests and analysis): 2x2: 4 states,
    `H_target = -0.5`; 3x3: 8 states, `-1`; 4x4: 2 states, `-2`.

## 2. Functional Requirements

- FR-001 (ubiquitous)   The system shall define `H_target(k) = E(k) - r q(k)`, with the constant `r = 0.5`, as a diagonal operator applied matrix-free, recomputing the entries from `k` on demand and never allocating a `dim x dim` matrix (ARC-1).
- FR-002 (event-driven) WHEN evolution is invoked with valid inputs, the system shall perform exactly `steps` Strang steps `U_j = Dz(dt/2, a_j) Rx(dt, b_j) Dz(dt/2, a_j)` for `j = 0..steps-1`, evaluating `a_j`, `b_j` once per step at the midpoint `tm_j` with the 003 schedule of the requested family.
- FR-003 (ubiquitous)   The system shall apply `Rx(dt, b)` as the product over all cells of `cos(theta) I - i sin(theta) sigma^x_c`, matrix-free, with no splitting error inside the driver term, and shall expose it as a public primitive testable with arbitrary `dt`, `b`.
- FR-004 (ubiquitous)   The system shall apply `Dz(s, a)` as the pointwise phase `exp(-i s a H_target(k))` on every amplitude, matrix-free, and shall expose it as a public primitive testable with arbitrary `s`, `a`.
- FR-005 (ubiquitous)   The system shall preserve the state norm: after every step, `|norm - 1| <= NORM_TOL` while the accumulated drift stays within it, without renormalizing the state (a violation is handled by FR-006, never hidden; the guarantee is verified for 2x2 and 3x3, see `STEPS_MAX`); the norm is measured with the pairwise summation of the Definitions.
- FR-006 (unwanted)     IF after any step `|norm - 1| > NORM_TOL` or any amplitude is non-finite, THEN the system shall stop at once, return `QA_ERR_DOMAIN` (the CLI exits 5), leave the caller's output buffer unchanged and not write `config.json`, so that the run directory is not presented as a valid run. The failing step `j` is not delivered to the observer or the snapshot sink: `trace.csv` ends at the last valid `j - 1`, only the snapshots of steps already reached remain, and nothing more is executed or written (EC-020).
- FR-007 (ubiquitous)   The global error of the final state against a finer-`dt` reference shall decrease as `O(dt^2)`. Measured case: 2x2, linear family, `T = 10`, `steps in {100, 200, 400, 800}`, reference `steps = 12800`; error = L2 norm of the difference of final states; the ratio of consecutive errors shall lie in `[3.6, 4.4]` while the error exceeds `1e-10`, and at least two ratios shall be checked, otherwise the test fails. The runs use pure evolution (NULL callbacks).
- FR-008 (ubiquitous)   Evolution shall be deterministic: equal inputs give bit-identical results on the same build and platform, with no RNG and no dependence on global state.
- FR-009 (event-driven) WHEN evolution is invoked, the system shall start from the 004 initial state `|->^numCells`, the ground state of `H(0) = H_driver`, supplied by the caller as a finite state normalized within `NORM_TOL`.
- FR-010 (event-driven) WHEN a run ends successfully, the system shall provide the final state `|phi(T)>`, its probabilities `|c_k|^2 = |phi_k|^2` for all `k`, with `|sum_k |c_k|^2 - 1| <= 2 NORM_TOL + 1e-15` (the squared norm deviates about twice as much as the norm; the extra term absorbs the rounding of the sum), the final `<H(T)>` and the maximum `|norm - 1|` over the run (the latter two feed the CLI summary, FR-015).
- FR-011 (event-driven) WHEN a run ends successfully, the system shall provide, for every `j = 0..steps` (including `t_0 = 0`), the observables `t_j`, `a(t_j)`, `b(t_j)`, the norm and `<H(t_j)>`, delivered to the observer callback during the evolution (never accumulated in memory) and streamed by the CLI to `trace.csv` row by row.
- FR-012 (optional)     WHERE a snapshot interval `M > 0` is requested (`M` is a `size_t`), the system shall write one snapshot file for each `j` in `{0}`, every multiple of `M` up to `steps`, and `steps`; WHERE `M = 0`, only the snapshot of `j = steps`. The snapshot count is `1 + floor(steps / M) + [steps mod M != 0]` for `M > 0` (so `M > steps` gives 2, `j = 0` and `j = steps`; `j = 0` is always the first snapshot when `M > 0`) and 1 for `M = 0`. Snapshots are delivered to the snapshot sink and streamed by the CLI to their file, never accumulated in memory. The number of snapshot files shall not exceed 10,000 per run, and the total snapshot rows (`count * dim`) shall not exceed `2^24`.
- FR-013 (event-driven) DURING a run, the CLI shall stream, from the library callbacks, `trace.csv` (one row per `j = 0..steps`) and the snapshot files `snapshots/snap_<j>.csv` under the run directory, as described in section 4. On any failure partial files may remain; only the absence of `config.json` marks a run as invalid.
- FR-014 (event-driven) WHEN a run completes successfully, the CLI shall write, LAST (its fields are known from the start, but it is written only after the evolution and every record succeeded), `config.json` in the run directory with the configuration record required by TST-4: `N`, schedule family, `T`, `steps`, `dt`, `r`, `M`, seed (`"N/A"`), git sha, clang version and CMake flags. Its presence marks a valid run.
- FR-015 (event-driven) WHEN the evolution CLI is invoked with valid arguments (`N`, family, `T`, `steps`, `M`, `--out <directory>`), it shall run the evolution, write the records of FR-013/FR-014 and print a summary (final `<H>`, maximum `|norm - 1|`, the run directory) to stdout, exiting 0. On an evolution or I/O error it exits with the numeric `QaStatus` value (1..6); usage errors exit 64 (EC-016).
- FR-016 (unwanted)     IF `N = 5`, THEN the system shall return `QA_ERR_UNSUPPORTED`; IF `N` is not in `[2, 5]`, THEN it shall return `QA_ERR_RANGE`; the `N`-gate precedes any size arithmetic.
- FR-017 (unwanted)     IF `steps = 0`, `T` is not finite or `T <= 0`, `dt = T / steps` is zero or not finite (subnormal `T`), or the schedule family is invalid, THEN the system shall return `QA_ERR_DOMAIN` before the first step.
- FR-018 (unwanted)     IF the initial state is NULL, non-finite or not normalized within `NORM_TOL`, THEN the system shall return `QA_ERR_DOMAIN` before the first step, leaving the caller's buffers unchanged.
- FR-019 (unwanted)     IF an allocation fails or a record file cannot be created or written, THEN the system shall return `QA_ERR_NOMEM` or `QA_ERR_IO` respectively (a callback error is propagated unchanged), release every resource it acquired, leave the caller's output buffer unchanged, not write `config.json` and report no success.
- FR-020 (ubiquitous)   The system shall validate, in this order and before any write: (1) the `N`-gate; (2) pointers (NULL gives `QA_ERR_DOMAIN`, except the two callbacks, which may be NULL); (3) `steps`, `T`, family; (4) size arithmetic (`steps + 1` overflow gives `QA_ERR_OVERFLOW`), `steps > STEPS_MAX` (`QA_ERR_RANGE`) and the snapshot count (more than 10,000 gives `QA_ERR_RANGE`); (5) `dim = 2^numCells` and, with it, the snapshot row bound `count * dim > 2^24` (`QA_ERR_RANGE`; no overflow possible since `count <= 10,000` and `dim <= 2^25`) and non-overlap of the output buffer with the initial state (`QA_ERR_DOMAIN`); both buffers hold exactly `dim` amplitudes; (6) the initial-state finiteness and norm. Combined faults resolve by this order.
- FR-021 (ubiquitous)   The system shall keep one documented owner and one release path for every working buffer, and shall leave no leaks in any test or specified run (MEM-1, MEM-2).
- FR-022 (ubiquitous)   The system shall be validated against closed forms: the `Rx` primitive on `|->^n` multiplies the state by the phase `exp(+i numCells theta)`; the `Dz` primitive on `|k>` multiplies it by `exp(-i s a H_target(k))`; `Rx` on `|0...0>` gives amplitude `cos(theta)^numCells` on `|0...0>`.
- FR-023 (ubiquitous)   In the adiabatic limit the system shall agree with an independent reference (a dense-free NumPy prototype of the same Strang scheme, kept with the tests, outside the C code). For linear schedules, `T = 100`, `steps = 10^4`, with `P_gs` computed by the test as in the Definitions: 3x3 gives `P_gs = 0.9868132` and `<H(T)> = -0.9932748`; 2x2 (`M = 100`) gives `P_gs = 0.9781068` and `<H(T)> = -0.4890516`; each is asserted within `1e-6`. The reference shares the midpoint Strang scheme with the C code, so it validates the implementation, not the method; the method is validated by FR-007 and FR-022. The reference script is `reference/proto.py` next to this spec (to move to `tests/reference/` on implementation). The 3x3 case and the representative run of the DoD use `M = 1000` (11 snapshots). The tests hard-code the four constants above; `reference/proto.py` is only used to regenerate them and is NOT run by CTest, so no Python or NumPy dependency exists (STK-3, LIM-1). `P_gs` is expected to be of the order of 0.98-0.99 but NO threshold on it is asserted or blocking: only the agreement with the reference is. 2x2 with `T = 10` and `T = 1000` and 4x4 are recorded, not asserted.
- FR-024 (unwanted)     IF `steps > STEPS_MAX = 10^6` (Definitions), THEN the system shall return `QA_ERR_RANGE` before any allocation or write. (FR-020)
- FR-025 (ubiquitous)   The failure paths of FR-019 and EC-010 shall be testable without real disk-full or memory exhaustion: allocation points go through a documented test-only fault-injection seam, and write failures are injected through a failing callback or a failing `io` write seam; a schedule-evaluation failure (EC-012) is injected through a documented test-only seam around the 003 call. No test depends on `/dev/full` or on platform-specific behavior (LIM-1, MEM-4).
- FR-026 (ubiquitous)   The system shall expose two pure public functions, `time_node(T, steps, j)` = `T (j / steps)` for `j in [0, steps]` (with `time_node(.., steps) = T` exactly) and `time_mid(T, steps, j)` = `T ((2j + 1) / (2 steps))` for `j in [0, steps-1]`, both clamped to `min(t, T)`; the evolution and the trace use exactly these functions. They validate `T`, `steps` as FR-017/FR-020 (`QA_ERR_DOMAIN`, `QA_ERR_RANGE`, `QA_ERR_OVERFLOW`) and `j` out of range gives `QA_ERR_RANGE`; on failure the out-param is unchanged. EC-018 and EC-019 are tested through them.
- FR-027 (ubiquitous)   The public primitives `H_target(k)` (value for board `k`), `Dz` and `Rx` shall validate, in this order and before any write: (1) the `N`-gate (FR-016 codes); (2) NULL pointers (`QA_ERR_DOMAIN`); (3) non-finite `s`, `a`, `dt`, `b` (`QA_ERR_DOMAIN`; a large finite `theta` is accepted, EC-014) and, before any write, a phase argument that is not finite: `s * a * max|H_target|` for `Dz`, `dt * b` for `Rx` (`QA_ERR_DOMAIN`, EC-021) and, for `H_target`, `k >= dim` (`QA_ERR_RANGE`); (4) `dim = 2^numCells` arithmetic. `Dz` and `Rx` act in place on a caller-owned state of exactly `dim` amplitudes, which is unchanged on any error. Each documents this order in its header (ARC-6).

- FR-028 (ubiquitous)   The library shall call the observer for `j = 0..steps` in increasing order, and the snapshot sink for the `j` of FR-012, only after the whole FR-020 validation succeeded and each step's FR-006 check passed; each callback returns a `QaStatus` and any non-`QA_OK` value stops the evolution at once and is propagated unchanged (FR-019). Both callbacks may be NULL. `evolution` does not depend on `io`.

## 3. Edge Cases

- EC-001 WHEN `steps = 1`, the system shall perform one Strang step with `dt = T`, evaluated at `tm_0 = T/2`, and produce records with rows for `j = 0` and `j = 1`. (FR-002, FR-011)
- EC-002 IF `steps + 1` overflows `size_t`, THEN the system shall return `QA_ERR_OVERFLOW` before any allocation. (FR-020)
- EC-003 WHEN `N = 2` (`dim = 16`), the system shall run correctly and every snapshot shall have 16 rows. (FR-012, FR-023)
- EC-004 WHEN `N = 4` (`dim = 65536`), the system shall run and keep the norm bound; this run is non-blocking for the DoD. (FR-005)
- EC-005 WHERE `M > steps`, the system shall write only the snapshots of `j = 0` and `j = steps`. (FR-012)
- EC-006 WHERE `M = 1`, the system shall write a snapshot at every step, provided the snapshot count does not exceed 10,000 (otherwise EC-015). (FR-012)
- EC-007 IF the initial state has a NaN or infinite amplitude, THEN the system shall return `QA_ERR_DOMAIN` without writing any record. (FR-018)
- EC-008 IF the initial state has `|norm - 1|` just above `NORM_TOL`, THEN the system shall reject it; just below, it shall accept it. (FR-018)
- EC-009 IF the run directory is unwritable or a write fails (injected, FR-025), THEN the system shall return `QA_ERR_IO`, the caller's output buffer shall be unchanged and `config.json` shall not exist, so no run is presented as valid. (FR-019, FR-025)
- EC-010 IF an allocation fails at any of the working buffers, THEN the system shall release the ones already acquired; each allocation point has a failure-path test using the seam of FR-025. (FR-019, FR-021, FR-025)
- EC-011 IF the output state buffer (`dim` amplitudes) overlaps the initial state, THEN the system shall return `QA_ERR_DOMAIN` before writing. (FR-020)
- EC-012 IF a schedule evaluation (at `tm_j` or at a trace node `t_j`) unexpectedly returns an error after the FR-017 checks, THEN the system shall propagate that code and stop, with the output buffer unchanged and no `config.json`. (FR-002, FR-011)
- EC-013 WHEN two runs use identical inputs, `trace.csv` and every snapshot shall be byte-identical, and `config.json` shall be byte-identical on the same build. (FR-008, FR-014)
- EC-014 WHEN `theta = dt b` is large (`dt b > 2 pi`), the system shall still preserve the norm, `Rx` being periodic in `theta` up to a global phase; no integration accuracy is claimed there, FR-007 holding only in its measured regime. (FR-003, FR-005)
- EC-015 IF the snapshot count would exceed 10,000 or the total snapshot rows `count * dim` would exceed `2^24` (4x4: at most 256 snapshots), THEN the system shall return `QA_ERR_RANGE` before writing anything. (FR-012, FR-020)
- EC-016 IF a CLI argument is missing, does not parse completely as an integer (`N`, `steps`, `M`) or a real (`T`), is negative where a count is expected, has trailing characters, or the family name is unknown, THEN the CLI shall print usage, write nothing and exit with status 64, distinct from the evolution errors (1..6). An argument that parses but is semantically invalid (`N` out of range, `T` `nan`, `inf` or `<= 0`, `steps = 0`) is passed to the library and exits with its `QaStatus` (1..6), per FR-016/FR-017/FR-020. (FR-015)
- EC-017 IF the `--out` directory already exists and is not empty, THEN the CLI shall fail with `QA_ERR_IO` and not overwrite anything; IF it exists and is not a directory (regular file, or a symlink that does not resolve to a directory), THEN it shall fail with `QA_ERR_IO`; IF it exists, is a directory and is empty, it is accepted; IF its parent does not exist, it shall fail with `QA_ERR_IO`. The check and the lazy creation are not atomic; a concurrent creator is outside the contract. (FR-015, FR-019)
- EC-018 WHEN `T` is not exactly representable as a multiple of `dt` (for example `T = 0.1`, `steps = 3`), every time returned by `time_node` and `time_mid` shall remain within `[0, T]` and `time_node(.., steps) = T` exactly. (FR-002, FR-011, FR-026)
- EC-019 WHEN `T = 1e300` or `T` is the smallest normal double, with `steps = 10^6`, `time_node` and `time_mid` (FR-026) shall return finite values, `t_j` non-decreasing and `t_j <= tm_j <= t_{j+1}` for every `j`; this is tested on the functions alone, without running the evolution. For a subnormal `T` with `dt = 0` the system shall return `QA_ERR_DOMAIN` (FR-017). (FR-002, FR-017, FR-026)
- EC-020 WHEN the norm check of FR-006 fails at step `j` of a CLI run, the CLI shall exit 5; `trace.csv` shall hold rows `0..j-1` only, the snapshots with step `< j` shall exist and no later snapshot, and `config.json` shall not exist. (FR-006, FR-013)
- EC-021 IF `s`, `a`, `dt`, `b` are finite but `s * a * max|H_target|` or `dt * b` overflows to infinity (for example `s = DBL_MAX`), THEN `Dz`/`Rx` shall return `QA_ERR_DOMAIN` before writing and the state shall be unchanged. (FR-027)

## 4. Output records

All records live in one run directory given by `--out`. Numbers are written
with `%.17g`; CSV separator `,`, line end `\n`, header row first, quotes only
where a field contains a comma. `format_version` (in `config.json`) rises when
any column or key below changes.

```
<run>/
  config.json
  trace.csv
  snapshots/snap_<j>.csv        (j zero-padded to the digit count of steps)
```

### config.json (written last; its presence marks a valid run)
One JSON object. Strings are escaped (quotes, backslashes).

| Key | Type | Meaning |
|---|---|---|
| `format_version` | integer | Record format version (now 1) |
| `spec` | string | `"005-evolution"` |
| `N` | integer | Board side (2..4) |
| `family` | string | 003 schedule family |
| `T` | number | Total time |
| `steps` | integer | Number of Strang steps |
| `dt` | number | `T / steps` |
| `r` | number | The constant 0.5 |
| `M` | integer | Snapshot interval (0 = final only) |
| `seed` | string | `"N/A"` (no randomness) |
| `git_sha` | string | Code version |
| `clang_version` | string | Compiler |
| `cmake_flags` | string | Build flags |

### trace.csv (`steps + 1` rows, `j = 0..steps`)

| Column | Meaning |
|---|---|
| `j` | Step index |
| `t` | `t_j = T (j / steps)` |
| `a` | `a(t_j)`, weight of `H_target` |
| `b` | `b(t_j)`, weight of `H_driver` |
| `norm` | `sqrt(sum_k p_k)`, within `NORM_TOL` of 1 |
| `energy` | `<H(t_j)>` |

Invariants: `t_0 = 0`, `t_steps = T` exactly; `a(0) = 0`, `b(0) = 1`, `a(T) = 1`, `b(T) = 0` within `SCHED_TOL` (003); `norm(0) = 1` within `NORM_TOL`; `energy(0) = -numCells` within `numCells * SCHED_TOL + 1e-12` absolute (QA-1).

### snapshots/snap_<j>.csv (`dim` rows, one file per snapshot step `j`)

| Column | Meaning |
|---|---|
| `k` | Board id (002 mapping), `0..dim-1` |
| `p` | `|phi_k|^2` at step `j` |

Invariants: `dim` rows, `sum p = norm^2` within `2 NORM_TOL + 1e-15` of 1 (the `norm` of `trace.csv` is the same quantity, not an independent check); the time of the file is `t_j` in `trace.csv`; the files exist exactly for the `j` of FR-012.

## 5. Scope

### In scope
- Diagonal `H_target(k) = E(k) - r q(k)` with the constant `r = 0.5`.
- Second-order Strang split-operator integrator with exact per-qubit driver rotations and diagonal phases, the `Dz` and `Rx` primitives being public.
- Schedule methods of TFG Eq. (6), with the four families of 003, midpoint evaluation.
- Initial state from 004; accepted boards `N = 2..4`.
- Outputs of section 4: per-step norm and `<H>`, probability snapshots every `M` steps, configuration record.
- Evolution CLI and TFG baseline reproduction (3x3).

### Out of scope
- `P_gs` and ground-set computation in C: done by data analysis from the records.
- Gamma(t) method (TFG Eq. 5) and Crank-Nicolson: the baseline method is not reproduced (SCP-2).
- Biased Hamiltonian (`|12>` at `-3 delta`): fidelity with a chosen state is not needed.
- Variable `r`: a future spec may reopen it with its own range.
- Phases of the final state in the records (probabilities only; no resumable state).
- Spectrum of `H(t)` and eigensolvers (TFG Fig. 5): need diagonalization; future spec.
- OpenMP, `N = 5` execution and Metal/GPU: later specs and optional (STK-2, SCP-1).
- Higher-order integrators, adaptive `dt`, sampling/measurement with random numbers: future specs.
- Plotting and figure generation: outside the C code.

## 6. Definition of Done
- [ ] FR-001..FR-028 covered by tests; the absence of a dense `dim x dim` matrix (FR-001, ARC-1) is verified by code review, not by a test
- [ ] EC-001..EC-021 covered by tests
- [ ] Debug build, full `ctest` green, non-regression of 001-004 tests
- [ ] ASan+UBSan clean on the 005 tests and on the representative run
- [ ] `leaks --atExit` clean on the representative run (zero leaks, MEM-2)
- [ ] Norm drift measured at `STEPS_MAX` (3x3, linear) and recorded in the active spec
- [ ] Norm bound `|norm - 1| <= 1e-12` verified at every step for 2x2 and 3x3 runs (4x4 attempted and reported, non-blocking)
- [ ] Order-2 convergence measured (FR-007) and closed-form checks pass (FR-022)
- [ ] Agreement with the independent reference verified for 3x3 and 2x2 (FR-023)
- [ ] 4x4 representative run attempted; its parameters and outcome recorded in the active spec (non-blocking)
- [ ] Run directory present with `config.json`, `trace.csv` and `snapshots/`, with the keys, columns and invariants of section 4
- [ ] Representative run command (3x3, linear, `T = 100`, `steps = 10^4`, `M = 1000`) documented in `specs/active-spec.md`
- [ ] Section 4 formats documented in the header of the `io` module (ARC-6)

## 7. Changelog
- 2026-10-09 QA rounds closed (QA-1..QA-7: tolerances within `SCHED_TOL`, overflow pre-check, small-`T` test dropped, snapshot row bound, callback contract FR, `--out` cases, FR on norm scoped). Retired FR/EC removed and FR/EC renumbered consecutively; approved.
