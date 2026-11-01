//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/GeometryMetadata.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/GeometryMetadata.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace geometry = oxygen::data::pak::geometry;
using oxygen::content::inspection::GeometryMetadataOptions;

//! `RunGeometryMetadataReport` as the tool runs it: the error, if any, goes to
//! stderr, and the result is the process exit code.
auto RunGeometryMetadataReport(const GeometryMetadataOptions& options) -> int
{
  return oxygen::content::toolkit::ExitCodeOf(std::cerr, [&] {
    return oxygen::content::inspection::RunGeometryMetadataReport(options);
  });
}

//! Throws when the loose cooked root `root` is not one the runtime loads.
auto ValidateRoot(const std::filesystem::path& root) -> void
{
  using oxygen::content::cooked::CookedContent;
  using oxygen::content::cooked::RealCookerStorage;
  using oxygen::content::cooked::Validate;
  Validate(CookedContent::Open(RealCookerStorage(), root));
}

using oxygen::cooker::testing::LooseAsset;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::data::AssetKey;
using oxygen::data::AssetReferences;
using oxygen::data::SurfaceId;

constexpr auto kGeometryPath = "/Content/Geometry/Inspection.ogeo";
constexpr auto kSharedMaterialPath = "/Content/Materials/Shared.omat";
constexpr uint32_t kTriangleSize = 3U;

auto MakeSurfaceId(const uint8_t value) -> SurfaceId
{
  SurfaceId::ByteArray bytes {};
  bytes.back() = value;
  return SurfaceId::FromBytes(bytes);
}

struct GeometryFixture {
  uint8_t version = geometry::kGeometryAssetVersion;
  SurfaceId first_surface = MakeSurfaceId(1);
};

template <typename T>
  requires std::is_trivially_copyable_v<T>
auto AppendRecord(std::vector<std::byte>& bytes, const T& value) -> void
{
  const auto record = std::as_bytes(std::span(&value, 1));
  bytes.insert(bytes.end(), record.begin(), record.end());
}

template <typename T, std::size_t N>
  requires std::is_trivially_copyable_v<T>
auto AppendRecords(
  std::vector<std::byte>& bytes, const std::array<T, N>& values) -> void
{
  const auto records = std::as_bytes(std::span(values));
  bytes.insert(bytes.end(), records.begin(), records.end());
}

//! Adds the buffers shared by every LOD: one triangle's vertices and indices,
//! at table entries 1 and 2 (entry 0 is the reserved fallback).
auto AddTriangleBuffers(LooseCookedRoot& library) -> void
{
  const auto vertex_data = std::array<oxygen::data::Vertex, kTriangleSize> {};
  const auto index_data = std::array<uint32_t, kTriangleSize> { 0U, 1U, 2U };
  library.AddBuffer(std::as_bytes(std::span(vertex_data)),
    {
      .usage_flags = 0x01U,
      .element_stride = sizeof(oxygen::data::Vertex),
    });
  library.AddBuffer(std::as_bytes(std::span(index_data)),
    {
      .usage_flags = 0x02U,
      .element_format = static_cast<uint8_t>(oxygen::Format::kR32UInt),
    });
}

//! The `Content` library holding the triangle buffers.
auto ContentRoot() -> LooseCookedRoot
{
  auto library = LooseCookedRoot { "Content" };
  AddTriangleBuffers(library);
  return library;
}

//! The descriptor at `virtual_path`, below the `Content` root.
auto DescriptorAsset(const std::string_view virtual_path,
  const oxygen::data::AssetType type, std::vector<std::byte> bytes,
  AssetReferences references) -> LooseAsset
{
  constexpr auto kRoot = std::string_view { "/Content/" };
  return LooseAsset {
    .type = type,
    .leaf = std::string(virtual_path.substr(kRoot.size())),
    .descriptor = std::move(bytes),
    .references = std::move(references),
  };
}

