//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <fstream>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <tuple>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>

#include "AsyncImporterFullTestBase.h"

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {

using oxygen::content::AssetLoader;
using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::SceneAsset;
using namespace oxygen::data::pak::world;

auto WorldTransform(const SceneAsset& scene, SceneNodeIndexT index) -> glm::mat4
{
  auto world = glm::mat4(1.0F);
  for (size_t depth = 0; depth < scene.GetNodes().size(); ++depth) {
    const auto& node = scene.GetNode(index);
    const auto rotation = glm::quat(
      node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
    const auto local = glm::translate(glm::mat4(1.0F),
                         glm::vec3(node.translation[0], node.translation[1],
                           node.translation[2]))
      * glm::mat4_cast(rotation)
      * glm::scale(glm::mat4(1.0F),
        glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    world = local * world;
    if (node.parent_index == index) {
      return world;
    }
    index = node.parent_index;
  }
  ADD_FAILURE() << "Loaded node hierarchy contains a cycle";
  return world;
}

auto ExpectVector(const glm::vec3 actual, const glm::vec3 expected) -> void
{
  EXPECT_NEAR(actual.x, expected.x, 0.0001F);
  EXPECT_NEAR(actual.y, expected.y, 0.0001F);
  EXPECT_NEAR(actual.z, expected.z, 0.0001F);
}

auto VerifyLoadedTriangle(const SceneAsset& scene,
  const GeometryAsset& geometry, const float unit_scale,
  const float front_sign = 1.0F, const bool hierarchy = false) -> void
{
  const auto renderables = scene.GetComponents<RenderableRecord>();
  ASSERT_EQ(renderables.size(), 1U);
  ASSERT_EQ(geometry.Meshes().size(), 1U);
  const auto& mesh = *geometry.Meshes().front();
  ASSERT_EQ(mesh.VertexCount(), 3U);
  ASSERT_EQ(mesh.IndexCount(), 3U);
  const auto world = WorldTransform(scene, renderables.front().node_index);
  const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3(world)));
  const auto expected = hierarchy
    ? std::array { glm::vec3(3, -3 * front_sign, 1) * unit_scale,
        glm::vec3(3, -3 * front_sign, 3) * unit_scale,
        glm::vec3(0, -3 * front_sign, 1) * unit_scale }
    : std::array { glm::vec3(1, -3 * front_sign, 2) * unit_scale,
        glm::vec3(3, -3 * front_sign, 2) * unit_scale,
        glm::vec3(1, -3 * front_sign, 5) * unit_scale };
  if (hierarchy) {
    const auto node = renderables.front().node_index;
    EXPECT_NE(scene.GetNode(node).parent_index, node);
  }
  auto minimum = glm::vec3(std::numeric_limits<float>::max());
  auto maximum = glm::vec3(std::numeric_limits<float>::lowest());
  std::vector<glm::vec3> points;
  for (const auto& vertex : mesh.Vertices()) {
    minimum = glm::min(minimum, vertex.position);
    maximum = glm::max(maximum, vertex.position);
    points.emplace_back(world * glm::vec4(vertex.position, 1));
    ExpectVector(
      glm::normalize(normal_matrix * vertex.normal), { 0, -front_sign, 0 });
  }
  for (const auto point : expected) {
    EXPECT_TRUE(std::ranges::any_of(points, [&](const auto actual) {
      return glm::distance(actual, point) < 0.0001F;
    }));
  }
  ExpectVector(geometry.BoundingBoxMin(), minimum);
  ExpectVector(geometry.BoundingBoxMax(), maximum);
  ExpectVector(mesh.BoundingBoxMin(), minimum);
  ExpectVector(mesh.BoundingBoxMax(), maximum);
  std::vector<uint32_t> indices;
  for (const auto index : mesh.IndexBuffer().Widened()) {
    indices.push_back(index);
  }
  const auto face_normal
    = glm::normalize(glm::cross(points[indices[1]] - points[indices[0]],
      points[indices[2]] - points[indices[0]]));
  ExpectVector(face_normal, { 0, -front_sign, 0 });
}

auto VerifyLoadedGltfValues(const SceneAsset& scene,
  const GeometryAsset& geometry, const MaterialAsset& material,
  const float unit_scale, const bool hierarchy) -> void
{
  VerifyLoadedTriangle(scene, geometry, unit_scale, 1.0F, hierarchy);
  const auto cameras = scene.GetComponents<PerspectiveCameraRecord>();
  ASSERT_EQ(cameras.size(), 1U);
  EXPECT_FLOAT_EQ(cameras[0].fov_y, 1.0F);
  EXPECT_NEAR(cameras[0].aspect_ratio, 16.0F / 9.0F, 0.0001F);
  EXPECT_NEAR(cameras[0].near_plane, 0.2F * unit_scale, 0.0001F);
  EXPECT_NEAR(cameras[0].far_plane, 250.0F * unit_scale, 0.0001F);
  ExpectVector(glm::vec3(WorldTransform(scene, cameras[0].node_index)[3]),
    glm::vec3(0, -5, 2) * unit_scale);
  const auto lights = scene.GetComponents<DirectionalLightRecord>();
  ASSERT_EQ(lights.size(), 1U);
  EXPECT_FLOAT_EQ(lights[0].intensity_lux, 5000.0F);
  ExpectVector({ lights[0].common.color_rgb[0], lights[0].common.color_rgb[1],
                 lights[0].common.color_rgb[2] },
    { 0.8F, 0.7F, 0.6F });
  const auto color = material.GetBaseColor();
  ExpectVector({ color[0], color[1], color[2] }, { 0.8F, 0.2F, 0.1F });
  EXPECT_FLOAT_EQ(color[3], 1.0F);
  EXPECT_NEAR(material.GetMetalness(), 0.25F, 0.001F);
  EXPECT_NEAR(material.GetRoughness(), 0.6F, 0.001F);
}

auto WriteU32(std::ostream& output, uint32_t value) -> void
{
  for (auto byte = 0; byte < 4; ++byte) {
    output.put(static_cast<char>(value & 0xffU));
    value >>= 8U;
  }
}

auto WritePositionBuffer(std::ostream& output) -> void
{
  for (const auto value :
    std::array { 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F }) {
    WriteU32(output, std::bit_cast<uint32_t>(value));
  }
}

auto WriteGltfSource(const std::filesystem::path& path, nlohmann::json document)
  -> void
{
  const auto binary = path.extension() == ".glb";
  if (binary) {
    document["buffers"][0].erase("uri");
  } else {
    document["buffers"][0]["uri"] = "positions.bin";
    std::ofstream positions(
      path.parent_path() / "positions.bin", std::ios::binary);
    WritePositionBuffer(positions);
  }
  auto text = document.dump(2);
  std::ofstream output(path, std::ios::binary);
  if (binary) {
    while (text.size() % 4 != 0) {
      text.push_back(' ');
    }
    WriteU32(output, 0x46546c67U);
    WriteU32(output, 2);
    WriteU32(output, static_cast<uint32_t>(20 + text.size() + 8 + 36));
    WriteU32(output, static_cast<uint32_t>(text.size()));
    WriteU32(output, 0x4e4f534aU);
  }
  output << text;
  if (binary) {
    WriteU32(output, 36);
    WriteU32(output, 0x004e4942U);
    WritePositionBuffer(output);
  }
}

class StaticScalarLoadedValuesTest
  : public oxygen::content::import::test::AsyncImporterFullTestBase {
protected:
  auto ImportAndVerifyCleanCopy(oxygen::content::import::ImportRequest request,
    const std::function<void(const SceneAsset&, const GeometryAsset&,
      const MaterialAsset&)>& verify) -> void
  {
    ASSERT_TRUE(request.cooked_root.has_value());
    auto copied = request;
    const auto root = MakeTempDir("clean_"
      + request.cooked_root->parent_path().filename().string() + "_"
      + request.cooked_root->filename().string());
    copied.source_path = root / request.source_path.filename();
    copied.cooked_root = root / "cooked";
    for (const auto& entry :
      std::filesystem::directory_iterator(request.source_path.parent_path())) {
      if (entry.is_regular_file()) {
        std::filesystem::copy_file(
          entry.path(), root / entry.path().filename());
      }
    }
    EXPECT_FALSE(std::filesystem::exists(*copied.cooked_root));
    const auto first = RunImport(std::move(request));
    ASSERT_TRUE(first.report.success);
    const auto second = RunImport(std::move(copied));
    ASSERT_TRUE(second.report.success);
    auto identities = [](const auto& report) {
      const auto inspection = LoadInspection(report.cooked_root);
      std::vector<std::tuple<std::string, oxygen::data::AssetKey, uint8_t>>
        result;
      for (const auto& asset : inspection.Assets()) {
        result.emplace_back(asset.virtual_path, asset.key, asset.asset_type);
      }
      std::ranges::sort(result);
      return result;
    };
    EXPECT_EQ(identities(first.report), identities(second.report));
    LoadAndVerify(first.report, verify);
    LoadAndVerify(second.report, verify);
  }

  static auto LoadAndVerify(const oxygen::content::import::ImportReport& report,
    const std::function<void(const SceneAsset&, const GeometryAsset&,
      const MaterialAsset&)>& verify) -> void
  {
    const auto inspection = LoadInspection(report.cooked_root);
    const auto scene_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kScene);
    const auto geometry_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kGeometry);
    const auto material_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kMaterial);
    ASSERT_TRUE(scene_entry && geometry_entry && material_entry);
    oxygen::co::testing::TestEventLoop loop;
    (oxygen::co::Run)(loop, [&]() -> oxygen::co::Co<> {
      oxygen::co::ThreadPool pool(loop, 2);
      oxygen::content::AssetLoaderConfig config;
      config.thread_pool = oxygen::observer_ptr(&pool);
      config.work_offline = true;
      config.verify_content_hashes = true;
      AssetLoader loader(
        oxygen::engine::internal::EngineTagFactory::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
      loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
      loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
      loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);
      loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        loader.AddLooseCookedRoot(report.cooked_root);
        const auto scene
          = co_await loader.LoadAssetAsync<SceneAsset>(scene_entry->key);
        const auto geometry
          = co_await loader.LoadAssetAsync<GeometryAsset>(geometry_entry->key);
        const auto material
          = co_await loader.LoadAssetAsync<MaterialAsset>(material_entry->key);
        EXPECT_TRUE(scene && geometry && material);
        if (scene && geometry && material) {
          verify(*scene, *geometry, *material);
        }
        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    });
  }
};

