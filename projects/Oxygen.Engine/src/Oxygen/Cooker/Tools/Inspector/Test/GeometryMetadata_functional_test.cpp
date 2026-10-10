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
#include <Oxygen/Cooker/Test/Pak/PakTestSupport.h>
#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Cooker/Tools/Inspector/GeometryMetadata.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/MaterialSlotInventory.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
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
using oxygen::data::MaterialSlotId;

constexpr auto kGeometryPath = "/Content/Geometry/Inspection.ogeo";
constexpr auto kSharedMaterialPath = "/Content/Materials/Shared.omat";
constexpr uint32_t kTriangleSize = 3U;

auto SlotId(const uint8_t value) -> MaterialSlotId
{
  MaterialSlotId::ByteArray bytes {};
  bytes.back() = value;
  return MaterialSlotId::FromBytes(bytes);
}

struct GeometryFixture {
  uint8_t version = geometry::kGeometryAssetVersion;
  MaterialSlotId first_slot = SlotId(1);
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
  auto bytes = std::vector<std::byte> {};
  AppendRecord(bytes, descriptor);
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
      constexpr auto label = "Surface";
      std::memcpy(submesh.name, label, std::strlen(label));
      submesh.slot_id = submesh_index == 0 ? fixture.first_slot : SlotId(2);
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
  auto references = AssetReferences::Create(
    {
      { .kind = oxygen::data::ResourceKind::kBuffer,
        .index = oxygen::ResourceIndexT { 1U } },
      { .kind = oxygen::data::ResourceKind::kBuffer,
        .index = oxygen::ResourceIndexT { 2U } },
    },
    {
      { .key = material,
        .kind = oxygen::data::KeyReferenceKind::kAsset,
        .expected_type = oxygen::data::AssetType::kMaterial },
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
  const auto slots = std::array {
    oxygen::data::MaterialSlot {
      .slot_id = SlotId(1),
      .display_name = "Surface",
      .bindings = {
        { .lod_index = oxygen::data::LodIndex {}, .submesh_index = oxygen::data::SubmeshIndex {}, .default_material_key = material },
        { .lod_index = oxygen::data::LodIndex { 1U }, .submesh_index = oxygen::data::SubmeshIndex {}, .default_material_key = {} },
      },
    },
    oxygen::data::MaterialSlot {
      .slot_id = SlotId(2),
      .display_name = "Surface",
      .bindings = {
        { .lod_index = oxygen::data::LodIndex {}, .submesh_index = oxygen::data::SubmeshIndex { 1U }, .default_material_key = material },
        { .lod_index = oxygen::data::LodIndex { 1U }, .submesh_index = oxygen::data::SubmeshIndex { 1U }, .default_material_key = material },
      },
    },
  };
  return oxygen::data::ComputeMaterialSlotLayoutRevision(slots).value();
}

//! Rewrites the root with the geometry and a scene overriding one of its
//! slots on the scene's only renderable node.
auto WriteOverrideScene(const std::filesystem::path& root,
  const oxygen::data::pak::world::MaterialOverrideRecord& assignment) -> void
{
  namespace world = oxygen::data::pak::world;
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kScene);
  descriptor.header.version = world::kSceneAssetVersion;
  descriptor.nodes = {
    .offset = sizeof(descriptor),
    .count = 1,
    .entry_size = sizeof(world::NodeRecord),
  };
  descriptor.scene_strings = {
    .offset = sizeof(descriptor) + sizeof(world::NodeRecord),
    .size = 1,
  };
  descriptor.component_table_directory_offset
    = descriptor.scene_strings.offset + 1;
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
      = static_cast<uint32_t>(oxygen::data::ComponentType::kMaterialOverride),
      .table = { .offset = records_offset + sizeof(world::RenderableRecord),
        .count = 1,
        .entry_size = sizeof(world::MaterialOverrideRecord), },
    },
  };
  auto node = world::NodeRecord {};
  node.node_id = AssetKey::FromVirtualPath("/Content/Nodes/Root");
  const auto renderable = world::RenderableRecord {
    .node_index = 0,
    .geometry_key = AssetKey::FromVirtualPath(kGeometryPath),
    .visible = 1,
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
      { .key = renderable.geometry_key,
        .kind = oxygen::data::KeyReferenceKind::kAsset,
        .expected_type = oxygen::data::AssetType::kGeometry },
      { .key = assignment.material_key,
        .kind = oxygen::data::KeyReferenceKind::kAsset,
        .expected_type = oxygen::data::AssetType::kMaterial },
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

NOLINT_TEST_F(InspectorGeometryMetadataTest, ReportsSeparateSlotsAcrossLods)
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
  const auto& slots = row.at("slots");
  ASSERT_EQ(slots.size(), 2U);
  for (size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
    const auto& slot = slots.at(slot_index);
    EXPECT_EQ(slot.at("slot_id"),
      oxygen::data::to_string(SlotId(static_cast<uint8_t>(slot_index + 1))));
    EXPECT_EQ(slot.at("display_name"), "Surface");
    ASSERT_EQ(slot.at("bindings").size(), 2U);
    for (size_t lod = 0; lod < 2; ++lod) {
      const auto& binding = slot.at("bindings").at(lod);
      EXPECT_EQ(binding.at("lod_index"), lod);
      EXPECT_EQ(binding.at("submesh_index"), slot_index);
      const auto material = lod == 1 && slot_index == 0
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

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsNilSlotIdentity)
{
  WriteGeometryRoot(Root(), { .first_slot = {} });
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

NOLINT_TEST_F(InspectorGeometryMetadataTest, AcceptsLocalSlotOverride)
{
  WriteGeometryRoot(Root());
  WriteOverrideScene(Root(),
    {
      .node_index = 0,
      .slot_id = SlotId(2),
      .material_key
      = AssetKey::FromVirtualPath("/Content/Materials/Override.omat"),
      .layout_revision = ExpectedLayoutRevision(),
    });
  EXPECT_NO_THROW(ValidateRoot(Root()));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsUnknownLocalSlotOverride)
{
  WriteGeometryRoot(Root());
  WriteOverrideScene(Root(),
    {
      .node_index = 0,
      .slot_id = SlotId(3),
      .material_key
      = AssetKey::FromVirtualPath("/Content/Materials/Override.omat"),
      .layout_revision = ExpectedLayoutRevision(),
    });
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsStaleLocalSlotLayout)
{
  WriteGeometryRoot(Root());
  auto revision = ExpectedLayoutRevision();
  revision.front() ^= 1U;
  WriteOverrideScene(Root(),
    {
      .node_index = 0,
      .slot_id = SlotId(2),
      .material_key
      = AssetKey::FromVirtualPath("/Content/Materials/Override.omat"),
      .layout_revision = revision,
    });
  EXPECT_THROW(ValidateRoot(Root()), std::runtime_error);
}

} // namespace
