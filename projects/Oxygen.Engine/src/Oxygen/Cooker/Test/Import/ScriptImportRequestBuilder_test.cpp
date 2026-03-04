//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <sstream>
#include <string>
#include <type_traits>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ScriptImportSettings.h>
#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>

namespace {

using oxygen::content::import::ScriptAssetImportSettings;
using oxygen::content::import::ScriptingImportKind;
using oxygen::content::import::ScriptingSidecarImportSettings;
using oxygen::content::import::ScriptStorageMode;
using oxygen::content::import::internal::BuildScriptAssetRequest;
using oxygen::content::import::internal::BuildScriptingSidecarRequest;
using oxygen::core::meta::scripting::ScriptCompileMode;

static_assert(std::is_same_v<
  decltype(oxygen::content::import::ImportOptions {}.scripting.compile_mode),
  ScriptCompileMode>);

auto MakeAbsoluteCookedRoot() -> std::string
{
  auto cooked_root
    = std::filesystem::temp_directory_path() / "oxygen_script_import_test";
  return cooked_root.string();
}

auto MakeValidScriptAssetSettings() -> ScriptAssetImportSettings
{
  ScriptAssetImportSettings settings {};
  settings.source_path = "scripts/player_controller.lua";
  settings.cooked_root = MakeAbsoluteCookedRoot();
  settings.job_name = "script-asset";
  settings.compile_scripts = true;
  settings.compile_mode = "optimized";
  settings.script_storage = "embedded";
  return settings;
}

auto MakeValidSidecarSettings() -> ScriptingSidecarImportSettings
{
  ScriptingSidecarImportSettings settings {};
  settings.source_path = "scripts/scene_scripting_sidecar.yaml";
  settings.cooked_root = MakeAbsoluteCookedRoot();
  settings.job_name = "scene-sidecar";
  settings.target_scene_virtual_path = "/.cooked/Scenes/TestScene.oscene";
  return settings;
}

auto MakeInlineSidecarSettings() -> ScriptingSidecarImportSettings
{
  auto settings = MakeValidSidecarSettings();
  settings.source_path.clear();
  settings.inline_bindings_json
    = R"({"bindings":[{"node_index":0,"slot_id":"main","script_virtual_path":"/Scripts/test.oscript"}]})";
  return settings;
}

NOLINT_TEST(ScriptImportRequestBuilderTest, ScriptStorageModeToStringIsStable)
{
  using oxygen::content::import::to_string;
  EXPECT_EQ(to_string(ScriptStorageMode::kEmbedded), "Embedded");
  EXPECT_EQ(to_string(ScriptStorageMode::kExternal), "External");
}

NOLINT_TEST(ScriptImportRequestBuilderTest, BuildScriptAssetRequestValidInput)
{
  auto settings = MakeValidScriptAssetSettings();
  std::ostringstream errors;

  const auto request = BuildScriptAssetRequest(settings, errors);

  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(errors.str().empty());
  EXPECT_EQ(
    request->options.scripting.import_kind, ScriptingImportKind::kScriptAsset);
  EXPECT_TRUE(request->options.scripting.compile_scripts);
  EXPECT_EQ(
    request->options.scripting.compile_mode, ScriptCompileMode::kOptimized);
  EXPECT_EQ(
    request->options.scripting.script_storage, ScriptStorageMode::kEmbedded);
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
}

NOLINT_TEST(ScriptImportRequestBuilderTest,
  BuildScriptAssetRequestRejectsCompileWithExternalStorage)
{
  auto settings = MakeValidScriptAssetSettings();
  settings.script_storage = "external";
  std::ostringstream errors;

  const auto request = BuildScriptAssetRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find(
                "compile_scripts=true is invalid with script_storage=external")
    != std::string::npos);
}

NOLINT_TEST(ScriptImportRequestBuilderTest,
  BuildScriptAssetRequestRejectsInvalidCompileMode)
{
  auto settings = MakeValidScriptAssetSettings();
  settings.compile_mode = "shipping";
  std::ostringstream errors;

  const auto request = BuildScriptAssetRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(
    errors.str().find("invalid script compile mode") != std::string::npos);
}

