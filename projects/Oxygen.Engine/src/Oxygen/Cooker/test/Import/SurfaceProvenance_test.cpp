//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/SurfaceProvenance.cpp,
//   Import/Internal/SurfaceAllocation.cpp

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/SurfaceAllocation.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::AllocatedSurfaces;
using oxygen::content::import::AllocateSurfaces;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportSeverity;
using oxygen::content::import::MeshBuildPipeline;
using oxygen::content::import::MeshLod;
using oxygen::content::import::SurfaceGeometryProvenance;
using oxygen::content::import::SurfaceProvenance;
using oxygen::content::import::TriangleRange;
using oxygen::data::SurfaceId;
namespace import_codes = oxygen::content::import::import_codes;

constexpr auto kSourceIdentity = "01900000-0000-7000-8000-000000000001";

auto Identity() -> oxygen::Uuid
{
  return oxygen::Uuid::FromString(kSourceIdentity).value();
}

//! A geometry with one triangle per source slot.
struct Source final {
  std::array<glm::vec3, 3> positions {
    glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 1.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 1.0F, 0.0F },
  };
  std::vector<uint32_t> indices;
  std::vector<TriangleRange> ranges;

  //! `materials` holds each slot's source material, as an adapter declares it.
  [[nodiscard]] auto MakeRequest(
    const std::vector<std::optional<std::string>>& materials
    = { "Paint", "Trim" }) -> MeshBuildPipeline::WorkItem
  {
    indices.clear();
    ranges.clear();
    auto item = MeshBuildPipeline::WorkItem {};
    for (uint32_t slot = 0; slot < materials.size(); ++slot) {
      ranges.push_back({
        .surface = 0U,
        .source_slot = slot,
        .first_index = static_cast<uint32_t>(indices.size()),
        .index_count = 3U,
      });
      indices.insert(indices.end(), { 0U, 1U, 2U });
      item.surface_materials.emplace(slot, materials.at(slot));
    }
    item.storage_mesh_name = "retained-mesh";
    item.request.surface_provenance
      = std::make_shared<const SurfaceProvenance>(Identity());
    auto lod = MeshLod {};
    lod.source.streams.positions = positions;
    lod.source.indices = indices;
    lod.source.ranges = ranges;
    item.lods.push_back(std::move(lod));
    return item;
  }
};

auto Allocate(const MeshBuildPipeline::WorkItem& item) -> AllocatedSurfaces
{
  auto diagnostics = std::vector<ImportDiagnostic> {};
  return AllocateSurfaces(item, diagnostics);
}

auto Publish(SurfaceGeometryProvenance geometry)
  -> std::shared_ptr<const SurfaceProvenance>
{
  uint32_t submesh = 0U;
  for (const auto& [declaration, id] : geometry.allocations) {
    static_cast<void>(declaration);
    geometry.inventory.surfaces.push_back(oxygen::data::Surface {
      .surface_id = id,
      .display_name = "same-label",
      .bindings = { { .lod_index = oxygen::data::LodIndex {},
        .submesh_index = oxygen::data::SubmeshIndex { submesh++ },
        .default_material_key = {}, }, },
      .defaults = std::nullopt,
    });
  }
  geometry.inventory.layout_revision
    = oxygen::data::ComputeSurfaceLayoutRevision(geometry.inventory.surfaces)
        .value();
  std::vector<SurfaceGeometryProvenance> geometries;
  geometries.push_back(std::move(geometry));
  return std::make_shared<const SurfaceProvenance>(
    Identity(), std::move(geometries));
}

//! The request for `materials`, against what `item` last allocated.
auto AfterImportOf(const MeshBuildPipeline::WorkItem& item,
  const std::vector<std::optional<std::string>>& materials, Source& source)
  -> MeshBuildPipeline::WorkItem
{
  // `item` views `source`, which the next request rewrites.
  auto recorded = Publish(Allocate(item).provenance);
  auto next = source.MakeRequest(materials);
  next.request.surface_provenance = std::move(recorded);
  return next;
}

