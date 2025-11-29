# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Scene"
  DESCRIPTION
    [=[
The Oxygen Scene module implements a scene-graph (core Scene and SceneNode
types) with transform and renderable components, node factories, reparenting
and flag handling; it provides traversal and query systems (sync/async, batch),
camera projections (orthographic/perspective), light/renderable utilities,
pretty-printing/string converters, and comprehensive tests and docs to support
rendering and editor workflows.
]=]
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_SCN_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

set(
  OXYGEN_SCENE_HEADERS
  "Camera/Orthographic.cpp"
  "Camera/Perspective.cpp"
  "Detail/GraphData.h"
  "Detail/NodeData.h"
  "Detail/PathMatcher.h"
  "Detail/PathParser.cpp"
  "Detail/PathParser.h"
  "Detail/RenderableComponent.cpp"
  "Detail/RenderableComponent.h"
  "Detail/RenderablePolicies.cpp"
  "Detail/Scene_safecall_impl.cpp"
  "Detail/Scene_safecall_impl.h"
  "Detail/StringConverters.cpp"
  "Detail/TransformComponent.cpp"
  "Scene_node_factory.cpp"
  "Scene_reparent.cpp"
  "Scene.cpp"
  "SceneFlags.cpp"
  "SceneNode_Transform.cpp"
  "SceneNode_Renderable.cpp"
  "SceneNode.cpp"
  "SceneNodeImpl.cpp"
  "ScenePrettyPrinter.cpp"
  "SceneQuery.cpp"
  "SceneQueryBatch.cpp"
)
set(
  OXYGEN_SCENE_PRIVATE_SOURCES
  "Camera/Orthographic.cpp"
  "Camera/Perspective.cpp"
  "Detail/GraphData.h"
  "Detail/NodeData.h"
  "Detail/PathMatcher.h"
  "Detail/PathParser.cpp"
  "Detail/PathParser.h"
  "Detail/RenderableComponent.cpp"
  "Detail/RenderableComponent.h"
  "Detail/RenderablePolicies.cpp"
  "Detail/Scene_safecall_impl.cpp"
  "Detail/Scene_safecall_impl.h"
  "Detail/StringConverters.cpp"
  "Detail/TransformComponent.cpp"
  "Scene_node_factory.cpp"
  "Scene_reparent.cpp"
  "Scene.cpp"
  "SceneFlags.cpp"
  "SceneNode_Transform.cpp"
  "SceneNode_Renderable.cpp"
  "SceneNode.cpp"
  "SceneNodeImpl.cpp"
  "ScenePrettyPrinter.cpp"
  "SceneQuery.cpp"
  "SceneQueryBatch.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_SCENE_PRIVATE_SOURCES}
    ${OXYGEN_SCENE_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_SCENE_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_SCN_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    glm::glm
    oxygen::base
    oxygen::composition
    oxygen::core
    oxygen::data
    oxygen::oxco
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Test")
  #add_subdirectory("Benchmarks")
endif()

# ------------------------------------------------------------------------------
# API Documentation
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_DOCS AND OXYGEN_WITH_DOXYGEN)
  asap_with_doxygen(
    MODULE_NAME ${META_MODULE_TARGET}
    VERSION ${META_VERSION}
    TITLE "\"Scene\""
    BRIEF "\"Scene management for Oxygen\""
    INPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