NOLINT_TEST(ScriptImportRequestBuilderTest,
  BuildScriptingSidecarRequestRejectsMissingTargetSceneVirtualPath)
{
  auto settings = MakeValidSidecarSettings();
  settings.target_scene_virtual_path.clear();
  std::ostringstream errors;

  const auto request = BuildScriptingSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("target_scene_virtual_path is required")
    != std::string::npos);
}

NOLINT_TEST(ScriptImportRequestBuilderTest,
  BuildScriptingSidecarRequestRejectsNonCanonicalVirtualPath)
{
  auto settings = MakeValidSidecarSettings();
  settings.target_scene_virtual_path = "/.cooked/Scenes/./TestScene.oscene";
  std::ostringstream errors;

  const auto request = BuildScriptingSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("canonical virtual path") != std::string::npos);
}

NOLINT_TEST(
  ScriptImportRequestBuilderTest, BuildScriptingSidecarRequestValidInput)
{
  auto settings = MakeValidSidecarSettings();
  std::ostringstream errors;

  const auto request = BuildScriptingSidecarRequest(settings, errors);

  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(errors.str().empty());
  EXPECT_EQ(request->options.scripting.import_kind,
    ScriptingImportKind::kScriptingSidecar);
  EXPECT_EQ(request->options.scripting.target_scene_virtual_path,
    settings.target_scene_virtual_path);
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
}

NOLINT_TEST(
  ScriptImportRequestBuilderTest, BuildScriptingSidecarRequestValidInlineInput)
{
  auto settings = MakeInlineSidecarSettings();
  std::ostringstream errors;

  const auto request = BuildScriptingSidecarRequest(settings, errors);

  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(errors.str().empty());
  EXPECT_EQ(request->options.scripting.import_kind,
    ScriptingImportKind::kScriptingSidecar);
  EXPECT_EQ(request->options.scripting.target_scene_virtual_path,
    settings.target_scene_virtual_path);
  EXPECT_TRUE(
    request->options.scripting.inline_bindings_json.find("\"bindings\"")
    != std::string::npos);
  EXPECT_TRUE(
    request->options.scripting.inline_bindings_json.find("script_virtual_path")
    != std::string::npos);
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
}

NOLINT_TEST(ScriptImportRequestBuilderTest,
  BuildScriptingSidecarRequestAcceptsInlineBindingsArrayPayload)
{
  auto settings = MakeValidSidecarSettings();
  settings.source_path.clear();
  settings.inline_bindings_json
    = R"([{"node_index":0,"slot_id":"main","script_virtual_path":"/Scripts/test.oscript"}])";
  std::ostringstream errors;

  const auto request = BuildScriptingSidecarRequest(settings, errors);

  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(errors.str().empty());
  EXPECT_TRUE(
    request->options.scripting.inline_bindings_json.find("\"bindings\"")
    != std::string::npos);
}

NOLINT_TEST(ScriptImportRequestBuilderTest,
  BuildScriptingSidecarRequestRejectsBothSourceAndInlineBindings)
{
  auto settings = MakeInlineSidecarSettings();
  settings.source_path = "scripts/scene_scripting_sidecar.yaml";
  std::ostringstream errors;

  const auto request = BuildScriptingSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("exactly one of source_path or "
                                "inline_bindings_json must be provided")
    != std::string::npos);
}

NOLINT_TEST(ScriptImportRequestBuilderTest,
  BuildScriptingSidecarRequestRejectsMissingSourceAndInlineBindings)
{
  auto settings = MakeValidSidecarSettings();
  settings.source_path.clear();
  settings.inline_bindings_json.clear();
  std::ostringstream errors;

  const auto request = BuildScriptingSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("exactly one of source_path or "
                                "inline_bindings_json must be provided")
    != std::string::npos);
}

NOLINT_TEST(ScriptImportRequestBuilderTest,
  BuildScriptingSidecarRequestRejectsInvalidInlineBindingsJson)
{
  auto settings = MakeValidSidecarSettings();
  settings.source_path.clear();
  settings.inline_bindings_json = "{ invalid }";
  std::ostringstream errors;

  const auto request = BuildScriptingSidecarRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("inline_bindings_json is not valid JSON")
    != std::string::npos);
}

} // namespace