auto GeometryAsset(const GeometryFixture& fixture) -> LooseAsset
{
  auto descriptor = geometry::GeometryAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
  descriptor.header.version = fixture.version;
  descriptor.lod_count = 2;
  // Every surface is drawn by default; the second casts no shadows.
  auto surfaces = std::vector<geometry::SurfaceDesc> {
    {
      .surface_id = fixture.first_surface,
      .flags
      = geometry::kSurfaceFlag_Visible | geometry::kSurfaceFlag_CastsShadows,
    },
  };
  if (fixture.first_surface != MakeSurfaceId(2)) {
    surfaces.push_back({
      .surface_id = MakeSurfaceId(2),
      .flags = geometry::kSurfaceFlag_Visible,
    });
  }
  descriptor.surface_count = static_cast<uint32_t>(surfaces.size());
  descriptor.default_lod_policy = {
    .type = geometry::LodPolicyType::kFixed,
    .lod_index = 1,
  };
  auto bytes = std::vector<std::byte> {};
  AppendRecord(bytes, descriptor);
  auto strings = oxygen::data::DescriptorStringsBuilder {};
  const auto material = AssetKey::FromVirtualPath(kSharedMaterialPath);
  for (uint32_t lod = 0; lod < descriptor.lod_count; ++lod) {
    auto mesh = geometry::MeshDesc {};
    mesh.mesh_type = static_cast<uint8_t>(oxygen::data::MeshType::kStandard);
    mesh.submesh_count = 2;
    mesh.mesh_view_count = 2;
    mesh.info.standard.vertex_buffer
      = oxygen::data::ResourceReferenceIndex { 0U };
    mesh.info.standard.index_buffer
      = oxygen::data::ResourceReferenceIndex { 1U };
    AppendRecord(bytes, mesh);
    for (uint32_t submesh_index = 0; submesh_index < mesh.submesh_count;
      ++submesh_index) {
      auto submesh = geometry::SubMeshDesc {};
      submesh.name_offset = strings.Add("Surface");
      submesh.surface_id
        = submesh_index == 0 ? fixture.first_surface : MakeSurfaceId(2);
      submesh.material_asset_key
        = lod == 1 && submesh_index == 0 ? AssetKey {} : material;
      submesh.mesh_view_count = 1;
      AppendRecord(bytes, submesh);
      const auto view = geometry::MeshViewDesc {
        .first_index = 0,
        .index_count = kTriangleSize,
        .first_vertex = 0,
        .vertex_count = kTriangleSize,
      };
      AppendRecord(bytes, view);
    }
  }
  for (const auto& surface : surfaces) {
    AppendRecord(bytes, surface);
  }
  oxygen::data::AppendDescriptorStrings(bytes, strings);
  auto references = AssetReferences::Create(
    {
      {
        .kind = oxygen::data::ResourceKind::kBuffer,
        .index = oxygen::ResourceIndexT { 1U },
      },
      {
        .kind = oxygen::data::ResourceKind::kBuffer,
        .index = oxygen::ResourceIndexT { 2U },
      },
    },
    {
      {
        .key = material,
        .kind = oxygen::data::KeyReferenceKind::kAsset,
        .expected_type = oxygen::data::AssetType::kMaterial,
      },
    });
  return DescriptorAsset(kGeometryPath, oxygen::data::AssetType::kGeometry,
    std::move(bytes), std::move(references).value());
}

auto WriteGeometryRoot(const std::filesystem::path& root,
  const GeometryFixture& fixture = {}) -> void
{
  auto library = ContentRoot();
  library.Add(GeometryAsset(fixture));
  Materialize(library.Files(), root);
}

