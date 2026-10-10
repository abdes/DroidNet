//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/ScriptingSidecarImportJob.cpp

#include <algorithm>
#include <filesystem>
#include <latch>
#include <string>
#include <string_view>
#include <utility>

#include "ScriptImportTestSupport.h"

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestPaths.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  auto MakeInlineSidecarRequest(const std::filesystem::path& cooked_root,
    std::string scene_virtual_path, std::string inline_bindings_json)
    -> ImportRequest
  {
    ImportRequest request {};
    request.cooked_root = cooked_root;
    request.options.scripting.import_kind
      = ScriptingImportKind::kScriptingSidecar;
    request.options.scripting.target_scene_virtual_path
      = std::move(scene_virtual_path);
    request.options.scripting.inline_bindings_json
      = std::move(inline_bindings_json);
    return request;
  }

  class ScriptingSidecarImportJobBindingTest : public ScriptingImportTestBase {
  };

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    ScriptingSidecarBindsScriptToSceneNode)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_binds_scene";
    const auto cooked = CookSceneWithScript(cooked_root, "return 123");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_TRUE(scene_asset.has_value())
      << "Expected scene asset to be present";
    ASSERT_TRUE(script_asset.has_value())
      << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    const auto sidecar_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(sidecar_report.success);

    const auto inspection_after_sidecar = LoadInspection(cooked_root);
    const auto files = inspection_after_sidecar.Files();
    EXPECT_FALSE(std::ranges::any_of(files, [](const auto& file) {
      return file.relpath.ends_with("script-bindings.table")
        || file.relpath.ends_with("script-bindings.data");
    }));

    const auto scene_bytes
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(scene_asset->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components.front().node_index, 0U);
    EXPECT_EQ(components.front().slot_count, 1U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    InlineBindingsPayloadBindsScriptToSceneNode)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_inline_binds_scene";
    const auto cooked = CookSceneWithScript(cooked_root, "return 123");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_TRUE(scene_asset.has_value())
      << "Expected scene asset to be present";
    ASSERT_TRUE(script_asset.has_value())
      << "Expected script asset to be present";

    const auto sidecar_report
      = Submit(MakeInlineSidecarRequest(cooked_root, scene_asset->virtual_path,
        MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path))));
    ASSERT_TRUE(sidecar_report.success);

    const auto inspection_after_sidecar = LoadInspection(cooked_root);
    const auto files = inspection_after_sidecar.Files();
    EXPECT_FALSE(std::ranges::any_of(files, [](const auto& file) {
      return file.relpath.ends_with("script-bindings.table")
        || file.relpath.ends_with("script-bindings.data");
    }));

    const auto scene_bytes
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(scene_asset->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components.front().node_index, 0U);
    EXPECT_EQ(components.front().slot_count, 1U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    ScriptingSidecarCallbacksProvideProgressAndSingleCompletion)
  {
    constexpr auto kScriptSource = std::string_view { "return 123" };
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_callbacks_sidecar";
    const auto cooked = CookSceneWithScript(
      cooked_root, kScriptSource, "callback_sidecar.luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_TRUE(scene_asset.has_value())
      << "Expected scene asset to be present";
    ASSERT_TRUE(script_asset.has_value())
      << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "callback_sidecar.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    const auto capture = SubmitAndCaptureCallbacks(Service(),
      MakeSidecarRequest(
        sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(capture.has_value()) << "Expected capture to be present";
    EXPECT_EQ(capture->completion_calls, 1U);
    EXPECT_TRUE(capture->report.success);
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kLoading));
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kWorking));
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kComplete));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    ScriptingSidecarReportCountersArePopulated)
  {
    constexpr auto kScriptSource = std::string_view { "return 123" };
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_report_counters_sidecar";
    const auto cooked
      = CookSceneWithScript(cooked_root, kScriptSource, "counter_sidecar.luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_TRUE(scene_asset.has_value())
      << "Expected scene asset to be present";
    ASSERT_TRUE(script_asset.has_value())
      << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "counter_sidecar.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(report.success);
    EXPECT_EQ(report.scripts_written, 0U);
    EXPECT_EQ(report.scripting_components_written, 1U);
    EXPECT_EQ(report.script_slots_written, 1U);
    EXPECT_EQ(report.script_params_written, 1U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    MixedBatchCommitPreservesSuccessfulScriptWhenSidecarFails)
  {
    constexpr auto kScriptSource = std::string_view { "return 10" };
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_mixed_batch_commit";
    const auto script_source = cooked_root / "input" / "batch_success.luau";
    const auto sidecar_source = cooked_root / "input" / "batch_failure.json";
    WriteText(script_source, kScriptSource);
    WriteText(sidecar_source, "{ \"bindings\": [] }");

    auto script_report = ImportReport {};
    auto sidecar_report = ImportReport {};
    std::latch script_done(1);
    std::latch sidecar_done(1);

    const auto script_submit
      = Service().SubmitImport(MakeScriptRequest(script_source, cooked_root,
                                 ScriptStorageMode::kExternal, false),
        [&script_report, &script_done](
          const auto /*job_id*/, const ImportReport& report) {
          script_report = report;
          script_done.count_down();
        });
    if (!script_submit.has_value()) {
      FAIL() << "Expected script submit to be present";
    }
    script_done.wait();
    ASSERT_TRUE(script_report.success);

    const auto sidecar_submit
      = Service().SubmitImport(MakeSidecarRequest(sidecar_source, cooked_root,
                                 CanonicalSceneVirtualPath("missing_scene")),
        [&sidecar_report, &sidecar_done](
          const auto /*job_id*/, const ImportReport& report) {
          sidecar_report = report;
          sidecar_done.count_down();
        });
    if (!sidecar_submit.has_value()) {
      FAIL() << "Expected sidecar submit to be present";
    }
    sidecar_done.wait();
    EXPECT_FALSE(sidecar_report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      sidecar_report.diagnostics, "script.sidecar.target_scene_missing"));

    const auto inspection = LoadInspection(cooked_root);
    const auto script_asset
      = FindFirstAssetByType(inspection, AssetType::kScript);
    ASSERT_TRUE(script_asset.has_value())
      << "Expected script asset to be present";
    EXPECT_TRUE(
      std::filesystem::exists(cooked_root / script_asset->descriptor_relpath));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    SidecarBindsToSceneAndScriptFromCookedRoot)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_dispatch_sidecar_cooked_context";
    const auto cooked
      = CookSceneWithScript(cooked_root, "return 21", "dispatch_cooked.luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_TRUE(scene_asset.has_value())
      << "Expected scene asset to be present";
    ASSERT_TRUE(script_asset.has_value())
      << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "dispatch_cooked.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(report.success);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    SidecarBindsToSceneFromInflightSceneContext)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_dispatch_sidecar_inflight_context";
    const auto inflight_scene_root
      = temp.Path() / "script_dispatch_sidecar_inflight_scene";
    const auto script_source = cooked_root / "input" / "dispatch_inflight.luau";
    WriteText(script_source, "return 31");

    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(
      Submit(MakeSceneRequest(model_path, inflight_scene_root)).success);

    const auto inflight_inspection = LoadInspection(inflight_scene_root);
    const auto inflight_scene
      = FindFirstAssetByType(inflight_inspection, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(LoadInspection(cooked_root), AssetType::kScript);
    ASSERT_TRUE(inflight_scene.has_value())
      << "Expected inflight scene to be present";
    ASSERT_TRUE(script_asset.has_value())
      << "Expected script asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "dispatch_inflight.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    auto request = MakeSidecarRequest(
      sidecar_source, cooked_root, inflight_scene->virtual_path);
    request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root, *inflight_scene));
    const auto report = Submit(std::move(request));
    ASSERT_TRUE(report.success);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    ConcurrentScriptAndSidecarImportsBothSucceed)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_dispatch_batch";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto base_script_source = cooked_root / "input" / "base_batch.luau";
    WriteText(base_script_source, "return 1");
    ASSERT_TRUE(Submit(MakeScriptRequest(base_script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    const auto base_script
      = FindScriptAssetByDescriptorName(inspection, "base_batch.oscript");
    ASSERT_TRUE(scene_asset.has_value())
      << "Expected scene asset to be present";
    ASSERT_TRUE(base_script.has_value())
      << "Expected base script to be present";

    const auto sidecar_source = cooked_root / "input" / "dispatch_batch.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(base_script->virtual_path)));

    const auto extra_script_source = cooked_root / "input" / "extra_batch.luau";
    WriteText(extra_script_source, "return 2");

    auto script_report = ImportReport {};
    auto sidecar_report = ImportReport {};
    std::latch done(2);

    const auto script_submit = Service().SubmitImport(
      MakeScriptRequest(
        extra_script_source, cooked_root, ScriptStorageMode::kExternal, false),
      [&script_report, &done](
        const auto /*job_id*/, const ImportReport& report) {
        script_report = report;
        done.count_down();
      });
    if (!script_submit.has_value()) {
      FAIL() << "Expected script submit to be present";
    }

    const auto sidecar_submit
      = Service().SubmitImport(MakeSidecarRequest(sidecar_source, cooked_root,
                                 scene_asset->virtual_path),
        [&sidecar_report, &done](
          const auto /*job_id*/, const ImportReport& report) {
          sidecar_report = report;
          done.count_down();
        });
    if (!sidecar_submit.has_value()) {
      FAIL() << "Expected sidecar submit to be present";
    }

    done.wait();
    EXPECT_TRUE(script_report.success);
    EXPECT_TRUE(sidecar_report.success);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobBindingTest,
    DependencyOrderingRequiresScriptAvailabilityBeforeSidecarResolution)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_dependency_ordering";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value())
      << "Expected scene asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "dependency_ordering.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(
        MakeSpeedBinding(CanonicalScriptVirtualPath("deferred"))));

    const auto first_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(first_report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      first_report.diagnostics, "script.sidecar.script_ref_unresolved"));

    const auto script_source = cooked_root / "input" / "deferred.luau";
    WriteText(script_source, "return 55");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection_after_script = LoadInspection(cooked_root);
    const auto deferred_script = FindScriptAssetByDescriptorName(
      inspection_after_script, "deferred.oscript");
    ASSERT_TRUE(deferred_script.has_value())
      << "Expected deferred script to be present";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(deferred_script->virtual_path)));

    const auto second_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_TRUE(second_report.success);
  }

} // namespace

} // namespace oxygen::content::import::test
