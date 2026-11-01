//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakPlanBuilder.cpp, Pak/PakMeasureStore.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakSceneTestSupport.h"

#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {
namespace base = oxygen::base;
namespace data = oxygen::data;
namespace fixtures = oxygen::cooker::testing;
namespace pak = oxygen::content::pak;
namespace render = oxygen::data::pak::render;

using oxygen::ResourceIndexT;
using oxygen::cooker::test::HasDiagnostic;
using oxygen::cooker::test::HasNoErrors;
using oxygen::cooker::testing::BaseEntry;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SeedBytes;
using oxygen::serio::testing::BytesEqual;
using pak::PakPatchAction;

//=== Helpers ===-------------------------------------------------------------//

auto ActionOf(const pak::PakPlan& plan, const data::AssetKey& key)
  -> std::optional<PakPatchAction>
{
  const auto actions = plan.PatchActions();
  const auto it
    = std::ranges::find(actions, key, &pak::PakPatchActionRecord::asset_key);
  if (it == actions.end()) {
    return std::nullopt;
  }
  return it->action;
}

auto ContainsAsset(const pak::PakPlan& plan, const data::AssetKey& key) -> bool
{
  return std::ranges::contains(
    plan.Assets(), key, &pak::PakAssetPlacementPlan::asset_key);
}

auto FindTable(const pak::PakPlan& plan, const std::string_view name)
  -> const pak::PakTablePlan*
{
  const auto tables = plan.Tables();
  const auto it = std::ranges::find_if(
    tables, [name](const pak::PakTablePlan& table) -> bool {
      return table.table_name == name;
    });
  return it == tables.end() ? nullptr : std::addressof(*it);
}

auto FindCatalogEntry(const data::PakCatalog& catalog,
  const data::AssetKey& key) -> const data::PakCatalogEntry*
{
  const auto it = std::ranges::find(
    catalog.entries, key, &data::PakCatalogEntry::asset_key);
  return it == catalog.entries.end() ? nullptr : std::addressof(*it);
}

//! The plan's patch actions, by asset key.
auto PatchActionSignature(const pak::PakPlan& plan)
  -> std::vector<std::pair<data::AssetKey, PakPatchAction>>
{
  auto signature = std::vector<std::pair<data::AssetKey, PakPatchAction>> {};
  for (const auto& action : plan.PatchActions()) {
    signature.emplace_back(action.asset_key, action.action);
  }
  std::ranges::sort(signature, [](const auto& lhs, const auto& rhs) -> bool {
    return lhs.first < rhs.first;
  });
  return signature;
}

//! The codes of the error diagnostics.
auto ErrorCodes(const std::span<const pak::PakDiagnostic> diagnostics)
  -> std::vector<std::string>
{
  auto codes = std::vector<std::string> {};
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.severity == pak::PakDiagnosticSeverity::kError) {
      codes.push_back(diagnostic.code);
    }
  }
  return codes;
}

//=== Classification ===------------------------------------------------------//

//! The four-way classification scenario.
/*!
 Create, replace and unchanged come from one loose library each; delete exists
 only in the base catalog. The base held another version of the replaced
 material and the very descriptor of the unchanged scene.
*/
struct Classification final {
  data::AssetKey created;
  data::AssetKey replaced;
  data::AssetKey unchanged;
  data::AssetKey deleted;
  //! The sources of the created, replaced and unchanged assets, in order.
  std::vector<data::CookedSource> sources;
  data::PakCatalog base;
};