NOLINT_TEST_F(StaticScalarLoadedValuesTest,
  GltfNativeLoadPreservesConvertedGeometryCameraLightAndMaterial)
{
  for (const auto extension : { "gltf", "glb" }) {
    SCOPED_TRACE(extension);
    for (const auto scale : { 1.0F, 2.0F }) {
      for (const auto hierarchy : { false, true }) {
        SCOPED_TRACE(scale);
        SCOPED_TRACE(hierarchy);
        const auto root = MakeTempDir(std::string("loaded_") + extension + "_"
          + std::to_string(scale) + "_" + std::to_string(hierarchy));
        const auto source = root / (std::string("scene.") + extension);
        std::ifstream input(
          TestModelsDirFromFile() / "static_scalar_camera_sun.gltf");
        auto document = nlohmann::json::parse(input);
        if (hierarchy) {
          document["nodes"].push_back(
            { { "name", "Parent" }, { "translation", { 5, 0, 0 } },
              { "rotation", { 0, 0, std::sqrt(0.5), std::sqrt(0.5) } },
              { "children", { 0 } } });
          document["scenes"][0]["nodes"] = { 3, 1, 2 };
        }
        WriteGltfSource(source, document);
        oxygen::content::import::ImportRequest request;
        request.source_path = source;
        request.cooked_root = root / "cooked";
        request.options.scene_content_policy
          = oxygen::content::import::SceneContentPolicy::kStaticScalar;
        request.options.coordinate.bake_transforms_into_meshes = false;
        request.options.coordinate.unit_normalization = oxygen::content::
          import::UnitNormalizationPolicy::kApplyCustomFactor;
        request.options.coordinate.unit_scale = scale;
        ImportAndVerifyCleanCopy(std::move(request),
          [scale, hierarchy](
            const auto& scene, const auto& geometry, const auto& material) {
            VerifyLoadedGltfValues(scene, geometry, material, scale, hierarchy);
          });
      }
    }
  }
}

