//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/Pak/PakManifestWriter.h>

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
constexpr auto kContentVersion = uint16_t { 7U };
constexpr auto kPatchDiffBasisIdentifier
  = std::string_view { "descriptor_plus_transitive_resources_v1" };

auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  auto bytes = std::array<uint8_t, data::SourceKey::kSizeBytes> {};
  bytes.fill(0U);
  bytes[0] = seed;
  return data::SourceKey::FromBytes(bytes);
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  auto bytes = std::array<uint8_t, data::AssetKey::kSizeBytes> {};
  bytes.fill(seed);
  return data::AssetKey::FromBytes(bytes);
}

auto MakeDigest(const uint8_t seed) -> std::array<uint8_t, 32>
{
  auto digest = std::array<uint8_t, 32> {};
  digest.fill(seed);
  return digest;
}

auto HasDiagnosticCode(const std::span<const pak::PakDiagnostic> diagnostics,
  const std::string_view code) -> bool
{
  return std::ranges::any_of(
    diagnostics, [code](const pak::PakDiagnostic& diagnostic) {
      return diagnostic.code == code;
    });
}

auto ReadFileBytes(const std::filesystem::path& path) -> std::vector<std::byte>
{
  auto stream = std::ifstream(path, std::ios::binary);
  if (!stream.is_open()) {
    return {};
  }

  auto bytes = std::vector<std::byte> {};
  char byte = '\0';
  while (stream.get(byte)) {
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
  }

  return bytes;
}

class PakManifestPhase6Test : public testing::Test {
protected:
  void SetUp() override
  {
    const auto timestamp
      = std::chrono::steady_clock::now().time_since_epoch().count();
    temp_dir_ = std::filesystem::temp_directory_path()
      / ("oxygen_pak_phase6_test_" + std::to_string(timestamp));
    std::filesystem::create_directories(temp_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(temp_dir_); }

  auto Path(std::string_view leaf) const -> std::filesystem::path
  {
    return temp_dir_ / std::filesystem::path(leaf);
  }

  auto MakeBaseCatalog(std::span<const data::PakCatalogEntry> entries) const
    -> data::PakCatalog
  {
    return data::PakCatalog {
      .source_key = MakeSourceKey(static_cast<uint8_t>(0xB1U)),
      .content_version = kContentVersion,
      .catalog_digest = MakeDigest(static_cast<uint8_t>(0xC1U)),
      .entries
      = std::vector<data::PakCatalogEntry>(entries.begin(), entries.end()),
    };
  }

private:
  std::filesystem::path temp_dir_;
};

NOLINT_TEST_F(PakManifestPhase6Test,
  PatchModeEmitsManifestWithDeletedSetAndCompatibilityEnvelope)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildOptions;
  using pak::PakBuildRequest;

  const auto key_a = MakeAssetKey(static_cast<uint8_t>(0x21U));
  const auto key_b = MakeAssetKey(static_cast<uint8_t>(0x22U));
  const auto base_entries = std::array<data::PakCatalogEntry, 2> {
    data::PakCatalogEntry {
      .asset_key = key_a,
      .asset_type = data::AssetType::kMaterial,
      .descriptor_digest = MakeDigest(static_cast<uint8_t>(0x31U)),
      .transitive_resource_digest = MakeDigest(static_cast<uint8_t>(0x41U)),
    },
    data::PakCatalogEntry {
      .asset_key = key_b,
      .asset_type = data::AssetType::kGeometry,
      .descriptor_digest = MakeDigest(static_cast<uint8_t>(0x32U)),
      .transitive_resource_digest = MakeDigest(static_cast<uint8_t>(0x42U)),
    },
  };

  const auto request = PakBuildRequest {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = Path("patch_build.pak"),
    .output_manifest_path = Path("patch_build.manifest.json"),
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(static_cast<uint8_t>(0xA1U)),
    .base_catalogs
    = std::vector<data::PakCatalog> {
        MakeBaseCatalog(std::span<const data::PakCatalogEntry>(base_entries)),
      },
    .patch_compat = {},
    .options = PakBuildOptions {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = false,
      .compute_crc32 = true,
      .fail_on_warnings = false,
    },
  };

  PakBuilder builder;
  const auto build_result = builder.Build(request);
  ASSERT_TRUE(build_result.has_value());
  const auto& result = build_result.value();

  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  ASSERT_TRUE(result.patch_manifest.has_value());
  ASSERT_TRUE(std::filesystem::exists(request.output_manifest_path));
  ASSERT_TRUE(result.telemetry.manifest_duration.has_value());

