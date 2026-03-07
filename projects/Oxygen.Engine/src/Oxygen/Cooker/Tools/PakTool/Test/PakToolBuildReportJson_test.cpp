//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <process.h>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/Tools/PakTool/BuildReportJson.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using nlohmann::json_schema::error_handler;
using nlohmann::json_schema::json_validator;
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
using oxygen::content::pak::tool::PakToolRequestSnapshot;
using oxygen::content::pak::tool::PublishArtifacts;
using oxygen::content::pak::tool::RealArtifactFileSystem;
using oxygen::content::pak::tool::ToCanonicalJsonString;
using oxygen::content::pak::tool::WriteReportFile;

class CollectingErrorHandler final : public error_handler {
public:
  void error(const json::json_pointer& ptr, const json& instance,
    const std::string& message) override
  {
    auto out = std::ostringstream {};
    const auto path = ptr.to_string();
    out << (path.empty() ? "<root>" : path) << ": " << message;
    if (!instance.is_discarded()) {
      out << " (value=" << instance.dump() << ")";
    }
    errors_.push_back(out.str());
  }

  [[nodiscard]] auto HasErrors() const noexcept -> bool
  {
    return !errors_.empty();
  }

  [[nodiscard]] auto ToString() const -> std::string
  {
    auto out = std::ostringstream {};
    for (const auto& error : errors_) {
      out << "- " << error << "\n";
    }
    return out.str();
  }

private:
  std::vector<std::string> errors_;
};

