# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Renderer"
  DESCRIPTION "Oxygen rendering layer"
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
      "OXGN_RNDR_EXPORTS"
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
  OXYGEN_RENDERER_HEADERS
  "api_export.h"
  "CameraView.h"
  "Detail/BindlessStructuredBuffer.h"
  "Passes/DepthPrePass.h"
  "Passes/RenderPass.h"
  "Passes/ShaderPass.h"
  "Passes/TransparentPass.h"
  "PreparedSceneFrame.h"
  "RenderContext.h"
  "RendererTag.h"
  "Renderer.h"
  "ScenePrep/CollectionConfig.h"
  "ScenePrep/FinalizationConfig.h"
  "Types/DrawMetadata.h"
  "Types/MaterialConstants.h"
  "Types/PassMask.h"
  "Types/SceneConstants.h"
)
set(
  OXYGEN_RENDERER_PRIVATE_SOURCES
  "CameraView.cpp"
  "Detail/BindlessStructuredBuffer.cpp"
  "Internal/RenderScope.h"
  "Passes/DepthPrePass.cpp"
  "Passes/RenderPass.cpp"
  "Passes/ShaderPass.cpp"
  "Passes/TransparentPass.cpp"
  "PreparedSceneFrame.cpp"
  "Renderer.cpp"
  "Resources/AtlasBuffer.cpp"
  "Resources/AtlasBuffer.h"
  "Resources/GeometryUploader.cpp"
  "Resources/GeometryUploader.h"
  "Resources/MaterialBinder.cpp"
  "Resources/MaterialBinder.h"
  "Resources/TransformUploader.cpp"
  "Resources/TransformUploader.h"
  "Resources/UploadHelpers.cpp"
  "Resources/UploadHelpers.h"
  "ScenePrep/Concepts.h"
  "ScenePrep/Extractors.h"
  "ScenePrep/Finalizers.h"
  "ScenePrep/RenderItemData.h"
  "ScenePrep/RenderItemProto.h"
  "ScenePrep/ScenePrepPipeline.cpp"
  "ScenePrep/ScenePrepPipeline.h"
  "ScenePrep/ScenePrepState.h"
  "ScenePrep/Types.h"
  "Types/SceneConstants.cpp"
  "Upload/RingBufferStaging.cpp"
  "Upload/RingBufferStaging.h"
  "Upload/SingleBufferStaging.h"
  "Upload/StagingProvider.h"
  "Upload/Types.h"
  "Upload/UploadCoordinator.cpp"
  "Upload/UploadCoordinator.h"
  "Upload/UploadDiagnostics.h"
  "Upload/UploaderTag.h"
  "Upload/UploadPlanner.cpp"
  "Upload/UploadPlanner.h"
  "Upload/UploadPolicy.cpp"
  "Upload/UploadPolicy.h"
  "Upload/UploadTracker.cpp"
  "Upload/UploadTracker.h"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_RENDERER_PRIVATE_SOURCES}
    ${OXYGEN_RENDERER_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_RENDERER_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_23)
target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_RNDR_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    glm::glm
    oxygen::base
    oxygen::oxco
    oxygen::data
    oxygen::scene
    oxygen::graphics-common
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
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
