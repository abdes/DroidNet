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

# -- Input vocabulary (oxygen::platform-input) ---------------------------------

add_executable("Oxygen.Platform.Input.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Platform.Input.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries(
  "Oxygen.Platform.Input.LinkTest"
  PRIVATE
    oxygen::platform-input
)
add_test(
  NAME "Oxygen.Platform.Input.LinkTest"
  COMMAND
    "Oxygen.Platform.Input.LinkTest"
)

include(GTestHelpers)

gtest_program(
  "Oxygen.Platform.Input.Tests"
  SOURCES
    "Input_test.cpp"
  DEPS
    oxygen::platform-input
    oxygen::testing
)

if(NOT OXYGEN_BUILD_FULL_ENGINE)
  return()
endif()

# -- SDL backend (oxygen::platform) --------------------------------------------

m_gtest_program(
  Backend
  SOURCES
    "InputEvents_test.cpp"
    "UiEvents_test.cpp"
    "Window_test.cpp"
)

# The backend tests exercise the SDL implementation directly.
target_link_libraries("${META_MODULE_NAME}.Backend.Tests" PRIVATE SDL3::SDL3)
