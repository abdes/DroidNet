//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakPlanBuilder.cpp, Pak/PakWriter.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "PakSceneTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace fixtures = oxygen::cooker::testing;
namespace pak = oxygen::content::pak;
namespace core = oxygen::data::pak::core;
namespace input = oxygen::data::pak::input;
namespace physics = oxygen::data::pak::physics;
namespace script = oxygen::data::pak::scripting;

using oxygen::ResourceIndexT;
using oxygen::cooker::test::HasDiagnostic;
using oxygen::cooker::test::HasNoErrors;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::ResourceTable;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SeedBytes;
using oxygen::serio::testing::BytesEqual;

auto FindTable(const pak::PakPlan& plan, const std::string_view name)
  -> const pak::PakTablePlan*
{
  const auto tables = plan.Tables();
  const auto it
    = std::ranges::find(tables, name, &pak::PakTablePlan::table_name);
  return it == tables.end() ? nullptr : std::addressof(*it);
}

//! Where the plan reads the descriptor of `key` from.
auto PayloadSourceOf(const pak::PakPlan& plan, const data::AssetKey& key)
  -> const pak::PakPayloadSourceSlicePlan&
{
  const auto assets = plan.Assets();
  const auto it
    = std::ranges::find(assets, key, &pak::PakAssetPlacementPlan::asset_key);
  if (it == assets.end()) {
    throw std::out_of_range("the plan does not place the asset");
  }
  return oxygen::base::CheckedAt(plan.AssetPayloadSources(),
    static_cast<size_t>(std::distance(assets.begin(), it)));
}

auto ComputeExpectedBrowsePayloadSize(
  std::span<const pak::PakBrowseEntryPlan> entries) -> uint64_t
{
  uint64_t string_bytes = 0;
  for (const auto& entry : entries) {
    string_bytes += entry.virtual_path.size();
  }
  return static_cast<uint64_t>(sizeof(core::PakBrowseIndexHeader))
    + (static_cast<uint64_t>(entries.size())
      * sizeof(core::PakBrowseIndexEntry))
    + string_bytes;
}

//=== Plan comparison ===-----------------------------------------------------//

//! The rows of `rows` as tuples of the fields `fields` selects, so that one
//! comparison checks a whole section of two plans.
template <class Rows, class Fields>
auto Project(const Rows& rows, Fields fields) -> auto
{
  return rows | std::views::transform(fields) | std::ranges::to<std::vector>();
}

//! Where the plan places its header, directory, browse index and footer.
auto LayoutFields(const pak::PakPlan& plan) -> auto
{
  const auto& header = plan.Header();
  return std::tuple { header.offset, header.size_bytes, header.content_version,
    header.source_key, plan.PlannedFileSize(), plan.Directory().offset,
    plan.Directory().size_bytes, plan.BrowseIndex().offset,
    plan.BrowseIndex().size_bytes, plan.Footer().offset,
    plan.Footer().size_bytes, plan.Footer().crc32_field_absolute_offset };
}

constexpr auto kRegionFields = [](const pak::PakRegionPlan& row) -> auto {
  return std::tuple { row.region_name, row.offset, row.size_bytes,
    row.alignment };
};

constexpr auto kTableFields = [](const pak::PakTablePlan& row) -> auto {
  return std::tuple { row.table_name, row.offset, row.size_bytes, row.count,
    row.entry_size, row.expected_entry_size, row.alignment,
    row.index_zero_required, row.index_zero_present, row.index_zero_forbidden };
};

constexpr auto kAssetFields
  = [](const pak::PakAssetPlacementPlan& row) -> auto {
  return std::tuple { row.asset_key, row.asset_type, row.offset, row.size_bytes,
    row.alignment, row.reserved_bytes_zeroed };
};

constexpr auto kResourceFields
  = [](const pak::PakResourcePlacementPlan& row) -> auto {
  return std::tuple { row.resource_kind, row.resource_index, row.region_name,
    row.offset, row.size_bytes, row.alignment, row.reserved_bytes_zeroed };
};

constexpr auto kDirectoryFields
  = [](const pak::PakAssetDirectoryEntryPlan& row) -> auto {
  return std::tuple { row.asset_key, row.asset_type, row.entry_offset,
    row.descriptor_offset, row.descriptor_size };
};

