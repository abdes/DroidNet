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
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

#include "PakTestSupport.h"

namespace {
namespace content = oxygen::content;
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace render = oxygen::data::pak::render;
namespace serio = oxygen::serio;

constexpr auto kSourceKeySeedValue = uint8_t { 0x7DU };
constexpr auto kHeaderSize = uint32_t { sizeof(core::PakHeader) };
constexpr auto kFooterSize = uint32_t { sizeof(core::PakFooter) };
constexpr auto kTextureDataSize = uint64_t { 32U };
constexpr auto kMismatchedOffset = uint64_t { 1U };
constexpr auto kAudioEntrySize
  = uint32_t { sizeof(oxygen::data::pak::audio::AudioResourceDesc) };
constexpr auto kScriptResourceEntrySize
  = uint32_t { sizeof(oxygen::data::pak::scripting::ScriptResourceDesc) };
constexpr auto kScriptSlotEntrySize
  = uint32_t { sizeof(oxygen::data::pak::scripting::ScriptSlotRecord) };
constexpr auto kPhysicsEntrySize
  = uint32_t { sizeof(oxygen::data::pak::physics::PhysicsResourceDesc) };

auto MakeSourceKey() -> data::SourceKey
{
  return paktest::MakeSourceKey(kSourceKeySeedValue);
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

auto ReadFooter(const std::filesystem::path& path) -> core::PakFooter
{
  auto stream = serio::FileStream<>(path, std::ios::binary | std::ios::in);
  auto size_result = stream.Size();
  EXPECT_TRUE(size_result.has_value());
  EXPECT_GE(size_result.value(), sizeof(core::PakFooter));
  EXPECT_TRUE(stream.Seek(size_result.value() - sizeof(core::PakFooter)));

  auto reader = serio::Reader(stream);
  auto align_guard = reader.ScopedAlignment(1);
  (void)align_guard;
  auto footer_result = reader.Read<core::PakFooter>();
  EXPECT_TRUE(footer_result.has_value());
  return footer_result.value_or(core::PakFooter {});
}

auto ReadAllBytes(const std::filesystem::path& path) -> std::vector<std::byte>
{
  auto stream = std::ifstream(path, std::ios::binary);
  EXPECT_TRUE(stream.good());
  const auto bytes = std::vector<char>(
    std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  EXPECT_TRUE(stream.good() || stream.eof());

  auto out = std::vector<std::byte> {};
  out.reserve(bytes.size());
  for (const auto byte : bytes) {
    out.push_back(static_cast<std::byte>(byte));
  }
  return out;
}

auto IsAllZero(std::span<const std::byte> bytes) -> bool
{
  return std::ranges::all_of(
    bytes, [](const std::byte byte) { return byte == std::byte { 0 }; });
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  return paktest::MakeAssetKey(seed);
}

auto WriteAllBytesToFile(const std::filesystem::path& path,
  const std::span<const std::byte> bytes) -> bool
{
  auto stream = std::ofstream(path, std::ios::binary | std::ios::trunc);
  if (!stream.good()) {
    return false;
  }

  if (!bytes.empty()) {
    stream.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  }
  stream.flush();
  return stream.good();
}

auto MakePatternBytes(const uint8_t start_value, const size_t byte_count)
  -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte> {};
  bytes.reserve(byte_count);

  for (size_t i = 0; i < byte_count; ++i) {
    const auto value
      = static_cast<uint8_t>(start_value + static_cast<uint8_t>(i));
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

struct RegionPlanSpec final {
  std::string region_name;
  uint64_t offset = 0U;
  uint64_t size_bytes = 0U;
  uint32_t alignment = 1U;
};

auto MakeRegionPlan(RegionPlanSpec spec) -> pak::PakRegionPlan
{
  return pak::PakRegionPlan {
    .region_name = std::move(spec.region_name),
    .offset = spec.offset,
    .size_bytes = spec.size_bytes,
    .alignment = spec.alignment,
  };
}

struct TablePlanSpec final {
  std::string table_name;
  uint64_t offset = 0U;
  uint64_t size_bytes = 0U;
  uint32_t count = 0U;
  uint32_t entry_size = 0U;
};

auto MakeTablePlan(TablePlanSpec spec) -> pak::PakTablePlan
{
  return pak::PakTablePlan {
    .table_name = std::move(spec.table_name),
    .offset = spec.offset,
    .size_bytes = spec.size_bytes,
    .count = spec.count,
    .entry_size = spec.entry_size,
    .expected_entry_size = spec.entry_size,
    .alignment = 1U,
    .index_zero_required = false,
    .index_zero_present = false,
    .index_zero_forbidden = false,
  };
}

struct CanonicalRegionSpec final {
  uint64_t base_offset = 0U;
  uint64_t texture_region_size = 0U;
};

auto MakeCanonicalRegions(const CanonicalRegionSpec spec)
  -> std::vector<pak::PakRegionPlan>
{
  const auto trailing_offset = spec.base_offset + spec.texture_region_size;
  return {
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "texture_region",
      .offset = spec.base_offset,
      .size_bytes = spec.texture_region_size,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "buffer_region",
      .offset = trailing_offset,
      .size_bytes = 0U,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "audio_region",
      .offset = trailing_offset,
      .size_bytes = 0U,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "script_region",
      .offset = trailing_offset,
      .size_bytes = 0U,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "physics_region",
      .offset = trailing_offset,
      .size_bytes = 0U,
    }),
  };
}

struct CanonicalTableSpec final {
  uint64_t texture_table_offset = 0U;
  uint32_t texture_count = 0U;
  uint64_t texture_table_size = 0U;
};

auto MakeCanonicalTables(const CanonicalTableSpec spec)
  -> std::vector<pak::PakTablePlan>
{
  const auto trailing_offset
    = spec.texture_table_offset + spec.texture_table_size;
  return {
    MakeTablePlan(TablePlanSpec {
      .table_name = "texture_table",
      .offset = spec.texture_table_offset,
      .size_bytes = spec.texture_table_size,
      .count = spec.texture_count,
      .entry_size = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "buffer_table",
      .offset = trailing_offset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "audio_table",
      .offset = trailing_offset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kAudioEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_resource_table",
      .offset = trailing_offset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptResourceEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_slot_table",
      .offset = trailing_offset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptSlotEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "physics_resource_table",
      .offset = trailing_offset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kPhysicsEntrySize,
    }),
  };
}

class PakWriterTest : public paktest::TempDirFixture { };

NOLINT_TEST_F(PakWriterTest, CrcEnabledWritesValidPakAndPatchesFooterCrc)
{
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  const auto output_path = Root() / "crc_enabled.pak";
  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = output_path,
    .output_manifest_path = {},
    .content_version = 3U,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = false,
      .compute_crc32 = true,
      .fail_on_warnings = false,
    },
  };

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto write_result = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(write_result.diagnostics));
  EXPECT_EQ(write_result.file_size, plan_result.plan->PlannedFileSize());
  EXPECT_TRUE(write_result.pak_crc32 != 0U);
  EXPECT_TRUE(write_result.writing_duration.has_value());

  const auto footer = ReadFooter(output_path);
  EXPECT_EQ(footer.pak_crc32, write_result.pak_crc32);

  EXPECT_NO_THROW({
    auto pak_file = content::PakFile(output_path);
    pak_file.ValidateCrc32Integrity();
  });
}

