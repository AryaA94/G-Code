cmake_minimum_required(VERSION 3.25)
project(gcodesim VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "G-code interpreter, linter and toolpath simulator")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GCODESIM_BUILD_TESTS  "Build unit + golden tests" ON)
option(GCODESIM_BUILD_BENCH  "Build benchmarks" ON)
option(GCODESIM_WERROR       "Treat warnings as errors" ON)
option(GCODESIM_SANITIZE     "Enable AddressSanitizer + UBSan" OFF)
option(GCODESIM_COVERAGE     "Instrument for gcov coverage (GCC/Clang)" OFF)

# ---- Third-party (pulled at configure time, no manual installs) -------------
include(FetchContent)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git GIT_TAG v2.4.2 GIT_SHALLOW TRUE SYSTEM)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(CLI11_BUILD_TESTS OFF CACHE INTERNAL "")
set(CLI11_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(CLI11_INSTALL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json CLI11)

# ---- Shared compile options -------------------------------------------------
add_library(gcodesim_options INTERFACE)
if(MSVC)
  target_compile_options(gcodesim_options INTERFACE /W4 /permissive- /utf-8)
  if(GCODESIM_WERROR)
    target_compile_options(gcodesim_options INTERFACE /WX)
  endif()
else()
  target_compile_options(gcodesim_options INTERFACE -Wall -Wextra -Wpedantic)
  if(GCODESIM_WERROR)
    target_compile_options(gcodesim_options INTERFACE -Werror)
  endif()
endif()
if(GCODESIM_SANITIZE AND NOT MSVC)
  target_compile_options(gcodesim_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  target_link_options(gcodesim_options INTERFACE -fsanitize=address,undefined)
endif()
if(GCODESIM_COVERAGE AND NOT MSVC)
  target_compile_options(gcodesim_options INTERFACE --coverage -O0 -g)
  target_link_options(gcodesim_options INTERFACE --coverage)
endif()

# ---- Library: all logic lives here -------------------------------------------
add_library(gcodesim_core STATIC
  src/diagnostics.cpp
  src/lexer.cpp
  src/parser.cpp
  src/segment.cpp
  src/machine.cpp
  src/interpreter.cpp
  src/planner.cpp
  src/linter.cpp
  src/stats.cpp
  src/svg.cpp
  src/analysis.cpp
  src/report.cpp)
target_include_directories(gcodesim_core PUBLIC include)
target_link_libraries(gcodesim_core PUBLIC nlohmann_json::nlohmann_json
                      PRIVATE gcodesim_options)

# ---- CLI: thin wrapper --------------------------------------------------------
add_executable(gcode-sim cli/main.cpp)
target_compile_definitions(gcode-sim PRIVATE GCODESIM_VERSION="${PROJECT_VERSION}")
target_link_libraries(gcode-sim PRIVATE gcodesim_core CLI11::CLI11 gcodesim_options)

# ---- Tests ------------------------------------------------------------------
if(GCODESIM_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.7.1 GIT_SHALLOW TRUE SYSTEM)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(CTest)
  include(Catch)
  add_executable(gcodesim_tests
    tests/unit/test_lexer.cpp
    tests/unit/test_arc.cpp
    tests/unit/test_interpreter.cpp
    tests/unit/test_cycles.cpp
    tests/unit/test_linter.cpp
    tests/unit/test_planner.cpp
    tests/unit/test_output.cpp
    tests/unit/test_robustness.cpp
    tests/golden/test_golden.cpp)
  target_include_directories(gcodesim_tests PRIVATE tests/unit)
  target_compile_definitions(gcodesim_tests PRIVATE
    GCODESIM_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
  target_link_libraries(gcodesim_tests PRIVATE gcodesim_core Catch2::Catch2WithMain
                        gcodesim_options)
  catch_discover_tests(gcodesim_tests)

  # ---- CLI smoke tests: exit codes are part of the documented interface ----
  set(EX ${CMAKE_CURRENT_SOURCE_DIR}/examples)
  function(gcodesim_cli_test name expect_rc expect_out)
    add_test(NAME cli_${name}
             COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:gcode-sim> "-DARGS=${ARGN}"
                     -DEXPECT_RC=${expect_rc} "-DEXPECT_OUT=${expect_out}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/check_exit.cmake)
  endfunction()
  gcodesim_cli_test(version 0 "${PROJECT_VERSION}" --version)
  gcodesim_cli_test(stats 0 "Cycle time" stats ${EX}/bracket.nc -c ${EX}/machine.json)
  gcodesim_cli_test(stats_json 0 "\"time_s\"" stats ${EX}/bracket.nc -c ${EX}/machine.json --format json)
  gcodesim_cli_test(stats_no_lookahead 0 "Cycle time" stats ${EX}/contour.nc -c ${EX}/machine.json --no-lookahead)
  gcodesim_cli_test(lint_clean 0 "0 error" lint ${EX}/contour.nc -c ${EX}/machine.json)
  gcodesim_cli_test(lint_broken 1 "LN00" lint ${EX}/lint_demo.nc -c ${EX}/machine.json)
  gcodesim_cli_test(lint_json 1 "\"errors\"" lint ${EX}/lint_demo.nc -c ${EX}/machine.json --format json)
  gcodesim_cli_test(rules 0 "LN009" rules)
  gcodesim_cli_test(render 0 "wrote" render ${EX}/bracket.nc --svg ${CMAKE_CURRENT_BINARY_DIR}/cli_test.svg)
  gcodesim_cli_test(render_depth 0 "wrote" render ${EX}/pocket.nc -c ${EX}/machine.json --color depth --svg ${CMAKE_CURRENT_BINARY_DIR}/cli_depth.svg)
  gcodesim_cli_test(missing_file 2 "does not exist" stats /no/such/file.nc)
  gcodesim_cli_test(bad_usage 2 "" --bogus-flag)
  gcodesim_cli_test(no_subcommand 2 "" )
  gcodesim_cli_test(bad_config 2 "machine config" stats ${EX}/bracket.nc -c ${EX}/bracket.nc)
  gcodesim_cli_test(bad_color 2 "" render ${EX}/bracket.nc --svg x.svg --color rainbow)
endif()

# ---- Benchmarks ---------------------------------------------------------------
if(GCODESIM_BUILD_BENCH)
  add_executable(bench_parse bench/bench_parse.cpp)
  target_link_libraries(bench_parse PRIVATE gcodesim_core gcodesim_options)
endif()
