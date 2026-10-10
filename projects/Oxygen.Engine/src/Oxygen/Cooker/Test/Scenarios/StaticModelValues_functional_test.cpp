//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/AsyncImportService.cpp, Import/Internal/fbx/FbxAdapter.cpp,
//   Import/Internal/gltf/GltfAdapter.cpp

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

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

using oxygen::cooker::test::ExpectVec3Near;

//! The cooked values of a single-mesh static model, read without the Content
//! runtime: the scene, geometry and material descriptors decoded parse-only,
//! and the mesh's vertex and index bytes copied from the cooked buffer files.
struct CookedStaticModel {
  std::unique_ptr<SceneAsset> scene;
  std::unique_ptr<GeometryAsset> geometry;
  std::unique_ptr<MaterialAsset> material;
  std::vector<oxygen::data::Vertex> vertices;
  std::vector<uint32_t> indices;
};

using CookedModelCheck = std::function<void(const CookedStaticModel&)>;

template <typename Decode>
auto DecodeParseOnly(const std::filesystem::path& cooked_root,
  const oxygen::content::lc::Inspection::AssetEntry& entry,
  const Decode& decode)
{
  oxygen::serio::FileStream<> stream(
    cooked_root / entry.descriptor_relpath, std::ios::in);
  oxygen::serio::Reader reader(stream);
  const oxygen::content::LoaderContext context {
    .current_asset_key = entry.key,
    .desc_reader = &reader,
    .work_offline = true,
    .parse_only = true,
  };
  return decode(context);
}

//! Copies the one vertex buffer and the one index buffer of the cooked root
//! into `model`. A single-mesh static model cooks exactly one of each.
auto ReadCookedMeshBuffers(const std::filesystem::path& cooked_root,
  const oxygen::content::lc::Inspection& inspection, CookedStaticModel& model)
  -> void
{
  using oxygen::data::BufferResource;
  using BufferDesc = oxygen::data::pak::core::BufferResourceDesc;
  const auto file_path = [&](const oxygen::content::lc::FileKind kind)
    -> std::optional<std::filesystem::path> {
    const auto files = inspection.Files();
    const auto it = std::ranges::find_if(
      files, [kind](const auto& file) -> auto { return file.kind == kind; });
    if (it == files.end()) {
      return std::nullopt;
    }
    return cooked_root / it->relpath;
  };
  const auto table_path
    = file_path(oxygen::content::lc::FileKind::kBuffersTable);
  const auto data_path = file_path(oxygen::content::lc::FileKind::kBuffersData);
  ASSERT_HAS_VALUE(table_path);
  ASSERT_HAS_VALUE(data_path);
  const auto table_bytes = oxygen::cooker::test::ReadBytes(*table_path);
  const auto data_bytes = oxygen::cooker::test::ReadBytes(*data_path);
  ASSERT_EQ(table_bytes.size() % sizeof(BufferDesc), 0U);
  std::vector<BufferDesc> table(table_bytes.size() / sizeof(BufferDesc));
  std::memcpy(table.data(), table_bytes.data(), table_bytes.size());

  const auto with_usage
    = [&](const BufferResource::UsageFlags usage) -> std::vector<BufferDesc> {
    std::vector<BufferDesc> matches;
    std::ranges::copy_if(table, std::back_inserter(matches),
      [usage](const BufferDesc& desc) -> bool {
        return (desc.usage_flags & static_cast<uint32_t>(usage)) != 0U;
      });
    return matches;
  };
  const auto vertex_buffers
    = with_usage(BufferResource::UsageFlags::kVertexBuffer);
  const auto index_buffers
    = with_usage(BufferResource::UsageFlags::kIndexBuffer);
  ASSERT_EQ(vertex_buffers.size(), 1U);
  ASSERT_EQ(index_buffers.size(), 1U);
  const auto& vertex_buffer = vertex_buffers.front();
  const auto& index_buffer = index_buffers.front();
  ASSERT_EQ(vertex_buffer.element_stride, sizeof(oxygen::data::Vertex));
  ASSERT_EQ(index_buffer.element_format,
    static_cast<uint8_t>(oxygen::Format::kR32UInt));
  for (const auto& buffer : { vertex_buffer, index_buffer }) {
    ASSERT_LE(buffer.data_offset + buffer.size_bytes, data_bytes.size());
  }
  ASSERT_EQ(vertex_buffer.size_bytes % sizeof(oxygen::data::Vertex), 0U);
  ASSERT_EQ(index_buffer.size_bytes % sizeof(uint32_t), 0U);
  model.vertices.resize(
    vertex_buffer.size_bytes / sizeof(oxygen::data::Vertex));
  std::memcpy(model.vertices.data(),
    data_bytes.data() + static_cast<std::ptrdiff_t>(vertex_buffer.data_offset),
    vertex_buffer.size_bytes);
  model.indices.resize(index_buffer.size_bytes / sizeof(uint32_t));
  std::memcpy(model.indices.data(),
    data_bytes.data() + static_cast<std::ptrdiff_t>(index_buffer.data_offset),
    index_buffer.size_bytes);
}

