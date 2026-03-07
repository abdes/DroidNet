//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <cstdint>
#include <string_view>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>

#include "PakTestSupport.h"

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;

auto MakeNonZeroSourceKey() -> data::SourceKey
{
  return paktest::MakeSourceKey(static_cast<uint8_t>(1U));
}

auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  return paktest::MakeSourceKey(seed);
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  return paktest::MakeAssetKey(seed);
}

auto HasDiagnosticCode(
  const pak::PakBuildResult& result, const std::string_view code) -> bool
{
  return paktest::HasDiagnosticCode(result.diagnostics, code);
}

class PakBuilderApiContractTestFixture : public paktest::TempDirFixture { };

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

NOLINT_TEST_F(
  PakBuilderApiContractTestFixture, PatchRequestValidationEmitsAllModeErrors)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;

  const PakBuildRequest request {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = Path("api_validation_output.pak"),
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

NOLINT_TEST_F(PakBuilderApiContractTestFixture,
  FullManifestOptionRequiresOutputManifestPath)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildOptions;
  using pak::PakBuildRequest;

  const PakBuildRequest request {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Path("full_manifest_option_output.pak"),
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

NOLINT_TEST_F(
  PakBuilderApiContractTestFixture, ValidRequestWritesPakAndReportsTelemetry)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;

  const PakBuildRequest request {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Path("valid_request_output.pak"),
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
  EXPECT_EQ(result.output_catalog.source_key, request.source_key);
  EXPECT_EQ(result.output_catalog.content_version, request.content_version);
  EXPECT_TRUE(result.output_catalog.entries.empty());
  EXPECT_NE(
    result.output_catalog.catalog_digest, data::PakCatalog {}.catalog_digest);
  EXPECT_FALSE(result.patch_manifest.has_value());
  EXPECT_TRUE(result.telemetry.total_duration.has_value());
  EXPECT_TRUE(result.telemetry.planning_duration.has_value());
  EXPECT_TRUE(result.telemetry.writing_duration.has_value());
  EXPECT_FALSE(result.telemetry.manifest_duration.has_value());
}

NOLINT_TEST_F(PakBuilderApiContractTestFixture,
  FullBuildWithManifestPopulatesPatchManifestAndManifestTelemetry)
{
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildOptions;
  using pak::PakBuildRequest;

  const PakBuildRequest request {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = Path("full_manifest_output.pak"),
    .output_manifest_path = Path("full_manifest_output.manifest.json"),
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
  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  EXPECT_TRUE(result.patch_manifest.has_value());
  EXPECT_TRUE(result.telemetry.manifest_duration.has_value());
}

NOLINT_TEST_F(PakBuilderApiContractTestFixture,
  FullBuildPopulatesOutputCatalogForEmittedAssets)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;

  const auto source = Root() / "full_catalog_source";
  const auto asset = paktest::AssetSpec {
    .key = MakeAssetKey(static_cast<uint8_t>(0x41U)),
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "Descriptors/material.desc",
    .virtual_path = "/Game/Materials/Main.omat",
    .descriptor_size = 32U,
    .descriptor_sha = paktest::MakeDigest(static_cast<uint8_t>(0x61U)),
  };
  const auto descriptor_bytes
    = std::vector<std::byte>(static_cast<size_t>(asset.descriptor_size));
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const paktest::AssetSpec>(&asset, 1U),
    std::span<const paktest::FileSpec> {}, static_cast<uint8_t>(0x31U)));
  ASSERT_TRUE(paktest::WriteFileBytes(source / asset.descriptor_relpath,
    std::span<const std::byte>(
      descriptor_bytes.data(), descriptor_bytes.size())));

  const PakBuildRequest request {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Path("full_catalog_output.pak"),
    .output_manifest_path = {},
    .content_version = 7,
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
  ASSERT_EQ(result.output_catalog.entries.size(), 1U);
  EXPECT_EQ(result.output_catalog.source_key, request.source_key);
  EXPECT_EQ(result.output_catalog.content_version, request.content_version);
  EXPECT_EQ(result.output_catalog.entries[0].asset_key, asset.key);
  EXPECT_EQ(result.output_catalog.entries[0].asset_type, asset.asset_type);
  EXPECT_EQ(
    result.output_catalog.entries[0].descriptor_digest, asset.descriptor_sha);
  const auto expected_transitive_digest
    = oxygen::base::ComputeSha256(std::span<const std::byte> {});
  EXPECT_EQ(result.output_catalog.entries[0].transitive_resource_digest,
    expected_transitive_digest);
  EXPECT_NE(
    result.output_catalog.catalog_digest, data::PakCatalog {}.catalog_digest);
}

NOLINT_TEST_F(PakBuilderApiContractTestFixture,
  PatchBuildPopulatesOutputCatalogForEmittedPatchAssets)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;

  const auto source = Root() / "patch_catalog_source";
  const auto asset_key = MakeAssetKey(static_cast<uint8_t>(0x51U));
  const auto asset = paktest::AssetSpec {
    .key = asset_key,
    .asset_type = data::AssetType::kMaterial,
    .descriptor_relpath = "Descriptors/material.desc",
    .virtual_path = "/Game/Materials/Patch.omat",
    .descriptor_size = 24U,
    .descriptor_sha = paktest::MakeDigest(static_cast<uint8_t>(0x71U)),
  };
  const auto descriptor_bytes
    = std::vector<std::byte>(static_cast<size_t>(asset.descriptor_size));
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const paktest::AssetSpec>(&asset, 1U),
    std::span<const paktest::FileSpec> {}, static_cast<uint8_t>(0x41U)));
  ASSERT_TRUE(paktest::WriteFileBytes(source / asset.descriptor_relpath,
    std::span<const std::byte>(
      descriptor_bytes.data(), descriptor_bytes.size())));

  data::PakCatalog base_catalog {};
  base_catalog.source_key = MakeSourceKey(static_cast<uint8_t>(0x21U));
  base_catalog.content_version = 5U;
  base_catalog.entries = {
    data::PakCatalogEntry {
      .asset_key = asset_key,
      .asset_type = data::AssetType::kMaterial,
      .descriptor_digest = paktest::MakeDigest(static_cast<uint8_t>(0x11U)),
      .transitive_resource_digest
      = paktest::MakeDigest(static_cast<uint8_t>(0x11U)),
    },
  };

  const PakBuildRequest request {
    .mode = BuildMode::kPatch,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source } },
    .output_pak_path = Path("patch_catalog_output.pak"),
    .output_manifest_path = Path("patch_catalog_output.manifest"),
    .content_version = 5U,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = { base_catalog },
    .patch_compat = {},
    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_TRUE(result_or_error.has_value());
  const auto& result = result_or_error.value();
  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  EXPECT_EQ(result.summary.patch_replaced, 1U);
  ASSERT_EQ(result.output_catalog.entries.size(), 1U);
  EXPECT_EQ(result.output_catalog.source_key, request.source_key);
  EXPECT_EQ(result.output_catalog.content_version, request.content_version);
  EXPECT_EQ(result.output_catalog.entries[0].asset_key, asset.key);
  EXPECT_EQ(result.output_catalog.entries[0].asset_type, asset.asset_type);
  EXPECT_EQ(
    result.output_catalog.entries[0].descriptor_digest, asset.descriptor_sha);
  const auto expected_transitive_digest
    = oxygen::base::ComputeSha256(std::span<const std::byte> {});
  EXPECT_EQ(result.output_catalog.entries[0].transitive_resource_digest,
    expected_transitive_digest);
  EXPECT_NE(
    result.output_catalog.catalog_digest, data::PakCatalog {}.catalog_digest);
  EXPECT_TRUE(result.patch_manifest.has_value());
  EXPECT_TRUE(result.telemetry.manifest_duration.has_value());
}