constexpr auto kBrowseFields = [](const pak::PakBrowseEntryPlan& row) -> auto {
  return std::tuple { row.asset_key, row.virtual_path };
};

constexpr auto kCatalogEntryFields
  = [](const data::PakCatalogEntry& row) -> auto {
  return std::tuple { row.asset_key, row.asset_type, row.descriptor_digest,
    row.transitive_resource_digest };
};

auto ExpectPlansEquivalent(const pak::PakPlan& lhs, const pak::PakPlan& rhs)
  -> void
{
  EXPECT_EQ(LayoutFields(lhs), LayoutFields(rhs));
  EXPECT_EQ(Project(lhs.Regions(), kRegionFields),
    Project(rhs.Regions(), kRegionFields));
  EXPECT_EQ(
    Project(lhs.Tables(), kTableFields), Project(rhs.Tables(), kTableFields));
  EXPECT_EQ(
    Project(lhs.Assets(), kAssetFields), Project(rhs.Assets(), kAssetFields));
  EXPECT_EQ(Project(lhs.Resources(), kResourceFields),
    Project(rhs.Resources(), kResourceFields));
  EXPECT_EQ(Project(lhs.Directory().entries, kDirectoryFields),
    Project(rhs.Directory().entries, kDirectoryFields));
  EXPECT_EQ(Project(lhs.BrowseIndex().entries, kBrowseFields),
    Project(rhs.BrowseIndex().entries, kBrowseFields));
}

auto ExpectCatalogsEquivalent(
  const data::PakCatalog& lhs, const data::PakCatalog& rhs) -> void
{
  EXPECT_EQ(std::tuple(lhs.source_key, lhs.content_version, lhs.catalog_digest),
    std::tuple(rhs.source_key, rhs.content_version, rhs.catalog_digest));
  EXPECT_EQ(Project(lhs.entries, kCatalogEntryFields),
    Project(rhs.entries, kCatalogEntryFields));
}

//=== Overrides and determinism ===-------------------------------------------//

//! The leaf every library of the override tests holds, so one key.
constexpr auto kSharedLeaf = std::string_view { "Asset.win" };

//! A library of `seed` whose shared asset is the material `name`.
auto MaterialRoot(const uint8_t seed, const std::string_view name)
  -> LooseCookedRoot
{
  auto library = LooseCookedRoot { "Game", seed };
  library.Add({
    .type = data::AssetType::kMaterial,
    .leaf = std::string(kSharedLeaf),
    .descriptor = fixtures::MaterialDescriptor(name).bytes,
  });
  return library;
}

//! The catalog digest of the material `name`.
auto MaterialDigest(const std::string_view name) -> oxygen::base::Sha256Digest
{
  return oxygen::base::ComputeSha256(fixtures::MaterialDescriptor(name).bytes);
}

