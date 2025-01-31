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

add_executable("Oxygen.Platform.Common.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Platform.Common.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Platform.Common.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Platform.Common.LinkTest"
  PRIVATE
    oxygen::platform-common
)
add_test(
  NAME "Oxygen.Platform.Common Link Test"
  COMMAND
    "Oxygen.Platform.Common.LinkTest"
)

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program("Oxygen.Platform.Common.Input.Tests" SOURCES "Input_test.cpp")
m_gtest_program("Oxygen.Platform.Common.Window.Tests" SOURCES "Window_test.cpp")
