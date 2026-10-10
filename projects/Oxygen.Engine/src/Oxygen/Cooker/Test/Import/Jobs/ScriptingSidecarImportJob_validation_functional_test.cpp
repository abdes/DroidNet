//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/ScriptingSidecarImportJob.cpp

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "ScriptImportTestSupport.h"

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  auto HasAnyObjectPath(const std::vector<ImportDiagnostic>& diagnostics)
    -> bool
  {
    return std::ranges::any_of(
      diagnostics, [](const ImportDiagnostic& diagnostic) -> bool {
        return !diagnostic.object_path.empty();
      });
  }

  class ScriptingSidecarImportJobValidationTest
    : public ScriptingImportTestBase { };

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    DiagnosticsFieldsAreCompleteForSidecarFailures)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_diagnostics_fields_sidecar_failure";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "diagnostics_fields_sidecar.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(
        MakeSpeedBinding("Scripts/not_canonical_script_path.oscript")));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(report.success);
    ExpectDiagnosticFieldsComplete(report.diagnostics);
    EXPECT_TRUE(HasAnyObjectPath(report.diagnostics));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    PackagingSummaryMatchesDiagnosticsForFailures)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_packaging_summary_sidecar_failure";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "summary_sidecar_failure.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(
        MakeSpeedBinding(CanonicalScriptVirtualPath("missing_script"))));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(report.success);
    ExpectPackagingSummaryMatchesDiagnostics(report);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    RejectsScriptRefThatResolvesToSceneAsset)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_cycle_rejection";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";

    const auto sidecar_source = cooked_root / "input" / "cycle_reject.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(scene_asset->virtual_path)));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.script_ref_not_script_asset"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    DiagnosticsOrderingIsDeterministicAcrossRepeatedInvalidRuns)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_diagnostics_ordering_determinism";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "diagnostics_ordering.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec {
          .node_index = 99999U,
          .slot_id = "oob",
          .script_virtual_path = CanonicalScriptVirtualPath("missing"),
          .execution_order = 1,
        },
        SidecarBindingSpec {
          .node_index = 0U,
          .slot_id = "bad_path",
          .script_virtual_path = "Scripts/not_canonical.oscript",
          .execution_order = 2,
        },
      }));

    const auto first_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    const auto second_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(first_report.success);
    ASSERT_FALSE(second_report.success);

    const auto MakeDiagnosticSignature
      = [](const ImportReport& report) -> std::vector<std::string> {
      auto signature = std::vector<std::string> {};
      signature.reserve(report.diagnostics.size());
      for (const auto& diagnostic : report.diagnostics) {
        signature.push_back(diagnostic.code + "|" + diagnostic.object_path);
      }
      return signature;
    };
    EXPECT_EQ(MakeDiagnosticSignature(first_report),
      MakeDiagnosticSignature(second_report));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    ScriptingSidecarRejectsMissingTargetScenePath)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_missing_target_scene";
    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteText(sidecar_source, "{ \"bindings\": [] }");

    auto request = MakeSidecarRequest(sidecar_source, cooked_root, "");

    const auto report = Submit(std::move(request));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.request.target_scene_virtual_path_missing"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    ScriptingSidecarRejectsInvalidCanonicalTargetScenePath)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_invalid_target_scene_path";
    const auto script_source = cooked_root / "input" / "seed_script.luau";
    WriteText(script_source, "return 1");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto sidecar_source = cooked_root / "input" / "invalid_scene.json";
    WriteText(sidecar_source, "{ \"bindings\": [] }");

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, "Scenes/not_canonical.oscene"));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.target_scene_virtual_path_invalid"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    ScriptingSidecarRejectsUnresolvedScriptReference)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_unresolved_script_reference";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto scene_report = Submit(MakeSceneRequest(model_path, cooked_root));
    ASSERT_TRUE(scene_report.success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";

    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(
        MakeSpeedBinding(CanonicalScriptVirtualPath("does_not_exist"))));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.script_ref_unresolved"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    MissingSourceFileFailsWithSidecarReadDiagnostic)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_missing_source_file";
    const auto missing_source = cooked_root / "input" / "missing_sidecar.json";

    const auto report = Submit(MakeSidecarRequest(
      missing_source, cooked_root, CanonicalSceneVirtualPath("any_scene")));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.source_read_failed"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest, RejectsMalformedJson)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_malformed_json";
    const auto sidecar_source = cooked_root / "input" / "bad_sidecar.json";
    WriteText(sidecar_source, "{ this is : not valid json ]");

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, CanonicalSceneVirtualPath("any_scene")));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.sidecar.parse_failed"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    RejectsMissingTargetSceneAssetInCookedRoot)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_missing_scene";
    const auto script_source = cooked_root / "input" / "seed_script.luau";
    WriteText(script_source, "return 0");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteText(sidecar_source, "{ \"bindings\": [] }");

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, CanonicalSceneVirtualPath("missing_scene")));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.target_scene_missing"));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportJobValidationTest, RejectsDuplicateBindingIdentity)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_duplicate_binding_identity";
    const auto cooked = CookSceneWithScript(cooked_root, "return 123");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "dup.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec {
          .script_virtual_path = script_asset->virtual_path,
        },
        SidecarBindingSpec {
          .script_virtual_path = script_asset->virtual_path,
        },
      }));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.duplicate_slot_conflict"));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportJobValidationTest, RejectsOutOfBoundsNodeReference)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_out_of_bounds_node";
    const auto cooked = CookSceneWithScript(cooked_root, "return 123");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "oob.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(SidecarBindingSpec {
        .node_index = 99999U,
        .script_virtual_path = script_asset->virtual_path,
      }));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.node_ref_unresolved"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobValidationTest,
    RejectsInvalidScriptVirtualPathFormat)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_invalid_script_virtual_path";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";

    const auto sidecar_source = cooked_root / "input" / "invalid_path.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(
        MakeSpeedBinding("Scripts/not_absolute_or_virtual_path.oscript")));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.script_virtual_path_invalid"));
  }

} // namespace

} // namespace oxygen::content::import::test
