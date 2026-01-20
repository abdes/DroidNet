//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Data/AssetKey.h>

// Only available to tests
#include <Oxygen/Content/Import/Adapters/AdapterTypes.h>
#include <Oxygen/Content/Import/Adapters/GltfAdapter.h>

using namespace oxygen::content::import;
using namespace oxygen::content::import::adapters;
namespace data = oxygen::data;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

struct GltfBuffers {
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> tangents;
};

class GeometryWorkItemCollector final : public GeometryWorkItemSink {
public:
  auto Consume(GeometryPipeline::WorkItem item) -> bool override
  {
    work_items.push_back(std::move(item));
    return true;
  }

  std::vector<GeometryPipeline::WorkItem> work_items;
};

[[nodiscard]] auto MakeDefaultMaterialKey() -> data::AssetKey
{
  return data::AssetKey { .guid = {
                            0x21,
                            0x22,
                            0x23,
                            0x24,
                            0x25,
                            0x26,
                            0x27,
                            0x28,
                            0x29,
                            0x2A,
                            0x2B,
                            0x2C,
                            0x2D,
                            0x2E,
                            0x2F,
                            0x30,
                          } };
}

[[nodiscard]] auto MakeRequest(const std::filesystem::path& source_path)
  -> ImportRequest
{
  ImportRequest request;
  request.source_path = source_path;
  return request;
}

class GltfGeometryAdapterTest : public ::testing::Test {
protected:
  GltfBuffers buffers_ {
    .positions = {
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
    },
    .normals = {
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      1.0F,
    },
    .tangents = {
      1.0F,
      0.0F,
      0.0F,
      1.0F,
      1.0F,
      0.0F,
      0.0F,
      1.0F,
      1.0F,
      0.0F,
      0.0F,
      1.0F,
    },
  };
};

