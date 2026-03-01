# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Content"
  DESCRIPTION "Oxygen asset management."
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

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
      "OXGN_CNTT_EXPORTS"
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
  OXYGEN_CONTENT_HEADERS
  "api_export.h"
  "PakFile.h"
  "AssetLoader.h"
  "Constants.h"
  "EvictionEvents.h"
  "VirtualPathResolver.h"
  "LoaderContext.h"
  "LoaderFunctions.h"
  "OperationCancelledException.h"
  "ResourceKey.h"
  "ResidencyPolicy.h"
  "ResourceTable.h"
  "ResourceTypeList.h"
  "SourceToken.h"
  "InputContextHydration.h"
  "IAssetLoader.h"
  "Layout.h"
  "LooseCookedIndex.h"
)
set(
  OXYGEN_CONTENT_PRIVATE_SOURCES
  "AssetLoader.cpp"
  "Internal/AssetIdentityIndex.h"
  "Internal/AssetIdentityIndex.cpp"
  "Internal/ContentSourceRegistry.h"
  "Internal/ContentSourceRegistry.cpp"
  "Internal/DependencyGraphStore.h"
  "Internal/DependencyGraphStore.cpp"
  "Internal/DependencyReleaseEngine.h"
  "Internal/DependencyReleaseEngine.cpp"
  "Internal/EvictionRegistry.h"
  "Internal/EvictionRegistry.cpp"
  "Internal/InFlightOperationTable.h"
  "Internal/InFlightOperationTable.cpp"
  "Internal/ResourceLoadPipeline.h"
  "Internal/ResourceLoadPipeline.cpp"
  "Internal/SceneCatalogQueryService.h"
  "Internal/SceneCatalogQueryService.cpp"
  "Internal/ScriptQueryService.h"
  "Internal/ScriptQueryService.cpp"
  "Internal/PhysicsQueryService.h"
  "Internal/PhysicsQueryService.cpp"
  "Internal/ResourceKeyRegistry.h"
  "Internal/ResourceKeyRegistry.cpp"
  "Internal/ScriptHotReloadService.h"
  "Internal/ScriptHotReloadService.cpp"
  "Internal/DependencyCollector.h"
  "Internal/IContentSource.h"
  "Internal/InputContextHydration.cpp"
  "Internal/InternalResourceKey.h"
  "Internal/LooseCookedIndexImpl.h"
  "Internal/LooseCookedIndexImpl.cpp"
  "Internal/LooseCookedIndexCodec.h"
  "Internal/LooseCookedSource.h"
  "Internal/PatchResolutionPolicy.h"
  "Internal/PatchResolutionPolicy.cpp"
  "Internal/PakFileSource.h"
  "Internal/ResourceRef.h"
  "Loaders/BufferLoader.h"
  "Loaders/GeometryLoader.h"
  "Loaders/Helpers.h"
  "Loaders/InputActionLoader.h"
  "Loaders/InputMappingContextLoader.h"
  "Loaders/MaterialLoader.h"
  "Loaders/PhysicsResourceLoader.h"
  "Loaders/PhysicsSceneLoader.h"
  "Loaders/SceneLoader.h"
  "Loaders/ScriptLoader.h"
  "Loaders/TextureLoader.h"
  "LooseCookedIndex.cpp"
  "PakFile.cpp"
  "ResourceKey.cpp"
  "VirtualPathResolver.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_CONTENT_PRIVATE_SOURCES}
    ${OXYGEN_CONTENT_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_CONTENT_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_options(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_CNTT_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::composition
    oxygen::console
    oxygen::data
  PRIVATE
    glm::glm
    oxygen::input
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
