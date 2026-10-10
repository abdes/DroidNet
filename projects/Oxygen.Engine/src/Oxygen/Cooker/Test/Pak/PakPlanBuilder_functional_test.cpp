//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakPlanBuilder.cpp, Pak/PakWriter.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakSceneTestSupport.h"
#include "PakTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakPlan.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

using oxygen::base::CheckedAt;

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace lc = oxygen::data::loose_cooked;
namespace core = oxygen::data::pak::core;
namespace physics = oxygen::data::pak::physics;
namespace script = oxygen::data::pak::scripting;

using paktest::AssetSpec;
using paktest::FileSpec;
using paktest::HasDiagnosticCode;
using paktest::HasError;
using paktest::MakeAssetKey;
using paktest::MakePhysicsSidecarSpec;

auto MakeNonZeroSourceKey(const uint8_t seed) -> data::SourceKey
{
  return paktest::MakeSourceKey(seed);
}

auto FindTable(const pak::PakPlan& plan, const std::string_view name)
  -> const pak::PakTablePlan*
{
  const auto tables = plan.Tables();
  const auto it = std::ranges::find_if(tables,
    [name](const auto& table) -> auto { return table.table_name == name; });
  if (it == tables.end()) {
    return nullptr;
  }
  return std::addressof(*it);
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

auto CheckPackagedPhysics(oxygen::co::testing::TestEventLoop* loop,
  std::filesystem::path path, data::SourceKey source) -> oxygen::co::Co<>
{
  oxygen::co::ThreadPool pool(*loop, 2);
  oxygen::content::AssetLoaderConfig config {};
  config.thread_pool = oxygen::observer_ptr(&pool);
  oxygen::content::AssetLoader loader(
    oxygen::engine::internal::EngineTagFactory::Get(), config);
  OXCO_WITH_NURSERY(nursery)
  {
    co_await nursery.Start(
      &oxygen::content::AssetLoader::ActivateAsync, &loader);
    loader.Run();
    loader.AddPakFile(path);
    const auto key
      = loader.MakePhysicsResourceKey(source, core::ResourceIndexT { 1U });
    if (!key) {
      ADD_FAILURE() << "Packed physics payload has no locator";
      loader.Stop();
      co_return oxygen::co::kJoin;
    }
    const auto payload = co_await loader.LoadPhysicsResourceAsync(*key);
    if (!payload) {
      ADD_FAILURE() << "Packed physics payload did not load";
    } else {
      EXPECT_THAT(payload->GetData(), ::testing::ElementsAre(2U, 2U, 2U, 2U));
    }
    loader.Stop();
    co_return oxygen::co::kJoin;
  };
}

class PakPlanBuilderTest : public paktest::TempDirFixture { };

auto ExpectPlansEquivalent(const pak::PakPlan& lhs, const pak::PakPlan& rhs)
  -> void
{
  EXPECT_EQ(lhs.Header().offset, rhs.Header().offset);
  EXPECT_EQ(lhs.Header().size_bytes, rhs.Header().size_bytes);
  EXPECT_EQ(lhs.Header().content_version, rhs.Header().content_version);
  EXPECT_EQ(lhs.Header().source_key.get(), rhs.Header().source_key.get());
  EXPECT_EQ(lhs.PlannedFileSize(), rhs.PlannedFileSize());

  const auto lhs_regions = lhs.Regions();
  const auto rhs_regions = rhs.Regions();
  ASSERT_EQ(lhs_regions.size(), rhs_regions.size());
  for (size_t i = 0; i < lhs_regions.size(); ++i) {
    EXPECT_EQ(CheckedAt(lhs_regions, i).region_name,
      CheckedAt(rhs_regions, i).region_name);
    EXPECT_EQ(
      CheckedAt(lhs_regions, i).offset, CheckedAt(rhs_regions, i).offset);
    EXPECT_EQ(CheckedAt(lhs_regions, i).size_bytes,
      CheckedAt(rhs_regions, i).size_bytes);
    EXPECT_EQ(
      CheckedAt(lhs_regions, i).alignment, CheckedAt(rhs_regions, i).alignment);
  }

  const auto lhs_tables = lhs.Tables();
  const auto rhs_tables = rhs.Tables();
  ASSERT_EQ(lhs_tables.size(), rhs_tables.size());
  for (size_t i = 0; i < lhs_tables.size(); ++i) {
    EXPECT_EQ(
      CheckedAt(lhs_tables, i).table_name, CheckedAt(rhs_tables, i).table_name);
    EXPECT_EQ(CheckedAt(lhs_tables, i).offset, CheckedAt(rhs_tables, i).offset);
    EXPECT_EQ(
      CheckedAt(lhs_tables, i).size_bytes, CheckedAt(rhs_tables, i).size_bytes);
    EXPECT_EQ(CheckedAt(lhs_tables, i).count, CheckedAt(rhs_tables, i).count);
    EXPECT_EQ(
      CheckedAt(lhs_tables, i).entry_size, CheckedAt(rhs_tables, i).entry_size);
    EXPECT_EQ(CheckedAt(lhs_tables, i).expected_entry_size,
      CheckedAt(rhs_tables, i).expected_entry_size);
    EXPECT_EQ(
      CheckedAt(lhs_tables, i).alignment, CheckedAt(rhs_tables, i).alignment);
    EXPECT_EQ(CheckedAt(lhs_tables, i).index_zero_required,
      CheckedAt(rhs_tables, i).index_zero_required);
    EXPECT_EQ(CheckedAt(lhs_tables, i).index_zero_present,
      CheckedAt(rhs_tables, i).index_zero_present);
    EXPECT_EQ(CheckedAt(lhs_tables, i).index_zero_forbidden,
      CheckedAt(rhs_tables, i).index_zero_forbidden);
  }

  const auto lhs_assets = lhs.Assets();
  const auto rhs_assets = rhs.Assets();
  ASSERT_EQ(lhs_assets.size(), rhs_assets.size());
  for (size_t i = 0; i < lhs_assets.size(); ++i) {
    EXPECT_EQ(
      CheckedAt(lhs_assets, i).asset_key, CheckedAt(rhs_assets, i).asset_key);
    EXPECT_EQ(
      CheckedAt(lhs_assets, i).asset_type, CheckedAt(rhs_assets, i).asset_type);
    EXPECT_EQ(CheckedAt(lhs_assets, i).offset, CheckedAt(rhs_assets, i).offset);
    EXPECT_EQ(
      CheckedAt(lhs_assets, i).size_bytes, CheckedAt(rhs_assets, i).size_bytes);
    EXPECT_EQ(
      CheckedAt(lhs_assets, i).alignment, CheckedAt(rhs_assets, i).alignment);
    EXPECT_EQ(CheckedAt(lhs_assets, i).reserved_bytes_zeroed,
      CheckedAt(rhs_assets, i).reserved_bytes_zeroed);
  }

  const auto lhs_resources = lhs.Resources();
  const auto rhs_resources = rhs.Resources();
  ASSERT_EQ(lhs_resources.size(), rhs_resources.size());
  for (size_t i = 0; i < lhs_resources.size(); ++i) {
    EXPECT_EQ(CheckedAt(lhs_resources, i).resource_kind,
      CheckedAt(rhs_resources, i).resource_kind);
    EXPECT_EQ(CheckedAt(lhs_resources, i).resource_index,
      CheckedAt(rhs_resources, i).resource_index);
    EXPECT_EQ(CheckedAt(lhs_resources, i).region_name,
      CheckedAt(rhs_resources, i).region_name);
    EXPECT_EQ(
      CheckedAt(lhs_resources, i).offset, CheckedAt(rhs_resources, i).offset);
    EXPECT_EQ(CheckedAt(lhs_resources, i).size_bytes,
      CheckedAt(rhs_resources, i).size_bytes);
    EXPECT_EQ(CheckedAt(lhs_resources, i).alignment,
      CheckedAt(rhs_resources, i).alignment);
    EXPECT_EQ(CheckedAt(lhs_resources, i).reserved_bytes_zeroed,
      CheckedAt(rhs_resources, i).reserved_bytes_zeroed);
  }

  EXPECT_EQ(lhs.Directory().offset, rhs.Directory().offset);
  EXPECT_EQ(lhs.Directory().size_bytes, rhs.Directory().size_bytes);
  ASSERT_EQ(lhs.Directory().entries.size(), rhs.Directory().entries.size());
  for (size_t i = 0; i < lhs.Directory().entries.size(); ++i) {
    const auto& lhs_entry = lhs.Directory().entries.at(i);
    const auto& rhs_entry = rhs.Directory().entries.at(i);
    EXPECT_EQ(lhs_entry.asset_key, rhs_entry.asset_key);
    EXPECT_EQ(lhs_entry.asset_type, rhs_entry.asset_type);
    EXPECT_EQ(lhs_entry.entry_offset, rhs_entry.entry_offset);
    EXPECT_EQ(lhs_entry.descriptor_offset, rhs_entry.descriptor_offset);
    EXPECT_EQ(lhs_entry.descriptor_size, rhs_entry.descriptor_size);
  }

  EXPECT_EQ(lhs.BrowseIndex().enabled, rhs.BrowseIndex().enabled);
  EXPECT_EQ(lhs.BrowseIndex().offset, rhs.BrowseIndex().offset);
  EXPECT_EQ(lhs.BrowseIndex().size_bytes, rhs.BrowseIndex().size_bytes);
  ASSERT_EQ(lhs.BrowseIndex().entries.size(), rhs.BrowseIndex().entries.size());
  for (size_t i = 0; i < lhs.BrowseIndex().entries.size(); ++i) {
    const auto& lhs_entry = lhs.BrowseIndex().entries.at(i);
    const auto& rhs_entry = rhs.BrowseIndex().entries.at(i);
    EXPECT_EQ(lhs_entry.asset_key, rhs_entry.asset_key);
    EXPECT_EQ(lhs_entry.virtual_path, rhs_entry.virtual_path);
  }

  EXPECT_EQ(lhs.Footer().offset, rhs.Footer().offset);
  EXPECT_EQ(lhs.Footer().size_bytes, rhs.Footer().size_bytes);
  EXPECT_EQ(lhs.Footer().crc32_field_absolute_offset,
    rhs.Footer().crc32_field_absolute_offset);
}

auto ExpectCatalogsEquivalent(
  const data::PakCatalog& lhs, const data::PakCatalog& rhs) -> void
{
  EXPECT_EQ(lhs.source_key, rhs.source_key);
  EXPECT_EQ(lhs.content_version, rhs.content_version);
  EXPECT_EQ(lhs.catalog_digest, rhs.catalog_digest);
  ASSERT_EQ(lhs.entries.size(), rhs.entries.size());
  for (size_t i = 0; i < lhs.entries.size(); ++i) {
    EXPECT_EQ(lhs.entries.at(i).asset_key, rhs.entries.at(i).asset_key);
    EXPECT_EQ(lhs.entries.at(i).asset_type, rhs.entries.at(i).asset_type);
    EXPECT_EQ(
      lhs.entries.at(i).descriptor_digest, rhs.entries.at(i).descriptor_digest);
    EXPECT_EQ(lhs.entries.at(i).transitive_resource_digest,
      rhs.entries.at(i).transitive_resource_digest);
  }
}

NOLINT_TEST_F(
  PakPlanBuilderTest, DeterministicPlanningPreservesDeclaredLayerPriority)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kAssetSeed = uint8_t { 0x21 };
  constexpr auto kDescriptorSize = uint64_t { 32U };
  constexpr auto kSourceKeySeed = uint8_t { 0x7A };
  constexpr auto kGuidA = uint8_t { 7U };
  constexpr auto kGuidZ = uint8_t { 19U };
  constexpr auto kShaA = uint8_t { 0x11U };
  constexpr auto kShaZ = uint8_t { 0x22U };

  const auto source_a = Root() / "a_source";
  const auto source_z = Root() / "z_source";

  auto asset_a = AssetSpec {
    .key = MakeAssetKey(kAssetSeed),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "A.desc",
    .virtual_path = "/Game/Asset.win",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_a.descriptor_sha.at(0) = kShaA;

  auto asset_z = AssetSpec {
    .key = MakeAssetKey(kAssetSeed),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "Z.desc",
    .virtual_path = "/Game/Asset.win",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_z.descriptor_sha.at(0) = kShaZ;

  ASSERT_TRUE(
    paktest::WriteLooseIndex(source_a, std::span<const AssetSpec>(&asset_a, 1U),
      std::span<const FileSpec> {}, kGuidA));
  ASSERT_TRUE(
    paktest::WriteLooseIndex(source_z, std::span<const AssetSpec>(&asset_z, 1U),
      std::span<const FileSpec> {}, kGuidZ));

  const auto options = pak::PakBuildOptions {
    .deterministic = true,
    .embed_browse_index = true,
    .emit_manifest_in_full = false,
    .compute_crc32 = true,
    .fail_on_warnings = false,
  };

  const auto request_a = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {
      CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source_z },
      CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source_a },
    },
    .output_pak_path = Root() / "det_a.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(kSourceKeySeed),
    .base_catalogs = {},

    .options = options,
  };

  const auto request_b = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {
      CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source_a },
      CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source_z },
    },
    .output_pak_path = Root() / "det_b.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(kSourceKeySeed),
    .base_catalogs = {},

    .options = options,
  };

  const auto builder = PakPlanBuilder {};
  const auto result_a = builder.Build(request_a);
  const auto result_b = builder.Build(request_b);

  ASSERT_FALSE(HasError(result_a.diagnostics));
  ASSERT_FALSE(HasError(result_b.diagnostics));
  ASSERT_TRUE(result_a.plan.has_value())
    << "Expected result_a.plan to contain a value";
  ASSERT_TRUE(result_b.plan.has_value())
    << "Expected result_b.plan to contain a value";

  const auto repeated = builder.Build(request_a);
  ASSERT_FALSE(HasError(repeated.diagnostics));
  ASSERT_TRUE(repeated.plan.has_value());
  ExpectPlansEquivalent(*result_a.plan, *repeated.plan);
  ExpectCatalogsEquivalent(result_a.output_catalog, repeated.output_catalog);

  const auto assets = result_a.plan->Assets();
  ASSERT_EQ(assets.size(), 1U);
  EXPECT_EQ(CheckedAt(assets, 0).asset_type, data::AssetType::kMaterial);
  ASSERT_EQ(result_a.output_catalog.entries.size(), 1U);
  EXPECT_EQ(
    result_a.output_catalog.entries.at(0).asset_key, MakeAssetKey(kAssetSeed));
  EXPECT_EQ(result_a.output_catalog.entries.at(0).asset_type,
    data::AssetType::kMaterial);
  EXPECT_EQ(result_a.output_catalog.entries.at(0).descriptor_digest,
    asset_a.descriptor_sha);
  ASSERT_EQ(result_b.output_catalog.entries.size(), 1U);
  EXPECT_EQ(result_b.output_catalog.entries.at(0).descriptor_digest,
    asset_z.descriptor_sha);

  // Same key, different type is invalid even when override order is explicit.
  asset_z.asset_type = data::AssetType::kScene;
  asset_z.descriptor_payload
    = oxygen::content::test::MakeEmptySceneDescriptor();
  asset_z.descriptor_size = asset_z.descriptor_payload.size();
  ASSERT_TRUE(
    paktest::WriteLooseIndex(source_z, std::span<const AssetSpec>(&asset_z, 1U),
      std::span<const FileSpec> {}, kGuidZ));
  const auto incompatible = builder.Build(request_b);
  EXPECT_TRUE(HasDiagnosticCode(
    incompatible.diagnostics, "pak.plan.asset_override_type_mismatch"));
  EXPECT_FALSE(incompatible.plan.has_value());
}