auto MakeClassification(PakWorkspace& workspace) -> Classification
{
  auto created = LooseCookedRoot { "Game", 1U };
  auto replaced = LooseCookedRoot { "Game", 2U };
  auto unchanged = LooseCookedRoot { "Game", 3U };
  const auto scene = fixtures::SceneBuilder { "Empty" }.Bytes();

  auto out = Classification {};
  out.created = created.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Patch/Create.omat",
    .descriptor = fixtures::MaterialBuilder { "Create" }.Bytes(),
  });
  out.replaced = replaced.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Patch/Replace.omat",
    .descriptor = fixtures::MaterialBuilder { "Replace" }.Bytes(),
  });
  out.unchanged = unchanged.Add({
    .type = data::AssetType::kScene,
    .leaf = "Patch/Unchanged.oscene",
    .descriptor = scene,
  });
  out.deleted = created.KeyOf("Patch/Delete.oscript");
  out.sources = {
    workspace.Add(created),
    workspace.Add(replaced),
    workspace.Add(unchanged),
  };
  out.base = created.BaseCatalog({
    BaseEntry(out.replaced, data::AssetType::kMaterial,
      fixtures::MaterialBuilder { "Replace (base)" }.Bytes()),
    BaseEntry(out.unchanged, data::AssetType::kScene, scene),
    BaseEntry(out.deleted, data::AssetType::kScript,
      oxygen::serio::testing::PatternBytes(sizeof(uint64_t))),
  });
  return out;
}

