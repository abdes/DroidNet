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

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(SmokeTest SOURCES "Headless_Smoke_test.cpp")
m_gtest_program(
  All
  SOURCES
    "Headless_Allocator_test.cpp"
    "Headless_Buffer_test.cpp"
    "Headless_Texture_test.cpp"
)
m_gtest_program(
  Commander
  SOURCES
    "Headless_Submission_test.cpp"
)
m_gtest_program(Execution SOURCES "CommandExecution_test.cpp")