NOLINT_TEST_F(PakPlanBuilderTest, FullModeIncludesInputAssetsFromLooseSource)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 42U };

  const auto source = Root() / "full_include_input_assets";

  const auto action_asset = AssetSpec {
    .key = MakeAssetKey(0x51U),
    .asset_type = data::AssetType::kInputAction,
    .descriptor_relpath = "Descriptors/Input/Move.oiact",
    .virtual_path = "/Game/Input/Move.oiact",
    .descriptor_size = 48U,
    .descriptor_sha = paktest::MakeDigest(0x51U),
  };
  const auto context_asset = AssetSpec {
    .key = MakeAssetKey(0x52U),
    .asset_type = data::AssetType::kInputMappingContext,
    .descriptor_relpath = "Descriptors/Input/Gameplay.oimap",
    .virtual_path = "/Game/Input/Gameplay.oimap",
    .descriptor_size = 64U,
    .descriptor_sha = paktest::MakeDigest(0x52U),
  };
  const auto scene_asset = AssetSpec {
    .key = MakeAssetKey(0x53U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Descriptors/Scenes/Main.oscene",
    .virtual_path = "/Game/Scenes/Main.oscene",
    .descriptor_size = oxygen::content::test::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = paktest::MakeDigest(0x53U),
    .descriptor_payload = oxygen::content::test::MakeEmptySceneDescriptor(),
  };
  const auto assets
    = std::array<AssetSpec, 3> { action_asset, context_asset, scene_asset };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec> {}, kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source, }, },
    .output_pak_path = Root() / "full_include_input_assets.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(0x91U),
    .base_catalogs = {},

    .options = {},
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value())
    << "Expected result.plan to contain a value";

  const auto plan_assets = result.plan->Assets();
  ASSERT_EQ(plan_assets.size(), assets.size());

  auto saw_input_action = false;
  auto saw_input_mapping_context = false;
  auto saw_scene = false;
  for (const auto& asset : plan_assets) {
    if (asset.asset_type == data::AssetType::kInputAction) {
      saw_input_action = true;
    } else if (asset.asset_type == data::AssetType::kInputMappingContext) {
      saw_input_mapping_context = true;
    } else if (asset.asset_type == data::AssetType::kScene) {
      saw_scene = true;
    }
  }
  EXPECT_TRUE(saw_input_action);
  EXPECT_TRUE(saw_input_mapping_context);
  EXPECT_TRUE(saw_scene);

  const auto& directory_entries = result.plan->Directory().entries;
  ASSERT_EQ(directory_entries.size(), assets.size());
  auto dir_input_action_count = size_t { 0 };
  auto dir_input_mapping_context_count = size_t { 0 };
  auto dir_scene_count = size_t { 0 };
  for (const auto& entry : directory_entries) {
    if (entry.asset_type == data::AssetType::kInputAction) {
      ++dir_input_action_count;
    } else if (entry.asset_type == data::AssetType::kInputMappingContext) {
      ++dir_input_mapping_context_count;
    } else if (entry.asset_type == data::AssetType::kScene) {
      ++dir_scene_count;
    }
  }
  EXPECT_EQ(dir_input_action_count, 1U);
  EXPECT_EQ(dir_input_mapping_context_count, 1U);
  EXPECT_EQ(dir_scene_count, 1U);
}

