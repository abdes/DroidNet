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
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)

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
  "AssetKey.h"
  "AssetType.h"
  "BufferResource.h"
  "ComponentType.h"
  "GeometryAsset.h"
  "HalfFloat.h"
  "LooseCookedIndexFormat.h"
  "MaterialAsset.h"
  "MaterialDomain.h"
  "MeshType.h"
  "PakFormat.h"
  "ProceduralMeshes.h"
  "SceneAsset.h"
  "ShaderReference.h"
  "SourceKey.h"
  "TextureResource.h"
  "Unorm16.h"
  "Vertex.h"
)
set(
  OXYGEN_DATA_PRIVATE_SOURCES
  "AssetKey.cpp"
  "BufferResource.cpp"
  "GeometryAsset.cpp"
  "MaterialAsset.cpp"
  "ProceduralMeshes.cpp"
  "SceneAsset.cpp"
  "ShaderReference.cpp"
  "SourceKey.cpp"
  "TextureResource.cpp"
  "ToStringConverters.cpp"
  "Procedural/ArrowGizmo.cpp"
  "Procedural/Cone.cpp"
  "Procedural/Cube.cpp"
  "Procedural/Cylinder.cpp"
  "Procedural/Plane.cpp"
  "Procedural/Quad.cpp"
  "Procedural/Sphere.cpp"
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

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
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
    stduuid::stduuid
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
  add_subdirectory("Test")
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