NOLINT_TEST(PakPlanBuilderTest, PlanningTheSameRequestTwiceGivesTheSamePlan)
{
  // Arrange
  auto workspace = PakWorkspace {};
  const auto request = workspace.FullRequest({
    workspace.Add(MaterialRoot(1U, "Z")),
    workspace.Add(MaterialRoot(2U, "A")),
  });
  const auto builder = pak::PakPlanBuilder {};

  // Act
  const auto first = builder.Build(request);
  const auto repeated = builder.Build(request);

  // Assert
  ASSERT_THAT(first.diagnostics, HasNoErrors());
  ASSERT_THAT(repeated.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(first.plan);
  ASSERT_HAS_VALUE(repeated.plan);
  ExpectPlansEquivalent(*first.plan, *repeated.plan);
  ExpectCatalogsEquivalent(first.output_catalog, repeated.output_catalog);
}

//! Two sources that hold the same asset: the later one in the request wins,
//! whatever their identities or paths.
NOLINT_TEST(PakPlanBuilderTest, LaterSourceWinsAnOverriddenAsset)
{
  // Arrange
  auto workspace = PakWorkspace {};
  const auto a = workspace.Add(MaterialRoot(1U, "A"));
  const auto z = workspace.Add(MaterialRoot(2U, "Z"));
  const auto key = LooseCookedRoot {}.KeyOf(kSharedLeaf);
  const auto builder = pak::PakPlanBuilder {};

  // Act
  const auto a_wins = builder.Build(workspace.FullRequest({ z, a }));
  const auto z_wins = builder.Build(workspace.FullRequest({ a, z }));

  // Assert
  ASSERT_THAT(a_wins.diagnostics, HasNoErrors());
  ASSERT_THAT(z_wins.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(a_wins.plan);
  ASSERT_EQ(a_wins.plan->Assets().size(), 1U);
  EXPECT_EQ(
    a_wins.plan->Assets().front().asset_type, data::AssetType::kMaterial);
  ASSERT_EQ(a_wins.output_catalog.entries.size(), 1U);
  const auto& a_entry = a_wins.output_catalog.entries.front();
  EXPECT_EQ(a_entry.asset_key, key);
  EXPECT_EQ(a_entry.asset_type, data::AssetType::kMaterial);
  EXPECT_EQ(a_entry.descriptor_digest, MaterialDigest("A"));
  ASSERT_EQ(z_wins.output_catalog.entries.size(), 1U);
  EXPECT_EQ(z_wins.output_catalog.entries.front().descriptor_digest,
    MaterialDigest("Z"));
}

//! An override may not change the asset's type, even in an explicit order.
NOLINT_TEST(PakPlanBuilderTest, RejectsAnOverrideThatChangesTheAssetType)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto scenes = LooseCookedRoot { "Game", 2U };
  scenes.Add({
    .type = data::AssetType::kScene,
    .leaf = std::string(kSharedLeaf),
    .descriptor = fixtures::MakeEmptySceneDescriptor(),
  });
  const auto request = workspace.FullRequest(
    { workspace.Add(MaterialRoot(1U, "A")), workspace.Add(scenes) });

  // Act
  const auto result = pak::PakPlanBuilder {}.Build(request);

  // Assert
  EXPECT_THAT(
    result.diagnostics, HasDiagnostic("pak.plan.asset_override_type_mismatch"));
  EXPECT_FALSE(result.plan.has_value());
}

//=== Asset types and tables ===----------------------------------------------//

NOLINT_TEST(PakPlanBuilderTest, FullModeIncludesInputAssetsFromLooseSource)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  auto action = input::InputActionAssetDesc {};
  action.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kInputAction);
  action.header.version = input::kInputActionAssetVersion;
  game.Add({
    .type = data::AssetType::kInputAction,
    .leaf = "Input/Move.oiact",
    .descriptor = data::MakeDescriptorBytes(action),
  });
  auto context = input::InputMappingContextAssetDesc {};
  context.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kInputMappingContext);
  context.header.version = input::kInputMappingContextAssetVersion;
  game.Add({
    .type = data::AssetType::kInputMappingContext,
    .leaf = "Input/Gameplay.oimap",
    .descriptor = data::MakeDescriptorBytes(context),
  });
  game.Add({
    .type = data::AssetType::kScene,
    .leaf = "Scenes/Main.oscene",
    .descriptor = fixtures::MakeEmptySceneDescriptor(),
  });
  const auto request = workspace.FullRequest({ workspace.Add(game) });

  // Act
  const auto result = pak::PakPlanBuilder {}.Build(request);

  // Assert
  ASSERT_THAT(result.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(result.plan);
  const auto expected_types
    = ::testing::UnorderedElementsAre(data::AssetType::kInputAction,
      data::AssetType::kInputMappingContext, data::AssetType::kScene);
  EXPECT_THAT(
    Project(result.plan->Assets(), &pak::PakAssetPlacementPlan::asset_type),
    expected_types);
  EXPECT_THAT(Project(result.plan->Directory().entries,
                &pak::PakAssetDirectoryEntryPlan::asset_type),
    expected_types);
}

