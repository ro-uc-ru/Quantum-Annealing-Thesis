# Quantum Annealing Simulator

## Purpose
Reproducible simulator of quantum annealing on N-Queens variants, with exact-state studies for 2x2 through 5x5 (staged goal: 2x2 and 3x3 MUST be completed, 4x4 expected, 5x5 stretch). Matrix-free, unitary split-operator evolution built with C17, CMake, clang and optional OpenMP; the original Python TFG is a scientific baseline, not code to reproduce blindly.

## Rules
- Read `docs/constitution.md` and `specs/active-spec.md` in full before any work.
- Target task: the one the user names; otherwise the first unticked task in `tasks.md` whose `After:` tasks are all ticked. Grep `- \[ \]` to find it, plus any `Review:` line under it; never read `tasks.md` otherwise.
- Read only what the task cites: Grep `spec.md` for each cited FR/EC ID and ranged-Read those sections; read `plan.md` only for the section matching the task's phase/module. Never read `spec.md` or `plan.md` in full for implementation, and do not list other spec directories or explore the repo tree.
- Do one task, run its "Done when" check, tick it `[x]`, STOP. Ticking is the only allowed edit to `tasks.md`.
- Do not implement code, advance a specification, or change `specs/`, `docs/constitution.md`, or this file without explicit user permission.
- Follow `docs/codestyle.md` for all code style (naming, `f -> a` spacing, `QaStatus` enums, `goto cleanup`).
- Never materialize dense Hamiltonian matrices; apply operators matrix-free and preserve state normalization.
- Every allocation has one documented owner and release path; check all allocation, file and numerical errors and free resources on every failure path.
- Prefer Grep and ranged Read over whole-file reads; pipe build/test/sanitizer/`leaks` output through `tail -n 40` and expand only on failure.
- Final report: exact commands, pass/fail, key numbers, numerical validation, and any check not run with the reason. No logs, no restating the task.

## Stack
- Language:   C17 (`complex double`)
- Build:      CMake, clang
- Parallel:   OpenMP (optional, macOS/Apple Silicon)
- Tests:      CTest
- Backend:    CPU by default; Metal is an optional future optimization, never a requirement

### Modules and dependencies
- `src/core`         <- grid and state-space basics. Depends on: libc, libm
- `src/model`        <- N-Queens problem model. Depends on: core
- `src/hamiltonian`  <- problem Hamiltonian, applied matrix-free. Depends on: core, model
- `src/evolution`    <- unitary split-operator annealing evolution. Depends on: core, hamiltonian, OpenMP (optional)
- `src/io`           <- reproducible machine-readable result output. Depends on: core
- `src/cli`          <- experiment and demo executables, documented in the active spec. Depends on: all above

## Commands
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build   # configure + build
ctest --test-dir build --output-on-failure                            # all tests
ctest --test-dir build -R <name> --output-on-failure                  # one test
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' && cmake --build build-san   # sanitizer build
leaks --atExit -- ./build/<executable> <args>                         # macOS leak check
ASAN_OPTIONS=detect_leaks=1 ./build-san/<executable> <args>           # Linux leak check (LeakSanitizer)
```

### Setup notes
- Representative run: use the executable and arguments documented in `specs/active-spec.md`.
- After each implementation task, ALL of these MUST be verified: debug build, all tests, a representative specified run, a sanitizer run and a `leaks` check.
