//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::cooker::test::HasDiagnostic;

using oxygen::content::import::ImportDiagnostic;
using oxygen::cooker::test::DiagnosticSummary;

using oxygen::content::import::ImportManifest;
using oxygen::content::import::ScriptingImportKind;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

NOLINT_TEST(ImportManifestScriptSidecarTest, AcceptsInlineBindingsForSidecarJob)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "inline_bindings" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "script-sidecar",
          "output": "oxygen_manifest_sidecar_root",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "00000000-0000-4000-8000-00000000000a",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);
  EXPECT_TRUE(manifest->jobs.at(0).scripting_sidecar.source_path.empty());
  EXPECT_FALSE(
    manifest->jobs.at(0).scripting_sidecar.inline_bindings_json.empty());

  std::vector<ImportDiagnostic> request_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  EXPECT_EQ(request->options.scripting.import_kind,
    ScriptingImportKind::kScriptingSidecar);
  EXPECT_FALSE(request->options.scripting.inline_bindings_json.empty());
}

//! A script sidecar job's instance bindings reach the sidecar beside its
//! bindings; a job with a source cannot hold them.
NOLINT_TEST(ImportManifestScriptSidecarTest, ForwardsInlineInstanceBindings)
{
  const ScopedTempDir temp;
  const auto job = [](const std::string& binding) {
    return R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "script-sidecar",
          "output": "oxygen_manifest_sidecar_root",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          )"
      + binding + R"(,
          "instance_bindings": [
            {
              "node_index": 0,
              "node_path": ["00000000-0000-4000-8000-00000000000b"],
              "slot_id": "00000000-0000-4000-8000-00000000000a",
              "params": [{ "key": "speed", "type": "float", "value": 2.5 }]
            }
          ]
        }
      ]
    })";
  };
  const auto inline_path = temp.Path() / "inline" / "import_manifest.json";
  const auto source_path = temp.Path() / "source" / "import_manifest.json";
  WriteText(inline_path, job(R"("bindings": [])"));
  WriteText(source_path, job(R"("source": "Scenes/TestScene.sidecar.json")"));

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(inline_path, {}, errors);
  std::vector<ImportDiagnostic> refused_errors;
  const auto refused = ImportManifest::Load(source_path, {}, refused_errors);

  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);
  const auto& sidecar
    = manifest->jobs.at(0).scripting_sidecar.inline_bindings_json;
  EXPECT_NE(sidecar.find("instance_bindings"), std::string::npos);
  EXPECT_NE(
    sidecar.find("00000000-0000-4000-8000-00000000000b"), std::string::npos);
  EXPECT_FALSE(refused.has_value());
}

//! Published roots resolve scripts and asset parameters cooked earlier.
NOLINT_TEST(ImportManifestScriptSidecarTest,
  SidecarCookedContextRootsResolveFromManifestDirectory)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "sidecar_context_roots" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "staging",
      "jobs": [
        {
          "type": "script-sidecar",
          "target_scene_virtual_path": "/Content/Scenes/Main.oscene",
          "cooked_context_roots": ["published"],
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "00000000-0000-4000-8000-00000000000a",
              "script_virtual_path": "/Content/move.oscript"
            }
          ]
        }
      ]
    })");

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  std::vector<ImportDiagnostic> request_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  EXPECT_THAT(request->cooked_context_roots,
    ::testing::ElementsAre(
      (manifest_path.parent_path() / "published").lexically_normal()));
}

//! An external script stores its source's mount path, so each script job's
//! request carries the mount path of its own mount.
NOLINT_TEST(ImportManifestScriptSidecarTest,
  ExternalScriptJobsRecordTheirSourceMountPaths)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "script_mounts" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [
        { "name": "Authoring", "path": "Authoring" },
        { "name": "Other", "path": "Other" }
      ],
      "output": "generated/cooked",
      "defaults": {
        "script": {
          "script_storage": "external",
          "compile": false
        }
      },
      "jobs": [
        {
          "type": "script",
          "source": "Authoring/main.lua",
          "virtual_path": "/Cooked/Scripts/AuthoringMain.oscript"
        },
        {
          "type": "script",
          "source": "Other/main.lua",
          "virtual_path": "/Cooked/Scripts/OtherMain.oscript"
        }
      ]
    })");
  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 2U);
  const auto expected = std::array {
    std::string_view { "/Authoring/main.lua" },
    std::string_view { "/Other/main.lua" },
  };
  for (std::size_t i = 0; i < expected.size(); ++i) {
    std::vector<ImportDiagnostic> request_errors;
    const auto request = manifest->jobs[i].BuildRequest(request_errors);
    ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
    EXPECT_EQ(request->source_mount_path, expected[i]);
  }
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  RejectsScriptSidecarJobWhenSourceAndBindingsBothSpecified)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "both_source_and_bindings" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "script-sidecar",
          "source": "scene.sidecar.json",
          "output": "oxygen_manifest_sidecar_root",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "00000000-0000-4000-8000-00000000000a",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_FALSE(errors.empty());
}

