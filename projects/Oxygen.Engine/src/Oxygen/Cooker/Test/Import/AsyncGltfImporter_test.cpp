//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_world.h>

#include "AsyncImporterFullTestBase.h"

namespace {

using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::content::import::test::AsyncImporterFullTestBase;
namespace world = oxygen::data::pak::world;

class AsyncGltfImporterFullTest : public AsyncImporterFullTestBase { };

NOLINT_TEST_F(AsyncGltfImporterFullTest,
  CaseOnlyMaterialNamesKeepDistinctDescriptorsAndMeshBindings)
{
  using oxygen::content::lc::Inspection;
  using oxygen::data::AssetType;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::geometry::MeshDesc;
  using oxygen::data::pak::geometry::MeshViewDesc;
  using oxygen::data::pak::geometry::SubMeshDesc;
  using oxygen::data::pak::render::MaterialAssetDesc;
  using oxygen::serio::FileStream;
  using oxygen::serio::Reader;

  const auto temp_dir = MakeTempDir("async_gltf_case_only_materials");
  const auto source_path = temp_dir / "case_only_materials.gltf";
  {
    std::ofstream source(source_path);
    ASSERT_TRUE(source.is_open());
    source << R"({
      "asset": {"version": "2.0"},
      "buffers": [{
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA",
        "byteLength": 36
      }],
      "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}],
      "accessors": [{
        "bufferView": 0, "componentType": 5126, "count": 3,
        "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]
      }],
      "materials": [
        {"name": "Paint", "pbrMetallicRoughness": {
          "baseColorFactor": [0.9, 0.1, 0.2, 1], "roughnessFactor": 0.2
        }},
        {"name": "paint", "pbrMetallicRoughness": {
          "baseColorFactor": [0.1, 0.8, 0.3, 1], "roughnessFactor": 0.8
        }}
      ],
      "meshes": [{"name": "TwoMaterials", "primitives": [
        {"attributes": {"POSITION": 0}, "material": 0},
        {"attributes": {"POSITION": 0}, "material": 1}
      ]}],
      "nodes": [{"name": "TwoMaterials", "mesh": 0}],
      "scenes": [{"nodes": [0]}],
      "scene": 0
    })";
    source.close();
    ASSERT_TRUE(source.good());
  }

  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir / "Cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .job_name = std::nullopt,
    .orchestration = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  const auto run_result = RunImport(std::move(request));
  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  ASSERT_TRUE(run_result.report.success);

  const auto inspection = LoadInspection(run_result.report.cooked_root);
  std::vector<Inspection::AssetEntry> materials;
  for (const auto& entry : inspection.Assets()) {
    if (entry.asset_type == static_cast<uint8_t>(AssetType::kMaterial)) {
      materials.push_back(entry);
    }
  }
  ASSERT_EQ(materials.size(), 2U);
  EXPECT_NE(materials[0].key, materials[1].key);

  // Check the storage contract even on a case-sensitive test filesystem.
  const auto fold_ascii_case = [](std::string path) {
    std::ranges::transform(path, path.begin(), [](const char value) {
      return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
    });
    return path;
  };
  EXPECT_NE(fold_ascii_case(materials[0].descriptor_relpath),
    fold_ascii_case(materials[1].descriptor_relpath));
  EXPECT_NE(fold_ascii_case(materials[0].virtual_path),
    fold_ascii_case(materials[1].virtual_path));

  ASSERT_EQ(CountAssetsOfType(inspection, AssetType::kGeometry), 1U);
  const auto geometry_entry = FindAssetOfType(inspection, AssetType::kGeometry);
  ASSERT_TRUE(geometry_entry.has_value());
  FileStream<> geometry_stream(run_result.report.cooked_root
      / std::filesystem::path(geometry_entry->descriptor_relpath),
    std::ios::in);
  Reader<FileStream<>> geometry_reader(geometry_stream);
  auto packed = geometry_reader.ScopedAlignment(1);
  GeometryAssetDesc geometry {};
  ASSERT_TRUE(geometry_reader.ReadBlobInto(
    std::as_writable_bytes(std::span<GeometryAssetDesc, 1>(&geometry, 1))));
  ASSERT_EQ(geometry.lod_count, 1U);
  MeshDesc mesh {};
  ASSERT_TRUE(geometry_reader.ReadBlobInto(
    std::as_writable_bytes(std::span<MeshDesc, 1>(&mesh, 1))));
  ASSERT_TRUE(mesh.IsStandard());
  ASSERT_EQ(mesh.submesh_count, 2U);
  ASSERT_EQ(mesh.mesh_view_count, 2U);

  constexpr std::array expected_colors {
    std::array { 0.9F, 0.1F, 0.2F, 1.0F },
    std::array { 0.1F, 0.8F, 0.3F, 1.0F },
  };
  constexpr std::array expected_roughness { 0.2F, 0.8F };
  std::array<oxygen::data::AssetKey, 2> bound_keys {};
  for (size_t slot = 0; slot < bound_keys.size(); ++slot) {
    SCOPED_TRACE(slot);
    SubMeshDesc submesh {};
    ASSERT_TRUE(geometry_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SubMeshDesc, 1>(&submesh, 1))));
    ASSERT_EQ(submesh.mesh_view_count, 1U);
    EXPECT_EQ(std::string(submesh.name), "mat_" + std::to_string(slot));
    MeshViewDesc view {};
    ASSERT_TRUE(geometry_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MeshViewDesc, 1>(&view, 1))));
    EXPECT_EQ(view.index_count, 3U);
    bound_keys[slot] = submesh.material_asset_key;

    const auto material_entry = std::ranges::find(
      materials, submesh.material_asset_key, &Inspection::AssetEntry::key);
    ASSERT_NE(material_entry, materials.end());
    FileStream<> material_stream(run_result.report.cooked_root
        / std::filesystem::path(material_entry->descriptor_relpath),
      std::ios::in);
    Reader<FileStream<>> material_reader(material_stream);
    auto material_packed = material_reader.ScopedAlignment(1);
    MaterialAssetDesc material {};
    ASSERT_TRUE(material_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<MaterialAssetDesc, 1>(&material, 1))));
    for (size_t channel = 0; channel < expected_colors[slot].size();
      ++channel) {
      EXPECT_FLOAT_EQ(
        material.base_color[channel], expected_colors[slot][channel]);
    }
    EXPECT_EQ(
      material.roughness, oxygen::data::Unorm16(expected_roughness[slot]));
  }
  EXPECT_NE(bound_keys[0], bound_keys[1]);
}

