//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include <Oxygen/Cooker/Pak/PakBuilder.h>

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;

auto MakeNonZeroSourceKey() -> data::SourceKey
{
  constexpr std::array<uint8_t, 16> bytes { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0 };
  return data::SourceKey { bytes };
}

auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  std::array<uint8_t, 16> bytes {}; // NOLINT
  bytes[0] = seed;
  return data::SourceKey { bytes };
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  auto bytes = std::array<uint8_t, data::AssetKey::kSizeBytes> {};
  bytes.fill(seed);
  return data::AssetKey::FromBytes(bytes);
}

auto HasDiagnosticCode(
  const pak::PakBuildResult& result, const std::string_view code) -> bool
{
  return std::ranges::any_of(
    result.diagnostics, [code](const pak::PakDiagnostic& diagnostic) {
      return diagnostic.code == code;
    });
}

NOLINT_TEST(PakBuilderApiContractTest, PublicTypeDefaultsMatchSpec)
{
  using pak::PakBuildOptions;
  using pak::PatchCompatibilityPolicy;

  const PakBuildOptions options {};
  EXPECT_TRUE(options.deterministic);
  EXPECT_FALSE(options.embed_browse_index);
  EXPECT_FALSE(options.emit_manifest_in_full);
  EXPECT_TRUE(options.compute_crc32);
  EXPECT_FALSE(options.fail_on_warnings);

  const PatchCompatibilityPolicy patch_compat {};
  EXPECT_TRUE(patch_compat.require_exact_base_set);
  EXPECT_TRUE(patch_compat.require_content_version_match);
  EXPECT_TRUE(patch_compat.require_base_source_key_match);
  EXPECT_TRUE(patch_compat.require_catalog_digest_match);
}

NOLINT_TEST(PakBuilderApiContractTest, PatchRequestValidationEmitsAllModeErrors)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;

  const PakBuildRequest request {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = "phase1_test_output.pak",
    .output_manifest_path = {},
    .content_version = 0,
    .source_key = {},
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_TRUE(result_or_error.has_value());
  const auto& result = result_or_error.value();

  EXPECT_EQ(result.summary.diagnostics_error, 3U);
  EXPECT_TRUE(HasDiagnosticCode(result, "pak.request.source_key_zero"));
  EXPECT_TRUE(
    HasDiagnosticCode(result, "pak.request.patch_requires_base_catalogs"));
  EXPECT_TRUE(HasDiagnosticCode(
    result, "pak.request.patch_requires_output_manifest_path"));
}

NOLINT_TEST(
  PakBuilderApiContractTest, FullManifestOptionRequiresOutputManifestPath)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildOptions;
  using pak::PakBuildRequest;

  const PakBuildRequest request {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = "phase1_test_output.pak",
    .output_manifest_path = {},
    .content_version = 1,
    .source_key = MakeNonZeroSourceKey(),
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
  const auto result_or_error = builder.Build(request);

  ASSERT_TRUE(result_or_error.has_value());
  const auto& result = result_or_error.value();

  EXPECT_EQ(result.summary.diagnostics_error, 1U);
  EXPECT_TRUE(HasDiagnosticCode(
    result, "pak.request.full_manifest_requires_output_manifest_path"));
}

NOLINT_TEST(PakBuilderApiContractTest, ValidRequestWritesPakAndReportsTelemetry)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;

  const PakBuildRequest request {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = "phase1_test_output.pak",
    .output_manifest_path = {},
    .content_version = 1,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_TRUE(result_or_error.has_value());
  const auto& result = result_or_error.value();

  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  EXPECT_FALSE(
    HasDiagnosticCode(result, "pak.write.phase3_writer_unavailable"));
  EXPECT_GT(result.file_size, 0U);
  EXPECT_TRUE(result.pak_crc32 != 0U);
  EXPECT_TRUE(result.telemetry.total_duration.has_value());
  EXPECT_TRUE(result.telemetry.planning_duration.has_value());
  EXPECT_TRUE(result.telemetry.writing_duration.has_value());
}

NOLINT_TEST(PakBuilderApiContractTest, PlannerRejectsBaseCatalogTypeMismatch)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;
  constexpr auto kBaseSourceKeyASeed = static_cast<uint8_t>(11);
  constexpr auto kBaseSourceKeyBSeed = static_cast<uint8_t>(12);

  const auto asset_key = MakeAssetKey(static_cast<uint8_t>(77));

  data::PakCatalogEntry entry_a {};
  entry_a.asset_key = asset_key;
  entry_a.asset_type = data::AssetType::kGeometry;

  data::PakCatalogEntry entry_b {};
  entry_b.asset_key = asset_key;
  entry_b.asset_type = data::AssetType::kMaterial;

  data::PakCatalog base_a {};
  base_a.source_key = MakeSourceKey(kBaseSourceKeyASeed);
  base_a.content_version = 1;
  base_a.entries = { entry_a };

  data::PakCatalog base_b {};
  base_b.source_key = MakeSourceKey(kBaseSourceKeyBSeed);
  base_b.content_version = 1;
  base_b.entries = { entry_b };

  const PakBuildRequest request {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = "phase2_test_output.pak",
    .output_manifest_path = "phase2_test_output.manifest",
    .content_version = 1,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = { base_a, base_b },
    .patch_compat = {},
    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_TRUE(result_or_error.has_value());
  const auto& result = result_or_error.value();
  EXPECT_TRUE(HasDiagnosticCode(result, "pak.plan.base_catalog_type_mismatch"));
}

NOLINT_TEST(PakBuilderApiContractTest, PatchModeClassifiesMissingSourceAsDelete)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;
  constexpr auto kBaseSourceSeed = static_cast<uint8_t>(21);
  constexpr auto kAssetASeed = static_cast<uint8_t>(31);
  constexpr auto kAssetBSeed = static_cast<uint8_t>(32);

  data::PakCatalog base_catalog {};
  base_catalog.source_key = MakeSourceKey(kBaseSourceSeed);
  base_catalog.content_version = 1;
  base_catalog.entries = {
    data::PakCatalogEntry {
      .asset_key = MakeAssetKey(kAssetASeed),
      .asset_type = data::AssetType::kGeometry,
      .descriptor_digest = {},
      .transitive_resource_digest = {},
    },
    data::PakCatalogEntry {
      .asset_key = MakeAssetKey(kAssetBSeed),
      .asset_type = data::AssetType::kMaterial,
      .descriptor_digest = {},
      .transitive_resource_digest = {},
    },
  };
  const auto expected_deleted
    = static_cast<uint32_t>(base_catalog.entries.size());

  const PakBuildRequest request {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = "phase4_patch_classification_output.pak",
    .output_manifest_path = "phase4_patch_classification_output.manifest",
    .content_version = 1,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = { base_catalog },
    .patch_compat = {},
    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_TRUE(result_or_error.has_value());
  const auto& result = result_or_error.value();
  EXPECT_EQ(result.summary.patch_created, 0U);
  EXPECT_EQ(result.summary.patch_replaced, 0U);
  EXPECT_EQ(result.summary.patch_deleted, expected_deleted);
  EXPECT_EQ(result.summary.patch_unchanged, 0U);
  EXPECT_EQ(result.summary.diagnostics_error, 0U);
}

} // namespace
