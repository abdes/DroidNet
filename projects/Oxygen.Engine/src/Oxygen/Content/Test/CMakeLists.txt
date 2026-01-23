# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Library Link Test ---------------------------------------------------------

set(link_test_target "Oxygen.Content.LinkTest")
add_executable(${link_test_target} "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${link_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(${link_test_target} PRIVATE oxygen::content)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  Loaders
  SOURCES
    "AssetHeaderLoader_test.cpp"
    "AssetLoader_async_test.cpp"
    "AssetLoader_basic_test.cpp"
    "AssetLoader_buffer_test.cpp"
    "AssetLoader_cancellation_test.cpp"
    "AssetLoader_deps_test.cpp"
    "AssetLoader_geometry_test.cpp"
    "AssetLoader_loading_test.cpp"
    "AssetLoader_scene_test.cpp"
    "AssetLoader_test_helpers.cpp"
    "AssetLoader_test.h"
    "BufferLoader_test.cpp"
    "EngineTagFactory_test_impl.cpp"
    "LooseCookedIndex_test.cpp"
    "MaterialLoader_test.cpp"
    "Mocks/MockStream.h"
    "SceneLoader_test.cpp"
    "TextureLoader_test.cpp"
    "Utils/PakUtils.h"
    "VirtualPathResolver_test.cpp"
)
target_compile_definitions(
  Oxygen.Content.Loaders.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

# Ensure the Loaders tests run only after PakGen editable install so the
# pakgen CLI is available for any dynamic pack builds performed by tests.
add_dependencies("${META_MODULE_NAME}.Loaders.Tests" pakgen_editable_install)

# -- Async Import Test Programs ------------------------------------------------

m_gtest_program(
  AsyncImportCore
  SOURCES
    "Import/ImageDecode_test.cpp"
    "Import/Bc7Encoder_test.cpp"
    "Import/ImageProcessing_test.cpp"
    "Import/LooseCookedWriter_test.cpp"
    "Import/AsyncImporter_test.cpp"
    "Import/AsyncImportService_test.cpp"
    "Import/ImportEventLoop_test.cpp"
    "Import/ImportJob_test.cpp"
    "Import/ImportSession_test.cpp"
    "Mocks/TestImportJob.cpp"
    "Mocks/TestImportJob.h"
)

m_gtest_program(
  AsyncImportIO
  SOURCES
    "Import/FileError_test.cpp"
    $<$<BOOL:${WIN32}>:Import/WindowsFileReader_test.cpp>
    $<$<BOOL:${WIN32}>:Import/WindowsFileWriter_test.cpp>
)

m_gtest_program(
  AsyncImportBuffer
  SOURCES
    "Import/BufferPipeline_test.cpp"
    "Import/BufferEmitter_test.cpp"
)

m_gtest_program(
  AsyncImportAssets
  SOURCES
    "Import/AssetEmitter_test.cpp"
    "Import/GeometryPipeline_test.cpp"
    "Import/MaterialPipeline_test.cpp"
    "Import/PipelineConformance_test.cpp"
    "Import/ScenePipeline_test.cpp"
)

m_gtest_program(
  AsyncImportTexture
  SOURCES
    "Import/ScratchImage_test.cpp"
    "Import/TextureCooker_basic_test.cpp"
    "Import/TextureImportPresets_test.cpp"
    "Import/TexturePackingPolicy_test.cpp"
    "Import/TextureSourceAssembly_test.cpp"
    "Import/TexturePipeline_basic_test.cpp"
    "Import/TexturePipeline_nonreg_test.cpp"
    "Import/TexturePipeline_edge_test.cpp"
    "Import/TextureImportJob_policy_test.cpp"
    "Import/TextureEmitter_test.cpp"
)

m_gtest_program(
  AsyncImportFbx
  SOURCES
    "Import/AsyncImporterFullTestBase.cpp"
    "Import/AsyncImporterFullTestBase.h"
    "Import/AsyncFbxImporter_test.cpp"
)

m_gtest_program(
  AsyncImportGltf
  SOURCES
    "Import/AsyncImporterFullTestBase.cpp"
    "Import/AsyncImporterFullTestBase.h"
    "Import/AsyncGltfImporter_test.cpp"
)

# -- Plan Test Programs -------------------------------------------------------

m_gtest_program(AsyncImportPlan SOURCES "Plan/ImportPlanner_test.cpp")