//! A surface is known by its source material: its name, escaped; a name that
//! repeats, numbered in slot order; a material without a name by its place
//! among those; and no material as such.
NOLINT_TEST(SurfaceProvenanceTest, KeysFollowTheSourceMaterial)
{
  auto source = Source {};
  const auto allocated = Allocate(source.MakeRequest({
    "Paint",
    "Paint",
    "",
    "",
    std::nullopt,
    "a[1]#b",
    "Paint[2]",
  }));

  auto keys = std::vector<std::string> {};
  for (const auto& [key, id] : allocated.provenance.allocations) {
    static_cast<void>(id);
    keys.push_back(key);
  }
  EXPECT_THAT(keys,
    testing::UnorderedElementsAre("Paint", "Paint[2]", "#unnamed[1]",
      "#unnamed[2]", "#unassigned", "a\\[1]\\#b", "Paint\\[2]"));
  EXPECT_EQ(allocated.ids_by_slot.size(), keys.size());
  EXPECT_EQ(allocated.ids_by_slot.at(1U),
    allocated.provenance.allocations.at("Paint[2]"));
}

//! Materials of one name in one mesh are separate surfaces, each with an ID
//! that the next import gives it again.
NOLINT_TEST(SurfaceProvenanceTest, DuplicateNamesStayDistinct)
{
  auto source = Source {};
  const auto materials
    = std::vector<std::optional<std::string>> { "Paint", "Paint" };
  const auto first = Allocate(source.MakeRequest(materials));
  const auto second = Allocate(source.MakeRequest(materials));
  EXPECT_EQ(first.provenance.allocations, second.provenance.allocations);
  ASSERT_EQ(first.ids_by_slot.size(), 2U);
  EXPECT_NE(first.ids_by_slot.at(0U), first.ids_by_slot.at(1U));
}

//! A recorded ID is what counts, whatever derived it: only the record crosses
//! from one import to the next.
NOLINT_TEST(SurfaceProvenanceTest, RecordedIdsSurviveCacheDeletion)
{
  auto source = Source {};
  auto first = Allocate(source.MakeRequest());
  const auto retained_id
    = SurfaceId::FromStableIdentity("older-native-allocation-policy");
  first.provenance.allocations.at("Trim") = retained_id;
  const auto persisted = Publish(std::move(first.provenance))->Serialize();

  auto recreated = source.MakeRequest();
  recreated.source_id = "different-machine-and-producer";
  recreated.mesh_name = "renamed-display-label";
  recreated.request.surface_provenance
    = SurfaceProvenance::Parse(persisted, Identity());
  EXPECT_EQ(Allocate(recreated).ids_by_slot.at(1U), retained_id);
}

//! Neither the geometry's content nor what its materials resolve to is part of
//! a surface's identity: nothing in it changed, so nothing is reported.
NOLINT_TEST(SurfaceProvenanceTest, EditsThatKeepTheMaterialsKeepEveryId)
{
  auto source = Source {};
  auto item = source.MakeRequest();
  const auto first = Allocate(item);
  item.request.surface_provenance = Publish(first.provenance);

  item.material_keys.push_back(
    oxygen::data::AssetKey::FromVirtualPath("/changed.omat"));
  item.surface_names.emplace(0U, "new label");
  auto converted = source.positions;
  for (auto& position : converted) {
    position *= 10.0F;
  }
  item.lods.front().source.streams.positions = converted;
  std::swap(source.indices.at(0), source.indices.at(1));

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto after = AllocateSurfaces(item, diagnostics);
  EXPECT_EQ(after.provenance.allocations, first.provenance.allocations);
  EXPECT_TRUE(diagnostics.empty());
}

