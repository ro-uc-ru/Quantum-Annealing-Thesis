# Quantum Annealing Simulator - top-level build.
# Constitution §1: production code is C17 built with CMake and clang.
# Constitution §14: every generated artifact stays under build/, bin/,
# results/ or *.log, so sanitizer builds reuse a subdirectory of build/.
#
# Traceability: spec 001-states, task T01 (build scaffold).
# Traceability: spec 002-hamiltonian, task T-002 (hamiltonian wiring).
# Traceability: spec 003-schedules, task T-002 (schedules wiring).
# Traceability: spec 004-driver, task T-003 (driver and io wiring).
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

# 004-driver Phase 1 wiring (task T-003, FR-005, FR-014). The driver joins the
# existing `qa_hamiltonian` library (see its sources above); `driver.c` is a
# placeholder translation unit that claims no behavior until Phase 2.
target_sources(qa_hamiltonian PRIVATE src/hamiltonian/driver.c)

# New static `qa_io` library holding the CSV writer (`src/io/config.c`,
# T-010). `src/io` is a PRIVATE include directory so the private seam
# `config-internal.h` is not part of the public interface.
add_library(qa_io STATIC src/io/config.c)
target_include_directories(qa_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(qa_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/io)
target_link_libraries(qa_io PUBLIC qa_core)
qa_configure_target(qa_io)

# `results/` is created at configure time, never by the writer or the demo
# (spec 004-driver §1). It is covered by the versioned `.gitignore`.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/results)

# 004-driver header test (task T-003): guards, self-containment and exact
# signatures of both public headers plus the private writer seam. The private
# `src/io` directory is added for this test only, so it can include
# `config-internal.h`.
add_executable(test-004-driver-header tests/hamiltonian/test-driver-header.c)
target_link_libraries(test-004-driver-header PRIVATE qa_hamiltonian qa_io)
target_include_directories(test-004-driver-header PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/io)
qa_configure_target(test-004-driver-header)
add_test(NAME 004-driver-header COMMAND test-004-driver-header)
set_tests_properties(
    004-driver-header
    PROPERTIES LABELS "004-driver;phase-1")

# 004-driver unit tests, Phase 3 (task T-004 onward, one group per CTest
# entry). One binary; each tasks.md `ctest -R 004-driver-<group>` pattern
# matches exactly one entry. Later groups append here.
add_executable(test-004-driver tests/hamiltonian/test-driver.c)
target_link_libraries(test-004-driver PRIVATE qa_hamiltonian)
qa_configure_target(test-004-driver)
add_test(NAME 004-driver-n-gate COMMAND test-004-driver n-gate)
add_test(NAME 004-driver-shape COMMAND test-004-driver shape)
add_test(NAME 004-driver-initial-state COMMAND test-004-driver initial-state)
add_test(NAME 004-driver-domain COMMAND test-004-driver domain)
add_test(NAME 004-driver-vectors COMMAND test-004-driver vectors)
add_test(NAME 004-driver-properties COMMAND test-004-driver properties)
add_test(NAME 004-driver-contract COMMAND test-004-driver contract)
# FR-005 source gate: no heap call in the driver or the CSV writer (T-009).
add_test(NAME 004-driver-no-heap
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-no-heap.sh
                 ${CMAKE_CURRENT_SOURCE_DIR}/src/hamiltonian/driver.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/src/io/config.c)
# The gate itself must keep failing on a heap call: it runs on a fixture that
# calls `malloc` and passes only when the script reports it (exit 2 for a
# missing file prints another message and so fails this test).
add_test(NAME 004-driver-no-heap-negative
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-no-heap.sh
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/hamiltonian/fixtures/heap-call.txt)
set_tests_properties(004-driver-no-heap-negative PROPERTIES
    PASS_REGULAR_EXPRESSION "check-no-heap: heap call in")
set_tests_properties(
    004-driver-n-gate
    004-driver-shape
    004-driver-initial-state
    004-driver-domain
    004-driver-vectors
    004-driver-properties
    004-driver-contract
    004-driver-no-heap
    004-driver-no-heap-negative
    PROPERTIES LABELS "004-driver;phase-3")

# 004-driver CSV writer tests, Phase 5 (task T-010 onward, one group per CTest
# entry). One binary; each `ctest -R 004-driver-io-<group>` pattern matches
# exactly one entry. The private `src/io` directory is added for this test so
# it can inject fake operations through `config-internal.h`.
add_executable(test-004-driver-io tests/io/test-config.c)
target_link_libraries(test-004-driver-io PRIVATE qa_io)
target_include_directories(test-004-driver-io PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/io)
qa_configure_target(test-004-driver-io)
add_test(NAME 004-driver-io-contract COMMAND test-004-driver-io contract)
add_test(NAME 004-driver-io-range COMMAND test-004-driver-io range)
add_test(NAME 004-driver-io-escape COMMAND test-004-driver-io escape)
add_test(NAME 004-driver-io-failure COMMAND test-004-driver-io io-failure)
set_tests_properties(
    004-driver-io-contract
    004-driver-io-range
    004-driver-io-escape
    004-driver-io-failure
    PROPERTIES LABELS "004-driver;phase-5")

# 001<->004 integration test, Phase 6 (task T-012, FR-012): board ids built
# with the 001 grid helpers against the driver Hamiltonian.
add_executable(test-004-integration tests/integration/test-004-integration.c)
target_link_libraries(test-004-integration PRIVATE qa_hamiltonian)
qa_configure_target(test-004-integration)
add_test(NAME 004-driver-integration COMMAND test-004-integration)
add_test(NAME 004-driver-integration-build-failure
         COMMAND test-004-integration build-failure)
set_tests_properties(
    004-driver-integration
    004-driver-integration-build-failure
    PROPERTIES LABELS "004-driver;phase-6")

# qa-004-demo (task T-013, FR-013, FR-014): the build metadata of the recorded
# configuration is fixed at configure time (TST-4). A missing git yields the
# non-empty fallback `unavailable`; the sha is that of the last configure.
execute_process(COMMAND git rev-parse HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE QA_GIT_SHA_VALUE
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET RESULT_VARIABLE QA_GIT_RESULT)
if(NOT QA_GIT_RESULT EQUAL 0 OR QA_GIT_SHA_VALUE STREQUAL "")
    set(QA_GIT_SHA_VALUE "unavailable")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" QA_BUILD_TYPE_UPPER)
string(STRIP "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${QA_BUILD_TYPE_UPPER}}"
       QA_CMAKE_FLAGS_VALUE)
set(QA_CMAKE_FLAGS_VALUE "build_type=${CMAKE_BUILD_TYPE} ${QA_CMAKE_FLAGS_VALUE}")
add_executable(qa-004-demo src/cli/qa-004-demo.c)
target_link_libraries(qa-004-demo PRIVATE qa_hamiltonian qa_io)
target_compile_definitions(qa-004-demo PRIVATE
    QA_GIT_SHA="${QA_GIT_SHA_VALUE}"
    QA_CLANG_VERSION="${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
    QA_CMAKE_FLAGS="${QA_CMAKE_FLAGS_VALUE}")
qa_configure_target(qa-004-demo)

# MEM-4 fault seams (task T-015): the same source rebuilt with one injected
# fault each; these test-only executables are never installed or run alone.
foreach(QA_FAULT 1 2 3 4 5)
    add_executable(qa-004-demo-fault-${QA_FAULT} src/cli/qa-004-demo.c)
    target_link_libraries(qa-004-demo-fault-${QA_FAULT} PRIVATE qa_hamiltonian qa_io)
    target_compile_definitions(qa-004-demo-fault-${QA_FAULT} PRIVATE
        QA_DEMO_FAULT=${QA_FAULT}
        QA_GIT_SHA="${QA_GIT_SHA_VALUE}"
        QA_CLANG_VERSION="${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
        QA_CMAKE_FLAGS="${QA_CMAKE_FLAGS_VALUE}")
    qa_configure_target(qa-004-demo-fault-${QA_FAULT})
endforeach()

# Demo tests (task T-013): a demo run writes a CSV (fixture setup) and the
# read-back checker validates it (EC-020); a second test requires a non-zero
# exit for a non-existent directory (EC-025).
add_executable(test-004-demo tests/cli/test-004-demo.c)
qa_configure_target(test-004-demo)
add_test(NAME 004-driver-demo-run
         COMMAND qa-004-demo ${CMAKE_CURRENT_BINARY_DIR}/004-config-test.csv)
add_test(NAME 004-driver-config
         COMMAND test-004-demo readback
                 ${CMAKE_CURRENT_BINARY_DIR}/004-config-test.csv)
add_test(NAME 004-driver-demo-failure
         COMMAND test-004-demo exit-status $<TARGET_FILE:qa-004-demo>
                 ${CMAKE_CURRENT_BINARY_DIR}/004-no-such-dir/out.csv)
add_test(NAME 004-driver-demo-usage
         COMMAND test-004-demo usage-status $<TARGET_FILE:qa-004-demo>)
foreach(QA_FAULT 1 2 3 4 5)
    add_test(NAME 004-driver-demo-fault-${QA_FAULT}
             COMMAND test-004-demo exit-status
                     $<TARGET_FILE:qa-004-demo-fault-${QA_FAULT}>
                     ${CMAKE_CURRENT_BINARY_DIR}/004-fault-${QA_FAULT}.csv)
    set_tests_properties(004-driver-demo-fault-${QA_FAULT}
                         PROPERTIES LABELS "004-driver;phase-6")
endforeach()
set_tests_properties(004-driver-demo-run PROPERTIES FIXTURES_SETUP 004-demo-csv)
set_tests_properties(004-driver-config PROPERTIES FIXTURES_REQUIRED 004-demo-csv)
set_tests_properties(
    004-driver-demo-run
    004-driver-config
    004-driver-demo-failure
    004-driver-demo-usage
    PROPERTIES LABELS "004-driver;phase-6")

# 004-driver public-header gate (task T-003, Phase 1). Same contract as the
# 001..003 gates (standalone compile + documentation check), with the
# `004-driver-` prefix so `ctest -R 004-driver-<group>` patterns match exactly
# one entry each. The private `config-internal.h` is not a public header.
set(QA_PUBLIC_HEADERS_004
    include/qa/hamiltonian/driver.h
    include/qa/io/config.h)

foreach(header IN LISTS QA_PUBLIC_HEADERS_004)
    string(REGEX REPLACE "[/.]" "-" header_slug "${header}")
    add_test(NAME 004-driver-${header_slug}-standalone
             COMMAND ${CMAKE_C_COMPILER} -std=c17 -Wall -Wextra -Wpedantic
                     -fsyntax-only
                     -I${CMAKE_CURRENT_SOURCE_DIR}/include
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    add_test(NAME 004-driver-${header_slug}-docs
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-header-docs.sh
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    set_tests_properties(
        004-driver-${header_slug}-standalone
        004-driver-${header_slug}-docs
        PROPERTIES LABELS "004-driver;phase-1;header-gate")
endforeach()