NOLINT_TEST(PakPlanBuilderTest, IndexZeroPolicyAppliedForResourceTables)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  auto logic = script::ScriptAssetDesc {};
  logic.header.asset_type = static_cast<uint8_t>(data::AssetType::kScript);
  logic.header.version = script::kScriptAssetVersion;
  game.Add({
    .type = data::AssetType::kScript,
    .leaf = "Logic.oscript",
    .descriptor = data::MakeDescriptorBytes(logic),
  });
  // Texture and script tables that hold only their reserved row zero.
  game.AddEmptyTable(ResourceTable::kTextures)
    .AddEmptyTable(ResourceTable::kScripts);
  const auto request = workspace.FullRequest({ workspace.Add(game) });

  // Act
  const auto result = pak::PakPlanBuilder {}.Build(request);

  // Assert
  ASSERT_THAT(result.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(result.plan);
  const auto* texture_table = FindTable(*result.plan, "texture_table");
  const auto* script_resource_table
    = FindTable(*result.plan, "script_resource_table");
  const auto* audio_table = FindTable(*result.plan, "audio_table");
  ASSERT_NE(texture_table, nullptr);
  ASSERT_NE(script_resource_table, nullptr);
  ASSERT_NE(audio_table, nullptr);

  EXPECT_EQ(texture_table->count, 1U);
  EXPECT_TRUE(texture_table->index_zero_required);
  EXPECT_TRUE(texture_table->index_zero_present);
  EXPECT_FALSE(texture_table->index_zero_forbidden);

  EXPECT_EQ(script_resource_table->count, 1U);
  EXPECT_TRUE(script_resource_table->index_zero_required);
  EXPECT_TRUE(script_resource_table->index_zero_present);
  EXPECT_FALSE(script_resource_table->index_zero_forbidden);

  EXPECT_EQ(audio_table->count, 0U);
  EXPECT_FALSE(audio_table->index_zero_required);
  EXPECT_FALSE(audio_table->index_zero_present);
}

//! The browse index lists its entries by virtual path, whatever the order of
//! the source, and plans exactly the bytes it serializes.
NOLINT_TEST(
  PakPlanBuilderTest, BrowsePayloadPlanSizeMatchesSerializedShapeInvariant)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  game.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Browse/B.omat",
    .descriptor = fixtures::MaterialDescriptor("B").bytes,
  });
  auto logic = script::ScriptAssetDesc {};
  logic.header.asset_type = static_cast<uint8_t>(data::AssetType::kScript);
  logic.header.version = script::kScriptAssetVersion;
  game.Add({
    .type = data::AssetType::kScript,
    .leaf = "Browse/A.oscript",
    .descriptor = data::MakeDescriptorBytes(logic),
  });
  const auto request = workspace.FullRequest({ workspace.Add(game) });

  // Act
  const auto result = pak::PakPlanBuilder {}.Build(request);

  // Assert
  ASSERT_THAT(result.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(result.plan);
  const auto& browse = result.plan->BrowseIndex();
  ASSERT_EQ(browse.entries.size(), 2U);
  EXPECT_LT(
    browse.entries.front().virtual_path, browse.entries.back().virtual_path);
  EXPECT_EQ(
    browse.size_bytes, ComputeExpectedBrowsePayloadSize(browse.entries));
  EXPECT_THAT(result.diagnostics,
    ::testing::Not(HasDiagnostic("pak.plan.stage.layout.browse_store_failed")));
  EXPECT_THAT(result.diagnostics,
    ::testing::Not(
      HasDiagnostic("pak.plan.stage.layout.browse_size_mismatch")));
}

//=== Scenes binding a texture through a mask ===-----------------------------//