//! The slots may come in another order: a surface follows its name.
NOLINT_TEST(SurfaceProvenanceTest, ReorderedSlotsKeepTheirIds)
{
  auto source = Source {};
  const auto item = source.MakeRequest({ "Paint", "Trim" });
  const auto first = Allocate(item);

  const auto reordered = AfterImportOf(item, { "Trim", "Paint" }, source);
  const auto after = Allocate(reordered);

  EXPECT_EQ(after.ids_by_slot.at(0U), first.ids_by_slot.at(1U));
  EXPECT_EQ(after.ids_by_slot.at(1U), first.ids_by_slot.at(0U));
}

//! One name gone and one new is a rename, which keeps the ID and says so.
NOLINT_TEST(SurfaceProvenanceTest, ARenamedMaterialKeepsItsId)
{
  auto source = Source {};
  const auto item = source.MakeRequest({ "Paint", "Material.001" });
  const auto first = Allocate(item);

  const auto renamed = AfterImportOf(item, { "Paint", "Trim" }, source);
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto after = AllocateSurfaces(renamed, diagnostics);

  EXPECT_EQ(after.ids_by_slot, first.ids_by_slot);
  EXPECT_TRUE(after.provenance.allocations.contains("Trim"));
  EXPECT_FALSE(after.provenance.allocations.contains("Material.001"));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, import_codes::kSurfacesChanged);
  EXPECT_EQ(diagnostics.front().severity, ImportSeverity::kInfo);
  EXPECT_THAT(diagnostics.front().message,
    testing::HasSubstr("'Material.001' to 'Trim'"));
}

//! With two names gone, nothing says which new name is which: each gets a new
//! ID, and the change is reported.
NOLINT_TEST(SurfaceProvenanceTest, TwoMaterialsRenamedAtOnceGetNewIds)
{
  auto source = Source {};
  const auto item = source.MakeRequest({ "Paint", "Trim" });
  const auto first = Allocate(item);

  const auto renamed = AfterImportOf(item, { "Body", "Rim" }, source);
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto after = AllocateSurfaces(renamed, diagnostics);

  EXPECT_NE(after.ids_by_slot.at(0U), first.ids_by_slot.at(0U));
  EXPECT_NE(after.ids_by_slot.at(1U), first.ids_by_slot.at(0U));
  EXPECT_NE(after.ids_by_slot.at(1U), first.ids_by_slot.at(1U));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_THAT(diagnostics.front().message,
    testing::AllOf(testing::HasSubstr("Vanished: 'Paint', 'Trim'"),
      testing::HasSubstr("New: 'Body', 'Rim'")));
}

//! A new slot takes nothing from the others, and a slot that goes takes its ID
//! with it until its name returns.
NOLINT_TEST(SurfaceProvenanceTest, AddingAndRemovingSlotsKeepsTheOthers)
{
  auto source = Source {};
  const auto item = source.MakeRequest({ "Paint", "Trim" });
  const auto first = Allocate(item);

  const auto removed = AfterImportOf(item, { "Trim" }, source);
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto without = AllocateSurfaces(removed, diagnostics);
  EXPECT_EQ(without.ids_by_slot.at(0U), first.ids_by_slot.at(1U));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_THAT(
    diagnostics.front().message, testing::HasSubstr("Vanished: 'Paint'"));

  const auto returned = AfterImportOf(removed, { "Trim", "Paint" }, source);
  const auto with = Allocate(returned);
  EXPECT_EQ(with.ids_by_slot.at(0U), first.ids_by_slot.at(1U));
  EXPECT_EQ(with.ids_by_slot.at(1U), first.ids_by_slot.at(0U));
}

//! A surface renamed from a name holds the ID that name derives, so a new
//! material of that name gets another.
NOLINT_TEST(SurfaceProvenanceTest, ANameThatReturnsAfterARenameGetsAnotherId)
{
  auto source = Source {};
  const auto item = source.MakeRequest({ "Paint" });
  const auto first = Allocate(item);
  const auto renamed = AfterImportOf(item, { "Car" }, source);
  ASSERT_EQ(Allocate(renamed).ids_by_slot.at(0U), first.ids_by_slot.at(0U));

  const auto again = AfterImportOf(renamed, { "Car", "Paint" }, source);
  const auto after = Allocate(again);

  EXPECT_EQ(after.ids_by_slot.at(0U), first.ids_by_slot.at(0U));
  EXPECT_NE(after.ids_by_slot.at(1U), first.ids_by_slot.at(0U));
}

