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
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
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
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Cooker/test/Support/TestValues.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::SceneAsset;
using oxygen::data::pak::world::DirectionalLightRecord;
using oxygen::data::pak::world::PerspectiveCameraRecord;
using oxygen::data::pak::world::RenderableRecord;
using oxygen::data::pak::world::SceneNodeIndexT;

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

//! The cooked values of a single-mesh model, read without the Content
//! runtime: the scene, geometry and material descriptors decoded parse-only,
//! and the mesh's vertex and index bytes copied from the cooked buffer files.
struct CookedModel {
  std::unique_ptr<SceneAsset> scene;
  std::unique_ptr<GeometryAsset> geometry;
  std::unique_ptr<MaterialAsset> material;
  std::vector<oxygen::data::Vertex> vertices;
  std::vector<uint32_t> indices;
};

using CookedModelCheck = std::function<void(const CookedModel&)>;

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
//! into `model`. A single-mesh model cooks exactly one of each.
auto ReadCookedMeshBuffers(const std::filesystem::path& cooked_root,
  const oxygen::content::lc::Inspection& inspection, CookedModel& model) -> void
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
    std::span(data_bytes)
      .subspan(static_cast<std::size_t>(vertex_buffer.data_offset))
      .data(),
    vertex_buffer.size_bytes);
  model.indices.resize(index_buffer.size_bytes / sizeof(uint32_t));
  std::memcpy(model.indices.data(),
    std::span(data_bytes)
      .subspan(static_cast<std::size_t>(index_buffer.data_offset))
      .data(),
    index_buffer.size_bytes);
}

auto VerifyCookedTriangle(const CookedModel& model, const float unit_scale,
  const float front_sign = 1.0F, const bool hierarchy = false) -> void
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

auto VerifyCookedGltfValues(const CookedModel& model, const float unit_scale,
  const bool hierarchy) -> void
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

class ModelValuesTest
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
    ASSERT_HAS_VALUE(scene_entry);
    ASSERT_HAS_VALUE(geometry_entry);
    ASSERT_HAS_VALUE(material_entry);
    auto model = CookedModel {};
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

NOLINT_TEST_F(ModelValuesTest, FbxGeneratesMissingNormalMapTangents)
{
  const auto root = MakeTempDir("normal_map_tangents");
  for (const auto* name :
    { "static_normal_triangle.fbx", "static_tangent_normal.png" }) {
    std::filesystem::copy_file(TestModelsDirFromFile() / name, root / name);
  }
  oxygen::content::import::ImportRequest request {};
  request.source_path = root / "static_normal_triangle.fbx";
  request.asset_virtual_path
    = oxygen::cooker::test::ModelVirtualPath(request.source_path);
  request.cooked_root = root / "cooked";
  request.options.coordinate.bake_transforms_into_meshes = false;
  request.options.tangent_policy
    = oxygen::content::import::GeometryAttributePolicy::kGenerateMissing;
  ImportAndVerifyCleanCopy(
    std::move(request), [](const CookedModel& model) -> void {
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
        // The geometry is in Oxygen axes: the Y-up source's +Z normal is -Y.
        ExpectVec3Near(vertex.normal, { 0, -1, 0 });
        ExpectVec3Near(vertex.tangent, { 0, 0, 1 });
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

NOLINT_TEST_F(
  ModelValuesTest, GltfCookPreservesConvertedGeometryCameraLightAndMaterial)
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
        request.asset_virtual_path
          = oxygen::cooker::test::ModelVirtualPath(request.source_path);
        request.cooked_root = root / "cooked";
        request.options.coordinate.bake_transforms_into_meshes = false;
        request.options.coordinate.unit_normalization = oxygen::content::
          import::UnitNormalizationPolicy::kApplyCustomFactor;
        request.options.coordinate.unit_scale = scale;
        ImportAndVerifyCleanCopy(std::move(request),
          [scale, hierarchy](const CookedModel& model) -> void {
            VerifyCookedGltfValues(model, scale, hierarchy);
          });
      }
    }
  }
}

