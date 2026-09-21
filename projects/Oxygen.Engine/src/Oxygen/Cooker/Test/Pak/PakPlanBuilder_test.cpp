//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakTestSupport.h"

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace lc = oxygen::data::loose_cooked;
namespace core = oxygen::data::pak::core;
namespace physics = oxygen::data::pak::physics;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;

using AssetSpec = paktest::AssetSpec;
using FileSpec = paktest::FileSpec;

auto MakeNonZeroSourceKey(const uint8_t seed) -> data::SourceKey
{
  return paktest::MakeSourceKey(seed);
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  return paktest::MakeAssetKey(seed);
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

auto FindTable(const pak::PakPlan& plan, const std::string_view name)
  -> const pak::PakTablePlan*
{
  const auto tables = plan.Tables();
  const auto it = std::ranges::find_if(
    tables, [name](const auto& table) { return table.table_name == name; });
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

auto ExpectPlansEquivalent(const pak::PakPlan& lhs, const pak::PakPlan& rhs)
  -> void
{
  EXPECT_EQ(lhs.Header().offset, rhs.Header().offset);
  EXPECT_EQ(lhs.Header().size_bytes, rhs.Header().size_bytes);
  EXPECT_EQ(lhs.Header().content_version, rhs.Header().content_version);
  EXPECT_EQ(lhs.Header().source_key.get(), rhs.Header().source_key.get());
  EXPECT_EQ(lhs.PlannedFileSize(), rhs.PlannedFileSize());
  EXPECT_EQ(lhs.ScriptParamRecordCount(), rhs.ScriptParamRecordCount());

  const auto lhs_regions = lhs.Regions();
  const auto rhs_regions = rhs.Regions();
  ASSERT_EQ(lhs_regions.size(), rhs_regions.size());
  for (size_t i = 0; i < lhs_regions.size(); ++i) {
    EXPECT_EQ(lhs_regions[i].region_name, rhs_regions[i].region_name);
    EXPECT_EQ(lhs_regions[i].offset, rhs_regions[i].offset);
    EXPECT_EQ(lhs_regions[i].size_bytes, rhs_regions[i].size_bytes);
    EXPECT_EQ(lhs_regions[i].alignment, rhs_regions[i].alignment);
  }

  const auto lhs_tables = lhs.Tables();
  const auto rhs_tables = rhs.Tables();
  ASSERT_EQ(lhs_tables.size(), rhs_tables.size());
  for (size_t i = 0; i < lhs_tables.size(); ++i) {
    EXPECT_EQ(lhs_tables[i].table_name, rhs_tables[i].table_name);
    EXPECT_EQ(lhs_tables[i].offset, rhs_tables[i].offset);
    EXPECT_EQ(lhs_tables[i].size_bytes, rhs_tables[i].size_bytes);
    EXPECT_EQ(lhs_tables[i].count, rhs_tables[i].count);
    EXPECT_EQ(lhs_tables[i].entry_size, rhs_tables[i].entry_size);
    EXPECT_EQ(
      lhs_tables[i].expected_entry_size, rhs_tables[i].expected_entry_size);
    EXPECT_EQ(lhs_tables[i].alignment, rhs_tables[i].alignment);
    EXPECT_EQ(
      lhs_tables[i].index_zero_required, rhs_tables[i].index_zero_required);
    EXPECT_EQ(
      lhs_tables[i].index_zero_present, rhs_tables[i].index_zero_present);
    EXPECT_EQ(
      lhs_tables[i].index_zero_forbidden, rhs_tables[i].index_zero_forbidden);
  }

  const auto lhs_assets = lhs.Assets();
  const auto rhs_assets = rhs.Assets();
  ASSERT_EQ(lhs_assets.size(), rhs_assets.size());
  for (size_t i = 0; i < lhs_assets.size(); ++i) {
    EXPECT_EQ(lhs_assets[i].asset_key, rhs_assets[i].asset_key);
    EXPECT_EQ(lhs_assets[i].asset_type, rhs_assets[i].asset_type);
    EXPECT_EQ(lhs_assets[i].offset, rhs_assets[i].offset);
    EXPECT_EQ(lhs_assets[i].size_bytes, rhs_assets[i].size_bytes);
    EXPECT_EQ(lhs_assets[i].alignment, rhs_assets[i].alignment);
    EXPECT_EQ(
      lhs_assets[i].reserved_bytes_zeroed, rhs_assets[i].reserved_bytes_zeroed);
  }

  const auto lhs_resources = lhs.Resources();
  const auto rhs_resources = rhs.Resources();
  ASSERT_EQ(lhs_resources.size(), rhs_resources.size());
  for (size_t i = 0; i < lhs_resources.size(); ++i) {
    EXPECT_EQ(lhs_resources[i].resource_kind, rhs_resources[i].resource_kind);
    EXPECT_EQ(lhs_resources[i].resource_index, rhs_resources[i].resource_index);
    EXPECT_EQ(lhs_resources[i].region_name, rhs_resources[i].region_name);
    EXPECT_EQ(lhs_resources[i].offset, rhs_resources[i].offset);
    EXPECT_EQ(lhs_resources[i].size_bytes, rhs_resources[i].size_bytes);
    EXPECT_EQ(lhs_resources[i].alignment, rhs_resources[i].alignment);
    EXPECT_EQ(lhs_resources[i].reserved_bytes_zeroed,
      rhs_resources[i].reserved_bytes_zeroed);
  }

  EXPECT_EQ(lhs.Directory().offset, rhs.Directory().offset);
  EXPECT_EQ(lhs.Directory().size_bytes, rhs.Directory().size_bytes);
  ASSERT_EQ(lhs.Directory().entries.size(), rhs.Directory().entries.size());
  for (size_t i = 0; i < lhs.Directory().entries.size(); ++i) {
    const auto& lhs_entry = lhs.Directory().entries[i];
    const auto& rhs_entry = rhs.Directory().entries[i];
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
    const auto& lhs_entry = lhs.BrowseIndex().entries[i];
    const auto& rhs_entry = rhs.BrowseIndex().entries[i];
    EXPECT_EQ(lhs_entry.asset_key, rhs_entry.asset_key);
    EXPECT_EQ(lhs_entry.virtual_path, rhs_entry.virtual_path);
  }

  EXPECT_EQ(lhs.Footer().offset, rhs.Footer().offset);
  EXPECT_EQ(lhs.Footer().size_bytes, rhs.Footer().size_bytes);
  EXPECT_EQ(lhs.Footer().crc32_field_absolute_offset,
    rhs.Footer().crc32_field_absolute_offset);

  const auto lhs_slots = lhs.ScriptSlots();
  const auto rhs_slots = rhs.ScriptSlots();
  ASSERT_EQ(lhs_slots.size(), rhs_slots.size());
  for (size_t i = 0; i < lhs_slots.size(); ++i) {
    EXPECT_EQ(lhs_slots[i].slot_index, rhs_slots[i].slot_index);
    EXPECT_EQ(lhs_slots[i].script_asset_key, rhs_slots[i].script_asset_key);
    EXPECT_EQ(lhs_slots[i].params_array_index, rhs_slots[i].params_array_index);
    EXPECT_EQ(lhs_slots[i].params_count, rhs_slots[i].params_count);
    EXPECT_EQ(lhs_slots[i].execution_order, rhs_slots[i].execution_order);
    EXPECT_EQ(lhs_slots[i].flags, rhs_slots[i].flags);
  }
}

auto ExpectCatalogsEquivalent(
  const data::PakCatalog& lhs, const data::PakCatalog& rhs) -> void
{
  EXPECT_EQ(lhs.source_key, rhs.source_key);
  EXPECT_EQ(lhs.content_version, rhs.content_version);
  EXPECT_EQ(lhs.catalog_digest, rhs.catalog_digest);
  ASSERT_EQ(lhs.entries.size(), rhs.entries.size());
  for (size_t i = 0; i < lhs.entries.size(); ++i) {
    EXPECT_EQ(lhs.entries[i].asset_key, rhs.entries[i].asset_key);
    EXPECT_EQ(lhs.entries[i].asset_type, rhs.entries[i].asset_type);
    EXPECT_EQ(
      lhs.entries[i].descriptor_digest, rhs.entries[i].descriptor_digest);
    EXPECT_EQ(lhs.entries[i].transitive_resource_digest,
      rhs.entries[i].transitive_resource_digest);
  }
}

class PakPlanBuilderTest : public paktest::TempDirFixture { };

NOLINT_TEST_F(PakPlanBuilderTest,
  DeterministicPlanningBuildsEquivalentPlanForReorderedSources)
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
  asset_a.descriptor_sha[0] = kShaA;

  auto asset_z = AssetSpec {
    .key = MakeAssetKey(kAssetSeed),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Z.desc",
    .virtual_path = "/Game/Asset.win",
    .descriptor_size = paktest::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = {},
    .descriptor_payload = paktest::MakeEmptySceneDescriptor(),
  };
  asset_z.descriptor_sha[0] = kShaZ;

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
    .patch_compat = {},
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
    .patch_compat = {},
    .options = options,
  };

  const auto builder = PakPlanBuilder {};
  const auto result_a = builder.Build(request_a);
  const auto result_b = builder.Build(request_b);

  ASSERT_FALSE(HasError(result_a.diagnostics));
  ASSERT_FALSE(HasError(result_b.diagnostics));
  ASSERT_TRUE(result_a.plan.has_value());
  ASSERT_TRUE(result_b.plan.has_value());

  ExpectPlansEquivalent(*result_a.plan, *result_b.plan);
  ExpectCatalogsEquivalent(result_a.output_catalog, result_b.output_catalog);

  const auto assets = result_a.plan->Assets();
  ASSERT_EQ(assets.size(), 1U);
  EXPECT_EQ(assets[0].asset_type, data::AssetType::kScene);
  ASSERT_EQ(result_a.output_catalog.entries.size(), 1U);
  EXPECT_EQ(
    result_a.output_catalog.entries[0].asset_key, MakeAssetKey(kAssetSeed));
  EXPECT_EQ(
    result_a.output_catalog.entries[0].asset_type, data::AssetType::kScene);
}

