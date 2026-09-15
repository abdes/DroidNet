//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Cooker/Import/Internal/MeshTransformBake.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Data/Vertex.h>

#include "AsyncImporterFullTestBase.h"
#include <Oxygen/Testing/GTest.h>

namespace {
using namespace oxygen::content::import;
namespace data = oxygen::data;

struct CookedImport {
  std::vector<MeshBuildPipeline::CookedGeometryPayload> geometries;
  std::vector<ImportDiagnostic> diagnostics;
  SceneBuild scene;
};

auto ReadFixture(const std::string& name) -> std::string
{
  std::ifstream input(
    std::filesystem::path(__FILE__).parent_path() / "Models" / name);
  return { std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>() };
}

auto GltfFixture() -> nlohmann::json
{
  return nlohmann::json::parse(ReadFixture("static_scalar_triangle.gltf"));
}

auto LocalMatrix(const data::pak::world::NodeRecord& node) -> glm::mat4
{
  return glm::translate(glm::mat4(1.0F),
           glm::vec3(
             node.translation[0], node.translation[1], node.translation[2]))
    * glm::mat4_cast(glm::quat(
      node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]))
    * glm::scale(
      glm::mat4(1.0F), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
}

auto WorldMatrix(const SceneBuild& scene, size_t index) -> glm::mat4
{
  glm::mat4 world(1.0F);
  for (size_t depth = 0; depth < scene.nodes.size(); ++depth) {
    const auto& node = scene.nodes[index];
    world = LocalMatrix(node) * world;
    if (node.parent_index == index) {
      return world;
    }
    index = node.parent_index;
  }
  ADD_FAILURE() << "Cyclic hierarchy";
  return world;
}

template <typename T>
auto ReadPayload(const CookedBufferPayload& buffer) -> std::vector<T>
{
  std::vector<T> values(buffer.data.size() / sizeof(T));
  std::memcpy(values.data(), buffer.data.data(), buffer.data.size());
  return values;
}

auto GeometryFor(const CookedImport& imported, const data::AssetKey& key)
  -> const MeshBuildPipeline::CookedGeometryPayload&
{
  const auto found = std::ranges::find(imported.geometries, key,
    &MeshBuildPipeline::CookedGeometryPayload::geometry_key);
  EXPECT_NE(found, imported.geometries.end());
  return *found;
}

auto ExpectVector(const glm::vec3& actual, const glm::vec3& expected) -> void
{
  EXPECT_NEAR(actual.x, expected.x, 0.0001F);
  EXPECT_NEAR(actual.y, expected.y, 0.0001F);
  EXPECT_NEAR(actual.z, expected.z, 0.0001F);
}

class MeshTransformBakeTest : public test::AsyncImporterFullTestBase {
protected:
  auto Cook(const std::string& source, const std::string& extension,
    const bool bake) -> CookedImport
  {
    CookedImport result;
    const auto test_name = std::string(
      testing::UnitTest::GetInstance()->current_test_info()->name());
    const auto root = MakeTempDir("mesh_bake_" + test_name + "_" + extension
      + (bake ? "_baked" : "_retained"));
    ImportRequest request;
    request.source_path = root / ("fixture." + extension);
    request.cooked_root = root / "cooked";
    request.options.coordinate.bake_transforms_into_meshes = bake;
    request.options.naming_strategy = std::make_shared<NoOpNamingStrategy>();
    {
      std::ofstream output(request.source_path, std::ios::binary);
      output << source;
    }
    AsyncImportService service(AsyncImportService::Config {
      .thread_pool_size = 2,
      .max_in_flight_jobs = 1,
    });
    ImportReport report;
    std::latch finished(1);
    const auto job = service.SubmitImport(
      std::move(request), [&](ImportJobId, const ImportReport& completed) {
        report = completed;
        finished.count_down();
      });
    EXPECT_TRUE(job.has_value());
    if (!job) {
      return result;
    }
    finished.wait();
    service.Stop();
    result.diagnostics = report.diagnostics;
    EXPECT_TRUE(report.success);
    if (!report.success) {
      for (const auto& diagnostic : report.diagnostics) {
        ADD_FAILURE() << diagnostic.code << ": " << diagnostic.message;
      }
      return result;
    }
    const auto scene = LoadSceneReadback(report);
    result.scene.nodes = scene.nodes;
    result.scene.renderables = scene.renderables;
    const auto inspection = LoadInspection(report.cooked_root);
    const auto read_bytes = [](const std::filesystem::path& path) {
      std::ifstream stream(path, std::ios::binary | std::ios::ate);
      std::vector<std::byte> bytes(static_cast<size_t>(stream.tellg()));
      stream.seekg(0);
      stream.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
      return bytes;
    };
    const auto table = read_bytes(
      report.cooked_root / LooseCookedLayout {}.BuffersTableRelPath());
    const auto buffers = read_bytes(
      report.cooked_root / LooseCookedLayout {}.BuffersDataRelPath());
    const auto read_buffer = [&](const data::pak::core::ResourceIndexT index) {
      data::pak::core::BufferResourceDesc descriptor;
      std::memcpy(&descriptor, table.data() + index.get() * sizeof(descriptor),
        sizeof(descriptor));
      CookedBufferPayload payload;
      payload.data.assign(buffers.begin() + descriptor.data_offset,
        buffers.begin() + descriptor.data_offset + descriptor.size_bytes);
      return payload;
    };
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type
        != static_cast<uint8_t>(data::AssetType::kGeometry)) {
        continue;
      }
      MeshBuildPipeline::CookedGeometryPayload geometry;
      geometry.geometry_key = asset.key;
      geometry.descriptor_bytes
        = read_bytes(report.cooked_root / asset.descriptor_relpath);
      data::pak::geometry::MeshDesc descriptor;
      std::memcpy(&descriptor,
        geometry.descriptor_bytes.data()
          + sizeof(data::pak::geometry::GeometryAssetDesc),
        sizeof(descriptor));
      MeshBuildPipeline::CookedMeshPayload mesh;
      mesh.vertex_buffer = read_buffer(descriptor.info.standard.vertex_buffer);
      mesh.index_buffer = read_buffer(descriptor.info.standard.index_buffer);
      std::copy_n(
        descriptor.info.standard.bounding_box_min, 3, mesh.bounds.min.begin());
      std::copy_n(
        descriptor.info.standard.bounding_box_max, 3, mesh.bounds.max.begin());
      geometry.lods.push_back(std::move(mesh));
      result.geometries.push_back(std::move(geometry));
    }
    return result;
  }

  auto ExpectEquivalentWorldGeometry(
    const CookedImport& retained, const CookedImport& baked) -> void
  {
    ASSERT_EQ(retained.scene.nodes.size(), baked.scene.nodes.size());
    ASSERT_EQ(
      retained.scene.renderables.size(), baked.scene.renderables.size());
    for (size_t draw = 0; draw < retained.scene.renderables.size(); ++draw) {
      const auto& original_draw = retained.scene.renderables[draw];
      const auto& baked_draw = baked.scene.renderables[draw];
      ASSERT_EQ(original_draw.node_index, baked_draw.node_index);
      const auto original_world
        = WorldMatrix(retained.scene, original_draw.node_index);
      const auto baked_world = WorldMatrix(baked.scene, baked_draw.node_index);
      const auto& original
        = GeometryFor(retained, original_draw.geometry_key).lods.front();
      const auto& cooked
        = GeometryFor(baked, baked_draw.geometry_key).lods.front();
      const auto original_vertices
        = ReadPayload<data::Vertex>(original.vertex_buffer);
      const auto vertices = ReadPayload<data::Vertex>(cooked.vertex_buffer);
      ASSERT_EQ(original_vertices.size(), vertices.size());
      glm::vec3 minimum(std::numeric_limits<float>::max());
      glm::vec3 maximum(std::numeric_limits<float>::lowest());
      for (size_t index = 0; index < vertices.size(); ++index) {
        ExpectVector(
          glm::vec3(baked_world * glm::vec4(vertices[index].position, 1)),
          glm::vec3(
            original_world * glm::vec4(original_vertices[index].position, 1)));
        ExpectVector(
          glm::normalize(glm::transpose(glm::inverse(glm::mat3(baked_world)))
            * vertices[index].normal),
          glm::normalize(glm::transpose(glm::inverse(glm::mat3(original_world)))
            * original_vertices[index].normal));
        minimum = glm::min(minimum, vertices[index].position);
        maximum = glm::max(maximum, vertices[index].position);
      }
      ExpectVector(glm::vec3(cooked.bounds.min[0], cooked.bounds.min[1],
                     cooked.bounds.min[2]),
        minimum);
      ExpectVector(glm::vec3(cooked.bounds.max[0], cooked.bounds.max[1],
                     cooked.bounds.max[2]),
        maximum);
      const auto indices = ReadPayload<uint32_t>(cooked.index_buffer);
      ASSERT_EQ(indices.size(), 3U);
      const auto face = glm::normalize(glm::cross(
        vertices[indices[1]].position - vertices[indices[0]].position,
        vertices[indices[2]].position - vertices[indices[0]].position));
      EXPECT_GT(glm::dot(face, vertices[indices[0]].normal), 0.999F);
    }
  }
};

