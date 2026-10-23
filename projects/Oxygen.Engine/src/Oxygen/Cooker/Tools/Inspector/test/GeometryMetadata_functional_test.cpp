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
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/Tools/Inspector/GeometryMetadata.h>
#include <Oxygen/Cooker/test/Pak/PakTestSupport.h>
#include <Oxygen/Cooker/test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace geometry = oxygen::data::pak::geometry;
namespace lc = oxygen::data::loose_cooked;
using oxygen::content::inspection::RunGeometryMetadataReport;
using oxygen::content::lc::ValidateRoot;
using oxygen::content::pak::test::AssetSpec;
using oxygen::content::pak::test::FileSpec;
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

//! Buffer table and data shared by every LOD: one triangle's vertices and
//! indices, at table entries 1 and 2 (entry 0 is the reserved fallback).
auto TriangleBufferFiles() -> std::vector<FileSpec>
{
  auto table = std::array<oxygen::data::pak::core::BufferResourceDesc, 3> {};
  auto& vertices = table.at(1);
  vertices.size_bytes = kTriangleSize * sizeof(oxygen::data::Vertex);
  vertices.usage_flags = 0x01U;
  vertices.element_stride = sizeof(oxygen::data::Vertex);
  auto& indices = table.at(2);
  indices.data_offset = vertices.size_bytes;
  indices.size_bytes = kTriangleSize * sizeof(uint32_t);
  indices.usage_flags = 0x02U;
  indices.element_format = static_cast<uint8_t>(oxygen::Format::kR32UInt);

  auto table_bytes = std::vector<std::byte> {};
  AppendRecords(table_bytes, table);
  const auto vertex_data = std::array<oxygen::data::Vertex, kTriangleSize> {};
  const auto index_data = std::array<uint32_t, kTriangleSize> { 0U, 1U, 2U };
  auto data_bytes = std::vector<std::byte> {};
  AppendRecords(data_bytes, vertex_data);
  AppendRecords(data_bytes, index_data);

  const auto layout = oxygen::content::import::LooseCookedLayout {};
  return {
    FileSpec {
      .kind = lc::FileKind::kBuffersTable,
      .relpath = layout.BuffersTableRelPath(),
      .payload = std::move(table_bytes),
    },
    FileSpec {
      .kind = lc::FileKind::kBuffersData,
      .relpath = layout.BuffersDataRelPath(),
      .payload = std::move(data_bytes),
    },
  };
}

auto DescriptorAsset(const std::string_view virtual_path,
  const oxygen::data::AssetType type, const std::string_view relpath,
  std::vector<std::byte> bytes, AssetReferences references) -> AssetSpec
{
  return AssetSpec {
    .key = AssetKey::FromVirtualPath(virtual_path),
    .asset_type = type,
    .descriptor_relpath = std::string(relpath),
    .virtual_path = std::string(virtual_path),
    .descriptor_size = bytes.size(),
    .descriptor_payload = std::move(bytes),
    .references = std::move(references),
  };
}

//! Writes the root without the cooker's descriptor validation, so fixtures
//! can also hold the invalid descriptors the inspector must reject.
auto WriteRoot(const std::filesystem::path& root,
  const std::span<const AssetSpec> assets) -> void
{
  const auto files = TriangleBufferFiles();
  if (!oxygen::content::pak::test::WriteLooseIndex(root, assets, files, 1U)) {
    throw std::runtime_error("Could not write the inspection root");
  }
}

auto GeometryAsset(const GeometryFixture& fixture) -> AssetSpec
{
  auto descriptor = geometry::GeometryAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
  descriptor.header.version = fixture.version;
  descriptor.lod_count = 2;
  // Every surface is drawn by default; the second casts no shadows.
  auto surfaces = std::vector<geometry::SurfaceDesc> {
    { .surface_id = fixture.first_surface,
      .flags
      = geometry::kSurfaceFlag_Visible | geometry::kSurfaceFlag_CastsShadows },
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
    "Geometry/Inspection.ogeo", std::move(bytes),
    std::move(references).value());
}