NOLINT_TEST_F(PakPlanBuilderTest, FullModeIncludesAllLiveAssetsAndResources)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 31U };
  constexpr auto kDescriptorSizeA = uint64_t { 64U };
  constexpr auto kDescriptorSizeB = uint64_t { 48U };
  constexpr auto kTexturePayloadBytes = size_t { 29U };
  constexpr auto kBufferPayloadBytes = size_t { 17U };
  constexpr auto kScriptPayloadBytes = size_t { 13U };
  constexpr auto kPhysicsPayloadBytes = size_t { 11U };

  const auto source = Root() / "full_include";

  auto material_asset = AssetSpec {
    .key = MakeAssetKey(0x10U),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "materials/mat.desc",
    .virtual_path = "/Game/Materials/Main.mat",
    .descriptor_size = kDescriptorSizeA,
    .descriptor_sha = {},
  };
  material_asset.descriptor_sha[0] = 0xAAU;

  auto script_asset = AssetSpec {
    .key = MakeAssetKey(0x20U),
    .asset_type = data::AssetType::kScript,
    .descriptor_relpath = "scripts/logic.desc",
    .virtual_path = "/Game/Scripts/Logic.script",
    .descriptor_size = kDescriptorSizeB,
    .descriptor_sha = {},
  };
  script_asset.descriptor_sha[0] = 0xBBU;

  const auto files = std::array<FileSpec, 8> {
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
    FileSpec {
      .kind = lc::FileKind::kScriptsTable,
      .relpath = "Resources/scripts.table",
      .payload = std::vector<std::byte>(sizeof(script::ScriptResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kScriptsData,
      .relpath = "Resources/scripts.data",
      .payload = std::vector<std::byte>(kScriptPayloadBytes),
    },
    FileSpec {
      .kind = lc::FileKind::kPhysicsTable,
      .relpath = "Physics/Resources/physics.table",
      .payload = std::vector<std::byte>(sizeof(physics::PhysicsResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kPhysicsData,
      .relpath = "Physics/Resources/physics.data",
      .payload = std::vector<std::byte>(kPhysicsPayloadBytes),
    },
  };

  const auto assets = std::array<AssetSpec, 2> { material_asset, script_asset };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "full_include.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(0xA1U),
    .base_catalogs = {},
    .patch_compat = {},
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
  ASSERT_TRUE(result.plan.has_value());

  EXPECT_EQ(result.plan->Assets().size(), 2U);
  EXPECT_EQ(result.plan->Resources().size(), 4U);

  const auto* texture_table = FindTable(*result.plan, "texture_table");
  const auto* buffer_table = FindTable(*result.plan, "buffer_table");
  const auto* script_resource_table
    = FindTable(*result.plan, "script_resource_table");
  const auto* script_slot_table = FindTable(*result.plan, "script_slot_table");
  const auto* physics_table = FindTable(*result.plan, "physics_resource_table");
  ASSERT_NE(texture_table, nullptr);
  ASSERT_NE(buffer_table, nullptr);
  ASSERT_NE(script_resource_table, nullptr);
  ASSERT_NE(script_slot_table, nullptr);
  ASSERT_NE(physics_table, nullptr);

  EXPECT_EQ(texture_table->count, 1U);
  EXPECT_EQ(buffer_table->count, 1U);
  EXPECT_EQ(script_resource_table->count, 1U);
  EXPECT_EQ(script_slot_table->count, 0U);
  EXPECT_EQ(physics_table->count, 1U);
  EXPECT_EQ(result.plan->ScriptParamRecordCount(), 0U);
  EXPECT_TRUE(result.plan->ScriptSlots().empty());
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
    .descriptor_size = paktest::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = paktest::MakeDigest(0x53U),
    .descriptor_payload = paktest::MakeEmptySceneDescriptor(),
  };
  const auto assets
    = std::array<AssetSpec, 3> { action_asset, context_asset, scene_asset };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec> {}, kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "full_include_input_assets.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(0x91U),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value());

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

NOLINT_TEST_F(
  PakPlanBuilderTest, FullModeExtractsScriptSlotSidecarWhenPresentAndInBounds)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 27U };
  constexpr auto kDescriptorSize = uint64_t { 16U };
  constexpr auto kSourceKeySeed = uint8_t { 0x09U };
  constexpr auto kParamRecordCount = uint32_t { 3U };
  constexpr auto kParamsOffsetRecords = uint64_t { 1U };
  constexpr auto kParamsCount = uint32_t { 2U };

  const auto source = Root() / "scene_with_scripts";
  auto scene_asset = AssetSpec {
    .key = MakeAssetKey(0x33U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "scene.desc",
    .virtual_path = "/Game/Scene.main",
    .descriptor_size = paktest::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = {},
    .descriptor_payload = paktest::MakeEmptySceneDescriptor(),
  };
  scene_asset.descriptor_sha[0] = 0x33U;

  auto slot = script::ScriptSlotRecord {};
  slot.script_asset_key = MakeAssetKey(0x44U);
  slot.params_array_offset
    = static_cast<uint64_t>(sizeof(script::ScriptParamRecord))
    * kParamsOffsetRecords;
  slot.params_count = kParamsCount;

  auto slot_bytes = std::vector<std::byte>(sizeof(slot));
  std::memcpy(slot_bytes.data(), &slot, sizeof(slot)); // NOLINT

  const auto script_data_bytes = std::vector<std::byte>(
    static_cast<size_t>(kParamRecordCount) * sizeof(script::ScriptParamRecord));
  const auto files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kScriptBindingsTable,
      .relpath = "Resources/script-bindings.table",
      .payload = slot_bytes,
    },
    FileSpec {
      .kind = lc::FileKind::kScriptBindingsData,
      .relpath = "Resources/script-bindings.data",
      .payload = script_data_bytes,
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(&scene_asset, 1U),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "script_slot_ok.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(kSourceKeySeed),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value());

  const auto* script_slot_table = FindTable(*result.plan, "script_slot_table");
  const auto* script_resource_table
    = FindTable(*result.plan, "script_resource_table");
  ASSERT_NE(script_slot_table, nullptr);
  ASSERT_NE(script_resource_table, nullptr);

  EXPECT_EQ(script_slot_table->count, 1U);
  EXPECT_EQ(script_resource_table->count, 0U);
  EXPECT_EQ(result.plan->ScriptParamRecordCount(), kParamRecordCount);

  const auto slots = result.plan->ScriptSlots();
  ASSERT_EQ(slots.size(), 1U);
  EXPECT_EQ(slots[0].slot_index, 0U);
  EXPECT_EQ(slots[0].script_asset_key, MakeAssetKey(0x44U));
  EXPECT_EQ(slots[0].params_array_index, kParamsOffsetRecords);
  EXPECT_EQ(slots[0].params_count, kParamsCount);
}

NOLINT_TEST_F(PakPlanBuilderTest, ScriptSlotOutOfBoundsIsRejected)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;

  constexpr auto kGuidSeed = uint8_t { 37U };
  constexpr auto kDescriptorSize = uint64_t { 16U };
  constexpr auto kParamRecordCount = uint32_t { 3U };
  constexpr auto kOutOfBoundsOffsetRecords = uint64_t { 2U };
  constexpr auto kOutOfBoundsCount = uint32_t { 2U };

  const auto source = Root() / "scene_with_invalid_slots";
  auto scene_asset = AssetSpec {
    .key = MakeAssetKey(0x35U),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "scene.desc",
    .virtual_path = "/Game/Scene.invalid",
    .descriptor_size = paktest::MakeEmptySceneDescriptor().size(),
    .descriptor_sha = {},
    .descriptor_payload = paktest::MakeEmptySceneDescriptor(),
  };
  scene_asset.descriptor_sha[0] = 0x35U;

  auto slot = script::ScriptSlotRecord {};
  slot.script_asset_key = MakeAssetKey(0x46U);
  slot.params_array_offset
    = static_cast<uint64_t>(sizeof(script::ScriptParamRecord))
    * kOutOfBoundsOffsetRecords;
  slot.params_count = kOutOfBoundsCount;

  auto slot_bytes = std::vector<std::byte>(sizeof(slot));
  std::memcpy(slot_bytes.data(), &slot, sizeof(slot)); // NOLINT

  const auto script_data_bytes = std::vector<std::byte>(
    static_cast<size_t>(kParamRecordCount) * sizeof(script::ScriptParamRecord));
  const auto files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kScriptBindingsTable,
      .relpath = "Resources/script-bindings.table",
      .payload = slot_bytes,
    },
    FileSpec {
      .kind = lc::FileKind::kScriptBindingsData,
      .relpath = "Resources/script-bindings.data",
      .payload = script_data_bytes,
    },
  };

  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const AssetSpec>(&scene_asset, 1U),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "script_slot_invalid.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(0x77U),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_TRUE(HasError(result.diagnostics));
  EXPECT_FALSE(result.plan.has_value());
  EXPECT_TRUE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.script_params_range_out_of_bounds"));
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
  script_asset.descriptor_sha[0] = 0x55U;

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
      .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Root() / "index0.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(11U),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value());

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
  asset_a.descriptor_sha[0] = 0x61U;

  auto asset_b = AssetSpec {
    .key = MakeAssetKey(0x62U),
    .asset_type = data::AssetType::kScript,
    .descriptor_relpath = "b.desc",
    .virtual_path = "/Game/Browse/A.asset",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_b.descriptor_sha[0] = 0x62U;

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
    .patch_compat = {},
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
  ASSERT_TRUE(result.plan.has_value());

  const auto& browse = result.plan->BrowseIndex();
  ASSERT_TRUE(browse.enabled);
  ASSERT_EQ(browse.entries.size(), 2U);
  EXPECT_LT(browse.entries[0].virtual_path, browse.entries[1].virtual_path);

  const auto expected_payload_size
    = ComputeExpectedBrowsePayloadSize(std::span<const pak::PakBrowseEntryPlan>(
      browse.entries.data(), browse.entries.size()));
  EXPECT_EQ(browse.size_bytes, expected_payload_size);
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.stage.layout.browse_store_failed"));
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.stage.layout.browse_size_mismatch"));
}

