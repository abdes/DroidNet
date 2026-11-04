//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/ScriptingSidecarImportJob.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScriptImportTestSupport.h"
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  constexpr auto kUpdatedSpeed = 9.5F;
  constexpr auto kSceneBSpeed = 7.5F;
  constexpr auto kFirstPart
    = std::string_view { "00000000-0000-4000-8000-0000000000a1" };
  constexpr auto kSecondPart
    = std::string_view { "00000000-0000-4000-8000-0000000000b2" };
  //! The script of the part's slots: a float `speed` and a material `orb`.
  constexpr auto kPartScript = std::string_view {
    "local Script = oxygen.script {\n"
    "  params = {\n"
    "    speed = oxygen.param.float {},\n"
    "    orb = oxygen.param.asset { kind = \"material\" },\n"
    "  },\n"
    "}\n"
    "return Script\n",
  };

  class ScriptingSidecarImportJobReimportTest : public ScriptingImportTestBase {
  protected:
    //! Cooks the scene `descriptor` describes at its name's identity.
    auto CookDescriptor(const std::filesystem::path& cooked_root,
      nlohmann::json descriptor) -> ImportReport
    {
      descriptor.emplace(
        "schema_version", internal::kSceneDescriptorSchemaVersion);
      auto request = ImportRequest {};
      request.source_path = "inline://scene.oscene.json";
      request.asset_virtual_path
        = CanonicalSceneVirtualPath(descriptor.at("name").get<std::string>());
      request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(request.source_path);
      request.cooked_root = cooked_root;
      request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
        .descriptor_json = descriptor.dump(),
      };
      return Submit(std::move(request));
    }

    //! How `CookInstancedPart` cooks the part.
    struct PartSpec final {
      //! The parameters each of the part's slots sets.
      nlohmann::json params;
      //! Where the script is cooked, mounted only for the part's own
      //! bindings; empty for the scenes' root.
      std::filesystem::path script_root;
    };

    //! Cooks `Part`, whose two nodes `kFirstPart` and `kSecondPart` each hold
    //! the slot `main` of `kPartScript`, and `Level`, whose node 1 instances
    //! it.
    auto CookInstancedPart(const std::filesystem::path& cooked_root,
      const PartSpec& spec = {}) -> bool
    {
      const auto& params = spec.params;
      const auto script_root
        = spec.script_root.empty() ? cooked_root : spec.script_root;
      const auto part = CookDescriptor(cooked_root,
        {
          { "name", "Part" },
          {
            "nodes",
            nlohmann::json::array({
              { { "id", kFirstPart }, { "name", "part" } },
              { { "id", kSecondPart }, { "name", "other" }, { "parent", 0 } },
            }),
          },
        });
      const auto script_source = script_root / "input" / "logic.luau";
      WriteText(script_source, kPartScript);
      const auto script = Submit(MakeScriptRequest({
        .source_path = script_source,
        .cooked_root = script_root,
      }));
      const auto part_sidecar = cooked_root / "input" / "part.sidescript.json";
      const auto script_path = SpellReference(
        CanonicalSceneVirtualPath("Part"), CanonicalScriptVirtualPath("logic"));
      WriteText(part_sidecar,
        MakeSidecarPayload(std::vector {
          SidecarBindingSpec {
            .node_index = 0,
            .script_virtual_path = script_path,
            .params = params,
          },
          SidecarBindingSpec {
            .node_index = 1,
            .script_virtual_path = script_path,
            .params = params,
          },
        }));
      auto bindings_request = MakeSidecarRequest({
        .source_path = part_sidecar,
        .cooked_root = cooked_root,
        .scene_virtual_path = CanonicalSceneVirtualPath("Part"),
      });
      if (script_root != cooked_root) {
        bindings_request.cooked_context_roots.push_back(script_root);
      }
      const auto bindings = Submit(std::move(bindings_request));
      const auto scene_ref = SpellReference(
        CanonicalSceneVirtualPath("Level"), CanonicalSceneVirtualPath("Part"));
      const auto level = CookDescriptor(cooked_root,
        {
          { "name", "Level" },
          {
            "nodes",
            nlohmann::json::array({
              {
                { "id", "00000000-0000-4000-8000-000000000001" },
                { "name", "root" },
              },
              {
                { "id", "00000000-0000-4000-8000-000000000002" },
                { "name", "part" },
                { "parent", 0 },
                { "instance", { { "scene_ref", scene_ref } } },
              },
            }),
          },
        });
      return part.success && script.success && bindings.success
        && level.success;
    }

    //! A change of the parameter `key` to `value`, of type `type`.
    [[nodiscard]] static auto ParamChange(const std::string_view key,
      const std::string_view type, const nlohmann::json& value)
      -> nlohmann::json
    {
      return {
        { "key", key },
        { "type", type },
        { "value", value },
      };
    }

    //! Applies a sidecar whose only rows make `change` of one slot of the
    //! nodes `paths` name below node 1, the scene's instance root.
    auto ApplyInstanceChanges(const std::filesystem::path& cooked_root,
      const std::vector<std::string_view>& paths,
      const nlohmann::json& change = ParamChange("speed", "float", 2.0))
      -> ImportReport
    {
      auto rows = nlohmann::json::array();
      for (const auto node : paths) {
        rows.push_back({
          { "node_index", 1 },
          { "node_path", nlohmann::json::array({ node }) },
          { "slot_id", kMainSlotId },
          { "params", nlohmann::json::array({ change }) },
        });
      }
      const auto source = cooked_root / "input" / "level.sidescript.json";
      WriteText(source,
        nlohmann::json {
          { "bindings", nlohmann::json::array() },
          { "instance_bindings", std::move(rows) },
        }
          .dump(2));
      return Submit(MakeSidecarRequest({
        .source_path = source,
        .cooked_root = cooked_root,
        .scene_virtual_path = CanonicalSceneVirtualPath("Level"),
      }));
    }

    //! The cooked level, loaded and validated.
    [[nodiscard]] static auto LoadLevel(
      const std::filesystem::path& cooked_root) -> data::SceneAsset
    {
      const auto key
        = data::AssetKey::FromVirtualPath(CanonicalSceneVirtualPath("Level"));
      const auto inspection = LoadInspection(cooked_root);
      if (inspection.FindAsset(key) == nullptr) {
        throw std::runtime_error("The level was not cooked");
      }
      return { key, ReadBytes(inspection.DescriptorStored(key).file) };
    }
  };

  //! Each change of an instanced node's script parameter names its node by
  //! path. Reimporting the sidecar keeps the scene loadable whatever paths
  //! it names, and keeps only the paths its changes still name. A change
  //! naming no node of the instanced scene is a warning and left out
  //! (scene instances 7.9).
  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    InstancedNodeChangesKeepOnlyTheirNodePaths)
  {
    // Arrange
    constexpr auto kFirst = kFirstPart;
    constexpr auto kSecond = kSecondPart;
    constexpr auto kStranger
      = std::string_view { "00000000-0000-4000-8000-0000000000c3" };
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "instanced_node_changes";
    ASSERT_TRUE(CookInstancedPart(cooked_root));

    // Act
    const auto second_only = ApplyInstanceChanges(cooked_root, { kSecond });
    const auto both = ApplyInstanceChanges(cooked_root, { kFirst, kSecond });
    const auto both_level = LoadLevel(cooked_root);
    const auto first_only = ApplyInstanceChanges(cooked_root, { kFirst });
    const auto first_level = LoadLevel(cooked_root);
    const auto with_stranger
      = ApplyInstanceChanges(cooked_root, { kFirst, kStranger });
    const auto stranger_level = LoadLevel(cooked_root);

    // Assert
    ASSERT_TRUE(second_only.success);
    ASSERT_TRUE(both.success);
    EXPECT_EQ(
      both_level
        .GetComponents<data::pak::scripting::ScriptParamOverrideRecord>()
        .size(),
      2U);
    EXPECT_EQ(
      both_level.GetComponents<data::pak::world::NodePathRecord>().size(), 2U);
    ASSERT_TRUE(first_only.success);
    const auto changes
      = first_level
          .GetComponents<data::pak::scripting::ScriptParamOverrideRecord>();
    ASSERT_EQ(changes.size(), 1U);
    const auto first = data::NodeId::FromString(kFirst);
    ASSERT_TRUE(first.has_value());
    EXPECT_THAT(first_level.GetNodePath(changes.front().target),
      ::testing::ElementsAre(first.value()));
    EXPECT_EQ(
      first_level.GetComponents<data::pak::world::NodePathRecord>().size(), 1U);
    ASSERT_TRUE(with_stranger.success);
    EXPECT_TRUE(HasDiagnosticCode(with_stranger.diagnostics,
      import_codes::kScriptSidecarInstanceOverrideUnresolved));
    EXPECT_EQ(
      stranger_level
        .GetComponents<data::pak::scripting::ScriptParamOverrideRecord>()
        .size(),
      1U);
  }

  //! A change of an instanced slot's parameter follows the declarations of
  //! the slot's script, as an override of the slot itself does, whether or
  //! not the slot sets the parameter: an undeclared key, another type and an
  //! asset of another type each fail at their path (R3 review, item 7).
  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    InstancedNodeChangesFollowTheSlotScriptDeclarations)
  {
    // Arrange
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "instanced_node_declarations";
    ASSERT_TRUE(CookInstancedPart(cooked_root));
    const auto script_asset = SpellReference(
      CanonicalSceneVirtualPath("Level"), CanonicalScriptVirtualPath("logic"));
    const auto path_of
      = [](const ImportReport& report,
          const std::string_view code) -> std::optional<std::string> {
      const auto found
        = std::ranges::find(report.diagnostics, code, &ImportDiagnostic::code);
      if (found == report.diagnostics.end()) {
        return std::nullopt;
      }
      return found->json_pointer;
    };

    // Act
    const auto undeclared = ApplyInstanceChanges(
      cooked_root, { kFirstPart }, ParamChange("spead", "float", 2.0));
    const auto other_type = ApplyInstanceChanges(
      cooked_root, { kFirstPart }, ParamChange("speed", "int32", 3));
    const auto other_asset_type = ApplyInstanceChanges(
      cooked_root, { kFirstPart }, ParamChange("orb", "asset", script_asset));
    const auto declared = ApplyInstanceChanges(cooked_root, { kFirstPart });

    // Assert
    EXPECT_FALSE(undeclared.success);
    const auto undeclared_path
      = path_of(undeclared, import_codes::kScriptSidecarParamUndeclared);
    ASSERT_HAS_VALUE(undeclared_path);
    EXPECT_EQ(*undeclared_path, "/instance_bindings/0/params/0/key");
    EXPECT_FALSE(other_type.success);
    const auto other_type_path
      = path_of(other_type, import_codes::kScriptSidecarParamTypeMismatch);
    ASSERT_HAS_VALUE(other_type_path);
    EXPECT_EQ(*other_type_path, "/instance_bindings/0/params/0/type");
    EXPECT_FALSE(other_asset_type.success);
    const auto other_asset_type_path = path_of(
      other_asset_type, import_codes::kScriptSidecarParamAssetKindMismatch);
    ASSERT_HAS_VALUE(other_asset_type_path);
    EXPECT_EQ(*other_asset_type_path, "/instance_bindings/0/params/0/value");
    EXPECT_TRUE(declared.success);
  }

  //! A change whose slot's script is not mounted is a warning and kept: its
  //! declarations are checked when the scene is instantiated.
  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    InstancedNodeChangeOfAnUnmountedScriptIsKept)
  {
    // Arrange
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "instanced_node_unmounted_script";
    ASSERT_TRUE(CookInstancedPart(cooked_root,
      {
        .script_root = temp.Path() / "instanced_node_scripts",
      }));

    // Act
    const auto report = ApplyInstanceChanges(cooked_root, { kFirstPart });
    const auto level = LoadLevel(cooked_root);

    // Assert
    EXPECT_TRUE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(report.diagnostics,
      import_codes::kScriptSidecarInstanceScriptUnavailable));
    EXPECT_EQ(
      level.GetComponents<data::pak::scripting::ScriptParamOverrideRecord>()
        .size(),
      1U);
  }

  //! A change that gives an instanced slot's float `speed` a string value is
  //! an error, and the level keeps the changes it had (R3 review, item 7).
  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    InstancedNodeChangeOfAnotherKindIsRefused)
  {
    // Arrange
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "instanced_node_change_kind";
    ASSERT_TRUE(CookInstancedPart(cooked_root,
      {
        .params = nlohmann::json::array({
          ParamChange("speed", "float", 1.0),
        }),
      }));
    ASSERT_TRUE(ApplyInstanceChanges(cooked_root, { kSecondPart }).success);

    // Act
    const auto other_kind = ApplyInstanceChanges(
      cooked_root, { kFirstPart }, ParamChange("speed", "string", "fast"));
    const auto level = LoadLevel(cooked_root);

    // Assert
    EXPECT_FALSE(other_kind.success);
    EXPECT_TRUE(HasDiagnosticCode(other_kind.diagnostics,
      import_codes::kScriptSidecarInstanceOverrideInvalid));
    const auto changes
      = level.GetComponents<data::pak::scripting::ScriptParamOverrideRecord>();
    ASSERT_EQ(changes.size(), 1U);
    const auto second = data::NodeId::FromString(kSecondPart);
    ASSERT_HAS_VALUE(second);
    EXPECT_THAT(level.GetNodePath(changes.front().target),
      ::testing::ElementsAre(*second));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    NodeBindingOverwriteAndAdditiveUpdateBehavior)
  {
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_overwrite_additive";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_a_source = cooked_root / "input" / "logic_a.luau";
    const auto script_b_source = cooked_root / "input" / "logic_b.luau";
    const auto script_c_source = cooked_root / "input" / "logic_c.luau";
    WriteText(script_a_source, SpeedScriptSource("'A'"));
    WriteText(script_b_source, SpeedScriptSource("'B'"));
    WriteText(script_c_source, SpeedScriptSource("'C'"));

    ASSERT_TRUE(Submit(MakeSceneRequest({
                         .source_path = model_path,
                         .cooked_root = cooked_root,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest({
                         .source_path = script_a_source,
                         .cooked_root = cooked_root,
                         .storage = ScriptStorageMode::kExternal,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest({
                         .source_path = script_b_source,
                         .cooked_root = cooked_root,
                         .storage = ScriptStorageMode::kExternal,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest({
                         .source_path = script_c_source,
                         .cooked_root = cooked_root,
                         .storage = ScriptStorageMode::kExternal,
                       }))
        .success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScene);
    const auto script_a
      = FindScriptAssetByDescriptorName(inspection_before, "logic_a.oscript");
    const auto script_b
      = FindScriptAssetByDescriptorName(inspection_before, "logic_b.oscript");
    const auto script_c
      = FindScriptAssetByDescriptorName(inspection_before, "logic_c.oscript");
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_a) << "Expected script a to be present";
    ASSERT_HAS_VALUE(script_b) << "Expected script b to be present";
    ASSERT_HAS_VALUE(script_c) << "Expected script c to be present";

    const auto base_scene_bytes
      = ReadCurrentDescriptor(cooked_root, scene_asset->key);
    auto base_scene = data::SceneAsset(scene_asset->key, base_scene_bytes);
    ASSERT_GT(base_scene.GetNodes().size(), 1U);

    const auto sidecar_source
      = cooked_root / "input" / "overwrite_additive.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec {
          .node_index = 0,
          .slot_id = std::string(kMainSlotId),
          .script_virtual_path
          = SpellReference(scene_asset->virtual_path, script_a->virtual_path),
          .execution_order = 1,
          .params = {},
        },
      }));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    WriteText(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec {
          .node_index = 0,
          .slot_id = std::string(kMainSlotId),
          .script_virtual_path
          = SpellReference(scene_asset->virtual_path, script_b->virtual_path),
          .execution_order = 2,
          .params = {},
        },
        SidecarBindingSpec {
          .node_index = 1,
          .slot_id = std::string(kAuxSlotId),
          .script_virtual_path
          = SpellReference(scene_asset->virtual_path, script_c->virtual_path),
          .execution_order = 3,
          .params = {},
        },
      }));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto script_state = ReadSceneScriptState(cooked_root, *scene_asset);
    const auto& slots = script_state.slots;

    const auto patched_scene_bytes
      = ReadCurrentDescriptor(cooked_root, scene_asset->key);
    auto patched_scene
      = data::SceneAsset(scene_asset->key, patched_scene_bytes);
    const auto components
      = patched_scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 2U);

    const auto node0_component = std::ranges::find_if(components,
      [](const auto& component) -> auto { return component.node_index == 0U; });
    const auto node1_component = std::ranges::find_if(components,
      [](const auto& component) -> auto { return component.node_index == 1U; });
    ASSERT_NE(node0_component, components.end());
    ASSERT_NE(node1_component, components.end());
    ASSERT_EQ(node0_component->slot_count, 1U);
    ASSERT_EQ(node1_component->slot_count, 1U);

    ASSERT_LT(node0_component->slot_start_index, slots.size());
    ASSERT_LT(node1_component->slot_start_index, slots.size());
    const auto& node0_slot = slots.at(node0_component->slot_start_index);
    const auto& node1_slot = slots.at(node1_component->slot_start_index);
    EXPECT_EQ(node0_slot.script_asset_key, script_b->key);
    EXPECT_EQ(node1_slot.script_asset_key, script_c->key);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    SidecarFailureIsAtomicAndDoesNotMutateOutputs)
  {

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_atomicity";
    const auto cooked = CookSceneWithScript({
      .cooked_root = cooked_root,
      .script_text = SpeedScriptSource("3"),
      .script_file_name = "stable.luau",
    });
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "atomic.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(SpellReference(
        scene_asset->virtual_path, script_asset->virtual_path))));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    const auto inspection_after_success = LoadInspection(cooked_root);

    const auto scene_before_failure
      = ReadCurrentDescriptor(cooked_root, scene_asset->key);

    WriteText(sidecar_source,
      MakeSidecarPayload(
        MakeSpeedBinding(SpellReference(scene_asset->virtual_path,
          CanonicalScriptVirtualPath("does_not_exist")))));
    const auto failure_report = Submit(MakeSidecarRequest({
      .source_path = sidecar_source,
      .cooked_root = cooked_root,
      .scene_virtual_path = scene_asset->virtual_path,
    }));
    ASSERT_FALSE(failure_report.success);
    EXPECT_TRUE(HasDiagnosticCode(failure_report.diagnostics,
      import_codes::kScriptSidecarScriptRefUnresolved));

    const auto scene_after_failure
      = ReadCurrentDescriptor(cooked_root, scene_asset->key);

    EXPECT_EQ(scene_before_failure, scene_after_failure);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    DescriptorWriteFailurePreservesPublishedSceneAndIndex)
  {

    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_table_write_atomicity";
    const auto cooked = CookSceneWithScript({
      .cooked_root = cooked_root,
      .script_text = SpeedScriptSource("9"),
      .script_file_name = "writer_fail.luau",
    });
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto scene_before
      = ReadCurrentDescriptor(cooked_root, scene_asset->key);
    ASSERT_FALSE(scene_before.empty());

    const auto sidecar_source
      = cooked_root / "input" / "writer_failure_sidecar.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(SpellReference(
        scene_asset->virtual_path, script_asset->virtual_path))));

    auto request = MakeSidecarRequest({
      .source_path = sidecar_source,
      .cooked_root = cooked_root,
      .scene_virtual_path = scene_asset->virtual_path,
    });
    // A refused write still ends in a commit of the writer's lease, so the
    // index bytes (its commit number) move; the listing it publishes must not.
    const auto listed_descriptors = [](const std::filesystem::path& root)
      -> std::vector<std::pair<data::AssetKey, std::filesystem::path>> {
      const auto content = LoadInspection(root);
      auto listed
        = std::vector<std::pair<data::AssetKey, std::filesystem::path>> {};
      for (const auto& asset : content.Assets()) {
        listed.emplace_back(
          asset.key, content.DescriptorStored(asset.key).file);
      }
      return listed;
    };
    const auto listing_before = listed_descriptors(cooked_root);
    auto inflight = MakeInflightSceneContext(cooked_root, *scene_asset);
    inflight.descriptor_relpath = "blocked-parent/scene.oscene";
    request.inflight_scene_contexts.push_back(std::move(inflight));
    WriteText(cooked_root / "blocked-parent", "not a directory");

    const auto report = Submit(std::move(request));
    ASSERT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, import_codes::kScriptSidecarSceneEmitFailed));
    EXPECT_EQ(
      scene_before, ReadCurrentDescriptor(cooked_root, scene_asset->key));
    EXPECT_EQ(listing_before, listed_descriptors(cooked_root));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportJobReimportTest, ReimportRebindsSlotToNewScriptKey)
  {
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_rebind";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_a_source = cooked_root / "input" / "script_a.luau";
    const auto script_b_source = cooked_root / "input" / "script_b.luau";
    WriteText(script_a_source, SpeedScriptSource("'A'"));
    WriteText(script_b_source, SpeedScriptSource("'B'"));

    ASSERT_TRUE(Submit(MakeSceneRequest({
                         .source_path = model_path,
                         .cooked_root = cooked_root,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest({
                         .source_path = script_a_source,
                         .cooked_root = cooked_root,
                         .storage = ScriptStorageMode::kExternal,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest({
                         .source_path = script_b_source,
                         .cooked_root = cooked_root,
                         .storage = ScriptStorageMode::kExternal,
                       }))
        .success);

    const auto before_inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(before_inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";

    auto script_a_asset = std::optional<AssetRef> {};
    auto script_b_asset = std::optional<AssetRef> {};
    for (const auto& asset : before_inspection.Assets()) {
      if (asset.type == AssetType::kScript) {
        const auto descriptor_name
          = DescriptorLayoutName(before_inspection, asset.key);
        if (descriptor_name == "script_a.oscript") {
          script_a_asset = AssetRef {
            .key = asset.key,
            .virtual_path = asset.virtual_path,
            .descriptor_relpath
            = DescriptorRelPath(before_inspection, asset.key),
            .references = asset.references,
            .type = AssetType::kScript,
          };
        } else if (descriptor_name == "script_b.oscript") {
          script_b_asset = AssetRef {
            .key = asset.key,
            .virtual_path = asset.virtual_path,
            .descriptor_relpath
            = DescriptorRelPath(before_inspection, asset.key),
            .references = asset.references,
            .type = AssetType::kScript,
          };
        }
      }
    }
    ASSERT_HAS_VALUE(script_a_asset) << "Expected script a asset to be present";
    ASSERT_HAS_VALUE(script_b_asset) << "Expected script b asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "rebind.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(SpellReference(
        scene_asset->virtual_path, script_a_asset->virtual_path))));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(SpellReference(
        scene_asset->virtual_path, script_b_asset->virtual_path))));
    const auto second_report = Submit(MakeSidecarRequest({
      .source_path = sidecar_source,
      .cooked_root = cooked_root,
      .scene_virtual_path = scene_asset->virtual_path,
    }));
    ASSERT_TRUE(second_report.success);

    const auto after_inspection = LoadInspection(cooked_root);

    const auto script_state = ReadSceneScriptState(cooked_root, *scene_asset);
    const auto& slots = script_state.slots;
    ASSERT_EQ(slots.size(), 1U);
    EXPECT_EQ(slots.at(0).script_asset_key, script_b_asset->key);

    const auto scene_bytes
      = ReadCurrentDescriptor(cooked_root, scene_asset->key);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(scene_asset->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components.front().slot_count, 1U);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportJobReimportTest, ReimportWithSamePayloadIsIdempotent)
  {

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_idempotent";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "idempotent_script.luau";
    WriteText(script_source, SpeedScriptSource("'stable'"));

    ASSERT_TRUE(Submit(MakeSceneRequest({
                         .source_path = model_path,
                         .cooked_root = cooked_root,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest({
                         .source_path = script_source,
                         .cooked_root = cooked_root,
                         .storage = ScriptStorageMode::kExternal,
                       }))
        .success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(inspection, AssetType::kScript);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "idempotent.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(SpellReference(
        scene_asset->virtual_path, script_asset->virtual_path))));

    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    const auto first_inspection = LoadInspection(cooked_root);

    const auto first_scene_bytes
      = ReadCurrentDescriptor(cooked_root, scene_asset->key);

    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    const auto second_scene_bytes
      = ReadCurrentDescriptor(cooked_root, scene_asset->key);

    EXPECT_EQ(first_scene_bytes, second_scene_bytes);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    ScenePatchPreservesNodeAndStringTables)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_scene_patch_preserves_core_tables";
    const auto cooked = CookSceneWithScript({
      .cooked_root = cooked_root,
      .script_text = SpeedScriptSource("1"),
      .script_file_name = "patch_guard.luau",
    });
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    // The scene's node table and string table, as the product reads them.
    const auto ExtractCoreTables = [](const std::vector<std::byte>& bytes)
      -> std::pair<std::vector<std::byte>, std::vector<std::byte>> {
      const auto scene = data::SceneAsset(data::AssetKey {}, bytes);
      const auto nodes = std::as_bytes(scene.GetNodes());
      const auto strings = std::as_bytes(scene.GetStrings().Bytes());
      return { std::vector<std::byte>(nodes.begin(), nodes.end()),
        std::vector<std::byte>(strings.begin(), strings.end()) };
    };

    const auto core_before
      = ExtractCoreTables(ReadCurrentDescriptor(cooked_root, scene_asset->key));

    const auto sidecar_source
      = cooked_root / "input" / "patch_guard_sidecar.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(SpellReference(
        scene_asset->virtual_path, script_asset->virtual_path))));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    const auto core_after
      = ExtractCoreTables(ReadCurrentDescriptor(cooked_root, scene_asset->key));

    // Nodes are untouched; the slot parameters' strings join the string
    // table after the strings already there, which keep their offsets.
    EXPECT_EQ(core_before.first, core_after.first);
    ASSERT_GE(core_after.second.size(), core_before.second.size());
    EXPECT_TRUE(std::ranges::equal(core_before.second,
      std::span(core_after.second).first(core_before.second.size())));
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    ReimportWithSameSlotShapePreservesDescriptorSize)
  {
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_in_place_update";
    const auto cooked = CookSceneWithScript({
      .cooked_root = cooked_root,
      .script_text = SpeedScriptSource("1"),
      .script_file_name = "in_place.luau",
    });
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "in_place.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(
        SpellReference(scene_asset->virtual_path, script_asset->virtual_path),
        1.0F)));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    const auto first_size
      = ReadCurrentDescriptor(cooked_root, scene_asset->key).size();

    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(
        SpellReference(scene_asset->virtual_path, script_asset->virtual_path),
        kUpdatedSpeed)));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_source,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_asset->virtual_path,
                       }))
        .success);

    EXPECT_EQ(
      ReadCurrentDescriptor(cooked_root, scene_asset->key).size(), first_size);
    const auto script_state = ReadSceneScriptState(cooked_root, *scene_asset);
    const auto& slots = script_state.slots;
    const auto& params = script_state.params;
    ASSERT_EQ(slots.size(), 1U);
    ASSERT_EQ(slots.at(0).params_count, 1U);
    const auto param_index = static_cast<size_t>(
      (slots.at(0).params_array_offset - script_state.parameter_base)
      / sizeof(ScriptParamRecord));
    ASSERT_LT(param_index, params.size());
    EXPECT_FLOAT_EQ(params.at(param_index).value.as_float, 9.5F);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    ReimportWithExpandedParamsPreservesOtherSceneSlotRanges)
  {
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_expand_preserve_other_scene";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto input_dir = cooked_root / "input";
    std::filesystem::create_directories(input_dir);
    const auto scene_a_source = input_dir / "scene_a.glb";
    const auto scene_b_source = input_dir / "scene_b.glb";
    std::filesystem::copy_file(model_path, scene_a_source);
    std::filesystem::copy_file(model_path, scene_b_source);

    const auto script_source = input_dir / "shared.luau";
    WriteText(script_source,
      "local Script = oxygen.script {\n"
      "  params = {\n"
      "    speed = oxygen.param.float {},\n"
      "    enabled = oxygen.param.bool {},\n"
      "  },\n"
      "}\n"
      "return Script\n");
    ASSERT_TRUE(Submit(MakeSceneRequest({
                         .source_path = scene_a_source,
                         .cooked_root = cooked_root,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest({
                         .source_path = scene_b_source,
                         .cooked_root = cooked_root,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest({
                         .source_path = script_source,
                         .cooked_root = cooked_root,
                         .storage = ScriptStorageMode::kExternal,
                       }))
        .success);

    const auto inspection = LoadInspection(cooked_root);
    auto scene_a = std::optional<AssetRef> {};
    auto scene_b = std::optional<AssetRef> {};
    auto script_asset = std::optional<AssetRef> {};
    for (const auto& entry : inspection.Assets()) {
      const auto type = entry.type;
      if (type == AssetType::kScript) {
        script_asset = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = DescriptorRelPath(inspection, entry.key),
          .references = entry.references,
          .type = type,
        };
        continue;
      }
      if (type != AssetType::kScene) {
        continue;
      }
      const auto descriptor_name = DescriptorLayoutName(inspection, entry.key);
      if (descriptor_name == "scene_a.oscene") {
        scene_a = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = DescriptorRelPath(inspection, entry.key),
          .references = entry.references,
          .type = type,
        };
      } else if (descriptor_name == "scene_b.oscene") {
        scene_b = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = DescriptorRelPath(inspection, entry.key),
          .references = entry.references,
          .type = type,
        };
      }
    }
    ASSERT_HAS_VALUE(scene_a) << "Expected scene a to be present";
    ASSERT_HAS_VALUE(scene_b) << "Expected scene b to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_a = input_dir / "scene_a.json";
    const auto sidecar_b = input_dir / "scene_b.json";
    WriteText(sidecar_a,
      MakeSidecarPayload(MakeSpeedBinding(
        SpellReference(scene_a->virtual_path, script_asset->virtual_path),
        1.0F)));
    WriteText(sidecar_b,
      MakeSidecarPayload(MakeSpeedBinding(
        SpellReference(scene_b->virtual_path, script_asset->virtual_path),
        kSceneBSpeed)));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_a,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_a->virtual_path,
                       }))
        .success);
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_b,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_b->virtual_path,
                       }))
        .success);

    const auto ReadSlotStartForNodeZero
      = [&](const AssetRef& scene_ref) -> auto {
      const auto scene_bytes
        = ReadCurrentDescriptor(cooked_root, scene_ref.key);
      auto scene = data::SceneAsset(scene_ref.key, scene_bytes);
      const auto components = scene.GetComponents<ScriptingComponentRecord>();
      const auto component
        = std::ranges::find_if(components, [](const auto& candidate) -> auto {
            return candidate.node_index == 0U;
          });
      EXPECT_NE(component, components.end());
      return component == components.end() ? 0U : component->slot_start_index;
    };

    const auto scene_b_slot_start_before = ReadSlotStartForNodeZero(*scene_b);

    WriteText(sidecar_a,
      MakeSidecarPayload(SidecarBindingSpec {
        .node_index = 0,
        .slot_id = std::string(kMainSlotId),
        .script_virtual_path
        = SpellReference(scene_a->virtual_path, script_asset->virtual_path),
        .execution_order = 2,
        .params = nlohmann::json::parse(R"([
        { "key": "speed", "type": "float", "value": 2.0 },
        { "key": "enabled", "type": "bool", "value": true }
      ])"),
      }));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar_a,
                         .cooked_root = cooked_root,
                         .scene_virtual_path = scene_a->virtual_path,
                       }))
        .success);

    const auto scene_b_slot_start_after = ReadSlotStartForNodeZero(*scene_b);
    EXPECT_EQ(scene_b_slot_start_before, scene_b_slot_start_after);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto script_state = ReadSceneScriptState(cooked_root, *scene_b);
    const auto& slots = script_state.slots;
    const auto& params = script_state.params;
    ASSERT_FALSE(slots.empty());
    ASSERT_FALSE(params.empty());

    const auto scene_b_bytes = ReadCurrentDescriptor(cooked_root, scene_b->key);
    auto scene_b_asset = data::SceneAsset(scene_b->key, scene_b_bytes);
    const auto components
      = scene_b_asset.GetComponents<ScriptingComponentRecord>();
    const auto component = std::ranges::find_if(components,
      [](const auto& candidate) -> auto { return candidate.node_index == 0U; });
    ASSERT_NE(component, components.end());
    const auto slot_index = static_cast<size_t>(component->slot_start_index);
    ASSERT_LT(slot_index, slots.size());
    const auto& slot = slots.at(slot_index);
    const auto param_index = static_cast<size_t>(
      (slot.params_array_offset - script_state.parameter_base)
      / sizeof(ScriptParamRecord));
    ASSERT_LT(param_index, params.size());
    EXPECT_FLOAT_EQ(params.at(param_index).value.as_float, kSceneBSpeed);
  }

} // namespace

} // namespace oxygen::content::import::test