NOLINT_TEST_F(
  MeshTransformBakeTest, GltfBakesPositiveAndMirroredLocalTransformsOnce)
{
  for (const float scale_x : { 2.0F, -2.0F }) {
    auto document = GltfFixture();
    document["nodes"][0]["scale"][0] = scale_x;
    const auto source = document.dump();
    const auto retained = Cook(source, "gltf", false);
    const auto baked = Cook(source, "gltf", true);
    ASSERT_EQ(retained.scene.renderables.size(), 1U);
    EXPECT_EQ(
      glm::determinant(glm::mat3(LocalMatrix(
        retained.scene.nodes[retained.scene.renderables[0].node_index])))
        < 0,
      scale_x < 0);
    EXPECT_EQ(
      ReadPayload<uint32_t>(retained.geometries[0].lods[0].index_buffer),
      (std::vector<uint32_t> { 0, 1, 2 }));
    ASSERT_EQ(baked.geometries.size(), 1U);
    ASSERT_EQ(baked.scene.renderables.size(), 1U);
    EXPECT_EQ(
      LocalMatrix(baked.scene.nodes[baked.scene.renderables[0].node_index]),
      glm::mat4(1.0F));
    ExpectEquivalentWorldGeometry(retained, baked);
  }
}

NOLINT_TEST_F(
  MeshTransformBakeTest, FbxBakesPositiveAndMirroredLocalTransformsOnce)
{
  for (const bool reflected : { false, true }) {
    auto source = ReadFixture("static_scalar_triangle.fbx");
    if (reflected) {
      const auto position = source.find("\"A\",2,3,4");
      ASSERT_NE(position, std::string::npos);
      source.replace(
        position, std::string("\"A\",2,3,4").size(), "\"A\",-2,3,4");
    }
    const auto retained = Cook(source, "fbx", false);
    const auto baked = Cook(source, "fbx", true);
    ASSERT_EQ(retained.scene.renderables.size(), 1U);
    EXPECT_EQ(
      glm::determinant(glm::mat3(LocalMatrix(
        retained.scene.nodes[retained.scene.renderables[0].node_index])))
        < 0,
      reflected);
    ASSERT_EQ(baked.geometries.size(), 1U);
    ASSERT_EQ(baked.scene.renderables.size(), 1U);
    EXPECT_EQ(
      LocalMatrix(baked.scene.nodes[baked.scene.renderables[0].node_index]),
      glm::mat4(1.0F));
    ExpectEquivalentWorldGeometry(retained, baked);
  }
}