//! A first import has nothing to compare, so reports nothing.
NOLINT_TEST(SurfaceProvenanceTest, AFirstImportReportsNoChange)
{
  auto source = Source {};
  auto diagnostics = std::vector<ImportDiagnostic> {};
  static_cast<void>(AllocateSurfaces(source.MakeRequest(), diagnostics));
  EXPECT_TRUE(diagnostics.empty());
}

//! An adapter declares every slot its ranges use.
NOLINT_TEST(SurfaceProvenanceTest, ASlotWithNoDeclarationIsRejected)
{
  auto source = Source {};
  auto item = source.MakeRequest();
  item.surface_materials.erase(1U);
  EXPECT_THROW(static_cast<void>(Allocate(item)), std::invalid_argument);
}

NOLINT_TEST(
  SurfaceProvenanceTest, CorruptedRetainedInventoryIsRejectedAtBoundary)
{
  auto source = Source {};
  auto document = nlohmann::json::parse(
    Publish(Allocate(source.MakeRequest()).provenance)->Serialize());
  document.at("geometries")
    .at(0)
    .at("surfaces")
    .at(0)
    .at("bindings")
    .at(0)
    .update({ { "submesh_index", 7U } });
  EXPECT_THROW(
    static_cast<void>(SurfaceProvenance::Parse(document.dump(), Identity())),
    std::invalid_argument);
}

NOLINT_TEST(
  SurfaceProvenanceTest, DuplicateGeometryAndCrossSourceIdsAreRejected)
{
  auto source = Source {};
  const auto first = Allocate(source.MakeRequest());
  auto document = nlohmann::json::parse(Publish(first.provenance)->Serialize());
  document.at("geometries").push_back(document.at("geometries").at(0));
  EXPECT_THROW(
    static_cast<void>(SurfaceProvenance::Parse(document.dump(), Identity())),
    std::invalid_argument);
  auto other = source.MakeRequest();
  other.request.surface_provenance = std::make_shared<const SurfaceProvenance>(
    oxygen::Uuid::FromString("01900000-0000-7000-8000-000000000002").value());
  EXPECT_NE(Allocate(other).ids_by_slot.at(0U), first.ids_by_slot.at(0U));
}

//! An allocation under one key twice is no record of anything.
NOLINT_TEST(SurfaceProvenanceTest, DuplicateDeclarationKeysAreRejected)
{
  auto source = Source {};
  auto document = nlohmann::json::parse(
    Publish(Allocate(source.MakeRequest()).provenance)->Serialize());
  auto& allocations = document.at("geometries").at(0).at("allocations");
  allocations.at(1).at("declaration_key")
    = allocations.at(0).at("declaration_key");
  EXPECT_THROW(
    static_cast<void>(SurfaceProvenance::Parse(document.dump(), Identity())),
    std::invalid_argument);
}

NOLINT_TEST(SurfaceProvenanceTest, EmptySourceRecordRoundTrips)
{
  const SurfaceProvenance initial(Identity());
  const auto serialized = nlohmann::json::parse(initial.Serialize());
  const auto parsed
    = SurfaceProvenance::Parse(serialized.dump(), initial.SourceIdentity());
  EXPECT_TRUE(parsed->Geometries().empty());
  EXPECT_EQ(parsed->SourceIdentity(), initial.SourceIdentity());
  // The record keeps the identity; the provenance does not repeat it.
  EXPECT_FALSE(serialized.contains("source_identity"));
  EXPECT_EQ(serialized.at("schema_version"), SurfaceProvenance::kSchemaVersion);
}

} // namespace