NOLINT_TEST_F(ModelValuesTest, FbxCookPreservesUnitsHandednessAndValues)
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
        request.asset_virtual_path
          = oxygen::cooker::test::ModelVirtualPath(request.source_path);
        request.cooked_root = root / "cooked";
        request.options.coordinate.bake_transforms_into_meshes = false;
        ImportAndVerifyCleanCopy(std::move(request),
          [centimeters, reflected, hierarchy](
            const CookedModel& model) -> void {
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

            // The source is Y up. Its geometry is in Oxygen axes on its own,
            // Z up, and its nodes carry no axis rotation, so a scene that
            // places the geometry without the model's nodes shows it upright.
            if (!reflected) {
              for (const auto expected : {
                     glm::vec3(0, 0, 0),
                     glm::vec3(1, 0, 0),
                     glm::vec3(0, 0, 1),
                   }) {
                EXPECT_TRUE(std::ranges::any_of(model.vertices,
                  [&](const oxygen::data::Vertex& vertex) -> bool {
                    return glm::distance(vertex.position, expected * units)
                      < 0.0001F;
                  }))
                  << "source (0,1,0) lands on Oxygen up";
              }
            }
            if (!reflected && !hierarchy) {
              const auto& shown = scene.GetNode(
                scene.GetComponents<RenderableRecord>().front().node_index);
              EXPECT_NEAR(std::abs(shown.rotation[3]), 1.0F, 0.0001F);
            }
            // Cameras and lights keep their frames: the FBX camera looks along
            // its +X with +Y up, and the FBX light points down its -Y.
            if (!reflected) {
              const auto camera_world
                = glm::mat3(WorldTransform(scene, cameras.front().node_index));
              ExpectVec3Near(glm::normalize(camera_world * glm::vec3(0, 0, -1)),
                { 1, 0, 0 });
              ExpectVec3Near(
                glm::normalize(camera_world * glm::vec3(0, 1, 0)), { 0, 0, 1 });
              const auto light_world
                = glm::mat3(WorldTransform(scene, lights.front().node_index));
              ExpectVec3Near(glm::normalize(light_world * glm::vec3(0, -1, 0)),
                { 0, 0, -1 });
            }
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

//! Whether `report` holds a warning of `code` at `object_path`.
auto Warns(const oxygen::content::import::ImportReport& report,
  const std::string_view code, const std::string_view object_path) -> bool
{
  return std::ranges::any_of(
    report.diagnostics, [&](const auto& diagnostic) -> bool {
      return diagnostic.severity
        == oxygen::content::import::ImportSeverity::kWarning
        && diagnostic.code == code && diagnostic.object_path == object_path;
    });
}

//! Whether `report` holds any diagnostic of `code`.
auto Reports(const oxygen::content::import::ImportReport& report,
  const std::string_view code) -> bool
{
  return std::ranges::any_of(report.diagnostics,
    [&](const auto& diagnostic) -> bool { return diagnostic.code == code; });
}

//! The geometry anchor of the one geometry `inspection` lists: its storage
//! name.
auto GeometryAnchor(const oxygen::content::lc::Inspection& inspection)
  -> std::string
{
  for (const auto& entry : inspection.Assets()) {
    if (entry.asset_type
      == static_cast<uint8_t>(oxygen::data::AssetType::kGeometry)) {
      return std::filesystem::path(entry.virtual_path).stem().string();
    }
  }
  return {};
}

//! The record's geometry defaults become the cooked geometry's asset
//! defaults; keys the file no longer has warn, and settings the format
//! ignores warn. Light and camera settings shape the scene.
NOLINT_TEST_F(ModelValuesTest, GltfSettingsShapeTheCookedModel)
{
  using oxygen::content::import::GeometryImportDefaults;
  using oxygen::content::import::SourceUpAxis;
  using oxygen::content::import::SurfaceImportDefaults;
  using oxygen::data::pak::geometry::LodPolicyType;
  const auto root = MakeTempDir("gltf_settings");
  const auto source = root / "scene.gltf";
  std::ifstream input(
    TestModelsDirFromFile() / "static_scalar_camera_sun.gltf");
  WriteGltfSource(source, nlohmann::json::parse(input));
  auto request = oxygen::content::import::ImportRequest {};
  request.source_path = source;
  request.asset_virtual_path = oxygen::cooker::test::ModelVirtualPath(source);
  request.cooked_root = root / "cooked";

  // A first import, as the source analysis would, names the parts.
  const auto first = RunImport(request);
  ASSERT_TRUE(first.report.success);
  auto surface = oxygen::data::SurfaceId {};
  auto first_lux = 0.0F;
  LoadAndVerify(first.report, [&](const CookedModel& model) -> void {
    ASSERT_EQ(model.geometry->Surfaces().surfaces.size(), 1U);
    surface = model.geometry->Surfaces().surfaces.front().surface_id;
    const auto lights = model.scene->GetComponents<DirectionalLightRecord>();
    ASSERT_EQ(lights.size(), 1U);
    first_lux = lights.front().intensity_lux;
    EXPECT_EQ(model.scene->GetComponents<PerspectiveCameraRecord>().size(), 1U);
  });
  const auto anchor = GeometryAnchor(LoadInspection(first.report.cooked_root));
  ASSERT_FALSE(anchor.empty());

  auto& options = request.options;
  auto lod = oxygen::data::pak::geometry::LodPolicyRecord {};
  lod.type = LodPolicyType::kFixed;
  lod.lod_index = 3;
  const auto gone = oxygen::data::SurfaceId::Generate();
  options.geometry_defaults.emplace(anchor,
    GeometryImportDefaults {
      .lod_policy = lod,
      .surfaces = {
        { surface, SurfaceImportDefaults { .visible = false, .casts_shadows = false } },
        { gone, SurfaceImportDefaults { .visible = true, .casts_shadows = {} } },
      },
    });
  options.geometry_defaults.emplace("Gone", GeometryImportDefaults {});
  options.texture_overrides.emplace("Gone.base_color", options.texture_tuning);
  options.coordinate.up_axis = SourceUpAxis::kZ;
  options.light_intensity_scale = 2.0F;
  options.import_cameras = false;
  const auto second = RunImport(request);
  ASSERT_TRUE(second.report.success);
  EXPECT_TRUE(Warns(
    second.report, "import.setting_target_missing", "geometry_defaults/Gone"));
  EXPECT_TRUE(Warns(second.report, "import.setting_target_missing",
    "texture_overrides/Gone.base_color"));
  EXPECT_TRUE(Warns(second.report, "import.setting_target_missing",
    anchor + "/" + oxygen::data::to_string(gone)));
  EXPECT_TRUE(Warns(second.report, "import.setting_not_applicable", "up_axis"));
  LoadAndVerify(second.report, [&](const CookedModel& model) -> void {
    const auto& policy = model.geometry->DefaultLodPolicy();
    ASSERT_HAS_VALUE(policy);
    EXPECT_EQ(policy->type, LodPolicyType::kFixed);
    EXPECT_EQ(policy->lod_index, 3U);
    const auto& surfaces = model.geometry->Surfaces().surfaces;
    ASSERT_EQ(surfaces.size(), 1U);
    EXPECT_EQ(surfaces.front().surface_id, surface);
    ASSERT_HAS_VALUE(surfaces.front().defaults);
    EXPECT_FALSE(surfaces.front().defaults->visible);
    EXPECT_FALSE(surfaces.front().defaults->casts_shadows);
    const auto lights = model.scene->GetComponents<DirectionalLightRecord>();
    ASSERT_EQ(lights.size(), 1U);
    EXPECT_FLOAT_EQ(lights.front().intensity_lux, 2.0F * first_lux);
    EXPECT_TRUE(model.scene->GetComponents<PerspectiveCameraRecord>().empty());
  });

  options = {};
  options.import_lights = false;
  const auto third = RunImport(request);
  ASSERT_TRUE(third.report.success);
  LoadAndVerify(third.report, [](const CookedModel& model) -> void {
    EXPECT_TRUE(model.scene->GetComponents<DirectionalLightRecord>().empty());
    EXPECT_EQ(model.scene->GetComponents<PerspectiveCameraRecord>().size(), 1U);
  });
}

//! A remapped source material binds the existing material and is not
//! produced; a target that does not exist warns and the source material is
//! produced; a key the file lacks warns.
NOLINT_TEST_F(ModelValuesTest, GltfMaterialRemapBindsTheExistingMaterial)
{
  using oxygen::data::AssetType;
  const auto root = MakeTempDir("gltf_material_remap");
  std::ifstream input(
    TestModelsDirFromFile() / "static_scalar_camera_sun.gltf");
  const auto document = nlohmann::json::parse(input);
  const auto import_model
    = [&](const std::string& name,
        const std::map<std::string, std::string, std::less<>>& remap) -> auto {
    std::filesystem::create_directories(root / name);
    const auto source = root / name / (name + ".gltf");
    WriteGltfSource(source, document);
    auto request = oxygen::content::import::ImportRequest {};
    request.source_path = source;
    request.asset_virtual_path = oxygen::cooker::test::ModelVirtualPath(source);
    request.cooked_root = root / "cooked";
    request.surface_provenance
      = std::make_shared<const oxygen::content::import::SurfaceProvenance>(
        oxygen::Uuid::Generate());
    request.options.material_remap = remap;
    auto result = RunImport(std::move(request));
    return std::pair { result.report,
      oxygen::cooker::test::ModelVirtualPath(source) };
  };
  const auto entries_under = [&](const std::string& prefix,
                               const AssetType type) {
    auto entries = std::vector<oxygen::content::lc::Inspection::AssetEntry> {};
    for (const auto& entry : LoadInspection(root / "cooked").Assets()) {
      if (entry.asset_type == static_cast<uint8_t>(type)
        && entry.virtual_path.starts_with(prefix + "/")) {
        entries.push_back(entry);
      }
    }
    return entries;
  };

  const auto [first, first_path] = import_model("first", {});
  ASSERT_TRUE(first.success);
  const auto existing = entries_under(first_path, AssetType::kMaterial);
  ASSERT_EQ(existing.size(), 1U);

  const auto [second, second_path] = import_model("second",
    {
      { "Scalar", existing.front().virtual_path },
      { "Ghost", "/Content/Ghost.omat" },
    });
  ASSERT_TRUE(second.success);
  EXPECT_TRUE(
    Warns(second, "import.setting_target_missing", "material_remap/Ghost"));
  EXPECT_TRUE(entries_under(second_path, AssetType::kMaterial).empty());
  const auto geometries = entries_under(second_path, AssetType::kGeometry);
  ASSERT_EQ(geometries.size(), 1U);
  const auto geometry = DecodeParseOnly(
    root / "cooked", geometries.front(), [](const auto& context) -> auto {
      return oxygen::content::loaders::LoadGeometryAsset(context);
    });
  ASSERT_TRUE(geometry);
  const auto& surfaces = geometry->Surfaces().surfaces;
  ASSERT_EQ(surfaces.size(), 1U);
  ASSERT_FALSE(surfaces.front().bindings.empty());
  EXPECT_EQ(surfaces.front().bindings.front().default_material_key,
    existing.front().key);

  const auto [third, third_path]
    = import_model("third", { { "Scalar", "/Content/Missing.omat" } });
  ASSERT_TRUE(third.success);
  EXPECT_TRUE(Warns(
    third, "import.material_remap_target_missing", "material_remap/Scalar"));
  EXPECT_EQ(entries_under(third_path, AssetType::kMaterial).size(), 1U);
}

//! Generated normals smooth across an edge whose faces meet within the
//! smoothing angle and keep it hard beyond it; a glTF primitive without
//! normals stays faceted unless they are recalculated.
NOLINT_TEST_F(ModelValuesTest, GeneratedNormalsFollowTheSmoothingAngle)
{
  using oxygen::content::import::GeometryAttributePolicy;
  // Two triangles folded 90 degrees about their shared edge.
  auto document = nlohmann::json::parse(R"({
    "asset": {"version": "2.0"},
    "buffers": [{"uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIA/", "byteLength": 72}],
    "bufferViews": [{"buffer": 0, "byteLength": 72}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 6,
      "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 1]}],
    "materials": [{"name": "Fold"}],
    "meshes": [{"name": "Fold", "primitives": [
      {"attributes": {"POSITION": 0}, "material": 0}]}],
    "nodes": [{"name": "Fold", "mesh": 0}],
    "scenes": [{"nodes": [0]}], "scene": 0
  })");
  struct Case final {
    GeometryAttributePolicy normals;
    float angle;
    bool smooth;
  };
  for (const auto& [index, item] : std::views::enumerate(std::array {
         Case {
           .normals = GeometryAttributePolicy::kAlwaysRecalculate,
           .angle = 60.0F,
           .smooth = false,
         },
         Case {
           .normals = GeometryAttributePolicy::kAlwaysRecalculate,
           .angle = 120.0F,
           .smooth = true,
         },
         Case {
           .normals = GeometryAttributePolicy::kGenerateMissing,
           .angle = 120.0F,
           .smooth = false,
         },
       })) {
    SCOPED_TRACE(index);
    const auto root = MakeTempDir("smoothing_" + std::to_string(index));
    const auto source = root / "fold.gltf";
    {
      std::ofstream output(source);
      output << document.dump();
    }
    auto request = oxygen::content::import::ImportRequest {};
    request.source_path = source;
    request.asset_virtual_path = oxygen::cooker::test::ModelVirtualPath(source);
    request.cooked_root = root / "cooked";
    request.options.normal_policy = item.normals;
    request.options.normals_smoothing_angle_deg = item.angle;
    const auto imported = RunImport(std::move(request));
    ASSERT_TRUE(imported.report.success);
    LoadAndVerify(imported.report, [&](const CookedModel& model) -> void {
      // The shared edge's corners: one normal per position when smooth.
      auto shared_normals = std::vector<glm::vec3> {};
      for (const auto& vertex : model.vertices) {
        if (glm::distance(vertex.position, glm::vec3(0.0F)) < 0.0001F) {
          shared_normals.push_back(vertex.normal);
        }
      }
      ASSERT_FALSE(shared_normals.empty());
      const auto all_equal = std::ranges::all_of(
        shared_normals, [&](const glm::vec3& normal) -> bool {
          return glm::distance(normal, shared_normals.front()) < 0.001F;
        });
      EXPECT_EQ(all_equal && shared_normals.size() >= 1U, item.smooth)
        << shared_normals.size() << " corners";
      if (!item.smooth) {
        EXPECT_GE(shared_normals.size(), 2U);
      }
    });
  }
}

//! A glTF primitive without normals gets flat ones: each corner takes its own
//! triangle's face normal, on tilted faces too, where a unit normal's dot with
//! itself rounds below 1.
NOLINT_TEST_F(ModelValuesTest, GltfWithoutNormalsGetsItsFaceNormals)
{
  // A 3 x 3 grid of quads with uneven heights, two triangles each, unindexed.
  const auto document = nlohmann::json::parse(R"({
    "asset": {"version": "2.0"},
    "buffers": [{"uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAM3MTD4AAIA/AACAP5qZmT4AAAAAAACAP5qZmT4AAAAAAAAAAM3MTD4AAIA/AACAPzMzMz8AAIA/AACAP5qZmT4AAAAAAACAPzMzMz8AAIA/AAAAQM3MzD0AAAAAAAAAQM3MzD0AAAAAAACAPzMzMz8AAIA/AAAAQAAAAD8AAIA/AAAAQM3MzD0AAAAAAAAAQAAAAD8AAIA/AABAQM3MzD4AAAAAAABAQM3MzD4AAAAAAAAAQAAAAD8AAIA/AABAQM3MzD0AAIA/AAAAAM3MTD4AAIA/AAAAAJqZGT8AAABAAACAPzMzMz8AAIA/AACAPzMzMz8AAIA/AAAAAJqZGT8AAABAAACAP83MTD4AAABAAACAPzMzMz8AAIA/AACAP83MTD4AAABAAAAAQAAAAD8AAIA/AAAAQAAAAD8AAIA/AACAP83MTD4AAABAAAAAQGZmZj8AAABAAAAAQAAAAD8AAIA/AAAAQGZmZj8AAABAAABAQM3MzD0AAIA/AABAQM3MzD0AAIA/AAAAQGZmZj8AAABAAABAQJqZmT4AAABAAAAAAJqZGT8AAABAAAAAAM3MzD0AAEBAAACAP83MTD4AAABAAACAP83MTD4AAABAAAAAAM3MzD0AAEBAAACAPwAAAD8AAEBAAACAP83MTD4AAABAAACAPwAAAD8AAEBAAAAAQGZmZj8AAABAAAAAQGZmZj8AAABAAACAPwAAAD8AAEBAAAAAQM3MTD4AAEBAAAAAQGZmZj8AAABAAAAAQM3MTD4AAEBAAABAQJqZmT4AAABAAABAQJqZmT4AAABAAAAAQM3MTD4AAEBAAABAQM3MTD8AAEBA", "byteLength": 648}],
    "bufferViews": [{"buffer": 0, "byteLength": 648}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 54,
      "type": "VEC3", "min": [0, 0, 0], "max": [3, 0.9, 3]}],
    "materials": [{"name": "Grid"}],
    "meshes": [{"name": "Grid", "primitives": [
      {"attributes": {"POSITION": 0}, "material": 0}]}],
    "nodes": [{"name": "Grid", "mesh": 0}],
    "scenes": [{"nodes": [0]}], "scene": 0
  })");
  const auto root = MakeTempDir("gltf_flat_normals");
  const auto source = root / "grid.gltf";
  {
    std::ofstream output(source);
    output << document.dump();
  }
  auto request = oxygen::content::import::ImportRequest {};
  request.source_path = source;
  request.asset_virtual_path = oxygen::cooker::test::ModelVirtualPath(source);
  request.cooked_root = root / "cooked";
  const auto imported = RunImport(std::move(request));
  ASSERT_TRUE(imported.report.success);
  LoadAndVerify(imported.report, [](const CookedModel& model) -> void {
    ASSERT_EQ(model.indices.size(), 54U);
    for (size_t tri = 0; tri < model.indices.size() / 3; ++tri) {
      SCOPED_TRACE(tri);
      const auto& v0 = model.vertices.at(model.indices.at(tri * 3));
      const auto& v1 = model.vertices.at(model.indices.at((tri * 3) + 1));
      const auto& v2 = model.vertices.at(model.indices.at((tri * 3) + 2));
      const auto face = glm::normalize(
        glm::cross(v1.position - v0.position, v2.position - v0.position));
      for (const auto* corner : { &v0, &v1, &v2 }) {
        ExpectVec3Near(corner->normal, face, 0.001F);
      }
    }
  });
}