//! Merging two scene mask libraries remaps the second scene's mask binding in
//! its reference table, packs its descriptor byte for byte from the source,
//! and the plan is writable.
NOLINT_TEST(PakPlanBuilderTest, RemapsSceneMaskIndicesAcrossCookedSources)
{
  // Arrange
  auto workspace = PakWorkspace {};
  const auto first = pak::test::MakeSceneMask(1U);
  const auto second = pak::test::MakeSceneMask(2U);
  const auto request = workspace.FullRequest(
    { workspace.Add(first.root), workspace.Add(second.root) });

  // Act
  const auto packed = oxygen::cooker::testing::PakWorkspace::Pack(request);

  // Assert
  ASSERT_THAT(packed.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(packed.planned.plan);
  ASSERT_EQ(packed.planned.plan->Assets().size(), 6U);
  const auto& source = PayloadSourceOf(*packed.planned.plan, second.scene);
  EXPECT_TRUE(source.inline_bytes.empty());
  const auto descriptor = workspace.Storage().Bytes(source.source_path);
  EXPECT_EQ(source.size_bytes, descriptor.size());
  const auto pak = workspace.Open(request.output_pak_path);
  EXPECT_THAT(pak.Descriptor(second.scene), BytesEqual(descriptor));
  EXPECT_EQ(
    pak.Resolve(second.scene, second.mask, data::ResourceKind::kTexture),
    ResourceIndexT { 2U });
}

//=== Physics payloads over two layers ===------------------------------------//

enum class ShapeKind : uint8_t { kSolid, kSensor };

//! A collision shape whose cooked payload is a physics resource two libraries
//! hold: the owner, which also holds the shape and its material, and a later
//! library that overrides the payload, so that the library's payload wins.
struct LayeredPhysics final {
  LooseCookedRoot owner { "Game", 1U };
  LooseCookedRoot library { "Game", 2U };
  data::AssetKey shape;
  std::vector<std::byte> owner_payload = SeedBytes(1U);
  std::vector<std::byte> library_payload = SeedBytes(2U);
};

auto MakeLayeredPhysics(const ShapeKind kind = ShapeKind::kSolid)
  -> LayeredPhysics
{
  auto layers = LayeredPhysics {};
  auto& owner = layers.owner;
  const auto payload_key = owner.KeyOf("Shape.convex");

  auto material = physics::PhysicsMaterialAssetDesc {};
  material.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kPhysicsMaterial);
  material.header.version = physics::kPhysicsMaterialAssetVersion;
  const auto material_key = owner.Add({
    .type = data::AssetType::kPhysicsMaterial,
    .leaf = "Material.opmat",
    .descriptor = data::MakeDescriptorBytes(material),
  });

  auto shape = physics::CollisionShapeAssetDesc {};
  shape.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kCollisionShape);
  shape.header.version = physics::kCollisionShapeAssetVersion;
  shape.shape_type = physics::ShapeType::kConvexHull;
  shape.material_asset_key = material_key;
  shape.cooked_shape_ref.payload_asset_key = payload_key;
  shape.cooked_shape_ref.payload_type = physics::ShapePayloadType::kConvex;
  if (kind == ShapeKind::kSensor) {
    shape.is_sensor = physics::kShapeIsSensorTrue;
  }
  layers.shape = owner.Add({
    .type = data::AssetType::kCollisionShape,
    .leaf = "Shape.ocshape",
    .descriptor = data::MakeDescriptorBytes(shape),
    .references = data::AssetReferences::Create({},
      {
        {
          .key = material_key,
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kPhysicsMaterial,
        },
        {
          .key = payload_key,
          .kind = data::KeyReferenceKind::kPhysicsResource,
          .expected_type = data::AssetType::kUnknown,
        },
      })
      .value(),
  });

  auto payload = physics::PhysicsResourceDesc {};
  payload.resource_asset_key = payload_key;
  owner.AddPhysics(layers.owner_payload, payload);
  layers.library.AddPhysics(layers.library_payload, payload);
  return layers;
}

//! Expects `plan` to carry exactly one physics payload, `payload`, read from
//! `library`.
auto ExpectPhysicsPayloadFrom(const pak::PakPlan& plan,
  const data::CookedSource& library, const std::span<const std::byte> payload)
  -> void
{
  auto payloads = std::vector<pak::PakPayloadSourceSlicePlan> {};
  const auto resources = plan.Resources();
  for (size_t index = 0; index < resources.size(); ++index) {
    const auto& resource = oxygen::base::CheckedAt(resources, index);
    if (resource.resource_kind == "physics" && resource.size_bytes != 0U) {
      payloads.push_back(
        oxygen::base::CheckedAt(plan.ResourcePayloadSources(), index));
    }
  }
  ASSERT_EQ(payloads.size(), 1U);
  EXPECT_EQ(payloads.front().source_path.parent_path(), library.path);
  EXPECT_EQ(payloads.front().size_bytes, payload.size());
}

