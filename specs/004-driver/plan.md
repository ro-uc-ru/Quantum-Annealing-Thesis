# Plan: 004-driver

- Status: approved
- Parent: `specs/004-driver/spec.md` (approved, Owner Roger, 2026-10-06).

## Approach

Add the driver operator and the initial state to the existing
`qa_hamiltonian` library as a second translation unit, `driver.c`, with its own
public header. Apply uses the same strict validate-then-compute pipeline as
002: `N`-gate, `dim` gate, pointers and `uintptr_t` overlap, a full finiteness
scan, a sequential `1e-12` norm gate, and only then one pass that writes
`outPsi[k] = sum_c phi[k XOR m_c]` in ascending `c`. All checks finish before
the first write, so a failure never leaves a partial output. The initial state
is a single pass after the `N`-gate, `dim` and pointer checks. The CSV writer
lives in a new `src/io` library (`qa_io`, depends on `qa_core` only), and the
demo CLI links `qa_hamiltonian` and `qa_io`.

Decisions and reasoning:

- **Own static helpers in `driver.c`, 002 untouched.** The N-gate, shape, finiteness
  and norm helpers of `problem.c` are `static`. Extracting them into a shared
  internal header would edit approved, tested 002 code outside this spec's
  scope. The cost is duplicated helper logic that must keep the same
  semantics; the 004 tests assert the same precedence and tolerances as the
  002 tests, so drift is detected. Alternative rejected: shared internal
  header (cleaner, but touches 002 and risks its non-regression for no 004
  requirement).
- **Atomic replace for the CSV via a temporary sibling file.** FR-015 and
  EC-023 require that a failure leaves an existing target bit-identical and an
  absent target absent, with no heap. The writer builds `path + ".tmp"` in a
  fixed stack buffer (`1024 + 4 + NUL` bytes, bound proven by the path check
  before any file work), opens it exclusively (`"wx"`, so it never clobbers an
  unrelated file), streams the fields, checks `fflush`/`fclose`, then
  `rename`s over the target (atomic on POSIX, both platforms of LIM-1). Any
  failure closes and removes the temporary file and leaves the target alone.
  Alternative rejected: writing the target in place (a failed write corrupts
  an existing valid file, violating EC-023).
- **Failure injection by an internal operations table, not by OS tricks.**
  EC-022 needs a write or close failure after the file is open. The decision
  (deferred to the plan by the spec) is a private header
  `src/io/config-internal.h` declaring `qaIoWriteConfigWith(path, record,
  ops)`, where `ops` is a small struct of function pointers (`open`, `write`,
  `close`, `rename`, `remove`). The public `qaIoWriteConfig` calls it with the
  libc-backed table. Tests include the private header and pass a table that
  fails at a chosen call. Alternatives rejected: `/dev/full` (Linux only,
  LIM-1 requires both platforms), `RLIMIT_FSIZE` (raises SIGXFSZ, fragile),
  linker `--wrap` (not available with the macOS linker).
- **FR-005 is verified by observable properties plus a source gate.** "No heap
  allocation" is checked by a CTest source gate (a script grepping `driver.c`
  and the writer for `malloc`, `calloc`, `realloc`, `free`, `strdup`), plus
  the ASan and `leaks` runs. "`phi` not modified" is checked by a bit-exact
  copy comparison. "Writes only within `outPsi[0..dim-1]`" is checked with
  guard-band buffers (canary words before and after `outPsi`). "No state across
  calls" is checked by interleaving calls with different inputs and requiring
  bit-identical repeats (also FR-009).
- **Build metadata through configure-time compile definitions.** CMake runs
  `git rev-parse HEAD` and reads the compiler version and flags at configure
  time, defining `QA_GIT_SHA`, `QA_CLANG_VERSION`, `QA_CMAKE_FLAGS` for the
  demo only; a missing git yields the non-empty fallback
  `unavailable`, so EC-020's non-empty rule always holds. Known limit: the sha
  is that of the last configure; the representative run reconfigures first.
  Alternative rejected: a shell script like 002's (the spec fixes the C
  writer in `src/io` called by the demo).
- **`results/` is created at configure time** with `file(MAKE_DIRECTORY ...)`
  in `CMakeLists.txt`, never by the writer or the demo (spec section 1).
  Reading the record back (EC-020) happens in a test, with its own small
  RFC 4180 parser, independent of the writer under test.
- **Norm accumulation stays plain sequential** (spec decision). EC-026 is the
  detector: if it fails, the spec changes to pairwise summation; the
  implementation does not change silently.

## Components / Phases

- Phase 1 - Headers and wiring.
  New `include/qa/hamiltonian/driver.h` (scope comment, numbered validation
  order, `@owner`, `@assumes`, both signatures) and
  `include/qa/io/config.h` (`QaConfigRecord` with ten `const char *` fields,
  `qaIoWriteConfig`, bounds constants `1024`/`4096`). New private
  `src/io/config-internal.h` (ops table and `qaIoWriteConfigWith`; not
  installed, not a public header). CMake: add `driver.c` to `qa_hamiltonian`,
  new `qa_io` static library, `file(MAKE_DIRECTORY results)`, build-metadata
  definitions, the 004 public headers registered in the standalone-compile
  and `check-header-docs.sh` gate under the `004-driver-` prefix, and
  header tests (self-containment, guard, exact signatures).
