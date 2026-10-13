//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/ScriptingSidecarImportPipeline.cpp
// (validation of overrides against script declarations, asset and node
// parameters, and the scene references they contribute).

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ScriptImportTestSupport.h"
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using data::pak::scripting::ScriptParamType;

  class ScriptingSidecarDeclarationsTest : public ScriptingImportTestBase {
  protected:
    //! Cooks the test scene and a script declaring `params_body`.
    [[nodiscard]] auto CookWithDeclaration(
      const std::filesystem::path& cooked_root,
      const std::string_view params_body) -> SceneAndScript
    {
      const auto source = std::string("local Script = oxygen.script {\n"
                                      "  params = {\n")
        + std::string(params_body) + "\n  },\n}\nreturn Script\n";
      return CookSceneWithScript(cooked_root, source, "declared.luau");
    }

    //! Applies a one-binding sidecar whose params are `params_json`.
    [[nodiscard]] auto ApplySidecar(const std::filesystem::path& cooked_root,
      const AssetRef& scene, const AssetRef& script,
      const std::string_view params_json) -> ImportReport
    {
      const auto sidecar = cooked_root / "input" / "declared.json";
      WriteText(sidecar,
        MakeSidecarPayload(SidecarBindingSpec {
          .node_index = 0,
          .slot_id = "main",
          .script_virtual_path = script.virtual_path,
          .execution_order = std::nullopt,
          .params = nlohmann::json::parse(params_json),
        }));
      return Submit(
        MakeSidecarRequest(sidecar, cooked_root, scene.virtual_path));
    }

    [[nodiscard]] static auto FindDiagnostic(const ImportReport& report,
      const std::string_view code) -> std::optional<ImportDiagnostic>
    {
      const auto it
        = std::ranges::find(report.diagnostics, code, &ImportDiagnostic::code);
      if (it == report.diagnostics.end()) {
        return std::nullopt;
      }
      return *it;
    }

    [[nodiscard]] static auto HasAssetReference(
      const data::AssetReferences& references, const data::AssetKey& key,
      const AssetType type) -> bool
    {
      return std::ranges::any_of(references.Keys(),
        [&key, type](const data::KeyReference& reference) -> bool {
          return reference.kind == data::KeyReferenceKind::kAsset
            && reference.key == key && reference.expected_type == type;
        });
    }

    [[nodiscard]] auto SceneReferences(const std::filesystem::path& cooked_root,
      const data::AssetKey& scene) -> data::AssetReferences
    {
      const auto inspection = LoadInspection(cooked_root);
      const auto entry
        = std::ranges::find(inspection.Assets(), scene, &lc::AssetEntry::key);
      EXPECT_NE(entry, inspection.Assets().end());
      return entry == inspection.Assets().end() ? data::AssetReferences {}
                                                : entry->references;
    }
  };

  //! An override of a parameter the script does not declare fails at its key.
  NOLINT_TEST_F(ScriptingSidecarDeclarationsTest, UndeclaredOverrideIsRejected)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "sidecar_undeclared";
    const auto cooked
      = CookWithDeclaration(cooked_root, "    speed = oxygen.param.float {},");
    ASSERT_HAS_VALUE(cooked.scene);
    ASSERT_HAS_VALUE(cooked.script);

    const auto report = ApplySidecar(cooked_root, *cooked.scene, *cooked.script,
      R"([ { "key": "spead", "type": "float", "value": 1.0 } ])");

    EXPECT_FALSE(report.success);
    const auto diagnostic
      = FindDiagnostic(report, "script.sidecar.param_undeclared");
    ASSERT_HAS_VALUE(diagnostic);
    EXPECT_EQ(diagnostic->object_path, "bindings[0].params[0].key");
  }

  //! An override whose type differs from the declaration fails at its type.
  NOLINT_TEST_F(ScriptingSidecarDeclarationsTest, TypeMismatchIsRejected)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "sidecar_type_mismatch";
    const auto cooked
      = CookWithDeclaration(cooked_root, "    speed = oxygen.param.float {},");
    ASSERT_HAS_VALUE(cooked.scene);
    ASSERT_HAS_VALUE(cooked.script);

    const auto report = ApplySidecar(cooked_root, *cooked.scene, *cooked.script,
      R"([ { "key": "speed", "type": "int32", "value": 3 } ])");

    EXPECT_FALSE(report.success);
    const auto diagnostic
      = FindDiagnostic(report, "script.sidecar.param_type_mismatch");
    ASSERT_HAS_VALUE(diagnostic);
    EXPECT_EQ(diagnostic->object_path, "bindings[0].params[0].type");
  }

  //! A node override cooks its scene node index; an index outside the scene
  //! fails at its value.
  NOLINT_TEST_F(ScriptingSidecarDeclarationsTest, NodeOverridesAreBounded)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "sidecar_node_param";
    const auto cooked
      = CookWithDeclaration(cooked_root, "    target = oxygen.param.node {},");
    ASSERT_HAS_VALUE(cooked.scene);
    ASSERT_HAS_VALUE(cooked.script);

    const auto rejected
      = ApplySidecar(cooked_root, *cooked.scene, *cooked.script,
        R"([ { "key": "target", "type": "node", "value": 1000000 } ])");
    const auto accepted = ApplySidecar(cooked_root, *cooked.scene,
      *cooked.script, R"([ { "key": "target", "type": "node", "value": 0 } ])");

    EXPECT_FALSE(rejected.success);
    const auto diagnostic
      = FindDiagnostic(rejected, "script.sidecar.param_node_out_of_range");
    ASSERT_HAS_VALUE(diagnostic);
    EXPECT_EQ(diagnostic->object_path, "bindings[0].params[0].value");

    ASSERT_TRUE(accepted.success);
    const auto state = ReadSceneScriptState(cooked_root, *cooked.scene);
    ASSERT_EQ(state.params.size(), 1U);
    EXPECT_EQ(state.params.front().type, ScriptParamType::kNode);
    EXPECT_EQ(state.params.front().value.as_node_index, 0U);
  }

  //! An asset override cooks the asset key and kind and joins the scene's
  //! references; removing it removes the reference again.
  NOLINT_TEST_F(
    ScriptingSidecarDeclarationsTest, AssetOverridesJoinSceneReferences)
  {
    // Arrange
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "sidecar_asset_param";
    const auto cooked = CookWithDeclaration(
      cooked_root, "    orb = oxygen.param.asset { kind = \"material\" },");
    ASSERT_HAS_VALUE(cooked.scene);
    ASSERT_HAS_VALUE(cooked.script);
    const auto base_references
      = SceneReferences(cooked_root, cooked.scene->key);
    auto material = std::optional<AssetRef> {};
    for (const auto& entry : cooked.inspection.Assets()) {
      if (static_cast<AssetType>(entry.asset_type) == AssetType::kMaterial
        && !HasAssetReference(
          base_references, entry.key, AssetType::kMaterial)) {
        material = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .references = entry.references,
          .type = AssetType::kMaterial,
        };
        break;
      }
    }
    ASSERT_HAS_VALUE(material) << "Expected a material the scene does not use";

    // Act
    const auto with_asset
      = ApplySidecar(cooked_root, *cooked.scene, *cooked.script,
        R"([ { "key": "orb", "type": "asset", "value": ")"
          + material->virtual_path + R"(" } ])");
    ASSERT_TRUE(with_asset.success);
    const auto state = ReadSceneScriptState(cooked_root, *cooked.scene);
    const auto references_with_asset
      = SceneReferences(cooked_root, cooked.scene->key);
    const auto without_asset
      = ApplySidecar(cooked_root, *cooked.scene, *cooked.script, "[]");

    // Assert
    ASSERT_EQ(state.params.size(), 1U);
    EXPECT_EQ(state.params.front().type, ScriptParamType::kAsset);
    EXPECT_EQ(state.params.front().value.as_asset.key, material->key);
    EXPECT_EQ(
      state.params.front().value.as_asset.asset_type, AssetType::kMaterial);
    EXPECT_TRUE(HasAssetReference(
      references_with_asset, material->key, AssetType::kMaterial));

    ASSERT_TRUE(without_asset.success);
    EXPECT_FALSE(
      HasAssetReference(SceneReferences(cooked_root, cooked.scene->key),
        material->key, AssetType::kMaterial));
  }

  //! An asset of another kind, or one that does not exist, fails at its value.
  NOLINT_TEST_F(ScriptingSidecarDeclarationsTest, InvalidAssetOverridesFail)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "sidecar_asset_kind";
    const auto cooked = CookWithDeclaration(
      cooked_root, "    orb = oxygen.param.asset { kind = \"material\" },");
    ASSERT_HAS_VALUE(cooked.scene);
    ASSERT_HAS_VALUE(cooked.script);
    const auto geometry
      = FindFirstAssetByType(cooked.inspection, AssetType::kGeometry);
    ASSERT_HAS_VALUE(geometry);

    const auto wrong_kind
      = ApplySidecar(cooked_root, *cooked.scene, *cooked.script,
        R"([ { "key": "orb", "type": "asset", "value": ")"
          + geometry->virtual_path + R"(" } ])");
    const auto missing = ApplySidecar(cooked_root, *cooked.scene,
      *cooked.script,
      R"([ { "key": "orb", "type": "asset", "value": "/.cooked/Materials/none.omat" } ])");

    EXPECT_FALSE(wrong_kind.success);
    const auto kind_diagnostic
      = FindDiagnostic(wrong_kind, "script.sidecar.param_asset_kind_mismatch");
    ASSERT_HAS_VALUE(kind_diagnostic);
    EXPECT_EQ(kind_diagnostic->object_path, "bindings[0].params[0].value");
    EXPECT_FALSE(missing.success);
    EXPECT_TRUE(FindDiagnostic(missing, "script.sidecar.param_asset_unresolved")
        .has_value());
  }

} // namespace

} // namespace oxygen::content::import::test
