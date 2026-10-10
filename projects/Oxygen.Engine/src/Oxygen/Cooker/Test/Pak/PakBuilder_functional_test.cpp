//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakBuilder.cpp

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "PakTestSupport.h"

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/Test/Fixtures/LooseCookedTestWriter.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;

auto MakeNonZeroSourceKey() -> data::SourceKey
{
  return paktest::MakeSourceKey(static_cast<uint8_t>(1U));
}

using paktest::MakeSourceKey;

using paktest::MakeAssetKey;

using paktest::HasDiagnosticCode;

class PakBuilderTest : public paktest::TempDirFixture { };

//! One invalid reference graph and the diagnostic the planner must report.
struct ReferenceGraphCase final {
  const char* name;
  const char* code;
};

class PakBuilderReferenceGraphTest
  : public PakBuilderTest,
    public ::testing::WithParamInterface<ReferenceGraphCase> { };

NOLINT_TEST_P(PakBuilderReferenceGraphTest, RejectsInvalidAssetReferenceGraph)
{
  const auto owner_key = MakeAssetKey(41U);
  const auto target_key = MakeAssetKey(42U);
  auto descriptor = data::pak::render::MaterialAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kMaterial);
  descriptor.header.version = data::pak::render::kMaterialAssetVersion;
  const auto bytes = std::as_bytes(std::span(&descriptor, 1U));
  auto script_descriptor = data::pak::scripting::ScriptAssetDesc {};
  script_descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kScript);
  script_descriptor.header.version = data::pak::scripting::kScriptAssetVersion;
  const auto script_bytes = std::as_bytes(std::span(&script_descriptor, 1U));
  const auto owner_references = data::AssetReferences::Create({},
    {
      {
        .key = target_key,
        .kind = data::KeyReferenceKind::kAsset,
        .expected_type = data::AssetType::kMaterial,
      },
    });
  ASSERT_HAS_VALUE(owner_references);

  {
    const auto* const code = GetParam().code;
    const auto root = Path(code);
    // Deliberately forge metadata: production writers reject these pairs.
    auto writer = oxygen::content::testing::LooseCookedTestWriter(root);
    writer.WriteAssetDescriptor(owner_key, data::AssetType::kMaterial,
      "/Game/Owner.omat", "Owner.omat", bytes, *owner_references);
    if (std::string_view(code) != "pak.plan.asset_reference_missing") {
      const bool cycle
        = std::string_view(code) == "pak.plan.asset_reference_cycle";
      const auto references = cycle
        ? data::AssetReferences::Create({},
            {
              {
                .key = owner_key,
                .kind = data::KeyReferenceKind::kAsset,
                .expected_type = data::AssetType::kMaterial,
              },
            })
        : data::AssetReferences::Create({}, {});
      ASSERT_HAS_VALUE(references);
      writer.WriteAssetDescriptor(target_key,
        cycle ? data::AssetType::kMaterial : data::AssetType::kScript,
        cycle ? "/Game/Target.omat" : "/Game/Target.oscript",
        cycle ? "Target.omat" : "Target.oscript", cycle ? bytes : script_bytes,
        *references);
    }
    static_cast<void>(writer.Finish());
    auto request = pak::PakBuildRequest {};
    request.sources = {
      { .kind = data::CookedSourceKind::kLooseCooked, .path = root },
    };
    request.output_pak_path = root / "output.pak";
    request.content_version = 1U;
    request.source_key = MakeNonZeroSourceKey();
    const auto result = pak::PakBuilder {}.Build(request);
    ASSERT_HAS_VALUE(result);
    EXPECT_TRUE(HasDiagnosticCode(result->diagnostics, code));
    EXPECT_FALSE(std::filesystem::exists(request.output_pak_path));
  }
}

INSTANTIATE_TEST_SUITE_P(InvalidGraphs, PakBuilderReferenceGraphTest,
  ::testing::Values(
    ReferenceGraphCase { "MissingTarget", "pak.plan.asset_reference_missing" },
    ReferenceGraphCase {
      "TypeMismatch", "pak.plan.asset_reference_type_mismatch" },
    ReferenceGraphCase { "Cycle", "pak.plan.asset_reference_cycle" }),
  [](const ::testing::TestParamInfo<ReferenceGraphCase>& info) -> std::string {
    return std::string(info.param.name);
  });