NOLINT_TEST_F(PakWriterTest, CrcDisabledLeavesFooterFieldAtZero)
{
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  const auto output_path = Root() / "crc_disabled.pak";
  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = output_path,
    .output_manifest_path = {},
    .content_version = 4U,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = false,
      .compute_crc32 = false,
      .fail_on_warnings = false,
    },
  };

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto write_result = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(write_result.diagnostics));
  EXPECT_EQ(write_result.pak_crc32, 0U);

  const auto footer = ReadFooter(output_path);
  EXPECT_EQ(footer.pak_crc32, 0U);

  EXPECT_NO_THROW({
    auto pak_file = content::PakFile(output_path);
    pak_file.ValidateCrc32Integrity();
  });
}

NOLINT_TEST_F(
  PakWriterTest, FullModeWritesInputAssetDescriptorsIntoFinalPakDirectory)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  const auto source = Root() / "input_assets_loose";
  const auto output_path = Root() / "input_assets_output.pak";

  const auto action_desc_rel = std::string { "Descriptors/Input/Move.oiact" };
  const auto context_desc_rel
    = std::string { "Descriptors/Input/Gameplay.oimap" };
  const auto scene_desc_rel = std::string { "Descriptors/Scenes/Main.oscene" };

  const auto action_desc_bytes = MakePatternBytes(0x31U, 48U);
  const auto context_desc_bytes = MakePatternBytes(0x41U, 64U);
  const auto scene_desc_bytes = MakePatternBytes(0x51U, 32U);

  ASSERT_TRUE(paktest::WriteFileBytes(source / action_desc_rel,
    std::span<const std::byte>(
      action_desc_bytes.data(), action_desc_bytes.size())));
  ASSERT_TRUE(paktest::WriteFileBytes(source / context_desc_rel,
    std::span<const std::byte>(
      context_desc_bytes.data(), context_desc_bytes.size())));
  ASSERT_TRUE(paktest::WriteFileBytes(source / scene_desc_rel,
    std::span<const std::byte>(
      scene_desc_bytes.data(), scene_desc_bytes.size())));

  const auto action_key = MakeAssetKey(0x61U);
  const auto context_key = MakeAssetKey(0x62U);
  const auto scene_key = MakeAssetKey(0x63U);
  const auto assets = std::array<paktest::AssetSpec, 3> {
    paktest::AssetSpec {
      .key = action_key,
      .asset_type = data::AssetType::kInputAction,
      .descriptor_relpath = action_desc_rel,
      .virtual_path = "/Game/Input/Move.oiact",
      .descriptor_size = static_cast<uint64_t>(action_desc_bytes.size()),
      .descriptor_sha = paktest::MakeDigest(0x61U),
    },
    paktest::AssetSpec {
      .key = context_key,
      .asset_type = data::AssetType::kInputMappingContext,
      .descriptor_relpath = context_desc_rel,
      .virtual_path = "/Game/Input/Gameplay.oimap",
      .descriptor_size = static_cast<uint64_t>(context_desc_bytes.size()),
      .descriptor_sha = paktest::MakeDigest(0x62U),
    },
    paktest::AssetSpec {
      .key = scene_key,
      .asset_type = data::AssetType::kScene,
      .descriptor_relpath = scene_desc_rel,
      .virtual_path = "/Game/Scenes/Main.oscene",
      .descriptor_size = static_cast<uint64_t>(scene_desc_bytes.size()),
      .descriptor_sha = paktest::MakeDigest(0x63U),
    },
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const paktest::AssetSpec>(assets.data(), assets.size()),
    std::span<const paktest::FileSpec> {}, 0x71U));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = output_path,
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeSourceKey(),
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

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto write_result = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(write_result.diagnostics));

  const auto pak_bytes = ReadAllBytes(output_path);
  auto pak_file = content::PakFile(output_path);
  const auto directory = pak_file.Directory();
  ASSERT_EQ(directory.size(), assets.size());

  auto saw_input_action = false;
  auto saw_input_mapping_context = false;
  auto saw_scene = false;
  for (const auto& entry : directory) {
    const auto descriptor_offset = static_cast<size_t>(entry.desc_offset);
    const auto descriptor_size = static_cast<size_t>(entry.desc_size);
    ASSERT_LE(descriptor_offset + descriptor_size, pak_bytes.size());
    const auto payload = std::span<const std::byte>(
      pak_bytes.data() + descriptor_offset, descriptor_size);

    if (entry.asset_key == action_key) {
      EXPECT_EQ(static_cast<data::AssetType>(entry.asset_type),
        data::AssetType::kInputAction);
      EXPECT_EQ(payload.size(), action_desc_bytes.size());
      EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
        action_desc_bytes.begin(), action_desc_bytes.end()));
      saw_input_action = true;
    } else if (entry.asset_key == context_key) {
      EXPECT_EQ(static_cast<data::AssetType>(entry.asset_type),
        data::AssetType::kInputMappingContext);
      EXPECT_EQ(payload.size(), context_desc_bytes.size());
      EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
        context_desc_bytes.begin(), context_desc_bytes.end()));
      saw_input_mapping_context = true;
    } else if (entry.asset_key == scene_key) {
      EXPECT_EQ(static_cast<data::AssetType>(entry.asset_type),
        data::AssetType::kScene);
      EXPECT_EQ(payload.size(), scene_desc_bytes.size());
      EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
        scene_desc_bytes.begin(), scene_desc_bytes.end()));
      saw_scene = true;
    } else {
      FAIL() << "Unexpected asset key in directory";
    }
  }

  EXPECT_TRUE(saw_input_action);
  EXPECT_TRUE(saw_input_mapping_context);
  EXPECT_TRUE(saw_scene);
}

