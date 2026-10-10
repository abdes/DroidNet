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
target_link_libraries("Oxygen.Cooker.LinkTest" PRIVATE oxygen::cooker)
add_test(NAME "Oxygen.Cooker.LinkTest" COMMAND "Oxygen.Cooker.LinkTest")

include(GTestHelpers)

# -- Import --------------------------------------------------------------------

oxygen_cooker_test(
  Import
  UNIT
  DEPS
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
  SOURCES
    "Import/AsyncImporter_test.cpp"
    "Import/Bc7Encoder_test.cpp"
    "Import/BufferSource_test.cpp"
    "Import/BuiltinGeometryCatalog_test.cpp"
    "Import/CapturedInputSet_test.cpp"
    "Import/FileError_test.cpp"
    "Import/GeometrySource_test.cpp"
    "Import/ImageDecode_test.cpp"
    "Import/ImageProcessing_test.cpp"
    "Import/ImportPlanner_test.cpp"
    "Import/ImportedLightSemantics_test.cpp"
    "Import/InputImportRequestBuilder_test.cpp"
    "Import/Jobs/TextureImportPolicy_test.cpp"
    "Import/MaterialSlotProvenance_test.cpp"
    "Import/MaterialSource_test.cpp"
    "Import/MeshTransformBake_test.cpp"
    "Import/Naming_test.cpp"
    "Import/PhysicsImportRequestBuilder_test.cpp"
    "Import/SceneNodeImportDefaults_test.cpp"
    "Import/SceneSource_test.cpp"
    "Import/Schemas/CollisionShapeDescriptorSchema_test.cpp"
    "Import/Schemas/GeometryDescriptorSchema_test.cpp"
    "Import/Schemas/InputSchema_test.cpp"
    "Import/Schemas/MaterialDescriptorSchema_test.cpp"
    "Import/Schemas/PhysicsMaterialDescriptorSchema_test.cpp"
    "Import/Schemas/PhysicsSidecarSchema_test.cpp"
    "Import/Schemas/SceneDescriptorSchema_test.cpp"
    "Import/Schemas/TextureDescriptorSchema_test.cpp"
    "Import/ScratchImage_test.cpp"
    "Import/ScriptImportRequestBuilder_test.cpp"
    "Import/StaticSourceValidation_test.cpp"
    "Import/TextureCooker_test.cpp"
    "Import/TextureImportPresets_test.cpp"
    "Import/TexturePackingPolicy_test.cpp"
    "Import/TextureSourceAssembly_test.cpp"
)
# TODO(CT-3.8): replaced by the OBJECT library
target_sources(
  Oxygen.Cooker.Import.Unit.Tests
  PRIVATE
    "../Import/Internal/MaterialSlotAllocation.cpp"
)
# The source-validation fixtures construct ufbx values passed into Cooker.
# Match Cooker's private vendor ABI without exposing it to engine consumers.
set_source_files_properties(
  "Import/StaticSourceValidation_test.cpp"
  PROPERTIES
    COMPILE_DEFINITIONS
      UFBX_REAL_IS_FLOAT
)