NOLINT_TEST_F(PakBuilderTest, SentinelTextureReferencesSurvivePackAndRepack)
{
  namespace content = oxygen::content;
  namespace render = data::pak::render;
  const auto key = MakeAssetKey(42U);
  const auto references = data::AssetReferences::Create(
    {
      {
        .kind = data::ResourceKind::kTexture,
        .index = data::pak::core::kErrorTextureResourceIndex,
      },
      {
        .kind = data::ResourceKind::kTexture,
        .index = data::pak::core::kNoResourceIndex,
      },
    },
    {});
  ASSERT_HAS_VALUE(references);

  auto descriptor = render::MaterialAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kMaterial);
  descriptor.header.version = render::kMaterialAssetVersion;
  descriptor.base_color_texture = data::ResourceReferenceIndex { 0U };
  descriptor.emissive_texture = data::ResourceReferenceIndex { 1U };
  const auto descriptor_bytes = std::as_bytes(std::span(&descriptor, 1U));
  const auto loose_root = Path("loose");
  auto writer = content::import::LooseCookedWriter(loose_root);
  writer.SetSourceKey(MakeNonZeroSourceKey());
  writer.WriteAssetDescriptor(key, data::AssetType::kMaterial,
    "/Game/Missing.omat", "Missing.omat", descriptor_bytes, *references);
  ASSERT_EQ(writer.Finish().assets.size(), 1U);

  auto request = pak::PakBuildRequest {};
  request.mode = pak::BuildMode::kFull;
  request.sources = {
    { .kind = data::CookedSourceKind::kLooseCooked, .path = loose_root },
  };
  request.content_version = 1U;
  request.source_key = MakeSourceKey(2U);
  for (const auto* const filename : { "packed.pak", "repacked.pak" }) {
    request.output_pak_path = Path(filename);
    const auto built = pak::PakBuilder {}.Build(request);
    ASSERT_HAS_VALUE(built);
    for (const auto& diagnostic : built->diagnostics) {
      EXPECT_NE(diagnostic.severity, pak::PakDiagnosticSeverity::kError)
        << diagnostic.message;
    }
    ASSERT_EQ(built->summary.diagnostics_error, 0U);

    auto archive = content::PakFile(request.output_pak_path);
    archive.ValidateCrc32Integrity();
    const auto entry = archive.FindEntry(key);
    ASSERT_HAS_VALUE(entry);
    const auto loaded_references = archive.ReadAssetReferences(key);
    EXPECT_EQ(loaded_references, *references);
    auto bytes_reader = archive.CreateReader(*entry);
    const auto bytes = bytes_reader.ReadBlob(entry->desc_size);
    ASSERT_HAS_VALUE(bytes);
    EXPECT_TRUE(std::ranges::equal(*bytes, descriptor_bytes));

    request.sources = {
      { .kind = data::CookedSourceKind::kPak, .path = request.output_pak_path },
    };
  }
}

NOLINT_TEST_F(PakBuilderTest, PatchRequestValidationEmitsAllModeErrors)
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

    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();

  EXPECT_EQ(result.summary.diagnostics_error, 3U);
  EXPECT_TRUE(
    HasDiagnosticCode(result.diagnostics, "pak.request.source_key_zero"));
  EXPECT_TRUE(HasDiagnosticCode(
    result.diagnostics, "pak.request.patch_requires_base_catalogs"));
  EXPECT_TRUE(HasDiagnosticCode(
    result.diagnostics, "pak.request.patch_requires_output_manifest_path"));
}

NOLINT_TEST_F(PakBuilderTest, FullManifestOptionRequiresOutputManifestPath)
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

  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();

  EXPECT_EQ(result.summary.diagnostics_error, 1U);
  EXPECT_TRUE(HasDiagnosticCode(result.diagnostics,
    "pak.request.full_manifest_requires_output_manifest_path"));
}

NOLINT_TEST_F(PakBuilderTest, ValidRequestWritesPakAndReportsTelemetry)
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

    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();

  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  EXPECT_FALSE(HasDiagnosticCode(
    result.diagnostics, "pak.write.phase3_writer_unavailable"));
  EXPECT_GT(result.file_size, 0U);
  EXPECT_NE(result.pak_crc32, 0U);
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

NOLINT_TEST_F(
  PakBuilderTest, FullBuildWithManifestEmitsPatchManifestAndTelemetry)
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

  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();
  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  EXPECT_TRUE(result.patch_manifest.has_value());
  EXPECT_TRUE(result.telemetry.manifest_duration.has_value());
}

NOLINT_TEST_F(
  PakBuilderTest, FullBuildReturnsOutputCatalogEntriesForEmittedAssets)
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
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const paktest::AssetSpec>(&asset, 1U),
    std::span<const paktest::FileSpec> {}, static_cast<uint8_t>(0x31U)));

  const PakBuildRequest request {
    .mode = BuildMode::kFull,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source, }, },
    .output_pak_path = Path("full_catalog_output.pak"),
    .output_manifest_path = {},
    .content_version = 7,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = {},

    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();
  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  ASSERT_EQ(result.output_catalog.entries.size(), 1U);
  EXPECT_EQ(result.output_catalog.source_key, request.source_key);
  EXPECT_EQ(result.output_catalog.content_version, request.content_version);
  EXPECT_EQ(result.output_catalog.entries.at(0).asset_key, asset.key);
  EXPECT_EQ(result.output_catalog.entries.at(0).asset_type, asset.asset_type);
  EXPECT_EQ(result.output_catalog.entries.at(0).descriptor_digest,
    asset.descriptor_sha);
  const auto expected_transitive_digest = asset.descriptor_sha;
  EXPECT_EQ(result.output_catalog.entries.at(0).transitive_resource_digest,
    expected_transitive_digest);
  EXPECT_NE(
    result.output_catalog.catalog_digest, data::PakCatalog {}.catalog_digest);
}

