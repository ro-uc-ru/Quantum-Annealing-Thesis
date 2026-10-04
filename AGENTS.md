# Quantum Annealing Simulator - Agent Guide

## Mission
- Build a reproducible C17 simulator for quantum annealing on N-Queens variants.
- Support exact-state studies for 2x2 through 5x5 using a matrix-free, unitary split-operator evolution. Staged goal: 2x2 and 3x3 MUST be completed, 4x4 is the expected goal, 5x5 is the final stretch goal.
- Treat the original Python TFG as a scientific baseline, not as code to reproduce blindly.

## Stack
- C17, CMake, clang, `complex double`, and optional OpenMP on macOS/Apple Silicon.
- Tests use CTest; experiments write documented, reproducible machine-readable results.
- CPU is the default backend; Metal is an optional future optimization, never a requirement.

## Mandatory Reading and Change Control
- Read `docs/constitution.md` and `specs/active-spec.md` in full.
- Task selection: the target task is the one the user names; otherwise the first unticked task in `tasks.md` whose `After:` tasks are all ticked. Read `tasks.md` only to find it (Grep `- \[ \]`) plus any `Review:` line under it.
- Then read only what that task cites: Grep `spec.md` for each cited FR/EC ID and read just those sections (ranged Read). Read `plan.md` only for the section matching the task's phase/module. Never read `spec.md` or `plan.md` in full for implementation.
- Do not list or read other spec directories or explore the repo tree. Open only files the task names or that Grep locates.
- Do one task, run its "Done when" check, tick it, STOP.
- Do not implement code, advance a specification, or change `specs/`, `docs/constitution.md`, or this file without explicit user permission. Ticking a task `[x]` is the only allowed edit to `tasks.md`.

## Token Budget
- Prefer Grep and ranged Read over whole-file reads.
- Pipe build/test/sanitizer/`leaks` output through `tail -n 40`; expand only on failure.
- Final report: exact commands, pass/fail, key numbers. No logs, no restating the task.

## Engineering Style
- Use standard C; camelCase is preferred. Focus on error handling and memory-leak prevention. Follow `docs/codestyle.md` for naming, `f -> a` spacing, documented `QaStatus` enums, and `goto cleanup` ownership handling.
- Document every public function and non-obvious internal function: purpose, inputs, outputs, ownership, errors, and numerical assumptions.
- Make allocation ownership explicit; every allocation has one owner and one documented release path.
- Check all allocation, file, and numerical errors; free resources on every failure path.
- Prefer bounds-safe integer arithmetic, const-correct interfaces, small modules, and deterministic behavior.
- Never materialize dense Hamiltonian matrices; apply operators matrix-free and preserve state normalization.

## Useful Commands
- Configure/build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`.
- Test: `ctest --test-dir build --output-on-failure`; run experiments via the documented executable in the active spec.
- Sanitizers: configure with `-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`.
- macOS leak check: `leaks --atExit -- ./build/<executable> <args>`.

## Completion Checklist
- After each implementation task, run the debug build, all tests, a representative specified run, and a sanitizer and `leaks` check. All checks MUST be verified.
- Report exact commands, results, numerical validation, and any checks not run with the reason.