NOLINT_TEST_F(PakPlanBuilderTest, IndexZeroPolicyAppliedForResourceTables)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 41U };
  constexpr auto kDescriptorSize = uint64_t { 24U };

  const auto source = Root() / "resource_tables";
  auto script_asset = AssetSpec {
    .key = MakeAssetKey(0x55U),
    .asset_type = data::AssetType::kScript,
    .descriptor_relpath = "script.desc",
    .virtual_path = "/Game/Script.main",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  script_asset.descriptor_sha.at(0) = 0x55U;

  const auto files = std::array<FileSpec, 4> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(core::TextureResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kTexturesData,
      .relpath = "Resources/textures.data",
      .payload = {},
    },
    FileSpec {
      .kind = lc::FileKind::kScriptsTable,
      .relpath = "Resources/scripts.table",
      .payload = std::vector<std::byte>(sizeof(script::ScriptResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kScriptsData,
      .relpath = "Resources/scripts.data",
      .payload = {},
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(&script_asset, 1U),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source, }, },
    .output_pak_path = Root() / "index0.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(11U),
    .base_catalogs = {},

    .options = {},
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value())
    << "Expected result.plan to contain a value";

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

NOLINT_TEST_F(
  PakPlanBuilderTest, BrowsePayloadPlanSizeMatchesSerializedShapeInvariant)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 53U };
  constexpr auto kDescriptorSize = uint64_t { 20U };

  const auto source = Root() / "browse_payload";
  auto asset_a = AssetSpec {
    .key = MakeAssetKey(0x61U),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "a.desc",
    .virtual_path = "/Game/Browse/B.asset",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_a.descriptor_sha.at(0) = 0x61U;

  auto asset_b = AssetSpec {
    .key = MakeAssetKey(0x62U),
    .asset_type = data::AssetType::kScript,
    .descriptor_relpath = "b.desc",
    .virtual_path = "/Game/Browse/A.asset",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_b.descriptor_sha.at(0) = 0x62U;

  const auto assets = std::array<AssetSpec, 2> { asset_a, asset_b };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec> {}, kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "browse_payload.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(0xB2U),
    .base_catalogs = {},

    .options = {
      .deterministic = true,
      .embed_browse_index = true,
      .emit_manifest_in_full = false,
      .compute_crc32 = true,
      .fail_on_warnings = false,
    },
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value())
    << "Expected result.plan to contain a value";

  const auto& browse = result.plan->BrowseIndex();
  ASSERT_TRUE(browse.enabled);
  ASSERT_EQ(browse.entries.size(), 2U);
  EXPECT_LT(
    browse.entries.at(0).virtual_path, browse.entries.at(1).virtual_path);

  const auto expected_payload_size
    = ComputeExpectedBrowsePayloadSize(std::span<const pak::PakBrowseEntryPlan>(
      browse.entries.data(), browse.entries.size()));
  EXPECT_EQ(browse.size_bytes, expected_payload_size);
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.stage.layout.browse_store_failed"));
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.stage.layout.browse_size_mismatch"));
}

