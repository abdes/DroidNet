# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Library Link Test ---------------------------------------------------------

add_executable("Oxygen.Profiling.Cpu.LinkTest" "LinkCpu_test.cpp")
set_target_properties(
  "Oxygen.Profiling.Cpu.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Profiling.Cpu.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Profiling.Cpu.LinkTest"
  PRIVATE
    oxygen::profiling-cpu
)
add_test(
  NAME "Oxygen.Profiling.Cpu Link Test"
  COMMAND
    "Oxygen.Profiling.Cpu.LinkTest"
)

add_executable("Oxygen.Profiling.Gpu.LinkTest" "LinkGpu_test.cpp")
set_target_properties(
  "Oxygen.Profiling.Gpu.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Profiling.Gpu.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Profiling.Gpu.LinkTest"
  PRIVATE
    oxygen::profiling-gpu
)
add_test(
  NAME "Oxygen.Profiling.Gpu Link Test"
  COMMAND
    "Oxygen.Profiling.Gpu.LinkTest"
)

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

gtest_program(
  "Oxygen.Profiling.ProfileScope.Tests"
  SOURCES
    "ProfileScope_test.cpp"
  DEPS
    oxygen::profiling-cpu
    oxygen::testing
)

if(TARGET oxygen::tracy)
  gtest_program(
    "Oxygen.Profiling.TracyCache.Tests"
    SOURCES
      "TracyCache_test.cpp"
    DEPS
      oxygen::profiling-cpu
      oxygen::tracy
      oxygen::testing
  )
endif()