NOLINT_TEST(PakPlanBuilderPatchTest, ClassifiesCreateReplaceUnchangedDelete)
{
  auto workspace = PakWorkspace {};
  const auto scenario = MakeClassification(workspace);

  const auto result = pak::PakPlanBuilder {}.Build(
    workspace.PatchRequest(scenario.sources, { scenario.base }));

  ASSERT_THAT(result.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(result.plan);
  EXPECT_EQ(ActionOf(*result.plan, scenario.created), PakPatchAction::kCreate);
  EXPECT_EQ(
    ActionOf(*result.plan, scenario.replaced), PakPatchAction::kReplace);
  EXPECT_EQ(
    ActionOf(*result.plan, scenario.unchanged), PakPatchAction::kUnchanged);
  EXPECT_EQ(ActionOf(*result.plan, scenario.deleted), PakPatchAction::kDelete);
  // Only create and replace carry descriptors.
  EXPECT_TRUE(ContainsAsset(*result.plan, scenario.created));
  EXPECT_TRUE(ContainsAsset(*result.plan, scenario.replaced));
  EXPECT_FALSE(ContainsAsset(*result.plan, scenario.unchanged));
  EXPECT_FALSE(ContainsAsset(*result.plan, scenario.deleted));
  EXPECT_EQ(result.summary.patch_created, 1U);
  EXPECT_EQ(result.summary.patch_replaced, 1U);
  EXPECT_EQ(result.summary.patch_deleted, 1U);
  EXPECT_EQ(result.summary.patch_unchanged, 1U);
}

//! The order of the sources does not change the classification.
NOLINT_TEST(PakPlanBuilderPatchTest, ClassificationIsDeterministic)
{
  auto workspace = PakWorkspace {};
  const auto scenario = MakeClassification(workspace);
  auto reversed = scenario.sources;
  std::ranges::reverse(reversed);
  const auto builder = pak::PakPlanBuilder {};

  const auto in_order = builder.Build(
    workspace.PatchRequest(scenario.sources, { scenario.base }, "a.pak"));
  const auto in_reverse = builder.Build(
    workspace.PatchRequest(std::move(reversed), { scenario.base }, "b.pak"));

  ASSERT_THAT(in_order.diagnostics, HasNoErrors());
  ASSERT_THAT(in_reverse.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(in_order.plan);
  ASSERT_HAS_VALUE(in_reverse.plan);
  EXPECT_EQ(PatchActionSignature(*in_order.plan),
    PatchActionSignature(*in_reverse.plan));
}

//=== Patch-local resources ===-----------------------------------------------//

//! A patch carries only the resources its created and replaced assets bind:
//! the unchanged asset's library holds a buffer the patch leaves out.
NOLINT_TEST(PakPlanBuilderPatchTest, PatchModeUsesPatchLocalResourcesAndClosure)
{
  auto workspace = PakWorkspace {};
  auto replaced = LooseCookedRoot { "Game", 1U };
  const auto texture = replaced.AddTexture(SeedBytes(1U));
  auto textured = fixtures::MaterialBuilder { "Replace" };
  textured.Texture(&render::MaterialAssetDesc::base_color_texture, texture);
  const auto replace_key = replaced.Material("Patch/Replace.omat", textured);
  auto unchanged = LooseCookedRoot { "Game", 2U };
  unchanged.AddBuffer(SeedBytes(2U)); // No asset binds it.
  const auto unchanged_bytes
    = fixtures::MaterialBuilder { "Unchanged" }.Bytes();
  const auto unchanged_key = unchanged.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Patch/Unchanged.omat",
    .descriptor = unchanged_bytes,
  });
  const auto base = unchanged.BaseCatalog({
    BaseEntry(replace_key, data::AssetType::kMaterial,
      fixtures::MaterialBuilder { "Replace (base)" }.Bytes()),
    BaseEntry(unchanged_key, data::AssetType::kMaterial, unchanged_bytes),
  });

  const auto result = pak::PakPlanBuilder {}.Build(workspace.PatchRequest(
    { workspace.Add(unchanged), workspace.Add(replaced) }, { base }));

  ASSERT_THAT(result.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(result.plan);
  EXPECT_EQ(ActionOf(*result.plan, replace_key), PakPatchAction::kReplace);
  EXPECT_EQ(ActionOf(*result.plan, unchanged_key), PakPatchAction::kUnchanged);
  EXPECT_TRUE(ContainsAsset(*result.plan, replace_key));
  EXPECT_FALSE(ContainsAsset(*result.plan, unchanged_key));
  ASSERT_EQ(result.plan->Resources().size(), 1U);
  EXPECT_EQ(
    base::CheckedAt(result.plan->Resources(), 0U).resource_kind, "texture");
  const auto* texture_table = FindTable(*result.plan, "texture_table");
  const auto* buffer_table = FindTable(*result.plan, "buffer_table");
  ASSERT_NE(texture_table, nullptr);
  ASSERT_NE(buffer_table, nullptr);
  // The bound texture and the empty row that reserves index zero.
  EXPECT_EQ(texture_table->count, 2U);
  EXPECT_EQ(buffer_table->count, 0U);
  const auto closure = result.plan->PatchClosure();
  ASSERT_EQ(closure.size(), 1U);
  EXPECT_EQ(base::CheckedAt(closure, 0U).asset_key, replace_key);
  EXPECT_EQ(base::CheckedAt(closure, 0U).resource_kind, "texture");
  EXPECT_THAT(result.diagnostics,
    ::testing::Not(HasDiagnostic("pak.plan.stage.patch.closure_incomplete")));
}

//! A replacement keeps the asset type the base gave its key.
NOLINT_TEST(PakPlanBuilderPatchTest, ReplaceAssetTypeMismatchIsRejected)
{
  auto workspace = PakWorkspace {};
  auto game = LooseCookedRoot {};
  const auto scene = game.Add({
    .type = data::AssetType::kScene,
    .leaf = "Patch/TypeMismatch.oscene",
    .descriptor = fixtures::SceneBuilder { "Empty" }.Bytes(),
  });
  // The base held a material under the scene's key.
  const auto base = game.BaseCatalog({
    BaseEntry(scene, data::AssetType::kMaterial,
      fixtures::MaterialBuilder { "Base" }.Bytes()),
  });

  const auto result = pak::PakPlanBuilder {}.Build(
    workspace.PatchRequest({ workspace.Add(game) }, { base }));

  EXPECT_FALSE(result.plan.has_value());
  EXPECT_THAT(ErrorCodes(result.diagnostics),
    ::testing::ElementsAre("pak.plan.replace_type_mismatch"));
}

//=== Patching one library ===------------------------------------------------//

//! A library whose materials A and B each bind their own texture.
struct TwoMaterials final {
  LooseCookedRoot library;
  data::AssetKey a;
  data::AssetKey b;
};

auto MakeTwoMaterials(const uint8_t seed,
  const std::span<const std::byte> a_payload,
  const std::span<const std::byte> b_payload) -> TwoMaterials
{
  auto library = LooseCookedRoot { "Game", seed };
  const auto texture_a = library.AddTexture(a_payload);
  const auto texture_b = library.AddTexture(b_payload);
  const auto add = [&library](const std::string_view name,
                     const ResourceIndexT texture) -> data::AssetKey {
    auto material = fixtures::MaterialBuilder { name };
    material.Texture(&render::MaterialAssetDesc::base_color_texture, texture);
    return library.Material(std::string(name) + ".omat", material);
  };
  const auto a = add("MatA", texture_a);
  const auto b = add("MatB", texture_b);
  return { .library = std::move(library), .a = a, .b = b };
}

//! Within one loose source, only the material whose texture payload changed
//! is replaced.
NOLINT_TEST(PakPlanBuilderPatchTest,
  PatchModeOnlyReplacesChangedAssetWithinSingleLooseCookedSource)
{
  auto workspace = PakWorkspace {};
  const auto shared_payload = SeedBytes(1U);
  const auto base_payload = SeedBytes(2U);
  const auto patched_payload = SeedBytes(3U);
  const auto base_state = MakeTwoMaterials(1U, shared_payload, base_payload);
  const auto patched_state
    = MakeTwoMaterials(2U, shared_payload, patched_payload);
  const auto base_source = workspace.Add(base_state.library);
  const auto patched_source = workspace.Add(patched_state.library);
  const auto builder = pak::PakPlanBuilder {};
  // The base catalog comes from planning the base state, so its digests are
  // whatever the planner computes for unchanged inputs.
  const auto base = builder.Build(workspace.FullRequest({ base_source }));
  ASSERT_THAT(base.diagnostics, HasNoErrors());
  const auto request
    = workspace.PatchRequest({ patched_source }, { base.output_catalog });

  const auto patch = builder.Build(request);

  ASSERT_THAT(patch.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(patch.plan);
  EXPECT_EQ(ActionOf(*patch.plan, base_state.a), PakPatchAction::kUnchanged);
  EXPECT_EQ(ActionOf(*patch.plan, base_state.b), PakPatchAction::kReplace);
  EXPECT_FALSE(ContainsAsset(*patch.plan, base_state.a));
  EXPECT_TRUE(ContainsAsset(*patch.plan, base_state.b));
  ASSERT_EQ(patch.plan->Resources().size(), 1U);
  EXPECT_EQ(
    base::CheckedAt(patch.plan->Resources(), 0U).resource_kind, "texture");
  const auto closure = patch.plan->PatchClosure();
  ASSERT_EQ(closure.size(), 1U);
  EXPECT_EQ(base::CheckedAt(closure, 0U).asset_key, base_state.b);
  EXPECT_EQ(base::CheckedAt(closure, 0U).resource_kind, "texture");
  EXPECT_EQ(patch.summary.patch_created, 0U);
  EXPECT_EQ(patch.summary.patch_replaced, 1U);
  EXPECT_EQ(patch.summary.patch_deleted, 0U);
  EXPECT_EQ(patch.summary.patch_unchanged, 1U);
}

//! The planner's own transitive digests move with the inputs only: A's
//! texture is the same in both states, B's payload differs.
NOLINT_TEST(PakPlanBuilderPatchTest, TransitiveDigestsMoveWithResourcePayloads)
{
  auto workspace = PakWorkspace {};
  const auto shared_payload = SeedBytes(1U);
  const auto base_state = MakeTwoMaterials(1U, shared_payload, SeedBytes(2U));
  const auto patched_state
    = MakeTwoMaterials(2U, shared_payload, SeedBytes(3U));
  const auto builder = pak::PakPlanBuilder {};

  const auto base = builder.Build(
    workspace.FullRequest({ workspace.Add(base_state.library) }, "base.pak"));
  const auto patched = builder.Build(workspace.FullRequest(
    { workspace.Add(patched_state.library) }, "patched.pak"));

  ASSERT_THAT(base.diagnostics, HasNoErrors());
  ASSERT_THAT(patched.diagnostics, HasNoErrors());
  const auto* base_a = FindCatalogEntry(base.output_catalog, base_state.a);
  const auto* base_b = FindCatalogEntry(base.output_catalog, base_state.b);
  const auto* patched_a
    = FindCatalogEntry(patched.output_catalog, patched_state.a);
  const auto* patched_b
    = FindCatalogEntry(patched.output_catalog, patched_state.b);
  ASSERT_NE(base_a, nullptr);
  ASSERT_NE(base_b, nullptr);
  ASSERT_NE(patched_a, nullptr);
  ASSERT_NE(patched_b, nullptr);
  EXPECT_EQ(
    base_a->transitive_resource_digest, patched_a->transitive_resource_digest);
  EXPECT_NE(
    base_b->transitive_resource_digest, patched_b->transitive_resource_digest);
}

//=== Scenes ===--------------------------------------------------------------//

NOLINT_TEST(PakPlanBuilderPatchTest, PatchPreservesSceneLocalScriptDescriptor)
{
  auto workspace = PakWorkspace {};
  const auto scripts = pak::test::MakeSceneScripts(1U);

  const auto packed = workspace.PackOrThrow(workspace.PatchRequest(
    { workspace.Add(scripts.root) }, { scripts.root.BaseCatalog() }));

  EXPECT_THAT(packed.Descriptor(scripts.scene),
    BytesEqual(pak::test::BuildScriptsScene(1U).Bytes()));
}

//! A patch of a pak over that pak's own catalog changes nothing.
NOLINT_TEST(PakPlanBuilderPatchTest, UnchangedPatchEmitsNoAssets)
{
  auto workspace = PakWorkspace {};
  const auto full = workspace.FullRequest(
    { workspace.Add(pak::test::MakeSceneMask(1U).root) }, "scene.pak");
  const auto base = oxygen::cooker::testing::PakWorkspace::Pack(full);
  ASSERT_THAT(base.Diagnostics(), HasNoErrors());
  const auto request
    = workspace.PatchRequest({ PakWorkspace::PakSource(full.output_pak_path) },
      { base.planned.output_catalog }, "unchanged.pak");

  const auto unchanged = pak::PakPlanBuilder {}.Build(request);

  ASSERT_THAT(unchanged.diagnostics, HasNoErrors());
  ASSERT_HAS_VALUE(unchanged.plan);
  EXPECT_TRUE(unchanged.plan->Assets().empty());
}

//=== Incremental patches mounted by the resolver ===-------------------------//

//! The resolver mounts real files: the paks are exported to the test's
//! directory.
class PakPlanBuilderPatchMountTest : public oxygen::testing::TempDirTest {
protected:
  [[nodiscard]] auto Workspace() noexcept -> PakWorkspace&
  {
    return workspace_;
  }

  //! Packs `request`; returns its plan, whose output catalog a later patch
  //! builds on.
  auto Pack(const pak::PakBuildRequest& request)
    -> pak::PakPlanBuilder::BuildResult
  {
    const auto packed = oxygen::cooker::testing::PakWorkspace::Pack(request);
    EXPECT_THAT(packed.Diagnostics(), HasNoErrors());
    EXPECT_TRUE(packed.written.has_value());
    return packed.planned;
  }

  //! Exports the pak `request` published; returns where it is on disk.
  [[nodiscard]] auto Export(const pak::PakBuildRequest& request) const
    -> std::filesystem::path
  {
    const auto disk_path
      = TempPath(request.output_pak_path.filename().string());
    Materialize(workspace_.Storage().Bytes(request.output_pak_path), disk_path);
    return disk_path;
  }

private:
  PakWorkspace workspace_;
};

//! A material with base color red `red`.
auto TintedMaterial(const float red) -> std::vector<std::byte>
{
  auto record = render::MaterialAssetDesc {};
  record.base_color[0] = red;
  return fixtures::MaterialBuilder { "Tinted", record }.Bytes();
}

//! A base holds M and D; patch 1 tints M and drops D; patch 2, over both,
//! reverts M. Mounted in order, the resolver sees D deleted; flattened, the
//! paks hold M as it was and record D's deletion.
NOLINT_TEST_F(PakPlanBuilderPatchMountTest,
  IncrementalPatchRevertsReplacementAndPreservesDeletion)
{
  const auto original = TintedMaterial(1.0F);
  auto base_state = LooseCookedRoot { "Game", 1U };
  const auto kept = base_state.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "M.omat",
    .descriptor = original,
  });
  const auto dropped = base_state.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "D.omat",
    .descriptor = original,
  });
  auto tinted_state = LooseCookedRoot { "Game", 2U };
  tinted_state.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "M.omat",
    .descriptor = TintedMaterial(0.25F),
  });
  auto reverted_state = LooseCookedRoot { "Game", 3U };
  reverted_state.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "M.omat",
    .descriptor = original,
  });
  auto& workspace = Workspace();

  const auto base_request
    = workspace.FullRequest({ workspace.Add(base_state) }, "base.pak");
  const auto base = Pack(base_request);
  ASSERT_HAS_VALUE(base.plan);

  const auto p1_request = workspace.PatchRequest(
    { workspace.Add(tinted_state) }, { base.output_catalog }, "p1.pak");
  const auto p1 = Pack(p1_request);
  ASSERT_HAS_VALUE(p1.plan);
  EXPECT_EQ(p1.output_catalog.deleted, std::vector { dropped });
  ASSERT_EQ(p1.output_catalog.entries.size(), 1U);

  const auto p2_request
    = workspace.PatchRequest({ workspace.Add(reverted_state) },
      { base.output_catalog, p1.output_catalog }, "p2.pak");
  const auto p2 = Pack(p2_request);
  ASSERT_HAS_VALUE(p2.plan);
  ASSERT_EQ(p2.plan->PatchActions().size(), 1U);
  EXPECT_EQ(base::CheckedAt(p2.plan->PatchActions(), 0U).action,
    PakPatchAction::kReplace);
  EXPECT_EQ(base::CheckedAt(p2.plan->PatchActions(), 0U).asset_key, kept);
  EXPECT_TRUE(p2.output_catalog.deleted.empty());

  auto resolver = oxygen::content::VirtualPathResolver {};
  const auto base_pak = Export(base_request);
  const auto p1_pak = Export(p1_request);
  const auto p2_pak = Export(p2_request);
  EXPECT_THROW(resolver.AddPakFile(p1_pak), std::invalid_argument);
  resolver.AddPakFile(base_pak);
  EXPECT_EQ(
    resolver.ResolveAssetKey(base_state.VirtualPath("D.omat")), dropped);
  resolver.AddPakFile(p1_pak);
  EXPECT_FALSE(
    resolver.ResolveAssetKey(base_state.VirtualPath("D.omat")).has_value());
  resolver.AddPakFile(p2_pak);
  EXPECT_EQ(resolver.ResolveAssetKey(base_state.VirtualPath("M.omat")), kept);

  const auto flatten = workspace.FullRequest(
    {
      PakWorkspace::PakSource(base_request.output_pak_path),
      PakWorkspace::PakSource(p1_request.output_pak_path),
      PakWorkspace::PakSource(p2_request.output_pak_path),
    },
    "flattened.pak");
  const auto flattened = workspace.PackOrThrow(flatten);
  EXPECT_FALSE(flattened.Pak().FindEntry(dropped).has_value());
  EXPECT_EQ(flattened.Pak().Catalog().deleted, std::vector { dropped });
  EXPECT_THAT(flattened.Descriptor(kept), BytesEqual(original));
}

} // namespace
