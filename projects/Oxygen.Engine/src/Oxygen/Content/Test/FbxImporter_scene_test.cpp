//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

#include "FbxImporterTest.h"

namespace {

using oxygen::content::LooseCookedInspection;
using oxygen::content::import::AssetImporter;
using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::data::AssetType;
using oxygen::data::ComponentType;
using oxygen::data::pak::OrthographicCameraRecord;
using oxygen::data::pak::PerspectiveCameraRecord;
using oxygen::data::pak::RenderableRecord;
using oxygen::data::pak::SceneAssetDesc;
using oxygen::data::pak::SceneComponentTableDesc;
using oxygen::serio::FileStream;
using oxygen::serio::Reader;

class FbxImporterSceneTest : public oxygen::content::test::FbxImporterTest { };

//! Test: Real FBX backend emits a SceneAsset compatible with SceneLoader.
/*!\
 Scenario: Writes a minimal ASCII FBX with one mesh node.
 Runs the default AssetImporter() (real FbxImporter backend) requesting
 scene+geometry output.

 Verifies:
 - a scene descriptor is emitted and indexed,
 - the descriptor parses via SceneLoader in parse-only mode.
*/
NOLINT_TEST_F(
  FbxImporterSceneTest, RealBackend_EmitsScene_ParseableBySceneLoader)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_scene");
  const auto source_path = temp_dir / "triangle_scene.fbx";

  // Minimal FBX ASCII with one mesh and one triangle.
  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 2\n"
                          "  ObjectType: \"Model\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Geometry\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Model: 1, \"Model::Triangle\", \"Mesh\" {\n"
                          "  }\n"
                          "  Geometry: 2, \"Geometry::Triangle\", \"Mesh\" {\n"
                          "    Vertices: *9 {\n"
                          "      a: 0,0,0,  1,0,0,  0,1,0\n"
                          "    }\n"
                          "    PolygonVertexIndex: *3 {\n"
                          "      a: 0,1,-3\n"
                          "    }\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
                          "  C: \"OO\", 2, 1\n"
                          "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = ImportContentFlags::kGeometry | ImportContentFlags::kScene;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.geometry_written, 1u);
  EXPECT_EQ(report.scenes_written, 1u);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  const auto assets = inspection.Assets();
  const auto geo_it = std::find_if(assets.begin(), assets.end(),
    [](const LooseCookedInspection::AssetEntry& e) {
      return e.asset_type == static_cast<uint8_t>(AssetType::kGeometry);
    });
  ASSERT_NE(geo_it, assets.end());

  const auto scene_it = std::find_if(assets.begin(), assets.end(),
    [](const LooseCookedInspection::AssetEntry& e) {
      return e.asset_type == static_cast<uint8_t>(AssetType::kScene);
    });
  ASSERT_NE(scene_it, assets.end());

  const auto desc_path
    = report.cooked_root / std::filesystem::path(scene_it->descriptor_relpath);

  FileStream<> stream(desc_path, std::ios::in);
  Reader<FileStream<>> desc_reader(stream);

  // Assert: component table directory has a renderable table linking geometry.
  {
    auto packed = desc_reader.ScopedAlignment(1);

    const auto base_pos_res = desc_reader.Position();
    ASSERT_TRUE(base_pos_res);
    const auto base_pos = *base_pos_res;

    SceneAssetDesc desc {};
    auto desc_result = desc_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SceneAssetDesc, 1>(&desc, 1)));
    ASSERT_TRUE(desc_result);

    EXPECT_GT(desc.nodes.count, 0u);
    EXPECT_GT(desc.scene_strings.size, 0u);

    ASSERT_GT(desc.component_table_count, 0u);
    ASSERT_NE(desc.component_table_directory_offset, 0u);

    auto seek_dir = desc_reader.Seek(
      base_pos + static_cast<size_t>(desc.component_table_directory_offset));
    ASSERT_TRUE(seek_dir);

    std::optional<SceneComponentTableDesc> renderables_entry;
    for (uint32_t i = 0; i < desc.component_table_count; ++i) {
      SceneComponentTableDesc entry {};
      auto entry_result = desc_reader.ReadBlobInto(std::as_writable_bytes(
        std::span<SceneComponentTableDesc, 1>(&entry, 1)));
      ASSERT_TRUE(entry_result);

      if (static_cast<ComponentType>(entry.component_type)
        == ComponentType::kRenderable) {
        renderables_entry = entry;
      }
    }
    ASSERT_TRUE(renderables_entry.has_value());
    EXPECT_EQ(renderables_entry->table.entry_size, sizeof(RenderableRecord));
    ASSERT_GT(renderables_entry->table.count, 0u);

    auto seek_table = desc_reader.Seek(
      base_pos + static_cast<size_t>(renderables_entry->table.offset));
    ASSERT_TRUE(seek_table);

    RenderableRecord renderable {};
    auto record_result = desc_reader.ReadBlobInto(
      std::as_writable_bytes(std::span<RenderableRecord, 1>(&renderable, 1)));
    ASSERT_TRUE(record_result);

    EXPECT_EQ(renderable.geometry_key, geo_it->key);
  }

  FileStream<> stream2(desc_path, std::ios::in);
  Reader<FileStream<>> desc_reader2(stream2);

  oxygen::content::LoaderContext context {
    .current_asset_key = scene_it->key,
    .desc_reader = &desc_reader2,
    .work_offline = true,
    .parse_only = true,
  };

  const auto scene_asset = oxygen::content::loaders::LoadSceneAsset(context);
  EXPECT_NE(scene_asset, nullptr);
}