//! Planning the scene mask sources leaves scenes whose mask binding needs no
//! remapping byte-for-byte untouched, and the plan is writable.
NOLINT_TEST_F(PakPlanBuilderTest, RemapsSceneMaskIndicesAcrossCookedSources)
{
  const auto fixture = paktest::WriteSceneMaskSources(Root());
  const auto request
    = paktest::MakeSceneMaskRequest(Root() / "masks.pak", fixture.sources);

  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value());

  const auto assets = result.plan->Assets();
  const auto payloads = result.plan->AssetPayloadSources();
  ASSERT_EQ(assets.size(), 6U);
  bool found_second_scene = false;
  for (size_t index = 0; index < assets.size(); ++index) {
    if (CheckedAt(assets, index).asset_key != MakeAssetKey(20U)) {
      continue;
    }
    EXPECT_TRUE(CheckedAt(payloads, index).inline_bytes.empty());
    EXPECT_EQ(
      CheckedAt(payloads, index).size_bytes, fixture.scene_descriptor_size);
    found_second_scene = true;
    break;
  }
  ASSERT_TRUE(found_second_scene);
  const auto written = pak::PakWriter {}.Write(request, *result.plan);
  ASSERT_FALSE(HasError(written.diagnostics));
}

NOLINT_TEST_F(
  PakPlanBuilderTest, LayeredPhysicsPayloadUsesWinningSourceInFullAndPatch)
{
  const auto owner_root = Root() / "owner";
  const auto library_root = Root() / "library";
  const auto material_key = MakeAssetKey(1U);
  const auto shape_key = MakeAssetKey(2U);
  const auto payload_key = MakeAssetKey(3U);
  const auto bytes_of = [](const auto& value) {
    const auto bytes = std::as_bytes(std::span { &value, 1U });
    return std::vector<std::byte>(bytes.begin(), bytes.end());
  };
  auto material = physics::PhysicsMaterialAssetDesc {};
  material.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kPhysicsMaterial);
  material.header.version = physics::kPhysicsMaterialAssetVersion;
  auto shape = physics::CollisionShapeAssetDesc {};
  shape.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kCollisionShape);
  shape.header.version = physics::kCollisionShapeAssetVersion;
  shape.shape_type = physics::ShapeType::kConvexHull;
  shape.material_asset_key = material_key;
  shape.cooked_shape_ref.payload_asset_key = payload_key;
  shape.cooked_shape_ref.payload_type = physics::ShapePayloadType::kConvex;
  auto shape_bytes = bytes_of(shape);
  auto assets = std::array {
    AssetSpec {
      .key = material_key,
      .asset_type = data::AssetType::kPhysicsMaterial,
      .descriptor_relpath = "Material.opmat",
      .virtual_path = "/Game/Material.opmat",
      .descriptor_size = sizeof(material),
      .descriptor_sha = oxygen::base::ComputeSha256(bytes_of(material)),
      .descriptor_payload = bytes_of(material),
      .references = {},
    },
    AssetSpec {
      .key = shape_key,
      .asset_type = data::AssetType::kCollisionShape,
      .descriptor_relpath = "Shape.ocshape",
      .virtual_path = "/Game/Shape.ocshape",
      .descriptor_size = sizeof(shape),
      .descriptor_sha = oxygen::base::ComputeSha256(shape_bytes),
      .descriptor_payload = shape_bytes,
      .references = data::AssetReferences::Create({},
        {
          { .key = material_key,
            .kind = data::KeyReferenceKind::kAsset,
            .expected_type = data::AssetType::kPhysicsMaterial },
          { .key = payload_key,
            .kind = data::KeyReferenceKind::kPhysicsResource,
            .expected_type = data::AssetType::kUnknown },
        })
        .value(),
    },
  };
  auto record = physics::PhysicsResourceDesc {};
  record.resource_asset_key = payload_key;
  record.size_bytes = 4U;
  const auto table = std::array { physics::PhysicsResourceDesc {}, record };
  const auto table_bytes = std::as_bytes(std::span(table));
  auto files = std::array {
    FileSpec { .kind = lc::FileKind::kPhysicsTable,
      .relpath = "physics.table",
      .payload = { table_bytes.begin(), table_bytes.end() } },
    FileSpec { .kind = lc::FileKind::kPhysicsData,
      .relpath = "physics.data",
      .payload = std::vector<std::byte>(4U, std::byte { 1 }) },
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(owner_root, assets, files, 1U));
  files.at(1).payload.assign(4U, std::byte { 2 });
  ASSERT_TRUE(paktest::WriteLooseIndex(library_root, {}, files, 2U));
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources = {
      { .kind = data::CookedSourceKind::kLooseCooked, .path = owner_root },
      { .kind = data::CookedSourceKind::kLooseCooked, .path = library_root },
    },
    .output_pak_path = Root() / "physics.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(3U),
  };
  const auto full = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(full.diagnostics));
  ASSERT_TRUE(full.plan.has_value());
  const auto assert_payload = [&](const pak::PakPlan& plan) {
    size_t payload_count = 0;
    const auto resources = plan.Resources();
    for (size_t index = 0; index < resources.size(); ++index) {
      const auto& resource = CheckedAt(resources, index);
      if (resource.resource_kind != "physics" || resource.size_bytes == 0U) {
        continue;
      }
      ++payload_count;
      const auto& payload = CheckedAt(plan.ResourcePayloadSources(), index);
      EXPECT_EQ(payload.source_path, library_root / "physics.data");
      EXPECT_EQ(payload.size_bytes, 4U);
    }
    EXPECT_EQ(payload_count, 1U);
  };
  assert_payload(*full.plan);
  EXPECT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *full.plan).diagnostics));
  {
    const oxygen::content::PakFile packed(request.output_pak_path);
    EXPECT_EQ(packed.PhysicsTable().Size().get(), 2U);
  }
  oxygen::co::testing::TestEventLoop loop;
  oxygen::co::Run(loop,
    CheckPackagedPhysics(&loop, request.output_pak_path, request.source_key));

  // Only the shape changes. Its payload owner has no emitted descriptor.
  shape.is_sensor = physics::kShapeIsSensorTrue;
  shape_bytes = bytes_of(shape);
  assets.at(1).descriptor_payload = shape_bytes;
  assets.at(1).descriptor_sha = oxygen::base::ComputeSha256(shape_bytes);
  files.at(1).payload.assign(4U, std::byte { 1 });
  ASSERT_TRUE(paktest::WriteLooseIndex(owner_root, assets, files, 1U));
  request.mode = pak::BuildMode::kPatch;
  request.base_catalogs = { full.output_catalog };
  request.source_key = MakeNonZeroSourceKey(4U);
  request.output_manifest_path = Root() / "physics.manifest.json";
  const auto patch = pak::PakPlanBuilder {}.Build(request);
  for (const auto& diagnostic : patch.diagnostics) {
    EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
      << diagnostic.code << ": " << diagnostic.message;
  }
  ASSERT_FALSE(HasError(patch.diagnostics));
  ASSERT_TRUE(patch.plan.has_value());
  ASSERT_EQ(patch.plan->Assets().size(), 1U);
  EXPECT_EQ(CheckedAt(patch.plan->Assets(), 0U).asset_key, shape_key);
  assert_payload(*patch.plan);
  const auto resources = patch.plan->Resources();
  const auto payload
    = std::ranges::find_if(resources, [](const auto& resource) {
        return resource.resource_kind == "physics" && resource.size_bytes == 4U;
      });
  ASSERT_NE(payload, resources.end());
  EXPECT_EQ(std::ranges::count_if(patch.plan->PatchClosure(),
              [&](const auto& dependency) {
                return dependency.asset_key == shape_key
                  && dependency.resource_kind == "physics"
                  && dependency.resource_index == payload->resource_index;
              }),
    1);
}

