# Quantum Annealing Simulator - top-level build.
# Constitution §1: production code is C17 built with CMake and clang.
# Constitution §14: every generated artifact stays under build/, bin/,
# results/ or *.log, so sanitizer builds reuse a subdirectory of build/.
#
# Traceability: spec 001-states, task T01 (build scaffold).
# Traceability: spec 002-hamiltonian, task T-002 (hamiltonian wiring).
# Traceability: spec 003-schedules, task T-002 (schedules wiring).
# Test naming contract, fixed here and consumed by T06-T10:
#   - CTest test name: the `ctest -R` pattern required by tasks.md T06-T10
#     (e.g. `001-states-n-gate`).
#   - Assertion identifier inside the test binary: `TEST-001-states-RF00X`
#     as required by spec §5.
# The recorded configuration of constitution §13 / spec §5 (N, grid vectors,
# git sha, clang version, CMake flags) is produced in Phase 4 (T11).

cmake_minimum_required(VERSION 3.16)

project(QuantumAnnealingThesis LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "Constitution §1 requires clang; CMAKE_C_COMPILER_ID is "
        "'${CMAKE_C_COMPILER_ID}'. Reconfigure with clang.")
endif()

# Optional sanitizer build for the constitution §9 gate and the AGENTS.md
# completion checklist. It must live inside build/ to honor the closed
# ignore list of constitution §14, e.g.
#   cmake -S . -B build/san -DCMAKE_BUILD_TYPE=Debug -DQA_SANITIZE=address,undefined
set(QA_SANITIZE "" CACHE STRING
    "Sanitizers to enable (e.g. address,undefined); empty disables them")
