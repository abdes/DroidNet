//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>

#include "PakTestSupport.h"

namespace {
namespace base = oxygen::base;
namespace data = oxygen::data;
namespace lc = oxygen::data::loose_cooked;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace core = oxygen::data::pak::core;
namespace render = oxygen::data::pak::render;

constexpr auto kDigestPrimaryByteIndex = size_t { 0U };

using AssetSpec = paktest::AssetSpec;
using FileSpec = paktest::FileSpec;

auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  return paktest::MakeSourceKey(seed);
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  return paktest::MakeAssetKey(seed);
}

auto MakeDigest(const uint8_t seed) -> base::Sha256Digest
{
  auto digest = base::Sha256Digest {};
  digest.fill(0U);
  digest[kDigestPrimaryByteIndex] = seed;
  return digest;
}

auto EmptyDigest() -> base::Sha256Digest
{
  static const auto kEmptyDigest
    = base::ComputeSha256(std::span<const std::byte> {});
  return kEmptyDigest;
}

auto HasError(std::span<const pak::PakDiagnostic> diagnostics) -> bool
{
  return paktest::HasError(diagnostics);
}

auto HasDiagnosticCode(std::span<const pak::PakDiagnostic> diagnostics,
  const std::string_view code) -> bool
{
  return paktest::HasDiagnosticCode(diagnostics, code);
}

auto FindAction(const pak::PakPlan& plan, const data::AssetKey& key)
  -> const pak::PakPatchActionRecord*
{
  const auto actions = plan.PatchActions();
  const auto it = std::ranges::find_if(
    actions, [&key](const pak::PakPatchActionRecord& record) {
      return record.asset_key == key;
    });
  if (it == actions.end()) {
    return nullptr;
  }
  return std::addressof(*it);
}

auto ContainsAsset(const pak::PakPlan& plan, const data::AssetKey& key) -> bool
{
  return std::ranges::any_of(
    plan.Assets(), [&key](const pak::PakAssetPlacementPlan& asset) {
      return asset.asset_key == key;
    });
}

auto ShouldContributeToTransitiveDigest(const lc::FileKind kind) -> bool
{
  switch (kind) {
  case lc::FileKind::kTexturesTable:
  case lc::FileKind::kTexturesData:
  case lc::FileKind::kBuffersTable:
  case lc::FileKind::kBuffersData:
  case lc::FileKind::kScriptsTable:
  case lc::FileKind::kScriptsData:
  case lc::FileKind::kScriptBindingsTable:
  case lc::FileKind::kScriptBindingsData:
  case lc::FileKind::kPhysicsTable:
  case lc::FileKind::kPhysicsData:
    return true;
  case lc::FileKind::kUnknown:
    return false;
  }

  return false;
}

auto ComputeTransitiveDigestFromFiles(std::span<const FileSpec> files)
  -> base::Sha256Digest
{
  auto inputs = std::vector<std::pair<uint16_t, base::Sha256Digest>> {};
  for (const auto& file : files) {
    if (!ShouldContributeToTransitiveDigest(file.kind)) {
      continue;
    }
    const auto digest = base::ComputeSha256(
      std::span<const std::byte>(file.payload.data(), file.payload.size()));
    inputs.emplace_back(static_cast<uint16_t>(file.kind), digest);
  }

  if (inputs.empty()) {
    return EmptyDigest();
  }

  std::ranges::sort(inputs, [](const auto& lhs, const auto& rhs) {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;
    }
    return lhs.second < rhs.second;
  });

  auto hasher = base::Sha256 {};
  for (const auto& [kind, digest] : inputs) {
    hasher.Update(std::as_bytes(std::span(&kind, 1)));
    hasher.Update(std::as_bytes(std::span(digest)));
  }
  return hasher.Finalize();
}

auto MakeCatalogEntry(const data::AssetKey& key, const data::AssetType type,
  const base::Sha256Digest& descriptor_digest,
  const base::Sha256Digest& transitive_digest) -> data::PakCatalogEntry
{
  return data::PakCatalogEntry {
    .asset_key = key,
    .asset_type = type,
    .descriptor_digest = descriptor_digest,
    .transitive_resource_digest = transitive_digest,
  };
}

auto MakeBaseCatalog(std::span<const data::PakCatalogEntry> entries)
  -> data::PakCatalog
{
  constexpr auto kBaseCatalogSourceSeed = uint8_t { 0xC1U };
  constexpr auto kBaseCatalogContentVersion = uint16_t { 9U };
  constexpr auto kBaseCatalogDigestSeed = uint8_t { 0x77U };

  return data::PakCatalog {
    .source_key = MakeSourceKey(kBaseCatalogSourceSeed),
    .content_version = kBaseCatalogContentVersion,
    .catalog_digest = MakeDigest(kBaseCatalogDigestSeed),
    .entries
    = std::vector<data::PakCatalogEntry>(entries.begin(), entries.end()),
  };
}

