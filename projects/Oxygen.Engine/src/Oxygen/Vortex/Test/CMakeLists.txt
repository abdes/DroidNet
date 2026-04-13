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

include(GTestHelpers)

# -- Library Link Test ---------------------------------------------------------

set(link_test_target "Oxygen.Vortex.LinkTest")
add_executable(${link_test_target} "Fakes/Graphics.h" "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${link_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${link_test_target}
  PRIVATE OXYGEN_VORTEX_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/Oxygen/Vortex"
)
target_link_libraries(${link_test_target} PRIVATE oxygen::vortex)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

# Add m_gtest_program entries here as source files are added to the module.