NOLINT_TEST_F(PakPlanBuilderTest, SceneMaskIndicesRemapAcrossCookedSources)
{
  namespace world = oxygen::data::pak::world;
  namespace serio = oxygen::serio;
  auto post = world::PostProcessVolumeEnvironmentRecord {};
  post.auto_exposure_metering_mask = core::ResourceIndexT { 1U };
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  descriptor.header.version = world::kSceneAssetVersion;
  const auto environment = world::SceneEnvironmentBlockHeader {
    .byte_size = sizeof(world::SceneEnvironmentBlockHeader) + sizeof(post),
    .systems_count = 1U,
  };
  serio::MemoryStream scene_stream;
  serio::Writer scene_writer(scene_stream);
  const auto scene_packed = scene_writer.ScopedAlignment(1);
  ASSERT_TRUE(scene_writer.Write(descriptor));
  ASSERT_TRUE(scene_writer.Write(environment));
  ASSERT_TRUE(serio::Store(scene_writer, post));
  const auto scene_bytes = scene_stream.Data();

  serio::MemoryStream texture_stream;
  serio::Writer texture_writer(texture_stream);
  const auto texture_packed = texture_writer.ScopedAlignment(1);
  ASSERT_TRUE(texture_writer.Write(core::TextureResourceDesc {}));
  ASSERT_TRUE(texture_writer.Write(core::TextureResourceDesc {
    .data_offset = 0U,
    .size_bytes = 4U,
    .texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D),
    .compression_type = 0U,
    .width = 1U,
    .height = 1U,
    .depth = 1U,
    .array_layers = 1U,
    .mip_levels = 1U,
    .format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm),
    .alignment = 256U,
  }));
  const auto texture_table = texture_stream.Data();
  auto sources = std::vector<data::CookedSource> {};
  for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
    const auto root = Root() / std::to_string(seed);
    const auto asset = AssetSpec {
      .key = MakeAssetKey(seed),
      .asset_type = data::AssetType::kScene,
      .descriptor_relpath = "Scene.oscene",
      .virtual_path = "/Game/Scene" + std::to_string(seed) + ".oscene",
      .descriptor_size = scene_bytes.size(),
      .descriptor_sha = paktest::MakeDigest(seed),
      .descriptor_payload = { scene_bytes.begin(), scene_bytes.end() },
    };
    const auto files = std::array {
      FileSpec { .kind = lc::FileKind::kTexturesTable,
        .relpath = "textures.table",
        .payload = { texture_table.begin(), texture_table.end() } },
      FileSpec { .kind = lc::FileKind::kTexturesData,
        .relpath = "textures.data",
        .payload = std::vector<std::byte>(4U, static_cast<std::byte>(seed)) },
    };
    auto material = render::MaterialAssetDesc {};
    material.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kMaterial);
    material.header.version = render::kMaterialAssetVersion;
    material.base_color_texture = core::ResourceIndexT { 1U };
    serio::MemoryStream material_stream;
    serio::Writer material_writer(material_stream);
    const auto material_packed = material_writer.ScopedAlignment(1);
    ASSERT_TRUE(
      material_writer.WriteBlob(std::as_bytes(std::span { &material, 1U })));
    const auto material_bytes = material_stream.Data();
    const auto material_asset = AssetSpec {
      .key = MakeAssetKey(static_cast<uint8_t>(seed + 10U)),
      .asset_type = data::AssetType::kMaterial,
      .descriptor_relpath = "Material.omat",
      .virtual_path = "/Game/Material" + std::to_string(seed) + ".omat",
      .descriptor_size = material_bytes.size(),
      .descriptor_sha = paktest::MakeDigest(seed),
      .descriptor_payload = { material_bytes.begin(), material_bytes.end() },
    };
    const auto source_assets = std::array { asset, material_asset };
    ASSERT_TRUE(paktest::WriteLooseIndex(root, source_assets, files, seed));
    sources.push_back(
      { .kind = data::CookedSourceKind::kLooseCooked, .path = root });
  }
  const auto request = pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources = sources,
    .output_pak_path = Root() / "masks.pak",
    .content_version = 1U,
    .source_key = MakeNonZeroSourceKey(3U),
  };
  const auto result = pak::PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(result.diagnostics));
  ASSERT_TRUE(result.plan.has_value());
  const auto assets = result.plan->Assets();
  const auto payloads = result.plan->AssetPayloadSources();
  ASSERT_EQ(assets.size(), 4U);
  bool found_second_scene = false;
  for (size_t index = 0; index < assets.size(); ++index) {
    if (assets[index].asset_key != MakeAssetKey(2U)) {
      continue;
    }
    ASSERT_FALSE(payloads[index].inline_bytes.empty());
    const auto scene
      = data::SceneAsset(assets[index].asset_key, payloads[index].inline_bytes);
    const auto remapped = scene.TryGetPostProcessVolumeEnvironment();
    ASSERT_TRUE(remapped.has_value());
    // Each source contributes its null record and its real texture.
    EXPECT_EQ(remapped->auto_exposure_metering_mask.get(), 3U);
    found_second_scene = true;
    break;
  }
  ASSERT_TRUE(found_second_scene);
  const auto written = pak::PakWriter {}.Write(request, *result.plan);
  ASSERT_FALSE(HasError(written.diagnostics));

  for (const auto mode : { pak::BuildMode::kFull, pak::BuildMode::kPatch }) {
    auto repack = request;
    repack.mode = mode;
    repack.sources = { { .kind = data::CookedSourceKind::kPak,
      .path = request.output_pak_path } };
    repack.output_pak_path
      = Root() / (mode == pak::BuildMode::kFull ? "repacked.pak" : "patch.pak");
    repack.source_key = MakeNonZeroSourceKey(4U);
    if (mode == pak::BuildMode::kPatch) {
      repack.output_manifest_path = Root() / "patch.manifest.json";
      repack.base_catalogs.push_back(data::PakCatalog {
        .source_key = MakeNonZeroSourceKey(5U),
        .content_version = 1U,
        .catalog_digest = paktest::MakeDigest(5U),
        .entries = {},
      });
    }
    const auto planned = pak::PakPlanBuilder {}.Build(repack);
    for (const auto& diagnostic : planned.diagnostics) {
      EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
        << diagnostic.message;
    }
    ASSERT_TRUE(planned.plan.has_value());
    ASSERT_FALSE(
      HasError(pak::PakWriter {}.Write(repack, *planned.plan).diagnostics));
    auto archive = oxygen::content::PakFile(repack.output_pak_path);
    archive.ValidateCrc32Integrity();
    for (const auto seed : { uint8_t { 1U }, uint8_t { 2U } }) {
      const auto entry = archive.FindEntry(MakeAssetKey(seed));
      ASSERT_TRUE(entry.has_value());
      auto descriptor_reader = archive.CreateReader(*entry);
      const auto bytes = descriptor_reader.ReadBlob(entry->desc_size);
      ASSERT_TRUE(bytes.has_value());
      const auto scene = data::SceneAsset(entry->asset_key, *bytes);
      const auto post_process = scene.TryGetPostProcessVolumeEnvironment();
      ASSERT_TRUE(post_process.has_value());
      const auto expected_index = seed == 1U ? 1U
        : mode == pak::BuildMode::kFull      ? 3U
                                             : 2U;
      EXPECT_EQ(
        post_process->auto_exposure_metering_mask.get(), expected_index);
      const auto texture_offset = archive.TexturesTable().GetResourceOffset(
        post_process->auto_exposure_metering_mask);
      ASSERT_TRUE(texture_offset.has_value());
      serio::FileStream<> stream(repack.output_pak_path, std::ios::in);
      serio::Reader reader(stream);
      ASSERT_TRUE(reader.Seek(*texture_offset));
      auto texture = core::TextureResourceDesc {};
      ASSERT_TRUE(serio::Load(reader, texture));
      ASSERT_TRUE(reader.Seek(texture.data_offset));
      const auto payload = reader.ReadBlob(texture.size_bytes);
      ASSERT_TRUE(payload.has_value());
      EXPECT_EQ(
        *payload, std::vector<std::byte>(4U, static_cast<std::byte>(seed)));
      const auto material_entry
        = archive.FindEntry(MakeAssetKey(static_cast<uint8_t>(seed + 10U)));
      ASSERT_TRUE(material_entry.has_value());
      auto material_reader = archive.CreateReader(*material_entry);
      auto material = render::MaterialAssetDesc {};
      const auto material_payload = material_reader.ReadBlob(sizeof(material));
      ASSERT_TRUE(material_payload.has_value());
      std::memcpy(&material, material_payload->data(), sizeof(material));
      EXPECT_EQ(material.base_color_texture.get(), expected_index);
    }
  }
}

} // namespace