//! Test: Real FBX backend emits a perspective camera component table.
/*!
 Scenario: Writes a minimal ASCII FBX containing a mesh node and a camera node.
 Runs the default AssetImporter() (real FbxImporter backend) requesting
 scene+geometry output.

 Verifies:
 - a `PCAM` component table exists,
 - the first perspective camera record references a valid node.
*/
NOLINT_TEST_F(FbxImporterSceneTest, RealBackend_EmitsPerspectiveCameraTable)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_scene_camera");
  const auto source_path = temp_dir / "triangle_scene_camera.fbx";

  const char* kFbxAscii
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 4\n"
      "  ObjectType: \"Model\" {\n"
      "    Count: 2\n"
      "  }\n"
      "  ObjectType: \"Geometry\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"NodeAttribute\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Model: 1, \"Model::Triangle\", \"Mesh\" {\n"
      "  }\n"
      "  Geometry: 2, \"Geometry::Triangle\", \"Mesh\" {\n"
      "    Vertices: *9 {\n"
      "      a: 0,0,0,  1,0,0,  0,1,0\n"
      "    }\n"
      "    PolygonVertexIndex: *3 {\n"
      "      a: 0,1,-3\n"
      "    }\n"
      "  }\n"
      "  Model: 3, \"Model::MainCamera\", \"Camera\" {\n"
      "  }\n"
      "  NodeAttribute: 4, \"NodeAttribute::MainCamera\", \"Camera\" {\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OO\", 2, 1\n"
      "  C: \"OO\", 4, 3\n"
      "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = ImportContentFlags::kGeometry | ImportContentFlags::kScene;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.scenes_written, 1u);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  const auto assets = inspection.Assets();
  const auto scene_it = std::find_if(assets.begin(), assets.end(),
    [](const LooseCookedInspection::AssetEntry& e) {
      return e.asset_type == static_cast<uint8_t>(AssetType::kScene);
    });
  ASSERT_NE(scene_it, assets.end());

  const auto desc_path
    = report.cooked_root / std::filesystem::path(scene_it->descriptor_relpath);

  FileStream<> stream(desc_path, std::ios::in);
  Reader<FileStream<>> reader(stream);

  auto packed = reader.ScopedAlignment(1);

  const auto base_pos_res = reader.Position();
  ASSERT_TRUE(base_pos_res);
  const auto base_pos = *base_pos_res;

  SceneAssetDesc desc {};
  auto desc_result = reader.ReadBlobInto(
    std::as_writable_bytes(std::span<SceneAssetDesc, 1>(&desc, 1)));
  ASSERT_TRUE(desc_result);
  ASSERT_GT(desc.component_table_count, 0u);
  ASSERT_NE(desc.component_table_directory_offset, 0u);

  auto seek_dir = reader.Seek(
    base_pos + static_cast<size_t>(desc.component_table_directory_offset));
  ASSERT_TRUE(seek_dir);

  std::optional<SceneComponentTableDesc> pcam_entry;
  for (uint32_t i = 0; i < desc.component_table_count; ++i) {
    SceneComponentTableDesc entry {};
    auto entry_result = reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SceneComponentTableDesc, 1>(&entry, 1)));
    ASSERT_TRUE(entry_result);

    if (static_cast<ComponentType>(entry.component_type)
      == ComponentType::kPerspectiveCamera) {
      pcam_entry = entry;
    }
  }

  ASSERT_TRUE(pcam_entry.has_value());
  ASSERT_GT(pcam_entry->table.count, 0u);
  EXPECT_EQ(pcam_entry->table.entry_size, sizeof(PerspectiveCameraRecord));

  auto seek_table
    = reader.Seek(base_pos + static_cast<size_t>(pcam_entry->table.offset));
  ASSERT_TRUE(seek_table);

  PerspectiveCameraRecord cam {};
  auto cam_result = reader.ReadBlobInto(
    std::as_writable_bytes(std::span<PerspectiveCameraRecord, 1>(&cam, 1)));
  ASSERT_TRUE(cam_result);

  EXPECT_LT(cam.node_index, desc.nodes.count);
}