//! Verify glTF adapter emits TriangleMesh work items with bitangents.
NOLINT_TEST_F(GltfGeometryAdapterTest, BuildWorkItems_EmitsTriangleMesh)
{
  // Arrange
  const auto temp_dir = std::filesystem::temp_directory_path()
    / "oxygen_content_tests" / "gltf_adapter";
  std::filesystem::create_directories(temp_dir);
  const auto source_path = temp_dir / "mesh.gltf";
  const auto buffer_path = temp_dir / "buffer.bin";

  const auto positions_bytes = buffers_.positions.size() * sizeof(float);
  const auto normals_bytes = buffers_.normals.size() * sizeof(float);
  const auto tangents_bytes = buffers_.tangents.size() * sizeof(float);
  const auto total_bytes = positions_bytes + normals_bytes + tangents_bytes;

  {
    std::ofstream file(buffer_path.string(), std::ios::binary);
    file.write(reinterpret_cast<const char*>(buffers_.positions.data()),
      static_cast<std::streamsize>(positions_bytes));
    file.write(reinterpret_cast<const char*>(buffers_.normals.data()),
      static_cast<std::streamsize>(normals_bytes));
    file.write(reinterpret_cast<const char*>(buffers_.tangents.data()),
      static_cast<std::streamsize>(tangents_bytes));
  }

  std::string json;
  json.append("{\n");
  json.append("  \"asset\": { \"version\": \"2.0\" },\n");
  json.append("  \"buffers\": [ { \"uri\": \"buffer.bin\", \"byteLength\": ");
  json.append(std::to_string(total_bytes));
  json.append(" } ],\n");
  json.append("  \"bufferViews\": [\n");
  json.append("    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": ");
  json.append(std::to_string(positions_bytes));
  json.append(" },\n");
  json.append("    { \"buffer\": 0, \"byteOffset\": ");
  json.append(std::to_string(positions_bytes));
  json.append(", \"byteLength\": ");
  json.append(std::to_string(normals_bytes));
  json.append(" },\n");
  json.append("    { \"buffer\": 0, \"byteOffset\": ");
  json.append(std::to_string(positions_bytes + normals_bytes));
  json.append(", \"byteLength\": ");
  json.append(std::to_string(tangents_bytes));
  json.append(" }\n");
  json.append("  ],\n");
  json.append("  \"accessors\": [\n");
  json.append("    { \"bufferView\": 0, \"componentType\": 5126, ");
  json.append("\"count\": 3, \"type\": \"VEC3\" },\n");
  json.append("    { \"bufferView\": 1, \"componentType\": 5126, ");
  json.append("\"count\": 3, \"type\": \"VEC3\" },\n");
  json.append("    { \"bufferView\": 2, \"componentType\": 5126, ");
  json.append("\"count\": 3, \"type\": \"VEC4\" }\n");
  json.append("  ],\n");
  json.append("  \"meshes\": [ { \"name\": \"Mesh\", \"primitives\": [ ");
  json.append("{ \"mode\": 4, \"attributes\": { \"POSITION\": 0, ");
  json.append("\"NORMAL\": 1, \"TANGENT\": 2 } } ] } ]\n");
  json.append("}\n");

  {
    std::ofstream file(source_path.string(), std::ios::binary);
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
  }

  std::vector<data::AssetKey> material_keys;
  AdapterInput input {
    .source_id_prefix = "glb",
    .object_path_prefix = "",
    .material_keys = std::span<const data::AssetKey>(material_keys),
    .default_material_key = MakeDefaultMaterialKey(),
    .request = MakeRequest(source_path),
    .stop_token = {},
  };

  GltfAdapter adapter;
  GeometryWorkItemCollector collector;

  // Act
  const auto parse_result = adapter.Parse(source_path, input);
  ASSERT_TRUE(parse_result.success);
  const auto output
    = adapter.BuildWorkItems(GeometryWorkTag {}, collector, input);

  // Assert
  ASSERT_TRUE(output.success);
  ASSERT_EQ(output.diagnostics.size(), 1u);
  EXPECT_EQ(output.diagnostics[0].severity, ImportSeverity::kWarning);
  EXPECT_EQ(output.diagnostics[0].code, "gltf.missing_indices");
  ASSERT_EQ(output.emitted, 1u);
  ASSERT_EQ(collector.work_items.size(), 1u);

  const auto& item = collector.work_items.front();
  ASSERT_EQ(item.lods.size(), 1u);
  EXPECT_EQ(item.lods.front().lod_name, "LOD0");

  const auto& triangle_mesh = item.lods.front().source;
  EXPECT_EQ(triangle_mesh.streams.positions.size(), 3u);
  EXPECT_EQ(triangle_mesh.streams.normals.size(), 3u);
  EXPECT_EQ(triangle_mesh.streams.tangents.size(), 3u);
  EXPECT_EQ(triangle_mesh.streams.bitangents.size(), 3u);

  const glm::vec3 expected_bitangent { 0.0F, 1.0F, 0.0F };
  EXPECT_FLOAT_EQ(triangle_mesh.streams.bitangents[0].x, expected_bitangent.x);
  EXPECT_FLOAT_EQ(triangle_mesh.streams.bitangents[0].y, expected_bitangent.y);
  EXPECT_FLOAT_EQ(triangle_mesh.streams.bitangents[0].z, expected_bitangent.z);
}

//! Verify glTF adapter maps material slots by material index.
NOLINT_TEST_F(GltfGeometryAdapterTest, BuildWorkItems_MapsMaterialSlot)
{
  // Arrange
  const auto temp_dir = std::filesystem::temp_directory_path()
    / "oxygen_content_tests" / "gltf_adapter_material";
  std::filesystem::create_directories(temp_dir);
  const auto source_path = temp_dir / "mesh.gltf";
  const auto buffer_path = temp_dir / "buffer.bin";

  const std::vector<float> positions = {
    0.0F,
    0.0F,
    0.0F,
    1.0F,
    0.0F,
    0.0F,
    0.0F,
    1.0F,
    0.0F,
  };
  const std::vector<uint16_t> indices = { 0, 1, 2 };

  const auto positions_bytes = positions.size() * sizeof(float);
  const auto indices_bytes = indices.size() * sizeof(uint16_t);
  const auto total_bytes = positions_bytes + indices_bytes;

  {
    std::ofstream file(buffer_path.string(), std::ios::binary);
    file.write(reinterpret_cast<const char*>(positions.data()),
      static_cast<std::streamsize>(positions_bytes));
    file.write(reinterpret_cast<const char*>(indices.data()),
      static_cast<std::streamsize>(indices_bytes));
  }

  std::string json;
  json.append("{\n");
  json.append("  \"asset\": { \"version\": \"2.0\" },\n");
  json.append("  \"buffers\": [ { \"uri\": \"buffer.bin\", \"byteLength\": ");
  json.append(std::to_string(total_bytes));
  json.append(" } ],\n");
  json.append("  \"bufferViews\": [\n");
  json.append("    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": ");
  json.append(std::to_string(positions_bytes));
  json.append(" },\n");
  json.append("    { \"buffer\": 0, \"byteOffset\": ");
  json.append(std::to_string(positions_bytes));
  json.append(", \"byteLength\": ");
  json.append(std::to_string(indices_bytes));
  json.append(" }\n");
  json.append("  ],\n");
  json.append("  \"accessors\": [\n");
  json.append("    { \"bufferView\": 0, \"componentType\": 5126, ");
  json.append("\"count\": 3, \"type\": \"VEC3\" },\n");
  json.append("    { \"bufferView\": 1, \"componentType\": 5123, ");
  json.append("\"count\": 3, \"type\": \"SCALAR\" }\n");
  json.append("  ],\n");
  json.append("  \"materials\": [ {}, {} ],\n");
  json.append("  \"meshes\": [ { \"name\": \"Mesh\", \"primitives\": [ ");
  json.append("{ \"mode\": 4, \"attributes\": { \"POSITION\": 0 }, ");
  json.append("\"indices\": 1, \"material\": 1 } ] } ]\n");
  json.append("}\n");

  {
    std::ofstream file(source_path.string(), std::ios::binary);
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
  }

  std::vector<data::AssetKey> material_keys;
  AdapterInput input {
    .source_id_prefix = "glb",
    .object_path_prefix = "",
    .material_keys = std::span<const data::AssetKey>(material_keys),
    .default_material_key = MakeDefaultMaterialKey(),
    .request = MakeRequest(source_path),
    .stop_token = {},
  };

  GltfAdapter adapter;
  GeometryWorkItemCollector collector;

  // Act
  const auto parse_result = adapter.Parse(source_path, input);
  ASSERT_TRUE(parse_result.success);
  const auto output
    = adapter.BuildWorkItems(GeometryWorkTag {}, collector, input);

  // Assert
  ASSERT_TRUE(output.success);
  ASSERT_TRUE(output.diagnostics.empty());
  ASSERT_EQ(output.emitted, 1u);
  ASSERT_EQ(collector.work_items.size(), 1u);

  const auto& item = collector.work_items.front();
  ASSERT_EQ(item.lods.size(), 1u);

  const auto& triangle_mesh = item.lods.front().source;
  ASSERT_EQ(triangle_mesh.ranges.size(), 1u);
  EXPECT_EQ(triangle_mesh.ranges[0].material_slot, 1u);
}

} // namespace