NOLINT_TEST_F(
  MeshTransformBakeTest, GltfSharesEqualVariantsAndPreservesAttachmentNode)
{
  auto document = GltfFixture();
  const auto source_node = document["nodes"][0];
  document["nodes"].push_back(source_node);
  document["nodes"][1]["name"] = "EqualInstance";
  document["nodes"].push_back(source_node);
  document["nodes"][2]["name"] = "ReflectedInstance";
  document["nodes"][2]["scale"][0] = -2;
  document["nodes"].push_back(source_node);
  document["nodes"][3]["name"] = "AttachmentParent";
  document["nodes"][3]["children"] = { 4 };
  document["nodes"].push_back(
    { { "name", "Attachment" }, { "translation", { 0, 1, 0 } } });
  document["scenes"][0]["nodes"] = { 0, 1, 2, 3 };
  const auto source = document.dump();
  const auto retained = Cook(source, "gltf", false);
  const auto baked = Cook(source, "gltf", true);
  ASSERT_EQ(retained.geometries.size(), 1U);
  ASSERT_EQ(baked.geometries.size(), 3U);
  ASSERT_EQ(baked.scene.renderables.size(), 4U);
  EXPECT_EQ(baked.scene.renderables[0].geometry_key,
    baked.scene.renderables[1].geometry_key);
  EXPECT_NE(baked.scene.renderables[0].geometry_key,
    baked.scene.renderables[2].geometry_key);
  EXPECT_NE(baked.scene.renderables[0].geometry_key,
    baked.scene.renderables[3].geometry_key);
  EXPECT_EQ(baked.scene.nodes[4].parent_index, 3U);
  EXPECT_EQ(
    LocalMatrix(baked.scene.nodes[3]), LocalMatrix(retained.scene.nodes[3]));
  EXPECT_EQ(WorldMatrix(baked.scene, 4), WorldMatrix(retained.scene, 4));
  EXPECT_TRUE(std::ranges::any_of(
    baked.diagnostics, [](const ImportDiagnostic& diagnostic) {
      return diagnostic.code == "mesh.transform_bake_retained";
    }));
  ExpectEquivalentWorldGeometry(retained, baked);
}