auto ExpectedLayoutRevision() -> oxygen::base::Sha256Digest
{
  const auto material = AssetKey::FromVirtualPath(kSharedMaterialPath);
  const auto surfaces = std::array {
    oxygen::data::Surface {
      .surface_id = MakeSurfaceId(1),
      .display_name = "Surface",
      .bindings = {
        { .lod_index = oxygen::data::LodIndex {}, .submesh_index = oxygen::data::SubmeshIndex {}, .default_material_key = material },
        { .lod_index = oxygen::data::LodIndex { 1U }, .submesh_index = oxygen::data::SubmeshIndex {}, .default_material_key = {} },
      },
      .defaults = std::nullopt,
    },
    oxygen::data::Surface {
      .surface_id = MakeSurfaceId(2),
      .display_name = "Surface",
      .bindings = {
        { .lod_index = oxygen::data::LodIndex {}, .submesh_index = oxygen::data::SubmeshIndex { 1U }, .default_material_key = material },
        { .lod_index = oxygen::data::LodIndex { 1U }, .submesh_index = oxygen::data::SubmeshIndex { 1U }, .default_material_key = material },
      },
      .defaults = std::nullopt,
    },
  };
  return oxygen::data::ComputeSurfaceLayoutRevision(surfaces).value();
}

//! Rewrites the root with the geometry and a scene overriding one of its
//! surfaces on the scene's only renderable node.
auto WriteOverrideScene(const std::filesystem::path& root,
  const oxygen::data::SurfaceId surface,
  const oxygen::base::Sha256Digest& layout_revision) -> void
{
  auto value = oxygen::data::pak::world::OverrideValue {};
  value.as_asset
    = AssetKey::FromVirtualPath("/Content/Materials/Override.omat");
  auto scene = SceneBuilder { "Inspection" };
  const auto node = scene.Node("Root");
  scene.Renderable(node, AssetKey::FromVirtualPath(kGeometryPath));
  scene.Override(node,
    oxygen::data::schema::OverrideId::kGeometrySurfaceMaterial, value, surface,
    layout_revision);
  auto library = ContentRoot();
  library.Add(GeometryAsset({}));
  library.Scene("Scenes/Inspection.oscene", scene);
  Materialize(library.Files(), root);
}

class InspectorGeometryMetadataTest : public oxygen::testing::TempDirTest {
protected:
  auto Report() -> int
  {
    return RunGeometryMetadataReport({
      .cooked_root = TempDir().string(),
      .output = TempPath("geometries.json").string(),
      .virtual_path = {},
    });
  }

  auto ReadReport() -> nlohmann::json
  {
    std::ifstream input(TempPath("geometries.json"));
    auto report = nlohmann::json::parse(input);
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Tools/Inspector/Schemas/oxygen.cooked-geometries.schema.json");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
    return report;
  }
};