NOLINT_TEST(PakPlanBuilderTest, LayeredPhysicsPayloadUsesWinningSourceInFull)
{
  // Arrange
  auto workspace = PakWorkspace {};
  const auto layers = MakeLayeredPhysics();
  const auto library = workspace.Add(layers.library);
  const auto request
    = workspace.FullRequest({ workspace.Add(layers.owner), library });

  // Act
  const auto packed = oxygen::cooker::testing::PakWorkspace::Pack(request);

  // Assert
  ASSERT_THAT(packed.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(packed.planned.plan);
  ExpectPhysicsPayloadFrom(
    *packed.planned.plan, library, layers.library_payload);
  const auto pak = workspace.Open(request.output_pak_path);
  EXPECT_EQ(pak.Pak().PhysicsTable().Size().get(), 2U);
  EXPECT_THAT(pak.Payload<data::PhysicsResource>(ResourceIndexT { 1U }),
    BytesEqual(layers.library_payload));
}

//! A patch that changes only the shape packs it with the winning payload and
//! records the payload in the shape's closure; the payload's owner emits no
//! descriptor of its own.
NOLINT_TEST(PakPlanBuilderTest, LayeredPhysicsPayloadUsesWinningSourceInPatch)
{
  // Arrange
  auto workspace = PakWorkspace {};
  const auto solid = MakeLayeredPhysics(ShapeKind::kSolid);
  const auto library = workspace.Add(solid.library);
  const auto full = pak::PakPlanBuilder {}.Build(
    workspace.FullRequest({ workspace.Add(solid.owner), library }));
  ASSERT_THAT(full.diagnostics, HasNoErrors());
  const auto sensor = MakeLayeredPhysics(ShapeKind::kSensor);
  const auto request = workspace.PatchRequest(
    { workspace.Add(sensor.owner), library }, { full.output_catalog });

  // Act
  const auto patch = pak::PakPlanBuilder {}.Build(request);

  // Assert
  ASSERT_THAT(patch.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(patch.plan);
  ASSERT_EQ(patch.plan->Assets().size(), 1U);
  EXPECT_EQ(patch.plan->Assets().front().asset_key, sensor.shape);
  ExpectPhysicsPayloadFrom(*patch.plan, library, sensor.library_payload);
  const auto resources = patch.plan->Resources();
  const auto payload
    = std::ranges::find_if(resources, [](const auto& resource) -> auto {
        return resource.resource_kind == "physics" && resource.size_bytes != 0U;
      });
  ASSERT_NE(payload, resources.end());
  EXPECT_EQ(std::ranges::count_if(patch.plan->PatchClosure(),
              [&](const auto& dependency) -> auto {
                return dependency.asset_key == sensor.shape
                  && dependency.resource_kind == "physics"
                  && dependency.resource_index == payload->resource_index;
              }),
    1);
}

//=== Physics scene pairs ===-------------------------------------------------//

//! Adds an empty scene; returns its key.
auto AddScene(LooseCookedRoot& library) -> data::AssetKey
{
  return library.Scene("Scene.oscene");
}

//! `catalog` with the descriptor digest of `key` changed.
auto WithChangedDescriptor(data::PakCatalog catalog, const data::AssetKey& key)
  -> data::PakCatalog
{
  const auto entry = std::ranges::find(
    catalog.entries, key, &data::PakCatalogEntry::asset_key);
  if (entry == catalog.entries.end()) {
    throw std::out_of_range("the catalog does not list the asset");
  }
  entry->descriptor_digest.front() ^= 1U;
  catalog.catalog_digest = catalog.ComputeDigest().value();
  return catalog;
}

NOLINT_TEST(PakPlanBuilderTest, PatchRecordsDeletedSidecarAsTombstone)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  const auto scene = AddScene(game);
  const auto sidecar = game.PhysicsSidecar("Scene.opscene", scene);
  const auto base_request
    = workspace.FullRequest({ workspace.Add(game) }, "base.pak");
  const auto base = oxygen::cooker::testing::PakWorkspace::Pack(base_request);
  ASSERT_THAT(base.Diagnostics(), HasNoErrors());
  ASSERT_TRUE(workspace.Open(base_request.output_pak_path)
      .Pak()
      .FindEntry(sidecar)
      .has_value());
  // The library is re-cooked without the sidecar.
  auto recooked = LooseCookedRoot {};
  AddScene(recooked);
  const auto request = workspace.PatchRequest(
    { workspace.Add(recooked) }, { base.planned.output_catalog });

  // Act
  const auto patch = workspace.PackOrThrow(request);

  // Assert
  EXPECT_THAT(patch.Pak().Catalog().deleted, ::testing::Contains(sidecar));
  EXPECT_FALSE(patch.Pak().FindEntry(sidecar).has_value());
}

//! A sidecar hashed against another scene's descriptor does not pair.
NOLINT_TEST(PakPlanBuilderTest, RejectsSidecarOfAnotherSceneDescriptor)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  const auto scene = AddScene(game);
  auto sidecar = fixtures::PhysicsSidecarDescriptor(
    scene, fixtures::SceneDescriptor("Other").bytes);
  game.Add({
    .type = data::AssetType::kPhysicsScene,
    .leaf = "Scene.opscene",
    .descriptor = std::move(sidecar.bytes),
    .references = std::move(sidecar.references),
  });
  const auto request = workspace.FullRequest({ workspace.Add(game) });

  // Act
  const auto result = pak::PakPlanBuilder {}.Build(request);

  // Assert
  EXPECT_FALSE(result.plan.has_value());
  EXPECT_THAT(
    result.diagnostics, HasDiagnostic("pak.plan.physics_scene_pair_invalid"));
}