NOLINT_TEST_F(PakBuilderApiContractTestFixture,
  FailOnWarningsEscalatesPlannerWarningToErrorDiagnostic)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakBuilder;
  using pak::PakBuildRequest;

  const auto base_pak_path = Path("warning_source_base.pak");
  const PakBuildRequest seed_request {
    .mode = BuildMode::kFull,
    .sources = {},
    .output_pak_path = base_pak_path,
    .output_manifest_path = {},
    .content_version = 1,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {},
  };

  PakBuilder builder;
  const auto seed_result_or_error = builder.Build(seed_request);
  ASSERT_TRUE(seed_result_or_error.has_value());
  ASSERT_EQ(seed_result_or_error.value().summary.diagnostics_error, 0U);

  const PakBuildRequest request {
    .mode = BuildMode::kFull,
    .sources
    = { CookedSource { .kind = CookedSourceKind::kPak, .path = base_pak_path } },
    .output_pak_path = Path("warning_escalated_output.pak"),
    .output_manifest_path = {},
    .content_version = 1,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = {},
    .patch_compat = {},
    .options = {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = false,
      .compute_crc32 = true,
      .fail_on_warnings = true,
    },
  };

  const auto result_or_error = builder.Build(request);
  ASSERT_TRUE(result_or_error.has_value());
  const auto& result = result_or_error.value();

  EXPECT_GT(result.summary.diagnostics_warning, 0U);
  EXPECT_GT(result.summary.diagnostics_error, 0U);
  EXPECT_TRUE(
    HasDiagnosticCode(result, "pak.plan.pak_source_regions_projected"));
  EXPECT_TRUE(HasDiagnosticCode(result, "pak.request.fail_on_warnings"));
  EXPECT_TRUE(result.telemetry.planning_duration.has_value());
  EXPECT_TRUE(result.telemetry.writing_duration.has_value());
}

NOLINT_TEST_F(
  PakBuilderApiContractTestFixture, PlannerRejectsBaseCatalogTypeMismatch)
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
    .output_pak_path = Path("type_mismatch_output.pak"),
    .output_manifest_path = Path("type_mismatch_output.manifest"),
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

NOLINT_TEST_F(
  PakBuilderApiContractTestFixture, PatchModeClassifiesMissingSourceAsDelete)
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
    .output_pak_path = Path("patch_classification_output.pak"),
    .output_manifest_path = Path("patch_classification_output.manifest"),
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
