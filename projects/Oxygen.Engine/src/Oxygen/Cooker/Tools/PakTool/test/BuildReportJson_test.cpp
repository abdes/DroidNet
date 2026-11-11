//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakTool/BuildReportJson.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>

#include "RequestSnapshot.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Pak/PakBuildPhase.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuildResult.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/Tools/PakTool/BuildReportJson.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::cooked::PathKind;
using oxygen::content::pak::BuildMode;
using oxygen::content::pak::PakBuildPhase;
using oxygen::content::pak::PakBuildRequest;
using oxygen::content::pak::PakBuildResult;
using oxygen::content::pak::PakBuildSummary;
using oxygen::content::pak::PakBuildTelemetry;
using oxygen::content::pak::PakDiagnostic;
using oxygen::content::pak::PakDiagnosticSeverity;
using oxygen::content::pak::tool::ArtifactPublicationIntent;
using oxygen::content::pak::tool::MakeArtifactPublicationPlan;
using oxygen::content::pak::tool::PakToolBuildReportInput;
using oxygen::content::pak::tool::PakToolCommandError;
using oxygen::content::pak::tool::PakToolRequestSnapshot;
using oxygen::content::pak::tool::PublishArtifacts;
using oxygen::content::pak::tool::ToCanonicalJsonString;
using oxygen::content::pak::tool::WriteReportFile;
using oxygen::cooker::testing::MemoryFileSystem;

class PakToolBuildReportJsonTest : public ::testing::Test {
protected:
  //! A root no test creates: the files live in `storage_`.
  [[nodiscard]] static auto Root() -> std::filesystem::path
  {
    return "C:/PakToolReports";
  }

  [[nodiscard]] static auto Schema() -> const json&
  {
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Tools/PakTool/Schemas/oxygen.pak-build-report.schema.json");
    return schema;
  }

  void SetUp() override { MakeSuccessfulInput(); }

  //! The report input of a patch build that published its pak, catalog and
  //! manifest, into `input_`.
  auto MakeSuccessfulInput() -> void
  {
    const auto source_key = oxygen::data::SourceKey::FromString(
      "01234567-89ab-7def-8123-456789abcdef");
    ASSERT_HAS_VALUE(source_key);
    const auto asset_key = oxygen::data::AssetKey::FromString(
      "11111111-2222-3333-4444-555555555555");
    ASSERT_HAS_VALUE(asset_key);

    auto request = PakBuildRequest {};
    request.mode = BuildMode::kPatch;
    request.sources = {
      oxygen::data::CookedSource {
        .kind = oxygen::data::CookedSourceKind::kLooseCooked,
        .path = Root() / "cook" / "base",
      },
      oxygen::data::CookedSource {
        .kind = oxygen::data::CookedSourceKind::kPak,
        .path = Root() / "cook" / "base.pak",
      },
    };
    request.content_version = 42;
    request.source_key = *source_key;
    request.output_pak_path = Root() / "release" / "game.pak";
    request.output_manifest_path = Root() / "release" / "game.manifest.json";

    const auto plan = MakeArtifactPublicationPlan({
      .pak = request.output_pak_path,
      .catalog = Root() / "release" / "game.catalog.json",
      .manifest = request.output_manifest_path,
      .report = Root() / "release" / "game.report.json",
    });
    ASSERT_HAS_VALUE(plan.manifest);

    storage_.AddFile(plan.pak.staged_path, "pak-data");
    storage_.AddFile(plan.catalog.staged_path, "catalog-data");
    storage_.AddFile(plan.manifest->staged_path, "manifest-data");

    auto intent = ArtifactPublicationIntent {
      .publish_pak = true,
      .publish_catalog = true,
      .publish_manifest = true,
    };
    auto publication_result = PublishArtifacts(plan, intent, storage_);
    EXPECT_TRUE(publication_result.success);

    auto build_result = PakBuildResult {};
    build_result.output_catalog = oxygen::data::PakCatalog {
      .source_key = request.source_key,
      .content_version = request.content_version,
      .catalog_digest = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
      },
      .mount = "Content",
      .entries = {
        oxygen::data::PakCatalogEntry {
          .asset_key = *asset_key,
          .asset_type = oxygen::data::AssetType::kMaterial,
          .descriptor_digest = {
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
            0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
          },
          .transitive_resource_digest = {
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
            0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
          },
        },
      },
    };
    build_result.file_size = 4096;
    build_result.pak_crc32 = 0x12345678;
    build_result.patch_manifest = oxygen::data::PatchManifest {};
    build_result.diagnostics.push_back(PakDiagnostic {
      .severity = PakDiagnosticSeverity::kInfo,
      .phase = PakBuildPhase::kPlanning,
      .code = "pak.plan.masked_source",
      .message = "Masked source recorded for collision diagnostics.",
      .asset_key = "11111111-2222-3333-4444-555555555555",
      .resource_kind = "Descriptor",
      .table_name = "assets",
      .path = Root() / "cook" / "base" / ".cooked" / "Scene.bin",
      .offset = 128,
    });
    build_result.summary = PakBuildSummary {
      .diagnostics_info = 1,
      .diagnostics_warning = 0,
      .diagnostics_error = 0,
      .assets_processed = 4,
      .resources_processed = 7,
      .patch_created = 1,
      .patch_replaced = 2,
      .patch_deleted = 1,
      .patch_unchanged = 0,
      .patch_deleted_textures = 0,
      .crc_computed = true,
    };
    build_result.telemetry = PakBuildTelemetry {
      .planning_duration = std::chrono::microseconds { 1250 },
      .writing_duration = std::chrono::microseconds { 2500 },
      .manifest_duration = std::chrono::microseconds { 750 },
      .total_duration = std::chrono::microseconds { 5000 },
    };

