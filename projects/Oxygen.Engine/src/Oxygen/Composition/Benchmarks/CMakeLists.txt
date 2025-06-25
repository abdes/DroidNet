# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(benchmark CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

set(program_name "Oxygen.Composition.Benchmarks")
set(sources Composition_benchmark.cpp)

add_executable(${program_name} ${sources})
set_target_properties(
  ${program_name}
  PROPERTIES
    FOLDER
      "Benchmarks"
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${sources})

target_compile_options(${program_name} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})

target_link_libraries(
  ${program_name}
  PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    oxygen::composition
)

# Define the test
add_test(NAME ${program_name} COMMAND ${program_name})

install(
  TARGETS
    ${program_name}
  RUNTIME
    DESTINATION bin
    COMPONENT benchmarks
)
