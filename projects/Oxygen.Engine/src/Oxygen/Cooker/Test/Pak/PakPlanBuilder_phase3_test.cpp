//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>

namespace {
namespace data = oxygen::data;
namespace lc = oxygen::data::loose_cooked;
namespace pak = oxygen::content::pak;
namespace core = oxygen::data::pak::core;
namespace physics = oxygen::data::pak::physics;
namespace render = oxygen::data::pak::render;
namespace script = oxygen::data::pak::scripting;

struct AssetSpec final {
  data::AssetKey key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  std::string descriptor_relpath;
  std::string virtual_path;
  uint64_t descriptor_size = 0;
  std::array<uint8_t, lc::kSha256Size> descriptor_sha {};
};

struct FileSpec final {
  lc::FileKind kind = lc::FileKind::kUnknown;
  std::string relpath;
  std::vector<std::byte> payload;
};

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  auto key = data::AssetKey {};
  key.guid.fill(seed);
  return key;
}

auto MakeNonZeroSourceKey(const uint8_t seed) -> data::SourceKey
{
  auto bytes = std::array<uint8_t, 16> {};
  bytes.fill(0U);
  bytes[0] = seed;
  return data::SourceKey { bytes };
}

auto WriteFileBytes(
  const std::filesystem::path& path, std::span<const std::byte> bytes) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto stream = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.good());
  stream.write(reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size())); // NOLINT
  ASSERT_TRUE(stream.good());
}

auto WriteLooseIndex(const std::filesystem::path& root,
  std::span<const AssetSpec> assets, std::span<const FileSpec> files,
  const uint8_t guid_seed) -> void
{
  std::filesystem::create_directories(root);
  for (const auto& file : files) {
    WriteFileBytes(root / file.relpath,
      std::span<const std::byte>(file.payload.data(), file.payload.size()));
  }

  auto strings = std::string {};
  strings.push_back('\0');

  auto asset_entries = std::vector<lc::AssetEntry> {};
  asset_entries.reserve(assets.size());
  for (const auto& asset : assets) {
    const auto descriptor_offset = static_cast<uint32_t>(strings.size());
    strings += asset.descriptor_relpath;
    strings.push_back('\0');

    const auto virtual_path_offset = static_cast<uint32_t>(strings.size());
    strings += asset.virtual_path;
    strings.push_back('\0');

    auto entry = lc::AssetEntry {};
    entry.asset_key = asset.key;
    entry.descriptor_relpath_offset = descriptor_offset;
    entry.virtual_path_offset = virtual_path_offset;
    entry.asset_type = static_cast<uint8_t>(asset.asset_type);
    entry.descriptor_size = asset.descriptor_size;
    std::ranges::copy(asset.descriptor_sha, std::begin(entry.descriptor_sha256));
    asset_entries.push_back(entry);
  }

  auto file_entries = std::vector<lc::FileRecord> {};
  file_entries.reserve(files.size());
  for (const auto& file : files) {
    const auto relpath_offset = static_cast<uint32_t>(strings.size());
    strings += file.relpath;
    strings.push_back('\0');

    auto entry = lc::FileRecord {};
    entry.kind = file.kind;
    entry.relpath_offset = relpath_offset;
    entry.size = static_cast<uint64_t>(file.payload.size());
    file_entries.push_back(entry);
  }

  auto header = lc::IndexHeader {};
  header.version = 1;
  header.flags = static_cast<uint32_t>(lc::kHasVirtualPaths);
  if (!file_entries.empty()) {
    header.flags |= static_cast<uint32_t>(lc::kHasFileRecords);
  }
  for (size_t i = 0; i < std::size(header.guid); ++i) {
    header.guid[i] = static_cast<uint8_t>(guid_seed + static_cast<uint8_t>(i + 1U));
  }
  header.string_table_offset = sizeof(lc::IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset = header.string_table_offset + header.string_table_size;
  header.asset_count = static_cast<uint32_t>(asset_entries.size());
  header.asset_entry_size = sizeof(lc::AssetEntry);
  header.file_records_offset = header.asset_entries_offset
    + (static_cast<uint64_t>(asset_entries.size()) * sizeof(lc::AssetEntry));
  header.file_record_count = static_cast<uint32_t>(file_entries.size());
  header.file_record_size = file_entries.empty() ? 0U : sizeof(lc::FileRecord);

  auto index = std::ofstream(
    root / "container.index.bin", std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(index.good());
  index.write(reinterpret_cast<const char*>(&header), sizeof(header)); // NOLINT
  index.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  for (const auto& asset : asset_entries) {
    index.write(reinterpret_cast<const char*>(&asset), sizeof(asset)); // NOLINT
  }
  for (const auto& file : file_entries) {
    index.write(reinterpret_cast<const char*>(&file), sizeof(file)); // NOLINT
  }
  ASSERT_TRUE(index.good());
}

auto HasError(std::span<const pak::PakDiagnostic> diagnostics) -> bool
{
  return std::ranges::any_of(diagnostics, [](const pak::PakDiagnostic& diagnostic) {
    return diagnostic.severity == pak::PakDiagnosticSeverity::kError;
  });
}

auto HasDiagnosticCode(std::span<const pak::PakDiagnostic> diagnostics,
  const std::string_view code) -> bool
{
  return std::ranges::any_of(diagnostics,
    [code](const pak::PakDiagnostic& diagnostic) {
      return diagnostic.code == code;
    });
}

auto FindTable(
  const pak::PakPlan& plan, const std::string_view name) -> const pak::PakTablePlan*
{
  const auto tables = plan.Tables();
  const auto it = std::ranges::find_if(tables, [name](const auto& table) {
    return table.table_name == name;
  });
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
    + (static_cast<uint64_t>(entries.size()) * sizeof(core::PakBrowseIndexEntry))
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
    EXPECT_EQ(lhs_tables[i].expected_entry_size, rhs_tables[i].expected_entry_size);
    EXPECT_EQ(lhs_tables[i].alignment, rhs_tables[i].alignment);
    EXPECT_EQ(lhs_tables[i].index_zero_required, rhs_tables[i].index_zero_required);
    EXPECT_EQ(lhs_tables[i].index_zero_present, rhs_tables[i].index_zero_present);
    EXPECT_EQ(lhs_tables[i].index_zero_forbidden, rhs_tables[i].index_zero_forbidden);
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
    EXPECT_EQ(lhs_assets[i].reserved_bytes_zeroed, rhs_assets[i].reserved_bytes_zeroed);
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
    EXPECT_EQ(
      lhs_resources[i].reserved_bytes_zeroed, rhs_resources[i].reserved_bytes_zeroed);
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
  EXPECT_EQ(
    lhs.Footer().crc32_field_absolute_offset, rhs.Footer().crc32_field_absolute_offset);

  const auto lhs_ranges = lhs.ScriptParamRanges();
  const auto rhs_ranges = rhs.ScriptParamRanges();
  ASSERT_EQ(lhs_ranges.size(), rhs_ranges.size());
  for (size_t i = 0; i < lhs_ranges.size(); ++i) {
    EXPECT_EQ(lhs_ranges[i].slot_index, rhs_ranges[i].slot_index);
    EXPECT_EQ(lhs_ranges[i].params_array_offset, rhs_ranges[i].params_array_offset);
    EXPECT_EQ(lhs_ranges[i].params_count, rhs_ranges[i].params_count);
  }
}

class PakPlanBuilderPhase3Test : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0 };
    const auto id = ++counter;
    root_ = std::filesystem::temp_directory_path() / "oxygen_pak_plan_phase3"
      / std::to_string(id);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  [[nodiscard]] auto Root() const -> const std::filesystem::path& { return root_; }

private:
  std::filesystem::path root_ {};
};

