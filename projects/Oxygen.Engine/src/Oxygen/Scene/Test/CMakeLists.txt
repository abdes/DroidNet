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

add_subdirectory("Helpers")

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
    "SceneNode_camera_test.cpp"
    "SceneNode_graph_test.cpp"
    "SceneNode_mesh_test.cpp"
    "SceneNode_test.cpp"
    "SceneNode_test.h"
    "SceneNode_Transform_test.cpp"
)

m_gtest_program(
  PathParser
  SOURCES
    "PathMatcher/Fixtures/PathParserTest.h"
    "PathMatcher/PathParser_test.cpp"
)

m_gtest_program(
  PathMatcher
  SOURCES
    "PathMatcher/Fixtures/PathMatcherTest.h"
    "PathMatcher/Fixtures/PathParserTest.h"
    "PathMatcher/PathMatcher_basic_test.cpp"
    "PathMatcher/PathMatcher_match_test.cpp"
    "PathMatcher/PathParser_test.cpp"
)

add_subdirectory("Camera")

# -- Complex Scene Tests -------------------------------------------------------

# Macro to add common dependencies and libraries for TestSceneFactory-based tests
macro(add_test_scene_factory_dependencies target_name)
  target_sources(${target_name} PRIVATE "${TEST_FACTORY_SOURCES}")
  add_dependencies(${target_name} "${META_MODULE_NAME}.TestSceneFactorySchema")
  target_link_libraries(
    ${target_name}
    PRIVATE
      nlohmann_json::nlohmann_json
      nlohmann_json_schema_validator
  )
endmacro()

m_gtest_program(
  TestSceneFactoryExample
  SOURCES
    ${TEST_FACTORY_SOURCES}
    "Helpers/TestSceneFactory_example.cpp"
)
add_test_scene_factory_dependencies(${META_MODULE_NAME}.TestSceneFactoryExample.Tests)

m_gtest_program(
  SceneBasic
  SOURCES
    ${TEST_FACTORY_SOURCES}
    "Scene_basic_test.cpp"
    "Scene_cloning_test.cpp"
    "Scene_graph_test.cpp"
    "Scene_nodes_test.cpp"
    "Scene_reparent_test.cpp"
    "Scene_traversal_basic_test.cpp"
)
add_test_scene_factory_dependencies(${META_MODULE_NAME}.SceneBasic.Tests)

m_gtest_program(
  SceneTraversal
  SOURCES
    ${TEST_FACTORY_SOURCES}
    "Scene_traversal_filter_test.cpp"
    "Scene_traversal_test.cpp"
    "Scene_traversal_test.h"
    "Scene_traversal_visitor_test.cpp"
)
add_test_scene_factory_dependencies(${META_MODULE_NAME}.SceneTraversal.Tests)

m_gtest_program(
  SceneAsyncTraversal
  SOURCES
    "AsyncTraversal/SceneTraversalAsync_test.cpp"
)
add_test_scene_factory_dependencies(${META_MODULE_NAME}.SceneAsyncTraversal.Tests)

m_gtest_program(SceneGraphFunctional SOURCES "SceneGraph_functional_test.cpp")
add_test_scene_factory_dependencies(${META_MODULE_NAME}.SceneGraphFunctional.Tests)

m_gtest_program(
  SceneQuery
  SOURCES
    "Query/SceneQuery_adl_test.cpp"
    "Query/SceneQuery_batch_test.cpp"
    "Query/SceneQuery_construction_test.cpp"
    "Query/SceneQuery_error_test.cpp"
    "Query/SceneQuery_immediate_test.cpp"
    "Query/SceneQuery_path_test.cpp"
    "Query/SceneQuery_integration_test.cpp"
    "Query/SceneQueryTestBase.cpp"
    "Query/SceneQueryTestBase.h"
)
add_test_scene_factory_dependencies(${META_MODULE_NAME}.SceneQuery.Tests)

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
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::cs-init>
)

# -- Scene Graph Pretty Printer ------------------------------------------------

add_executable("Oxygen.Scene.PrettyPrinter" "ScenePrettyPrinter_example.cpp")
set_target_properties(
  "Oxygen.Scene.PrettyPrinter"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Scene.PrettyPrinter"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries(
  "Oxygen.Scene.PrettyPrinter"
  PRIVATE
    oxygen::scene
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::cs-init>
)
add_test_scene_factory_dependencies(Oxygen.Scene.PrettyPrinter)
