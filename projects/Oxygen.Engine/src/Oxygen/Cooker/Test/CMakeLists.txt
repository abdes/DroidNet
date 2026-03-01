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

add_executable("Oxygen.Cooker.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Cooker.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Cooker.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries("Oxygen.Cooker.LinkTest" PRIVATE oxygen::cooker)
add_test(NAME "Oxygen.Cooker.LinkTest" COMMAND "Oxygen.Cooker.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

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
  AsyncImportScripting
  SOURCES
    "Import/ImportManifest_script_sidecar_test.cpp"
    "Import/ScriptImportRequestBuilder_test.cpp"
    "Import/ScriptImportJob_test.cpp"
    "Import/ScriptImportPipeline_test.cpp"
    "Import/ScriptCompileMode_contract_test.cpp"
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

m_gtest_program(AsyncImportPlan SOURCES "Plan/ImportPlanner_test.cpp")

# -- Loose Cooked --------------------------------------------------------------

m_gtest_program(
  LooseCooked
  SOURCES
    "Loose/DummyIndex.h"
    "Loose/Inspection_test.cpp"
    "Loose/LooseCookedLayout_test.cpp"
    "Loose/Validation_test.cpp"
)
target_compile_features(Oxygen.Cooker.LooseCooked.Tests PUBLIC cxx_std_20)

# -- Pak Builder ---------------------------------------------------------------

m_gtest_program(
  PakBinaryConformance
  SOURCES
    "Pak/PakBinaryConformance_test.cpp"
    "Pak/PakTestSupport.h"
)

m_gtest_program(
  PakDomainValidation
  SOURCES
    "Pak/PakDomainValidation_test.cpp"
    "Pak/PakPlanBuilder_test.cpp"
    "Pak/PakTestSupport.h"
)

m_gtest_program(PakPatchPlanner SOURCES "Pak/PakPatchPlanner_test.cpp")

m_gtest_program(PakWriter SOURCES "Pak/PakWriter_test.cpp")

m_gtest_program(
  PakE2E
  SOURCES
    "Pak/PakBuilder_api_test.cpp"
    "Pak/PakManifest_test.cpp"
    "Pak/PakTestSupport.h"
)