  const auto& manifest = *result.patch_manifest;
  EXPECT_TRUE(manifest.created.empty());
  EXPECT_TRUE(manifest.replaced.empty());
  EXPECT_EQ(manifest.deleted.size(), base_entries.size());
  EXPECT_EQ(manifest.diff_basis_identifier, kPatchDiffBasisIdentifier);
  EXPECT_EQ(manifest.patch_source_key, request.source_key);
  EXPECT_EQ(manifest.compatibility_envelope.patch_content_version,
    request.content_version);
  EXPECT_EQ(
    manifest.compatibility_envelope.required_base_source_keys.size(), 1U);
  EXPECT_EQ(
    manifest.compatibility_envelope.required_base_content_versions.size(), 1U);
  EXPECT_EQ(
    manifest.compatibility_envelope.required_base_catalog_digests.size(), 1U);
  EXPECT_TRUE(manifest.patch_pak_digest.has_value());
  ASSERT_TRUE(manifest.patch_pak_crc32.has_value());
  EXPECT_EQ(*manifest.patch_pak_crc32, result.pak_crc32);
}

NOLINT_TEST_F(PakManifestPhase6Test,
  FullModeEmitManifestInFullWritesManifestWithFullSemantics)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildOptions;
  using pak::PakBuildRequest;
  using pak::PatchCompatibilityPolicy;

  const auto request = PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Path("full_build.pak"),
    .output_manifest_path = Path("full_build.manifest.json"),
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(static_cast<uint8_t>(0xA2U)),
    .base_catalogs = {},
    .patch_compat = PatchCompatibilityPolicy {
      .require_exact_base_set = false,
      .require_content_version_match = true,
      .require_base_source_key_match = false,
      .require_catalog_digest_match = true,
    },
    .options = PakBuildOptions {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = true,
      .compute_crc32 = false,
      .fail_on_warnings = false,
    },
  };

  PakBuilder builder;
  const auto build_result = builder.Build(request);
  ASSERT_TRUE(build_result.has_value());
  const auto& result = build_result.value();

  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  ASSERT_TRUE(result.patch_manifest.has_value());
  ASSERT_TRUE(std::filesystem::exists(request.output_manifest_path));
  ASSERT_TRUE(result.telemetry.manifest_duration.has_value());

  const auto& manifest = *result.patch_manifest;
  EXPECT_TRUE(manifest.created.empty());
  EXPECT_TRUE(manifest.replaced.empty());
  EXPECT_TRUE(manifest.deleted.empty());
  EXPECT_TRUE(
    manifest.compatibility_envelope.required_base_source_keys.empty());
  EXPECT_TRUE(
    manifest.compatibility_envelope.required_base_content_versions.empty());
  EXPECT_TRUE(
    manifest.compatibility_envelope.required_base_catalog_digests.empty());
  EXPECT_EQ(manifest.compatibility_envelope.patch_content_version,
    request.content_version);
  EXPECT_EQ(manifest.diff_basis_identifier, kPatchDiffBasisIdentifier);
  EXPECT_EQ(manifest.patch_source_key, request.source_key);
  EXPECT_TRUE(manifest.patch_pak_digest.has_value());
  EXPECT_TRUE(manifest.patch_pak_crc32.has_value());
  EXPECT_EQ(manifest.compatibility_policy_snapshot.require_exact_base_set,
    request.patch_compat.require_exact_base_set);
  EXPECT_EQ(
    manifest.compatibility_policy_snapshot.require_content_version_match,
    request.patch_compat.require_content_version_match);
  EXPECT_EQ(
    manifest.compatibility_policy_snapshot.require_base_source_key_match,
    request.patch_compat.require_base_source_key_match);
  EXPECT_EQ(manifest.compatibility_policy_snapshot.require_catalog_digest_match,
    request.patch_compat.require_catalog_digest_match);
}

NOLINT_TEST_F(PakManifestPhase6Test, PatchModeFailsWhenManifestCannotBeWritten)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;

  const auto base_entries = std::array<data::PakCatalogEntry, 1> {
    data::PakCatalogEntry {
      .asset_key = MakeAssetKey(static_cast<uint8_t>(0x23U)),
      .asset_type = data::AssetType::kMaterial,
      .descriptor_digest = MakeDigest(static_cast<uint8_t>(0x33U)),
      .transitive_resource_digest = MakeDigest(static_cast<uint8_t>(0x43U)),
    },
  };
  const auto manifest_as_directory = Path("patch_manifest_as_directory");
  std::filesystem::create_directories(manifest_as_directory);

  const auto request = PakBuildRequest {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = Path("patch_fail_manifest.pak"),
    .output_manifest_path = manifest_as_directory,
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(static_cast<uint8_t>(0xA3U)),
    .base_catalogs
    = std::vector<data::PakCatalog> {
        MakeBaseCatalog(std::span<const data::PakCatalogEntry>(base_entries)),
      },
    .patch_compat = {},
    .options = {},
  };

  PakBuilder builder;
  const auto build_result = builder.Build(request);
  ASSERT_TRUE(build_result.has_value());
  const auto& result = build_result.value();

  EXPECT_GT(result.summary.diagnostics_error, 0U);
  EXPECT_TRUE(
    HasDiagnosticCode(result.diagnostics, "pak.manifest.write_open_failed"));
  EXPECT_FALSE(result.patch_manifest.has_value());
}

