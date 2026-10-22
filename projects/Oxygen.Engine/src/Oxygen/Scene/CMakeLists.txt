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

# TODO: do not export internal headers
set(
  OXYGEN_SCENE_HEADERS
  "api_export.h"
  "Scene.h"
  "SceneFlags.h"
  "SceneNode.h"
  "SceneNodeImpl.h"
  "ScenePrettyPrinter.h"
  "ExposureSettings.h"
  "Scripting/ScriptingComponent.h"
  "Schema/Generated.SceneComponents.h"
  "SceneQuery.h"
  "SceneTraversal.h"
  "SceneTraversalAsync.h"
  "SceneTraversalBase.h"
  "Camera/CameraExposure.h"
  "Camera/Orthographic.h"
  "Camera/Perspective.h"
  "Environment/EnvironmentSystem.h"
  "Environment/Background.h"
  "Environment/Fog.h"
  "Environment/LocalFogVolume.h"
  "Environment/PostProcessVolume.h"
  "Environment/SceneEnvironment.h"
  "Environment/SkyAtmosphere.h"
  "Environment/SkyLight.h"
  "Environment/SkySphere.h"
  "Environment/VolumetricClouds.h"
  "Light/LightValidation.h"
  "Light/DirectionalLight.h"
  "Light/DirectionalLightResolver.h"
  "Light/LightCommon.h"
  "Light/PointLight.h"
  "Light/SpotLight.h"
  "Detail/GraphData.h"
  "Detail/NodeData.h"
  "Detail/PathMatcher.h"
  "Detail/PathParser.h"
  "Detail/RenderableComponent.h"
  "Detail/Scene_safecall_impl.h"
  "Detail/TransformComponent.h"
  "Types/ActiveMesh.h"
  "Types/Flags.h"
  "Types/NodeHandle.h"
  "Types/ScriptSlotIndex.h"
  "Types/RenderablePolicies.h"
  "Types/SceneFlagEnum.h"
  "Types/Strong.h"
  "Types/Traversal.h"
)

set(
  OXYGEN_SCENE_PRIVATE_SOURCES
  "Scene.cpp"
  "Scene_node_factory.cpp"
  "Scene_reparent.cpp"
  "SceneFlags.cpp"
  "SceneNode_Renderable.cpp"
  "SceneNode_Transform.cpp"
  "SceneNode.cpp"
  "SceneNodeImpl.cpp"
  "ScenePrettyPrinter.cpp"
  "Scripting/ScriptingComponent.cpp"
  "SceneQuery.cpp"
  "SceneQueryBatch.cpp"
  "Camera/Orthographic.cpp"
  "Camera/Perspective.cpp"
  "Light/LightValidation.cpp"
  "Light/DirectionalLight.cpp"
  "Light/DirectionalLightResolver.cpp"
  "Light/PointLight.cpp"
  "Light/SpotLight.cpp"
  "Detail/PathParser.cpp"
  "Detail/RenderableComponent.cpp"
  "Detail/RenderablePolicies.cpp"
  "Detail/Scene_safecall_impl.cpp"
  "Detail/StringConverters.cpp"
  "Detail/TransformComponent.cpp"
  "Internal/IMutationCollector.h"
  "Internal/MutationDispatcher.cpp"
  "Internal/MutationDispatcher.h"
  "Internal/MutationCollector.cpp"
  "Internal/MutationCollector.h"
  "Internal/MutationTraits.h"
  "Internal/MutationTypes.h"
  "Internal/ScriptSlotMutationProcessor.cpp"
  "Internal/ScriptSlotMutationProcessor.h"
  "Environment/LocalFogVolume.cpp"
  "ExposureSettings.cpp"
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

target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
  PUBLIC
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
# Component and field ids of the scene components schema
# ------------------------------------------------------------------------------

# SceneSchemaCodeGen validates Schema/SceneComponents.yaml and writes
# Schema/Generated.SceneComponents.h into the source tree, where it is
# versioned. A stamp in the build tree is the only declared output, so Clean
# never deletes the versioned header. The generator writes the header only when
# its text changes, so a regeneration that changes nothing recompiles nothing.
set(
  _scene_schema_tool_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/Tools/SceneSchemaCodeGen"
)
set(_scene_schema_dir "${CMAKE_CURRENT_SOURCE_DIR}/Schema")
set(_scene_schema_header "${_scene_schema_dir}/Generated.SceneComponents.h")
file(
  GLOB _scene_schema_tool_scripts
  CONFIGURE_DEPENDS
  "${_scene_schema_tool_dir}/src/scene_schema_codegen/*.py"
)
# MSBuild can already have loaded the old dependency list when ZERO_CHECK runs.
# Track the scripts through CMake and expose one stable input to the build rule.
set_property(
  DIRECTORY
  APPEND
  PROPERTY
    CMAKE_CONFIGURE_DEPENDS
      ${_scene_schema_tool_scripts}
)
set(_scene_schema_tool_versions "")
foreach(_script IN LISTS _scene_schema_tool_scripts)
  file(SHA256 "${_script}" _script_hash)
  string(APPEND _scene_schema_tool_versions "${_script} ${_script_hash}\n")
endforeach()
set(
  _scene_schema_tool_inventory
  "${CMAKE_CURRENT_BINARY_DIR}/scene_schema_generator_sources.txt"
)
file(
  GENERATE
  OUTPUT
  "${_scene_schema_tool_inventory}"
  CONTENT "${_scene_schema_tool_versions}"
)
set(_scene_schema_stamp "${CMAKE_CURRENT_BINARY_DIR}/scene_schema.stamp")
add_custom_command(
  OUTPUT
    ${_scene_schema_stamp}
  COMMAND
    ${Python3_EXECUTABLE} -m scene_schema_codegen.cli --schema
    "${_scene_schema_dir}/SceneComponents.yaml" --json-schema
    "${_scene_schema_dir}/SceneComponents.schema.json" --cpp-out
    "${_scene_schema_header}"
  COMMAND
    ${CMAKE_COMMAND} -E touch ${_scene_schema_stamp}
  DEPENDS
    "${_scene_schema_dir}/SceneComponents.yaml"
    "${_scene_schema_dir}/SceneComponents.schema.json"
    "${_scene_schema_tool_inventory}"
    "${_scene_schema_tool_dir}/pyproject.toml"
    "${OXYGEN_PYTHON_LOCKFILE}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT
    "[SceneSchemaCodeGen] Generating the scene component ids (source tree)"
  VERBATIM
)
add_custom_target(oxygen-scene_schema_gen DEPENDS ${_scene_schema_stamp})
add_dependencies(${META_MODULE_TARGET} oxygen-scene_schema_gen)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("test")
  add_subdirectory("Tools/SceneSchemaCodeGen")
endif()
if(OXYGEN_BUILD_BENCHMARKS)
  add_subdirectory("Benchmarks")
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
    INPUT_PATH
      "${CMAKE_CURRENT_SOURCE_DIR}"
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