NOLINT_TEST_F(
  PakWriterTest, FullModeWritesPhysicsSceneDescriptorsIntoFinalPakDirectory)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  const auto source = Root() / "physics_assets_loose";
  const auto output_path = Root() / "physics_assets_output.pak";

  const auto physics_desc_rel = std::string { "Scenes/Main.opscene" };
  const auto physics_desc_bytes = MakePatternBytes(0x71U, 56U);

  ASSERT_TRUE(paktest::WriteFileBytes(source / physics_desc_rel,
    std::span<const std::byte>(
      physics_desc_bytes.data(), physics_desc_bytes.size())));

  const auto physics_key = MakeAssetKey(0xA1U);
  const auto assets = std::array<paktest::AssetSpec, 1> {
    paktest::AssetSpec {
      .key = physics_key,
      .asset_type = data::AssetType::kPhysicsScene,
      .descriptor_relpath = physics_desc_rel,
      .virtual_path = "/Game/Scenes/Main.opscene",
      .descriptor_size = static_cast<uint64_t>(physics_desc_bytes.size()),
      .descriptor_sha = paktest::MakeDigest(0xA1U),
    },
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const paktest::AssetSpec>(assets.data(), assets.size()),
    std::span<const paktest::FileSpec> {}, 0xA2U));

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = output_path,
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeSourceKey(),
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

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto write_result = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(write_result.diagnostics));

  const auto pak_bytes = ReadAllBytes(output_path);
  auto pak_file = content::PakFile(output_path);
  const auto directory = pak_file.Directory();
  ASSERT_EQ(directory.size(), assets.size());

  const auto& entry = directory.front();
  EXPECT_EQ(entry.asset_key, physics_key);
  EXPECT_EQ(static_cast<data::AssetType>(entry.asset_type),
    data::AssetType::kPhysicsScene);

  const auto descriptor_offset = static_cast<size_t>(entry.desc_offset);
  const auto descriptor_size = static_cast<size_t>(entry.desc_size);
  ASSERT_LE(descriptor_offset + descriptor_size, pak_bytes.size());

  const auto payload = std::span<const std::byte>(
    pak_bytes.data() + descriptor_offset, descriptor_size);
  ASSERT_EQ(payload.size(), physics_desc_bytes.size());
  EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
    physics_desc_bytes.begin(), physics_desc_bytes.end()));
}