oxygen_cooker_test(
  Import
  FUNCTIONAL
  DEPS
    oxygen::content
    oxygen::serio
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
  SOURCES
    "Import/AsyncImportService_functional_test.cpp"
    "Import/AsyncImportService_routing_functional_test.cpp"
    "Import/AsyncImporter_functional_test.cpp"
    "Import/BufferContainerImportRequestBuilder_functional_test.cpp"
    "Import/CollisionShapeDescriptorImportRequestBuilder_functional_test.cpp"
    "Import/GeometryDescriptorImportRequestBuilder_functional_test.cpp"
    "Import/ImageDecode_functional_test.cpp"
    "Import/ImportEventLoop_functional_test.cpp"
    "Import/ImportJob_functional_test.cpp"
    "Import/ImportJob_lifetime_functional_test.cpp"
    "Import/ImportManifest_buffer_container_functional_test.cpp"
    "Import/ImportManifest_collision_shape_descriptor_functional_test.cpp"
    "Import/ImportManifest_geometry_descriptor_functional_test.cpp"
    "Import/ImportManifest_input_functional_test.cpp"
    "Import/ImportManifest_material_descriptor_functional_test.cpp"
    "Import/ImportManifest_physics_material_descriptor_functional_test.cpp"
    "Import/ImportManifest_physics_sidecar_functional_test.cpp"
    "Import/ImportManifest_scene_descriptor_functional_test.cpp"
    "Import/ImportManifest_script_sidecar_functional_test.cpp"
    "Import/ImportManifest_texture_descriptor_functional_test.cpp"
    "Import/ImportSession_functional_test.cpp"
    "Import/ImportSourceAnalysis_functional_test.cpp"
    "Import/ImportSourceSnapshot_functional_test.cpp"
    "Import/LooseCookedIndexRegistry_functional_test.cpp"
    "Import/LooseCookedWriter_external_registration_functional_test.cpp"
    "Import/LooseCookedWriter_file_pairs_functional_test.cpp"
    "Import/LooseCookedWriter_identity_functional_test.cpp"
    "Import/LooseCookedWriter_path_validation_functional_test.cpp"
    "Import/LooseCookedWriter_update_functional_test.cpp"
    "Import/MaterialDescriptorImportRequestBuilder_functional_test.cpp"
    "Import/MeshTransformBake_functional_test.cpp"
    "Import/ModelGeometrySource_functional_test.cpp"
    "Import/ModelMaterialSource_functional_test.cpp"
    "Import/ModelTextureSource_functional_test.cpp"
    "Import/PhysicsMaterialDescriptorImportRequestBuilder_functional_test.cpp"
    "Import/ResourceTableRegistry_functional_test.cpp"
    "Import/RetainedModelImport_functional_test.cpp"
    "Import/SceneDescriptorImportRequestBuilder_functional_test.cpp"
    "Import/SceneImportRequestBuilder_functional_test.cpp"
    "Import/SceneSourceInspection_functional_test.cpp"
    "Import/SourceLayoutHash_functional_test.cpp"
    "Import/TextureDescriptorImportRequestBuilder_functional_test.cpp"
    "Import/TextureDescriptorImportRequestBuilder_preparation_functional_test.cpp"
    $<$<BOOL:${WIN32}>:Import/WindowsFileReader_functional_test.cpp>
    $<$<BOOL:${WIN32}>:Import/WindowsFileWriter_functional_test.cpp>
)
# TODO(CT-3.8): replaced by the OBJECT library
target_sources(
  Oxygen.Cooker.Import.Functional.Tests
  PRIVATE
    "../Import/Internal/ImportSourceSnapshot.cpp"
)

oxygen_cooker_test(
  Import.Jobs
  FUNCTIONAL
  DEPS
    oxygen::content
    oxygen::serio
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
  SOURCES
    "Import/Jobs/BufferContainerImportJob_functional_test.cpp"
    "Import/Jobs/CollisionShapeDescriptorImportJob_functional_test.cpp"
    "Import/Jobs/GeometryDescriptorImportJob_buffers_functional_test.cpp"
    "Import/Jobs/GeometryDescriptorImportJob_functional_test.cpp"
    "Import/Jobs/GeometryDescriptorImportJob_procedural_functional_test.cpp"
    "Import/Jobs/MaterialDescriptorImportJob_functional_test.cpp"
    "Import/Jobs/PhysicsMaterialDescriptorImportJob_functional_test.cpp"
    "Import/Jobs/PhysicsSidecarImportJob_functional_test.cpp"
    "Import/Jobs/SceneDescriptorImportJob_functional_test.cpp"
    "Import/Jobs/ScriptAssetImportJob_functional_test.cpp"
    "Import/Jobs/ScriptingSidecarImportJob_binding_functional_test.cpp"
    "Import/Jobs/ScriptingSidecarImportJob_context_functional_test.cpp"
    "Import/Jobs/ScriptingSidecarImportJob_params_functional_test.cpp"
    "Import/Jobs/ScriptingSidecarImportJob_reimport_functional_test.cpp"
    "Import/Jobs/ScriptingSidecarImportJob_validation_functional_test.cpp"
)

