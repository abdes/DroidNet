//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/Test/Pak/PakTestSupport.h>
#include <Oxygen/Cooker/Tools/Inspector/GeometryMetadata.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/MaterialSlotInventory.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace geometry = oxygen::data::pak::geometry;
using oxygen::content::inspection::RunGeometryMetadataReport;
using oxygen::content::lc::ValidateRoot;
using oxygen::data::AssetKey;
using oxygen::data::MaterialSlotId;

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

auto WriteGeometryRoot(const std::filesystem::path& root,
  const GeometryFixture& fixture = {}) -> void
{
  auto descriptor = geometry::GeometryAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
  descriptor.header.version = fixture.version;
  descriptor.lod_count = 2;
  auto bytes = std::vector<std::byte> {};
  AppendRecord(bytes, descriptor);
  const auto material
    = AssetKey::FromVirtualPath("/Content/Materials/Shared.omat");
  for (uint32_t lod = 0; lod < descriptor.lod_count; ++lod) {
    auto mesh = geometry::MeshDesc {};
    mesh.mesh_type = static_cast<uint8_t>(oxygen::data::MeshType::kStandard);
    mesh.submesh_count = 2;
    mesh.mesh_view_count = 2;
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
        .index_count = 3,
        .first_vertex = 0,
        .vertex_count = 3,
      };
      AppendRecord(bytes, view);
    }
  }
  constexpr auto virtual_path = "/Content/Geometry/Inspection.ogeo";
  oxygen::content::import::LooseCookedWriter writer(root);
  writer.WriteAssetDescriptor(AssetKey::FromVirtualPath(virtual_path),
    oxygen::data::AssetType::kGeometry, virtual_path,
    "Geometry/Inspection.ogeo", bytes, {});
  [[maybe_unused]] const auto result = writer.Finish();
}

auto ExpectedLayoutRevision() -> oxygen::base::Sha256Digest
{
  const auto material
    = AssetKey::FromVirtualPath("/Content/Materials/Shared.omat");
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
    .geometry_key
    = AssetKey::FromVirtualPath("/Content/Geometry/Inspection.ogeo"),
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
  constexpr auto virtual_path = "/Content/Scenes/Inspection.oscene";
  oxygen::content::import::LooseCookedWriter writer(root);
  writer.WriteAssetDescriptor(AssetKey::FromVirtualPath(virtual_path),
    oxygen::data::AssetType::kScene, virtual_path, "Scenes/Inspection.oscene",
    bytes, {});
  [[maybe_unused]] const auto result = writer.Finish();
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
    const auto schema_path = std::filesystem::path(__FILE__).parent_path()
      / "../../Tools/Inspector/Schemas/oxygen.cooked-geometries.schema.json";
    std::ifstream schema(schema_path);
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(schema));
    validator.validate(report);
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
