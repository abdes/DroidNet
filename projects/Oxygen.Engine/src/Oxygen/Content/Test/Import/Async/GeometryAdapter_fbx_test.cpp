//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <Oxygen/Content/Import/Async/Adapters/FbxGeometryAdapter.h>
#include <Oxygen/Content/Import/Async/Adapters/GeometryAdapterTypes.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::content::import::adapters;
namespace data = oxygen::data;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

[[nodiscard]] auto MakeDefaultMaterialKey() -> data::AssetKey
{
  return data::AssetKey { .guid = {
                            0x31,
                            0x32,
                            0x33,
                            0x34,
                            0x35,
                            0x36,
                            0x37,
                            0x38,
                            0x39,
                            0x3A,
                            0x3B,
                            0x3C,
                            0x3D,
                            0x3E,
                            0x3F,
                            0x40,
                          } };
}

[[nodiscard]] auto MakeRequest(const std::filesystem::path& source_path)
  -> ImportRequest
{
  ImportRequest request;
  request.source_path = source_path;
  return request;
}

[[nodiscard]] auto RepoRootFromFile() -> std::filesystem::path
{
  auto path = std::filesystem::path(__FILE__).parent_path();
  for (int i = 0; i < 6; ++i) {
    path = path.parent_path();
  }
  return path;
}

//! Verify FBX adapter emits TriangulatedMesh work items.
NOLINT_TEST(FbxGeometryAdapterTest, BuildWorkItems_EmitsTriangulatedMesh)
{
  // Arrange
  const auto temp_dir = std::filesystem::temp_directory_path()
    / "oxygen_content_tests" / "fbx_adapter";
  std::filesystem::create_directories(temp_dir);
  const auto source_path = temp_dir / "triangle.fbx";

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

  {
    std::ofstream file(source_path.string(), std::ios::binary);
    file.write(kFbxAscii, static_cast<std::streamsize>(std::strlen(kFbxAscii)));
  }

  GeometryAdapterInput input {
    .source_id_prefix = "fbx",
    .object_path_prefix = "",
    .material_keys = {},
    .default_material_key = MakeDefaultMaterialKey(),
    .request = MakeRequest(source_path),
    .stop_token = {},
  };

  FbxGeometryAdapter adapter;

  // Act
  const auto output = adapter.BuildWorkItems(source_path, input);

  // Assert
  ASSERT_TRUE(output.success);
  ASSERT_TRUE(output.diagnostics.empty());
  ASSERT_FALSE(output.work_items.empty());

  const auto& item = output.work_items.front();
  ASSERT_EQ(item.lods.size(), 1u);
  const auto& tri_mesh = item.lods.front().source;
  EXPECT_FALSE(tri_mesh.streams.positions.empty());
  EXPECT_FALSE(tri_mesh.indices.empty());
  EXPECT_FALSE(tri_mesh.ranges.empty());
}

