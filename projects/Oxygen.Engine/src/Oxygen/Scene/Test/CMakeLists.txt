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

add_executable("Oxygen.Scene.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Scene.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Scene.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries("Oxygen.Scene.LinkTest" PRIVATE oxygen::scene)
add_test(NAME "Oxygen.Scene Link Test" COMMAND "Oxygen.Scene.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(TransformComponent SOURCES "TransformComponent_test.cpp")
m_gtest_program(SceneFlags SOURCES "SceneFlags_test.cpp")
m_gtest_program(SceneNodeImpl SOURCES "SceneNodeImpl_test.cpp")
m_gtest_program(SceneNode SOURCES "SceneNode_test.cpp")
m_gtest_program(Scene SOURCES "Scene_test.cpp")
m_gtest_program(SceneGraphFunctional SOURCES "SceneGraph_functional_test.cpp")

# -- Example Scene Graph -------------------------------------------------------

add_executable("Oxygen.Scene.Example" "SceneGraph_example.cpp")
set_target_properties(
  "Oxygen.Scene.Example"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Scene.Example"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Scene.Example"
  PRIVATE
    oxygen::scene
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::ts-init>
)