    input_ = PakToolBuildReportInput {
      .tool_version = "0.1.0",
      .command = "patch",
      .command_line
      = "Oxygen.Cooker.PakTool patch --out game.pak --catalog-out game.catalog.json --manifest-out game.manifest.json --diagnostics-file game.report.json",
      .request_snapshot = PakToolRequestSnapshot {
        .request = request,
        .base_pak_paths = { Root() / "cook" / "base.catalog.json" },
      },
      .publication_plan = plan,
      .publication_result = publication_result,
      .build_result = build_result,
      .exit_code = 0,
      .success = true,
    };
  }

  //! Holds the staged artifacts and the reports the tests publish.
  MemoryFileSystem storage_;
  //! The report input of a successful patch build.
  PakToolBuildReportInput input_ {};
};

NOLINT_TEST_F(PakToolBuildReportJsonTest,
  CanonicalReportJsonIsDeterministicAndMatchesSchema)
{
  const auto& input = input_;

  const auto first = ToCanonicalJsonString(input);
  const auto second = ToCanonicalJsonString(input);

  EXPECT_EQ(first, second);

  auto parsed = json::parse(first);
  EXPECT_THAT(
    oxygen::cooker::test::ValidateJson(Schema(), parsed), ::testing::IsEmpty());
}

//! The report is published through the storage, as the schema describes it.
NOLINT_TEST_F(PakToolBuildReportJsonTest,
  WriteReportFilePublishesFailureReportThatMatchesSchema)
{
  auto input = input_;
  input.success = false;
  input.exit_code = 3;
  input.request_snapshot.request.options.compute_crc32 = false;
  input.build_result.summary.diagnostics_warning = 1;
  input.build_result.summary.diagnostics_error = 1;
  input.build_result.telemetry.writing_duration.reset();
  input.build_result.telemetry.manifest_duration.reset();
  input.build_result.pak_crc32 = 0;
  input.build_result.file_size = 0;
  input.build_result.patch_manifest.reset();
  ASSERT_HAS_VALUE(input.publication_result.manifest);
  input.publication_result.pak.published = false;
  input.publication_result.catalog.published = false;
  input.publication_result.manifest->published = false;
  input.publication_result.manifest->publish_requested = false;
  input.build_result.diagnostics.push_back(PakDiagnostic {
    .severity = PakDiagnosticSeverity::kError,
    .phase = PakBuildPhase::kFinalize,
    .code = "pak.request.fail_on_warnings",
    .message = "fail_on_warnings=true and at least one warning was emitted.",
  });

  const auto output_path = Root() / "reports" / "failure.report.json";
  const auto write_result = WriteReportFile(storage_, output_path, input);

  ASSERT_TRUE(write_result.success)
    << write_result.error_code << ": " << write_result.error_message;
  ASSERT_EQ(storage_.KindOf(output_path), PathKind::kFile);
  auto text = std::string {};
  std::ranges::transform(storage_.Bytes(output_path), std::back_inserter(text),
    [](const std::byte b) -> char { return static_cast<char>(b); });
  EXPECT_EQ(text, ToCanonicalJsonString(input));
  auto parsed = json::parse(text);
  EXPECT_THAT(
    oxygen::cooker::test::ValidateJson(Schema(), parsed), ::testing::IsEmpty());
  EXPECT_EQ(parsed.at("exit_code"), 3);
  EXPECT_FALSE(parsed.at("success"));
}