NOLINT_TEST_F(
  PakBuilderTest, PatchBuildReturnsOutputCatalogEntriesForEmittedPatchAssets)
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
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const paktest::AssetSpec>(&asset, 1U),
    std::span<const paktest::FileSpec> {}, static_cast<uint8_t>(0x41U)));

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

  base_catalog.catalog_digest = base_catalog.ComputeDigest().value();

  const PakBuildRequest request {
    .mode = BuildMode::kPatch,
    .sources = { CookedSource {
      .kind = CookedSourceKind::kLooseCooked, .path = source, }, },
    .output_pak_path = Path("patch_catalog_output.pak"),
    .output_manifest_path = Path("patch_catalog_output.manifest"),
    .content_version = 5U,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = { base_catalog },

    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();
  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  EXPECT_EQ(result.summary.patch_replaced, 1U);
  ASSERT_EQ(result.output_catalog.entries.size(), 1U);
  EXPECT_EQ(result.output_catalog.source_key, request.source_key);
  EXPECT_EQ(result.output_catalog.content_version, request.content_version);
  EXPECT_EQ(result.output_catalog.entries.at(0).asset_key, asset.key);
  EXPECT_EQ(result.output_catalog.entries.at(0).asset_type, asset.asset_type);
  EXPECT_EQ(result.output_catalog.entries.at(0).descriptor_digest,
    asset.descriptor_sha);
  const auto expected_transitive_digest = asset.descriptor_sha;
  EXPECT_EQ(result.output_catalog.entries.at(0).transitive_resource_digest,
    expected_transitive_digest);
  EXPECT_NE(
    result.output_catalog.catalog_digest, data::PakCatalog {}.catalog_digest);
  EXPECT_TRUE(result.patch_manifest.has_value());
  EXPECT_TRUE(result.telemetry.manifest_duration.has_value());
}

NOLINT_TEST_F(
  PakBuilderTest, CurrentPakInputBuildsWithoutWarningsUnderStrictPolicy)
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

    .options = {},
  };

  PakBuilder builder;
  const auto seed_result_or_error = builder.Build(seed_request);
  ASSERT_HAS_VALUE(seed_result_or_error);
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

    .options = {
      .deterministic = true,
      .embed_browse_index = false,
      .emit_manifest_in_full = false,
      .compute_crc32 = true,
      .fail_on_warnings = true,
    },
  };

  const auto result_or_error = builder.Build(request);
  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();

  EXPECT_EQ(result.summary.diagnostics_warning, 0U);
  EXPECT_EQ(result.summary.diagnostics_error, 0U);
  EXPECT_FALSE(
    HasDiagnosticCode(result.diagnostics, "pak.request.fail_on_warnings"));
  EXPECT_TRUE(result.telemetry.planning_duration.has_value());
  EXPECT_TRUE(result.telemetry.writing_duration.has_value());
}

NOLINT_TEST_F(PakBuilderTest, PlannerRejectsBaseCatalogTypeMismatch)
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
  base_a.catalog_digest = base_a.ComputeDigest().value();

  data::PakCatalog base_b {};
  base_b.source_key = MakeSourceKey(kBaseSourceKeyBSeed);
  base_b.content_version = 1;
  base_b.entries = { entry_b };
  base_b.catalog_digest = base_b.ComputeDigest().value();

  const PakBuildRequest request {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = Path("type_mismatch_output.pak"),
    .output_manifest_path = Path("type_mismatch_output.manifest"),
    .content_version = 1,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = { base_a, base_b },

    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();
  EXPECT_TRUE(HasDiagnosticCode(
    result.diagnostics, "pak.plan.base_catalog_type_mismatch"));
}

NOLINT_TEST_F(PakBuilderTest, PatchModeClassifiesMissingSourceAsDelete)
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

  base_catalog.catalog_digest = base_catalog.ComputeDigest().value();

  const PakBuildRequest request {
    .mode = BuildMode::kPatch,
    .sources = {},
    .output_pak_path = Path("patch_classification_output.pak"),
    .output_manifest_path = Path("patch_classification_output.manifest"),
    .content_version = 1,
    .source_key = MakeNonZeroSourceKey(),
    .base_catalogs = { base_catalog },

    .options = {},
  };

  PakBuilder builder;
  const auto result_or_error = builder.Build(request);

  ASSERT_HAS_VALUE(result_or_error);
  const auto& result = result_or_error.value();
  EXPECT_EQ(result.summary.patch_created, 0U);
  EXPECT_EQ(result.summary.patch_replaced, 0U);
  EXPECT_EQ(result.summary.patch_deleted, expected_deleted);
  EXPECT_EQ(result.summary.patch_unchanged, 0U);
  EXPECT_EQ(result.summary.diagnostics_error, 0U);
}

} // namespace