oxygen_cooker_test(
  Import.Pipelines
  FUNCTIONAL
  SOURCES
    "Import/Pipelines/BufferPipeline_functional_test.cpp"
    "Import/Pipelines/GeometryPipeline_functional_test.cpp"
    "Import/Pipelines/MaterialPipeline_functional_test.cpp"
    "Import/Pipelines/PhysicsSidecarImportPipeline_functional_test.cpp"
    "Import/Pipelines/PipelineConformance_functional_test.cpp"
    "Import/Pipelines/SceneBuildTestSupport.h"
    "Import/Pipelines/ScenePipeline_functional_test.cpp"
    "Import/Pipelines/ScriptImportPipeline_functional_test.cpp"
    "Import/Pipelines/TexturePipeline_cooker_parity_functional_test.cpp"
    "Import/Pipelines/TexturePipeline_edge_cases_functional_test.cpp"
    "Import/Pipelines/TexturePipeline_functional_test.cpp"
)

oxygen_cooker_test(
  Import.Emitters
  FUNCTIONAL
  SOURCES
    "Import/Emitters/AssetEmitter_functional_test.cpp"
    "Import/Emitters/BufferEmitter_functional_test.cpp"
    $<$<BOOL:${WIN32}>:Import/Emitters/ResourceDescriptorEmitter_functional_test.cpp>
    "Import/Emitters/TextureEmitter_functional_test.cpp"
)

# -- Loose Cooked --------------------------------------------------------------

oxygen_cooker_test(
  Loose
  UNIT
  SOURCES
    "Loose/LooseCookedLayout_test.cpp"
)

oxygen_cooker_test(
  Loose
  FUNCTIONAL
  SOURCES
    "Loose/Inspection_functional_test.cpp"
    "Loose/Validation_functional_test.cpp"
)

# -- Pak -----------------------------------------------------------------------

oxygen_cooker_test(
  Pak
  UNIT
  DEPS
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
  SOURCES
    "Pak/PakBuilder_test.cpp"
    "Pak/PakCatalogIo_test.cpp"
    "Pak/PakTestSupport.h"
    "Pak/PakValidation_test.cpp"
    "Pak/Schemas/PakCatalogSchema_test.cpp"
)

oxygen_cooker_test(
  Pak
  FUNCTIONAL
  DEPS
    oxygen::content
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
  SOURCES
    "Pak/PakBinaryConformance_functional_test.cpp"
    "Pak/PakBuilder_functional_test.cpp"
    "Pak/PakCatalogIo_functional_test.cpp"
    "Pak/PakManifestWriter_functional_test.cpp"
    "Pak/PakPlanBuilder_functional_test.cpp"
    "Pak/PakPlanBuilder_patch_functional_test.cpp"
    "Pak/PakRepack_functional_test.cpp"
    "Pak/PakSceneTestSupport.h"
    "Pak/PakTestSupport.h"
    "Pak/PakWriterTestSupport.h"
    "Pak/PakWriter_functional_test.cpp"
    "Pak/PakWriter_payload_layout_functional_test.cpp"
    "Pak/PakWriter_plan_contract_functional_test.cpp"
)

# -- Scenarios -----------------------------------------------------------------

oxygen_cooker_test(
  Scenarios
  FUNCTIONAL
  TIMEOUT
    900
  DEPS
    oxygen::content
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
  SOURCES
    "Scenarios/FbxModelImport_functional_test.cpp"
    "Scenarios/GltfModelImport_functional_test.cpp"
    "Scenarios/ModelTextureCapture_functional_test.cpp"
    "Scenarios/PhysicsSidecarRecook_functional_test.cpp"
    "Scenarios/StaticModelImport_functional_test.cpp"
    "Scenarios/StaticModelValues_functional_test.cpp"
)