if(NOT QA_SANITIZE STREQUAL "")
    add_compile_options(-fsanitize=${QA_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${QA_SANITIZE})
endif()

# Reported by the scaffold smoke test. Kept defined even when the caller
# configures without -DCMAKE_BUILD_TYPE, so no test sees an empty macro.
if(CMAKE_BUILD_TYPE STREQUAL "")
    set(QA_BUILD_TYPE "unspecified")
else()
    set(QA_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

enable_testing()

# Strict warnings (-Werror) are the norm (constitution STK-5). Configure with
# -DQA_WERROR=OFF for a non-blocking development build.
option(QA_WERROR "Treat compiler warnings as errors" ON)

# One warning set for every target (constitution §1, codestyle §9).
function(qa_configure_target target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(QA_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    target_compile_definitions(${target} PRIVATE QA_BUILD_TYPE="${QA_BUILD_TYPE}")
endfunction()

# Pure module library. `src/core/grid.c` is the T01 placeholder translation
# unit; RF-001..RF-007 behavior arrives with T03-T05.
add_library(qa_core STATIC src/core/grid.c)
target_include_directories(qa_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
qa_configure_target(qa_core)

# 001-states scaffold smoke test (task T01): the harness builds, links
# `qa_core`, runs, and exits 0. Behavior tests arrive in T06-T10.
add_executable(test-001-states tests/core/test-scaffold.c)
target_link_libraries(test-001-states PRIVATE qa_core)
qa_configure_target(test-001-states)
add_test(NAME 001-states-scaffold COMMAND test-001-states)

# 001-states header test (task T02): self-containment, include guard,
# QaGridId width, exact getBit/withBit signatures, QaStatus values.
add_executable(test-001-states-header tests/core/test-grid-header.c)
target_link_libraries(test-001-states-header PRIVATE qa_core)
qa_configure_target(test-001-states-header)
add_test(NAME 001-states-header COMMAND test-001-states-header)

# 001-states behavior tests, Phase 3 (tasks T06-T10, one group per task).
# One binary, one CTest entry per group: each tasks.md `ctest -R
# 001-states-<group>` pattern matches exactly one entry, and the binary
# prints the spec §5 `TEST-001-states-RF00X` identifiers on failure.
add_executable(test-001-states-grid tests/core/test-grid.c)
target_link_libraries(test-001-states-grid PRIVATE qa_core)
qa_configure_target(test-001-states-grid)
add_test(NAME 001-states-n-gate COMMAND test-001-states-grid n-gate)
add_test(NAME 001-states-id-range COMMAND test-001-states-grid id-range)
add_test(NAME 001-states-cell-access COMMAND test-001-states-grid cell-access)
add_test(NAME 001-states-vectors COMMAND test-001-states-grid vectors)
add_test(NAME 001-states-immutability COMMAND test-001-states-grid immutability)
set_tests_properties(
    001-states-n-gate
    001-states-id-range
    001-states-cell-access
    001-states-vectors
    001-states-immutability
    PROPERTIES LABELS "001-states;phase-3")

# 001-states recorded configuration, Phase 4 (task T11): constitution §13
# and spec §5 require the test configuration (N, grid vectors, git sha,
# clang version, CMake flags; seed/schedule/dt are N/A per spec §5) in a
# machine-readable record. The script writes it into the build tree
# (constitution §14) and validates its contract; the versioned source of
# truth is that script plus this file.
add_test(NAME 001-states-config
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/record-001-config.sh
                 ${CMAKE_C_COMPILER} "${QA_BUILD_TYPE}" "${QA_SANITIZE}"
                 "${CMAKE_C_FLAGS}" ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_BINARY_DIR}/001-states-config.json)
set_tests_properties(
    001-states-config
    PROPERTIES LABELS "001-states;phase-4")

# 002-hamiltonian Phase 1 wiring (task T-002, FR-005). Static hamiltonian
# module linking `qa_core`; `src/hamiltonian/problem.c` is the T-002
# placeholder translation unit (proves the include chain, claims no
# behavior). FR-001/FR-002/FR-003/FR-004/FR-006/FR-007/FR-008 behavior arrives
# with T-003-T-006 (Phase 2).
add_library(qa_hamiltonian STATIC src/hamiltonian/problem.c)
target_include_directories(qa_hamiltonian PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qa_hamiltonian PUBLIC qa_core)
qa_configure_target(qa_hamiltonian)

# 002-hamiltonian header test (task T-002, FR-005): self-containment, include
# guard, and the exact `qaHamiltonianApplyProblem` signature. No operator
# behavior is exercised here; T-003-T-007 bring the behavior tests.
add_executable(test-002-hamiltonian tests/hamiltonian/test-problem-header.c)
target_link_libraries(test-002-hamiltonian PRIVATE qa_hamiltonian)
qa_configure_target(test-002-hamiltonian)
add_test(NAME 002-hamiltonian-header COMMAND test-002-hamiltonian)
set_tests_properties(
    002-hamiltonian-header
    PROPERTIES LABELS "002-hamiltonian;phase-1")

# qa-002-demo placeholder (task T-002): builds, links `qa_hamiltonian`, runs,
# and exits 0. The representative normalized N=4 run (FR-010) arrives with
# T-008 (Phase 4).
add_executable(qa-002-demo src/cli/qa-002-demo.c)
target_link_libraries(qa-002-demo PRIVATE qa_hamiltonian)
qa_configure_target(qa-002-demo)

# 002-hamiltonian behavior tests, Phase 2-3 (tasks T-003-T-007, one group per
# task). One binary, one CTest entry per group: each tasks.md `ctest -R
# 002-hamiltonian-<group>` pattern matches exactly one entry, and the binary
# prints the `TEST-002-hamiltonian-FR0XX` identifiers on failure.
add_executable(test-002-hamiltonian-problem tests/hamiltonian/test-problem.c)
target_link_libraries(test-002-hamiltonian-problem PRIVATE qa_hamiltonian)
qa_configure_target(test-002-hamiltonian-problem)
add_test(NAME 002-hamiltonian-n-gate COMMAND test-002-hamiltonian-problem n-gate)
set_tests_properties(
    002-hamiltonian-n-gate
    PROPERTIES LABELS "002-hamiltonian;phase-2")
add_test(NAME 002-hamiltonian-shape COMMAND test-002-hamiltonian-problem shape)
set_tests_properties(
    002-hamiltonian-shape
    PROPERTIES LABELS "002-hamiltonian;phase-2")
add_test(NAME 002-hamiltonian-vectors COMMAND test-002-hamiltonian-problem vectors)
set_tests_properties(
    002-hamiltonian-vectors
    PROPERTIES LABELS "002-hamiltonian;phase-2")
add_test(NAME 002-hamiltonian-domain COMMAND test-002-hamiltonian-problem domain)
set_tests_properties(
    002-hamiltonian-domain
    PROPERTIES LABELS "002-hamiltonian;phase-2")
add_test(NAME 002-hamiltonian-linearity COMMAND test-002-hamiltonian-problem linearity)
set_tests_properties(
    002-hamiltonian-linearity
    PROPERTIES LABELS "002-hamiltonian;phase-3")

# 002-hamiltonian recorded configuration, Phase 4 (task T-009, FR-011):
# constitution §13 plus the versioned `v1` CSV with spec_version, N, dim,
# §7 vectors, git sha, clang version, and CMake flags (seed/schedule/dt
# are N/A per FR-011). Unlike the 001-states JSON (build tree), the task
# requires the CSV at `results/002-config.csv`, allowed by the closed
# ignore list of constitution §14. The script validates its own contract;
# the versioned source of truth is that script plus this file.
add_test(NAME 002-hamiltonian-config
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/record-002-config.sh
                 ${CMAKE_C_COMPILER} "${QA_BUILD_TYPE}" "${QA_SANITIZE}"
                 "${CMAKE_C_FLAGS}" ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/results/002-config.csv)
set_tests_properties(
    002-hamiltonian-config
    PROPERTIES LABELS "002-hamiltonian;phase-4")

# 001<->002 integration test, Phase 5 (tasks T-011/T-012/T-013, FR-012,
# EC-018/EC-019/EC-020). Links both modules and builds every EC-018 board
# with the 001 `withBit` helper before applying `H_problem`; the fixed
# observable sequence (build id, basis input, apply, oracle + zeros +
# immutability) is asserted inside the binary with
# `TEST-002-hamiltonian-FR012` tags. The main entry additionally takes
# `--record results/002-config.csv` (T-013): the run first requires the
# FR-011 record to authorize its exact `N`/`dim`/vectors (EC-020) and only
# then executes the cases, with `DEPENDS` regenerating the record first in
# full-suite runs.
add_executable(test-002-hamiltonian-integration tests/hamiltonian/test-integration.c)
# `qa_core` arrives transitively via `qa_hamiltonian PUBLIC`; listing it
# again only trips the duplicate-library warning.
target_link_libraries(test-002-hamiltonian-integration PRIVATE qa_hamiltonian)
qa_configure_target(test-002-hamiltonian-integration)
add_test(NAME 002-hamiltonian-integration
         COMMAND test-002-hamiltonian-integration --record
                 ${CMAKE_CURRENT_SOURCE_DIR}/results/002-config.csv)
set_tests_properties(
    002-hamiltonian-integration
    PROPERTIES DEPENDS 002-hamiltonian-config
               LABELS "002-hamiltonian;phase-5")
add_test(NAME 002-hamiltonian-integration-build-failure COMMAND test-002-hamiltonian-integration build-failure)
set_tests_properties(
    002-hamiltonian-integration-build-failure
    PROPERTIES LABELS "002-hamiltonian;phase-5")

# Public-header gate (tasks T02). Every public header must (a) compile as the
# first include of a translation unit, codestyle §3, and (b) satisfy the
# constitution §6 documentation check. The list is explicit and complete on
# purpose: the previous scaffold checked only `grid.h`, so `qa/core/status.h`
# shipped with an undocumented public declaration (the T01/T02 audit defect).
set(QA_PUBLIC_HEADERS
    include/qa/core/status.h
    include/qa/core/grid.h)

foreach(header IN LISTS QA_PUBLIC_HEADERS)
    string(REGEX REPLACE "[/.]" "-" header_slug "${header}")
    add_test(NAME 001-states-${header_slug}-standalone
             COMMAND ${CMAKE_C_COMPILER} -std=c17 -Wall -Wextra -Wpedantic
                     -fsyntax-only
                     -I${CMAKE_CURRENT_SOURCE_DIR}/include
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    add_test(NAME 001-states-${header_slug}-docs
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-header-docs.sh
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    set_tests_properties(
        001-states-${header_slug}-standalone
        001-states-${header_slug}-docs
        PROPERTIES LABELS "001-states;phase-1;header-gate")
endforeach()

set_tests_properties(
    001-states-scaffold
    PROPERTIES LABELS "001-states;phase-0")
set_tests_properties(
    001-states-header
    PROPERTIES LABELS "001-states;phase-1")

# 002-hamiltonian public-header gate (task T-002, Phase 1). Same contract as
# the 001-states gate above (standalone compile + constitution §6 docs
# check), with the `002-hamiltonian-` prefix so the future `ctest -R
# 002-hamiltonian-<group>` patterns of T-003-T-007 match exactly one entry
# each.
set(QA_PUBLIC_HEADERS_002
    include/qa/hamiltonian/problem.h)

foreach(header IN LISTS QA_PUBLIC_HEADERS_002)
    string(REGEX REPLACE "[/.]" "-" header_slug "${header}")
    add_test(NAME 002-hamiltonian-${header_slug}-standalone
             COMMAND ${CMAKE_C_COMPILER} -std=c17 -Wall -Wextra -Wpedantic
                     -fsyntax-only
                     -I${CMAKE_CURRENT_SOURCE_DIR}/include
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    add_test(NAME 002-hamiltonian-${header_slug}-docs
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-header-docs.sh
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    set_tests_properties(
        002-hamiltonian-${header_slug}-standalone
        002-hamiltonian-${header_slug}-docs
        PROPERTIES LABELS "002-hamiltonian;phase-1;header-gate")
endforeach()

# 003-schedules Phase 1 wiring (task T-002, FR-002, FR-013). Static schedules
# module linking `qa_core` and libm; `src/evolution/schedules/schedules.c` is
# the T-002 placeholder translation unit (proves the include chain, claims no
# behavior). FR-001..FR-014 behavior arrives with T-003 and T-004 (Phase 2).
add_library(qa_schedules STATIC src/evolution/schedules/schedules.c)
target_include_directories(qa_schedules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qa_schedules PUBLIC qa_core m)
qa_configure_target(qa_schedules)

# 003-schedules header test (task T-002): self-containment, include guard,
# four-family enum and exact `qaScheduleEval` / `qaScheduleName` signatures.
add_executable(test-003-schedules-header tests/evolution/test-schedules-header.c)
target_link_libraries(test-003-schedules-header PRIVATE qa_schedules)
qa_configure_target(test-003-schedules-header)
add_test(NAME 003-schedules-header COMMAND test-003-schedules-header)
set_tests_properties(
    003-schedules-header
    PROPERTIES LABELS "003-schedules;phase-1")

# 003-schedules behavior tests, Phase 3 (tasks T-005..T-009, one group per
# CTest entry). One binary; each tasks.md `ctest -R 003-schedules-<group>`
# pattern matches exactly one entry. Later groups append here.
add_executable(test-003-schedules tests/evolution/test-schedules.c)
target_link_libraries(test-003-schedules PRIVATE qa_schedules)
qa_configure_target(test-003-schedules)
add_test(NAME 003-schedules-eval COMMAND test-003-schedules eval)
add_test(NAME 003-schedules-boundary COMMAND test-003-schedules boundary)
add_test(NAME 003-schedules-properties COMMAND test-003-schedules properties)
add_test(NAME 003-schedules-limits COMMAND test-003-schedules limits)
add_test(NAME 003-schedules-errors COMMAND test-003-schedules errors)
add_test(NAME 003-schedules-names COMMAND test-003-schedules names)
add_test(NAME 003-schedules-determinism COMMAND test-003-schedules determinism)
set_tests_properties(
    003-schedules-eval
    003-schedules-boundary
    003-schedules-properties
    003-schedules-limits
    003-schedules-errors
    003-schedules-names
    003-schedules-determinism
    PROPERTIES LABELS "003-schedules;phase-3")

# 003-schedules public-header gate (task T-002, Phase 1). Same contract as the
# 001/002 gates (standalone compile + constitution §6 docs check), with the
# `003-schedules-` prefix so `ctest -R 003-schedules-<group>` patterns match
# exactly one entry each.
set(QA_PUBLIC_HEADERS_003
    include/qa/evolution/schedules.h)

foreach(header IN LISTS QA_PUBLIC_HEADERS_003)
    string(REGEX REPLACE "[/.]" "-" header_slug "${header}")
    add_test(NAME 003-schedules-${header_slug}-standalone
             COMMAND ${CMAKE_C_COMPILER} -std=c17 -Wall -Wextra -Wpedantic
                     -fsyntax-only
                     -I${CMAKE_CURRENT_SOURCE_DIR}/include
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    add_test(NAME 003-schedules-${header_slug}-docs
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-header-docs.sh
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    set_tests_properties(
        003-schedules-${header_slug}-standalone
        003-schedules-${header_slug}-docs
        PROPERTIES LABELS "003-schedules;phase-1;header-gate")
endforeach()