NOLINT_TEST_F(PakWriterTest, OffsetMismatchEmitsActionableDiagnostic)
{
  using pak::BuildMode;
  using pak::PakWriter;

  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = 1U,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = {
    MakeRegionPlan(RegionPlanSpec { .region_name = "texture_region",
      .offset = kMismatchedOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "buffer_region",
      .offset = kMismatchedOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "audio_region",
      .offset = kMismatchedOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "script_region",
      .offset = kMismatchedOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "physics_region",
      .offset = kMismatchedOffset,
      .size_bytes = 0U }),
  };
  plan_data.tables = {
    MakeTablePlan(TablePlanSpec {
      .table_name = "texture_table",
      .offset = kMismatchedOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "buffer_table",
      .offset = kMismatchedOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "audio_table",
      .offset = kMismatchedOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kAudioEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_resource_table",
      .offset = kMismatchedOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptResourceEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_slot_table",
      .offset = kMismatchedOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptSlotEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "physics_resource_table",
      .offset = kMismatchedOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kPhysicsEntrySize,
    }),
  };
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kMismatchedOffset, .size_bytes = 0U, .entries = {}
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false, .offset = 0U, .size_bytes = 0U, .entries = {}
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kMismatchedOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kMismatchedOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kMismatchedOffset + kFooterSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "offset_mismatch.pak",
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(
    HasDiagnosticCode(write_result.diagnostics, "pak.write.offset_mismatch"));
}

NOLINT_TEST_F(PakWriterTest, InvalidCrcPatchOffsetEmitsDiagnostic)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kBaseOffset = uint64_t { 256U };

  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = 5U,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = {
    MakeRegionPlan(RegionPlanSpec { .region_name = "texture_region",
      .offset = kBaseOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "buffer_region",
      .offset = kBaseOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "audio_region", .offset = kBaseOffset, .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "script_region",
      .offset = kBaseOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "physics_region",
      .offset = kBaseOffset,
      .size_bytes = 0U }),
  };
  plan_data.tables = {
    MakeTablePlan(TablePlanSpec {
      .table_name = "texture_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "buffer_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "audio_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kAudioEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_resource_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptResourceEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_slot_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptSlotEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "physics_resource_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kPhysicsEntrySize,
    }),
  };
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kBaseOffset,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0U,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kBaseOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kBaseOffset + offsetof(core::PakFooter, pak_crc32) + 1U,
  };
  plan_data.planned_file_size = kBaseOffset + kFooterSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "invalid_crc_offset.pak",
    .output_manifest_path = {},
    .content_version = 5U,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.crc_field_offset_invalid"));
}

NOLINT_TEST_F(PakWriterTest, ResourcePayloadSourceCountMismatchIsRejected)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kBaseOffset = uint64_t { 256U };
  constexpr auto kResourceSize = uint64_t { 4U };
  constexpr auto kFooterOffset = uint64_t { 512U };
  constexpr auto kPlannedFileSize = kFooterOffset + kFooterSize;
  constexpr auto kContentVersion = uint16_t { 10U };

  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = MakeCanonicalRegions(CanonicalRegionSpec {
    .base_offset = kBaseOffset,
    .texture_region_size = kResourceSize,
  });
  plan_data.resources = {
    pak::PakResourcePlacementPlan {
      .resource_kind = "texture",
      .resource_index = 0U,
      .region_name = "texture_region",
      .offset = kBaseOffset,
      .size_bytes = kResourceSize,
      .alignment = 1U,
      .reserved_bytes_zeroed = true,
    },
  };
  plan_data.tables = MakeCanonicalTables(CanonicalTableSpec {
    .texture_table_offset = kFooterOffset,
  });
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kFooterOffset,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0U,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kFooterOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kFooterOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kPlannedFileSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "resource_source_count_mismatch.pak",
    .output_manifest_path = {},
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.resource_source_count_mismatch"));
}