//! Verify FBX adapter orders triangle ranges by material slot.
NOLINT_TEST(FbxGeometryAdapterTest, BuildWorkItems_OrdersRangesByMaterialSlot)
{
  // Arrange
  const auto temp_dir = std::filesystem::temp_directory_path()
    / "oxygen_content_tests" / "fbx_adapter_material";
  std::filesystem::create_directories(temp_dir);
  const auto source_path = temp_dir / "quad.fbx";

  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 4\n"
                          "  ObjectType: \"Model\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Geometry\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Material\" {\n"
                          "    Count: 2\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Model: 1, \"Model::Quad\", \"Mesh\" {\n"
                          "  }\n"
                          "  Geometry: 2, \"Geometry::Quad\", \"Mesh\" {\n"
                          "    Vertices: *12 {\n"
                          "      a: 0,0,0,  1,0,0,  1,1,0,  0,1,0\n"
                          "    }\n"
                          "    PolygonVertexIndex: *6 {\n"
                          "      a: 0,1,-3,  0,2,-4\n"
                          "    }\n"
                          "    LayerElementMaterial: 0 {\n"
                          "      Version: 101\n"
                          "      Name: \"\"\n"
                          "      MappingInformationType: \"ByPolygon\"\n"
                          "      ReferenceInformationType: \"IndexToDirect\"\n"
                          "      Materials: *2 { a: 1,0 }\n"
                          "    }\n"
                          "    Layer: 0 {\n"
                          "      Version: 100\n"
                          "      LayerElement:  {\n"
                          "        Type: \"LayerElementMaterial\"\n"
                          "        TypedIndex: 0\n"
                          "      }\n"
                          "    }\n"
                          "  }\n"
                          "  Material: 3, \"Material::MatA\", \"\" {\n"
                          "  }\n"
                          "  Material: 4, \"Material::MatB\", \"\" {\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
                          "  C: \"OO\", 2, 1\n"
                          "  C: \"OO\", 3, 1\n"
                          "  C: \"OO\", 4, 1\n"
                          "}\n";

  {
    std::ofstream file(source_path.string(), std::ios::binary);
    file.write(kFbxAscii, static_cast<std::streamsize>(std::strlen(kFbxAscii)));
  }

  GeometryAdapterInput input {
    .source_id_prefix = "fbx",
    .object_path_prefix = "",
    .material_keys = {},
    .default_material_key = MakeDefaultMaterialKey(),
    .request = MakeRequest(source_path),
    .stop_token = {},
  };

  FbxGeometryAdapter adapter;

  // Act
  const auto output = adapter.BuildWorkItems(source_path, input);

  // Assert
  ASSERT_TRUE(output.success);
  ASSERT_TRUE(output.diagnostics.empty());
  ASSERT_EQ(output.work_items.size(), 1u);

  const auto& item = output.work_items.front();
  ASSERT_EQ(item.lods.size(), 1u);
  const auto& tri_mesh = item.lods.front().source;
  ASSERT_EQ(tri_mesh.ranges.size(), 2u);
  EXPECT_EQ(tri_mesh.ranges[0].material_slot, 0u);
  EXPECT_EQ(tri_mesh.ranges[1].material_slot, 1u);
}

//! Verify FBX adapter detects skinned meshes and builds joint buffers.
NOLINT_TEST(FbxGeometryAdapterTest, BuildWorkItems_SkinnedMeshDetected)
{
  // Arrange
  const auto source_path = RepoRootFromFile() / "src" / "Oxygen" / "Content"
    / "Test" / "Import" / "Models" / "Rigged_Humanoid_a.fbx";
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  GeometryAdapterInput input {
    .source_id_prefix = "fbx",
    .object_path_prefix = "",
    .material_keys = {},
    .default_material_key = MakeDefaultMaterialKey(),
    .request = MakeRequest(source_path),
    .stop_token = {},
  };

  FbxGeometryAdapter adapter;

  // Act
  const auto output = adapter.BuildWorkItems(source_path, input);

  // Assert
  ASSERT_TRUE(output.success);

  const MeshLod* skinned_lod = nullptr;
  for (const auto& item : output.work_items) {
    if (item.lods.empty()) {
      continue;
    }
    if (item.lods.front().source.mesh_type == data::MeshType::kSkinned) {
      skinned_lod = &item.lods.front();
      break;
    }
  }

  ASSERT_NE(skinned_lod, nullptr);
  EXPECT_EQ(skinned_lod->lod_name, "LOD0");

  const auto& mesh = skinned_lod->source;
  EXPECT_FALSE(mesh.streams.joint_indices.empty());
  EXPECT_FALSE(mesh.streams.joint_weights.empty());
  EXPECT_EQ(
    mesh.streams.joint_indices.size(), mesh.streams.joint_weights.size());
  EXPECT_EQ(mesh.streams.joint_indices.size(), mesh.streams.positions.size());
  EXPECT_FALSE(mesh.inverse_bind_matrices.empty());
  EXPECT_FALSE(mesh.joint_remap.empty());
  EXPECT_EQ(mesh.inverse_bind_matrices.size(), mesh.joint_remap.size());

  const size_t sample_count
    = (std::min)(mesh.streams.joint_weights.size(), static_cast<size_t>(128));
  bool any_nonzero = false;
  for (size_t i = 0; i < sample_count; ++i) {
    const auto weights = mesh.streams.joint_weights[i];
    const float sum = weights.x + weights.y + weights.z + weights.w;
    if (sum > 0.0F) {
      any_nonzero = true;
      EXPECT_NEAR(sum, 1.0F, 0.05F);
    }
  }
  EXPECT_TRUE(any_nonzero);
}

} // namespace