NOLINT_TEST_F(PakPlanBuilderTest, DeletedSidecarIsAbsentInNewLoadScope)
{
  const auto bytes = oxygen::content::test::MakeEmptySceneDescriptor();
  const auto scene = AssetSpec { .key = MakeAssetKey(10U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene.oscene",
    .descriptor_size = bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(bytes),
    .descriptor_payload = bytes,
    .references = {} };
  auto sidecar = MakePhysicsSidecarSpec(11U, scene);
  sidecar.references = data::AssetReferences::Create({},
    { { .key = scene.key,
      .kind = data::KeyReferenceKind::kLogical,
      .expected_type = data::AssetType::kScene } })
                         .value();
  const auto root = Root() / "source";
  ASSERT_TRUE(
    paktest::WriteLooseIndex(root, std::array { scene, sidecar }, {}, 1U));
  auto request = pak::PakBuildRequest { .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = root } },
    .output_pak_path = Root() / "base.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(20U) };
  const auto base = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(base.plan);
  ASSERT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *base.plan).diagnostics));
  oxygen::content::AssetLoader loader(
    oxygen::engine::internal::EngineTagFactory::Get());
  loader.AddPakFile(request.output_pak_path);
  const auto original = loader.BeginLoadScope();
  const data::SceneAsset scene_asset(scene.key, bytes);
  EXPECT_EQ(loader.FindPhysicsSidecarAssetKeyForScene(scene_asset, original),
    sidecar.key);
  ASSERT_TRUE(paktest::WriteLooseIndex(root, std::array { scene }, {}, 1U));
  request.mode = pak::BuildMode::kPatch;
  request.output_pak_path = Root() / "patch.pak";
  request.output_manifest_path = Root() / "patch.manifest.json";
  request.source_key = MakeNonZeroSourceKey(21U);
  request.base_catalogs = { base.output_catalog };
  const auto patch = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(patch.plan);
  ASSERT_FALSE(
    HasError(pak::PakWriter {}.Write(request, *patch.plan).diagnostics));
  loader.AddPakFile(request.output_pak_path);
  EXPECT_FALSE(loader.FindPhysicsSidecarAssetKeyForScene(
    scene_asset, loader.BeginLoadScope()));
  EXPECT_EQ(loader.FindPhysicsSidecarAssetKeyForScene(scene_asset, original),
    sidecar.key);
}