//! Test: Real FBX backend emits an orthographic camera component table.
/*!
 Scenario: Writes a minimal ASCII FBX containing a mesh node and an orthographic
 camera node.
 Runs the default AssetImporter() (real FbxImporter backend) requesting
 scene+geometry output.

 Verifies:
 - an `OCAM` component table exists,
 - the first orthographic camera record references a valid node.
*/
NOLINT_TEST_F(FbxImporterSceneTest, RealBackend_EmitsOrthographicCameraTable)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_scene_ortho_camera");
  const auto source_path = temp_dir / "triangle_scene_ortho_camera.fbx";

  const char* kFbxAscii
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 4\n"
      "  ObjectType: \"Model\" {\n"
      "    Count: 2\n"
      "  }\n"
      "  ObjectType: \"Geometry\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"NodeAttribute\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Model: 1, \"Model::Triangle\", \"Mesh\" {\n"
      "  }\n"
      "  Geometry: 2, \"Geometry::Triangle\", \"Mesh\" {\n"
      "    Vertices: *9 {\n"
      "      a: 0,0,0,  1,0,0,  0,1,0\n"
      "    }\n"
      "    PolygonVertexIndex: *3 {\n"
      "      a: 0,1,-3\n"
      "    }\n"
      "  }\n"
      "  Model: 3, \"Model::OrthoCamera\", \"Camera\" {\n"
      "  }\n"
      "  NodeAttribute: 4, \"NodeAttribute::OrthoCamera\", \"Camera\" {\n"
      "    Properties70:  {\n"
      "      P: \"ProjectionType\", \"enum\", \"\", \"\",1\n"
      "      P: \"CameraProjectionType\", \"enum\", \"\", \"\",1\n"
      "      P: \"OrthoZoom\", \"double\", \"Number\", \"\",1\n"
      "      P: \"NearPlane\", \"double\", \"Number\", \"\",0.1\n"
      "      P: \"FarPlane\", \"double\", \"Number\", \"\",1000\n"
      "    }\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OO\", 2, 1\n"
      "  C: \"OO\", 4, 3\n"
      "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = ImportContentFlags::kGeometry | ImportContentFlags::kScene;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.scenes_written, 1u);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  const auto assets = inspection.Assets();
  const auto scene_it = std::find_if(assets.begin(), assets.end(),
    [](const LooseCookedInspection::AssetEntry& e) {
      return e.asset_type == static_cast<uint8_t>(AssetType::kScene);
    });
  ASSERT_NE(scene_it, assets.end());

  const auto desc_path
    = report.cooked_root / std::filesystem::path(scene_it->descriptor_relpath);

  FileStream<> stream(desc_path, std::ios::in);
  Reader<FileStream<>> reader(stream);

  auto packed = reader.ScopedAlignment(1);

  const auto base_pos_res = reader.Position();
  ASSERT_TRUE(base_pos_res);
  const auto base_pos = *base_pos_res;

  SceneAssetDesc desc {};
  auto desc_result = reader.ReadBlobInto(
    std::as_writable_bytes(std::span<SceneAssetDesc, 1>(&desc, 1)));
  ASSERT_TRUE(desc_result);
  ASSERT_GT(desc.component_table_count, 0u);
  ASSERT_NE(desc.component_table_directory_offset, 0u);

  auto seek_dir = reader.Seek(
    base_pos + static_cast<size_t>(desc.component_table_directory_offset));
  ASSERT_TRUE(seek_dir);

  std::optional<SceneComponentTableDesc> ocam_entry;
  for (uint32_t i = 0; i < desc.component_table_count; ++i) {
    SceneComponentTableDesc entry {};
    auto entry_result = reader.ReadBlobInto(
      std::as_writable_bytes(std::span<SceneComponentTableDesc, 1>(&entry, 1)));
    ASSERT_TRUE(entry_result);

    if (static_cast<ComponentType>(entry.component_type)
      == ComponentType::kOrthographicCamera) {
      ocam_entry = entry;
    }
  }

  ASSERT_TRUE(ocam_entry.has_value());
  ASSERT_GT(ocam_entry->table.count, 0u);
  EXPECT_EQ(ocam_entry->table.entry_size, sizeof(OrthographicCameraRecord));

  auto seek_table
    = reader.Seek(base_pos + static_cast<size_t>(ocam_entry->table.offset));
  ASSERT_TRUE(seek_table);

  OrthographicCameraRecord cam {};
  auto cam_result = reader.ReadBlobInto(
    std::as_writable_bytes(std::span<OrthographicCameraRecord, 1>(&cam, 1)));
  ASSERT_TRUE(cam_result);

  EXPECT_LT(cam.node_index, desc.nodes.count);
  EXPECT_LT(cam.left, cam.right);
  EXPECT_LT(cam.bottom, cam.top);
  EXPECT_GT(cam.far_plane, cam.near_plane);
}

} // namespace
