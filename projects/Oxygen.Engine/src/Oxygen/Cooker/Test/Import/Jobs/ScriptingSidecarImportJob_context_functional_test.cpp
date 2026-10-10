//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/ScriptingSidecarImportJob.cpp

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>

#include "ScriptImportTestSupport.h"

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  class ScriptingSidecarImportJobContextTest : public ScriptingImportTestBase {
  };

  NOLINT_TEST_F(ScriptingSidecarImportJobContextTest,
    SidecarPathOnlySuccessWithInflightSceneContext)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_inflight_ok";
    const auto inflight_scene_root
      = temp.Path() / "script_sidecar_inflight_scene_source";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "runtime.luau";
    WriteText(script_source, "return 7");
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
    ASSERT_HAS_VALUE(inflight_scene) << "Expected inflight scene to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    auto request = MakeSidecarRequest(
      sidecar_source, cooked_root, inflight_scene->virtual_path);
    request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root, *inflight_scene));

    const auto report = Submit(std::move(request));
    ASSERT_TRUE(report.success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto patched_scene
      = FindFirstAssetByType(inspection_after, AssetType::kScene);
    ASSERT_HAS_VALUE(patched_scene) << "Expected patched scene to be present";
    EXPECT_EQ(patched_scene->key, inflight_scene->key);

    const auto files = inspection_after.Files();
    EXPECT_FALSE(std::ranges::any_of(files, [](const auto& file) -> auto {
      return file.relpath.ends_with("script-bindings.table")
        || file.relpath.ends_with("script-bindings.data");
    }));

    const auto scene_bytes
      = ReadBytes(cooked_root / patched_scene->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(patched_scene->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components.front().node_index, 0U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobContextTest,
    InflightDuplicatePathMatchRejectedDeterministically)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_inflight_duplicate_match";
    const auto inflight_scene_root_a
      = temp.Path() / "script_sidecar_inflight_scene_a";
    const auto inflight_scene_root_b
      = temp.Path() / "script_sidecar_inflight_scene_b";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "seed.luau";
    WriteText(script_source, "return 1");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(
      Submit(MakeSceneRequest(model_path, inflight_scene_root_a)).success);
    ASSERT_TRUE(
      Submit(MakeSceneRequest(model_path, inflight_scene_root_b)).success);

    const auto inspection_a = LoadInspection(inflight_scene_root_a);
    const auto inspection_b = LoadInspection(inflight_scene_root_b);
    const auto scene_a = FindFirstAssetByType(inspection_a, AssetType::kScene);
    const auto scene_b = FindFirstAssetByType(inspection_b, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_a) << "Expected scene a to be present";
    ASSERT_HAS_VALUE(scene_b) << "Expected scene b to be present";
    ASSERT_EQ(scene_a->virtual_path, scene_b->virtual_path);
    ASSERT_EQ(scene_a->key, scene_b->key);

    const auto sidecar_source = cooked_root / "input" / "duplicate.json";
    WriteText(sidecar_source, "{ \"bindings\": [] }");

    auto request
      = MakeSidecarRequest(sidecar_source, cooked_root, scene_a->virtual_path);
    request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root_a, *scene_a));
    request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root_b, *scene_b));

    const auto report = Submit(std::move(request));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.inflight_target_scene_ambiguous"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobContextTest,
    ResolverDelegationSupportsDeterministicKeyAcrossMountedContexts)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_resolver_precedence_target";
    const auto context_root_a
      = temp.Path() / "script_sidecar_resolver_precedence_a";
    const auto context_root_b
      = temp.Path() / "script_sidecar_resolver_precedence_b";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "resolver_ref.luau";
    WriteText(script_source, "return 5");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, context_root_a)).success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, context_root_b)).success);

    const auto inspection_a = LoadInspection(context_root_a);
    const auto inspection_b = LoadInspection(context_root_b);
    const auto scene_a = FindFirstAssetByType(inspection_a, AssetType::kScene);
    const auto scene_b = FindFirstAssetByType(inspection_b, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(LoadInspection(cooked_root), AssetType::kScript);
    ASSERT_HAS_VALUE(scene_a) << "Expected scene a to be present";
    ASSERT_HAS_VALUE(scene_b) << "Expected scene b to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";
    ASSERT_EQ(scene_a->virtual_path, scene_b->virtual_path);
    ASSERT_EQ(scene_a->key, scene_b->key);

    const auto sidecar_source = cooked_root / "input" / "precedence.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    auto request
      = MakeSidecarRequest(sidecar_source, cooked_root, scene_a->virtual_path);
    request.cooked_context_roots.push_back(context_root_a);
    request.cooked_context_roots.push_back(context_root_b);

    const auto report = Submit(std::move(request));
    ASSERT_TRUE(report.success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto resolved_scene
      = FindFirstAssetByType(inspection_after, AssetType::kScene);
    ASSERT_HAS_VALUE(resolved_scene) << "Expected resolved scene to be present";
    EXPECT_EQ(resolved_scene->key, scene_b->key);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobContextTest,
    ContextResolvedSceneLoadsScriptsTablesFromWinningCookedContext)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;

    const ScopedTempDir temp;
    const auto request_root
      = temp.Path() / "script_sidecar_context_table_source_request";
    const auto context_root
      = temp.Path() / "script_sidecar_context_table_source_context";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto context_script_source
      = context_root / "input" / "context_script.luau";
    WriteText(context_script_source, "return 77");
    ASSERT_TRUE(Submit(MakeScriptRequest(context_script_source, context_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, context_root)).success);

    const auto context_inspection_before = LoadInspection(context_root);
    const auto context_scene
      = FindFirstAssetByType(context_inspection_before, AssetType::kScene);
    const auto context_script
      = FindFirstAssetByType(context_inspection_before, AssetType::kScript);
    ASSERT_HAS_VALUE(context_scene) << "Expected context scene to be present";
    ASSERT_HAS_VALUE(context_script) << "Expected context script to be present";

    const auto context_sidecar_source
      = context_root / "input" / "seed_context_sidecar.json";
    WriteText(context_sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(context_script->virtual_path)));
    ASSERT_TRUE(Submit(MakeSidecarRequest(context_sidecar_source, context_root,
                         context_scene->virtual_path))
        .success);

    const auto request_script_source
      = request_root / "input" / "context_script.luau";
    WriteText(request_script_source, "return 88");
    ASSERT_TRUE(Submit(MakeScriptRequest(request_script_source, request_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto request_inspection_before = LoadInspection(request_root);
    const auto request_script
      = FindFirstAssetByType(request_inspection_before, AssetType::kScript);
    ASSERT_HAS_VALUE(request_script) << "Expected request script to be present";

    const auto request_sidecar_source
      = request_root / "input" / "context_resolve_sidecar.json";
    WriteText(request_sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(request_script->virtual_path)));

    auto request = MakeSidecarRequest(
      request_sidecar_source, request_root, context_scene->virtual_path);
    request.cooked_context_roots.push_back(context_root);
    const auto report = Submit(std::move(request));
    ASSERT_TRUE(report.success);

    const auto request_inspection_after = LoadInspection(request_root);
    const auto patched_scene
      = FindFirstAssetByType(request_inspection_after, AssetType::kScene);
    ASSERT_HAS_VALUE(patched_scene) << "Expected patched scene to be present";
    EXPECT_EQ(patched_scene->key, context_scene->key);

    const auto files = request_inspection_after.Files();
    EXPECT_FALSE(std::ranges::any_of(files, [](const auto& file) -> auto {
      return file.relpath.ends_with("script-bindings.table")
        || file.relpath.ends_with("script-bindings.data");
    }));

    const auto scene_bytes
      = ReadBytes(request_root / patched_scene->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(patched_scene->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobContextTest,
    ContextParityInflightAndStandaloneProduceEquivalentSerializedState)
  {
    using data::loose_cooked::FileKind;

    const ScopedTempDir temp;
    const auto concurrent_root = temp.Path() / "script_sidecar_parity_a";
    const auto standalone_root = temp.Path() / "script_sidecar_parity_b";
    const auto inflight_scene_root
      = temp.Path() / "script_sidecar_parity_inflight_scene";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source_a = concurrent_root / "input" / "parity.luau";
    const auto script_source_b = standalone_root / "input" / "parity.luau";
    constexpr auto kScriptText = std::string_view { "return 42" };
    WriteText(script_source_a, kScriptText);
    WriteText(script_source_b, kScriptText);

    ASSERT_TRUE(Submit(MakeScriptRequest(script_source_a, concurrent_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source_b, standalone_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(
      Submit(MakeSceneRequest(model_path, inflight_scene_root)).success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, standalone_root)).success);

    const auto inflight_inspection = LoadInspection(inflight_scene_root);
    const auto inflight_scene
      = FindFirstAssetByType(inflight_inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(inflight_scene) << "Expected inflight scene to be present";

    const auto script_a = FindFirstAssetByType(
      LoadInspection(concurrent_root), AssetType::kScript);
    const auto script_b = FindFirstAssetByType(
      LoadInspection(standalone_root), AssetType::kScript);
    ASSERT_HAS_VALUE(script_a) << "Expected script a to be present";
    ASSERT_HAS_VALUE(script_b) << "Expected script b to be present";

    const auto sidecar_source_a
      = concurrent_root / "input" / "parity_sidecar.json";
    const auto sidecar_source_b
      = standalone_root / "input" / "parity_sidecar.json";
    WriteText(sidecar_source_a,
      MakeSidecarPayload(MakeSpeedBinding(script_a->virtual_path)));
    WriteText(sidecar_source_b,
      MakeSidecarPayload(MakeSpeedBinding(script_b->virtual_path)));

    auto concurrent_request = MakeSidecarRequest(
      sidecar_source_a, concurrent_root, inflight_scene->virtual_path);
    concurrent_request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root, *inflight_scene));
    ASSERT_TRUE(Submit(std::move(concurrent_request)).success);

    const auto standalone_scene_before = FindFirstAssetByType(
      LoadInspection(standalone_root), AssetType::kScene);
    ASSERT_HAS_VALUE(standalone_scene_before)
      << "Expected standalone scene before to be present";
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source_b, standalone_root,
                         standalone_scene_before->virtual_path))
        .success);

    const auto concurrent_inspection = LoadInspection(concurrent_root);
    const auto standalone_inspection = LoadInspection(standalone_root);

    const auto concurrent_scene
      = FindFirstAssetByType(concurrent_inspection, AssetType::kScene);
    const auto standalone_scene
      = FindFirstAssetByType(standalone_inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(concurrent_scene)
      << "Expected concurrent scene to be present";
    ASSERT_HAS_VALUE(standalone_scene)
      << "Expected standalone scene to be present";

    const auto concurrent_scene_bytes
      = ReadBytes(concurrent_root / concurrent_scene->descriptor_relpath);
    const auto standalone_scene_bytes
      = ReadBytes(standalone_root / standalone_scene->descriptor_relpath);

    EXPECT_EQ(concurrent_scene_bytes, standalone_scene_bytes);
  }

} // namespace

} // namespace oxygen::content::import::test
