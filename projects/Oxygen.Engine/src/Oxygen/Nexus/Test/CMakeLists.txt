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

set(link_test_target "Oxygen.Nexus.LinkTest")
add_executable(${link_test_target} "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries(${link_test_target} PRIVATE oxygen::nexus)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

# Isolate replacement allocation functions from the ordinary test programs.
m_gtest_program(AllocationFailure SOURCES "AllocationFailure_test.cpp")

m_gtest_program(
  Reuse
  SOURCES
    "GenerationTracker_test.cpp"
    "IndexReuse_test.cpp"
    "DomainIndexMapper_test.cpp"
    "FrameDrivenSlotReuse_test.cpp"
    "FrameDrivenIndexReuse_test.cpp"
    "TimelineGatedSlotReuse_test.cpp"
)
