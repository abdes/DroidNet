//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakPlanBuilder.cpp, Pak/PakMeasureStore.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "PakSceneTestSupport.h"
#include "PakTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace base = oxygen::base;
namespace cooktest = oxygen::cooker::test;
namespace data = oxygen::data;
namespace lc = oxygen::data::loose_cooked;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace core = oxygen::data::pak::core;
namespace render = oxygen::data::pak::render;

constexpr auto kDigestPrimaryByteIndex = size_t { 0U };

using paktest::AssetSpec;
using paktest::BuildSceneDescriptorWithScriptingSlots;
using paktest::FileSpec;
using paktest::HasDiagnosticCode;
using paktest::HasError;
using paktest::MakeAssetKey;
using paktest::MakeBaseCatalog;
using paktest::MakeSceneScriptAssets;
using paktest::MakeSourceKey;

//! Writing a loose index cannot fail today; a helper still must not carry on
//! silently if it ever does.
auto RequireWritten(const bool written) -> void
{
  if (!written) {
    throw std::runtime_error("cannot write the loose cooked index");
  }
}

auto MakeSingleByteDigest(const uint8_t seed) -> base::Sha256Digest
{
  auto digest = base::Sha256Digest {};
  digest.fill(0U);
  digest.at(kDigestPrimaryByteIndex) = seed;
  return digest;
}

auto BuildTextureRecord(const uint64_t data_offset, const uint32_t size_bytes)
  -> core::TextureResourceDesc
{
  auto record = core::TextureResourceDesc {};
  record.data_offset = data_offset;
  record.size_bytes = size_bytes;
  record.texture_type = 1U;
  record.compression_type = 0U;
  record.width = 1U;
  record.height = 1U;
  record.depth = 1U;
  record.array_layers = 1U;
  record.mip_levels = 1U;
  record.format = 0U;
  record.alignment = 256U;
  return record;
}
auto EmptyDigest() -> base::Sha256Digest
{
  static const auto kEmptyDigest
    = base::ComputeSha256(std::span<const std::byte> {});
  return kEmptyDigest;
}

auto FindCatalogEntry(const data::PakCatalog& catalog,
  const data::AssetKey& key) -> const data::PakCatalogEntry*
{
  const auto it = std::ranges::find(
    catalog.entries, key, &data::PakCatalogEntry::asset_key);
  return it == catalog.entries.end() ? nullptr : std::addressof(*it);
}