//! A publication that fails and cannot roll back says what each final file
//! holds, why it failed and which rollback step failed.
NOLINT_TEST_F(PakToolBuildReportJsonTest,
  ReportOfFailedPublicationStatesFinalFilesErrorAndIssues)
{
  auto input = input_;
  const auto& plan = input.publication_plan;
  storage_.AddFile(plan.pak.staged_path, "pak-new");
  storage_.AddFile(plan.catalog.staged_path, "catalog-new");
  storage_.FailMoving(plan.catalog.staged_path);
  storage_.FailMoving(plan.pak.backup_path);
  input.publication_result = PublishArtifacts(plan,
    ArtifactPublicationIntent {
      .publish_pak = true,
      .publish_catalog = true,
      .publish_manifest = false,
    },
    storage_);
  ASSERT_FALSE(input.publication_result.success);
  input.error = PakToolCommandError {
    .code = input.publication_result.error_code,
    .message = input.publication_result.error_message,
    .path = {},
  };
  input.exit_code = 4;
  input.success = false;

  const auto parsed = json::parse(ToCanonicalJsonString(input));

  EXPECT_THAT(
    oxygen::cooker::test::ValidateJson(Schema(), parsed), ::testing::IsEmpty());
  // The new pak stays: its backup could not be put back.
  EXPECT_TRUE(parsed.at("artifacts").at("pak").at("published"));
  EXPECT_FALSE(parsed.at("artifacts").at("catalog").at("published"));
  EXPECT_EQ(parsed.at("error").at("code"), "paktool.publish.rename_failed");
  EXPECT_TRUE(parsed.at("error").at("path").is_null());
  ASSERT_EQ(parsed.at("publication_issues").size(), 1U);
  EXPECT_EQ(parsed.at("publication_issues").at(0).at("code"),
    "paktool.publish.restore_failed");
}

//! A report that cannot be published says so with its own code.
NOLINT_TEST_F(
  PakToolBuildReportJsonTest, WriteReportFileReportsUnpublishableDestination)
{
  const auto& input = input_;
  const auto output_path = Root() / "reports" / "locked.report.json";
  storage_.FailPublishing(output_path);

  const auto write_result = WriteReportFile(storage_, output_path, input);

  EXPECT_FALSE(write_result.success);
  EXPECT_EQ(write_result.error_code, "paktool.report.write_open_failed");
  EXPECT_EQ(storage_.KindOf(output_path), PathKind::kNone);
}

} // namespace
