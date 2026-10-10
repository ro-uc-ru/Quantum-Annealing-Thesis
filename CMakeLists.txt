# Quantum Annealing Simulator - top-level build.
# Constitution §1: production code is C17 built with CMake and clang.
# Constitution §14: every generated artifact stays under build/, bin/,
# results/ or *.log, so sanitizer builds reuse a subdirectory of build/.
#
# Traceability: spec 001-states, task T01 (build scaffold).
# Traceability: spec 002-hamiltonian, task T-002 (hamiltonian wiring).
# Traceability: spec 003-schedules, task T-002 (schedules wiring).
# Traceability: spec 004-driver, task T-003 (driver and io wiring).
# Traceability: spec 005-evolution, task T-005 (headers and wiring).
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

# 005-evolution Phase 1 wiring (task T-005, FR-016, FR-021). Placeholder
# translation units only: each module file is implemented by the task named in
# its own header comment, and replaces its placeholder body there.
#
# `qa_evolution` (Strang driver, primitives and time grid) links the 002 and 003
# libraries and libm. The placeholders are the T-007, T-009 and T-010 files.
add_library(qa_evolution STATIC
    src/evolution/primitives.c
    src/evolution/timegrid.c
    src/evolution/evolve.c)
target_include_directories(qa_evolution PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qa_evolution PUBLIC qa_hamiltonian qa_schedules m)
qa_configure_target(qa_evolution)

# `qa_hamiltonian` gains `H_target(k)` (task T-006, FR-001, FR-016, FR-027) and
# its test; the CTest name carries the tasks.md pattern `005-evolution-target`.
target_sources(qa_hamiltonian PRIVATE src/hamiltonian/target.c)
add_executable(test-005-target tests/hamiltonian/test-target.c)
target_link_libraries(test-005-target PRIVATE qa_hamiltonian)
qa_configure_target(test-005-target)
add_test(NAME 005-evolution-target COMMAND test-005-target)
set_tests_properties(
    005-evolution-target
    PROPERTIES LABELS "005-evolution;phase-2")

# Primitive tests (task T-007 `dz`; task T-008 `rx`). One binary, one
# CTest entry per group; the names carry the tasks.md pattern `005-evolution-<group>`.
add_executable(test-005-primitives tests/evolution/test-primitives.c)
target_link_libraries(test-005-primitives PRIVATE qa_evolution)
qa_configure_target(test-005-primitives)
add_test(NAME 005-evolution-dz COMMAND test-005-primitives dz)
add_test(NAME 005-evolution-rx COMMAND test-005-primitives rx)
set_tests_properties(
    005-evolution-dz
    005-evolution-rx
    PROPERTIES LABELS "005-evolution;phase-3")

# Time grid test (task T-009, FR-017, FR-026); CTest name `005-evolution-timegrid`.
add_executable(test-005-timegrid tests/evolution/test-timegrid.c)
target_link_libraries(test-005-timegrid PRIVATE qa_evolution)
qa_configure_target(test-005-timegrid)
add_test(NAME 005-evolution-timegrid COMMAND test-005-timegrid)
set_tests_properties(
    005-evolution-timegrid
    PROPERTIES LABELS "005-evolution;phase-3")

# `qa_io` gains the run record writers (`src/io/run.c`, T-013). The file joins
# the existing library, so the 004 configuration writer is unchanged.
target_sources(qa_io PRIVATE src/io/run.c)

# Run writer tests (task T-013, FR-013, FR-014, FR-019, FR-025, EC-009, EC-017),
# one CTest entry per group; the names carry the tasks.md pattern
# `005-evolution-io`. `src/io` is private for the ops-table seam.
add_executable(test-005-run tests/io/test-run.c)
target_link_libraries(test-005-run PRIVATE qa_io)
target_include_directories(test-005-run PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/io)
qa_configure_target(test-005-run)
foreach(group outdir layout config order failure)
    add_test(NAME 005-evolution-io-${group} COMMAND test-005-run ${group})
    set_tests_properties(005-evolution-io-${group}
                         PROPERTIES LABELS "005-evolution;phase-5")