//! The scalar FBX scene, with a material, and with its axes or a film back
//! as asked.
auto ScalarFbxText(const std::filesystem::path& models, const bool axes,
  const bool film_back) -> std::string
{
  std::ifstream input(models / "static_scalar_camera_sun.fbx");
  std::string text((std::istreambuf_iterator<char>(input)), {});
  if (!axes) {
    for (const auto* property : {
           "UpAxis\"",
           "UpAxisSign\"",
           "FrontAxis\"",
           "FrontAxisSign\"",
           "CoordAxis\"",
           "CoordAxisSign\"",
         }) {
      const auto at = text.find(std::string("P: \"") + property);
      if (at != std::string::npos) {
        text.erase(at, text.find('\n', at) - at);
      }
    }
  }
  if (film_back) {
    const auto properties = text.find(R"(P: "FieldOfView")");
    text.insert(properties, R"(P: "FilmHeight", "double", "Number", "",0.9
            P: "FocalLength", "double", "Number", "",35
            )");
  }
  const auto objects
    = text.find("Objects: {") + std::string("Objects: {").size();
  text.insert(objects, R"(
    Material: 1007, "Material::Scalar", "" {
        Version: 102
        ShadingModel: "lambert"
        Properties70: {
            P: "DiffuseColor", "Color", "", "A",0.8,0.2,0.1
        }
    }
)");
  const auto connections
    = text.find("Connections: {") + std::string("Connections: {").size();
  text.insert(connections, "\n    C: \"OO\",1007,1002\n");
  return text;
}