NOLINT_TEST_F(PakPlanBuilderPhase3Test,
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
    .virtual_path = "/Content/Asset.win",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_a.descriptor_sha[0] = kShaA;

  auto asset_z = AssetSpec {
    .key = MakeAssetKey(kAssetSeed),
    .asset_type = data::AssetType::kScene,
    .descriptor_relpath = "Z.desc",
    .virtual_path = "/Content/Asset.win",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_z.descriptor_sha[0] = kShaZ;

  WriteLooseIndex(source_a, std::span<const AssetSpec>(&asset_a, 1U),
    std::span<const FileSpec> {}, kGuidA);
  WriteLooseIndex(source_z, std::span<const AssetSpec>(&asset_z, 1U),
    std::span<const FileSpec> {}, kGuidZ);

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

  const auto assets = result_a.plan->Assets();
  ASSERT_EQ(assets.size(), 1U);
  EXPECT_EQ(assets[0].asset_type, data::AssetType::kScene);
}

NOLINT_TEST_F(
  PakPlanBuilderPhase3Test, FullModeIncludesAllLiveAssetsAndResources)
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
    .virtual_path = "/Content/Materials/Main.mat",
    .descriptor_size = kDescriptorSizeA,
    .descriptor_sha = {},
  };
  material_asset.descriptor_sha[0] = 0xAAU;

  auto script_asset = AssetSpec {
    .key = MakeAssetKey(0x20U),
    .asset_type = data::AssetType::kScript,
    .descriptor_relpath = "scripts/logic.desc",
    .virtual_path = "/Content/Scripts/Logic.script",
    .descriptor_size = kDescriptorSizeB,
    .descriptor_sha = {},
  };
  script_asset.descriptor_sha[0] = 0xBBU;

  const auto files = std::array<FileSpec, 8> {
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
      .relpath = "Resources/physics.table",
      .payload = std::vector<std::byte>(sizeof(physics::PhysicsResourceDesc)),
    },
    FileSpec {
      .kind = lc::FileKind::kPhysicsData,
      .relpath = "Resources/physics.data",
      .payload = std::vector<std::byte>(kPhysicsPayloadBytes),
    },
  };

  const auto assets = std::array<AssetSpec, 2> { material_asset, script_asset };
  WriteLooseIndex(source, std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed);

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
  EXPECT_TRUE(result.plan->ScriptParamRanges().empty());
}

