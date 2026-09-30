# Quantum Annealing Simulator

A specification-driven C17 project for simulating quantum annealing on
N-Queens variants. The project begins from the accompanying undergraduate
thesis, while prioritizing a clearly specified, reproducible implementation.

## Design direction

The reference implementation will run on the CPU on macOS/Apple Silicon.
It will use a matrix-free, unitary split-operator time evolution: dense
Hamiltonian allocation is intentionally out of scope. OpenMP and Metal are
possible future optimizations, not prerequisites.

## Layout

```text
docs/       Project constitution and design records.
specs/      Active and numbered feature specifications.
include/qa/ Public headers, organized by module.
src/        Implementation, mirroring the header modules.
tests/      Unit and integration tests, also organized by module.
scripts/    Reproducible development and experiment helpers.
bin/        Generated executables (not versioned).
build/      Generated objects and dependency files (not versioned).
```

The source modules are `core`, `model`, `evolution`, `io`, and `cli`.

## Specification workflow

Read `docs/constitution.md` and `specs/active-spec.md` before beginning a
task. Each approved feature will live in `specs/NNN-feature-name/` and contain
`spec.md`, `plan.md`, and `tasks.md`. No implementation is added until its
specification is approved.

## Status

Spec `001-states` (classical board states) is approved and partially
implemented. Phases 0 and 1 are done: `CMakeLists.txt` builds the `qa_core`
static library with C17 on clang and registers CTest gates for every public
header; `include/qa/core/status.h` and `include/qa/core/grid.h` declare the
shared `QaStatus` channel and the `QaGridId` board type. No grid behavior
exists yet: it arrives with tasks T03-T05, and its unit tests with T06-T10 of
`specs/001-states/tasks.md`.

Build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
ctest --test-dir build --output-on-failure
```