NOLINT_TEST_F(PakWriterTest, AssetPayloadSourceCountMismatchIsRejected)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kBaseOffset = uint64_t { 256U };
  constexpr auto kAssetOffset = uint64_t { 384U };
  constexpr auto kAssetSize = uint64_t { 8U };
  constexpr auto kDirectoryOffset = uint64_t { 512U };
  constexpr auto kFooterOffset = uint64_t { 640U };
  constexpr auto kPlannedFileSize = kFooterOffset + kFooterSize;
  constexpr auto kAssetKeySeed = uint8_t { 0x2AU };
  constexpr auto kContentVersion = uint16_t { 11U };

  const auto asset_key = MakeAssetKey(kAssetKeySeed);
  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = MakeCanonicalRegions(CanonicalRegionSpec {
    .base_offset = kBaseOffset,
  });
  plan_data.tables = MakeCanonicalTables(CanonicalTableSpec {
    .texture_table_offset = kBaseOffset,
  });
  plan_data.assets = {
    pak::PakAssetPlacementPlan {
      .asset_key = asset_key,
      .asset_type = data::AssetType::kGeometry,
      .offset = kAssetOffset,
      .size_bytes = kAssetSize,
      .alignment = 1U,
      .reserved_bytes_zeroed = true,
    },
  };
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kDirectoryOffset,
    .size_bytes = sizeof(core::AssetDirectoryEntry),
    .entries = {
      pak::PakAssetDirectoryEntryPlan {
        .asset_key = asset_key,
        .asset_type = data::AssetType::kGeometry,
        .entry_offset = kDirectoryOffset,
        .descriptor_offset = kAssetOffset,
        .descriptor_size = static_cast<uint32_t>(kAssetSize),
      },
    },
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0U,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kFooterOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kFooterOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kPlannedFileSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "asset_source_count_mismatch.pak",
    .output_manifest_path = {},
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.asset_source_count_mismatch"));
}

NOLINT_TEST_F(PakWriterTest, MissingResourceSourceFileEmitsStoreDiagnostic)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kTextureRegionOffset = uint64_t { 256U };
  constexpr auto kTexturePayloadSize = uint64_t { 8U };
  constexpr auto kTextureTableOffset = uint64_t { 512U };
  constexpr auto kFooterOffset = uint64_t { 896U };
  constexpr auto kPlannedFileSize = kFooterOffset + kFooterSize;
  constexpr auto kContentVersion = uint16_t { 12U };

  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = MakeCanonicalRegions(CanonicalRegionSpec {
    .base_offset = kTextureRegionOffset,
    .texture_region_size = kTexturePayloadSize,
  });
  plan_data.resources = {
    pak::PakResourcePlacementPlan {
      .resource_kind = "texture",
      .resource_index = 0U,
      .region_name = "texture_region",
      .offset = kTextureRegionOffset,
      .size_bytes = kTexturePayloadSize,
      .alignment = 1U,
      .reserved_bytes_zeroed = true,
    },
  };
  plan_data.resource_payload_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = Root() / "missing_texture_payload.bin",
      .source_offset = 0U,
      .size_bytes = kTexturePayloadSize,
    },
  };
  plan_data.tables = MakeCanonicalTables(CanonicalTableSpec {
    .texture_table_offset = kTextureTableOffset,
    .texture_count = 1U,
    .texture_table_size = sizeof(core::TextureResourceDesc),
  });
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kFooterOffset,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0U,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kFooterOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kFooterOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kPlannedFileSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "missing_resource_payload_source.pak",
    .output_manifest_path = {},
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.resource_store_failed"));
}

NOLINT_TEST_F(
  PakWriterTest, MissingAssetDescriptorSourceFileEmitsStoreDiagnostic)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kBaseOffset = uint64_t { 256U };
  constexpr auto kAssetOffset = uint64_t { 512U };
  constexpr auto kAssetSize = uint64_t { 8U };
  constexpr auto kDirectoryOffset = uint64_t { 768U };
  constexpr auto kFooterOffset = uint64_t { 1024U };
  constexpr auto kPlannedFileSize = kFooterOffset + kFooterSize;
  constexpr auto kAssetKeySeed = uint8_t { 0x4CU };
  constexpr auto kContentVersion = uint16_t { 13U };

  const auto asset_key = MakeAssetKey(kAssetKeySeed);
  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = MakeCanonicalRegions(CanonicalRegionSpec {
    .base_offset = kBaseOffset,
  });
  plan_data.tables = MakeCanonicalTables(CanonicalTableSpec {
    .texture_table_offset = kBaseOffset,
  });
  plan_data.assets = {
    pak::PakAssetPlacementPlan {
      .asset_key = asset_key,
      .asset_type = data::AssetType::kGeometry,
      .offset = kAssetOffset,
      .size_bytes = kAssetSize,
      .alignment = 1U,
      .reserved_bytes_zeroed = true,
    },
  };
  plan_data.asset_payload_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = Root() / "missing_descriptor_payload.bin",
      .source_offset = 0U,
      .size_bytes = kAssetSize,
    },
  };
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kDirectoryOffset,
    .size_bytes = sizeof(core::AssetDirectoryEntry),
    .entries = {
      pak::PakAssetDirectoryEntryPlan {
        .asset_key = asset_key,
        .asset_type = data::AssetType::kGeometry,
        .entry_offset = kDirectoryOffset,
        .descriptor_offset = kAssetOffset,
        .descriptor_size = static_cast<uint32_t>(kAssetSize),
      },
    },
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0U,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kFooterOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kFooterOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kPlannedFileSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "missing_asset_descriptor_payload_source.pak",
    .output_manifest_path = {},
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.asset_descriptor_store_failed"));
}

