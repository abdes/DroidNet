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

set(link_test_target "Oxygen.Renderer.LinkTest")
add_executable(${link_test_target} "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${link_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(${link_test_target} PRIVATE oxygen::renderer)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(RenderItemsList SOURCES "RenderItemsList_test.cpp")

m_gtest_program(RenderItem SOURCES "RenderItem_test.cpp")

m_gtest_program(Frustum SOURCES "Frustum_test.cpp")

m_gtest_program(RenderListBuilder SOURCES "RenderListBuilder_basic_test.cpp")