NOLINT_TEST_F(PakPlanBuilderTest, RejectsInvalidPhysicsScenePairs)
{
  const auto scene_bytes = oxygen::content::test::MakeEmptySceneDescriptor();
  const auto scene = AssetSpec {
    .key = MakeAssetKey(10U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene.oscene",
    .descriptor_size = scene_bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(scene_bytes),
    .descriptor_payload = scene_bytes,
  };
  const auto source = Root() / "invalid-pair";
  const auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "invalid-pair.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(2U),
  };
  const auto expect_rejected
    = [&](const std::span<const AssetSpec> assets) -> void {
    ASSERT_TRUE(paktest::WriteLooseIndex(source, assets, {}, 1U));
    const auto result = pak::PakPlanBuilder {}.Build(request);
    EXPECT_FALSE(result.plan.has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      result.diagnostics, "pak.plan.physics_scene_pair_invalid"));
  };
  auto sidecar = MakePhysicsSidecarSpec(1U, scene);
  sidecar.descriptor_payload.at(offsetof(physics::PhysicsSceneAssetDesc,
    target_scene_content_hash)) ^= std::byte { 1 };
  expect_rejected(std::array { scene, sidecar });

  sidecar = MakePhysicsSidecarSpec(1U, scene);
  auto duplicate = MakePhysicsSidecarSpec(2U, scene);
  duplicate.descriptor_relpath = "Duplicate.opscene";
  expect_rejected(std::array { scene, sidecar, duplicate });

  sidecar.descriptor_payload.at(offsetof(
    physics::PhysicsSceneAssetDesc, target_node_count)) = std::byte { 1 };
  expect_rejected(std::array { scene, sidecar });
}