- Phase 2 - Driver library (`src/hamiltonian/driver.c`).
  `N`-gate (reuses `qa/core` types, `QA_ERR_UNSUPPORTED` for 5, `QA_ERR_RANGE`
  for `N < 2` or `N > 5`), `dim == 2^numCells` check before any pointer
  arithmetic, pointer and `uintptr_t` overlap check, finiteness scan, norm
  gate, then the apply loop with `m_c = 1 << (numCells - 1 - c)` and ascending
  `c`. The initial state loop uses `(-1)^popcount(k) * s`, imaginary part
  `+0.0`, `s = 1.0 / sqrt((double) dim)`. No allocation, no globals, no
  OpenMP, no dense matrix.
- Phase 3 - Driver unit tests (`tests/hamiltonian/test-driver.c`, one binary,
  one CTest entry per group, identifiers `TEST-004-driver-FR0XX` /
  `...-EC0XX`): `n-gate`, `shape` (dim, NULL, overlap, precedence, untouched
  output), `domain` (non-finite, zero, unnormalized, overflowing squares,
  EC-026), `vectors` (EC-008, section 7 vectors, initial state, EC-015/016
  bit-exact at `N = 2`), `properties` (EC-009..012: eigenvalue of `|+>^n`,
  norm bound, Hermiticity, linearity), `contract` (FR-005 guard bands, `phi`
  immutability, determinism and no state across calls, FR-009, EC-014,
  EC-017).
- Phase 4 - CSV writer (`src/io/config.c`).
  Validation first (NULLs, path and every field bound, all before any file is
  created), then the temporary-file write with RFC 4180 quoting, checked
  stream calls, `rename`, and cleanup on every failure. No heap; owner of the
  `FILE *` is the writer's single `cleanup` path.
- Phase 5 - Writer tests (`tests/io/test-config.c`).
  Groups: `contract` (header, row, `v1`, replace-on-success EC-029), `range`
  (NULLs EC-024, exact-bound accept and over-bound reject EC-028), `escape`
  (EC-027 round trip through the independent parser), `io-failure` (missing
  directory EC-021, non-writable directory EC-021, injected write and close
  failures EC-022, existing target unchanged EC-023, temporary file removed).
- Phase 6 - Integration test, demo and record.
  `tests/integration/test-004-integration.c`: for `N` in {2, 3, 4}, builds ids
  with `qaGridWithBit`, checks FR-012 (a), (b), (c) in the fixed sequence, and
  fails the case without calling the Hamiltonian when a helper call fails
  (helper failure injected through an invalid `N`/index case).
  `src/cli/qa-004-demo.c`: one optional path argument, builds the `N = 4`
  initial state, applies `H_driver`, checks `-16 * psi0[k]` within `1e-12`,
  fills `QaConfigRecord` from the build definitions, writes the CSV, exits 0
  only if every step succeeded; heap buffers freed through one `goto cleanup`.
  Tests: a demo run (fixture setup) followed by a read-back checker (EC-020),
  and a demo run against a non-existent directory whose exit status is
  asserted non-zero by a wrapper test (EC-025).
- Phase 7 - Gates.
  004 CTest labels, FR-005 source gate script (`scripts/check-no-heap.sh`),
  ASan+UBSan build over the 004 tests and `qa-004-demo`, `leaks` through
  `scripts/check-leaks.sh build "./build/qa-004-demo"` (LeakSanitizer on
  Linux), `arc6-doc-auditor` over the new sources, tests and headers, and the
  full suite run for 001..003 non-regression.

## Coverage

