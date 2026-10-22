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
#include <stdexcept>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/SurfaceAllocation.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/SourceLayoutTestSupport.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::AllocateSurfaces;
using oxygen::content::import::MeshBuildPipeline;
using oxygen::content::import::MeshLod;
using oxygen::content::import::SurfaceGeometryProvenance;
using oxygen::content::import::SurfaceProvenance;
using oxygen::content::import::TriangleRange;
using oxygen::data::SurfaceId;

constexpr auto kSourceIdentity = "01900000-0000-7000-8000-000000000001";

struct Source final {
  std::array<glm::vec3, 3> positions {
    glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 1.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 1.0F, 0.0F },
  };
  std::array<uint32_t, 6> indices { 0U, 1U, 2U, 0U, 2U, 1U };
  std::array<TriangleRange, 2> ranges {
    TriangleRange {
      .surface = 0U,
      .source_slot = 0U,
      .first_index = 0U,
      .index_count = 3U,
    },
    TriangleRange {
      .surface = 0U,
      .source_slot = 1U,
      .first_index = 3U,
      .index_count = 3U,
    },
  };

  [[nodiscard]] auto MakeRequest() const -> MeshBuildPipeline::WorkItem
  {
    auto item = MeshBuildPipeline::WorkItem {};
    item.storage_mesh_name = "retained-mesh";
    item.request.surface_provenance = std::make_shared<const SurfaceProvenance>(
      oxygen::Uuid::FromString(kSourceIdentity).value());
    auto lod = MeshLod {};
    lod.source.streams.positions = positions;
    lod.source.indices = indices;
    lod.source.ranges = ranges;
    item.lods.push_back(std::move(lod));
    item.source_layout_witness
      = oxygen::content::import::test::FixtureSourceLayoutWitness(item.lods);
    return item;
  }
};

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
    });
  }
  geometry.inventory.layout_revision
    = oxygen::data::ComputeSurfaceLayoutRevision(geometry.inventory.surfaces)
        .value();
  std::vector<SurfaceGeometryProvenance> geometries;
  geometries.push_back(std::move(geometry));
  return std::make_shared<const SurfaceProvenance>(
    oxygen::Uuid::FromString(kSourceIdentity).value(), std::move(geometries));
}

NOLINT_TEST(
  SurfaceProvenanceTest, SeparateDeclarationsWithEqualMaterialsStayDistinct)
{
  const Source source;
  const auto first = AllocateSurfaces(source.MakeRequest());
  const auto second = AllocateSurfaces(source.MakeRequest());
  EXPECT_EQ(first.allocations, second.allocations);
  ASSERT_EQ(first.allocations.size(), 2U);
  EXPECT_NE(first.allocations.at(0U), first.allocations.at(1U));
}

NOLINT_TEST(SurfaceProvenanceTest,
  RetainedAllocationsSurviveCacheDeletionAndProducerChanges)
{
  const Source source;
  auto item = source.MakeRequest();
  auto first = AllocateSurfaces(item);
  const auto retained_id
    = SurfaceId::FromStableIdentity("older-native-allocation-policy");
  first.allocations.at(1U) = retained_id;
  const auto persisted = Publish(std::move(first))->Serialize();

  // A new request has no derived files or cached geometry. Only the retained
  // native source record crosses the recreation boundary.
  auto recreated = source.MakeRequest();
  recreated.source_id = "different-machine-and-producer";
  recreated.mesh_name = "renamed-display-label";
  recreated.request.surface_provenance = SurfaceProvenance::Parse(persisted);
  const auto result = AllocateSurfaces(recreated);
  EXPECT_EQ(result.allocations.at(1U), retained_id);
}

