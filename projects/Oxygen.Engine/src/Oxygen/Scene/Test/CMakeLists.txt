# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)

add_subdirectory("Fixtures")

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
m_gtest_program(
  SceneFlags
  SOURCES
    "Mocks/TestFlag.h"
    "SceneFlags_test.cpp"
    "SceneFlagsAtomic_test.cpp"
    "SceneFlag_test.cpp"
)
m_gtest_program(NodeData SOURCES "NodeData_test.cpp")
m_gtest_program(SceneNodeImpl SOURCES "SceneNodeImpl_test.cpp")
m_gtest_program(
  SceneNode
  SOURCES
    "SceneNode_test.cpp"
    "SceneNode_Transform_test.cpp"
)
# m_gtest_program(
#   SceneFixturesExample
#   SOURCES
#     "Fixtures/Exampleusage_test.cpp"
# )
m_gtest_program(
  TestSceneFactoryExample
  SOURCES
    "Fixtures/TestSceneFactory.h"
    "Fixtures/TestSceneFactory.cpp"
    "Fixtures/TestSceneFactory_example.cpp"
)
add_dependencies(
  "${META_MODULE_NAME}.TestSceneFactoryExample.Tests"
  "${META_MODULE_NAME}.TestSceneFactorySchema"
)
target_link_libraries(
  "${META_MODULE_NAME}.TestSceneFactoryExample.Tests"
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)
m_gtest_program(
  Scene
  SOURCES
    "Fixtures/SceneTest.h"
    "Fixtures/SceneTestFixtures.h"
    "Fixtures/SceneCloningTest.h"
    "Fixtures/SceneReparentTest.h"
    "Fixtures/SceneFlagsTest.h"
    "Fixtures/SceneTraversalTest.h"
    "Fixtures/TestSceneFactory.h"
    "Fixtures/TestSceneFactory.cpp"
    "Scene_basic_test.cpp"
    "Scene_cloning_test.cpp"
    "Scene_graph_test.cpp"
    "Scene_node_factory_test.cpp"
    "Scene_reparent_test.cpp"
    "Scene_traversal_basic_test.cpp"
    "Scene_traversal_filter_test.cpp"
    "Scene_traversal_test.cpp"
    "Scene_traversal_test.h"
    "Scene_traversal_visitor_test.cpp"
)
add_dependencies(
  "${META_MODULE_NAME}.Scene.Tests"
  "${META_MODULE_NAME}.TestSceneFactorySchema"
)
target_link_libraries(
  "${META_MODULE_NAME}.Scene.Tests"
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)
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
