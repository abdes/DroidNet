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

add_executable("Oxygen.Loader.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Loader.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Loader.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries("Oxygen.Loader.LinkTest" PRIVATE oxygen::loader)
add_test(NAME "Oxygen.Loader Link Test" COMMAND "Oxygen.Loader.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program("GraphicsBackend" SOURCES "GraphicsBackendLoader_test.cpp")

m_gtest_program("PlatformServices" SOURCES "PlatformServices_test.cpp")
