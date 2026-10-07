# Quantum Annealing Simulator

A specification-driven C17 project for simulating quantum annealing on
N-Queens variants. The project begins from the accompanying undergraduate
thesis, while prioritizing a clearly specified, reproducible implementation.

## Design direction

The reference implementation runs on the CPU on macOS/Apple Silicon, with exact
state studies for 2x2 through 5x5 boards (2x2 and 3x3 required, 4x4 expected,
5x5 stretch). It uses a matrix-free, unitary split-operator time evolution:
dense Hamiltonian allocation is intentionally out of scope. OpenMP is optional
and Metal is a possible future optimization, not a prerequisite.

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
results/    Generated experiment output, e.g. config CSVs (not versioned).
```

The source modules are `core`, `model`, `hamiltonian`, `evolution`, `io`, and
`cli`. `evolution/schedules/` holds the pure a(t), b(t) evaluation; the unitary
split-operator evolution itself arrives in a later spec.

## Specification workflow

Read `docs/constitution.md` and `specs/active-spec.md` before beginning a
task. Each approved feature will live in `specs/NNN-feature-name/` and contain
`spec.md`, `plan.md`, and `tasks.md`. No implementation is added until its
specification is approved.

## Status

Specs `001-states`, `002-hamiltonian`, `003-schedules` and `004-driver` are
approved and fully implemented (all their tasks are ticked). There is no active
spec at the moment.

- `001-states`: `qa_core` static library with the shared `QaStatus` channel
  (`include/qa/core/status.h`) and the classical board grid helpers
  (`include/qa/core/grid.h`, `src/core/grid.c`).
- `002-hamiltonian`: matrix-free application of the diagonal N-Queens problem
  Hamiltonian (`include/qa/hamiltonian/problem.h`), the `qa-002-demo` CLI
  (representative N=4 run) and the versioned `results/002-config.csv`.
- `003-schedules`: pure evaluation of the four annealing schedules (linear,
  trigonometric, degree-2 polynomial, exponential) as `a(t)` and `b(t)`
  (`include/qa/evolution/schedules.h`).

- `004-driver`: matrix-free transverse (driver) Hamiltonian
  `H_driver = sum sigma^x` and the initial state `|->^n`, the ground state of
  `H(0)` (`include/qa/hamiltonian/driver.h`, `src/hamiltonian/driver.c`); the
  `qa_io` library with the CSV configuration writer
  (`include/qa/io/config.h`, `src/io/config.c`); the `qa-004-demo` CLI
  (representative N=4 run) and the versioned `results/004-config.csv`.

The split-operator evolution and `model` are not implemented yet; `io` only
holds the configuration CSV writer.

Build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
ctest --test-dir build --output-on-failure
```

Representative run, sanitizer build and leak check (macOS):

```sh
./build/qa-004-demo
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' && cmake --build build-san
leaks --atExit -- ./build/qa-004-demo
```
