# Plan: 005-evolution

- Status: approved
- Parent: `specs/005-evolution/spec.md` (approved, Owner Roger, 2026-10-09).

## Approach

Add one new library, `qa_evolution`, on top of the existing `qa_core`,
`qa_hamiltonian`, `qa_schedules`. `H_target` lives in `qa_hamiltonian` (it is an
operator, ARC-1; the scope comment of its header holds `r = 0.5`); the `Dz` and
`Rx` primitives, the two time functions and the Strang driver `qaEvolve` live
in `qa_evolution`, which never includes `io`. Records are produced by new
streaming writers in `qa_io` and wired to the library only through the two
callbacks, from a CLI whose logic sits in a small library so tests can drive it.

Decisions and reasoning:

- **Own `E(k)` in the new `target.c`, 002 untouched.** The pair-energy helper of
  `problem.c` is `static`. As in 004, `target.c` carries its own static copy and
  a test cross-checks `H_target(k) + r q(k)` against `qaHamiltonianApplyProblem`
  on basis states, so drift is detected. Alternative rejected: exporting the
  helper from 002 (edits approved, tested code outside this spec).
- **Working state in a scratch buffer, copied out on success only.** FR-006,
  FR-019, EC-012 require the caller's output buffer to be unchanged on every
  failure, including a failure at step 900 000. The loop therefore evolves a
  library-owned copy and `memcpy`s it to the output after the last check.
  Alternative rejected: evolving in place and "restoring" (the initial state is
  not retained, and the caller's buffer must never be touched).
- **Three owned buffers, one release path.** All are allocated by `qaEvolve`
  after FR-020 passes and freed at one `cleanup` label: the work state
  (`dim` complex), the diagonal table `H_target(k)` (`dim` doubles, filled once
  from the public `H_target`, exact because values are multiples of 0.5) and the
  probability vector for the sink (`dim` doubles, allocated only when a sink is
  given). The table is a vector, not a matrix (ARC-1), and the public
  `Dz`/`H_target` still recompute entries on demand as FR-001 states; a test
  checks that the loop and the public `Dz` give bit-identical results. Alternative
  rejected: recomputing `E(k)` at every `Dz` inside the loop (about
  `2 * steps * dim * pairs` operations, too slow near `STEPS_MAX`).
- **Pairwise norm over a fixed tree.** One internal helper sums `|phi_k|^2`
  by recursive halving of `[0, dim)` (dim is a power of two) down to a fixed
  leaf block, so the order depends on `dim` only (FR-008). The same helper is
  used for the FR-018 entry check, FR-005/006 per step and `<H>`. Alternative
  rejected: plain sequential sum (drift grows with `dim * steps`, which the
  spec measured against `STEPS_MAX`).
- **`Rx` as per-cell butterfly passes.** For each cell `c` in ascending order, each
  pair `(k, k xor m_c)` is mixed by the exact 2x2 block `cos(theta) I - i sin(theta) sigma^x`
  (cost `O(numCells * dim)`, no splitting error, FR-003). Alternative rejected:
  a Walsh-Hadamard diagonalization (more arithmetic, rounding differs from the
  closed form FR-022 tests).
- **Time grid as two pure functions.** `time_node` returns `T` for `j = steps`
  and otherwise `T * ((double) j / (double) steps)`; `time_mid` uses
  `T * ((double) (2j + 1) / (double) (2 steps))`; both are clamped to `min(t, T)`.
  Monotone rounding of the two correctly rounded fractions, and the exact
  real inequalities `j/s <= (2j+1)/(2s) <= (j+1)/s`, give EC-019's ordering for any
  finite `T`. `2 steps` and `2j + 1` are exact because `steps <= STEPS_MAX` is
  checked first.
- **Callback order per step.** After validation: observer `j = 0` (and sink if
  `0` is a snapshot step), then for each `j = 1..steps`: step, FR-006 check,
  observer, sink. A failing step is never delivered (FR-006, FR-028). When both
  fire at the same `j`, the observer runs first (documented in the header).
- **Fault and perturbation seams through a private ops table**, as in 004
  (`src/io/config-internal.h`): `src/evolution/evolve-internal.h` declares
  `qaEvolveWith(..., ops)` with function pointers for `alloc`, schedule
  evaluation (EC-012) and an optional per-step `perturb` hook (used only by tests
  to force the FR-006 norm failure for EC-020, since no valid input reaches it).
  The public `qaEvolve` passes the libc/003-backed table. Alternatives rejected:
  `/dev/full`, `RLIMIT_FSIZE`, `--wrap` (FR-025, LIM-1).
- **Streaming writers with an ops table in `qa_io`.** A run handle owns the open
  `trace.csv` stream and the paths; lazy creation of the directory, `snapshots/`
  and `trace.csv` happens on the first `appendTrace` (`j = 0`). `--out` rules
  (EC-017) are a separate check function that creates nothing. `config.json` is
  written last through a temporary sibling and `rename`, as in 004. Write
  failures are injected by the ops table (FR-025).
- **CLI logic in a library.** `src/cli/qa-005-evolve.c` is a thin `main`; argument
  parsing (strict integer/real parsing, EC-016 exit 64), summary and
  `QaStatus` to exit-code mapping sit in `qaCliEvolveRun(argc, argv, ops)` in a
  static library linked by `main` and by the CLI tests, so EC-020 is tested through
  the real CLI path with the perturb hook. Build metadata uses the 004
  configure-time definitions.
- **Python reference stays outside CTest** (FR-023): the four constants are
  hard-coded in the test; `reference/proto.py` moves to `tests/reference/` in
  the implementation task that the user approves.

## Components / Phases

- Phase 1 - Headers and wiring.
  New public headers: `include/qa/hamiltonian/target.h` (`H_target(k)` function,
  constant `r`, scope comment citing this spec), `include/qa/evolution/primitives.h`
  (`Dz`, `Rx`), `include/qa/evolution/evolve.h` (`time_node`, `time_mid`,
  `STEPS_MAX`, `NORM_TOL`, snapshot bounds, callback types, `qaEvolve`,
  documented validation order), `include/qa/io/run.h` (run handle, `--out`
  check, trace/snapshot/config.json writers, section 4 formats in the header
  comment). Private: `src/evolution/evolve-internal.h`, `src/io/run-internal.h`.
  CMake: `qa_evolution` static library (links `qa_hamiltonian`, `qa_schedules`,
  `qa_core`, `m`), `qa_io` gains the run writers, `qa_cli_evolve` library and
  `qa-005-evolve` executable, new headers registered in the standalone-compile and
  `check-header-docs` gates under the `005-evolution-` prefix, header tests.
- Phase 2 - `H_target` (`src/hamiltonian/target.c`).
  N-gate, `k >= dim` range, own static `E(k)` and `q(k)`, value `E - r q`. No
  allocation.
- Phase 3 - Primitives and time grid (`src/evolution/primitives.c`, `timegrid.c`).
  `Dz`: N-gate, NULL, finiteness of `s`, `a`, then pass 1 computes `max|H_target|`
  and checks `s * a * max|H|` is finite, then pass 2 writes the phases. `Rx`:
  N-gate, NULL, finiteness of `dt`, `b`, `theta` finite, then per-cell passes. Both
  in place, state unchanged on any error. `time_node`, `time_mid` with FR-017 /
  FR-020 validation and out-param untouched on failure.
- Phase 4 - Integrator (`src/evolution/evolve.c`).
  FR-020 order (N-gate, pointers, `steps`/`T`/`family`/`dt`, size arithmetic,
  `STEPS_MAX`, snapshot count and rows, `dim`, overlap by `uintptr_t`, initial state
  finiteness and norm), allocation, Strang loop with `tm_j`, FR-006 check
  (finite + pairwise norm), observer/sink calls, max `|norm - 1|`, final `<H>`,
  copy-out, single `cleanup`.
- Phase 5 - Records (`src/io/run.c`).
  Strict `%.17g` CSV rows, zero-padded snapshot names to the digit count of
  `steps`, JSON string escaping, `--out` check (EC-017), lazy creation, atomic
  `config.json` written last, all through the ops table.
- Phase 6 - CLI (`src/cli/qa-005-evolve*.c`).
  Argument parsing, `--out` check before the library call, callbacks that stream
  to the run handle, summary, exit codes (0, 1..6, 64).
- Phase 7 - Tests.
  Unit tests per module (`tests/hamiltonian/test-target.c`,
  `tests/evolution/test-primitives.c`, `test-timegrid.c`, `test-evolve.c`,
  `tests/io/test-run.c`, `tests/cli/test-evolve-cli.c`), integration tests
  (`tests/integration/`) for FR-007, FR-023, EC-013, EC-004 and the representative
  run, CTest source gate for the absence of a dense matrix and of `io` includes in
  `src/evolution`.
- Phase 8 - Measurements and documentation (needs explicit user permission,
  edits `specs/active-spec.md`): norm drift at `STEPS_MAX`, 4x4 run outcome,
  representative run command, `proto.py` move.

## Coverage

| FR / EC | Covered by |
|---|---|
| FR-001 | Phase 2 (`target.c`, matrix-free, recomputed on demand); table vector in Phase 4 is O(dim); code review of ARC-1 |
| FR-002 | Phase 4 (loop, `tm_j` via `time_mid`, 003 eval) |
| FR-003 | Phase 3 (`Rx` public, per-cell exact blocks) |
| FR-004 | Phase 3 (`Dz` public) |
| FR-005 | Phase 4 (pairwise norm, no renormalization); Phase 7 tests on 2x2, 3x3 |
| FR-006 | Phase 4 (check before delivery, scratch buffer, no copy-out); Phase 6 (no `config.json`) |
| FR-007 | Phase 7 integration (pure evolution, 100..800 vs 12800) |
| FR-008 | Phase 4 (fixed-tree norm, no RNG or globals); Phase 7 (repeat bit-identity) |
| FR-009 | Phase 4 (initial state from caller, validated by FR-018 step) |
| FR-010 | Phase 4 (copy-out, final `<H>`, max drift) |
| FR-011 | Phase 4 (observer `j = 0..steps`), Phase 5/6 (stream to `trace.csv`) |
| FR-012 | Phase 4 (snapshot steps and count formula), Phase 5/6 (stream to files) |
| FR-013 | Phase 5, Phase 6 |
| FR-014 | Phase 5 (`config.json` last), Phase 6 |
| FR-015 | Phase 6 (CLI, summary, exit codes) |
| FR-016 | Phases 2, 3, 4 (N-gate first in every entry point) |
| FR-017 | Phase 3 (`time_*`), Phase 4 |
| FR-018 | Phase 4 (validation step 6) |
| FR-019 | Phase 4 (single `cleanup`, scratch buffer), Phase 5 (IO errors), Phase 6 |
| FR-020 | Phase 4 (numbered order in header and code) |
| FR-021 | Phase 4 (three buffers, one owner, one release path); Phase 7 (ASan, `leaks`) |
| FR-022 | Phase 7 (closed-form tests of `Rx`, `Dz`) |
| FR-023 | Phase 7 integration (four hard-coded constants, 1e-6); Phase 8 (`proto.py` move) |
| FR-024 | Phase 4 (step 4 of FR-020, before any allocation) |
| FR-025 | Private ops tables (Phases 4, 5), perturb hook for EC-020 |
| FR-026 | Phase 3 (`time_node`, `time_mid`), used by Phase 4 and the trace |
| FR-027 | Phases 2 and 3 (numbered order in each header) |
| FR-028 | Phase 4 (callback order and propagation) |
| EC-001 | Phase 4 + Phase 7 (`steps = 1`) |
| EC-002 | Phase 4 (FR-020 step 4) |
| EC-003 | Phase 7 (2x2, 16 rows per snapshot) |
| EC-004 | Phase 7 (4x4 run, non-blocking) |
| EC-005 | Phase 4 (count formula), Phase 7 |
| EC-006 | Phase 4, Phase 7 (`M = 1` within 10,000) |
| EC-007 | Phase 4 (validation step 6, before any callback), Phase 6 (nothing written) |
| EC-008 | Phase 4, Phase 7 (just above/below `NORM_TOL`) |
| EC-009 | Phase 5 (unwritable dir, injected write failure), Phase 4 (output unchanged) |
| EC-010 | Phase 4 (ops table `alloc`, one test per allocation point) |
| EC-011 | Phase 4 (overlap check, step 5) |
| EC-012 | Phase 4 (ops table schedule eval failure) |
| EC-013 | Phase 7 integration (byte comparison of two runs) |
| EC-014 | Phase 3 (`Rx` with large `theta`), Phase 7 |
| EC-015 | Phase 4 (count and rows bound before writing) |
| EC-016 | Phase 6 (strict parsing, exit 64 vs library codes) |
| EC-017 | Phase 5 (`--out` check, no creation), Phase 6 |
| EC-018 | Phase 3 (`time_*`), Phase 7 |
| EC-019 | Phase 3 (functions alone), Phase 7 |
| EC-020 | Phase 4 (no delivery of the failing step), Phase 6, Phase 7 (perturb hook through the CLI path) |
| EC-021 | Phase 3 (`Dz`/`Rx` overflow pre-check), Phase 7 |

## Constitution check

- STK-1, STK-2, STK-3: C17 with CMake and clang, CPU only, no new dependency (libm, libc).
- STK-4: run directories go to `--out` (under `results/`, already ignored).
- STK-5: new targets use the common strict flags with `-Werror`.
- MEM-1, MEM-4: three buffers with one owner and one `cleanup`; every allocation point has an injected failure test via the ops table.
- MEM-2, MEM-3, MEM-6: ASan/UBSan and `leaks` on the 005 tests and on the representative run before ticking.
- MEM-5: sizes and indices validated before any multiply, shift or allocation (FR-020 order); snapshot names built into bounded buffers.
- ARC-1: diagonal vector and bit-flip passes only; no dense matrix, verified by code review and a source gate.
- ARC-2, ARC-6: doc blocks on every function, scope comments and numbered validation order in the four public headers, section 4 formats in the `io` header.
- ARC-3: the coverage table maps every FR/EC to a phase and a test.
- ARC-4: `NORM_TOL = 1e-12`, per-step check, no renormalization; drift measured at `STEPS_MAX` and recorded.
- TST-1, TST-2, TST-3: unit, integration and failure-path tests, deterministic and parameterized.
- TST-4: `config.json` with seed `"N/A"`, N, family, `dt`/`steps`, git sha, clang version, CMake flags.
- TST-5: spec approved 2026-10-09 before any implementation.
- ERR-1, ERR-2: every allocation, file operation, integer conversion and numerical domain check returns a `QaStatus`.
- SCP-1: 2x2 and 3x3 mandatory, 4x4 attempted non-blocking, 5x5 returns `QA_ERR_UNSUPPORTED`.
- SCP-2: Strang scheme, not the TFG Gamma(t)/Crank-Nicolson method.
- LIM-1: no Linux- or macOS-only mechanism for fault injection.