NOLINT_TEST_F(PakWriterTest, TablePayloadSizeMismatchEmitsDiagnostic)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kBaseOffset = uint64_t { 256U };
  constexpr auto kTextureTableCount = uint32_t { 1U };

  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = 7U,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = {
    MakeRegionPlan(RegionPlanSpec { .region_name = "texture_region",
      .offset = kBaseOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "buffer_region",
      .offset = kBaseOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "audio_region", .offset = kBaseOffset, .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "script_region",
      .offset = kBaseOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "physics_region",
      .offset = kBaseOffset,
      .size_bytes = 0U }),
  };
  plan_data.tables = {
    MakeTablePlan(TablePlanSpec {
      .table_name = "texture_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = kTextureTableCount,
      .entry_size = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "buffer_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "audio_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kAudioEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_resource_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptResourceEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_slot_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptSlotEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "physics_resource_table",
      .offset = kBaseOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kPhysicsEntrySize,
    }),
  };
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kBaseOffset,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0U,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kBaseOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kBaseOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kBaseOffset + kFooterSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "table_size_mismatch.pak",
    .output_manifest_path = {},
    .content_version = 7U,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.table_size_mismatch"));
}

NOLINT_TEST_F(PakWriterTest, StoresResourceAndDescriptorPayloadBytesFromSources)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kTextureRegionOffset = uint64_t { 256U };
  constexpr auto kTexturePayloadSize = uint64_t { 8U };
  constexpr auto kTextureTableOffset = uint64_t { 512U };
  constexpr auto kAssetDescriptorOffset = uint64_t { 768U };
  constexpr auto kAssetDescriptorSize = uint64_t { 12U };
  constexpr auto kDirectoryOffset = uint64_t { 896U };
  constexpr auto kFooterOffset = uint64_t { 1152U };
  constexpr auto kPlannedFileSize = kFooterOffset + kFooterSize;
  constexpr auto kTexturePayloadStart = uint8_t { 1U };
  constexpr auto kDescriptorPayloadStart = uint8_t { 161U };

  const auto texture_source_path = Root() / "texture_payload.bin";
  const auto descriptor_source_path = Root() / "descriptor_payload.bin";
  const auto texture_payload = MakePatternBytes(
    kTexturePayloadStart, static_cast<size_t>(kTexturePayloadSize));
  const auto descriptor_payload = MakePatternBytes(
    kDescriptorPayloadStart, static_cast<size_t>(kAssetDescriptorSize));
  ASSERT_TRUE(WriteAllBytesToFile(texture_source_path, texture_payload));
  ASSERT_TRUE(WriteAllBytesToFile(descriptor_source_path, descriptor_payload));

  const auto asset_key = MakeAssetKey(static_cast<uint8_t>(0x3D));
  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = 8U,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = {
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "texture_region",
      .offset = kTextureRegionOffset,
      .size_bytes = kTexturePayloadSize,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "buffer_region",
      .offset = kTextureRegionOffset + kTexturePayloadSize,
      .size_bytes = 0U,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "audio_region",
      .offset = kTextureRegionOffset + kTexturePayloadSize,
      .size_bytes = 0U,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "script_region",
      .offset = kTextureRegionOffset + kTexturePayloadSize,
      .size_bytes = 0U,
    }),
    MakeRegionPlan(RegionPlanSpec {
      .region_name = "physics_region",
      .offset = kTextureRegionOffset + kTexturePayloadSize,
      .size_bytes = 0U,
    }),
  };
  plan_data.resources = {
    pak::PakResourcePlacementPlan {
      .resource_kind = "texture",
      .resource_index = 0U,
      .region_name = "texture_region",
      .offset = kTextureRegionOffset,
      .size_bytes = kTexturePayloadSize,
      .alignment = 1U,
      .reserved_bytes_zeroed = true,
    },
  };
  plan_data.resource_payload_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = texture_source_path,
      .source_offset = 0U,
      .size_bytes = kTexturePayloadSize,
    },
  };
  plan_data.tables = {
    MakeTablePlan(TablePlanSpec {
      .table_name = "texture_table",
      .offset = kTextureTableOffset,
      .size_bytes = sizeof(core::TextureResourceDesc),
      .count = 1U,
      .entry_size = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "buffer_table",
      .offset = kTextureTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "audio_table",
      .offset = kTextureTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kAudioEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_resource_table",
      .offset = kTextureTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptResourceEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_slot_table",
      .offset = kTextureTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptSlotEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "physics_resource_table",
      .offset = kTextureTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kPhysicsEntrySize,
    }),
  };
  plan_data.assets = {
    pak::PakAssetPlacementPlan {
      .asset_key = asset_key,
      .asset_type = data::AssetType::kGeometry,
      .offset = kAssetDescriptorOffset,
      .size_bytes = kAssetDescriptorSize,
      .alignment = 1U,
      .reserved_bytes_zeroed = true,
    },
  };
  plan_data.asset_payload_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = descriptor_source_path,
      .source_offset = 0U,
      .size_bytes = kAssetDescriptorSize,
    },
  };
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kDirectoryOffset,
    .size_bytes = sizeof(core::AssetDirectoryEntry),
    .entries = {
      pak::PakAssetDirectoryEntryPlan {
        .asset_key = asset_key,
        .asset_type = data::AssetType::kGeometry,
        .entry_offset = kDirectoryOffset,
        .descriptor_offset = kAssetDescriptorOffset,
        .descriptor_size = static_cast<uint32_t>(kAssetDescriptorSize),
      },
    },
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0U,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kFooterOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kFooterOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kPlannedFileSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "payload_copy.pak",
    .output_manifest_path = {},
    .content_version = 8U,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = false,
      .compute_crc32 = false,
      .fail_on_warnings = false,
    },
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  ASSERT_FALSE(HasError(write_result.diagnostics));

  const auto bytes = ReadAllBytes(request.output_pak_path);
  ASSERT_EQ(bytes.size(), static_cast<size_t>(kPlannedFileSize));

  const auto texture_view
    = std::span<const std::byte>(bytes.data() + kTextureRegionOffset,
      static_cast<size_t>(kTexturePayloadSize));
  const auto descriptor_view
    = std::span<const std::byte>(bytes.data() + kAssetDescriptorOffset,
      static_cast<size_t>(kAssetDescriptorSize));
  EXPECT_TRUE(std::ranges::equal(texture_view, texture_payload));
  EXPECT_TRUE(std::ranges::equal(descriptor_view, descriptor_payload));
}

