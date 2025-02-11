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

add_executable("Oxygen.Platform.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Platform.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Platform.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Platform.LinkTest"
  PRIVATE
    oxygen::platform
)
add_test(
  NAME "Oxygen.Platform Link Test"
  COMMAND
    "Oxygen.Platform.LinkTest"
)

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program("Oxygen.Platform.Input.Tests" SOURCES "Input_test.cpp")
