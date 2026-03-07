//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using nlohmann::json_schema::error_handler;
using nlohmann::json_schema::json_validator;

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

auto SchemaFile(const std::filesystem::path& repo_root) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Tools" / "PakTool"
    / "Schemas" / "oxygen.pak-build-report.schema.json";
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
    [[maybe_unused]] auto _ = validator.validate(instance, handler);
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

NOLINT_TEST(PakBuildReportJsonSchemaTest, AcceptsCanonicalPatchReportDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "$schema": "https://oxygen-engine.dev/schemas/oxygen.pak-build-report.schema.json",
    "schema_version": 1,
    "tool_name": "Oxygen.Cooker.PakTool",
    "tool_version": "0.1.0",
    "command": "patch",
    "command_line": "Oxygen.Cooker.PakTool patch --out game_patch.pak --catalog-out game_patch.catalog.json --manifest-out game_patch.manifest.json",
    "request": {
      "mode": "patch",
      "source_key": "01234567-89ab-7def-8123-456789abcdef",
      "content_version": 42,
      "sources": [
        {
          "kind": "loose_cooked",
          "path": "F:/Cooked/Base"
        },
        {
          "kind": "pak",
          "path": "F:/Cooked/Dlc/base.pak"
        }
      ],
      "base_catalogs": [
        "F:/Cooked/Base/base.catalog.json"
      ],
      "options": {
        "deterministic": true,
        "embed_browse_index": true,
        "compute_crc32": true,
        "fail_on_warnings": false,
        "emit_manifest_in_full": false
      },
      "patch_compatibility": {
        "require_exact_base_set": true,
        "require_content_version_match": true,
        "require_base_source_key_match": true,
        "require_catalog_digest_match": true
      }
    },
    "artifacts": {
      "pak": {
        "final_path": "F:/Build/game_patch.pak",
        "staged_path": "F:/Build/game_patch.pak.tmp",
        "published": true,
        "size_bytes": 4096,
        "crc32": 305419896
      },
      "catalog": {
        "final_path": "F:/Build/game_patch.catalog.json",
        "staged_path": "F:/Build/game_patch.catalog.json.tmp",
        "published": true,
        "catalog_digest": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
      },
      "manifest": {
        "requested": true,
        "emitted": true,
        "final_path": "F:/Build/game_patch.manifest.json",
        "staged_path": "F:/Build/game_patch.manifest.json.tmp",
        "published": true
      },
      "report": {
        "final_path": "F:/Build/game_patch.report.json",
        "staged_path": "F:/Build/game_patch.report.json.tmp",
        "published": true
      }
    },
    "summary": {
      "diagnostics_info": 1,
      "diagnostics_warning": 0,
      "diagnostics_error": 0,
      "assets_processed": 4,
      "resources_processed": 7,
      "patch_created": 1,
      "patch_replaced": 2,
      "patch_deleted": 1,
      "patch_unchanged": 0,
      "crc_computed": true
    },
    "telemetry": {
      "time_ms_planning": 1.25,
      "time_ms_writing": 2.5,
      "time_ms_manifest": 0.75,
      "time_ms_publish": 0.5,
      "time_ms_total": 5.0
    },
    "diagnostics": [
      {
        "severity": "info",
        "phase": "planning",
        "code": "pak.plan.masked_source",
        "message": "Masked source recorded for collision diagnostics.",
        "asset_key": "11111111-2222-3333-4444-555555555555",
        "path": "F:/Cooked/Base/.cooked/Scene.bin",
        "offset": 128
      }
    ],
    "exit_code": 0,
    "success": true
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(PakBuildReportJsonSchemaTest,
  AcceptsFailureReportWithSuppressedManifestPublication)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "schema_version": 1,
    "tool_name": "Oxygen.Cooker.PakTool",
    "tool_version": "0.1.0",
    "command": "build",
    "command_line": "Oxygen.Cooker.PakTool build --out game_full.pak --catalog-out game_full.catalog.json --diagnostics-file game_full.report.json",
    "request": {
      "mode": "full",
      "source_key": "01234567-89ab-7def-8123-456789abcdef",
      "content_version": 7,
      "sources": [],
      "base_catalogs": [],
      "options": {
        "deterministic": true,
        "embed_browse_index": false,
        "compute_crc32": false,
        "fail_on_warnings": true,
        "emit_manifest_in_full": false
      },
      "patch_compatibility": {
        "require_exact_base_set": true,
        "require_content_version_match": true,
        "require_base_source_key_match": true,
        "require_catalog_digest_match": true
      }
    },
    "artifacts": {
      "pak": {
        "final_path": "F:/Build/game_full.pak",
        "staged_path": "F:/Build/game_full.pak.tmp",
        "published": false,
        "size_bytes": null,
        "crc32": null
      },
      "catalog": {
        "final_path": "F:/Build/game_full.catalog.json",
        "staged_path": "F:/Build/game_full.catalog.json.tmp",
        "published": false,
        "catalog_digest": null
      },
      "manifest": {
        "requested": false,
        "emitted": false,
        "final_path": null,
        "staged_path": null,
        "published": false
      },
      "report": {
        "final_path": "F:/Build/game_full.report.json",
        "staged_path": null,
        "published": true
      }
    },
    "summary": {
      "diagnostics_info": 0,
      "diagnostics_warning": 1,
      "diagnostics_error": 1,
      "assets_processed": 0,
      "resources_processed": 0,
      "patch_created": 0,
      "patch_replaced": 0,
      "patch_deleted": 0,
      "patch_unchanged": 0,
      "crc_computed": false
    },
    "telemetry": {
      "time_ms_planning": null,
      "time_ms_writing": null,
      "time_ms_manifest": null,
      "time_ms_publish": 0.1,
      "time_ms_total": 0.1
    },
    "diagnostics": [
      {
        "severity": "warning",
        "phase": "planning",
        "code": "pak.plan.empty_build",
        "message": "Build produced no payloads."
      },
      {
        "severity": "error",
        "phase": "finalize",
        "code": "pak.request.fail_on_warnings",
        "message": "fail_on_warnings=true and at least one warning was emitted."
      }
    ],
    "exit_code": 3,
    "success": false
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(PakBuildReportJsonSchemaTest,
  RejectsNonCanonicalRequestFieldsAndUnexpectedProperties)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "schema_version": 1,
    "tool_name": "Oxygen.Cooker.PakTool",
    "tool_version": "0.1.0",
    "command": "patch",
    "command_line": "paktool patch",
    "request": {
      "mode": "patch",
      "source_key": "01234567-89ab-6def-8123-456789abcdef",
      "content_version": 70000,
      "sources": [
        {
          "kind": "archive",
          "path": "F:/Cooked/Base"
        }
      ],
      "base_catalogs": [],
      "options": {
        "deterministic": true,
        "embed_browse_index": false,
        "compute_crc32": true,
        "fail_on_warnings": false,
        "emit_manifest_in_full": false
      },
      "patch_compatibility": {
        "require_exact_base_set": true,
        "require_content_version_match": true,
        "require_base_source_key_match": true,
        "require_catalog_digest_match": true
      }
    },
    "artifacts": {
      "pak": {
        "final_path": "F:/Build/out.pak",
        "staged_path": "F:/Build/out.pak.tmp",
        "published": true,
        "size_bytes": 1,
        "crc32": 1
      },
      "catalog": {
        "final_path": "F:/Build/out.catalog.json",
        "staged_path": "F:/Build/out.catalog.json.tmp",
        "published": true,
        "catalog_digest": "XYZ"
      },
      "manifest": {
        "requested": true,
        "emitted": true,
        "final_path": "F:/Build/out.manifest.json",
        "staged_path": "F:/Build/out.manifest.json.tmp",
        "published": true
      },
      "report": {
        "final_path": "F:/Build/out.report.json",
        "staged_path": "F:/Build/out.report.json.tmp",
        "published": true,
        "unexpected": true
      }
    },
    "summary": {
      "diagnostics_info": 0,
      "diagnostics_warning": 0,
      "diagnostics_error": 0,
      "assets_processed": 0,
      "resources_processed": 0,
      "patch_created": 0,
      "patch_replaced": 0,
      "patch_deleted": 0,
      "patch_unchanged": 0,
      "crc_computed": true
    },
    "telemetry": {
      "time_ms_planning": 1.0,
      "time_ms_writing": 2.0,
      "time_ms_manifest": 3.0,
      "time_ms_publish": 4.0,
      "time_ms_total": 5.0
    },
    "diagnostics": [],
    "exit_code": 0,
    "success": true
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