//! Full async import validates supported glTF content is emitted.
/*!
 Uses the async glTF import job to process Tabuleiro.glb and validates the
 cooked outputs contain the expected content types.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, AsyncBackendImportsFullTabuleiroScene)
{
  // Arrange
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "Tabuleiro.glb";
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_gltf_tabuleiro");
  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .job_name = std::nullopt,
    .orchestration = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  // Act
  const auto run_result = RunImport(std::move(request));

  // Assert
  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);

  const ExpectedSceneOutputs expected {
    .materials = 3U,
    .geometry = 5U,
    .scenes = 1U,
    .nodes_min = std::nullopt,
    .texture_files = 0U,
  };
  ValidateSceneOutputs(run_result.report, expected);

  const auto scene = LoadSceneReadback(run_result.report);
  ASSERT_FALSE(scene.renderables.empty());
  for (const auto& renderable : scene.renderables) {
    ASSERT_LT(renderable.node_index, scene.nodes.size());
    const auto node_flags = scene.nodes[renderable.node_index].node_flags;
    EXPECT_NE(node_flags & world::kSceneNodeFlag_CastsShadows, 0U);
    EXPECT_NE(node_flags & world::kSceneNodeFlag_ReceivesShadows, 0U);
  }

  GTEST_LOG_(INFO) << "Cooked root: " << run_result.report.cooked_root.string();
}

NOLINT_TEST_F(
  AsyncGltfImporterFullTest, AsyncBackendImportsLightExtrasAsSceneSemantics)
{
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "light_overrides.gltf";
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_gltf_light_overrides");
  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .job_name = std::nullopt,
    .orchestration = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  const auto run_result = RunImport(std::move(request));

  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);

  const auto scene = LoadSceneReadback(run_result.report);
  EXPECT_TRUE(scene.renderables.empty());
  EXPECT_TRUE(scene.directional_lights.empty());
  ASSERT_EQ(scene.point_lights.size(), 1U);
  EXPECT_TRUE(scene.spot_lights.empty());

  const auto& light = scene.point_lights.front();
  EXPECT_EQ(light.common.affects_world, 0U);
  EXPECT_EQ(light.common.casts_shadows, 0U);
}

//! Async import succeeds for glTF Sponza when asset is available.
/*!
 Validates the async glTF importer can handle the external-texture Sponza
 dataset when the source file is present on disk.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, DISABLEDAsyncBackendImportsSponza)
{
  // Arrange
  const auto source_path = std::filesystem::path(
    "F:\\projects\\main_sponza\\NewSponza_Main_glTF_003.gltf");
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_gltf_sponza");
  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .job_name = std::nullopt,
    .orchestration = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  const auto run_result = RunImport(std::move(request));

  // Assert
  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);
  GTEST_LOG_(INFO) << "Cooked root: " << run_result.report.cooked_root.string();
}

} // namespace