auto FindAction(const pak::PakPlan& plan, const data::AssetKey& key)
  -> const pak::PakPatchActionRecord*
{
  const auto actions = plan.PatchActions();
  const auto it = std::ranges::find_if(
    actions, [&key](const pak::PakPatchActionRecord& record) -> bool {
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
    plan.Assets(), [&key](const pak::PakAssetPlacementPlan& asset) -> bool {
      return asset.asset_key == key;
    });
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

auto MakePatchRequest(const std::filesystem::path& pak_path,
  std::initializer_list<data::CookedSource> sources,
  std::span<const data::PakCatalog> base_catalogs) -> pak::PakBuildRequest
{
  constexpr auto kPatchContentVersion = uint16_t { 42U };
  constexpr auto kPatchSourceSeed = uint8_t { 0xA5U };

  return paktest::MakePatchRequest(pak_path,
    std::vector<data::PakCatalog>(base_catalogs.begin(), base_catalogs.end()),
    {
      .sources
      = std::vector<data::CookedSource>(sources.begin(), sources.end()),
      .content_version = kPatchContentVersion,
      .source_key = MakeSourceKey(kPatchSourceSeed),
    });
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
  std::ranges::sort(signature, [](const auto& lhs, const auto& rhs) -> auto {
    return lhs.first < rhs.first;
  });
  return signature;
}

//! The four-way classification scenario.
/*!
  Create and replace come from loose sources, unchanged matches the base
  catalog, and delete only exists in the base catalog. The two requests list
  the same sources in different orders.
*/
struct ClassificationScenario final {
  data::AssetKey create_key;
  data::AssetKey replace_key;
  data::AssetKey unchanged_key;
  data::AssetKey delete_key;
  pak::PakBuildRequest request_a;
  pak::PakBuildRequest request_b;
};

class PakPlanBuilderPatchTest : public paktest::TempDirFixture {
protected:
  [[nodiscard]] auto MakeClassificationScenario() const
    -> ClassificationScenario;
};

auto PakPlanBuilderPatchTest::MakeClassificationScenario() const
  -> ClassificationScenario
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
  constexpr auto kBaseReplaceDescriptorSeed = uint8_t { 0x52U };
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
    .virtual_path = "/Game/Patch/Create.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  create_asset.descriptor_sha.at(0) = kCreateDescriptorSeed;

  auto replace_asset = AssetSpec {
    .key = replace_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "replace.desc",
    .virtual_path = "/Game/Patch/Replace.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  replace_asset.descriptor_sha.at(0) = kReplaceDescriptorSeed;

  auto unchanged_asset = AssetSpec {
    .key = unchanged_key,
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "unchanged.desc",
    .virtual_path = "/Game/Patch/Unchanged.asset",
    .descriptor_size = oxygen::content::test::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = {},
    .descriptor_payload = oxygen::content::test::MakeEmptySceneDescriptor(),
  };
  unchanged_asset.descriptor_sha.at(0) = kUnchangedDescriptorSeed;

  const auto replace_files = std::array<FileSpec, 4> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(core::TextureResourceDesc)),
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

  RequireWritten(paktest::WriteLooseIndex(source_create,
    std::span<const AssetSpec>(&create_asset, 1U), std::span<const FileSpec> {},
    kCreateGuidSeed));
  RequireWritten(paktest::WriteLooseIndex(source_replace,
    std::span<const AssetSpec>(&replace_asset, 1U),
    std::span<const FileSpec>(replace_files.data(), replace_files.size()),
    kReplaceGuidSeed));
  RequireWritten(paktest::WriteLooseIndex(source_unchanged,
    std::span<const AssetSpec>(&unchanged_asset, 1U),
    std::span<const FileSpec> {}, kUnchangedGuidSeed));

  const auto base_entries = std::array<data::PakCatalogEntry, 3> {
    MakeCatalogEntry(replace_key, data::AssetType::kMaterial,
      MakeSingleByteDigest(kBaseReplaceDescriptorSeed),
      MakeSingleByteDigest(kBaseReplaceDescriptorSeed)),
    MakeCatalogEntry(unchanged_key, data::AssetType::kScene,
      MakeSingleByteDigest(kUnchangedDescriptorSeed),
      MakeSingleByteDigest(kUnchangedDescriptorSeed)),
    MakeCatalogEntry(delete_key, data::AssetType::kScript,
      MakeSingleByteDigest(kDeleteDescriptorSeed), EmptyDigest()),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));

  auto request_a = MakePatchRequest(Root() / "patch_a.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_unchanged,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_create,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_replace,
      },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));
  auto request_b = MakePatchRequest(Root() / "patch_b.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_replace,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_unchanged,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_create,
      },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  return ClassificationScenario {
    .create_key = create_key,
    .replace_key = replace_key,
    .unchanged_key = unchanged_key,
    .delete_key = delete_key,
    .request_a = std::move(request_a),
    .request_b = std::move(request_b),
  };
}

NOLINT_TEST_F(PakPlanBuilderPatchTest, ClassifiesCreateReplaceUnchangedDelete)
{
  const auto scenario = MakeClassificationScenario();

  const auto result = pak::PakPlanBuilder {}.Build(scenario.request_a);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_HAS_VALUE(result.plan);

  const auto* create_action = FindAction(*result.plan, scenario.create_key);
  const auto* replace_action = FindAction(*result.plan, scenario.replace_key);
  const auto* unchanged_action
    = FindAction(*result.plan, scenario.unchanged_key);
  const auto* delete_action = FindAction(*result.plan, scenario.delete_key);
  ASSERT_NE(create_action, nullptr);
  ASSERT_NE(replace_action, nullptr);
  ASSERT_NE(unchanged_action, nullptr);
  ASSERT_NE(delete_action, nullptr);
  EXPECT_EQ(create_action->action, pak::PakPatchAction::kCreate);
  EXPECT_EQ(replace_action->action, pak::PakPatchAction::kReplace);
  EXPECT_EQ(unchanged_action->action, pak::PakPatchAction::kUnchanged);
  EXPECT_EQ(delete_action->action, pak::PakPatchAction::kDelete);

  // Only create and replace carry descriptors.
  EXPECT_TRUE(ContainsAsset(*result.plan, scenario.create_key));
  EXPECT_TRUE(ContainsAsset(*result.plan, scenario.replace_key));
  EXPECT_FALSE(ContainsAsset(*result.plan, scenario.unchanged_key));
  EXPECT_FALSE(ContainsAsset(*result.plan, scenario.delete_key));

  EXPECT_EQ(result.summary.patch_created, 1U);
  EXPECT_EQ(result.summary.patch_replaced, 1U);
  EXPECT_EQ(result.summary.patch_deleted, 1U);
  EXPECT_EQ(result.summary.patch_unchanged, 1U);
}

