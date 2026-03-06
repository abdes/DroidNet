# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)
find_package(nlohmann_json_schema_validator REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(ftxui REQUIRED)

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
    "Import/AssetEmitter_test.cpp"
    "Import/AsyncImporter_test.cpp"
    "Import/AsyncImportService_test.cpp"
    "Import/Bc7Encoder_test.cpp"
    "Import/ImageDecode_test.cpp"
    "Import/ImageProcessing_test.cpp"
    "Import/ImportEventLoop_test.cpp"
    "Import/ImportJob_test.cpp"
    "Import/ImportSession_test.cpp"
    "Import/PipelineConformance_test.cpp"
    "Mocks/TestImportJob.cpp"
    "Mocks/TestImportJob.h"
)

m_gtest_program(
  AsyncImportInput
  SOURCES
    "Import/InputJsonSchema_test.cpp"
    "Import/InputImportRequestBuilder_test.cpp"
    "Import/ImportManifest_input_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportInput.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

m_gtest_program(
  AsyncImportPhysics
  SOURCES
    "Import/ImportManifest_physics_sidecar_test.cpp"
    "Import/PhysicsPhase3Closure_test.cpp"
    "Import/PhysicsImportJob_test.cpp"
    "Import/PhysicsImportPipeline_test.cpp"
    "Import/PhysicsImportRequestBuilder_test.cpp"
    "Import/PhysicsJsonSchema_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportPhysics.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

gtest_program(
  "Oxygen.Cooker.ImportToolBatchDag.Tests"
  SOURCES
    "Import/BatchCommand_physics_dag_test.cpp"
    "../Tools/ImportTool/BatchCommand.cpp"
    "../Tools/ImportTool/ReportJson.cpp"
    "../Tools/ImportTool/UI/Screens/BatchImportScreen.cpp"
    "../Tools/ImportTool/UI/Screens/ImportScreen.cpp"
  DEPS
    ${META_MODULE_TARGET}
    oxygen::testing
    oxygen::clap
    fmt::fmt-header-only
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
)
target_compile_definitions(
  Oxygen.Cooker.ImportToolBatchDag.Tests
  PRIVATE
    OXYGEN_IMPORT_TOOL_VERSION="0.1"
)

m_gtest_program(
  AsyncImportPhysicsMaterialDescriptor
  SOURCES
    "Import/PhysicsMaterialDescriptorImportJob_test.cpp"
    "Import/ImportManifest_physics_material_descriptor_test.cpp"
    "Import/PhysicsMaterialDescriptorImportRequestBuilder_test.cpp"
    "Import/PhysicsMaterialDescriptorJsonSchema_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportPhysicsMaterialDescriptor.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

m_gtest_program(
  AsyncImportCollisionShapeDescriptor
  SOURCES
    "Import/CollisionShapeDescriptorImportJob_test.cpp"
    "Import/ImportManifest_collision_shape_descriptor_test.cpp"
    "Import/CollisionShapeDescriptorImportRequestBuilder_test.cpp"
    "Import/CollisionShapeDescriptorJsonSchema_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportCollisionShapeDescriptor.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

m_gtest_program(
  AsyncImportTextureDescriptor
  SOURCES
    "Import/ImportManifest_texture_descriptor_test.cpp"
    "Import/TextureDescriptorImportRequestBuilder_test.cpp"
    "Import/TextureDescriptorJsonSchema_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportTextureDescriptor.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

m_gtest_program(
  AsyncImportMaterialDescriptor
  SOURCES
    "Import/MaterialDescriptorImportJob_test.cpp"
    "Import/ImportManifest_material_descriptor_test.cpp"
    "Import/MaterialDescriptorImportRequestBuilder_test.cpp"
    "Import/MaterialDescriptorJsonSchema_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportMaterialDescriptor.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

m_gtest_program(
  AsyncImportGeometryDescriptor
  SOURCES
    "Import/GeometryDescriptorImportJob_test.cpp"
    "Import/ImportManifest_geometry_descriptor_test.cpp"
    "Import/GeometryDescriptorImportRequestBuilder_test.cpp"
    "Import/GeometryDescriptorJsonSchema_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportGeometryDescriptor.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

m_gtest_program(
  AsyncImportSceneDescriptor
  SOURCES
    "Import/SceneDescriptorImportJob_test.cpp"
    "Import/ImportManifest_scene_descriptor_test.cpp"
    "Import/SceneDescriptorImportRequestBuilder_test.cpp"
    "Import/SceneDescriptorJsonSchema_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportSceneDescriptor.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

m_gtest_program(
  AsyncImportScript
  SOURCES
    "Import/ImportManifest_script_sidecar_test.cpp"
    "Import/ScriptImportRequestBuilder_test.cpp"
    "Import/ScriptImportJob_test.cpp"
    "Import/ScriptImportPipeline_test.cpp"
    "Import/ScriptCompileMode_contract_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportScript.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
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
  AsyncImportBufferContainer
  SOURCES
    "Import/BufferContainerImportJob_test.cpp"
    "Import/BufferContainerImportRequestBuilder_test.cpp"
    "Import/ImportManifest_buffer_container_test.cpp"
)
target_link_libraries(
  Oxygen.Cooker.AsyncImportBufferContainer.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

m_gtest_program(AsyncImportGeometry SOURCES "Import/GeometryPipeline_test.cpp")

m_gtest_program(AsyncImportScene SOURCES "Import/ScenePipeline_test.cpp")

m_gtest_program(AsyncImportMaterials SOURCES "Import/MaterialPipeline_test.cpp")

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
    "Loose/LooseCookedWriter_test.cpp"
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