| FR / EC | Covered by |
|---------|------------|
| FR-001 | Phase 2 (apply definition); Phase 3 vectors; Phase 6 (b) |
| FR-002 | Phase 2 (N-gate); Phase 3 n-gate |
| FR-003 | Phase 2 (N-gate); Phase 3 n-gate |
| FR-004 | Phase 2 (apply loop, ascending `c`); Phase 3 vectors |
| FR-005 | Phase 1 (signatures, header test); Phase 3 contract; Phase 7 (no-heap gate, sanitizers) |
| FR-006 | Phase 2 (dim, pointers, overlap); Phase 3 shape |
| FR-007 | Phase 2 (finiteness scan); Phase 3 domain |
| FR-008 | Phase 2 (norm gate); Phase 3 domain |
| FR-009 | Phase 2 (pure functions); Phase 3 contract |
| FR-010 | Phase 2 (initial state); Phase 3 vectors |
| FR-011 | Phase 3 vectors; Phase 6 (c); Phase 6 demo check |
| FR-012 | Phase 6 integration test |
| FR-013 | Phase 6 demo |
| FR-014 | Phase 4 (writer); Phase 5 contract; Phase 6 read-back |
| FR-015 | Phase 4 (temp file, cleanup); Phase 5 range and io-failure |
| FR-016 | Phase 2 (operator); Phase 3 properties |
| EC-001 | Phase 3 n-gate (FR-003) |
| EC-002 | Phase 3 n-gate (FR-002) |
| EC-003 | Phase 3 shape (FR-006) |
| EC-004 | Phase 3 shape (FR-006) |
| EC-005 | Phase 3 shape (FR-006) |
| EC-006 | Phase 3 domain (FR-007) |
| EC-007 | Phase 3 domain (FR-008) |
| EC-008 | Phase 3 vectors (FR-004) |
| EC-009 | Phase 3 properties (FR-016) |
| EC-010 | Phase 3 properties (FR-016) |
| EC-011 | Phase 3 properties (FR-016) |
| EC-012 | Phase 3 properties (FR-016, FR-009) |
| EC-014 | Phase 3 contract/shape/domain (FR-002, FR-003, FR-006..FR-008) |
| EC-015 | Phase 3 vectors (FR-010) |
| EC-016 | Phase 3 vectors, bit-exact at `N = 2` (FR-010, FR-011) |
| EC-017 | Phase 3 contract (FR-002, FR-003, FR-006, FR-010) |
| EC-020 | Phase 6 demo + read-back test (FR-012, FR-014) |
| EC-021 | Phase 5 io-failure (FR-015) |
| EC-022 | Phase 5 io-failure with the operations table (FR-015) |
| EC-023 | Phase 5 io-failure (FR-015) |
| EC-024 | Phase 5 range (FR-015) |
| EC-025 | Phase 6 demo exit-status wrapper (FR-013, FR-015) |
| EC-026 | Phase 3 domain (FR-008) |
| EC-027 | Phase 5 escape (FR-014) |
| EC-028 | Phase 5 range (FR-015) |
| EC-029 | Phase 5 contract (FR-014) |

## Constitution check

- STK-1: C17, CMake, clang; the existing clang gate applies to the new targets.
- STK-2: CPU only; no accelerator.
- STK-3: libc and libm only; no new dependency, OpenMP not used.
- STK-4: `results/` stays ignored by `.gitignore` and is created at configure time, never in a source directory.
- STK-5: every new target goes through `qa_configure_target` (`-Wall -Wextra -Wpedantic -Werror`).
- MEM-1: the driver functions allocate nothing and both buffers are caller-owned; the writer allocates nothing; the demo owns two heap buffers freed at one `cleanup` label.
- MEM-2: `leaks --atExit` (macOS) or LeakSanitizer (Linux) on every 004 test and on `qa-004-demo`.
- MEM-3: ASan and UBSan over every 004 test and the demo, both platforms.
- MEM-4: every failure path (apply, writer, demo) has a test: temp file removed, `FILE *` closed, buffers freed, target unchanged.
- MEM-5: `dim` is validated before any pointer arithmetic (`dim <= 65536`, so the span fits in 1 MiB); the writer checks path and field lengths before building the temporary name in a fixed buffer; no `strcpy`, `sprintf`.
- ARC-1: matrix-free apply, no dense or sparse matrix anywhere.
- ARC-2 / ARC-6: header, per-function and inline documentation at the density of `grid.c` and `schedules.h`, checked by `check-header-docs.sh` and `arc6-doc-auditor`.
- ARC-3: every FR/EC maps to a phase and a test carrying its identifier (Coverage).
- ARC-4: the norm gate `1e-12` is enforced on every apply input, the initial state is verified normalized, and the output norm bound is tested.
- TST-1: unit tests for the operator, the initial state, normalization and every failure path.
- TST-2: integration test with the 001 helpers and the demo with its read-back.
- TST-3: no RNG or clock; all inputs are deterministic and parameterized by `N`.
- TST-4: the versioned `v1` CSV records `N`, seed, schedule, dt/steps (`N/A`), git sha, clang version and CMake flags.
- TST-5: spec 004 is approved (2026-10-06) before this plan; no code until this plan and its tasks are approved.
- ERR-1 / ERR-2: allocation, I/O (`fopen`, `fputs`, `fflush`, `fclose`, `rename`, `remove`), integer conversions and numerical-domain errors are checked and returned as `QaStatus`.
- SCP-1: serves the staged 2x2..4x4 studies; `N = 5` stays reserved.
- LIM-1: no platform-specific mechanism (injection table instead of `/dev/full`; POSIX `rename` available on both).

## Review decisions (Roger)

1. `driver.c` duplicates the 002 static helpers; no shared header, 002 untouched.
2. The git sha taken at configure time is acceptable (no sha is recorded elsewhere yet).
3. The temporary file `path + ".tmp"` adds 4 bytes to the stack buffer only; the 1024-byte bound applies to `path`.
