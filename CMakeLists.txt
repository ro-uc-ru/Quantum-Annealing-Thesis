# Quantum Annealing Simulator - top-level build.
# Constitution §1: production code is C17 built with CMake and clang.
# Constitution §14: every generated artifact stays under build/, bin/,
# results/ or *.log, so sanitizer builds reuse a subdirectory of build/.
#
# Traceability: spec 001-states, task T01 (build scaffold).
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

# One warning set for every target (constitution §1, codestyle §9).
function(qa_configure_target target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
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