auto VerifyCookedTriangle(const CookedStaticModel& model,
  const float unit_scale, const float front_sign = 1.0F,
  const bool hierarchy = false) -> void
{
  const auto& scene = *model.scene;
  const auto& geometry = *model.geometry;
  const auto renderables = scene.GetComponents<RenderableRecord>();
  ASSERT_EQ(renderables.size(), 1U);
  ASSERT_EQ(geometry.Meshes().size(), 1U);
  const auto& mesh = *geometry.Meshes().front();
  ASSERT_EQ(model.vertices.size(), 3U);
  ASSERT_EQ(model.indices.size(), 3U);
  const auto world = WorldTransform(scene, renderables.front().node_index);
  const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3(world)));
  const auto expected = hierarchy
    ? std::array { glm::vec3(3, -3 * front_sign, 1) * unit_scale,
        glm::vec3(3, -3 * front_sign, 3) * unit_scale,
        glm::vec3(0, -3 * front_sign, 1) * unit_scale, }
    : std::array { glm::vec3(1, -3 * front_sign, 2) * unit_scale,
        glm::vec3(3, -3 * front_sign, 2) * unit_scale,
        glm::vec3(1, -3 * front_sign, 5) * unit_scale, };
  if (hierarchy) {
    const auto node = renderables.front().node_index;
    EXPECT_NE(scene.GetNode(node).parent_index, node);
  }
  auto minimum = glm::vec3(std::numeric_limits<float>::max());
  auto maximum = glm::vec3(std::numeric_limits<float>::lowest());
  std::vector<glm::vec3> points;
  for (const auto& vertex : model.vertices) {
    minimum = glm::min(minimum, vertex.position);
    maximum = glm::max(maximum, vertex.position);
    points.emplace_back(world * glm::vec4(vertex.position, 1));
    ExpectVec3Near(
      glm::normalize(normal_matrix * vertex.normal), { 0, -front_sign, 0 });
  }
  for (const auto point : expected) {
    EXPECT_TRUE(std::ranges::any_of(points, [&](const auto actual) -> auto {
      return glm::distance(actual, point) < 0.0001F;
    }));
  }
  ExpectVec3Near(geometry.BoundingBoxMin(), minimum);
  ExpectVec3Near(geometry.BoundingBoxMax(), maximum);
  ExpectVec3Near(mesh.BoundingBoxMin(), minimum);
  ExpectVec3Near(mesh.BoundingBoxMax(), maximum);
  const auto& indices = model.indices;
  const auto face_normal = glm::normalize(
    glm::cross(points.at(indices.at(1)) - points.at(indices.at(0)),
      points.at(indices.at(2)) - points.at(indices.at(0))));
  ExpectVec3Near(face_normal, { 0, -front_sign, 0 });
}