NOLINT_TEST_F(PakPlanBuilderTest, PatchIncludesBothMembersOfPhysicsScenePair)
{
  const auto scene_bytes = oxygen::content::test::MakeEmptySceneDescriptor();
  const auto scene = AssetSpec {
    .key = MakeAssetKey(10U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Scene.oscene",
    .virtual_path = "/Game/Scene.oscene",
    .descriptor_size = scene_bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(scene_bytes),
    .descriptor_payload = scene_bytes,
  };
  const auto sidecar = MakePhysicsSidecarSpec(1U, scene);
  const auto source = Root() / "pair";
  ASSERT_TRUE(
    paktest::WriteLooseIndex(source, std::array { scene, sidecar }, {}, 1U));
  auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources
    = { { .kind = data::CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "pair.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(2U),
  };
  const auto baseline = pak::PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(baseline.plan.has_value());
  request.mode = pak::BuildMode::kPatch;
  request.source_key = MakeNonZeroSourceKey(3U);
  request.output_manifest_path = Root() / "patch.manifest.json";
  for (const auto changed_key : { scene.key, sidecar.key }) {
    auto base = baseline.output_catalog;
    const auto changed = std::ranges::find(
      base.entries, changed_key, &data::PakCatalogEntry::asset_key);
    ASSERT_NE(changed, base.entries.end());
    changed->descriptor_digest.front() ^= 1U;
    base.catalog_digest = base.ComputeDigest().value();
    request.base_catalogs = { std::move(base) };
    const auto patch = pak::PakPlanBuilder {}.Build(request);
    for (const auto& diagnostic : patch.diagnostics) {
      EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
        << diagnostic.message;
    }
    ASSERT_TRUE(patch.plan.has_value())
      << "Expected patch.plan to contain a value";
    EXPECT_EQ(patch.plan->Assets().size(), 2U);
    for (const auto& action : patch.plan->PatchActions()) {
      EXPECT_EQ(action.action, pak::PakPatchAction::kReplace);
    }
  }
}

} // namespace
