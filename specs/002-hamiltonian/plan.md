# Plan: 002-hamiltonian

- Status: approved
- Parent: `specs/002-hamiltonian/spec.md` (approved, Owner Roger, 2026-10-03).

## Approach

Meet the spec with a small pure hamiltonian module that recomputes the
energy `E(k)` pointwise from the 001-states bit pattern and applies the
diagonal operator in a strict two-pass validate-then-compute pipeline, so no
failure ever leaves a partial write and no dense matrix ever exists. The
001-states composition is covered by a dedicated integration test that builds
each board with the implemented helpers and checks the fixed FR-012
observable sequence. A minimal CLI covers the representative run and a script
covers the versioned CSV record, mirroring the 001-states Phase 0-4 shape.

Decisions: the new module depends on `qa_core` to reuse grid indexing rather
than duplicating bit logic; buffer overlap is detected by comparing address
ranges instead of checking pointer equality only, which misses partial
overlap; test IDs use the new scheme because the spec migrated 1:1 from `RF`
to `FR`; the integration test builds each id with the bit-write helper
rather than hardcoding ids, because only that proves the 001-002
composition; the integration test reuses the FR-011 recorded configuration
rather than defining its own, because EC-020 requires reproducibility from
that record. Alternative rejected: a precomputed per-`N` energy table — same
order of memory as the state vectors with no measurable gain at
`dim <= 65536`, plus one more cache to keep consistent. Second alternative
rejected: hardcoding ids in the integration test — shorter but proves nothing
about composition with the implemented helpers required by FR-012.

## Components / Phases

- Phase 1 - Header: declares the problem-application contract and the
  internal energy-helper contract, with per-function documentation per
  constitution §6; registered in the CMake public-header gate (standalone
  build plus docs check).
- Phase 2 - Library: new static hamiltonian module linking the core module;
  implements the `N`-gate with checked arithmetic, the fixed validation
  order, the finiteness scan, the `1e-12` norm gate, and the pointwise apply;
  allocates nothing and never terminates the process.
- Phase 3 - Unit tests: grouped by spec area (`N`-gate, shape for
  NULL/overlap/dim, domain for finiteness/norm, vectors, linearity,
  determinism), each asserting its `FR`/`EC` identifiers.
- Phase 4 - CLI and record: demo CLI with one representative normalized `N=4`
  input (exit code only) plus a script writing the `v1` CSV; CMake wires the
  demo binary, the 002 CTest entries including 001-states non-regression, the
  extended header gate, and the sanitizer and `leaks` runs.
- Phase 5 - 001↔002 integration: new integration test binary linking both
  modules; builds each EC-018 board with the write helper, prepares the
  matching basis input, applies the operator, and requires the §7 oracle
  bit-exact, zeros elsewhere, and input immutability; a build failure is
  treated as a case failure with the failing step reported and no apply call;
  the test runs under the FR-011 recorded configuration.

## Coverage

| FR / EC | Covered by |
|---------|------------|
| FR-001 | Phase 2 (energy core); Phase 3 vectors; Phase 5 oracles |
| FR-002 | Phase 2 (N-gate); Phase 3 n-gate |
| FR-003 | Phase 2 (N-gate); Phase 3 n-gate |
| FR-004 | Phase 2 (apply); Phase 3 vectors; Phase 5 basis states |
| FR-005 | Phase 1 (prototype); Phase 2 (contract); Phase 3 shape; Phase 5 immutability |
| FR-006 | Phase 2 (pointers/overlap/dim); Phase 3 shape |
| FR-007 | Phase 2 (finiteness scan); Phase 3 domain |
| FR-008 | Phase 2 (norm gate); Phase 3 domain |
| FR-009 | Phase 2 (purity); Phase 3 determinism; Phase 5 fixed sequence |
| FR-010 | Phase 4 (demo CLI) |
| FR-011 | Phase 4 (config script); Phase 5 runs under that record |
| FR-012 | Phase 5 (001↔002 integration, fixed observable sequence) |
| EC-001 | Phase 3 n-gate (FR-003) |
| EC-002 | Phase 3 n-gate (FR-002) |
| EC-003 | Phase 3 shape (FR-006) |
| EC-004 | Phase 3 shape (FR-006) |
| EC-005 | Phase 3 shape (FR-006) |
| EC-006 | Phase 3 domain (FR-007) |
| EC-007 | Phase 3 vectors (FR-004) |
| EC-008 | Phase 3 vectors (FR-004) |
| EC-009 | Phase 3 domain (FR-008) |
| EC-010 | Phase 3 shape/vectors (FR-005) |
| EC-011 | Phase 3 shape/domain (FR-006, FR-007, FR-008) |
| EC-012 | Phase 3 linearity (FR-004, FR-009) |
| EC-013 | Phase 3 n-gate precedence (FR-002) |
| EC-014 | Phase 3 shape precedence (FR-006) |
| EC-015 | Phase 3 shape (FR-006) |
| EC-016 | Phase 3 domain (FR-007) |
| EC-017 | Phase 3 determinism (FR-009) |
| EC-018 | Phase 5 integration, empty/full/16770 boards (FR-012) |
| EC-019 | Phase 5 integration, build failure without apply call (FR-012) |
| EC-020 | Phase 5 integration under the FR-011 record (FR-012, FR-011) |

## Constitution check

- §1: C17 via the existing CMake toolchain and clang with the same warning set; clang required.
- §2: CPU reference backend only; no accelerator in this plan.
- §3: matrix-free apply, never allocating a dense or sparse matrix; review against dense structures.
- §4: no implementation until human approval of this plan and its tasks.
- §5: every FR/EC maps to a phase and a test carrying its identifier.
- §6: every public and non-obvious internal function documents purpose, ownership, errors, and numerical assumptions.
- §7: the library allocates nothing and both buffers stay caller-owned; the CLI frees on every path.
- §8: checked size and shift arithmetic, range-based overlap detection, finiteness scan, and checked CSV I/O.
- §9: ASan+UBSan gate plus `leaks` on the demo CLI.
- §10: unit tests cover reused indexing, the operator, the norm gate, and failure paths.
- §11: a dedicated 001↔002 integration test validates the specified scientific outcomes and reproducibility.
- §12: `1e-12` norm tolerance for the gate and irrational-scaling comparisons; remainder bit-exact.
- §13: deterministic parameterized CTest plus a versioned CSV with explicit fields and `N/A` values.
- §14: artifacts only under `build/` (tests, CLI) and `results/` (CSV).
- §15: no new dependencies, libc and compiler only; CSV chosen over JSON for this reason.
