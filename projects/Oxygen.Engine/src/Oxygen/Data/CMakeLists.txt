# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Data"
  DESCRIPTION "Oxygen immutable,shareable assets module."
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

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
      "OXGN_DATA_EXPORTS"
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
  OXYGEN_DATA_HEADERS
  "api_export.h"
  "Asset.h"
  "AssetReferences.h"
  "AssetRuntimeBindings.h"
  "AssetType.h"
  "BufferResource.h"
  "ComponentType.h"
  "GeometryAsset.h"
  "GeometryIndices.h"
  "HalfFloat.h"
  "InputActionAsset.h"
  "InputMappingContextAsset.h"
  "CookedSource.h"
  "DescriptorStrings.h"
  "LooseCookedIndexFormat.h"
  "MaterialAsset.h"
  "MaterialDomain.h"
  "StableId.h"
  "SurfaceId.h"
  "SurfaceInventory.h"
  "MeshType.h"
  "PakCatalog.h"
  "PakFormat.h"
  "PakFormatVersions.h"
  "PakFormatVersions.inc"
  "PakFormat_core.h"
  "PakFormat_references.h"
  "PakFormat_world.h"
  "SceneRecordDefaults.h"
  "LightValidation.h"
  "LodPolicyValidation.h"
  "PakFormat_geometry.h"
  "PakFormat_render.h"
  "PakFormat_scripting.h"
  "PakFormat_input.h"
  "PakFormat_physics.h"
  "PakFormat_audio.h"
  "PakFormat_animation.h"
  "PakFormatSerioLoaders.h"
  "PakFormatSerioWriters.h"
  "PatchManifest.h"
  "PhysicsResource.h"
  "PhysicsSceneAsset.h"
  "ProceduralMeshes.h"
  "ProceduralMeshDefaults.h"
  "SceneAsset.h"
  "SceneChangeValues.h"
  "SceneIds.h"
  "SceneRecordFields.h"
  "ScriptAsset.h"
  "ScriptResource.h"
  "ShaderReference.h"
  "SourceKey.h"
  "SourceOrigin.h"
  "TextureResource.h"
  "TextureResourceDescriptor.h"
  "Unorm16.h"
  "Vertex.h"
  "Schema/Generated.SceneChanges.h"
  "Schema/Generated.SceneComponents.h"
  "Schema/PropertyRange.h"
)
set(
  OXYGEN_DATA_PRIVATE_SOURCES
  "AssetReferences.cpp"
  "BufferResource.cpp"
  "CookedSource.cpp"
  "DescriptorStrings.cpp"
  "GeometryAsset.cpp"
  "InputActionAsset.cpp"
  "InputMappingContextAsset.cpp"
  "MaterialAsset.cpp"
  "StableId.cpp"
  "SurfaceInventory.cpp"
  "PakCatalog.cpp"
  "PhysicsResource.cpp"
  "PhysicsSceneAsset.cpp"
  "ProceduralMeshes.cpp"
  "SceneAsset.cpp"
  "SceneRecordFields.cpp"
  "ScriptAsset.cpp"
  "ScriptResource.cpp"
  "ShaderReference.cpp"
  "TextureResource.cpp"
  "TextureResourceDescriptor.cpp"
  "ToStringConverters.cpp"
  "Procedural/ArrowGizmo.cpp"
  "Procedural/Capsule.cpp"
  "Procedural/Cone.cpp"
  "Procedural/Cube.cpp"
  "Procedural/Cylinder.cpp"
  "Procedural/IcoSphere.cpp"
  "Procedural/Plane.cpp"
  "Procedural/Quad.cpp"
  "Procedural/Sphere.cpp"
  "Procedural/SubdividedCube.cpp"
  "Procedural/Torus.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_DATA_PRIVATE_SOURCES}
    ${OXYGEN_DATA_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_DATA_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_DATA_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::core
    oxygen::composition
    oxygen::serio
    fmt::fmt-header-only
    glm::glm
)

# ------------------------------------------------------------------------------
# Component and field ids of the scene components schema
# ------------------------------------------------------------------------------

# SceneSchemaCodeGen validates Schema/SceneComponents.yaml and writes
# Schema/Generated.SceneComponents.h (the ids, defaults and ranges) and
# Schema/Generated.SceneChanges.h (the change encoder and record appliers) into
# the source tree, where they are versioned. A stamp in the build tree is the
# only declared output, so Clean never deletes the versioned headers. The
# generator writes a header only when its text changes, so a regeneration that
# changes nothing recompiles nothing.
set(
  _scene_schema_tool_dir
  "${CMAKE_CURRENT_SOURCE_DIR}/Tools/SceneSchemaCodeGen"
)
set(_scene_schema_dir "${CMAKE_CURRENT_SOURCE_DIR}/Schema")
set(_scene_schema_header "${_scene_schema_dir}/Generated.SceneComponents.h")
set(_scene_changes_header "${_scene_schema_dir}/Generated.SceneChanges.h")
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
    "${_scene_schema_header}" --changes-out "${_scene_changes_header}"
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
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory("test")
  endif()
  add_subdirectory("Tools/SceneSchemaCodeGen")
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

if(OXYGEN_INSTALL)
  # install-sdk: static
  # The scene property schema ships with the SDK: consumers built on the
  # installed SDK, such as the editor, generate their own code from it.
  install(
    FILES
      "${_scene_schema_dir}/SceneComponents.yaml"
      "${_scene_schema_dir}/SceneComponents.schema.json"
    DESTINATION "${OXYGEN_INSTALL_SCHEMAS}"
    COMPONENT Oxygen_data
  )
endif()

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