NOLINT_TEST_F(MeshTransformBakeTest, GltfRetainsAnimatedAndMorphNodeTransforms)
{
  for (const bool animated : { false, true }) {
    auto document = GltfFixture();
    if (animated) {
      document["buffers"].push_back({ { "byteLength", 32 },
        { "uri",
          "data:application/"
          "octet-stream;base64,AAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/"
          "AAAAAAAAAAA=" } });
      document["bufferViews"].push_back(
        { { "buffer", 1 }, { "byteOffset", 0 }, { "byteLength", 8 } });
      document["bufferViews"].push_back(
        { { "buffer", 1 }, { "byteOffset", 8 }, { "byteLength", 24 } });
      document["accessors"].push_back(
        { { "bufferView", 1 }, { "componentType", 5126 }, { "count", 2 },
          { "type", "SCALAR" }, { "min", { 0 } }, { "max", { 1 } } });
      document["accessors"].push_back({ { "bufferView", 2 },
        { "componentType", 5126 }, { "count", 2 }, { "type", "VEC3" } });
      nlohmann::json animation;
      animation["samplers"][0]["input"] = 1;
      animation["samplers"][0]["output"] = 2;
      animation["channels"][0]["sampler"] = 0;
      animation["channels"][0]["target"]["node"] = 0;
      animation["channels"][0]["target"]["path"] = "translation";
      document["animations"][0] = std::move(animation);
    } else {
      document["meshes"][0]["primitives"][0]["targets"]
        = { { { "POSITION", 0 } } };
      document["meshes"][0]["weights"] = { 0 };
    }
    const auto source = document.dump();
    const auto retained = Cook(source, "gltf", false);
    const auto baked = Cook(source, "gltf", true);
    ASSERT_EQ(baked.scene.renderables.size(), 1U);
    const auto node = baked.scene.renderables[0].node_index;
    EXPECT_EQ(LocalMatrix(baked.scene.nodes[node]),
      LocalMatrix(retained.scene.nodes[node]));
    EXPECT_EQ(baked.scene.renderables[0].geometry_key,
      retained.scene.renderables[0].geometry_key);
    EXPECT_TRUE(std::ranges::any_of(
      baked.diagnostics, [](const ImportDiagnostic& diagnostic) {
        return diagnostic.code == "mesh.transform_bake_retained";
      }));
    ExpectEquivalentWorldGeometry(retained, baked);
  }
}

NOLINT_TEST_F(
  MeshTransformBakeTest, ParentAndChildReflectionsKeepOnlyParentAtRuntime)
{
  auto document = GltfFixture();
  document["nodes"][0]["scale"][0] = -2;
  document["nodes"].push_back({ { "name", "ReflectedParent" },
    { "scale", { -1, 2, 1 } }, { "children", { 0 } } });
  document["scenes"][0]["nodes"] = { 1 };
  const auto source = document.dump();
  const auto retained = Cook(source, "gltf", false);
  const auto baked = Cook(source, "gltf", true);
  ASSERT_EQ(baked.scene.renderables.size(), 1U);
  const auto index = baked.scene.renderables[0].node_index;
  EXPECT_GT(glm::determinant(glm::mat3(WorldMatrix(retained.scene, index))), 0);
  EXPECT_LT(glm::determinant(glm::mat3(WorldMatrix(baked.scene, index))), 0);
  EXPECT_EQ(LocalMatrix(baked.scene.nodes[index]), glm::mat4(1));
  EXPECT_NE(baked.scene.nodes[index].parent_index, index);
  ExpectEquivalentWorldGeometry(retained, baked);
}