NOLINT_TEST_F(PakPlanBuilderPatchTest, ClassificationIsDeterministic)
{
  const auto scenario = MakeClassificationScenario();

  const auto builder = pak::PakPlanBuilder {};
  const auto result_a = builder.Build(scenario.request_a);
  const auto result_b = builder.Build(scenario.request_b);

  ASSERT_FALSE(HasError(result_a.diagnostics));
  ASSERT_FALSE(HasError(result_b.diagnostics));
  ASSERT_HAS_VALUE(result_a.plan);
  ASSERT_HAS_VALUE(result_b.plan);

  EXPECT_EQ(
    PatchActionSignature(*result_a.plan), PatchActionSignature(*result_b.plan));
}

NOLINT_TEST_F(
  PakPlanBuilderPatchTest, PatchModeUsesPatchLocalResourcesAndClosure)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  constexpr auto kDescSize = uint64_t { 20U };
  constexpr auto kReplaceAssetSeed = uint8_t { 0x21U };
  constexpr auto kUnchangedAssetSeed = uint8_t { 0x22U };
  constexpr auto kReplaceDescriptorSeed = uint8_t { 0x71U };
  constexpr auto kBaseReplaceDescriptorSeed = uint8_t { 0x73U };
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
    .virtual_path = "/Game/Patch/Replace2.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  replace_asset.descriptor_sha.at(0) = kReplaceDescriptorSeed;

  auto unchanged_asset = AssetSpec {
    .key = unchanged_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "unchanged.desc",
    .virtual_path = "/Game/Patch/Unchanged2.asset",
    .descriptor_size = kDescSize,
    .descriptor_sha = {},
  };
  unchanged_asset.descriptor_sha.at(0) = kUnchangedDescriptorSeed;

  const auto replace_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(core::TextureResourceDesc)),
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

  const auto base_entries = std::array<data::PakCatalogEntry, 2> {
    MakeCatalogEntry(replace_key, data::AssetType::kMaterial,
      MakeSingleByteDigest(kBaseReplaceDescriptorSeed),
      MakeSingleByteDigest(kBaseReplaceDescriptorSeed)),
    MakeCatalogEntry(unchanged_key, data::AssetType::kMaterial,
      MakeSingleByteDigest(kUnchangedDescriptorSeed),
      MakeSingleByteDigest(kUnchangedDescriptorSeed)),
  };
  const auto base_catalog
    = MakeBaseCatalog(std::span<const data::PakCatalogEntry>(
      base_entries.data(), base_entries.size()));
  const auto request = MakePatchRequest(Root() / "patch_local.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_unchanged,
      },
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = source_replace,
      },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_HAS_VALUE(result.plan);

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
  const auto texture_table_it
    = std::ranges::find_if(tables, [](const auto& table) -> auto {
        return table.table_name == "texture_table";
      });
  const auto buffer_table_it
    = std::ranges::find_if(tables, [](const auto& table) -> auto {
        return table.table_name == "buffer_table";
      });
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