NOLINT_TEST_F(PakPlanBuilderPhase3Test,
  FullModeExtractsScriptSlotSidecarWhenPresentAndInBounds)
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
    .virtual_path = "/Content/Scene.main",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  scene_asset.descriptor_sha[0] = 0x33U;

  auto slot = script::ScriptSlotRecord {};
  slot.script_asset_key = MakeAssetKey(0x44U);
  slot.params_array_offset = static_cast<uint64_t>(sizeof(script::ScriptParamRecord))
    * kParamsOffsetRecords;
  slot.params_count = kParamsCount;

  auto slot_bytes = std::vector<std::byte>(sizeof(slot));
  std::memcpy(slot_bytes.data(), &slot, sizeof(slot)); // NOLINT

  const auto script_data_bytes = std::vector<std::byte>(
    static_cast<size_t>(kParamRecordCount) * sizeof(script::ScriptParamRecord));
  const auto files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kScriptsTable,
      .relpath = "Resources/scripts.table",
      .payload = slot_bytes,
    },
    FileSpec {
      .kind = lc::FileKind::kScriptsData,
      .relpath = "Resources/scripts.data",
      .payload = script_data_bytes,
    },
  };

  WriteLooseIndex(source, std::span<const AssetSpec>(&scene_asset, 1U),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed);

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
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

  const auto ranges = result.plan->ScriptParamRanges();
  ASSERT_EQ(ranges.size(), 1U);
  EXPECT_EQ(ranges[0].slot_index, 0U);
  EXPECT_EQ(ranges[0].params_array_offset, kParamsOffsetRecords);
  EXPECT_EQ(ranges[0].params_count, kParamsCount);
}

NOLINT_TEST_F(PakPlanBuilderPhase3Test, ScriptSlotOutOfBoundsIsRejected)
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
    .virtual_path = "/Content/Scene.invalid",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  scene_asset.descriptor_sha[0] = 0x35U;

  auto slot = script::ScriptSlotRecord {};
  slot.script_asset_key = MakeAssetKey(0x46U);
  slot.params_array_offset = static_cast<uint64_t>(sizeof(script::ScriptParamRecord))
    * kOutOfBoundsOffsetRecords;
  slot.params_count = kOutOfBoundsCount;

  auto slot_bytes = std::vector<std::byte>(sizeof(slot));
  std::memcpy(slot_bytes.data(), &slot, sizeof(slot)); // NOLINT

  const auto script_data_bytes = std::vector<std::byte>(
    static_cast<size_t>(kParamRecordCount) * sizeof(script::ScriptParamRecord));
  const auto files = std::array<FileSpec, 2> {
    FileSpec {
      .kind = lc::FileKind::kScriptsTable,
      .relpath = "Resources/scripts.table",
      .payload = slot_bytes,
    },
    FileSpec {
      .kind = lc::FileKind::kScriptsData,
      .relpath = "Resources/scripts.data",
      .payload = script_data_bytes,
    },
  };

  WriteLooseIndex(source, std::span<const AssetSpec>(&scene_asset, 1U),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed);

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
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

NOLINT_TEST_F(PakPlanBuilderPhase3Test, IndexZeroPolicyAppliedForResourceTables)
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
    .virtual_path = "/Content/Script.main",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  script_asset.descriptor_sha[0] = 0x55U;

  const auto files = std::array<FileSpec, 4> {
    FileSpec {
      .kind = lc::FileKind::kTexturesTable,
      .relpath = "Resources/textures.table",
      .payload = std::vector<std::byte>(sizeof(render::TextureResourceDesc)),
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

  WriteLooseIndex(source, std::span<const AssetSpec>(&script_asset, 1U),
    std::span<const FileSpec>(files.data(), files.size()), kGuidSeed);

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
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

NOLINT_TEST_F(PakPlanBuilderPhase3Test,
  BrowsePayloadPlanSizeMatchesSerializedShapeInvariant)
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
    .virtual_path = "/Content/Browse/B.asset",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_a.descriptor_sha[0] = 0x61U;

  auto asset_b = AssetSpec {
    .key = MakeAssetKey(0x62U),
    .asset_type = data::AssetType::kScript,
    .descriptor_relpath = "b.desc",
    .virtual_path = "/Content/Browse/A.asset",
    .descriptor_size = kDescriptorSize,
    .descriptor_sha = {},
  };
  asset_b.descriptor_sha[0] = 0x62U;

  const auto assets = std::array<AssetSpec, 2> { asset_a, asset_b };
  WriteLooseIndex(source, std::span<const AssetSpec>(assets.data(), assets.size()),
    std::span<const FileSpec> {}, kGuidSeed);

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
  EXPECT_FALSE(
    HasDiagnosticCode(result.diagnostics, "pak.plan.stage.layout.browse_size_mismatch"));
}

} // namespace
