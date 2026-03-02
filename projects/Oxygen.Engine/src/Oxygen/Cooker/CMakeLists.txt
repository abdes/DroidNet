# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Cooker"
  DESCRIPTION "Oxygen content cooking module."
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
find_package(SPNG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

include(JsonSchemaHelpers)

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_COOK_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

oxygen_embed_json_schemas(
  TARGET ${META_MODULE_TARGET}
  OUTPUT_HEADER
    "${CMAKE_BINARY_DIR}/generated/Oxygen/Cooker/Import/Internal/ImportManifest_schema.h"
  NAMESPACE "oxygen::content::import"
  INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/generated"
  CHUNK_SIZE 8192
  SCHEMAS
    kImportManifestSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.import-manifest.schema.json"
    kBufferContainerSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.buffer-container.schema.json"
    kTextureDescriptorSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.texture-descriptor.schema.json"
    kMaterialDescriptorSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.material-descriptor.schema.json"
    kGeometryDescriptorSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.geometry-descriptor.schema.json"
    kSceneDescriptorSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.scene-descriptor.schema.json"
    kInputSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.input.schema.json"
    kInputActionSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.input-action.schema.json"
    kPhysicsSidecarSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.physics-sidecar.schema.json"
)

set(
  OXYGEN_COOKER_HEADERS
  "api_export.h"
  "Import/AsyncImportService.h"
  "Import/BufferContainerImportRequestBuilder.h"
  "Import/BufferContainerImportSettings.h"
  "Import/BufferImportTypes.h"
  "Import/FileError.h"
  "Import/GeometryDescriptorImportRequestBuilder.h"
  "Import/GeometryDescriptorImportSettings.h"
  "Import/IAsyncFileReader.h"
  "Import/IAsyncFileWriter.h"
  "Import/ImportConcurrency.h"
  "Import/ImportDiagnostics.h"
  "Import/ImportJobId.h"
  "Import/ImportManifest.h"
  "Import/ImportOptions.h"
  "Import/ImportProgress.h"
  "Import/ImportReport.h"
  "Import/ImportRequest.h"
  "Import/InputImportRequestBuilder.h"
  "Import/InputImportSettings.h"
  "Import/MaterialDescriptorImportRequestBuilder.h"
  "Import/MaterialDescriptorImportSettings.h"
  "Import/PhysicsImportRequestBuilder.h"
  "Import/PhysicsImportSettings.h"
  "Import/Naming.h"
  "Import/ScriptImportRequestBuilder.h"
  "Import/ScriptImportSettings.h"
  "Import/SceneImportSettings.h"
  "Import/SceneDescriptorImportRequestBuilder.h"
  "Import/SceneDescriptorImportSettings.h"
  "Import/ScratchImage.h"
  "Import/TextureImportDesc.h"
  "Import/TextureDescriptorImportRequestBuilder.h"
  "Import/TextureDescriptorImportSettings.h"
  "Import/TextureImporter.h"
  "Import/TextureImportError.h"
  "Import/TextureImportPresets.h"
  "Import/TextureImportSettings.h"
  "Import/TextureImportTypes.h"
  "Import/TexturePackingPolicy.h"
  "Import/TextureSourceAssembly.h"
  "Loose/Errors.h"
  "Loose/Inspection.h"
  "Loose/Layout.h"
  "Loose/LooseCookedLayout.h"
  "Loose/Types.h"
  "Loose/Validation.h"
  "Pak/PakBuilder.h"
  "Pak/PakBuildPhase.h"
  "Pak/PakBuildRequest.h"
  "Pak/PakBuildResult.h"
  "Pak/PakBuildReport.h"
)

# utility headers used internally but not previously listed
list(APPEND OXYGEN_COOKER_PRIVATE_HEADERS
  "Import/Internal/Utils/DescriptorDocument.h"
  "Import/Internal/Utils/VirtualPathResolution.h"
)

set(
  OXYGEN_COOKER_PRIVATE_HEADERS
  "Import/Internal/AdapterTypes.h"
  "Import/Internal/AsyncImporter.h"
  "Import/Internal/bc7/bc7enc.h"
  "Import/Internal/bc7/Bc7Encoder.h"
  "Import/Internal/Emitters/AssetEmitter.h"
  "Import/Internal/Emitters/BufferEmitter.h"
  "Import/Internal/Emitters/ResourceDescriptorEmitter.h"
  "Import/Internal/Emitters/TextureEmitter.h"
  "Import/Internal/fbx/CoordTransform.h"
  "Import/Internal/fbx/FbxAdapter.h"
  "Import/Internal/fbx/ufbx.h"
  "Import/Internal/gltf/cgltf.h"
  "Import/Internal/gltf/GltfAdapter.h"
  "Import/Internal/ImageDecode.h"
  "Import/Internal/ImageProcessing.h"
  "Import/Internal/ImportEventLoop.h"
  "Import/Internal/ImportJob.h"
  "Import/Internal/ImportJobParams.h"
  "Import/Internal/ImportPipeline.h"
  "Import/Internal/ImportPlanner.h"
  "Import/Internal/ImportSession.h"
  "Import/Internal/JobEntry.h"
  "Import/Internal/Jobs/AudioImportJob.h"
  "Import/Internal/Jobs/BufferImportSubmitter.h"
  "Import/Internal/Jobs/FbxImportJob.h"
  "Import/Internal/Jobs/GeometryDescriptorImportJob.h"
  "Import/Internal/Jobs/GlbImportJob.h"
  "Import/Internal/Jobs/InputImportJob.h"
  "Import/Internal/Jobs/BufferContainerImportJob.h"
  "Import/Internal/Jobs/MaterialDescriptorImportJob.h"
  "Import/Internal/Jobs/PhysicsSidecarImportJob.h"
  "Import/Internal/Jobs/SceneDescriptorImportJob.h"
  "Import/Internal/Jobs/ScriptAssetImportJob.h"
  "Import/Internal/Jobs/ScriptingSidecarImportJob.h"
  "Import/Internal/Jobs/TextureImportJob.h"
  "Import/Internal/Jobs/TextureImportPolicy.h"
  "Import/Internal/LooseCookedIndexRegistry.h"
  "Import/Internal/LooseCookedWriter.h"
  "Import/Internal/Pipelines/BufferPipeline.h"
  "Import/Internal/Pipelines/GeometryPipeline_tangents.h"
  "Import/Internal/Pipelines/GeometryPipeline.h"
  "Import/Internal/Pipelines/InputImportPipeline.h"
  "Import/Internal/Pipelines/MaterialPipeline.h"
  "Import/Internal/Pipelines/MeshBuildPipeline.h"
  "Import/Internal/Pipelines/PhysicsSidecarImportPipeline.h"
  "Import/Internal/Pipelines/ScriptAssetImportPipeline.h"
  "Import/Internal/Pipelines/ScriptImportPipelineCommon.h"
  "Import/Internal/Pipelines/ScenePipeline.h"
  "Import/Internal/Pipelines/ScriptingSidecarImportPipeline.h"
  "Import/Internal/Pipelines/TexturePipeline.h"
  "Import/Internal/ResourceTableAggregator.h"
  "Import/Internal/ResourceTableRegistry.h"
  "Import/Internal/SceneImportRequestBuilder.h"
  "Import/Internal/SidecarSceneResolver.h"
  "Import/Internal/stb/stb_image.h"
  "Import/Internal/TextureCooker.h"
  "Import/Internal/TextureImportRequestBuilder.h"
  "Import/Internal/TextureSourceAssembly_internal.h"
  "Import/Internal/Utils/AssetKeyUtils.h"
  "Import/Internal/Utils/BufferDescriptorSidecar.h"
  "Import/Internal/Utils/ContentHashUtils.h"
  "Import/Internal/Utils/ImportSettingsUtils.h"
  "Import/Internal/Utils/JsonSchemaValidation.h"
  "Import/Internal/Utils/StringUtils.h"
  "Import/Internal/WindowsFileReader.h"
  "Import/Internal/WindowsFileWriter.h"
  "Import/Internal/WorkDispatcher.h"
  "Import/Internal/WorkPayloadStore.h"
  "Pak/PakPlan.h"
  "Pak/PakPlanBuilder.h"
  "Pak/PakMeasureStore.h"
  "Pak/PakPlanPolicy.h"
  "Pak/PakStreamingCrc32.h"
  "Pak/PakValidation.h"
  "Pak/PakWriter.h"
  "Pak/PakManifestWriter.h"
)

set(
  OXYGEN_COOKER_PRIVATE_SOURCES
  "Import/AsyncImportService.cpp"
  "Import/FileError.cpp"
  "Import/ImportManifest.cpp"
  "Import/ImportOptions.cpp"
  "Import/ImportRequest.cpp"
  "Import/Internal/AsyncImporter.cpp"
  "Import/Internal/bc7/bc7enc.cpp"
  "Import/Internal/bc7/Bc7Encoder.cpp"
  "Import/Internal/Emitters/AssetEmitter.cpp"
  "Import/Internal/Emitters/BufferEmitter.cpp"
  "Import/Internal/Emitters/ResourceDescriptorEmitter.cpp"
  "Import/Internal/Emitters/TextureEmitter.cpp"
  "Import/Internal/fbx/FbxAdapter.cpp"
  "Import/Internal/fbx/ufbx.c"
  "Import/Internal/gltf/cgltf.cpp"
  "Import/Internal/gltf/GltfAdapter.cpp"
  "Import/Internal/ImageDecode.cpp"
  "Import/Internal/ImageProcessing.cpp"
  "Import/Internal/ImportEventLoop.cpp"
  "Import/Internal/ImportJob.cpp"
  "Import/Internal/ImportPlanner.cpp"
  "Import/Internal/ImportSession.cpp"
  "Import/Internal/InputImportRequestBuilder.cpp"
  "Import/Internal/BufferContainerImportRequestBuilder.cpp"
  "Import/Internal/GeometryDescriptorImportRequestBuilder.cpp"
  "Import/Internal/Jobs/BufferImportSubmitter.cpp"
  "Import/Internal/MaterialDescriptorImportRequestBuilder.cpp"
  "Import/Internal/PhysicsImportRequestBuilder.cpp"
  "Import/Internal/SceneDescriptorImportRequestBuilder.cpp"
  "Import/Internal/Jobs/AudioImportJob.cpp"
  "Import/Internal/Jobs/FbxImportJob.cpp"
  "Import/Internal/Jobs/GeometryDescriptorImportJob.cpp"
  "Import/Internal/Jobs/GlbImportJob.cpp"
  "Import/Internal/Jobs/InputImportJob.cpp"
  "Import/Internal/Jobs/BufferContainerImportJob.cpp"
  "Import/Internal/Jobs/MaterialDescriptorImportJob.cpp"
  "Import/Internal/Jobs/PhysicsSidecarImportJob.cpp"
  "Import/Internal/Jobs/SceneDescriptorImportJob.cpp"
  "Import/Internal/Jobs/ScriptAssetImportJob.cpp"
  "Import/Internal/Jobs/ScriptingSidecarImportJob.cpp"
  "Import/Internal/Jobs/TextureImportJob.cpp"
  "Import/Internal/LooseCookedIndexRegistry.cpp"
  "Import/Internal/LooseCookedWriter.cpp"
  "Import/Internal/Pipelines/BufferPipeline.cpp"
  "Import/Internal/Pipelines/GeometryPipeline.cpp"
  "Import/Internal/Pipelines/InputImportPipeline.cpp"
  "Import/Internal/Pipelines/MaterialPipeline.cpp"
  "Import/Internal/Pipelines/MeshBuildPipeline.cpp"
  "Import/Internal/Pipelines/PhysicsSidecarImportPipeline.cpp"
  "Import/Internal/Pipelines/ScriptAssetImportPipeline.cpp"
  "Import/Internal/Pipelines/ScenePipeline.cpp"
  "Import/Internal/Pipelines/ScriptingSidecarImportPipeline.cpp"
  "Import/Internal/Pipelines/TexturePipeline.cpp"
  "Import/Internal/ResourceTableRegistry.cpp"
  "Import/Internal/ScriptImportRequestBuilder.cpp"
  "Import/Internal/SceneImportRequestBuilder.cpp"
  "Import/Internal/SidecarSceneResolver.cpp"
  "Import/Internal/TextureCooker.cpp"
  "Import/Internal/TextureDescriptorImportRequestBuilder.cpp"
  "Import/Internal/TextureImportRequestBuilder.cpp"
  "Import/Internal/Utils/BufferDescriptorSidecar.cpp"
  "Import/Internal/Utils/ImportSettingsUtils.cpp"
  "Import/Internal/WindowsFileReader.cpp"
  "Import/Internal/WindowsFileWriter.cpp"
  "Import/Internal/WorkDispatcher.cpp"
  "Import/Internal/WorkPayloadStore.cpp"
  "Import/Naming.cpp"
  "Import/ScratchImage.cpp"
  "Import/TextureImportDesc.cpp"
  "Import/TextureImporter.cpp"
  "Import/TextureImportPresets.cpp"
  "Import/TextureImportTypes.cpp"
  "Import/TexturePackingPolicy.cpp"
  "Import/TextureSourceAssembly.cpp"
  "Loose/Inspection.cpp"
  "Loose/Validation.cpp"
  "Pak/PakPlan.cpp"
  "Pak/PakPlanBuilder.cpp"
  "Pak/PakMeasureStore.cpp"
  "Pak/PakPlanPolicy.cpp"
  "Pak/PakStreamingCrc32.cpp"
  "Pak/PakValidation.cpp"
  "Pak/PakWriter.cpp"
  "Pak/PakManifestWriter.cpp"
  "Pak/PakBuilder.cpp"
  "Pak/PakBuildToString.cpp"
  ${OXYGEN_COOKER_PRIVATE_HEADERS}
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_COOKER_PRIVATE_SOURCES}
    ${OXYGEN_COOKER_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_COOKER_HEADERS}
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
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_COOK_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::core
    oxygen::composition
    oxygen::console
    oxygen::data
    oxygen::content
  PRIVATE
    glm::glm
    spng::spng_static
    tinyexr::tinyexr
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

# ------------------------------------------------------------------------------
# Tools
# ------------------------------------------------------------------------------

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tools")
  add_subdirectory("Tools")
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
  add_subdirectory("Test")
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

if(${META_PROJECT_ID}_INSTALL)
  set(
    _oxygen_cooker_schema_files
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.import-manifest.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.buffer-container.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.texture-descriptor.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.material-descriptor.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.geometry-descriptor.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.scene-descriptor.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.input.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.input-action.schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Import/Schemas/oxygen.physics-sidecar.schema.json"
  )
  foreach(_schema_file IN LISTS _oxygen_cooker_schema_files)
    if(NOT EXISTS "${_schema_file}")
      message(FATAL_ERROR "Missing schema file: ${_schema_file}")
    endif()
  endforeach()

  install(
    FILES
      ${_oxygen_cooker_schema_files}
    DESTINATION ${OXYGEN_INSTALL_DATA}/schemas
    COMPONENT ${META_PROJECT_NAME}_data
  )
endif()

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")
