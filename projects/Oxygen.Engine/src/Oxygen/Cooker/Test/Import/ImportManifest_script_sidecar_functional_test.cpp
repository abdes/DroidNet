//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportManifest;
using oxygen::content::import::ScriptingImportKind;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

auto JsonPath(const std::filesystem::path& path) -> std::string
{
  return path.lexically_normal().generic_string();
}

NOLINT_TEST(ImportManifestScriptSidecarTest, AcceptsInlineBindingsForSidecarJob)
{
  const ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "oxygen_manifest_sidecar_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path
    = temp.Path() / "inline_bindings" / "import_manifest.json";

  WriteText(manifest_path,
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
  EXPECT_TRUE(manifest->jobs.at(0).scripting_sidecar.source_path.empty());
  EXPECT_FALSE(
    manifest->jobs.at(0).scripting_sidecar.inline_bindings_json.empty());

  std::ostringstream request_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  EXPECT_EQ(request->options.scripting.import_kind,
    ScriptingImportKind::kScriptingSidecar);
  EXPECT_FALSE(request->options.scripting.inline_bindings_json.empty());
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  ScriptAuthoringRootsResolveDefaultsAndOverridesFromManifestDirectory)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "explicit_script_roots" / "import_manifest.json";
  const auto output = manifest_path.parent_path() / "generated" / "cooked";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + JsonPath(output) + R"(",
      "defaults": {
        "script": {
          "script_storage": "external",
          "compile": false,
          "source_root": "Authoring"
        }
      },
      "jobs": [
        { "type": "script", "source": "Authoring/main.lua" },
        { "type": "script", "source": "Other/main.lua", "source_root": "Other" }
      ]
    })");
  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  for (const auto& job : manifest->jobs) {
    std::ostringstream request_errors;
    const auto request = job.BuildRequest(request_errors);
    ASSERT_TRUE(request.has_value()) << request_errors.str();
    EXPECT_EQ(request->options.scripting.source_root,
      request->source_path.parent_path());
    EXPECT_NE(request->options.scripting.source_root, output.parent_path());
  }
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  RejectsScriptSidecarJobWhenSourceAndBindingsBothSpecified)
{
  const ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "oxygen_manifest_sidecar_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path
    = temp.Path() / "both_source_and_bindings" / "import_manifest.json";

  WriteText(manifest_path,
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
  const ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "oxygen_manifest_script_root";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path
    = temp.Path() / "bindings_non_sidecar" / "import_manifest.json";

  WriteText(manifest_path,
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
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("job.bindings is only valid for type "
                         "'script-sidecar'"));
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  TopLevelOutputAppliesToScriptAndSidecarJobsWhenNoOverridesExist)
{
  const ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "oxygen_manifest_global_output";
  const auto cooked_root_json = JsonPath(cooked_root);
  const auto manifest_path
    = temp.Path() / "top_level_output_fallback" / "import_manifest.json";

  WriteText(manifest_path,
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
  const auto request_0 = manifest->jobs.at(0).BuildRequest(request_errors_0);
  ASSERT_TRUE(request_0.has_value()) << request_errors_0.str();
  ASSERT_TRUE(request_0->cooked_root.has_value())
    << "Expected cooked root to be present";
  EXPECT_EQ(
    request_0->cooked_root->lexically_normal(), cooked_root.lexically_normal());

  std::ostringstream request_errors_1;
  const auto request_1 = manifest->jobs.at(1).BuildRequest(request_errors_1);
  ASSERT_TRUE(request_1.has_value()) << request_errors_1.str();
  ASSERT_TRUE(request_1->cooked_root.has_value())
    << "Expected cooked root to be present";
  EXPECT_EQ(
    request_1->cooked_root->lexically_normal(), cooked_root.lexically_normal());
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  DefaultsOutputOverridesTopLevelOutputForSidecarJobs)
{
  const ScopedTempDir temp;
  const auto top_level_output = temp.Path() / "oxygen_manifest_output_top";
  const auto defaults_output = temp.Path() / "oxygen_manifest_output_defaults";
  const auto manifest_path
    = temp.Path() / "defaults_over_top_level_output" / "import_manifest.json";

  WriteText(manifest_path,
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
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value())
    << "Expected cooked root to be present";
  EXPECT_EQ(request->cooked_root->lexically_normal(),
    defaults_output.lexically_normal());
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  JobOutputOverridesDefaultsAndTopLevelForSidecarJobs)
{
  const ScopedTempDir temp;
  const auto top_level_output = temp.Path() / "oxygen_manifest_output_top2";
  const auto defaults_output = temp.Path() / "oxygen_manifest_output_defaults2";
  const auto job_output = temp.Path() / "oxygen_manifest_output_job2";
  const auto manifest_path
    = temp.Path() / "job_overrides_defaults_output" / "import_manifest.json";

  WriteText(manifest_path,
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
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value())
    << "Expected cooked root to be present";
  EXPECT_EQ(
    request->cooked_root->lexically_normal(), job_output.lexically_normal());
}

} // namespace
