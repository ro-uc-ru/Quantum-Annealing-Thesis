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
split-operator evolution lives in `evolution/`.

## Specification workflow

Read `docs/constitution.md` and `specs/active-spec.md` before beginning a
task. Each approved feature will live in `specs/NNN-feature-name/` and contain
`spec.md`, `plan.md`, and `tasks.md`. No implementation is added until its
specification is approved.

## Status

Specs `001-states` through `005-evolution` are approved and implemented. The
full pipeline now runs end to end: initial state `|->^n`, Strang split-operator
evolution under a chosen schedule, and reproducible CSV/JSON output.

- `001-states`: `qa_core` library with the shared `QaStatus` channel and the
  classical board grid helpers (`include/qa/core/`).
- `002-hamiltonian`: matrix-free diagonal N-Queens problem Hamiltonian
  (`include/qa/hamiltonian/problem.h`) and the `qa-002-demo` CLI.
- `003-schedules`: pure `a(t)`, `b(t)` for the four schedules (linear,
  trigonometric, quadratic, exponential; `include/qa/evolution/schedules.h`).
- `004-driver`: matrix-free driver Hamiltonian `sum sigma^x`, initial state
  `|->^n`, the `qa_io` configuration writer and the `qa-004-demo` CLI.
- `005-evolution`: unitary split-operator evolution (`qa_evolution`), run
  writers (`qa_io`) and the `qa-005-evolve` CLI that performs a complete run.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
ctest --test-dir build --output-on-failure
```

For long runs (4x4) a Release build is much faster:

```sh
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel
```

## Running a full simulation

```sh
./build/qa-005-evolve <N> <family> <T> <steps> <M> --out <dir>
```

| Argument   | Meaning                                                              |
|------------|----------------------------------------------------------------------|
| `N`        | Board size, 2 to 4 (5 is not supported yet)                          |
| `family`   | `linear`, `trigonometric`, `quadratic` or `exponential`              |
| `T`        | Total annealing time (real, > 0)                                     |
| `steps`    | Number of Strang steps (`dt = T / steps`; at most 10^6)              |
| `M`        | Snapshot interval in steps; `0` writes only the final snapshot       |
| `--out`    | Output directory: new or empty, its parent must exist               |

Representative run (3x3, linear, `T = 100`, 10^4 steps, a snapshot every 1000):

```sh
./build/qa-005-evolve 3 linear 100 10000 1000 --out results/3x3-linear-T100
```

It prints the final energy `<H>` (about `-0.99327`), the maximum
`|norm - 1|` (about `3e-14`, bound `1e-12`) and the run directory, and exits 0.
Other examples:

```sh
./build/qa-005-evolve 2 linear 100 10000 100 --out results/2x2-linear-T100
./build-rel/qa-005-evolve 4 linear 100 10000 1000 --out results/4x4-linear-T100   # ~81 s in Debug
```

Output (`results/` is not versioned):

```text
<dir>/config.json            N, family, T, steps, dt, r, M, seed, git sha, clang, CMake flags
<dir>/trace.csv              j,t,a,b,norm,energy for every step j = 0..steps
<dir>/snapshots/snap_<j>.csv k,p   probabilities |c_k|^2 at the snapshot steps
```

`config.json` is written last: a directory without it belongs to a failed run.
Exit codes: 0 success, 1 to 6 the numeric `QaStatus` of an evolution or I/O
error, 64 usage error. Measurements for 2x2, 3x3 and 4x4 are recorded in
`specs/active-spec.md`.

## Verification

Sanitizer build and leak check (macOS):

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' && cmake --build build-san
./build-san/qa-005-evolve 3 linear 100 10000 1000 --out /tmp/qa-san-run
leaks --atExit -- ./build/qa-005-evolve 3 linear 100 10000 1000 --out /tmp/qa-leaks-run
```

The earlier demos remain available: `./build/qa-002-demo` and `./build/qa-004-demo`.
