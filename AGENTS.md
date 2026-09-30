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
- Before any task, read `docs/constitution.md` and `specs/active-spec.md` in full.
- Do not implement code, advance a specification, or change `specs/`, `docs/constitution.md`, or this file without explicit user permission.
- Spec lifecycle: new specs start as Draft; on explicit user approval record `Status: Approved (date)` in the spec and move `specs/active-spec.md` to it. The active spec is the source of truth for scope, RFs, and completion criteria.
- If a requested change conflicts with either document, stop and explain the conflict.
- Keep the active specification, tests, implementation, and experiment outputs traceable to one another.

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