NOLINT_TEST_F(PakManifestPhase6Test, FullModeFailsWhenManifestCannotBeWritten)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildOptions;
  using pak::PakBuildRequest;

  const auto manifest_as_directory = Path("full_manifest_as_directory");
  std::filesystem::create_directories(manifest_as_directory);

  const auto request = PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Path("full_fail_manifest.pak"),
    .output_manifest_path = manifest_as_directory,
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(static_cast<uint8_t>(0xA4U)),
    .base_catalogs = {},
    .patch_compat = {},
    .options = PakBuildOptions {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = true,
      .compute_crc32 = true,
      .fail_on_warnings = false,
    },
  };

  PakBuilder builder;
  const auto build_result = builder.Build(request);
  ASSERT_TRUE(build_result.has_value());
  const auto& result = build_result.value();

  EXPECT_GT(result.summary.diagnostics_error, 0U);
  EXPECT_TRUE(
    HasDiagnosticCode(result.diagnostics, "pak.manifest.write_open_failed"));
  EXPECT_FALSE(result.patch_manifest.has_value());
}

NOLINT_TEST_F(
  PakManifestPhase6Test, ManifestValidationRejectsNonDisjointPatchSetsPreEmit)
{
  using pak::BuildMode;
  using pak::PakBuildRequest;
  using pak::PakManifestWriter;
  using pak::PakPatchAction;
  using pak::PakPatchActionRecord;
  using pak::PakPlan;

  const auto conflicting_key = MakeAssetKey(static_cast<uint8_t>(0x61U));
  const auto empty_base_entries = std::array<data::PakCatalogEntry, 0> {};
  const auto request = PakBuildRequest {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = Path("pre_emit_validation.pak"),
    .output_manifest_path = Path("pre_emit_validation.manifest.json"),
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(static_cast<uint8_t>(0xA5U)),
    .base_catalogs
    = std::vector<data::PakCatalog> {
        MakeBaseCatalog(std::span<const data::PakCatalogEntry>(empty_base_entries)),
      },
    .patch_compat = {},
    .options = {},
  };

  auto plan_data = PakPlan::Data {};
  plan_data.patch_actions = {
    PakPatchActionRecord {
      .asset_key = conflicting_key,
      .asset_type = data::AssetType::kMaterial,
      .action = PakPatchAction::kCreate,
    },
    PakPatchActionRecord {
      .asset_key = conflicting_key,
      .asset_type = data::AssetType::kMaterial,
      .action = PakPatchAction::kDelete,
    },
  };

  PakManifestWriter writer;
  const auto write_result = writer.Write(
    request, PakPlan(std::move(plan_data)), static_cast<uint32_t>(0U));

  EXPECT_TRUE(HasDiagnosticCode(
    write_result.diagnostics, "pak.manifest.patch_sets_not_disjoint"));
  EXPECT_FALSE(write_result.manifest.has_value());
  EXPECT_FALSE(std::filesystem::exists(request.output_manifest_path));
}

NOLINT_TEST_F(
  PakManifestPhase6Test, FullModeManifestEmissionIsBitExactDeterministic)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildOptions;
  using pak::PakBuildRequest;

  const auto manifest_path = Path("deterministic.manifest.json");
  const auto request = PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Path("deterministic.pak"),
    .output_manifest_path = manifest_path,
    .content_version = kContentVersion,
    .source_key = MakeSourceKey(static_cast<uint8_t>(0xA6U)),
    .base_catalogs = {},
    .patch_compat = {},
    .options = PakBuildOptions {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = true,
      .compute_crc32 = true,
      .fail_on_warnings = false,
    },
  };

  PakBuilder builder;
  const auto first_result = builder.Build(request);
  ASSERT_TRUE(first_result.has_value());
  ASSERT_EQ(first_result.value().summary.diagnostics_error, 0U);
  const auto first_bytes = ReadFileBytes(manifest_path);
  ASSERT_FALSE(first_bytes.empty());

  const auto second_result = builder.Build(request);
  ASSERT_TRUE(second_result.has_value());
  ASSERT_EQ(second_result.value().summary.diagnostics_error, 0U);
  const auto second_bytes = ReadFileBytes(manifest_path);

  EXPECT_EQ(first_bytes, second_bytes);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