NOLINT_TEST_F(PakPlanBuilderPatchTest, ReplaceAssetTypeMismatchIsRejected)
{
  using data::CookedSource;
  using data::CookedSourceKind;

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
    .virtual_path = "/Game/Patch/TypeMismatch.asset",
    .descriptor_size = oxygen::content::test::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = {},
    .descriptor_payload = oxygen::content::test::MakeEmptySceneDescriptor(),
  };
  source_asset.descriptor_sha.at(0) = kSourceDescriptorSeed;
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(&source_asset, 1U), std::span<const FileSpec> {},
    kGuidSeed));

  const auto base_entries = std::array<data::PakCatalogEntry, 1> {
    MakeCatalogEntry(key, data::AssetType::kMaterial,
      MakeSingleByteDigest(kBaseDescriptorSeed), EmptyDigest()),
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

NOLINT_TEST_F(PakPlanBuilderPatchTest,
  PatchModeOnlyReplacesChangedAssetWithinSingleLooseCookedSource)
{
  using data::CookedSource;
  using data::CookedSourceKind;

  const auto patch_source = Root() / "single_source_patch";

  const auto asset_a_key = MakeAssetKey(0x61U);
  const auto asset_b_key = MakeAssetKey(0x62U);

  const auto material_a_bytes
    = oxygen::content::test::TexturedMaterialDescriptor(
      "MatA", oxygen::ResourceIndexT { 1U })
        .bytes;
  const auto material_b_bytes
    = oxygen::content::test::TexturedMaterialDescriptor(
      "MatB", oxygen::ResourceIndexT { 2U })
        .bytes;

  const auto material_a_sha = base::ComputeSha256(material_a_bytes);
  const auto material_b_sha = base::ComputeSha256(material_b_bytes);

  const auto texture_fallback_payload
    = std::vector<std::byte>(8U, std::byte { 0x01 });
  const auto texture_a_payload = std::vector<std::byte>(8U, std::byte { 0x11 });
  const auto texture_b_payload_base
    = std::vector<std::byte>(8U, std::byte { 0x22 });
  auto texture_b_payload_patch = std::vector<std::byte>(8U, std::byte { 0x22 });
  texture_b_payload_patch.at(0) = std::byte { 0x33 };

  const auto texture_record_fallback = BuildTextureRecord(
    0U, static_cast<uint32_t>(texture_fallback_payload.size()));
  const auto texture_record_a
    = BuildTextureRecord(texture_fallback_payload.size(),
      static_cast<uint32_t>(texture_a_payload.size()));
  const auto texture_record_b = BuildTextureRecord(
    texture_fallback_payload.size() + texture_a_payload.size(),
    static_cast<uint32_t>(texture_b_payload_base.size()));

  auto textures_table_bytes
    = std::vector<std::byte>(3U * sizeof(core::TextureResourceDesc));
  std::memcpy(textures_table_bytes.data(), &texture_record_fallback,
    sizeof(texture_record_fallback));
  std::memcpy(textures_table_bytes.data() + sizeof(texture_record_fallback),
    &texture_record_a, sizeof(texture_record_a));
  std::memcpy(textures_table_bytes.data() + sizeof(texture_record_fallback)
      + sizeof(texture_record_a),
    &texture_record_b, sizeof(texture_record_b));

  auto textures_data_base = texture_fallback_payload;
  textures_data_base.insert(textures_data_base.end(), texture_a_payload.begin(),
    texture_a_payload.end());
  textures_data_base.insert(textures_data_base.end(),
    texture_b_payload_base.begin(), texture_b_payload_base.end());

  auto textures_data_patch = texture_fallback_payload;
  textures_data_patch.insert(textures_data_patch.end(),
    texture_a_payload.begin(), texture_a_payload.end());
  textures_data_patch.insert(textures_data_patch.end(),
    texture_b_payload_patch.begin(), texture_b_payload_patch.end());

  const auto assets = std::array<AssetSpec, 2> {
    AssetSpec {
      .key = asset_a_key,
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "Materials/MatA.omat",
      .virtual_path = "/Game/MatA.omat",
      .descriptor_size = material_a_bytes.size(),
      .descriptor_sha = material_a_sha,
      .descriptor_payload = material_a_bytes,
      .references = data::AssetReferences::Create(
        {
          {
            .kind = data::ResourceKind::kTexture,
            .index = oxygen::ResourceIndexT { 1U },
          },
        },
        {})
        .value(),
    },
    AssetSpec {
      .key = asset_b_key,
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "Materials/MatB.omat",
      .virtual_path = "/Game/MatB.omat",
      .descriptor_size = material_b_bytes.size(),
      .descriptor_sha = material_b_sha,
      .descriptor_payload = material_b_bytes,
      .references = data::AssetReferences::Create(
        {
          {
            .kind = data::ResourceKind::kTexture,
            .index = oxygen::ResourceIndexT { 2U },
          },
        },
        {})
        .value(),
    },
  };

  const auto base_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = textures_table_bytes,
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = textures_data_base,
    },
  };
  const auto patch_files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = textures_table_bytes,
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = textures_data_patch,
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(patch_source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec>(patch_files.data(), patch_files.size()), 0x92U));
  // The base catalog comes from planning the base state with the production
  // code, so its digests are whatever the planner computes for unchanged
  // inputs.
  const auto base_source = Root() / "single_source_base";
  ASSERT_TRUE(paktest::WriteLooseIndex(base_source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec>(base_files.data(), base_files.size()), 0x91U));

  const auto builder = pak::PakPlanBuilder {};
  const auto base_build
    = builder.Build(paktest::MakeFullRequest(Root() / "single_base.pak",
      { .sources = { CookedSource {
          .kind = CookedSourceKind::kLooseCooked, .path = base_source, }, }, }));
  ASSERT_FALSE(HasError(base_build.diagnostics))
    << cooktest::DiagnosticSummary(base_build.diagnostics);
  const auto& base_catalog = base_build.output_catalog;

  const auto patch_request = MakePatchRequest(Root() / "single_patch.pak",
    {
      CookedSource {
        .kind = CookedSourceKind::kLooseCooked,
        .path = patch_source,
      },
    },
    std::span<const data::PakCatalog>(&base_catalog, 1U));

  const auto patch_result = builder.Build(patch_request);
  ASSERT_FALSE(HasError(patch_result.diagnostics))
    << cooktest::DiagnosticSummary(patch_result.diagnostics);
  ASSERT_HAS_VALUE(patch_result.plan);

  const auto* action_a = FindAction(*patch_result.plan, asset_a_key);
  const auto* action_b = FindAction(*patch_result.plan, asset_b_key);
  ASSERT_NE(action_a, nullptr);
  ASSERT_NE(action_b, nullptr);
  EXPECT_EQ(action_a->action, pak::PakPatchAction::kUnchanged);
  EXPECT_EQ(action_b->action, pak::PakPatchAction::kReplace);

  EXPECT_FALSE(ContainsAsset(*patch_result.plan, asset_a_key));
  EXPECT_TRUE(ContainsAsset(*patch_result.plan, asset_b_key));
  ASSERT_EQ(patch_result.plan->Resources().size(), 1U);
  EXPECT_EQ(patch_result.plan->Resources()[0].resource_kind, "texture");

  const auto closure = patch_result.plan->PatchClosure();
  ASSERT_EQ(closure.size(), 1U);
  EXPECT_EQ(closure[0].asset_key, asset_b_key);
  EXPECT_EQ(closure[0].resource_kind, "texture");

  EXPECT_EQ(patch_result.summary.patch_created, 0U);
  EXPECT_EQ(patch_result.summary.patch_replaced, 1U);
  EXPECT_EQ(patch_result.summary.patch_deleted, 0U);
  EXPECT_EQ(patch_result.summary.patch_unchanged, 1U);

  // The planner's own digests must move with the inputs only: A's resources
  // are identical in both states, B's texture payload differs.
  const auto patched
    = builder.Build(paktest::MakeFullRequest(Root() / "single_patched.pak",
      { .sources = { CookedSource {
          .kind = CookedSourceKind::kLooseCooked, .path = patch_source, }, }, }));
  ASSERT_FALSE(HasError(patched.diagnostics))
    << cooktest::DiagnosticSummary(patched.diagnostics);
  const auto* base_a = FindCatalogEntry(base_catalog, asset_a_key);
  const auto* base_b = FindCatalogEntry(base_catalog, asset_b_key);
  const auto* patched_a = FindCatalogEntry(patched.output_catalog, asset_a_key);
  const auto* patched_b = FindCatalogEntry(patched.output_catalog, asset_b_key);
  ASSERT_NE(base_a, nullptr);
  ASSERT_NE(base_b, nullptr);
  ASSERT_NE(patched_a, nullptr);
  ASSERT_NE(patched_b, nullptr);
  EXPECT_EQ(
    base_a->transitive_resource_digest, patched_a->transitive_resource_digest);
  EXPECT_NE(
    base_b->transitive_resource_digest, patched_b->transitive_resource_digest);
}