NOLINT_TEST(PakPlanBuilderTest, RejectsTwoSidecarsOfOneScene)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  const auto scene = AddScene(game);
  game.PhysicsSidecar("Scene.opscene", scene);
  game.PhysicsSidecar("Duplicate.opscene", scene);
  const auto request = workspace.FullRequest({ workspace.Add(game) });

  // Act
  const auto result = pak::PakPlanBuilder {}.Build(request);

  // Assert
  EXPECT_FALSE(result.plan.has_value());
  EXPECT_THAT(
    result.diagnostics, HasDiagnostic("pak.plan.physics_scene_pair_invalid"));
}

//! A sidecar expecting more nodes than its scene has does not pair.
NOLINT_TEST(PakPlanBuilderTest, RejectsSidecarOfAnotherNodeCount)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  const auto empty = SceneBuilder { "Scene" };
  const auto scene = game.Scene("Scene.oscene", empty);
  auto sidecar = fixtures::PhysicsSidecarDescriptor(scene, empty.Bytes());
  DescriptorDefects::SetSidecarNodeCount(sidecar.bytes, 1U);
  game.Add({
    .type = data::AssetType::kPhysicsScene,
    .leaf = "Scene.opscene",
    .descriptor = std::move(sidecar.bytes),
    .references = std::move(sidecar.references),
  });
  const auto request = workspace.FullRequest({ workspace.Add(game) });

  // Act
  const auto result = pak::PakPlanBuilder {}.Build(request);

  // Assert
  EXPECT_FALSE(result.plan.has_value());
  EXPECT_THAT(
    result.diagnostics, HasDiagnostic("pak.plan.physics_scene_pair_invalid"));
}

//! Which member of a physics scene pair a patch changes.
enum class PairMember : uint8_t { kScene, kSidecar };

class PakPlanBuilderPairPatchTest
  : public ::testing::TestWithParam<PairMember> { };

//! A patch that changes either member of a physics scene pair replaces both.
NOLINT_TEST_P(PakPlanBuilderPairPatchTest, IncludesBothMembersOfThePair)
{
  // Arrange
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  const auto scene = AddScene(game);
  const auto sidecar = game.PhysicsSidecar("Scene.opscene", scene);
  const auto source = workspace.Add(game);
  const auto baseline
    = pak::PakPlanBuilder {}.Build(workspace.FullRequest({ source }));
  ASSERT_THAT(baseline.diagnostics, HasNoErrors());
  const auto changed = GetParam() == PairMember::kScene ? scene : sidecar;
  const auto request = workspace.PatchRequest(
    { source }, { WithChangedDescriptor(baseline.output_catalog, changed) });

  // Act
  const auto patch = pak::PakPlanBuilder {}.Build(request);

  // Assert
  ASSERT_THAT(patch.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(patch.plan);
  EXPECT_EQ(patch.plan->Assets().size(), 2U);
  EXPECT_THAT(
    Project(patch.plan->PatchActions(), &pak::PakPatchActionRecord::action),
    ::testing::Each(pak::PakPatchAction::kReplace));
}

INSTANTIATE_TEST_SUITE_P(Members, PakPlanBuilderPairPatchTest,
  ::testing::Values(PairMember::kScene, PairMember::kSidecar),
  [](const auto& info) {
    return std::string(info.param == PairMember::kScene ? "Scene" : "Sidecar");
  });

} // namespace
