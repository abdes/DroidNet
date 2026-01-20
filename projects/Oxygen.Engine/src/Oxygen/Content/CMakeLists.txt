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
  # Imporeters - External code
  # "Import/AssetImporter.h"
  "Import/ImportDiagnostics.h"
  "Import/ImportFormat.h"
  "Import/Layout.h"
  "Import/Naming.h"
  "Import/ImportOptions.h"
  "Import/ImportedGeometry.h"
  "Import/ImportReport.h"
  "Import/ImportRequest.h"
  "Import/LooseCookedLayout.h"
  "Import/LooseCookedWriter.h"
  "Import/ImportPlanner.h"
  "Import/TextureImportTypes.h"
  "Import/BufferImportTypes.h"
  "Import/TextureImportError.h"
  "Import/TextureImportDesc.h"
  "Import/ScratchImage.h"
)
set(
  OXYGEN_CONTENT_PRIVATE_SOURCES
  "AssetLoader.cpp"
  "LooseCookedInspection.cpp"
  "PakFile.cpp"
  "ResourceKey.cpp"
  "VirtualPathResolver.cpp"
  "Internal/ContentSource.h"
  "Internal/InternalResourceKey.h"
  "Internal/LooseCookedIndexLoad.h"
  "Internal/DependencyCollector.h"
  "Internal/ResourceRef.h"
  "Loaders/BufferLoader.h"
  "Loaders/GeometryLoader.h"
  "Loaders/MaterialLoader.h"
  "Loaders/SceneLoader.h"
  "Loaders/TextureLoader.h"
  "Loaders/Helpers.h"
  "Detail/LooseCookedIndex.cpp"
  # Imporeters - Internal code
  # "Import/AssetImporter.cpp"
  # "Import/FbxImporter.cpp"
  "Import/ImageDecode.h"
  "Import/ImageDecode.cpp"
  "Import/ImageProcessing.h"
  "Import/ImageProcessing.cpp"
  "Import/LooseCookedWriter.cpp"
  "Import/fbx/ufbx.h"
  "Import/fbx/ufbx.c"
  "Import/fbx/UfbxUtils.h"
  "Import/gltf/cgltf.h"
  "Import/gltf/cgltf.cpp"
  "Import/stb/stb_image.h"
  "Import/bc7/bc7enc.cpp"
  "Import/bc7/bc7enc.h"
  "Import/bc7/Bc7Encoder.h"
  "Import/bc7/Bc7Encoder.cpp"
  "Import/TextureImportTypes.cpp"
  "Import/TextureImportDesc.cpp"
  "Import/TexturePackingPolicy.h"
  "Import/TexturePackingPolicy.cpp"
  "Import/TextureCooker.h"
  "Import/TextureCooker.cpp"
  "Import/TextureImportPresets.h"
  "Import/TextureImportPresets.cpp"
  "Import/TextureSourceAssembly.h"
  "Import/TextureSourceAssembly.cpp"
  "Import/ScratchImage.cpp"
  "Import/TextureImporter.h"
  "Import/TextureImporter.cpp"
  # Import utilities
  "Import/util/Constants.h"
  "Import/util/CoordTransform.h"
  "Import/util/ImportNaming.h"
  "Import/util/Signature.h"
  "Import/util/StringUtils.h"
  "Import/util/TangentGen.h"
  "Import/util/TextureRepack.h"
  "Import/Internal/TextureSourceAssemblyInternal.h"
  "Import/fbx/FbxAdapter.h"
  "Import/fbx/FbxAdapter.cpp"
  "Import/gltf/GltfAdapter.h"
  "Import/gltf/GltfAdapter.cpp"
  "Import/Detail/AdapterTypes.h"
  "Import/ImportEventLoop.h"
  "Import/ImportEventLoop.cpp"
  "Import/FileError.h"
  "Import/FileError.cpp"
  "Import/IAsyncFileReader.h"
  "Import/IAsyncFileWriter.h"
  "Import/MaterialReadinessTracker.h"
  "Import/ImportSession.h"
  "Import/ImportSession.cpp"
  "Import/MaterialReadinessTracker.cpp"
  "Import/ImportPlanner.h"
  "Import/ImportPlanner.cpp"
  "Import/AsyncImportService.h"
  "Import/AsyncImportService.cpp"
  "Import/ResourceTableAggregator.h"
  "Import/ResourceTableRegistry.h"
  "Import/ResourceTableRegistry.cpp"
  "Import/Detail/AsyncImporter.h"
  "Import/Detail/AsyncImporter.cpp"
  "Import/Detail/JobEntry.h"
  "Import/Detail/WorkDispatcher.h"
  "Import/Detail/WorkDispatcher.cpp"
  "Import/Detail/WorkPayloadStore.h"
  "Import/Detail/WorkPayloadStore.cpp"
  "Import/Detail/ImportJob.h"
  "Import/Detail/ImportJob.cpp"
  "Import/ImportPipeline.h"
  "Import/Jobs/FbxImportJob.h"
  "Import/Jobs/FbxImportJob.cpp"
  "Import/Jobs/GlbImportJob.h"
  "Import/Jobs/GlbImportJob.cpp"
  "Import/Jobs/TextureImportPolicy.h"
  "Import/Jobs/TextureImportJob.h"
  "Import/Jobs/TextureImportJob.cpp"
  "Import/Jobs/AudioImportJob.h"
  "Import/Jobs/AudioImportJob.cpp"
  "Import/Emitters/TextureEmitter.h"
  "Import/Emitters/TextureEmitter.cpp"
  "Import/Emitters/BufferEmitter.h"
  "Import/Emitters/BufferEmitter.cpp"
  "Import/Emitters/AssetEmitter.h"
  "Import/Emitters/AssetEmitter.cpp"
  "Import/Pipelines/BufferPipeline.h"
  "Import/Pipelines/BufferPipeline.cpp"
  "Import/Pipelines/GeometryPipeline.h"
  "Import/Pipelines/GeometryPipeline.cpp"
  "Import/Pipelines/TexturePipeline.h"
  "Import/Pipelines/TexturePipeline.cpp"
  "Import/Pipelines/ScenePipeline.h"
  "Import/Pipelines/ScenePipeline.cpp"
  "Import/Pipelines/MaterialPipeline.h"
  "Import/Pipelines/MaterialPipeline.cpp"
  "Import/Naming.cpp"
  $<$<BOOL:${WIN32}>:Import/WindowsFileReader.h>
  $<$<BOOL:${WIN32}>:Import/WindowsFileReader.cpp>
  $<$<BOOL:${WIN32}>:Import/WindowsFileWriter.h>
  $<$<BOOL:${WIN32}>:Import/WindowsFileWriter.cpp>
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
  "Import/bc7enc/bc7enc.cpp"
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