auto WriteGeometryRoot(const std::filesystem::path& root,
  const GeometryFixture& fixture = {}) -> void
{
  const auto assets = std::array { GeometryAsset(fixture) };
  WriteRoot(root, assets);
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
  namespace world = oxygen::data::pak::world;
  auto assignment = world::OverrideRecord {
    .node_index = 0,
    .override_id = oxygen::data::schema::OverrideId::kGeometrySurfaceMaterial,
    .part_id = surface,
    .layout_revision = layout_revision,
  };
  assignment.value.as_asset
    = AssetKey::FromVirtualPath("/Content/Materials/Override.omat");
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
  descriptor.header.version = world::kSceneAssetVersion;
  descriptor.nodes = {
    .offset = sizeof(descriptor),
    .count = 1,
    .entry_size = sizeof(world::NodeRecord),
  };
  descriptor.header.strings = {
    .offset = sizeof(descriptor) + sizeof(world::NodeRecord),
    .size = 1,
  };
  descriptor.component_table_directory_offset
    = descriptor.header.strings.offset + 1;
  descriptor.component_table_count = 2;
  const auto records_offset = descriptor.component_table_directory_offset
    + (2 * sizeof(world::SceneComponentTableDesc));
  const auto directory = std::array {
    world::SceneComponentTableDesc {
      .component_type
      = static_cast<uint32_t>(oxygen::data::ComponentType::kRenderable),
      .table = { .offset = records_offset,
        .count = 1,
        .entry_size = sizeof(world::RenderableRecord), },
    },
    world::SceneComponentTableDesc {
      .component_type
      = static_cast<uint32_t>(oxygen::data::ComponentType::kOverride),
      .table = { .offset = records_offset + sizeof(world::RenderableRecord),
        .count = 1,
        .entry_size = sizeof(world::OverrideRecord), },
    },
  };
  auto node = world::NodeRecord {};
  node.node_id = AssetKey::FromVirtualPath("/Content/Nodes/Root");
  const auto renderable = world::RenderableRecord {
    .node_index = 0,
    .geometry_key = AssetKey::FromVirtualPath(kGeometryPath),
  };
  auto environment = world::SceneEnvironmentBlockHeader {};
  environment.byte_size = sizeof(environment);
  auto bytes = std::vector<std::byte> {};
  AppendRecord(bytes, descriptor);
  AppendRecord(bytes, node);
  bytes.push_back(std::byte { 0 });
  for (const auto& table : directory) {
    AppendRecord(bytes, table);
  }
  AppendRecord(bytes, renderable);
  AppendRecord(bytes, assignment);
  AppendRecord(bytes, environment);
  auto references = AssetReferences::Create({},
    {
      {
        .key = renderable.geometry_key,
        .kind = oxygen::data::KeyReferenceKind::kAsset,
        .expected_type = oxygen::data::AssetType::kGeometry,
      },
      {
        .key = assignment.value.as_asset,
        .kind = oxygen::data::KeyReferenceKind::kAsset,
        .expected_type = oxygen::data::AssetType::kMaterial,
      },
    });
  const auto assets = std::array {
    GeometryAsset({}),
    DescriptorAsset("/Content/Scenes/Inspection.oscene",
      oxygen::data::AssetType::kScene, "Scenes/Inspection.oscene",
      std::move(bytes), std::move(references).value()),
  };
  WriteRoot(root, assets);
}

