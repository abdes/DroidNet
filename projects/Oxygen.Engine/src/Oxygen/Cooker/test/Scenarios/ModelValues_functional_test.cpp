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
#include <system_error>
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
#include <glm/trigonometric.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/GltfDocuments.h>
#include <Oxygen/Cooker/test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Cooker/test/Support/TestValues.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Lighting/LightPhotometry.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/RegionStream.h>
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
auto DecodeParseOnly(const oxygen::content::cooked::CookedContent& content,
  const oxygen::content::cooked::CookedAsset& entry, const Decode& decode)
{
  const auto descriptor_path = content.DescriptorStored(entry.key).file;
  auto file = oxygen::serio::NativeFile::Open(descriptor_path, {});
  if (!file) {
    throw std::system_error(
      file.error(), "open '" + descriptor_path.string() + "'");
  }
  auto stream = oxygen::serio::OpenReadStream(*file);
  if (!stream) {
    throw std::system_error(
      stream.error(), "size '" + descriptor_path.string() + "'");
  }
  oxygen::serio::Reader reader(*stream);
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
auto ReadCookedMeshBuffers(
  const oxygen::content::cooked::CookedContent& content, CookedModel& model)
  -> void
{
  using oxygen::data::BufferResource;
  using BufferDesc = oxygen::data::pak::core::BufferResourceDesc;
  const auto& buffers
    = content.ResourceTable(oxygen::data::ResourceKind::kBuffer);
  const auto& table_path = buffers.RowsStored().file;
  const auto& data_path = buffers.DataStored().file;
  ASSERT_FALSE(table_path.empty());
  ASSERT_FALSE(data_path.empty());
  const auto table_bytes = oxygen::cooker::test::ReadBytes(table_path);
  const auto data_bytes = oxygen::cooker::test::ReadBytes(data_path);
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
      std::vector<std::tuple<std::string, oxygen::data::AssetKey,
        oxygen::data::AssetType>>
        result;
      for (const auto& asset : inspection.Assets()) {
        result.emplace_back(asset.virtual_path, asset.key, asset.type);
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
    const auto inspection = LoadInspection(report.cooked_root);
    oxygen::content::cooked::Validate(
      inspection, oxygen::content::lc::IntegrityCheck::kFull);
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
      inspection, *scene_entry, [](const auto& context) -> auto {
        return oxygen::content::loaders::LoadSceneAsset(context);
      });
    model.geometry = DecodeParseOnly(
      inspection, *geometry_entry, [](const auto& context) -> auto {
        return oxygen::content::loaders::LoadGeometryAsset(context);
      });
    model.material = DecodeParseOnly(
      inspection, *material_entry, [](const auto& context) -> auto {
        return oxygen::content::loaders::LoadMaterialAsset(context);
      });
    ASSERT_TRUE(model.scene && model.geometry && model.material);
    ASSERT_NO_FATAL_FAILURE(ReadCookedMeshBuffers(inspection, model));
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
  request.source_mount_path
    = oxygen::cooker::test::SourceMountPath(request.source_path);
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
      // `space::texture`: the bitangent runs the way V grows over the surface
      // in the cooked UVs, whose V grows down the image; the source's V grew
      // up and was flipped on import.
      const auto e1 = vertices[1].position - vertices[0].position;
      const auto e2 = vertices[2].position - vertices[0].position;
      const auto d1 = vertices[1].texcoord - vertices[0].texcoord;
      const auto d2 = vertices[2].texcoord - vertices[0].texcoord;
      const auto v_direction = glm::normalize((e2 * d1.x) - (e1 * d2.x))
        * glm::sign((d1.x * d2.y) - (d1.y * d2.x));
      ExpectVec3Near(v_direction, { -1, 0, 0 });
      for (const auto& vertex : vertices) {
        // The geometry is in Oxygen axes: the Y-up source's +Z normal is -Y.
        ExpectVec3Near(vertex.normal, { 0, -1, 0 });
        ExpectVec3Near(vertex.tangent, { 0, 0, 1 });
        ExpectVec3Near(vertex.bitangent, v_direction);
        ExpectVec3Near(
          glm::normalize(normal_matrix * vertex.normal), { 0, -1, 0 });
        ExpectVec3Near(glm::normalize(world * vertex.tangent), { 0, 0, 1 });
        ExpectVec3Near(glm::normalize(world * vertex.bitangent), { -1, 0, 0 });
        EXPECT_NEAR(
          glm::dot(glm::cross(vertex.normal, vertex.tangent), vertex.bitangent),
          1.0F, 0.0001F);
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
        request.source_mount_path
          = oxygen::cooker::test::SourceMountPath(request.source_path);
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
        request.source_mount_path
          = oxygen::cooker::test::SourceMountPath(request.source_path);
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
            // Cameras and lights face their node's forward: the FBX camera
            // looks along its +X with +Y up, and the FBX light points down its
            // -Y.
            if (!reflected) {
              const auto camera_world
                = glm::mat3(WorldTransform(scene, cameras.front().node_index));
              ExpectVec3Near(
                glm::normalize(camera_world * oxygen::space::node::Forward),
                { 1, 0, 0 });
              ExpectVec3Near(
                glm::normalize(camera_world * oxygen::space::node::Up),
                { 0, 0, 1 });
              const auto light_world
                = glm::mat3(WorldTransform(scene, lights.front().node_index));
              ExpectVec3Near(
                glm::normalize(light_world * oxygen::space::node::Forward),
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
auto GeometryAnchor(const oxygen::content::cooked::CookedContent& inspection)
  -> std::string
{
  for (const auto& entry : inspection.Assets()) {
    if (entry.type == oxygen::data::AssetType::kGeometry) {
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
  request.source_mount_path
    = oxygen::cooker::test::SourceMountPath(request.source_path);
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
  EXPECT_TRUE(Warns(second.report,
    oxygen::content::import::import_codes::kSettingTargetMissing,
    "geometry_defaults/Gone"));
  EXPECT_TRUE(Warns(second.report,
    oxygen::content::import::import_codes::kSettingTargetMissing,
    "texture_overrides/Gone.base_color"));
  EXPECT_TRUE(Warns(second.report,
    oxygen::content::import::import_codes::kSettingTargetMissing,
    anchor + "/" + oxygen::data::to_string(gone)));
  EXPECT_TRUE(Warns(second.report,
    oxygen::content::import::import_codes::kSettingNotApplicable, "up_axis"));
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
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
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
    auto entries = std::vector<oxygen::content::cooked::CookedAsset> {};
    for (const auto& entry : LoadInspection(root / "cooked").Assets()) {
      if (entry.type == type && entry.virtual_path.starts_with(prefix + "/")) {
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
      {
        "Scalar",
        oxygen::cooker::test::SpellReference(
          oxygen::cooker::test::ModelVirtualPath(
            root / "second" / "second.gltf"),
          existing.front().virtual_path),
      },
      { "Ghost", "Ghost.omat" },
    });
  ASSERT_TRUE(second.success);
  EXPECT_TRUE(
    Warns(second, oxygen::content::import::import_codes::kSettingTargetMissing,
      "material_remap/Ghost"));
  EXPECT_TRUE(entries_under(second_path, AssetType::kMaterial).empty());
  const auto geometries = entries_under(second_path, AssetType::kGeometry);
  ASSERT_EQ(geometries.size(), 1U);
  const auto geometry = DecodeParseOnly(LoadInspection(root / "cooked"),
    geometries.front(), [](const auto& context) -> auto {
      return oxygen::content::loaders::LoadGeometryAsset(context);
    });
  ASSERT_TRUE(geometry);
  const auto& surfaces = geometry->Surfaces().surfaces;
  ASSERT_EQ(surfaces.size(), 1U);
  ASSERT_FALSE(surfaces.front().bindings.empty());
  EXPECT_EQ(surfaces.front().bindings.front().default_material_key,
    existing.front().key);

  const auto [third, third_path]
    = import_model("third", { { "Scalar", "Missing.omat" } });
  ASSERT_TRUE(third.success);
  EXPECT_TRUE(Warns(third,
    oxygen::content::import::import_codes::kMaterialRemapTargetMissing,
    "material_remap/Scalar"));
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
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
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
  request.source_mount_path
    = oxygen::cooker::test::SourceMountPath(request.source_path);
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
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = root / "cooked";
    request.options.coordinate.up_axis = item.up_axis;
    request.options.import_lights = item.lights;
    request.options.import_cameras = item.cameras;
    request.options.light_intensity_scale = 3.0F;
    const auto imported = RunImport(std::move(request));
    ASSERT_TRUE(imported.report.success);
    EXPECT_EQ(Reports(imported.report,
                oxygen::content::import::import_codes::kFbxAxisUnknown),
      item.axis_unknown);
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

//! What a node of a parity model holds.
enum class ParityKind : uint8_t {
  kEmpty,
  kMesh,
  kCamera,
  kSun,
  kLamp,
  kSpot,
};

//! A node written the same way to both formats. An FBX camera and light start
//! along other axes than glTF's: the FBX writer turns them to match.
struct ParityNode {
  std::string name;
  std::string parent;
  ParityKind kind = ParityKind::kEmpty;
  glm::vec3 translation { 0.0F };
  float rotation_z_degrees = 0.0F;
  glm::vec3 scale { 1.0F };
};

//! A spot light's full cone angles, in degrees.
struct ParityCone {
  float inner_degrees = 40.0F;
  float outer_degrees = 60.0F;
};

// One FBX `Intensity` of 100 reads as 15 lumens, and 500 as 5 lux.
constexpr auto kParityFlux = 15.0F;
constexpr auto kParitySunLux = 5.0F;

auto FbxFile(const std::string_view objects, const std::string_view connections,
  const int coord_axis_sign = 1) -> std::string
{
  auto text = std::string {
    "; FBX 7.4.0 project file\n"
    "FBXHeaderExtension: {\n    FBXHeaderVersion: 1003\n    FBXVersion: 7400\n"
    "    Creator: \"Oxygen.Engine Test Fixture\"\n}\n"
    "GlobalSettings: {\n    Version: 1000\n    Properties70: {\n"
    "        P: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
    "        P: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
    "        P: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
    "        P: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
    "        P: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
  };
  text += R"(        P: "CoordAxisSign", "int", "Integer", "",)"
    + std::to_string(coord_axis_sign) + "\n";
  text += "        P: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n"
          "    }\n}\nObjects: {\n";
  text += objects;
  text += "}\nConnections: {\n";
  text += connections;
  text += "}\n";
  return text;
}

auto FbxTriangleGeometry(
  const std::string_view polygon_vertices = "*3 { a: 0,1,-3 }") -> std::string
{
  auto text
    = std::string { "    Geometry: 1001, \"Geometry::Triangle\", \"Mesh\" {\n"
                    "        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
                    "        PolygonVertexIndex: " };
  text += polygon_vertices;
  text += "\n        LayerElementMaterial: 0 {\n"
          "            Version: 101\n"
          "            MappingInformationType: \"AllSame\"\n"
          "            ReferenceInformationType: \"IndexToDirect\"\n"
          "            Materials: *1 { a: 0 }\n        }\n"
          "        Layer: 0 {\n            Version: 100\n"
          "            LayerElement: {\n"
          "                Type: \"LayerElementMaterial\"\n"
          "                TypedIndex: 0\n            }\n        }\n    }\n";
  return text;
}

auto FbxMaterial(const std::string_view extra_properties = {}) -> std::string
{
  auto text = std::string {
    "    Material: 1003, \"Material::Scalar\", \"\" {\n"
    "        Version: 102\n        ShadingModel: \"phong\"\n"
    "        Properties70: {\n"
    "            P: \"DiffuseColor\", \"Color\", \"\", \"A\",0.8,0.2,0.1\n"
    "            P: \"DiffuseFactor\", \"Number\", \"\", \"A\",1\n"
  };
  text += extra_properties;
  text += "        }\n    }\n";
  return text;
}

auto FbxVector3(const std::string_view property, const glm::vec3 value)
  -> std::string
{
  auto text = "            P: \"" + std::string(property) + "\", \""
    + std::string(property) + R"(", "", "A",)";
  text += std::to_string(value.x) + "," + std::to_string(value.y) + ","
    + std::to_string(value.z) + "\n";
  return text;
}

auto ParityFbx(const std::vector<ParityNode>& nodes, const ParityCone cone = {})
  -> std::string
{
  constexpr auto kModelIds = 2000U;
  constexpr auto kAttributeIds = 3000U;
  auto objects = FbxTriangleGeometry() + FbxMaterial();
  auto connections = std::string {};
  const auto connect = [&](const size_t child, const size_t parent) -> void {
    connections += "    C: \"OO\"," + std::to_string(child) + ","
      + std::to_string(parent) + "\n";
  };
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes.at(i);
    const auto model_id = kModelIds + i;
    const auto attribute_id = kAttributeIds + i;

    auto rotation = glm::vec3 { 0.0F, 0.0F, node.rotation_z_degrees };
    auto attribute = std::string {};
    const auto light = [&](const int type, const int intensity,
                         const std::string_view extra) -> std::string {
      auto text = "    NodeAttribute: " + std::to_string(attribute_id)
        + ", \"NodeAttribute::" + node.name + "\", \"Light\" {\n"
        + "        TypeFlags: \"Light\"\n        Properties70: {\n"
        + R"(            P: "LightType", "enum", "", "",)"
        + std::to_string(type) + "\n"
        + "            P: \"Color\", \"ColorRGB\", \"Color\", "
          "\"\",0.8,0.7,0.6\n"
        + R"(            P: "Intensity", "double", "Number", "",)"
        + std::to_string(intensity) + "\n";
      text += extra;
      text += "        }\n    }\n";
      return text;
    };
    switch (node.kind) {
    case ParityKind::kEmpty:
      break;
    case ParityKind::kMesh:
      connect(1001, model_id);
      connect(1003, model_id);
      break;
    case ParityKind::kCamera:
      // An FBX camera looks along its +X; a quarter turn makes that glTF's -Z.
      rotation = { 0.0F, 90.0F, 0.0F };
      attribute = "    NodeAttribute: " + std::to_string(attribute_id)
        + ", \"NodeAttribute::" + node.name + "\", \"Camera\" {\n"
        + "        TypeFlags: \"Camera\"\n        Properties70: {\n"
        + "            P: \"CameraProjectionType\", \"enum\", \"\", \"\",0\n"
        + "            P: \"FieldOfView\", \"FieldOfView\", \"\", \"\",60\n"
        + "            P: \"NearPlane\", \"double\", \"Number\", \"\",0.2\n"
        + "            P: \"FarPlane\", \"double\", \"Number\", \"\",250\n"
        + "            P: \"AspectWidth\", \"double\", \"Number\", \"\",16\n"
        + "            P: \"AspectHeight\", \"double\", \"Number\", \"\",9\n"
        + "        }\n    }\n";
      break;
    case ParityKind::kSun:
      // An FBX light points down its -Y; a quarter turn makes that glTF's -Z.
      rotation = { 90.0F, 0.0F, 0.0F };
      attribute = light(1, 500, {});
      break;
    case ParityKind::kLamp:
      attribute = light(0, 100, {});
      break;
    case ParityKind::kSpot: {
      rotation = { 90.0F, 0.0F, 0.0F };
      const auto angles
        = R"(            P: "InnerAngle", "double", "Number", "",)"
        + std::to_string(cone.inner_degrees)
        + "\n            P: \"OuterAngle\", \"double\", \"Number\", \"\","
        + std::to_string(cone.outer_degrees) + "\n";
      attribute = light(2, 100, angles);
      break;
    }
    }
    if (!attribute.empty()) {
      objects += attribute;
      connect(attribute_id, model_id);
    }

    const auto model_class = [&] -> std::string_view {
      switch (node.kind) {
      case ParityKind::kEmpty:
        return "Null";
      case ParityKind::kMesh:
        return "Mesh";
      case ParityKind::kCamera:
        return "Camera";
      default:
        return "Light";
      }
    }();
    objects += "    Model: " + std::to_string(model_id)
      + ", \"Model::" + node.name + "\", \"" + std::string(model_class)
      + "\" {\n        Version: 232\n" + "        Properties70: {\n"
      + FbxVector3("Lcl Translation", node.translation)
      + FbxVector3("Lcl Rotation", rotation)
      + FbxVector3("Lcl Scaling", node.scale) + "        }\n    }\n";

    const auto parent
      = std::ranges::find(nodes, node.parent, &ParityNode::name);
    connect(model_id,
      parent == nodes.end() ? size_t { 0 }
                            : kModelIds
          + static_cast<size_t>(std::distance(nodes.begin(), parent)));
  }
  return FbxFile(objects, connections);
}

auto ParityGltf(const std::filesystem::path& models,
  const std::vector<ParityNode>& nodes, const ParityCone cone = {})
  -> nlohmann::json
{
  using nlohmann::json;
  std::ifstream input(models / "static_scalar_camera_sun.gltf");
  auto document = json::parse(input);
  document["cameras"][0]["perspective"]["yfov"]
    = std::numbers::pi_v<float> / 3.0F;

  auto lights = json::array();
  auto written = json::array();
  auto roots = json::array();
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes.at(i);
    const auto half_turn = glm::radians(node.rotation_z_degrees) / 2.0F;
    auto entry = json {
      { "name", node.name },
      {
        "translation",
        json::array(
          { node.translation.x, node.translation.y, node.translation.z }),
      },
      {
        "rotation",
        json::array({ 0.0F, 0.0F, std::sin(half_turn), std::cos(half_turn) }),
      },
      { "scale", json::array({ node.scale.x, node.scale.y, node.scale.z }) },
    };
    auto children = json::array();
    for (size_t child = 0; child < nodes.size(); ++child) {
      if (nodes.at(child).parent == node.name) {
        children.push_back(child);
      }
    }
    if (!children.empty()) {
      entry["children"] = children;
    }
    const auto add_light = [&](json light) -> void {
      light["color"] = json::array({ 0.8F, 0.7F, 0.6F });
      entry["extensions"]["KHR_lights_punctual"]["light"] = lights.size();
      lights.push_back(std::move(light));
    };
    switch (node.kind) {
    case ParityKind::kEmpty:
      break;
    case ParityKind::kMesh:
      entry["mesh"] = 0;
      break;
    case ParityKind::kCamera:
      entry["camera"] = 0;
      break;
    case ParityKind::kSun:
      add_light({ { "type", "directional" }, { "intensity", kParitySunLux } });
      break;
    case ParityKind::kLamp:
      add_light({
        { "type", "point" },
        { "intensity", kParityFlux / (4.0F * std::numbers::pi_v<float>)},
      });
      break;
    case ParityKind::kSpot: {
      const auto inner = glm::radians(cone.inner_degrees) / 2.0F;
      const auto outer = glm::radians(cone.outer_degrees) / 2.0F;
      const auto profile
        = oxygen::lighting::ResolveSpotConeProfile(inner, outer);
      add_light({
        { "type", "spot" },
        { "intensity", profile ? kParityFlux / profile->solid_angle_sr : 1.0F },
        {
          "spot",
          { { "innerConeAngle", inner }, { "outerConeAngle", outer } },
        },
      });
      break;
    }
    }
    written.push_back(std::move(entry));
    if (node.parent.empty()) {
      roots.push_back(i);
    }
  }
  document["nodes"] = written;
  document["scenes"][0]["nodes"] = roots;
  document["extensions"]["KHR_lights_punctual"]["lights"] = lights;
  return document;
}

//! A node of an imported model, found by name.
struct ImportedNode {
  std::string parent;
  oxygen::data::NodeId id;
  glm::mat4 world { 1.0F };
};

//! What two imports of the same model must agree on.
struct ImportedModel {
  std::map<std::string, ImportedNode, std::less<>> nodes;
  std::vector<glm::vec3> world_vertices;
  std::array<float, 4> base_color {};
  oxygen::data::MaterialDomain domain {};
  std::vector<PerspectiveCameraRecord> cameras;
  std::vector<glm::mat4> camera_worlds;
  std::vector<DirectionalLightRecord> suns;
  std::vector<glm::mat4> sun_worlds;
  std::vector<oxygen::data::pak::world::PointLightRecord> lamps;
  std::vector<oxygen::data::pak::world::SpotLightRecord> spots;
  std::vector<glm::mat4> spot_worlds;
  oxygen::content::import::ImportReport report;
};

//! The file format of a model a test writes.
enum class ModelFormat : uint8_t {
  kGltf,
  kFbx,
};

class ModelFormatsTest : public ModelValuesTest {
protected:
  //! The request of an import of `text`, written in `folder` as `format`, under
  //! this test's one source identity: a node's ID derives from it and its
  //! anchor.
  auto MakeRequest(const std::string_view folder, const ModelFormat format,
    const std::string_view text) -> oxygen::content::import::ImportRequest
  {
    const auto root = MakeTempDir(folder);
    const auto source
      = root / (format == ModelFormat::kFbx ? "model.fbx" : "model.gltf");
    {
      std::ofstream output(source, std::ios::binary);
      output << text;
    }
    auto request = oxygen::content::import::ImportRequest {};
    request.source_path = source;
    request.asset_virtual_path = oxygen::cooker::test::ModelVirtualPath(source);
    request.source_mount_path = oxygen::cooker::test::SourceMountPath(source);
    request.cooked_root = root / "cooked";
    request.options.coordinate.bake_transforms_into_meshes = false;
    request.surface_provenance
      = std::make_shared<const oxygen::content::import::SurfaceProvenance>(
        identity_);
    return request;
  }

  auto ImportModel(oxygen::content::import::ImportRequest request)
    -> ImportedModel
  {
    auto imported = ImportedModel {};
    imported.report = RunImport(std::move(request)).report;
    if (!imported.report.success) {
      ADD_FAILURE() << "The import failed";
      return imported;
    }
    LoadAndVerify(imported.report, [&](const CookedModel& model) -> void {
      const auto& scene = *model.scene;
      for (SceneNodeIndexT i = 0; i < scene.GetNodes().size(); ++i) {
        const auto& node = scene.GetNode(i);
        imported.nodes.emplace(std::string(scene.GetNodeName(node)),
          ImportedNode {
            .parent = node.parent_index == i
              ? std::string {}
              : std::string(
                  scene.GetNodeName(scene.GetNode(node.parent_index))),
            .id = node.node_id,
            .world = WorldTransform(scene, i),
          });
      }
      const auto renderables = scene.GetComponents<RenderableRecord>();
      ASSERT_EQ(renderables.size(), 1U);
      const auto mesh_world
        = WorldTransform(scene, renderables.front().node_index);
      for (const auto& vertex : model.vertices) {
        imported.world_vertices.emplace_back(
          mesh_world * glm::vec4(vertex.position, 1.0F));
      }
      const auto color = model.material->GetBaseColor();
      std::ranges::copy(color, imported.base_color.begin());
      imported.domain = model.material->GetMaterialDomain();
      for (const auto& camera :
        scene.GetComponents<PerspectiveCameraRecord>()) {
        imported.cameras.push_back(camera);
        imported.camera_worlds.push_back(
          WorldTransform(scene, camera.node_index));
      }
      for (const auto& sun : scene.GetComponents<DirectionalLightRecord>()) {
        imported.suns.push_back(sun);
        imported.sun_worlds.push_back(WorldTransform(scene, sun.node_index));
      }
      for (const auto& lamp :
        scene.GetComponents<oxygen::data::pak::world::PointLightRecord>()) {
        imported.lamps.push_back(lamp);
      }
      for (const auto& spot :
        scene.GetComponents<oxygen::data::pak::world::SpotLightRecord>()) {
        imported.spots.push_back(spot);
        imported.spot_worlds.push_back(WorldTransform(scene, spot.node_index));
      }
    });
    return imported;
  }

  //! Imports `nodes` once per format, in that order: glTF, FBX.
  auto ImportBothFormats(const std::string_view label,
    const std::vector<ParityNode>& nodes,
    const oxygen::content::import::NodePruningPolicy pruning
    = oxygen::content::import::NodePruningPolicy::kKeepAll,
    const ParityCone cone = {}) -> std::array<ImportedModel, 2>
  {
    auto gltf = MakeRequest(std::string(label) + "_gltf", ModelFormat::kGltf,
      ParityGltf(TestModelsDirFromFile(), nodes, cone).dump());
    auto fbx = MakeRequest(
      std::string(label) + "_fbx", ModelFormat::kFbx, ParityFbx(nodes, cone));
    gltf.options.node_pruning = pruning;
    fbx.options.node_pruning = pruning;
    return { ImportModel(std::move(gltf)), ImportModel(std::move(fbx)) };
  }

  oxygen::Uuid identity_ = oxygen::Uuid::Generate();
};

auto ExpectSameTransform(const glm::mat4& actual, const glm::mat4& expected)
  -> void
{
  for (glm::length_t column = 0; column < 4; ++column) {
    ExpectVec3Near(glm::vec3(actual[column]), glm::vec3(expected[column]));
  }
}

auto Origin(const glm::mat4& world) -> glm::vec3
{
  return { world * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F) };
}

auto Forward(const glm::mat4& world) -> glm::vec3
{
  return glm::normalize(glm::mat3(world) * oxygen::space::node::Forward);
}

//! A model imports the same from either format: the same nodes under the same
//! parents with the same IDs, at the same places, with the same values. No node
//! exists for FBX's implicit scene root.
NOLINT_TEST_F(ModelFormatsTest, SameModelImportsTheSameFromBothFormats)
{
  const auto [gltf, fbx] = ImportBothFormats("parity",
    {
      {
        .name = "Group",
        .translation = { 1.0F, 2.0F, 3.0F },
        .scale = glm::vec3 { 2.0F },
      },
      {
        .name = "Triangle",
        .parent = "Group",
        .kind = ParityKind::kMesh,
        .translation = { 1.0F, 0.0F, 0.0F },
      },
      {
        .name = "Lamp",
        .parent = "Group",
        .kind = ParityKind::kLamp,
        .translation = { 0.0F, 1.0F, 0.0F },
      },
      {
        .name = "View",
        .kind = ParityKind::kCamera,
        .translation = { 0.0F, 2.0F, 5.0F },
      },
      { .name = "Sun", .kind = ParityKind::kSun },
      {
        .name = "Beam",
        .kind = ParityKind::kSpot,
        .translation = { 3.0F, 0.0F, 0.0F },
      },
    });
  ASSERT_FALSE(gltf.nodes.empty());

  EXPECT_EQ(gltf.nodes.size(), fbx.nodes.size());
  for (const auto& [name, node] : gltf.nodes) {
    SCOPED_TRACE(name);
    const auto found = fbx.nodes.find(name);
    ASSERT_NE(found, fbx.nodes.end());
    EXPECT_EQ(found->second.parent, node.parent);
    EXPECT_EQ(found->second.id, node.id);
    ExpectVec3Near(Origin(found->second.world), Origin(node.world));
  }
  EXPECT_FALSE(fbx.nodes.contains("root"));
  EXPECT_TRUE(fbx.nodes.at("Group").parent.empty());
  ExpectSameTransform(
    fbx.nodes.at("Group").world, gltf.nodes.at("Group").world);
  ExpectSameTransform(
    fbx.nodes.at("Triangle").world, gltf.nodes.at("Triangle").world);
  // Both could agree and be wrong: source (3,2,3), Y up, lands at (3,-3,2).
  ExpectVec3Near(Origin(fbx.nodes.at("Triangle").world), { 3.0F, -3.0F, 2.0F });

  ASSERT_EQ(fbx.world_vertices.size(), gltf.world_vertices.size());
  for (const auto point : gltf.world_vertices) {
    EXPECT_TRUE(std::ranges::any_of(
      fbx.world_vertices, [&](const glm::vec3 other) -> bool {
        return glm::distance(other, point) < 0.0001F;
      }));
  }
  for (const auto& model : { std::cref(gltf), std::cref(fbx) }) {
    const auto& color = model.get().base_color;
    ExpectVec3Near({ color[0], color[1], color[2] }, { 0.8F, 0.2F, 0.1F });
    EXPECT_FLOAT_EQ(color[3], 1.0F);
  }

  ASSERT_EQ(gltf.cameras.size(), 1U);
  ASSERT_EQ(fbx.cameras.size(), 1U);
  EXPECT_NEAR(fbx.cameras.front().fov_y, gltf.cameras.front().fov_y, 0.0001F);
  EXPECT_NEAR(fbx.cameras.front().aspect_ratio,
    gltf.cameras.front().aspect_ratio, 0.0001F);
  EXPECT_NEAR(
    fbx.cameras.front().near_plane, gltf.cameras.front().near_plane, 0.0001F);
  EXPECT_NEAR(
    fbx.cameras.front().far_plane, gltf.cameras.front().far_plane, 0.0001F);
  ExpectVec3Near(
    Forward(fbx.camera_worlds.front()), Forward(gltf.camera_worlds.front()));

  ASSERT_EQ(gltf.suns.size(), 1U);
  ASSERT_EQ(fbx.suns.size(), 1U);
  EXPECT_NEAR(gltf.suns.front().intensity_lux, kParitySunLux, 0.001F);
  EXPECT_NEAR(fbx.suns.front().intensity_lux, kParitySunLux, 0.001F);
  ExpectVec3Near(
    Forward(fbx.sun_worlds.front()), Forward(gltf.sun_worlds.front()));

  ASSERT_EQ(gltf.lamps.size(), 1U);
  ASSERT_EQ(fbx.lamps.size(), 1U);
  EXPECT_NEAR(gltf.lamps.front().luminous_flux_lm, kParityFlux, 0.001F);
  EXPECT_NEAR(fbx.lamps.front().luminous_flux_lm, kParityFlux, 0.001F);
  // Neither file states a range, so both take the same policy.
  EXPECT_FLOAT_EQ(gltf.lamps.front().range, 4096.0F);
  EXPECT_FLOAT_EQ(fbx.lamps.front().range, 4096.0F);

  ASSERT_EQ(gltf.spots.size(), 1U);
  ASSERT_EQ(fbx.spots.size(), 1U);
  EXPECT_NEAR(gltf.spots.front().luminous_flux_lm, kParityFlux, 0.001F);
  EXPECT_NEAR(fbx.spots.front().luminous_flux_lm, kParityFlux, 0.001F);
  EXPECT_FLOAT_EQ(gltf.spots.front().range, 4096.0F);
  EXPECT_FLOAT_EQ(fbx.spots.front().range, 4096.0F);
  EXPECT_NEAR(fbx.spots.front().inner_cone_angle_radians,
    gltf.spots.front().inner_cone_angle_radians, 0.0001F);
  EXPECT_NEAR(fbx.spots.front().outer_cone_angle_radians,
    gltf.spots.front().outer_cone_angle_radians, 0.0001F);
  ExpectVec3Near(
    Forward(fbx.spot_worlds.front()), Forward(gltf.spot_worlds.front()));
}

//! A dropped parent takes nothing with it, in either format: a node with no
//! kept ancestor becomes a root where the source placed it.
NOLINT_TEST_F(ModelFormatsTest, PruningWithNoKeptAncestorKeepsTheWorldTransform)
{
  const auto models = ImportBothFormats("prune_root",
    {
      {
        .name = "Mover",
        .translation = { 5.0F, 0.0F, 0.0F },
      },
      {
        .name = "Triangle",
        .parent = "Mover",
        .kind = ParityKind::kMesh,
        .translation = { 0.0F, 1.0F, 0.0F },
      },
    },
    oxygen::content::import::NodePruningPolicy::kDropEmptyNodes);
  for (const auto& model : models) {
    ASSERT_TRUE(model.nodes.contains("Triangle"));
    EXPECT_FALSE(model.nodes.contains("Mover"));
    EXPECT_TRUE(model.nodes.at("Triangle").parent.empty());
    ExpectVec3Near(
      Origin(model.nodes.at("Triangle").world), { 5.0F, 0.0F, 1.0F });
  }
}

//! A kept parent the node's transform can sit under takes it; a rotated one
//! cannot, and the node becomes a root with a warning. Neither moves the node.
NOLINT_TEST_F(ModelFormatsTest, PruningUnderAKeptAncestorKeepsTheWorldTransform)
{
  using oxygen::content::import::NodePruningPolicy;
  using oxygen::content::import::import_codes::kScenePruningReparentSkipped;
  const auto nodes = [](const float rotation_z) -> std::vector<ParityNode> {
    return {
      {
        .name = "Base",
        .kind = ParityKind::kLamp,
        .translation = { 1.0F, 0.0F, 0.0F },
        .rotation_z_degrees = rotation_z,
      },
      {
        .name = "Spacer",
        .parent = "Base",
        .translation = { 0.0F, 2.0F, 0.0F },
      },
      {
        .name = "Triangle",
        .parent = "Spacer",
        .kind = ParityKind::kMesh,
        .translation = { 0.0F, 0.0F, 1.0F },
      },
    };
  };
  const auto kept = ImportBothFormats(
    "prune_kept", nodes(0.0F), NodePruningPolicy::kDropEmptyNodes);
  const auto turned = ImportBothFormats(
    "prune_turned", nodes(90.0F), NodePruningPolicy::kDropEmptyNodes);
  const auto turned_all = ImportBothFormats("prune_turned_all", nodes(90.0F));
  for (size_t format = 0; format < kept.size(); ++format) {
    SCOPED_TRACE(format);
    ASSERT_TRUE(kept.at(format).nodes.contains("Triangle"));
    EXPECT_EQ(kept.at(format).nodes.at("Triangle").parent, "Base");
    EXPECT_FALSE(Reports(kept.at(format).report, kScenePruningReparentSkipped));
    // Source (1,2,1), Y up.
    ExpectVec3Near(Origin(kept.at(format).nodes.at("Triangle").world),
      { 1.0F, -1.0F, 2.0F });

    ASSERT_TRUE(turned.at(format).nodes.contains("Triangle"));
    EXPECT_TRUE(turned.at(format).nodes.at("Triangle").parent.empty());
    EXPECT_TRUE(Warns(
      turned.at(format).report, kScenePruningReparentSkipped, "Triangle"));
    ExpectSameTransform(turned.at(format).nodes.at("Triangle").world,
      turned_all.at(format).nodes.at("Triangle").world);
    // The lamp's own frame turns none of its children: source (-1,0,1), Y up.
    ExpectVec3Near(Origin(turned_all.at(format).nodes.at("Triangle").world),
      { -1.0F, -1.0F, 0.0F });
  }
}

//! Two angles that form no cone drop the light with a warning in either format,
//! and leave no node behind for it. The model still imports.
NOLINT_TEST_F(ModelFormatsTest, SpotWithNoConeIsDroppedAndLeavesNoNode)
{
  using oxygen::content::import::import_codes::kSceneLightPhotometryInvalid;
  const auto models = ImportBothFormats("bad_cone",
    {
      { .name = "Triangle", .kind = ParityKind::kMesh },
      { .name = "Beam", .kind = ParityKind::kSpot },
    },
    oxygen::content::import::NodePruningPolicy::kKeepAll,
    ParityCone { .inner_degrees = 60.0F, .outer_degrees = 40.0F });
  for (const auto& model : models) {
    EXPECT_TRUE(model.report.success);
    EXPECT_TRUE(model.spots.empty());
    EXPECT_TRUE(Warns(model.report, kSceneLightPhotometryInvalid, "Beam"));
    EXPECT_TRUE(model.nodes.contains("Beam"));
    EXPECT_FALSE(model.nodes.contains("Beam_Light"));
  }
}

//! An FBX light's flux follows its intensity all the way: a light once brighter
//! than the old threshold must not import dimmer than a weaker one.
NOLINT_TEST_F(ModelFormatsTest, FbxLightFluxIsProportionalToIntensity)
{
  auto text = ParityFbx({
    { .name = "Triangle", .kind = ParityKind::kMesh },
    { .name = "Lamp", .kind = ParityKind::kLamp },
  });
  const auto stated
    = std::string { R"("Intensity", "double", "Number", "",100)" };
  const auto at = text.find(stated);
  ASSERT_NE(at, std::string::npos);
  text.replace(
    at, stated.size(), R"("Intensity", "double", "Number", "",2000)");
  const auto model = ImportModel(MakeRequest("flux", ModelFormat::kFbx, text));
  ASSERT_EQ(model.lamps.size(), 1U);
  EXPECT_NEAR(model.lamps.front().luminous_flux_lm, 20.0F * kParityFlux, 0.01F);
}

//! A lamp model with `properties` added to its light, in a file of
//! `unit_factor` centimeters per unit.
auto LampFbx(const std::string_view properties, const int unit_factor = 100)
  -> std::string
{
  auto text = ParityFbx({
    { .name = "Triangle", .kind = ParityKind::kMesh },
    { .name = "Lamp", .kind = ParityKind::kLamp },
  });
  const auto intensity
    = std::string { R"(P: "Intensity", "double", "Number", "",100)" };
  text.insert(
    text.find(intensity) + intensity.size(), "\n" + std::string(properties));
  const auto units
    = std::string { R"("UnitScaleFactor", "double", "Number", "",100)" };
  text.replace(text.find(units), units.size(),
    R"("UnitScaleFactor", "double", "Number", "",)"
      + std::to_string(unit_factor));
  return text;
}

//! An enabled far attenuation is the FBX light's stated range, in the file's
//! units; the option covers only a light that states none.
NOLINT_TEST_F(ModelFormatsTest, FbxFarAttenuationIsTheLightsRange)
{
  constexpr auto kFarAttenuation
    = R"(P: "EnableFarAttenuation", "bool", "", "",1
         P: "FarAttenuationEnd", "double", "Number", "",500)";
  const auto meters = ImportModel(
    MakeRequest("far_meters", ModelFormat::kFbx, LampFbx(kFarAttenuation)));
  ASSERT_EQ(meters.lamps.size(), 1U);
  EXPECT_NEAR(meters.lamps.front().range, 500.0F, 0.01F);

  const auto centimeters = ImportModel(MakeRequest(
    "far_centimeters", ModelFormat::kFbx, LampFbx(kFarAttenuation, 1)));
  ASSERT_EQ(centimeters.lamps.size(), 1U);
  EXPECT_NEAR(centimeters.lamps.front().range, 5.0F, 0.001F);

  const auto disabled = ImportModel(MakeRequest("far_disabled",
    ModelFormat::kFbx, LampFbx(R"(P: "EnableFarAttenuation", "bool", "", "",0
               P: "FarAttenuationEnd", "double", "Number", "",500)")));
  ASSERT_EQ(disabled.lamps.size(), 1U);
  EXPECT_FLOAT_EQ(disabled.lamps.front().range, 4096.0F);

  auto request = MakeRequest("omitted_option", ModelFormat::kFbx, LampFbx({}));
  request.options.omitted_light_range_m = 64.0F;
  const auto chosen = ImportModel(std::move(request));
  ASSERT_EQ(chosen.lamps.size(), 1U);
  EXPECT_FLOAT_EQ(chosen.lamps.front().range, 64.0F);
}

//! Oxygen has one falloff. A file that chose another decay learns its light
//! will differ; quadratic is that falloff, and a file that states none never
//! chose.
NOLINT_TEST_F(ModelFormatsTest, FbxDecayOtherThanQuadraticWarns)
{
  using oxygen::content::import::import_codes::kFbxLightDecayUnsupported;
  const auto import_with
    = [&](const std::string_view label,
        const std::string_view properties) -> ImportedModel {
    return ImportModel(
      MakeRequest(label, ModelFormat::kFbx, LampFbx(properties)));
  };
  for (const auto [label, decay] : {
         std::pair { "decay_none", 0 },
         std::pair { "decay_linear", 1 },
         std::pair { "decay_cubic", 3 },
       }) {
    SCOPED_TRACE(label);
    const auto model = import_with(
      label, R"(P: "DecayType", "enum", "", "",)" + std::to_string(decay));
    EXPECT_TRUE(Warns(model.report, kFbxLightDecayUnsupported, "Lamp"));
    EXPECT_EQ(model.lamps.size(), 1U);
  }
  EXPECT_FALSE(Reports(
    import_with("decay_quadratic", R"(P: "DecayType", "enum", "", "",2)")
      .report,
    kFbxLightDecayUnsupported));
  EXPECT_FALSE(Reports(
    import_with("decay_unstated", {}).report, kFbxLightDecayUnsupported));
}

//! A node with a name of its own keeps its ID, in either format, whatever else
//! the file adds, removes or reorders.
NOLINT_TEST_F(ModelFormatsTest, UniqueNamesKeepTheirIdsAcrossFileEdits)
{
  using oxygen::content::import::import_codes::kNodeNamesAmbiguous;
  const auto triangle = ParityNode {
    .name = "Triangle",
    .kind = ParityKind::kMesh,
  };
  const auto door = ParityNode { .name = "Door" };
  const auto lid = ParityNode { .name = "Lid" };
  const auto hatch = ParityNode { .name = "Hatch" };

  const auto before = ImportBothFormats("ids_before", { triangle, door, lid });
  const auto edited
    = ImportBothFormats("ids_edited", { hatch, lid, triangle, door });
  const auto removed = ImportBothFormats("ids_removed", { door, triangle });

  for (size_t format = 0; format < before.size(); ++format) {
    SCOPED_TRACE(format);
    for (const auto* name : { "Triangle", "Door", "Lid" }) {
      SCOPED_TRACE(name);
      ASSERT_TRUE(edited.at(format).nodes.contains(name));
      EXPECT_EQ(edited.at(format).nodes.at(name).id,
        before.at(format).nodes.at(name).id);
    }
    EXPECT_EQ(removed.at(format).nodes.at("Door").id,
      before.at(format).nodes.at("Door").id);
    EXPECT_FALSE(Reports(before.at(format).report, kNodeNamesAmbiguous));
    EXPECT_FALSE(Reports(edited.at(format).report, kNodeNamesAmbiguous));
  }
}

//! Same-named siblings are known by their place, in either format: the import
//! warns, and a new first sibling takes the ID the next one had. This is what
//! the warning is for.
NOLINT_TEST_F(ModelFormatsTest, SameNamedSiblingsWarnAndShiftWithTheFile)
{
  using oxygen::content::import::import_codes::kNodeNamesAmbiguous;
  const auto triangle = ParityNode {
    .name = "Triangle",
    .kind = ParityKind::kMesh,
  };
  const auto mirror = [](const float x) -> ParityNode {
    return { .name = "Mirror", .translation = { x, 0.0F, 0.0F } };
  };

  const auto before
    = ImportBothFormats("shift_before", { triangle, mirror(1), mirror(2) });
  const auto after = ImportBothFormats(
    "shift_after", { triangle, mirror(9), mirror(1), mirror(2) });

  for (size_t format = 0; format < before.size(); ++format) {
    SCOPED_TRACE(format);
    EXPECT_TRUE(Reports(before.at(format).report, kNodeNamesAmbiguous));
    const auto& first = before.at(format).nodes.at("Mirror");
    const auto& inserted = after.at(format).nodes.at("Mirror");
    const auto& moved = after.at(format).nodes.at("Mirror_1");
    EXPECT_EQ(inserted.id, first.id);
    EXPECT_NE(moved.id, first.id);
    ExpectVec3Near(Origin(moved.world), Origin(first.world));
  }
}

//! The remedy the warning gives: renamed in the file, with each new name
//! continuing the anchor it had, the siblings keep their IDs and the model
//! reports no change.
NOLINT_TEST_F(ModelFormatsTest, RenamedSiblingsContinueTheirAnchors)
{
  using oxygen::content::import::import_codes::kNodeNamesAmbiguous;
  using oxygen::content::import::import_codes::kNodesChanged;
  const auto triangle = ParityNode {
    .name = "Triangle",
    .kind = ParityKind::kMesh,
  };
  const auto before = ImportBothFormats(
    "rename_before", { triangle, { .name = "Mirror" }, { .name = "Mirror" } });
  const auto renamed = std::vector<ParityNode> {
    triangle,
    { .name = "Mirror_L" },
    { .name = "Mirror_R" },
  };

  for (const auto format : { ModelFormat::kGltf, ModelFormat::kFbx }) {
    const auto index = static_cast<size_t>(format);
    SCOPED_TRACE(index);
    const auto& first = before.at(index);
    auto request = MakeRequest("rename_after_" + std::to_string(index), format,
      format == ModelFormat::kFbx
        ? ParityFbx(renamed)
        : ParityGltf(TestModelsDirFromFile(), renamed).dump());
    request.model_import = oxygen::content::import::ModelImportInputs {
      .node_continuity = {
        { .anchor = "Mirror_L", .continues = "Mirror" },
        { .anchor = "Mirror_R", .continues = "Mirror[2]" },
      },
      .published_nodes = first.report.published_nodes,
      .vanished_nodes = first.report.vanished_nodes,
    };

    const auto after = ImportModel(std::move(request));

    EXPECT_EQ(after.nodes.at("Mirror_L").id, first.nodes.at("Mirror").id);
    EXPECT_EQ(after.nodes.at("Mirror_R").id, first.nodes.at("Mirror_1").id);
    EXPECT_TRUE(after.report.new_nodes.empty());
    EXPECT_TRUE(after.report.vanished_nodes.empty());
    EXPECT_FALSE(Reports(after.report, kNodesChanged));
    EXPECT_FALSE(Reports(after.report, kNodeNamesAmbiguous));
  }
}

//! A node that ignores its parent's scale keeps its own, whether the parent's
//! scale is uniform, which a compensating scale undoes, or not, which takes a
//! helper node. The helper changes no node's identity.
NOLINT_TEST_F(ModelFormatsTest, FbxInheritModesPlaceNodesAsAuthored)
{
  const auto import_with = [&](const std::string_view label,
                             const glm::vec3 parent_scale) -> ImportedModel {
    auto objects = FbxTriangleGeometry() + FbxMaterial();
    const auto model = [&](const int id, const std::string_view name,
                         const glm::vec3 translation, const glm::vec3 scale,
                         const std::string_view extra) -> std::string {
      auto text = "    Model: " + std::to_string(id)
        + ", \"Model::" + std::string(name)
        + "\", \"Mesh\" {\n        Version: 232\n" + "        Properties70: {\n"
        + FbxVector3("Lcl Translation", translation)
        + FbxVector3("Lcl Scaling", scale);
      text += extra;
      text += "        }\n    }\n";
      return text;
    };
    objects += model(2000, "Parent", glm::vec3 { 0.0F }, parent_scale, {});
    objects
      += model(2001, "Compensated", { 1.0F, 0.0F, 0.0F }, glm::vec3 { 1.0F },
        "            P: \"InheritType\", \"enum\", \"\", \"\",2\n");
    objects
      += model(2002, "Plain", { 1.0F, 0.0F, 0.0F }, glm::vec3 { 1.0F }, {});
    return ImportModel(MakeRequest(label, ModelFormat::kFbx,
      FbxFile(objects,
        "    C: \"OO\",2000,0\n    C: \"OO\",2001,2000\n"
        "    C: \"OO\",2002,2000\n    C: \"OO\",1001,2001\n"
        "    C: \"OO\",1003,2001\n")));
  };
  const auto uniform = import_with("inherit_uniform", glm::vec3 { 2.0F });
  const auto uneven = import_with("inherit_uneven", { 2.0F, 3.0F, 4.0F });

  const auto scale_x = [](const glm::mat4& world) -> float {
    return glm::length(glm::vec3(world[0]));
  };
  for (const auto& model : { std::cref(uniform), std::cref(uneven) }) {
    const auto& nodes = model.get().nodes;
    ASSERT_TRUE(nodes.contains("Compensated"));
    ASSERT_TRUE(nodes.contains("Plain"));
    EXPECT_NEAR(scale_x(nodes.at("Compensated").world), 1.0F, 0.0001F);
    EXPECT_NEAR(scale_x(nodes.at("Plain").world), 2.0F, 0.0001F);
    // The parent's scale still moves a child that ignores it.
    ExpectVec3Near(Origin(nodes.at("Compensated").world), { 2.0F, 0.0F, 0.0F });
    ExpectVec3Near(Origin(nodes.at("Plain").world), { 2.0F, 0.0F, 0.0F });
  }
  EXPECT_EQ(uniform.nodes.at("Plain").id, uneven.nodes.at("Plain").id);
  EXPECT_EQ(
    uniform.nodes.at("Compensated").id, uneven.nodes.at("Compensated").id);
}

//! A classic FBX material states transparency as a factor of a colour. Opacity
//! under 1 blends; the common spellings of an opaque material stay opaque, and
//! one that claims to be fully invisible warns and stays opaque.
NOLINT_TEST_F(ModelFormatsTest, FbxOpacityReachesTheMaterial)
{
  using oxygen::content::import::import_codes::kFbxMaterialFullyTransparent;
  using oxygen::data::MaterialDomain;
  const auto transparency = [](const std::string_view factor,
                              const std::string_view color) -> std::string {
    return R"(            P: "TransparencyFactor", "Number", "", "A",)"
      + std::string(factor)
      + "\n            P: \"TransparentColor\", \"Color\", \"\", \"A\","
      + std::string(color) + "\n";
  };
  const auto import_with = [&](const std::string_view label,
                             const std::string& properties) -> ImportedModel {
    return ImportModel(MakeRequest(label, ModelFormat::kFbx,
      FbxFile(FbxTriangleGeometry() + FbxMaterial(properties)
          + "    Model: 2000, \"Model::Triangle\", \"Mesh\" {\n"
            "        Version: 232\n    }\n",
        "    C: \"OO\",2000,0\n    C: \"OO\",1001,2000\n"
        "    C: \"OO\",1003,2000\n")));
  };

  const auto glass = import_with("glass", transparency("0.25", "1,1,1"));
  EXPECT_NEAR(glass.base_color[3], 0.75F, 0.0001F);
  EXPECT_EQ(glass.domain, MaterialDomain::kAlphaBlended);
  EXPECT_NEAR(glass.base_color[0], 0.8F, 0.0001F);

  const auto stated_opaque
    = import_with("stated_opaque", transparency("1", "0,0,0"));
  EXPECT_FLOAT_EQ(stated_opaque.base_color[3], 1.0F);
  EXPECT_EQ(stated_opaque.domain, MaterialDomain::kOpaque);
  EXPECT_FALSE(Reports(stated_opaque.report, kFbxMaterialFullyTransparent));

  const auto silent = import_with("silent", {});
  EXPECT_FLOAT_EQ(silent.base_color[3], 1.0F);
  EXPECT_EQ(silent.domain, MaterialDomain::kOpaque);

  const auto invisible = import_with("invisible", transparency("1", "1,1,1"));
  EXPECT_FLOAT_EQ(invisible.base_color[3], 1.0F);
  EXPECT_EQ(invisible.domain, MaterialDomain::kOpaque);
  EXPECT_TRUE(Warns(invisible.report, kFbxMaterialFullyTransparent, "Scalar"));
}

//! A chosen up axis reads the file as right-handed. A left-handed file then
//! imports mirrored, which only the warning can tell the author; its own axes
//! import it correctly.
NOLINT_TEST_F(ModelFormatsTest, ChosenUpAxisWarnsThatItMirrorsALeftHandedFile)
{
  using oxygen::content::import::SourceUpAxis;
  using oxygen::content::import::import_codes::kFbxLeftHandedAxesReplaced;
  const auto objects = FbxTriangleGeometry() + FbxMaterial()
    + "    Model: 2000, \"Model::Triangle\", \"Mesh\" {\n"
      "        Version: 232\n    }\n";
  const auto connections = std::string {
    "    C: \"OO\",2000,0\n    C: \"OO\",1001,2000\n    C: \"OO\",1003,2000\n"
  };
  const auto import_with = [&](const std::string_view label, const int sign,
                             const SourceUpAxis up) -> ImportedModel {
    auto request = MakeRequest(
      label, ModelFormat::kFbx, FbxFile(objects, connections, sign));
    request.options.coordinate.up_axis = up;
    return ImportModel(std::move(request));
  };
  EXPECT_TRUE(Reports(import_with("left_chosen", -1, SourceUpAxis::kY).report,
    kFbxLeftHandedAxesReplaced));
  EXPECT_FALSE(
    Reports(import_with("left_own", -1, SourceUpAxis::kFromFile).report,
      kFbxLeftHandedAxesReplaced));
  EXPECT_FALSE(Reports(import_with("right_chosen", 1, SourceUpAxis::kY).report,
    kFbxLeftHandedAxesReplaced));
}

//! Bad faces are counted, not reported one by one, and a face with no triangles
//! leaves nothing behind. A mesh that binds no material has nothing to warn of.
NOLINT_TEST_F(ModelFormatsTest, FbxBadFacesWarnOnceAndNoMaterialIsNotADefect)
{
  using oxygen::content::import::import_codes::kMeshFaceMaterialMissing;
  using oxygen::content::import::import_codes::kMeshInvalidFace;
  const auto imported = RunImport(MakeRequest("bad_faces", ModelFormat::kFbx,
    FbxFile("    Geometry: 1001, \"Geometry::Triangle\", \"Mesh\" {\n"
            "        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
            "        PolygonVertexIndex: *7 { a: 0,-2,0,1,-3,1,-3 }\n    }\n"
            "    Model: 2000, \"Model::Triangle\", \"Mesh\" {\n"
            "        Version: 232\n    }\n",
      "    C: \"OO\",2000,0\n    C: \"OO\",1001,2000\n")));
  ASSERT_TRUE(imported.report.success);
  const auto invalid = std::ranges::count_if(
    imported.report.diagnostics, [](const auto& diagnostic) -> bool {
      return diagnostic.code == kMeshInvalidFace;
    });
  EXPECT_EQ(invalid, 1);
  EXPECT_FALSE(Reports(imported.report, kMeshFaceMaterialMissing));
}

//! glTF's own defaults hold where the engine's differ: an RGB colour attribute
//! is opaque, a material with no PBR block is metallic and rough, and the
//! lowest UV set is the first whatever the file's order.
NOLINT_TEST_F(ModelFormatsTest, GltfDefaultsAreGltfs)
{
  using nlohmann::json;
  namespace test = oxygen::content::import::test;
  auto bytes = std::vector<unsigned char> {};
  const auto add = [&](const auto& values) -> std::size_t {
    return test::AppendAligned(bytes, std::as_bytes(std::span(values)));
  };
  const auto positions
    = std::array { 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F };
  const auto colors
    = std::array { 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F };
  const auto uv_first = std::array { 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F };
  const auto uv_second
    = std::array { 0.75F, 0.75F, 0.75F, 0.75F, 0.75F, 0.75F };
  auto views = json::array();
  auto accessors = json::array();
  const auto describe = [&](const std::size_t offset, const std::size_t size,
                          const std::string_view type) -> void {
    views.push_back(
      { { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", size } });
    accessors.push_back({
      { "bufferView", views.size() - 1 },
      { "componentType", 5126 },
      { "type", type },
      { "count", 3 },
    });
  };
  describe(add(positions), sizeof(positions), "VEC3");
  describe(add(colors), sizeof(colors), "VEC3");
  describe(add(uv_first), sizeof(uv_first), "VEC2");
  describe(add(uv_second), sizeof(uv_second), "VEC2");
  const auto document = json {
    { "asset", { { "version", "2.0" } } },
    {
      "buffers",
      json::array({
        {
          { "byteLength", bytes.size() },
          { "uri", test::DataUri(bytes) },
        },
      }),
    },
    { "bufferViews", views },
    { "accessors", accessors },
    { "materials", json::array({ { { "name", "Paint" } } }) },
    {
      "meshes",
      json::array({
        {
          { "name", "Triangle" },
          {
            "primitives",
            json::array({
              {
                {
                  "attributes",
                  {
                    { "POSITION", 0 },
                    { "COLOR_0", 1 },
                    { "TEXCOORD_1", 3 },
                    { "TEXCOORD_0", 2 },
                  },
                },
                { "material", 0 },
              },
            }),
          },
        },
      }),
    },
    { "nodes", json::array({ { { "name", "Triangle" }, { "mesh", 0 } } }) },
    { "scenes", json::array({ { { "nodes", json::array({ 0 }) } } }) },
    { "scene", 0 },
  };
  const auto imported = RunImport(
    MakeRequest("gltf_defaults", ModelFormat::kGltf, document.dump()));
  ASSERT_TRUE(imported.report.success);
  LoadAndVerify(imported.report, [](const CookedModel& model) -> void {
    ASSERT_FALSE(model.vertices.empty());
    for (const auto& vertex : model.vertices) {
      EXPECT_NEAR(vertex.color.r, 0.5F, 0.0001F);
      EXPECT_FLOAT_EQ(vertex.color.a, 1.0F);
      EXPECT_NEAR(vertex.texcoord.x, 0.25F, 0.0001F);
    }
    EXPECT_NEAR(model.material->GetMetalness(), 1.0F, 0.001F);
    EXPECT_NEAR(model.material->GetRoughness(), 1.0F, 0.001F);
  });
}

//! In glTF, a skin without inverse bind matrices means identity matrices: the
//! mesh stays skinned. Dropping a skin always warns.
NOLINT_TEST_F(ModelFormatsTest, GltfSkinWithoutInverseBindMatricesStaysSkinned)
{
  using nlohmann::json;
  using oxygen::content::import::import_codes::kMeshMissingInverseBind;
  namespace test = oxygen::content::import::test;
  auto bytes = std::vector<unsigned char> {};
  const auto positions
    = std::array { 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F };
  const auto weights = std::array {
    1.0F,
    0.0F,
    0.0F,
    0.0F,
    1.0F,
    0.0F,
    0.0F,
    0.0F,
    1.0F,
    0.0F,
    0.0F,
    0.0F,
  };
  const auto joints = std::array<unsigned char, 12> {};
  const auto positions_at
    = test::AppendAligned(bytes, std::as_bytes(std::span(positions)));
  const auto weights_at
    = test::AppendAligned(bytes, std::as_bytes(std::span(weights)));
  const auto joints_at
    = test::AppendAligned(bytes, std::as_bytes(std::span(joints)));
  const auto document = json {
    { "asset", { { "version", "2.0" } } },
    {
      "buffers",
      json::array({
        {
          { "byteLength", bytes.size() },
          { "uri", test::DataUri(bytes) },
        },
      }),
    },
    {
      "bufferViews",
      json::array({
        {
          { "buffer", 0 },
          { "byteOffset", positions_at },
          { "byteLength", sizeof(positions) },
        },
        {
          { "buffer", 0 },
          { "byteOffset", weights_at },
          { "byteLength", sizeof(weights) },
        },
        {
          { "buffer", 0 },
          { "byteOffset", joints_at },
          { "byteLength", sizeof(joints) },
        },
      }),
    },
    {
      "accessors",
      json::array({
        {
          { "bufferView", 0 },
          { "componentType", 5126 },
          { "type", "VEC3" },
          { "count", 3 },
        },
        {
          { "bufferView", 1 },
          { "componentType", 5126 },
          { "type", "VEC4" },
          { "count", 3 },
        },
        {
          { "bufferView", 2 },
          { "componentType", 5121 },
          { "type", "VEC4" },
          { "count", 3 },
        },
      }),
    },
    { "materials", json::array({ { { "name", "Paint" } } }) },
    {
      "meshes",
      json::array({
        {
          { "name", "Triangle" },
          {
            "primitives",
            json::array({
              {
                {
                  "attributes",
                  { { "POSITION", 0 }, { "WEIGHTS_0", 1 }, { "JOINTS_0", 2 } },
                },
                { "material", 0 },
              },
            }),
          },
        },
      }),
    },
    { "skins", json::array({ { { "joints", json::array({ 1 }) } } }) },
    {
      "nodes",
      json::array({
        { { "name", "Triangle" }, { "mesh", 0 }, { "skin", 0 } },
        { { "name", "Bone" } },
      }),
    },
    { "scenes", json::array({ { { "nodes", json::array({ 0, 1 }) } } }) },
    { "scene", 0 },
  };
  const auto imported
    = RunImport(MakeRequest("gltf_skin", ModelFormat::kGltf, document.dump()));
  EXPECT_TRUE(imported.report.success);
  EXPECT_FALSE(Reports(imported.report, kMeshMissingInverseBind));
}

} // namespace