NOLINT_TEST_F(
  StaticScalarLoadedValuesTest, FbxNativeLoadPreservesUnitsHandednessAndValues)
{
  for (const auto centimeters : { false, true }) {
    for (const auto reflected : { false, true }) {
      for (const auto hierarchy : { false, true }) {
        SCOPED_TRACE(centimeters);
        SCOPED_TRACE(reflected);
        SCOPED_TRACE(hierarchy);
        const auto root
          = MakeTempDir("loaded_fbx_" + std::to_string(centimeters) + "_"
            + std::to_string(reflected) + "_" + std::to_string(hierarchy));
        std::ifstream input(
          TestModelsDirFromFile() / "static_scalar_camera_sun.fbx");
        std::string text((std::istreambuf_iterator<char>(input)), {});
        ASSERT_FALSE(text.empty());
        if (centimeters) {
          const std::string before
            = R"("UnitScaleFactor", "double", "Number", "",100)";
          const auto position = text.find(before);
          ASSERT_NE(position, std::string::npos);
          text.replace(position, before.size(),
            R"("UnitScaleFactor", "double", "Number", "",1)");
        }
        if (reflected) {
          const std::string before
            = R"("FrontAxisSign", "int", "Integer", "",1)";
          const auto position = text.find(before);
          ASSERT_NE(position, std::string::npos);
          text.replace(position, before.size(),
            R"("FrontAxisSign", "int", "Integer", "",-1)");
        }
        const auto properties = text.find(R"(P: "FieldOfView")");
        ASSERT_NE(properties, std::string::npos);
        text.insert(properties, R"(P: "ApertureMode", "enum", "", "",2
            )");
        const auto objects = text.find("Objects: {");
        ASSERT_NE(objects, std::string::npos);
        text.insert(objects + std::string("Objects: {").size(), R"(
    Material: 1007, "Material::Scalar", "" {
        Version: 102
        ShadingModel: "lambert"
        Properties70: {
            P: "DiffuseColor", "Color", "", "A",0.8,0.2,0.1
            P: "DiffuseFactor", "Number", "", "A",1
        }
    }
)");
        const auto connections = text.find("Connections: {");
        ASSERT_NE(connections, std::string::npos);
        text.insert(connections + std::string("Connections: {").size(),
          "\n    C: \"OO\",1007,1002\n");
        if (hierarchy) {
          text.insert(objects + std::string("Objects: {").size(), R"(
    Model: 1008, "Model::Parent", "Null" {
      Version: 232
      Properties70: {
        P: "Lcl Translation", "Lcl Translation", "", "A",5,0,0
        P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,90
        P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
      }
    }
)");
          const std::string old_parent = "C: \"OO\",1002,0";
          const auto link = text.find(old_parent);
          ASSERT_NE(link, std::string::npos);
          text.replace(link, old_parent.size(),
            "C: \"OO\",1002,1008\n    C: \"OO\",1008,0");
        }
        const auto source = root / "scene.fbx";
        {
          std::ofstream output(source);
          output << text;
        }
        oxygen::content::import::ImportRequest request;
        request.source_path = source;
        request.cooked_root = root / "cooked";
        request.options.scene_content_policy
          = oxygen::content::import::SceneContentPolicy::kStaticScalar;
        request.options.coordinate.bake_transforms_into_meshes = false;
        ImportAndVerifyCleanCopy(std::move(request),
          [centimeters, reflected, hierarchy](
            const auto& scene, const auto& geometry, const auto& material) {
            const auto units = centimeters ? 0.01F : 1.0F;
            VerifyLoadedTriangle(
              scene, geometry, units, reflected ? -1.0F : 1.0F, hierarchy);
            const auto cameras
              = scene.template GetComponents<PerspectiveCameraRecord>();
            ASSERT_EQ(cameras.size(), 1U);
            EXPECT_NEAR(
              cameras[0].fov_y, std::numbers::pi_v<float> / 3.0F, 0.0001F);
            EXPECT_NEAR(cameras[0].aspect_ratio, 16.0F / 9.0F, 0.0001F);
            EXPECT_NEAR(cameras[0].near_plane, 0.2F * units, 0.0001F);
            EXPECT_NEAR(cameras[0].far_plane, 250.0F * units, 0.0001F);
            const auto lights
              = scene.template GetComponents<DirectionalLightRecord>();
            ASSERT_EQ(lights.size(), 1U);
            EXPECT_FLOAT_EQ(lights[0].intensity_lux, 1.0F);
            ExpectVector(
              { lights[0].common.color_rgb[0], lights[0].common.color_rgb[1],
                lights[0].common.color_rgb[2] },
              { 0.8F, 0.7F, 0.6F });
            const auto color = material.GetBaseColor();
            ExpectVector(
              { color[0], color[1], color[2] }, { 0.8F, 0.2F, 0.1F });
            EXPECT_FLOAT_EQ(color[3], 1.0F);
            EXPECT_NEAR(material.GetMetalness(), 0.0F, 0.001F);
            EXPECT_NEAR(material.GetRoughness(), 1.0F, 0.001F);
          });
      }
    }
  }
}

} // namespace
