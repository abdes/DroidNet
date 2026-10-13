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

add_executable("Oxygen.Host.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Host.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries("Oxygen.Host.LinkTest" PRIVATE oxygen::host)
add_test(NAME "Oxygen.Host Link Test" COMMAND "Oxygen.Host.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  Host
  SOURCES
    "MainViewOptions_test.cpp"
    "RuntimeCli_test.cpp"
    "SceneStage_test.cpp"
)