//! FBX import settings: incomplete axes read as FBX's default, Y up, with a
//! warning; a chosen up axis replaces the file's; a stated film back sets the
//! field of view; lights scale and lights and cameras can be left out.
NOLINT_TEST_F(ModelValuesTest, FbxSettingsShapeTheCookedModel)
{
  using oxygen::content::import::SourceUpAxis;
  struct Case final {
    std::string name;
    bool axes = true;
    bool film_back = false;
    SourceUpAxis up_axis = SourceUpAxis::kFromFile;
    bool lights = true;
    bool cameras = true;
    glm::vec3 source_up {};
    bool axis_unknown = false;
  };
  for (const auto& item :
    {
      Case {
        .name = "no_axes",
        .axes = false,
        .source_up = { 0, 0, 1 },
        .axis_unknown = true,
      },
      Case {
        .name = "no_axes_z",
        .axes = false,
        .up_axis = SourceUpAxis::kZ,
        .source_up = { 0, 1, 0 },
      },
      Case {
        .name = "y_file_z",
        .up_axis = SourceUpAxis::kZ,
        .source_up = { 0, 1, 0 },
      },
      Case {
        .name = "film_back",
        .film_back = true,
        .lights = false,
        .source_up = { 0, 0, 1 },
      },
      Case { .name = "no_cameras", .cameras = false, .source_up = { 0, 0, 1 } },
    }) {
    SCOPED_TRACE(item.name);
    const auto root = MakeTempDir("fbx_settings_" + item.name);
    const auto source = root / "scene.fbx";
    {
      std::ofstream output(source);
      output << ScalarFbxText(
        TestModelsDirFromFile(), item.axes, item.film_back);
    }
    auto request = oxygen::content::import::ImportRequest {};
    request.source_path = source;
    request.asset_virtual_path = oxygen::cooker::test::ModelVirtualPath(source);
    request.cooked_root = root / "cooked";
    request.options.coordinate.up_axis = item.up_axis;
    request.options.import_lights = item.lights;
    request.options.import_cameras = item.cameras;
    request.options.light_intensity_scale = 3.0F;
    const auto imported = RunImport(std::move(request));
    ASSERT_TRUE(imported.report.success);
    EXPECT_EQ(Reports(imported.report, "fbx.axis_unknown"), item.axis_unknown);
    LoadAndVerify(imported.report, [&](const CookedModel& model) -> void {
      // The source's (0,1,0) corner, in Oxygen axes.
      EXPECT_TRUE(std::ranges::any_of(
        model.vertices, [&](const oxygen::data::Vertex& vertex) -> bool {
          return glm::distance(vertex.position, item.source_up) < 0.0001F;
        }));
      const auto cameras
        = model.scene->GetComponents<PerspectiveCameraRecord>();
      const auto lights = model.scene->GetComponents<DirectionalLightRecord>();
      EXPECT_EQ(cameras.size(), item.cameras ? 1U : 0U);
      EXPECT_EQ(lights.size(), item.lights ? 1U : 0U);
      if (!lights.empty()) {
        EXPECT_FLOAT_EQ(lights.front().intensity_lux, 3.0F);
      }
      if (!cameras.empty()) {
        const auto expected = item.film_back
          ? static_cast<float>(2.0 * std::atan((0.9 * 25.4) / (2.0 * 35.0)))
          : std::numbers::pi_v<float> / 3.0F;
        EXPECT_NEAR(cameras.front().fov_y, expected, 0.0001F);
      }
    });
  }
}

} // namespace