auto FindRepoRoot() -> std::filesystem::path
{
  auto path = std::filesystem::path(__FILE__).parent_path();
  while (!path.empty()) {
    if (std::filesystem::exists(path / "src" / "Oxygen" / "Cooker" / "Tools"
          / "PakTool" / "Schemas" / "oxygen.pak-build-report.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto LoadJsonFile(const std::filesystem::path& path) -> std::optional<json>
{
  auto in = std::ifstream(path);
  if (!in) {
    return std::nullopt;
  }
  try {
    auto parsed = json {};
    in >> parsed;
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

auto ValidateSchema(
  const json& schema, const json& instance, std::string& errors) -> bool
{
  try {
    auto validator = json_validator {};
    validator.set_root_schema(schema);
    auto handler = CollectingErrorHandler {};
    [[maybe_unused]] const auto _ = validator.validate(instance, handler);
    if (handler.HasErrors()) {
      errors = handler.ToString();
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    errors = ex.what();
    return false;
  }
}

class PakToolBuildReportJsonTest : public testing::Test {
protected:
  void SetUp() override
  {
    static auto counter = std::atomic_uint64_t { 0 };
    const auto pid = static_cast<unsigned long long>(_getpid());
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    root_ = std::filesystem::temp_directory_path() / "oxygen_paktool_report"
      / ("pid-" + std::to_string(pid) + "-case-" + std::to_string(id));
    std::filesystem::remove_all(root_);
    std::filesystem::create_directories(root_);
  }

  void TearDown() override
  {
    std::error_code ec {};
    std::filesystem::remove_all(root_, ec);
  }

  [[nodiscard]] auto Root() const -> const std::filesystem::path&
  {
    return root_;
  }

  static auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view content) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << path.string();
    out << content;
  }

  [[nodiscard]] static auto ReadTextFile(const std::filesystem::path& path)
    -> std::string
  {
    auto in = std::ifstream(path, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << path.string();
    return std::string(
      std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  [[nodiscard]] auto Schema() const -> json
  {
    const auto repo_root = FindRepoRoot();
    EXPECT_FALSE(repo_root.empty());
    const auto path = repo_root / "src" / "Oxygen" / "Cooker" / "Tools"
      / "PakTool" / "Schemas" / "oxygen.pak-build-report.schema.json";
    const auto schema = LoadJsonFile(path);
    EXPECT_TRUE(schema.has_value());
    return *schema;
  }

  [[nodiscard]] static auto MakeSourceKey() -> oxygen::data::SourceKey
  {
    const auto parsed = oxygen::data::SourceKey::FromString(
      "01234567-89ab-7def-8123-456789abcdef");
    EXPECT_TRUE(parsed.has_value());
    return parsed.value();
  }

  [[nodiscard]] static auto MakeAssetKey(const std::string_view text)
    -> oxygen::data::AssetKey
  {
    const auto parsed = oxygen::data::AssetKey::FromString(text);
    EXPECT_TRUE(parsed.has_value());
    return parsed.value();
  }

  [[nodiscard]] auto MakeSuccessfulInput() const -> PakToolBuildReportInput
  {
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
    request.source_key = MakeSourceKey();
    request.options.embed_browse_index = true;
    request.output_pak_path = Root() / "release" / "game.pak";
    request.output_manifest_path = Root() / "release" / "game.manifest.json";

    const auto plan = MakeArtifactPublicationPlan(request.output_pak_path,
      Root() / "release" / "game.catalog.json", request.output_manifest_path,
      Root() / "release" / "game.report.json");

    WriteTextFile(plan.pak.staged_path, "pak-data");
    WriteTextFile(plan.catalog.staged_path, "catalog-data");
    WriteTextFile(plan.manifest->staged_path, "manifest-data");
    WriteTextFile(plan.report->staged_path, "report-staged");

    auto intent = ArtifactPublicationIntent {
      .create_parent_directories = true,
      .publish_pak = true,
      .publish_catalog = true,
      .publish_manifest = true,
      .publish_report = true,
    };
    auto fs = RealArtifactFileSystem {};
    auto publication_result = PublishArtifacts(plan, intent, fs);
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
      .entries = {
        oxygen::data::PakCatalogEntry {
          .asset_key = MakeAssetKey("11111111-2222-3333-4444-555555555555"),
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
      .crc_computed = true,
    };
    build_result.telemetry = PakBuildTelemetry {
      .planning_duration = std::chrono::microseconds { 1250 },
      .writing_duration = std::chrono::microseconds { 2500 },
      .manifest_duration = std::chrono::microseconds { 750 },
      .total_duration = std::chrono::microseconds { 5000 },
    };

    return PakToolBuildReportInput {
      .tool_version = "0.1.0",
      .command = "patch",
      .command_line
      = "Oxygen.Cooker.PakTool patch --out game.pak --catalog-out game.catalog.json --manifest-out game.manifest.json --diagnostics-file game.report.json",
      .request_snapshot = PakToolRequestSnapshot {
        .request = request,
        .base_catalog_paths = { Root() / "cook" / "base.catalog.json" },
      },
      .publication_plan = plan,
      .publication_result = publication_result,
      .build_result = build_result,
      .exit_code = 0,
      .success = true,
    };
  }

private:
  std::filesystem::path root_;
};

NOLINT_TEST_F(PakToolBuildReportJsonTest,
  CanonicalReportJsonIsDeterministicAndMatchesSchema)
{
  const auto input = MakeSuccessfulInput();

  const auto first = ToCanonicalJsonString(input);
  const auto second = ToCanonicalJsonString(input);

  EXPECT_EQ(first, second);

  auto parsed = json::parse(first);
  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(Schema(), parsed, errors)) << errors;
}

NOLINT_TEST_F(PakToolBuildReportJsonTest,
  WriteReportFilePersistsFailureReportThatMatchesSchema)
{
  auto input = MakeSuccessfulInput();
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
  input.publication_result.pak.published = false;
  input.publication_result.catalog.published = false;
  input.publication_result.manifest->published = false;
  input.publication_result.manifest->publish_requested = false;
  input.publication_result.report->published = false;
  input.build_result.diagnostics.push_back(PakDiagnostic {
    .severity = PakDiagnosticSeverity::kError,
    .phase = PakBuildPhase::kFinalize,
    .code = "pak.request.fail_on_warnings",
    .message = "fail_on_warnings=true and at least one warning was emitted.",
  });

  const auto output_path = Root() / "reports" / "failure.report.json";
  const auto write_result = WriteReportFile(output_path, input);
  ASSERT_TRUE(write_result.success)
    << write_result.error_code << ": " << write_result.error_message;
  ASSERT_TRUE(std::filesystem::exists(output_path));

  auto parsed = json::parse(ReadTextFile(output_path));
  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(Schema(), parsed, errors)) << errors;
  EXPECT_EQ(parsed.at("exit_code"), 3);
  EXPECT_FALSE(parsed.at("success"));
}

} // namespace
