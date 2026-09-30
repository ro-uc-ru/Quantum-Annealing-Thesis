# Project Constitution

These principles are non-negotiable. Any exception requires explicit user approval
before implementation and a documented rationale in the active specification.

1. Production code MUST be written in C17 and built with CMake and clang.
2. The CPU implementation MUST remain the reference backend; accelerators are optional.
3. Hamiltonian operators MUST be matrix-free; dense Hamiltonian allocation is forbidden.
4. Specifications MUST be approved before related implementation begins. Approval is explicit user approval recorded in the active specification (Status plus date).
5. Every behavior MUST be traceable from specification to tests and implementation.
6. Every public and non-obvious internal function MUST document purpose, ownership, errors, and numerical assumptions.
7. Every allocation MUST have one documented owner and one unambiguous release path.
8. All allocations, I/O, integer conversions, and numerical-domain errors MUST be checked.
9. Code MUST be sanitizer-clean; unresolved memory leaks or undefined behavior block completion.
10. Unit tests MUST cover mathematical primitives, indexing, operators, normalization, and failure paths.
11. Integration tests MUST validate specified scientific outcomes and reproducibility.
12. Time evolution MUST preserve required invariants within specified numerical tolerances. Default NORM_TOL is 1e-12 for `complex double`; each specification may tighten or relax it with justification.
13. Tests and experiments MUST be deterministic, parameterized, and record their configuration. Recorded configuration MUST include seed, N, schedule, dt/steps, git sha, clang version, and CMake flags in a versioned machine-readable record.
14. Generated build artifacts, logs, and results MUST stay outside source directories and be ignored by Git. Closed list: `build/`, `bin/`, `results/`, `*.log`, covered by a versioned `.gitignore`.
15. Dependencies MUST be minimal, documented, and justified by a concrete project need. Minimal means: if it is not necessary, it is not included.

## Staged scope

- Exact-state studies target 2x2 through 5x5.
- 2x2 and 3x3 MUST be completed; 4x4 is the expected goal; 5x5 is the final stretch goal.