NOLINT_TEST_F(InspectorGeometryMetadataTest, ReportsSeparateSurfacesAcrossLods)
{
  WriteGeometryRoot(TempDir());
  EXPECT_NO_THROW(ValidateRoot(TempDir()));
  ASSERT_EQ(Report(), 0);
  const auto report = ReadReport();
  ASSERT_EQ(report.at("geometries").size(), 1U);
  const auto& row = report.at("geometries").front();
  EXPECT_EQ(row.at("geometry_asset_key"),
    oxygen::data::to_string(
      AssetKey::FromVirtualPath("/Content/Geometry/Inspection.ogeo")));
  EXPECT_NE(row.at("layout_revision"), std::string(64, '0'));
  EXPECT_EQ(row.at("lod_policy"),
    nlohmann::json({ { "type", "fixed" }, { "lod_index", 1 } }));
  const auto& surfaces = row.at("surfaces");
  ASSERT_EQ(surfaces.size(), 2U);
  for (size_t surface_index = 0; surface_index < surfaces.size();
    ++surface_index) {
    const auto& surface = surfaces.at(surface_index);
    EXPECT_EQ(surface.at("surface_id"),
      oxygen::data::to_string(
        MakeSurfaceId(static_cast<uint8_t>(surface_index + 1))));
    EXPECT_EQ(surface.at("display_name"), "Surface");
    EXPECT_EQ(surface.at("visible"), true);
    EXPECT_EQ(surface.at("casts_shadows"), surface_index == 0);
    ASSERT_EQ(surface.at("bindings").size(), 2U);
    for (size_t lod = 0; lod < 2; ++lod) {
      const auto& binding = surface.at("bindings").at(lod);
      EXPECT_EQ(binding.at("lod_index"), lod);
      EXPECT_EQ(binding.at("submesh_index"), surface_index);
      const auto material = lod == 1 && surface_index == 0
        ? AssetKey {}
        : AssetKey::FromVirtualPath("/Content/Materials/Shared.omat");
      EXPECT_EQ(
        binding.at("default_material_key"), oxygen::data::to_string(material));
      // Each submesh draws one view of one triangle.
      EXPECT_EQ(binding.at("triangle_count"), kTriangleSize / 3U);
    }
  }
  ASSERT_EQ(Report(), 0);
  EXPECT_EQ(ReadReport(), report);
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsRetiredGeometryVersion)
{
  WriteGeometryRoot(TempDir(), { .version = 1 });
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
  EXPECT_EQ(Report(), 2);
  EXPECT_FALSE(std::filesystem::exists(TempPath("geometries.json")));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, FiltersByNativeVirtualPath)
{
  WriteGeometryRoot(TempDir());
  EXPECT_EQ(RunGeometryMetadataReport({
              .cooked_root = TempDir().string(),
              .output = TempPath("geometries.json").string(),
              .virtual_path = "/Content/Geometry/Absent.ogeo",
            }),
    0);
  EXPECT_TRUE(ReadReport().at("geometries").empty());
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsNilSurfaceIdentity)
{
  WriteGeometryRoot(TempDir(), { .first_surface = {} });
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
  EXPECT_EQ(Report(), 2);
  EXPECT_FALSE(std::filesystem::exists(TempPath("geometries.json")));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, DoesNotOverwriteSourceDescriptor)
{
  WriteGeometryRoot(TempDir());
  EXPECT_EQ(
    RunGeometryMetadataReport({
      .cooked_root = TempDir().string(),
      .output = TempPath("Descriptors/Geometry/Inspection.ogeo").string(),
      .virtual_path = {},
    }),
    2);
  EXPECT_NO_THROW(ValidateRoot(TempDir()));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, AcceptsLocalSurfaceOverride)
{
  WriteGeometryRoot(TempDir());
  WriteOverrideScene(TempDir(), MakeSurfaceId(2), ExpectedLayoutRevision());
  EXPECT_NO_THROW(ValidateRoot(TempDir()));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsUnknownLocalSurfaceOverride)
{
  WriteGeometryRoot(TempDir());
  WriteOverrideScene(TempDir(), MakeSurfaceId(3), ExpectedLayoutRevision());
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsStaleLocalSurfaceLayout)
{
  WriteGeometryRoot(TempDir());
  auto revision = ExpectedLayoutRevision();
  revision.front() ^= 1U;
  WriteOverrideScene(TempDir(), MakeSurfaceId(2), revision);
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
}

//! A pak reports the same surface inventory as the loose root it was packed
//! from.
NOLINT_TEST_F(InspectorGeometryMetadataTest, ReportsSurfacesFromAPak)
{
  // Arrange
  const auto loose = TempPath("loose");
  const auto material = oxygen::cooker::testing::MaterialDescriptor("Shared");
  auto library = ContentRoot();
  library.Add(GeometryAsset({}));
  library.Add(DescriptorAsset(kSharedMaterialPath,
    oxygen::data::AssetType::kMaterial, material.bytes, material.references));
  Materialize(library.Files(), loose);
  auto workspace = PakWorkspace {};
  workspace.PackOrThrow(workspace.FullRequest({ workspace.Add(library) }));
  const auto pak = TempPath("content.pak");
  Materialize(workspace.Storage().Bytes(PakWorkspace::PakPath("out.pak")), pak);
  ASSERT_EQ(RunGeometryMetadataReport({
              .cooked_root = loose.string(),
              .output = TempPath("loose.json").string(),
              .virtual_path = {},
            }),
    0);

  // Act
  const auto exit_code = RunGeometryMetadataReport({
    .cooked_root = pak.string(),
    .output = TempPath("geometries.json").string(),
    .virtual_path = "/Content/Geometry/Inspection.ogeo",
  });

  // Assert
  ASSERT_EQ(exit_code, 0);
  std::ifstream expected(TempPath("loose.json"));
  EXPECT_EQ(ReadReport(), nlohmann::json::parse(expected));
}

} // namespace