NOLINT_TEST_F(PakWriterTest, WriterZeroFillsPlannedPaddingAndTrailingGaps)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kTextureRegionOffset = uint64_t { 512U };
  constexpr auto kTextureRegionEnd = kTextureRegionOffset + kTextureDataSize;
  constexpr auto kTableOffset = uint64_t { 1024U };
  constexpr auto kDirectoryOffset = uint64_t { 1152U };
  constexpr auto kFooterOffset = uint64_t { 1280U };
  constexpr auto kPlannedFileSize = uint64_t { 1792U };

  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = 2U,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = {
    MakeRegionPlan(RegionPlanSpec { .region_name = "texture_region",
      .offset = kTextureRegionOffset,
      .size_bytes = kTextureDataSize }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "buffer_region",
      .offset = kTextureRegionEnd,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "audio_region",
      .offset = kTextureRegionEnd,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "script_region",
      .offset = kTextureRegionEnd,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "physics_region",
      .offset = kTextureRegionEnd,
      .size_bytes = 0U }),
  };
  plan_data.resources = {
    pak::PakResourcePlacementPlan {
      .resource_kind = "texture",
      .resource_index = 0U,
      .region_name = "texture_region",
      .offset = kTextureRegionOffset,
      .size_bytes = kTextureDataSize,
      .alignment = 1U,
      .reserved_bytes_zeroed = true,
    },
  };
  const auto resource_source_path = Root() / "writer_zero_fill_texture.bin";
  const auto resource_bytes = std::vector<std::byte>(
    static_cast<size_t>(kTextureDataSize), std::byte { 0 });
  ASSERT_TRUE(WriteAllBytesToFile(resource_source_path, resource_bytes));
  plan_data.resource_payload_sources = {
    pak::PakPayloadSourceSlicePlan {
      .source_path = resource_source_path,
      .source_offset = 0U,
      .size_bytes = kTextureDataSize,
    },
  };
  plan_data.tables = {
    MakeTablePlan(TablePlanSpec {
      .table_name = "texture_table",
      .offset = kTableOffset,
      .size_bytes = sizeof(core::TextureResourceDesc),
      .count = 1U,
      .entry_size = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "buffer_table",
      .offset = kTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "audio_table",
      .offset = kTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kAudioEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_resource_table",
      .offset = kTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptResourceEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_slot_table",
      .offset = kTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptSlotEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "physics_resource_table",
      .offset = kTableOffset + sizeof(core::TextureResourceDesc),
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kPhysicsEntrySize,
    }),
  };
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kDirectoryOffset, .size_bytes = 0U, .entries = {}
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false, .offset = 0U, .size_bytes = 0U, .entries = {}
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kFooterOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kFooterOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kPlannedFileSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "zero_fill.pak",
    .output_manifest_path = {},
    .content_version = 2U,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  ASSERT_FALSE(HasError(write_result.diagnostics));
  ASSERT_EQ(write_result.file_size, kPlannedFileSize);

  const auto bytes = ReadAllBytes(request.output_pak_path);
  ASSERT_EQ(bytes.size(), static_cast<size_t>(kPlannedFileSize));

  EXPECT_TRUE(IsAllZero(std::span<const std::byte>(bytes.data() + kHeaderSize,
    static_cast<size_t>(kTextureRegionOffset - kHeaderSize))));
  EXPECT_TRUE(
    IsAllZero(std::span<const std::byte>(bytes.data() + kTextureRegionEnd,
      static_cast<size_t>(kTableOffset - kTextureRegionEnd))));
  EXPECT_TRUE(IsAllZero(
    std::span<const std::byte>(bytes.data() + (kFooterOffset + kFooterSize),
      static_cast<size_t>(kPlannedFileSize - (kFooterOffset + kFooterSize)))));
}

