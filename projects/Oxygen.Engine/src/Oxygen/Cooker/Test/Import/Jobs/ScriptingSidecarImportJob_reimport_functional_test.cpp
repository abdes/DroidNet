//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/ScriptingSidecarImportJob.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "ScriptImportTestSupport.h"
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  class ScriptingSidecarImportJobReimportTest : public ScriptingImportTestBase {
  };

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    NodeBindingOverwriteAndAdditiveUpdateBehavior)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_overwrite_additive";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_a_source = cooked_root / "input" / "logic_a.luau";
    const auto script_b_source = cooked_root / "input" / "logic_b.luau";
    const auto script_c_source = cooked_root / "input" / "logic_c.luau";
    WriteText(script_a_source, "return 'A'");
    WriteText(script_b_source, "return 'B'");
    WriteText(script_c_source, "return 'C'");

    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_a_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_b_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_c_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
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
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);
    auto base_scene = data::SceneAsset(scene_asset->key, base_scene_bytes);
    ASSERT_GT(base_scene.GetNodes().size(), 1U);

    const auto sidecar_source
      = cooked_root / "input" / "overwrite_additive.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec {
          .node_index = 0,
          .slot_id = "main",
          .script_virtual_path = script_a->virtual_path,
          .execution_order = 1,
        },
      }));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    WriteText(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec {
          .node_index = 0,
          .slot_id = "main",
          .script_virtual_path = script_b->virtual_path,
          .execution_order = 2,
        },
        SidecarBindingSpec {
          .node_index = 1,
          .slot_id = "aux",
          .script_virtual_path = script_c->virtual_path,
          .execution_order = 3,
        },
      }));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto script_state = ReadSceneScriptState(cooked_root, *scene_asset);
    const auto& slots = script_state.slots;

    const auto patched_scene_bytes
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);
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
    using data::loose_cooked::FileKind;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_atomicity";
    const auto cooked
      = CookSceneWithScript(cooked_root, "return 3", "stable.luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / "atomic.sidescript.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto inspection_after_success = LoadInspection(cooked_root);

    const auto scene_before_failure
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);

    WriteText(sidecar_source,
      MakeSidecarPayload(
        MakeSpeedBinding(CanonicalScriptVirtualPath("does_not_exist"))));
    const auto failure_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(failure_report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      failure_report.diagnostics, "script.sidecar.script_ref_unresolved"));

    const auto scene_after_failure
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);

    EXPECT_EQ(scene_before_failure, scene_after_failure);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    DescriptorWriteFailurePreservesPublishedSceneAndIndex)
  {
    using data::loose_cooked::FileKind;

    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_table_write_atomicity";
    const auto cooked
      = CookSceneWithScript(cooked_root, "return 9", "writer_fail.luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto scene_before
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_FALSE(scene_before.empty());

    const auto sidecar_source
      = cooked_root / "input" / "writer_failure_sidecar.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    auto request = MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path);
    const auto index_before = ReadBytes(cooked_root / "container.index.bin");
    auto inflight = MakeInflightSceneContext(cooked_root, *scene_asset);
    inflight.descriptor_relpath = "blocked-parent/scene.oscene";
    request.inflight_scene_contexts.push_back(std::move(inflight));
    WriteText(cooked_root / "blocked-parent", "not a directory");

    const auto report = Submit(std::move(request));
    ASSERT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.scene_emit_failed"));
    EXPECT_EQ(
      scene_before, ReadBytes(cooked_root / scene_asset->descriptor_relpath));
    EXPECT_EQ(index_before, ReadBytes(cooked_root / "container.index.bin"));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportJobReimportTest, ReimportRebindsSlotToNewScriptKey)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_rebind";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_a_source = cooked_root / "input" / "script_a.luau";
    const auto script_b_source = cooked_root / "input" / "script_b.luau";
    WriteText(script_a_source, "return 'A'");
    WriteText(script_b_source, "return 'B'");

    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_a_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_b_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto before_inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(before_inspection, AssetType::kScene);
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";

    auto script_a_asset = std::optional<AssetRef> {};
    auto script_b_asset = std::optional<AssetRef> {};
    for (const auto& asset : before_inspection.Assets()) {
      if (static_cast<AssetType>(asset.asset_type) == AssetType::kScript) {
        const auto descriptor_name
          = std::filesystem::path(asset.descriptor_relpath).filename().string();
        if (descriptor_name == "script_a.oscript") {
          script_a_asset = AssetRef {
            .key = asset.key,
            .virtual_path = asset.virtual_path,
            .descriptor_relpath = asset.descriptor_relpath,
            .references = asset.references,
            .type = AssetType::kScript,
          };
        } else if (descriptor_name == "script_b.oscript") {
          script_b_asset = AssetRef {
            .key = asset.key,
            .virtual_path = asset.virtual_path,
            .descriptor_relpath = asset.descriptor_relpath,
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
      MakeSidecarPayload(MakeSpeedBinding(script_a_asset->virtual_path)));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_b_asset->virtual_path)));
    const auto second_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(second_report.success);

    const auto after_inspection = LoadInspection(cooked_root);

    const auto script_state = ReadSceneScriptState(cooked_root, *scene_asset);
    const auto& slots = script_state.slots;
    ASSERT_EQ(slots.size(), 1U);
    EXPECT_EQ(slots.at(0).script_asset_key, script_b_asset->key);

    const auto scene_bytes
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(scene_asset->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components.front().slot_count, 1U);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportJobReimportTest, ReimportWithSamePayloadIsIdempotent)
  {
    using data::loose_cooked::FileKind;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_idempotent";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "idempotent_script.luau";
    WriteText(script_source, "return 'stable'");

    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
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
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));

    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto first_inspection = LoadInspection(cooked_root);

    const auto first_scene_bytes
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);

    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto second_scene_bytes
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);

    EXPECT_EQ(first_scene_bytes, second_scene_bytes);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    ScenePatchPreservesNodeAndStringTables)
  {
    using data::pak::world::SceneAssetDesc;

    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_scene_patch_preserves_core_tables";
    const auto cooked
      = CookSceneWithScript(cooked_root, "return 1", "patch_guard.luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto before_scene_bytes
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_GE(before_scene_bytes.size(), sizeof(SceneAssetDesc));

    const auto ExtractCoreTables = [](const std::vector<std::byte>& bytes)
      -> std::optional<
        std::pair<std::vector<std::byte>, std::vector<std::byte>>> {
      if (bytes.size() < sizeof(SceneAssetDesc)) {
        return std::nullopt;
      }
      auto desc = SceneAssetDesc {};
      std::memcpy(&desc, bytes.data(), sizeof(desc));
      const auto range_ok
        = [&](const uint64_t offset, const uint64_t size) -> bool {
        return offset <= bytes.size() && size <= (bytes.size() - offset);
      };

      const auto node_size = static_cast<uint64_t>(desc.nodes.count)
        * static_cast<uint64_t>(desc.nodes.entry_size);
      const auto string_size = static_cast<uint64_t>(desc.scene_strings.size);
      if (!range_ok(desc.nodes.offset, node_size)
        || !range_ok(desc.scene_strings.offset, string_size)) {
        return std::nullopt;
      }

      auto nodes = std::vector<std::byte> {};
      nodes.resize(static_cast<size_t>(node_size));
      if (!nodes.empty()) {
        std::memcpy(nodes.data(),
          bytes.data() + static_cast<size_t>(desc.nodes.offset), nodes.size());
      }

      auto strings = std::vector<std::byte> {};
      strings.resize(static_cast<size_t>(string_size));
      if (!strings.empty()) {
        std::memcpy(strings.data(),
          bytes.data() + static_cast<size_t>(desc.scene_strings.offset),
          strings.size());
      }
      return std::make_optional(
        std::make_pair(std::move(nodes), std::move(strings)));
    };

    const auto core_before = ExtractCoreTables(before_scene_bytes);
    ASSERT_HAS_VALUE(core_before) << "Expected core before to be present";

    const auto sidecar_source
      = cooked_root / "input" / "patch_guard_sidecar.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path)));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto after_scene_bytes
      = ReadBytes(cooked_root / scene_asset->descriptor_relpath);
    const auto core_after = ExtractCoreTables(after_scene_bytes);
    ASSERT_HAS_VALUE(core_after) << "Expected core after to be present";

    EXPECT_EQ(core_before->first, core_after->first);
    EXPECT_EQ(core_before->second, core_after->second);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobReimportTest,
    ReimportWithSameSlotShapePreservesDescriptorSize)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_in_place_update";
    const auto cooked
      = CookSceneWithScript(cooked_root, "return 1", "in_place.luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source = cooked_root / "input" / "in_place.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path, 1.0F)));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto first_size = std::filesystem::file_size(
      cooked_root / scene_asset->descriptor_relpath);

    WriteText(sidecar_source,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path, 9.5F)));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    EXPECT_EQ(
      std::filesystem::file_size(cooked_root / scene_asset->descriptor_relpath),
      first_size);
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
    using data::loose_cooked::FileKind;
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
    WriteText(script_source, "return 7");
    ASSERT_TRUE(Submit(MakeSceneRequest(scene_a_source, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeSceneRequest(scene_b_source, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection = LoadInspection(cooked_root);
    auto scene_a = std::optional<AssetRef> {};
    auto scene_b = std::optional<AssetRef> {};
    auto script_asset = std::optional<AssetRef> {};
    for (const auto& entry : inspection.Assets()) {
      const auto type = static_cast<AssetType>(entry.asset_type);
      if (type == AssetType::kScript) {
        script_asset = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = type,
        };
        continue;
      }
      if (type != AssetType::kScene) {
        continue;
      }
      const auto descriptor_name
        = std::filesystem::path(entry.descriptor_relpath).filename().string();
      if (descriptor_name == "scene_a.oscene") {
        scene_a = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = type,
        };
      } else if (descriptor_name == "scene_b.oscene") {
        scene_b = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
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
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path, 1.0F)));
    WriteText(sidecar_b,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path, 7.5F)));
    ASSERT_TRUE(
      Submit(MakeSidecarRequest(sidecar_a, cooked_root, scene_a->virtual_path))
        .success);
    ASSERT_TRUE(
      Submit(MakeSidecarRequest(sidecar_b, cooked_root, scene_b->virtual_path))
        .success);

    const auto ReadSlotStartForNodeZero
      = [&](const AssetRef& scene_ref) -> auto {
      const auto scene_bytes
        = ReadBytes(cooked_root / scene_ref.descriptor_relpath);
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
        .script_virtual_path = script_asset->virtual_path,
        .execution_order = 2,
        .params = nlohmann::json::parse(R"([
        { "key": "speed", "type": "float", "value": 2.0 },
        { "key": "enabled", "type": "bool", "value": true }
      ])"),
      }));
    ASSERT_TRUE(
      Submit(MakeSidecarRequest(sidecar_a, cooked_root, scene_a->virtual_path))
        .success);

    const auto scene_b_slot_start_after = ReadSlotStartForNodeZero(*scene_b);
    EXPECT_EQ(scene_b_slot_start_before, scene_b_slot_start_after);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto script_state = ReadSceneScriptState(cooked_root, *scene_b);
    const auto& slots = script_state.slots;
    const auto& params = script_state.params;
    ASSERT_FALSE(slots.empty());
    ASSERT_FALSE(params.empty());

    const auto scene_b_bytes
      = ReadBytes(cooked_root / scene_b->descriptor_relpath);
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
    EXPECT_FLOAT_EQ(params.at(param_index).value.as_float, 7.5F);
  }

} // namespace

} // namespace oxygen::content::import::test