auto VerifyCookedGltfValues(const CookedStaticModel& model,
  const float unit_scale, const bool hierarchy) -> void
{
  VerifyCookedTriangle(model, unit_scale, 1.0F, hierarchy);
  const auto& scene = *model.scene;
  const auto& material = *model.material;
  const auto cameras = scene.GetComponents<PerspectiveCameraRecord>();
  ASSERT_EQ(cameras.size(), 1U);
  EXPECT_FLOAT_EQ(cameras.front().fov_y, 1.0F);
  EXPECT_NEAR(cameras.front().aspect_ratio, 16.0F / 9.0F, 0.0001F);
  EXPECT_NEAR(cameras.front().near_plane, 0.2F * unit_scale, 0.0001F);
  EXPECT_NEAR(cameras.front().far_plane, 250.0F * unit_scale, 0.0001F);
  ExpectVec3Near(glm::vec3(WorldTransform(scene, cameras.front().node_index)
                   * glm::vec4(0, 0, 0, 1)),
    glm::vec3(0, -5, 2) * unit_scale);
  const auto lights = scene.GetComponents<DirectionalLightRecord>();
  ASSERT_EQ(lights.size(), 1U);
  EXPECT_FLOAT_EQ(lights.front().intensity_lux, 5000.0F);
  ExpectVec3Near(
    { lights.front().common.color_rgb[0], lights.front().common.color_rgb[1],
      lights.front().common.color_rgb[2] },
    { 0.8F, 0.7F, 0.6F });
  const auto color = material.GetBaseColor();
  ExpectVec3Near(
    { oxygen::base::CheckedAt(color, 0), oxygen::base::CheckedAt(color, 1),
      oxygen::base::CheckedAt(color, 2) },
    { 0.8F, 0.2F, 0.1F });
  EXPECT_FLOAT_EQ(oxygen::base::CheckedAt(color, 3), 1.0F);
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
    document.at("buffers").at(0).erase("uri");
  } else {
    document.at("buffers").at(0).at("uri") = "positions.bin";
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

class StaticModelValuesTest
  : public oxygen::content::import::test::ModelImportTestBase {
protected:
  auto ImportAndVerifyCleanCopy(oxygen::content::import::ImportRequest request,
    const CookedModelCheck& verify) -> void
  {
    if (!request.cooked_root.has_value()) {
      FAIL() << "Import verification requires a cooked root";
    }
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
    auto identities = [](const auto& report) -> auto {
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

  //! Verifies the cooked root's hashes, then reads the model back from its
  //! cooked files and runs `verify` on it.
  static auto LoadAndVerify(const oxygen::content::import::ImportReport& report,
    const CookedModelCheck& verify) -> void
  {
    oxygen::content::lc::ValidateRoot(
      report.cooked_root, oxygen::content::lc::IntegrityCheck::kFull);
    const auto inspection = LoadInspection(report.cooked_root);
    const auto scene_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kScene);
    const auto geometry_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kGeometry);
    const auto material_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kMaterial);
    ASSERT_TRUE(scene_entry && geometry_entry && material_entry);
    auto model = CookedStaticModel {};
    model.scene = DecodeParseOnly(
      report.cooked_root, *scene_entry, [](const auto& context) -> auto {
        return oxygen::content::loaders::LoadSceneAsset(context);
      });
    model.geometry = DecodeParseOnly(
      report.cooked_root, *geometry_entry, [](const auto& context) -> auto {
        return oxygen::content::loaders::LoadGeometryAsset(context);
      });
    model.material = DecodeParseOnly(
      report.cooked_root, *material_entry, [](const auto& context) -> auto {
        return oxygen::content::loaders::LoadMaterialAsset(context);
      });
    ASSERT_TRUE(model.scene && model.geometry && model.material);
    ASSERT_NO_FATAL_FAILURE(
      ReadCookedMeshBuffers(report.cooked_root, inspection, model));
    verify(model);
  }
};

NOLINT_TEST_F(StaticModelValuesTest, FbxGeneratesMissingNormalMapTangents)
{
  const auto root = MakeTempDir("normal_map_tangents");
  for (const auto* name :
    { "static_normal_triangle.fbx", "static_tangent_normal.png" }) {
    std::filesystem::copy_file(TestModelsDirFromFile() / name, root / name);
  }
  oxygen::content::import::ImportRequest request {};
  request.source_path = root / "static_normal_triangle.fbx";
  request.cooked_root = root / "cooked";
  request.options.scene_content_policy
    = oxygen::content::import::SceneContentPolicy::kStatic;
  request.options.coordinate.bake_transforms_into_meshes = false;
  request.options.tangent_policy
    = oxygen::content::import::GeometryAttributePolicy::kGenerateMissing;
  ImportAndVerifyCleanCopy(
    std::move(request), [](const CookedStaticModel& model) -> void {
      const auto& scene = *model.scene;
      ASSERT_NE(
        model.material->GetNormalTexture(), oxygen::data::kNoResourceReference);
      ASSERT_EQ(model.geometry->Meshes().size(), 1U);
      const auto& vertices = model.vertices;
      ASSERT_EQ(vertices.size(), 3U);
      const auto renderables = scene.GetComponents<RenderableRecord>();
      ASSERT_EQ(renderables.size(), 1U);
      const auto world
        = glm::mat3(WorldTransform(scene, renderables.front().node_index));
      const auto normal_matrix = glm::transpose(glm::inverse(world));
      for (const auto& vertex : vertices) {
        ExpectVec3Near(vertex.normal, { 0, 0, 1 });
        ExpectVec3Near(vertex.tangent, { 0, 1, 0 });
        ExpectVec3Near(vertex.bitangent, { 1, 0, 0 });
        ExpectVec3Near(
          glm::normalize(normal_matrix * vertex.normal), { 0, -1, 0 });
        ExpectVec3Near(glm::normalize(world * vertex.tangent), { 0, 0, 1 });
        ExpectVec3Near(glm::normalize(world * vertex.bitangent), { 1, 0, 0 });
        EXPECT_NEAR(
          glm::dot(glm::cross(vertex.normal, vertex.tangent), vertex.bitangent),
          -1.0F, 0.0001F);
      }
    });
}