NOLINT_TEST_F(PakPlanBuilderPatchTest, PatchPreservesSceneLocalScriptDescriptor)
{
  const auto source = Root() / "scene";
  const auto assets = MakeSceneScriptAssets(1U);
  ASSERT_TRUE(paktest::WriteLooseIndex(source, assets, {}, 1U));
  const auto base = MakeBaseCatalog({});
  const auto request = MakePatchRequest(Root() / "scene-patch.pak",
    { { .kind = data::CookedSourceKind::kLooseCooked, .path = source } },
    std::span(&base, 1U));
  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics))
    << cooktest::DiagnosticSummary(result.diagnostics);
  ASSERT_HAS_VALUE(result.plan);
  ASSERT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *result.plan).diagnostics));
  auto archive = oxygen::content::PakFile(request.output_pak_path);
  const auto entry = archive.FindEntry(MakeAssetKey(1U));
  ASSERT_HAS_VALUE(entry);
  auto reader = archive.CreateReader(*entry);
  const auto bytes = reader.ReadBlob(entry->desc_size);
  ASSERT_HAS_VALUE(bytes);
  EXPECT_EQ(*bytes, BuildSceneDescriptorWithScriptingSlots(1U));
}

NOLINT_TEST_F(PakPlanBuilderPatchTest,
  IncrementalPatchRevertsReplacementAndPreservesDeletion)
{
  const auto root = Root() / "source";
  auto material = render::MaterialAssetDesc {};
  material.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
  material.header.version = render::kMaterialAssetVersion;
  const auto material_bytes = [&] -> std::vector<std::byte> {
    const auto bytes = std::as_bytes(std::span { &material, 1U });
    return { bytes.begin(), bytes.end() };
  };
  auto original_bytes = material_bytes();
  auto assets = std::array {
    AssetSpec {
      .key = MakeAssetKey(1U),
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "M.omat",
      .virtual_path = "/Game/M.omat",
      .descriptor_size = original_bytes.size(),
      .descriptor_sha = oxygen::base::ComputeSha256(original_bytes),
      .descriptor_payload = original_bytes,
      .references = {},
    },
    AssetSpec {
      .key = MakeAssetKey(2U),
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "D.omat",
      .virtual_path = "/Game/D.omat",
      .descriptor_size = original_bytes.size(),
      .descriptor_sha = oxygen::base::ComputeSha256(original_bytes),
      .descriptor_payload = original_bytes,
      .references = {},
    },
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(root, assets, {}, 1U));
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = root } },
    .output_pak_path = Root() / "base.pak",
    .content_version = 1U,
    .source_key = MakeSourceKey(1U),
  };
  request.options.embed_browse_index = true;
  const auto build_and_write = [&] -> pak::PakPlanBuilder::BuildResult {
    auto result = pak::PakPlanBuilder {}.Build(request);
    for (const auto& diagnostic : result.diagnostics) {
      EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
        << diagnostic.code << ": " << diagnostic.message;
    }
    if (result.plan) {
      EXPECT_FALSE(
        HasError(pak::PakWriter {}.Write(request, *result.plan).diagnostics));
    }
    return result;
  };
  const auto base = build_and_write();
  ASSERT_HAS_VALUE(base.plan);

  material.base_color[0] = 0.25F;
  assets.at(0).descriptor_payload = material_bytes();
  assets.at(0).descriptor_sha
    = oxygen::base::ComputeSha256(assets.at(0).descriptor_payload);
  ASSERT_TRUE(
    paktest::WriteLooseIndex(root, std::span(assets).first(1U), {}, 2U));
  request.mode = pak::BuildMode::kPatch;
  request.source_key = MakeSourceKey(2U);
  request.output_pak_path = Root() / "p1.pak";
  request.output_manifest_path = Root() / "p1.manifest.json";
  request.base_catalogs = { base.output_catalog };
  const auto p1 = build_and_write();
  ASSERT_HAS_VALUE(p1.plan);
  EXPECT_EQ(p1.output_catalog.deleted, std::vector { assets.at(1).key });
  ASSERT_EQ(p1.output_catalog.entries.size(), 1U);

  assets.at(0).descriptor_payload = original_bytes;
  assets.at(0).descriptor_sha = oxygen::base::ComputeSha256(original_bytes);
  ASSERT_TRUE(
    paktest::WriteLooseIndex(root, std::span(assets).first(1U), {}, 3U));
  request.source_key = MakeSourceKey(3U);
  request.output_pak_path = Root() / "p2.pak";
  request.output_manifest_path = Root() / "p2.manifest.json";
  request.base_catalogs.push_back(p1.output_catalog);
  const auto p2 = build_and_write();
  ASSERT_HAS_VALUE(p2.plan);
  ASSERT_EQ(p2.plan->PatchActions().size(), 1U);
  EXPECT_EQ(base::CheckedAt(p2.plan->PatchActions(), 0U).action,
    pak::PakPatchAction::kReplace);
  EXPECT_EQ(
    base::CheckedAt(p2.plan->PatchActions(), 0U).asset_key, assets.at(0).key);
  EXPECT_TRUE(p2.output_catalog.deleted.empty());

  oxygen::content::VirtualPathResolver resolver;
  EXPECT_THROW(resolver.AddPakFile(Root() / "p1.pak"), std::invalid_argument);
  resolver.AddPakFile(Root() / "base.pak");
  EXPECT_EQ(resolver.ResolveAssetKey("/Game/D.omat"), assets.at(1).key);
  resolver.AddPakFile(Root() / "p1.pak");
  EXPECT_FALSE(resolver.ResolveAssetKey("/Game/D.omat").has_value());
  resolver.AddPakFile(Root() / "p2.pak");
  EXPECT_EQ(resolver.ResolveAssetKey("/Game/M.omat"), assets.at(0).key);

  request.mode = pak::BuildMode::kFull;
  request.base_catalogs.clear();
  request.output_manifest_path.clear();
  request.output_pak_path = Root() / "flattened.pak";
  request.source_key = MakeSourceKey(4U);
  request.sources = {
    { .kind = data::CookedSourceKind::kPak, .path = Root() / "base.pak" },
    { .kind = data::CookedSourceKind::kPak, .path = Root() / "p1.pak" },
    { .kind = data::CookedSourceKind::kPak, .path = Root() / "p2.pak" },
  };
  const auto flattened = build_and_write();
  ASSERT_HAS_VALUE(flattened.plan);
  const oxygen::content::PakFile archive(request.output_pak_path);
  EXPECT_FALSE(archive.FindEntry(assets.at(1).key).has_value());
  EXPECT_EQ(archive.Catalog().deleted, std::vector { assets.at(1).key });
  const auto entry = archive.FindEntry(assets.at(0).key);
  ASSERT_HAS_VALUE(entry);
  auto reader = archive.CreateReader(*entry);
  const auto bytes = reader.ReadBlob(entry->desc_size);
  ASSERT_HAS_VALUE(bytes);
  EXPECT_EQ(*bytes, original_bytes);
}

NOLINT_TEST_F(PakPlanBuilderPatchTest, UnchangedPatchEmitsNoAssets)
{
  const auto source_pak = paktest::BuildSceneMaskPak(Root());

  auto request = source_pak.request;
  request.mode = pak::BuildMode::kPatch;
  request.sources = {
    {
      .kind = data::CookedSourceKind::kPak,
      .path = request.output_pak_path,
    },
  };
  request.output_pak_path = Root() / "unchanged.pak";
  request.output_manifest_path = Root() / "unchanged.manifest.json";
  request.source_key = MakeSourceKey(6U);
  request.base_catalogs = { source_pak.catalog };

  const auto unchanged = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(unchanged.diagnostics));
  ASSERT_HAS_VALUE(unchanged.plan);
  EXPECT_TRUE(unchanged.plan->Assets().empty());
}

} // namespace