class InspectorGeometryMetadataTest
  : public oxygen::content::pak::test::TempDirFixture {
protected:
  auto Report() -> int
  {
    return RunGeometryMetadataReport({
      .cooked_root = Root().string(),
      .output = Path("geometries.json").string(),
      .virtual_path = {},
    });
  }

  auto ReadReport() -> nlohmann::json
  {
    std::ifstream input(Path("geometries.json"));
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
  WriteGeometryRoot(Root());
  EXPECT_NO_THROW(ValidateRoot(Root()));
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
    }
  }
  ASSERT_EQ(Report(), 0);
  EXPECT_EQ(ReadReport(), report);
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsRetiredGeometryVersion)
{
  WriteGeometryRoot(Root(), { .version = 1 });
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
  EXPECT_EQ(Report(), 2);
  EXPECT_FALSE(std::filesystem::exists(Path("geometries.json")));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, FiltersByNativeVirtualPath)
{
  WriteGeometryRoot(Root());
  EXPECT_EQ(RunGeometryMetadataReport({
              .cooked_root = Root().string(),
              .output = Path("geometries.json").string(),
              .virtual_path = "/Content/Geometry/Absent.ogeo",
            }),
    0);
  EXPECT_TRUE(ReadReport().at("geometries").empty());
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsNilSurfaceIdentity)
{
  WriteGeometryRoot(Root(), { .first_surface = {} });
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
  EXPECT_EQ(Report(), 2);
  EXPECT_FALSE(std::filesystem::exists(Path("geometries.json")));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, DoesNotOverwriteSourceDescriptor)
{
  WriteGeometryRoot(Root());
  EXPECT_EQ(RunGeometryMetadataReport({
              .cooked_root = Root().string(),
              .output = Path("Geometry/Inspection.ogeo").string(),
              .virtual_path = {},
            }),
    2);
  EXPECT_NO_THROW(ValidateRoot(Root()));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, AcceptsLocalSurfaceOverride)
{
  WriteGeometryRoot(Root());
  WriteOverrideScene(Root(), MakeSurfaceId(2), ExpectedLayoutRevision());
  EXPECT_NO_THROW(ValidateRoot(Root()));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsUnknownLocalSurfaceOverride)
{
  WriteGeometryRoot(Root());
  WriteOverrideScene(Root(), MakeSurfaceId(3), ExpectedLayoutRevision());
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsStaleLocalSurfaceLayout)
{
  WriteGeometryRoot(Root());
  auto revision = ExpectedLayoutRevision();
  revision.front() ^= 1U;
  WriteOverrideScene(Root(), MakeSurfaceId(2), revision);
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
}

//! A pak reports the same surface inventory as the loose root it was packed
//! from.
NOLINT_TEST_F(InspectorGeometryMetadataTest, ReportsSurfacesFromAPak)
{
  // Arrange
  const auto loose = Path("loose");
  const auto material = oxygen::content::test::MaterialDescriptor("Shared");
  const auto assets = std::array {
    GeometryAsset({}),
    DescriptorAsset("/Content/Materials/Shared.omat",
      oxygen::data::AssetType::kMaterial, "Materials/Shared.omat",
      material.bytes, material.references),
  };
  WriteRoot(loose, assets);
  const auto pak = Path("content.pak");
  const auto built = oxygen::content::pak::PakBuilder {}.Build(
    oxygen::content::pak::test::MakeFullRequest(pak,
      {
        .sources = { oxygen::data::CookedSource {
          .kind = oxygen::data::CookedSourceKind::kLooseCooked,
          .path = loose,
        } },
        .embed_browse_index = true,
      }));
  ASSERT_TRUE(built.has_value());
  for (const auto& diagnostic : built->diagnostics) {
    EXPECT_NE(
      diagnostic.severity, oxygen::content::pak::PakDiagnosticSeverity::kError)
      << diagnostic.code;
  }
  ASSERT_EQ(RunGeometryMetadataReport({
              .cooked_root = loose.string(),
              .output = Path("loose.json").string(),
              .virtual_path = {},
            }),
    0);

  // Act
  const auto exit_code = RunGeometryMetadataReport({
    .cooked_root = pak.string(),
    .output = Path("geometries.json").string(),
    .virtual_path = "/Content/Geometry/Inspection.ogeo",
  });

  // Assert
  ASSERT_EQ(exit_code, 0);
  std::ifstream expected(Path("loose.json"));
  EXPECT_EQ(ReadReport(), nlohmann::json::parse(expected));
}

} // namespace