NOLINT_TEST(
  ImportManifestScriptSidecarTest, RejectsBindingsFieldForNonSidecarJobs)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "bindings_non_sidecar" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "jobs": [
        {
          "type": "script",
          "source": "main.luau",
          "virtual_path": "/Cooked/Scripts/Main.oscript",
          "output": "oxygen_manifest_script_root",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "00000000-0000-4000-8000-00000000000a",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(
      oxygen::content::import::manifest_codes::kBindingsNotAllowed));
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  TopLevelOutputAppliesToScriptAndSidecarJobsWhenNoOverridesExist)
{
  const ScopedTempDir temp;
  const auto cooked_root = temp.Path() / "top_level_output_fallback"
    / "oxygen_manifest_global_output";
  const auto manifest_path
    = temp.Path() / "top_level_output_fallback" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "oxygen_manifest_global_output",
      "jobs": [
        {
          "type": "script",
          "source": "main.luau",
          "virtual_path": "/Cooked/Scripts/Main.oscript"
        },
        {
          "type": "script-sidecar",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "00000000-0000-4000-8000-00000000000a",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 2U);

  std::vector<ImportDiagnostic> request_errors_0;
  const auto request_0 = manifest->jobs.at(0).BuildRequest(request_errors_0);
  ASSERT_HAS_VALUE(request_0) << DiagnosticSummary(request_errors_0);
  ASSERT_HAS_VALUE(request_0->cooked_root)
    << "Expected cooked root to be present";
  EXPECT_EQ(
    request_0->cooked_root->lexically_normal(), cooked_root.lexically_normal());

  std::vector<ImportDiagnostic> request_errors_1;
  const auto request_1 = manifest->jobs.at(1).BuildRequest(request_errors_1);
  ASSERT_HAS_VALUE(request_1) << DiagnosticSummary(request_errors_1);
  ASSERT_HAS_VALUE(request_1->cooked_root)
    << "Expected cooked root to be present";
  EXPECT_EQ(
    request_1->cooked_root->lexically_normal(), cooked_root.lexically_normal());
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  DefaultsOutputOverridesTopLevelOutputForSidecarJobs)
{
  const ScopedTempDir temp;
  const auto defaults_output = temp.Path() / "defaults_over_top_level_output"
    / "oxygen_manifest_output_defaults";
  const auto manifest_path
    = temp.Path() / "defaults_over_top_level_output" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "oxygen_manifest_output_top",
      "defaults": {
        "scripting_sidecar": {
          "output": "oxygen_manifest_output_defaults"
        }
      },
      "jobs": [
        {
          "type": "script-sidecar",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "00000000-0000-4000-8000-00000000000a",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::vector<ImportDiagnostic> request_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  ASSERT_HAS_VALUE(request->cooked_root)
    << "Expected cooked root to be present";
  EXPECT_EQ(request->cooked_root->lexically_normal(),
    defaults_output.lexically_normal());
}

NOLINT_TEST(ImportManifestScriptSidecarTest,
  JobOutputOverridesDefaultsAndTopLevelForSidecarJobs)
{
  const ScopedTempDir temp;
  const auto job_output = temp.Path() / "job_overrides_defaults_output"
    / "oxygen_manifest_output_job2";
  const auto manifest_path
    = temp.Path() / "job_overrides_defaults_output" / "import_manifest.json";

  WriteText(manifest_path,
    R"({
      "version": 1,
      "mounts": [{ "name": "Content", "path": "." }],
      "output": "oxygen_manifest_output_top2",
      "defaults": {
        "scripting_sidecar": {
          "output": "oxygen_manifest_output_defaults2"
        }
      },
      "jobs": [
        {
          "type": "script-sidecar",
          "output": "oxygen_manifest_output_job2",
          "target_scene_virtual_path": "/Scenes/TestScene.oscene",
          "bindings": [
            {
              "node_index": 0,
              "slot_id": "00000000-0000-4000-8000-00000000000a",
              "script_virtual_path": "/Scripts/test.oscript"
            }
          ]
        }
      ]
    })");

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 1U);

  std::vector<ImportDiagnostic> request_errors;
  const auto request = manifest->jobs.at(0).BuildRequest(request_errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(request_errors);
  ASSERT_HAS_VALUE(request->cooked_root)
    << "Expected cooked root to be present";
  EXPECT_EQ(
    request->cooked_root->lexically_normal(), job_output.lexically_normal());
}

} // namespace