endforeach()

# `qa_cli_evolve` holds the CLI logic (parsing, `--out` check, summary, exit
# codes; T-014) so that the CLI tests can call it without `main`. `qa-005-evolve`
# is the thin `main` that links it.
add_library(qa_cli_evolve STATIC src/cli/qa-005-evolve-cli.c)
target_include_directories(qa_cli_evolve PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/cli
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/evolution)
target_link_libraries(qa_cli_evolve PUBLIC qa_evolution qa_io qa_hamiltonian
                      qa_schedules)
# Build metadata of `config.json` (TST-4, FR-014): the same configure-time
# values as qa-004-demo, fixed when CMake configures (a missing git yields
# `unavailable`).
target_compile_definitions(qa_cli_evolve PRIVATE
    QA_GIT_SHA="${QA_GIT_SHA_VALUE}"
    QA_CLANG_VERSION="${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
    QA_CMAKE_FLAGS="${QA_CMAKE_FLAGS_VALUE}")
qa_configure_target(qa_cli_evolve)

add_executable(qa-005-evolve src/cli/qa-005-evolve.c)
target_link_libraries(qa-005-evolve PRIVATE qa_cli_evolve)
qa_configure_target(qa-005-evolve)

# CLI tests (task T-014, FR-006, FR-013..FR-015, EC-007, EC-009, EC-016, EC-017,
# EC-020); one CTest entry per group, names carry `005-evolution-cli`. The
# working directory of the tests is a scratch directory under the build tree.
add_executable(test-005-cli tests/cli/test-evolve-cli.c)
target_link_libraries(test-005-cli PRIVATE qa_cli_evolve)
qa_configure_target(test-005-cli)
foreach(group usage codes layout perturb)
    add_test(NAME 005-evolution-cli-${group}
             COMMAND test-005-cli ${group} ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(005-evolution-cli-${group}
                         PROPERTIES LABELS "005-evolution;phase-6")
endforeach()

# 005-evolution header test (task T-005): guards, double inclusion, the
# constants, the struct members and the exact signatures of the four public
# headers, plus the two private seams. Compile-time checks only, so the binary
# links nothing; `src/io` and `src/evolution` are private include directories
# for the two seams.
add_executable(test-005-evolution-header tests/evolution/test-evolution-header.c)
target_link_libraries(test-005-evolution-header PRIVATE qa_evolution qa_io)
target_include_directories(test-005-evolution-header PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/io
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/evolution)
qa_configure_target(test-005-evolution-header)
add_test(NAME 005-evolution-header COMMAND test-005-evolution-header)
set_tests_properties(
    005-evolution-header
    PROPERTIES LABELS "005-evolution;phase-1")

# 005-evolution public-header gate (task T-005, Phase 1). Same contract as the
# 001..004 gates (standalone compile as the first include, plus the
# constitution §6 documentation check), with the `005-evolution-` prefix. The
# private seams are not public headers and are not listed.
set(QA_PUBLIC_HEADERS_005
    include/qa/hamiltonian/target.h
    include/qa/evolution/primitives.h
    include/qa/evolution/evolve.h
    include/qa/io/run.h)

foreach(header IN LISTS QA_PUBLIC_HEADERS_005)
    string(REGEX REPLACE "[/.]" "-" header_slug "${header}")
    add_test(NAME 005-evolution-${header_slug}-standalone
             COMMAND ${CMAKE_C_COMPILER} -std=c17 -Wall -Wextra -Wpedantic
                     -fsyntax-only
                     -I${CMAKE_CURRENT_SOURCE_DIR}/include
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    add_test(NAME 005-evolution-${header_slug}-docs
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-header-docs.sh
                     ${CMAKE_CURRENT_SOURCE_DIR}/${header})
    set_tests_properties(
        005-evolution-${header_slug}-standalone
        005-evolution-${header_slug}-docs
        PROPERTIES LABELS "005-evolution;phase-1;header-gate")
endforeach()

# Driver validation tests (task T-010, FR-016..FR-018, FR-020, FR-024). One
# binary, one CTest entry per group; the names carry the tasks.md pattern
# `005-evolution-validate`. `src/evolution` is private for the internal seam.
add_executable(test-005-evolve tests/evolution/test-evolve.c)
target_link_libraries(test-005-evolve PRIVATE qa_evolution)
target_include_directories(test-005-evolve PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/evolution)
qa_configure_target(test-005-evolve)
foreach(group n-gate validation-order bounds initial-state)
    add_test(NAME 005-evolution-validate-${group}
             COMMAND test-005-evolve ${group})
    set_tests_properties(005-evolution-validate-${group}
                         PROPERTIES LABELS "005-evolution;phase-4")
endforeach()

# Strang loop tests (task T-011, FR-002, FR-005, FR-006, FR-010..FR-012,
# FR-028); same binary, CTest names carry the pattern `005-evolution-evolve`.
foreach(group steps1 norm snapshots callbacks repeat)
    add_test(NAME 005-evolution-evolve-${group}
             COMMAND test-005-evolve ${group})
    set_tests_properties(005-evolution-evolve-${group}
                         PROPERTIES LABELS "005-evolution;phase-4")
endforeach()

# Failure-path tests through the private ops table (task T-012, FR-006,
# FR-019, FR-025, EC-010, EC-012, EC-020); CTest names carry the pattern
# `005-evolution-failure`.
foreach(group alloc schedule perturb)
    add_test(NAME 005-evolution-failure-${group}
             COMMAND test-005-evolve ${group})
    set_tests_properties(005-evolution-failure-${group}
                         PROPERTIES LABELS "005-evolution;phase-4")
endforeach()

# 005-evolution integration tests (task T-015, FR-007, FR-008, FR-023, EC-004,
# EC-013); one CTest entry per group, names carry `005-evolution-integration`.
# `src/cli` and `src/evolution` are private include directories for the CLI.
add_executable(test-005-integration tests/integration/test-005-integration.c)
target_link_libraries(test-005-integration PRIVATE qa_cli_evolve)
qa_configure_target(test-005-integration)
foreach(group convergence reference n4)
    add_test(NAME 005-evolution-integration-${group}
             COMMAND test-005-integration ${group})
    set_tests_properties(005-evolution-integration-${group}
                         PROPERTIES LABELS "005-evolution;phase-7")
endforeach()
add_test(NAME 005-evolution-integration-repeat
         COMMAND test-005-integration repeat ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(005-evolution-integration-repeat
                     PROPERTIES LABELS "005-evolution;phase-7")

# 005-evolution source gates (task T-016, FR-001, FR-021, FR-028): no `io`
# include, no dense matrix shape and no allocation outside the documented owner
# under `src/evolution`. The three `planted` tests run the same script on
# fixture directories holding one deliberate violation each and require the
# script's own exit code for that class (1 io, 2 dense, 3 alloc), proving the
# gate can fail for the right reason. The fixtures are never compiled.
set(QA_GATE_005 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-005-evolution-gate.sh)
add_test(NAME 005-evolution-gate
         COMMAND ${QA_GATE_005} ${CMAKE_CURRENT_SOURCE_DIR}/src/evolution)
set_tests_properties(005-evolution-gate PROPERTIES LABELS "005-evolution;phase-7")
set(QA_GATE_005_CODE_io 1)
set(QA_GATE_005_CODE_dense 2)
set(QA_GATE_005_CODE_alloc 3)
foreach(kind io dense alloc)
    add_test(NAME 005-evolution-gate-planted-${kind}
             COMMAND sh -c
             "${QA_GATE_005} ${CMAKE_CURRENT_SOURCE_DIR}/tests/evolution/gate-fixtures/${kind}; test $? -eq ${QA_GATE_005_CODE_${kind}}")
    set_tests_properties(005-evolution-gate-planted-${kind}
                         PROPERTIES LABELS "005-evolution;phase-7")
endforeach()