NOLINT_TEST_F(MeshTransformBakeTest, FbxMapsSharedVariantsAndPerNodeMaterials)
{
  auto source = ReadFixture("static_scalar_triangle.fbx");
  const auto objects_end = source.find("\n}\nConnections:");
  ASSERT_NE(objects_end, std::string::npos);
  source.insert(objects_end, R"(
    Material: 1003, "Material::First", "" { }
    Material: 1006, "Material::Second", "" { }
    Model: 1004, "Model::EqualInstance", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",1,2,3
            P: "Lcl Scaling", "Lcl Scaling", "", "A",2,3,4
        }
    }
    Model: 1005, "Model::OtherMaterial", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",1,2,3
            P: "Lcl Scaling", "Lcl Scaling", "", "A",2,3,4
        }
    }
)");
  source.insert(source.rfind('}'), R"(
    C: "OO",1003,1002
    C: "OO",1001,1004
    C: "OO",1004,0
    C: "OO",1003,1004
    C: "OO",1001,1005
    C: "OO",1005,0
    C: "OO",1006,1005
)");
  const auto retained = Cook(source, "fbx", false);
  const auto baked = Cook(source, "fbx", true);
  ASSERT_EQ(baked.scene.renderables.size(), 3U);
  ASSERT_EQ(baked.geometries.size(), 2U);
  EXPECT_EQ(baked.scene.renderables[0].geometry_key,
    baked.scene.renderables[1].geometry_key);
  EXPECT_NE(baked.scene.renderables[0].geometry_key,
    baked.scene.renderables[2].geometry_key);
  const auto& first
    = GeometryFor(baked, baked.scene.renderables[0].geometry_key);
  const auto& second
    = GeometryFor(baked, baked.scene.renderables[2].geometry_key);
  const auto material_key
    = [](const MeshBuildPipeline::CookedGeometryPayload& geometry) {
        data::pak::geometry::SubMeshDesc submesh;
        std::memcpy(&submesh,
          geometry.descriptor_bytes.data()
            + sizeof(data::pak::geometry::GeometryAssetDesc)
            + sizeof(data::pak::geometry::MeshDesc),
          sizeof(submesh));
        return submesh.material_asset_key;
      };
  EXPECT_NE(material_key(first), material_key(second));
  ExpectEquivalentWorldGeometry(retained, baked);
}

NOLINT_TEST(MeshTransformBakePlanTest,
  RetainsUnsafeTransformsAndSeparatesMaterialBindings)
{
  const auto transform = glm::scale(glm::mat4(1), glm::vec3(-2, 3, 4));
  std::array<internal::MeshBakeNode, 5> nodes;
  for (auto& node : nodes) {
    node.mesh_index = 0;
    node.local_transform = transform;
  }
  nodes[1].material_binding = 1;
  nodes[2].retain_reason = "skinning";
  nodes[3].retain_reason = "animation";
  nodes[4].local_transform = glm::scale(glm::mat4(1), glm::vec3(0, 1, 1));
  const std::array<uint8_t, 1> enabled { 1 };
  const auto plan = internal::BuildMeshBakePlan(nodes, enabled, true);
  ASSERT_EQ(plan.variants.size(), 3U);
  EXPECT_NE(plan.node_variant[0], plan.node_variant[1]);
  EXPECT_EQ(plan.node_variant[2], plan.node_variant[3]);
  EXPECT_EQ(plan.node_variant[3], plan.node_variant[4]);
  EXPECT_FALSE(plan.variants[plan.node_variant[2]].transform.has_value());
  EXPECT_EQ(plan.retained_reasons[2], "skinning");
  EXPECT_EQ(plan.retained_reasons[3], "animation");
  EXPECT_EQ(plan.retained_reasons[4], "non-invertible transform");
}
} // namespace