auto MakePatchRequest(const std::filesystem::path& pak_path,
  std::initializer_list<data::CookedSource> sources,
  std::span<const data::PakCatalog> base_catalogs) -> pak::PakBuildRequest
{
  constexpr auto kPatchContentVersion = uint16_t { 42U };
  constexpr auto kPatchSourceSeed = uint8_t { 0xA5U };

  auto manifest_path = pak_path;
  manifest_path.replace_extension(".manifest");

  return pak::PakBuildRequest {
    .mode = pak::BuildMode::kPatch,
    .sources = std::vector<data::CookedSource>(sources.begin(), sources.end()),
    .output_pak_path = pak_path,
    .output_manifest_path = manifest_path,
    .content_version = kPatchContentVersion,
    .source_key = MakeSourceKey(kPatchSourceSeed),
    .base_catalogs = std::vector<data::PakCatalog>(
      base_catalogs.begin(), base_catalogs.end()),
    .patch_compat = {},
    .options = {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = false,
      .compute_crc32 = true,
      .fail_on_warnings = false,
    },
  };
}

auto PatchActionSignature(const pak::PakPlan& plan)
  -> std::vector<std::pair<data::AssetKey, pak::PakPatchAction>>
{
  auto signature
    = std::vector<std::pair<data::AssetKey, pak::PakPatchAction>> {};
  signature.reserve(plan.PatchActions().size());
  for (const auto& action : plan.PatchActions()) {
    signature.emplace_back(action.asset_key, action.action);
  }
  std::ranges::sort(signature,
    [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
  return signature;
}

class PakPatchPlannerTest : public paktest::TempDirFixture { };

NOLINT_TEST_F(PakPatchPlannerTest,
  PatchClassificationIsDeterministicAndEmitsOnlyCreateReplaceDescriptors)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  constexpr auto kDescSize = uint64_t { 24U };
  constexpr auto kCreateAssetSeed = uint8_t { 0x10U };
  constexpr auto kReplaceAssetSeed = uint8_t { 0x11U };
  constexpr auto kUnchangedAssetSeed = uint8_t { 0x12U };
  constexpr auto kDeleteAssetSeed = uint8_t { 0x13U };
  constexpr auto kCreateDescriptorSeed = uint8_t { 0x31U };
  constexpr auto kReplaceDescriptorSeed = uint8_t { 0x32U };
  constexpr auto kUnchangedDescriptorSeed = uint8_t { 0x33U };
  constexpr auto kDeleteDescriptorSeed = uint8_t { 0x44U };
  constexpr auto kTexturePayloadBytes = size_t { 19U };
  constexpr auto kBufferPayloadBytes = size_t { 17U };
  constexpr auto kCreateGuidSeed = uint8_t { 1U };
  constexpr auto kReplaceGuidSeed = uint8_t { 2U };
  constexpr auto kUnchangedGuidSeed = uint8_t { 3U };

  const auto source_create = Root() / "a_create";
  const auto source_replace = Root() / "b_replace";
  const auto source_unchanged = Root() / "c_unchanged";

  const auto create_key = MakeAssetKey(kCreateAssetSeed);
  const auto replace_key = MakeAssetKey(kReplaceAssetSeed);
  const auto unchanged_key = MakeAssetKey(kUnchangedAssetSeed);
  const auto delete_key = MakeAssetKey(kDeleteAssetSeed);

  auto create_asset = AssetSpec {
    .key = create_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "create.desc",
    .virtual_path = "/Content/Patch/Create.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  create_asset.descriptor_sha[0] = kCreateDescriptorSeed;

  auto replace_asset = AssetSpec {
    .key = replace_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "replace.desc",
    .virtual_path = "/Content/Patch/Replace.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  replace_asset.descriptor_sha[0] = kReplaceDescriptorSeed;

  auto unchanged_asset = AssetSpec {
    .key = unchanged_key,
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "unchanged.desc",
    .virtual_path = "/Content/Patch/Unchanged.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  unchanged_asset.descriptor_sha[0] = kUnchangedDescriptorSeed;

  const auto replace_files = std::array<FileSpec, 4> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(render::TextureResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = std::vector<std::byte>(kTexturePayloadBytes),
    },
    FileSpec {
      .kind = lc::FileKind::kBuffersTable,
      .relpath = "Resources/buffers.table",
      .payload = std::vector<std::byte>(sizeof(core::BufferResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kBuffersData,
      .relpath = "Resources/buffers.data",
      .payload = std::vector<std::byte>(kBufferPayloadBytes),
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(source_create,
    std::span<const AssetSpec>(&create_asset, 1U), std::span<const FileSpec> {},
    kCreateGuidSeed));
  ASSERT_TRUE(paktest::WriteLooseIndex(source_replace,
    std::span<const AssetSpec>(&replace_asset, 1U),
    std::span<const FileSpec>(replace_files.data(), replace_files.size()),
    kReplaceGuidSeed));
  ASSERT_TRUE(paktest::WriteLooseIndex(source_unchanged,
    std::span<const AssetSpec>(&unchanged_asset, 1U),
    std::span<const FileSpec> {}, kUnchangedGuidSeed));

  const auto base_entries = std::array<data::PakCatalogEntry, 3> {
    MakeCatalogEntry(replace_key, data::AssetType::kMaterial,
      MakeDigest(kReplaceDescriptorSeed), EmptyDigest()),
    MakeCatalogEntry(unchanged_key, data::AssetType::kScene,
      MakeDigest(kUnchangedDescriptorSeed), EmptyDigest()),
    MakeCatalogEntry(delete_key, data::AssetType::kScript,
      MakeDigest(kDeleteDescriptorSeed), EmptyDigest()),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));

  const auto request_a = MakePatchRequest(Root() / "patch_a.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source_unchanged },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source_create },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source_replace },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));
  const auto request_b = MakePatchRequest(Root() / "patch_b.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source_replace },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source_unchanged },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source_create },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto builder = pak::PakPlanBuilder {};
  const auto result_a = builder.Build(request_a);
  const auto result_b = builder.Build(request_b);

  ASSERT_FALSE(HasError(result_a.diagnostics));
  ASSERT_FALSE(HasError(result_b.diagnostics));
  ASSERT_TRUE(result_a.plan.has_value());
  ASSERT_TRUE(result_b.plan.has_value());

  EXPECT_EQ(
    PatchActionSignature(*result_a.plan), PatchActionSignature(*result_b.plan));

  const auto* create_action = FindAction(*result_a.plan, create_key);
  const auto* replace_action = FindAction(*result_a.plan, replace_key);
  const auto* unchanged_action = FindAction(*result_a.plan, unchanged_key);
  const auto* delete_action = FindAction(*result_a.plan, delete_key);
  ASSERT_NE(create_action, nullptr);
  ASSERT_NE(replace_action, nullptr);
  ASSERT_NE(unchanged_action, nullptr);
  ASSERT_NE(delete_action, nullptr);
  EXPECT_EQ(create_action->action, pak::PakPatchAction::kCreate);
  EXPECT_EQ(replace_action->action, pak::PakPatchAction::kReplace);
  EXPECT_EQ(unchanged_action->action, pak::PakPatchAction::kUnchanged);
  EXPECT_EQ(delete_action->action, pak::PakPatchAction::kDelete);

  EXPECT_TRUE(ContainsAsset(*result_a.plan, create_key));
  EXPECT_TRUE(ContainsAsset(*result_a.plan, replace_key));
  EXPECT_FALSE(ContainsAsset(*result_a.plan, unchanged_key));
  EXPECT_FALSE(ContainsAsset(*result_a.plan, delete_key));

  EXPECT_EQ(result_a.summary.patch_created, 1U);
  EXPECT_EQ(result_a.summary.patch_replaced, 1U);
  EXPECT_EQ(result_a.summary.patch_deleted, 1U);
  EXPECT_EQ(result_a.summary.patch_unchanged, 1U);
}

NOLINT_TEST_F(PakPatchPlannerTest, PatchModeUsesPatchLocalResourcesAndClosure)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  constexpr auto kDescSize = uint64_t { 20U };
  constexpr auto kReplaceAssetSeed = uint8_t { 0x21U };
  constexpr auto kUnchangedAssetSeed = uint8_t { 0x22U };
  constexpr auto kReplaceDescriptorSeed = uint8_t { 0x71U };
  constexpr auto kUnchangedDescriptorSeed = uint8_t { 0x72U };
  constexpr auto kReplaceTexturePayloadBytes = size_t { 23U };
  constexpr auto kUnchangedBufferPayloadBytes = size_t { 29U };
  constexpr auto kReplaceGuidSeed = uint8_t { 5U };
  constexpr auto kUnchangedGuidSeed = uint8_t { 6U };

  const auto source_replace = Root() / "a_replace";
  const auto source_unchanged = Root() / "b_unchanged";

  const auto replace_key = MakeAssetKey(kReplaceAssetSeed);
  const auto unchanged_key = MakeAssetKey(kUnchangedAssetSeed);

  auto replace_asset = AssetSpec {
    .key = replace_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "replace.desc",
    .virtual_path = "/Content/Patch/Replace2.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  replace_asset.descriptor_sha[0] = kReplaceDescriptorSeed;

  auto unchanged_asset = AssetSpec {
    .key = unchanged_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "unchanged.desc",
    .virtual_path = "/Content/Patch/Unchanged2.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  unchanged_asset.descriptor_sha[0] = kUnchangedDescriptorSeed;

  const auto replace_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(render::TextureResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = std::vector<std::byte>(kReplaceTexturePayloadBytes),
    },
  };
  const auto unchanged_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kBuffersTable,
      .relpath = "Resources/buffers.table",
      .payload = std::vector<std::byte>(sizeof(core::BufferResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kBuffersData,
      .relpath = "Resources/buffers.data",
      .payload = std::vector<std::byte>(kUnchangedBufferPayloadBytes),
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(source_replace,
    std::span<const AssetSpec>(&replace_asset, 1U),
    std::span<const FileSpec>(replace_files.data(), replace_files.size()),
    kReplaceGuidSeed));
  ASSERT_TRUE(paktest::WriteLooseIndex(source_unchanged,
    std::span<const AssetSpec>(&unchanged_asset, 1U),
    std::span<const FileSpec>(unchanged_files.data(), unchanged_files.size()),
    kUnchangedGuidSeed));

  const auto unchanged_transitive = ComputeTransitiveDigestFromFiles(
    std::span<const FileSpec>(unchanged_files.data(), unchanged_files.size()));
  const auto base_entries = std::array<data::PakCatalogEntry, 2> {
    MakeCatalogEntry(replace_key, data::AssetType::kMaterial,
      MakeDigest(kReplaceDescriptorSeed), EmptyDigest()),
    MakeCatalogEntry(unchanged_key, data::AssetType::kMaterial,
      MakeDigest(kUnchangedDescriptorSeed), unchanged_transitive),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));
  const auto request = MakePatchRequest(Root() / "patch_local.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source_unchanged },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source_replace },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value());

  const auto* replace_action = FindAction(*result.plan, replace_key);
  const auto* unchanged_action = FindAction(*result.plan, unchanged_key);
  ASSERT_NE(replace_action, nullptr);
  ASSERT_NE(unchanged_action, nullptr);
  EXPECT_EQ(replace_action->action, pak::PakPatchAction::kReplace);
  EXPECT_EQ(unchanged_action->action, pak::PakPatchAction::kUnchanged);

  EXPECT_TRUE(ContainsAsset(*result.plan, replace_key));
  EXPECT_FALSE(ContainsAsset(*result.plan, unchanged_key));
  ASSERT_EQ(result.plan->Resources().size(), 1U);
  EXPECT_EQ(result.plan->Resources()[0].resource_kind, "texture");

  const auto tables = result.plan->Tables();
  const auto texture_table_it = std::ranges::find_if(tables,
    [](const auto& table) { return table.table_name == "texture_table"; });
  const auto buffer_table_it = std::ranges::find_if(tables,
    [](const auto& table) { return table.table_name == "buffer_table"; });
  ASSERT_NE(texture_table_it, tables.end());
  ASSERT_NE(buffer_table_it, tables.end());
  EXPECT_EQ(texture_table_it->count, 1U);
  EXPECT_EQ(buffer_table_it->count, 0U);

  const auto closure = result.plan->PatchClosure();
  ASSERT_EQ(closure.size(), 1U);
  EXPECT_EQ(closure[0].asset_key, replace_key);
  EXPECT_EQ(closure[0].resource_kind, "texture");
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.stage.patch.closure_incomplete"));
}

NOLINT_TEST_F(PakPatchPlannerTest, ReplaceAssetTypeMismatchIsRejected)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  constexpr auto kDescSize = uint64_t { 20U };
  constexpr auto kAssetSeed = uint8_t { 0x31U };
  constexpr auto kSourceDescriptorSeed = uint8_t { 0x55U };
  constexpr auto kBaseDescriptorSeed = uint8_t { 0x12U };
  constexpr auto kGuidSeed = uint8_t { 11U };

  const auto source = Root() / "type_mismatch";
  const auto key = MakeAssetKey(kAssetSeed);

  auto source_asset = AssetSpec {
    .key = key,
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "scene.desc",
    .virtual_path = "/Content/Patch/TypeMismatch.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  source_asset.descriptor_sha[0] = kSourceDescriptorSeed;
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(&source_asset, 1U), std::span<const FileSpec> {},
    kGuidSeed));

  const auto base_entries = std::array<data::PakCatalogEntry, 1> {
    MakeCatalogEntry(key, data::AssetType::kMaterial,
      MakeDigest(kBaseDescriptorSeed), EmptyDigest()),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));
  const auto request = MakePatchRequest(Root() / "type_mismatch.pak",
    { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(HasError(result.diagnostics));
  EXPECT_FALSE(result.plan.has_value());
  EXPECT_TRUE(
    HasDiagnosticCode(result.diagnostics, "pak.plan.replace_type_mismatch"));
}

} // namespace