NOLINT_TEST(
  SurfaceProvenanceTest, MaterialAndGeneratedAttributeChangesKeepIdentity)
{
  const Source source;
  auto item = source.MakeRequest();
  const auto first = AllocateSurfaces(item);
  item.request.surface_provenance = Publish(first);
  item.material_keys.push_back(
    oxygen::data::AssetKey::FromVirtualPath("/changed.omat"));
  item.surface_names.emplace(0U, "new label");
  std::array<glm::vec3, 3> normals {
    glm::vec3 { 0.0F, 0.0F, 1.0F },
    glm::vec3 { 0.0F, 0.0F, 1.0F },
    glm::vec3 { 0.0F, 0.0F, 1.0F },
  };
  item.lods.front().source.streams.normals = normals;
  EXPECT_EQ(AllocateSurfaces(item).allocations, first.allocations);
}

NOLINT_TEST(
  SurfaceProvenanceTest, StructuralChangeAllocatesNewIdsWithoutOrdinalGuessing)
{
  Source source;
  auto item = source.MakeRequest();
  const auto first = AllocateSurfaces(item);
  item.request.surface_provenance = Publish(first);
  std::swap(source.indices.at(0), source.indices.at(1));
  item.source_layout_witness
    = oxygen::content::import::test::FixtureSourceLayoutWitness(item.lods);
  const auto changed = AllocateSurfaces(item);
  EXPECT_NE(changed.allocations.at(0U), first.allocations.at(0U));
  EXPECT_NE(changed.allocations.at(1U), first.allocations.at(1U));
}

NOLINT_TEST(
  SurfaceProvenanceTest, ProducerCoordinatesDoNotReplaceAdapterWitness)
{
  const Source source;
  auto item = source.MakeRequest();
  const auto first = AllocateSurfaces(item);
  item.request.surface_provenance = Publish(first);
  auto converted = source.positions;
  for (auto& position : converted) {
    position *= 10.0F;
  }
  item.lods.front().source.streams.positions = converted;
  EXPECT_EQ(AllocateSurfaces(item).allocations, first.allocations);
}

NOLINT_TEST(SurfaceProvenanceTest, MissingAdapterWitnessIsRejected)
{
  const Source source;
  auto item = source.MakeRequest();
  item.source_layout_witness = {};
  EXPECT_THROW(
    static_cast<void>(AllocateSurfaces(item)), std::invalid_argument);
}

NOLINT_TEST(
  SurfaceProvenanceTest, CorruptedRetainedInventoryIsRejectedAtBoundary)
{
  const Source source;
  auto document = nlohmann::json::parse(
    Publish(AllocateSurfaces(source.MakeRequest()))->Serialize());
  document.at("geometries")
    .at(0)
    .at("surfaces")
    .at(0)
    .at("bindings")
    .at(0)
    .update({ { "submesh_index", 7U } });
  EXPECT_THROW(static_cast<void>(SurfaceProvenance::Parse(document.dump())),
    std::invalid_argument);
}

NOLINT_TEST(
  SurfaceProvenanceTest, DuplicateGeometryAndCrossSourceIdsAreRejected)
{
  const Source source;
  const auto first = AllocateSurfaces(source.MakeRequest());
  auto document = nlohmann::json::parse(Publish(first)->Serialize());
  document.at("geometries").push_back(document.at("geometries").at(0));
  EXPECT_THROW(static_cast<void>(SurfaceProvenance::Parse(document.dump())),
    std::invalid_argument);
  auto other = source.MakeRequest();
  other.request.surface_provenance = std::make_shared<const SurfaceProvenance>(
    oxygen::Uuid::FromString("01900000-0000-7000-8000-000000000002").value());
  EXPECT_NE(
    AllocateSurfaces(other).allocations.at(0U), first.allocations.at(0U));
}

NOLINT_TEST(SurfaceProvenanceTest, EmptySourceRecordRoundTrips)
{
  const SurfaceProvenance initial(
    oxygen::Uuid::FromString(kSourceIdentity).value());
  const auto parsed = SurfaceProvenance::Parse(initial.Serialize());
  EXPECT_TRUE(parsed->Geometries().empty());
  EXPECT_EQ(parsed->SourceIdentity(), initial.SourceIdentity());
}

} // namespace
