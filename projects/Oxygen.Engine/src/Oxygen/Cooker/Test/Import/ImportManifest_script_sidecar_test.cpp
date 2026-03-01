//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>

namespace {

using oxygen::content::import::ImportManifest;
using oxygen::content::import::ScriptingImportKind;

auto JsonPath(const std::filesystem::path& path) -> std::string
{
  return path.lexically_normal().generic_string();
}

auto MakeManifestPath(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path() / "oxygen_manifest_script";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir / "import_manifest.json";
}

auto WriteManifestFile(
  const std::filesystem::path& path, const std::string& json_text) -> void
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << json_text;
}

NOLINT_TEST(ImportManifestScriptSidecarTest, AcceptsInlineBindingsForSidecarJob)
{
  const auto cooked_root
    = std::filesystem::temp_directory_path() / "oxygen_manifest_sidecar_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path = MakeManifestPath("inline_bindings");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "script-sidecar",
          "output": ")"
      + cooked_root_json + R"(",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "main",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);
  EXPECT_TRUE(manifest->jobs[0].scripting_sidecar.source_path.empty());
  EXPECT_FALSE(
    manifest->jobs[0].scripting_sidecar.inline_bindings_json.empty());

  std::ostringstream request_errors;
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  EXPECT_EQ(request->options.scripting.import_kind,
    ScriptingImportKind::kScriptingSidecar);
  EXPECT_FALSE(request->options.scripting.inline_bindings_json.empty());
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  RejectsScriptSidecarJobWhenSourceAndBindingsBothSpecified)
{
  const auto cooked_root
    = std::filesystem::temp_directory_path() / "oxygen_manifest_sidecar_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path = MakeManifestPath("both_source_and_bindings");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "script-sidecar",
          "source": "scene.sidecar.json",
          "output": ")"
      + cooked_root_json + R"(",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "main",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_FALSE(errors.str().empty());
}

NOLINT_TEST(
  ImportManifestScriptSidecarTest, RejectsBindingsFieldForNonSidecarJobs)
{
  const auto cooked_root
    = std::filesystem::temp_directory_path() / "oxygen_manifest_script_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path = MakeManifestPath("bindings_non_sidecar");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "script",
          "source": "main.luau",
          "output": ")"
      + cooked_root_json + R"(",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "main",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_TRUE(errors.str().find("job.bindings is only valid for type "
                                "'script-sidecar'")
    != std::string::npos);
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  TopLevelOutputAppliesToScriptAndSidecarJobsWhenNoOverridesExist)
{
  const auto cooked_root
    = std::filesystem::temp_directory_path() / "oxygen_manifest_global_output";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path = MakeManifestPath("top_level_output_fallback");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + cooked_root_json + R"(",
      "jobs": [
        {
          "type": "script",
          "source": "main.luau"
        },
        {
          "type": "script-sidecar",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "main",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);

  std::ostringstream request_errors_0;
  const auto request_0 = manifest->jobs[0].BuildRequest(request_errors_0);
  ASSERT_TRUE(request_0.has_value()) << request_errors_0.str();
  ASSERT_TRUE(request_0->cooked_root.has_value());
  EXPECT_EQ(
    request_0->cooked_root->lexically_normal(), cooked_root.lexically_normal());

  std::ostringstream request_errors_1;
  const auto request_1 = manifest->jobs[1].BuildRequest(request_errors_1);
  ASSERT_TRUE(request_1.has_value()) << request_errors_1.str();
  ASSERT_TRUE(request_1->cooked_root.has_value());
  EXPECT_EQ(
    request_1->cooked_root->lexically_normal(), cooked_root.lexically_normal());
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  DefaultsOutputOverridesTopLevelOutputForSidecarJobs)
{
  const auto top_level_output
    = std::filesystem::temp_directory_path() / "oxygen_manifest_output_top";
  const auto defaults_output = std::filesystem::temp_directory_path()
    / "oxygen_manifest_output_defaults";
  const auto manifest_path = MakeManifestPath("defaults_over_top_level_output");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + JsonPath(top_level_output) + R"(",
      "defaults": {
        "scripting_sidecar": {
          "output": ")"
      + JsonPath(defaults_output) + R"("
        }
      },
      "jobs": [
        {
          "type": "script-sidecar",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "main",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream request_errors;
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(request->cooked_root->lexically_normal(),
    defaults_output.lexically_normal());
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  JobOutputOverridesDefaultsAndTopLevelForSidecarJobs)
{
  const auto top_level_output
    = std::filesystem::temp_directory_path() / "oxygen_manifest_output_top2";
  const auto defaults_output = std::filesystem::temp_directory_path()
    / "oxygen_manifest_output_defaults2";
  const auto job_output
    = std::filesystem::temp_directory_path() / "oxygen_manifest_output_job2";
  const auto manifest_path = MakeManifestPath("job_overrides_defaults_output");

  WriteManifestFile(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + JsonPath(top_level_output) + R"(",
      "defaults": {
        "scripting_sidecar": {
          "output": ")"
      + JsonPath(defaults_output) + R"("
        }
      },
      "jobs": [
        {
          "type": "script-sidecar",
          "output": ")"
      + JsonPath(job_output) + R"(",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "main",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::ostringstream request_errors;
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(
    request->cooked_root->lexically_normal(), job_output.lexically_normal());
}

} // namespace
