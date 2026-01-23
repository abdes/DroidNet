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
find_package(tinyexr CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)

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
  "EngineTag.h"
  "EvictionEvents.h"
  "VirtualPathResolver.h"
  "LooseCookedInspection.h"
  "LoaderContext.h"
  "LoaderFunctions.h"
  "OperationCancelledException.h"
  "ResourceKey.h"
  "ResourceTable.h"
  "ResourceTypeList.h"
  "SourceToken.h"
  "IAssetLoader.h"
  "Detail/LooseCookedIndex.h"
  # Import - Public API
  "Import/AsyncImportService.h"
  "Import/BufferImportTypes.h"
  "Import/FileError.h"
  "Import/IAsyncFileReader.h"
  "Import/IAsyncFileWriter.h"
  "Import/ImportDiagnostics.h"
  "Import/ImportConcurrency.h"
  "Import/ImportJobId.h"
  "Import/ImportManifest.h"
  "Import/ImportOptions.h"
  "Import/ImportProgress.h"
  "Import/ImportReport.h"
  "Import/ImportRequest.h"
  "Import/SceneImportSettings.h"
  "Import/TextureImportSettings.h"
  "Import/Layout.h"
  "Import/LooseCookedLayout.h"
  "Import/Naming.h"
  "Import/ScratchImage.h"
  "Import/TextureImportDesc.h"
  "Import/TextureImporter.h"
  "Import/TextureImportError.h"
  "Import/TextureImportPresets.h"
  "Import/TextureImportTypes.h"
  "Import/TexturePackingPolicy.h"
  "Import/TextureSourceAssembly.h"
)
set(
  OXYGEN_CONTENT_PRIVATE_SOURCES
  "AssetLoader.cpp"
  "Detail/LooseCookedIndex.cpp"
  "Internal/ContentSource.h"
  "Internal/DependencyCollector.h"
  "Internal/InternalResourceKey.h"
  "Internal/LooseCookedIndexLoad.h"
  "Internal/ResourceRef.h"
  "Loaders/BufferLoader.h"
  "Loaders/GeometryLoader.h"
  "Loaders/Helpers.h"
  "Loaders/MaterialLoader.h"
  "Loaders/SceneLoader.h"
  "Loaders/TextureLoader.h"
  "LooseCookedInspection.cpp"
  "PakFile.cpp"
  "ResourceKey.cpp"
  "VirtualPathResolver.cpp"
  # Import - implementation
  "Import/AsyncImportService.cpp"
  "Import/FileError.cpp"
  "Import/ImportManifest.cpp"
  "Import/ImportRequest.cpp"
  "Import/Internal/ImportManifest_schema.h"
  "Import/Internal/SceneImportRequestBuilder.h"
  "Import/Internal/SceneImportRequestBuilder.cpp"
  "Import/Internal/TextureImportRequestBuilder.h"
  "Import/Internal/TextureImportRequestBuilder.cpp"
  "Import/Naming.cpp"
  "Import/ScratchImage.cpp"
  "Import/TextureImportDesc.cpp"
  "Import/TextureImporter.cpp"
  "Import/TextureImportPresets.cpp"
  "Import/TextureImportTypes.cpp"
  "Import/TexturePackingPolicy.cpp"
  "Import/TextureSourceAssembly.cpp"
  # Import - Internal stuff
  "Import/Internal/LooseCookedWriter.cpp"
  "Import/Internal/LooseCookedWriter.h"
  "Import/Internal/LooseCookedIndexRegistry.cpp"
  "Import/Internal/LooseCookedIndexRegistry.h"
  "Import/Internal/AdapterTypes.h"
  "Import/Internal/AsyncImporter.cpp"
  "Import/Internal/AsyncImporter.h"
  "Import/Internal/ImageDecode.cpp"
  "Import/Internal/ImageDecode.h"
  "Import/Internal/ImageProcessing.cpp"
  "Import/Internal/ImageProcessing.h"
  "Import/Internal/ImportEventLoop.cpp"
  "Import/Internal/ImportEventLoop.h"
  "Import/Internal/ImportJob.cpp"
  "Import/Internal/ImportJob.h"
  "Import/Internal/ImportJobParams.h"
  "Import/Internal/ImportPipeline.h"
  "Import/Internal/ImportPlanner.cpp"
  "Import/Internal/ImportPlanner.h"
  "Import/Internal/ImportSession.cpp"
  "Import/Internal/ImportSession.h"
  "Import/Internal/JobEntry.h"
  "Import/Internal/ResourceTableAggregator.h"
  "Import/Internal/ResourceTableRegistry.cpp"
  "Import/Internal/ResourceTableRegistry.h"
  "Import/Internal/TextureCooker.cpp"
  "Import/Internal/TextureCooker.h"
  "Import/Internal/TextureSourceAssembly_internal.h"
  "Import/Internal/WorkDispatcher.cpp"
  "Import/Internal/WorkDispatcher.h"
  "Import/Internal/WorkPayloadStore.cpp"
  "Import/Internal/WorkPayloadStore.h"
  # Import - Internal emitters
  "Import/Internal/Emitters/AssetEmitter.h"
  "Import/Internal/Emitters/AssetEmitter.cpp"
  "Import/Internal/Emitters/BufferEmitter.h"
  "Import/Internal/Emitters/BufferEmitter.cpp"
  "Import/Internal/Emitters/TextureEmitter.h"
  "Import/Internal/Emitters/TextureEmitter.cpp"
  # Import - Internal jobs
  "Import/Internal/Jobs/AudioImportJob.h"
  "Import/Internal/Jobs/AudioImportJob.cpp"
  "Import/Internal/Jobs/FbxImportJob.h"
  "Import/Internal/Jobs/FbxImportJob.cpp"
  "Import/Internal/Jobs/GlbImportJob.h"
  "Import/Internal/Jobs/GlbImportJob.cpp"
  "Import/Internal/Jobs/TextureImportJob.h"
  "Import/Internal/Jobs/TextureImportJob.cpp"
  "Import/Internal/Jobs/TextureImportPolicy.h"
  # Import - Internal pipelines
  "Import/Internal/Pipelines/BufferPipeline.h"
  "Import/Internal/Pipelines/BufferPipeline.cpp"
  "Import/Internal/Pipelines/MeshBuildPipeline.h"
  "Import/Internal/Pipelines/MeshBuildPipeline.cpp"
  "Import/Internal/Pipelines/GeometryPipeline.h"
  "Import/Internal/Pipelines/GeometryPipeline_tangents.h"
  "Import/Internal/Pipelines/GeometryPipeline.cpp"
  "Import/Internal/Pipelines/MaterialPipeline.h"
  "Import/Internal/Pipelines/MaterialPipeline.cpp"
  "Import/Internal/Pipelines/ScenePipeline.h"
  "Import/Internal/Pipelines/ScenePipeline.cpp"
  "Import/Internal/Pipelines/TexturePipeline.h"
  "Import/Internal/Pipelines/TexturePipeline.cpp"
  # Import - Utilities
  "Import/Internal/Utils/ContentHashUtils.h"
  "Import/Internal/Utils/StringUtils.h"
  "Import/Internal/Utils/AssetKeyUtils.h"
  "Import/Internal/Utils/ImportSettingsUtils.h"
  "Import/Internal/Utils/ImportSettingsUtils.cpp"
  # Import - Platform-specific implementations
  $<$<BOOL:${WIN32}>:Import/Internal/WindowsFileReader.h>
  $<$<BOOL:${WIN32}>:Import/Internal/WindowsFileReader.cpp>
  $<$<BOOL:${WIN32}>:Import/Internal/WindowsFileWriter.h>
  $<$<BOOL:${WIN32}>:Import/Internal/WindowsFileWriter.cpp>
  # Import - Backends and Third-party libraries
  "Import/Internal/bc7/bc7enc.h"
  "Import/Internal/bc7/bc7enc.cpp"
  "Import/Internal/bc7/Bc7Encoder.h"
  "Import/Internal/bc7/Bc7Encoder.cpp"
  "Import/Internal/fbx/CoordTransform.h"
  "Import/Internal/fbx/ufbx.h"
  "Import/Internal/fbx/ufbx.c"
  "Import/Internal/fbx/FbxAdapter.h"
  "Import/Internal/fbx/FbxAdapter.cpp"
  "Import/Internal/gltf/cgltf.h"
  "Import/Internal/gltf/cgltf.cpp"
  "Import/Internal/gltf/GltfAdapter.h"
  "Import/Internal/gltf/GltfAdapter.cpp"
  "Import/Internal/stb/stb_image.h"
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

# Enable OpenMP for bc7enc source only: /openmp for MSVC, -fopenmp otherwise
set_source_files_properties(
  "Import/Internal/bc7/bc7enc.cpp"
  PROPERTIES
    COMPILE_OPTIONS
      "$<$<CXX_COMPILER_ID:MSVC>:/openmp>$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fopenmp>"
)

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_options(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    UFBX_REAL_IS_FLOAT
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_CNTT_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::composition
    oxygen::data
  PRIVATE
    glm::glm
    tinyexr::tinyexr
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

# ------------------------------------------------------------------------------
# Tools
# ------------------------------------------------------------------------------

add_subdirectory("Tools")

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