NOLINT_TEST_F(PakWriterTest, DeterministicModeProducesBitExactOutputs)
{
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "deterministic_a.pak",
    .output_manifest_path = {},
    .content_version = 9U,
    .source_key = MakeSourceKey(),
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

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto first = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(first.diagnostics));

  request.output_pak_path = Root() / "deterministic_b.pak";
  const auto second = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(second.diagnostics));

  const auto first_bytes = ReadAllBytes(Root() / "deterministic_a.pak");
  const auto second_bytes = ReadAllBytes(Root() / "deterministic_b.pak");

  EXPECT_EQ(first.pak_crc32, second.pak_crc32);
  EXPECT_EQ(first.file_size, second.file_size);
  EXPECT_EQ(first_bytes, second_bytes);
}

NOLINT_TEST_F(PakWriterTest, ScriptSlotSerializerInvariantViolationFailsWriting)
{
  using pak::BuildMode;
  using pak::PakWriter;

  constexpr auto kTableOffset = uint64_t { 256U };
  constexpr auto kScriptSlotCount = uint32_t { 1U };
  constexpr auto kScriptSlotSize
    = static_cast<uint64_t>(kScriptSlotCount) * kScriptSlotEntrySize;
  constexpr auto kFooterOffset = kTableOffset + kScriptSlotSize;

  auto plan_data = pak::PakPlan::Data {};
  plan_data.header = pak::PakHeaderPlan {
    .offset = 0U,
    .size_bytes = kHeaderSize,
    .content_version = 6U,
    .source_key = MakeSourceKey(),
  };
  plan_data.regions = {
    MakeRegionPlan(RegionPlanSpec { .region_name = "texture_region",
      .offset = kTableOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "buffer_region",
      .offset = kTableOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "audio_region",
      .offset = kTableOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "script_region",
      .offset = kTableOffset,
      .size_bytes = 0U }),
    MakeRegionPlan(RegionPlanSpec { .region_name = "physics_region",
      .offset = kTableOffset,
      .size_bytes = 0U }),
  };
  plan_data.tables = {
    MakeTablePlan(TablePlanSpec {
      .table_name = "texture_table",
      .offset = kTableOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::TextureResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "buffer_table",
      .offset = kTableOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = static_cast<uint32_t>(sizeof(core::BufferResourceDesc)),
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "audio_table",
      .offset = kTableOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kAudioEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_resource_table",
      .offset = kTableOffset,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kScriptResourceEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "script_slot_table",
      .offset = kTableOffset,
      .size_bytes = kScriptSlotSize,
      .count = kScriptSlotCount,
      .entry_size = kScriptSlotEntrySize,
    }),
    MakeTablePlan(TablePlanSpec {
      .table_name = "physics_resource_table",
      .offset = kTableOffset + kScriptSlotSize,
      .size_bytes = 0U,
      .count = 0U,
      .entry_size = kPhysicsEntrySize,
    }),
  };
  plan_data.script_param_ranges = {
    pak::PakScriptParamRangePlan {
      .slot_index = 2U,
      .params_array_offset = 0U,
      .params_count = 1U,
    },
  };
  plan_data.directory = pak::PakDirectoryPlan {
    .offset = kFooterOffset,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.browse_index = pak::PakBrowseIndexPlan {
    .enabled = false,
    .offset = 0U,
    .size_bytes = 0U,
    .entries = {},
  };
  plan_data.footer = pak::PakFooterPlan {
    .offset = kFooterOffset,
    .size_bytes = kFooterSize,
    .crc32_field_absolute_offset
    = kFooterOffset + offsetof(core::PakFooter, pak_crc32),
  };
  plan_data.planned_file_size = kFooterOffset + kFooterSize;

  const auto request = pak::PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Root() / "script_slot_invariant.pak",
    .output_manifest_path = {},
    .content_version = 6U,
    .source_key = MakeSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  const auto write_result
    = PakWriter {}.Write(request, pak::PakPlan(std::move(plan_data)));
  EXPECT_TRUE(HasError(write_result.diagnostics));
  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.write.table_store_failed"));
}

} // namespace