NOLINT_TEST_F(StaticModelValuesTest,
  GltfCookPreservesConvertedGeometryCameraLightAndMaterial)
{
  for (const auto* const extension : { "gltf", "glb" }) {
    SCOPED_TRACE(extension);
    for (const auto scale : { 1.0F, 2.0F }) {
      for (const auto hierarchy : { false, true }) {
        SCOPED_TRACE(scale);
        SCOPED_TRACE(hierarchy);
        const auto root = MakeTempDir(std::string("loaded_") + extension + "_"
          + std::to_string(scale) + "_"
          + std::to_string(static_cast<int>(hierarchy)));
        const auto source = root / (std::string("scene.") + extension);
        std::ifstream input(
          TestModelsDirFromFile() / "static_scalar_camera_sun.gltf");
        auto document = nlohmann::json::parse(input);
        if (hierarchy) {
          document.at("nodes").push_back({
            { "name", "Parent" },
            { "translation", { 5, 0, 0 } },
            { "rotation", { 0, 0, std::sqrt(0.5), std::sqrt(0.5) } },
            { "children", { 0 } },
          });
          document.at("scenes").at(0).at("nodes") = { 3, 1, 2 };
        }
        WriteGltfSource(source, document);
        oxygen::content::import::ImportRequest request;
        request.source_path = source;
        request.cooked_root = root / "cooked";
        request.options.scene_content_policy
          = oxygen::content::import::SceneContentPolicy::kStatic;
        request.options.coordinate.bake_transforms_into_meshes = false;
        request.options.coordinate.unit_normalization = oxygen::content::
          import::UnitNormalizationPolicy::kApplyCustomFactor;
        request.options.coordinate.unit_scale = scale;
        ImportAndVerifyCleanCopy(std::move(request),
          [scale, hierarchy](const CookedStaticModel& model) -> void {
            VerifyCookedGltfValues(model, scale, hierarchy);
          });
      }
    }
  }
}

NOLINT_TEST_F(StaticModelValuesTest, FbxCookPreservesUnitsHandednessAndValues)
{
  for (const auto centimeters : { false, true }) {
    for (const auto reflected : { false, true }) {
      for (const auto hierarchy : { false, true }) {
        SCOPED_TRACE(centimeters);
        SCOPED_TRACE(reflected);
        SCOPED_TRACE(hierarchy);
        const auto root = MakeTempDir("loaded_fbx_"
          + std::to_string(static_cast<int>(centimeters)) + "_"
          + std::to_string(static_cast<int>(reflected)) + "_"
          + std::to_string(static_cast<int>(hierarchy)));
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
          = oxygen::content::import::SceneContentPolicy::kStatic;
        request.options.coordinate.bake_transforms_into_meshes = false;
        ImportAndVerifyCleanCopy(std::move(request),
          [centimeters, reflected, hierarchy](
            const CookedStaticModel& model) -> void {
            const auto& scene = *model.scene;
            const auto& material = *model.material;
            const auto units = centimeters ? 0.01F : 1.0F;
            VerifyCookedTriangle(
              model, units, reflected ? -1.0F : 1.0F, hierarchy);
            const auto cameras = scene.GetComponents<PerspectiveCameraRecord>();
            ASSERT_EQ(cameras.size(), 1U);
            EXPECT_NEAR(
              cameras.front().fov_y, std::numbers::pi_v<float> / 3.0F, 0.0001F);
            EXPECT_NEAR(cameras.front().aspect_ratio, 16.0F / 9.0F, 0.0001F);
            EXPECT_NEAR(cameras.front().near_plane, 0.2F * units, 0.0001F);
            EXPECT_NEAR(cameras.front().far_plane, 250.0F * units, 0.0001F);
            const auto lights = scene.GetComponents<DirectionalLightRecord>();
            ASSERT_EQ(lights.size(), 1U);
            EXPECT_FLOAT_EQ(lights.front().intensity_lux, 1.0F);
            ExpectVec3Near({ lights.front().common.color_rgb[0],
                             lights.front().common.color_rgb[1],
                             lights.front().common.color_rgb[2] },
              { 0.8F, 0.7F, 0.6F });
            const auto color = material.GetBaseColor();
            ExpectVec3Near({ oxygen::base::CheckedAt(color, 0),
                             oxygen::base::CheckedAt(color, 1),
                             oxygen::base::CheckedAt(color, 2) },
              { 0.8F, 0.2F, 0.1F });
            EXPECT_FLOAT_EQ(oxygen::base::CheckedAt(color, 3), 1.0F);
            EXPECT_NEAR(material.GetMetalness(), 0.0F, 0.001F);
            EXPECT_NEAR(material.GetRoughness(), 1.0F, 0.001F);
          });
      }
    }
  }
}

} // namespace
