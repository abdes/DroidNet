//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/ScriptingSidecarImportJob.cpp

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <latch>
#include <optional>
#include <string>

#include "ScriptImportTestSupport.h"
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  class ScriptingSidecarImportJobParamsTest : public ScriptingImportTestBase {
  };

  NOLINT_TEST_F(ScriptingSidecarImportJobParamsTest,
    SequentialSidecarImportsKeepDistinctParamsAcrossScenes)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_multiscene_distinct_params";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto input_dir = cooked_root / "input";
    std::filesystem::create_directories(input_dir);
    const auto scene_a_source = input_dir / "scene_a.glb";
    const auto scene_b_source = input_dir / "scene_b.glb";
    std::filesystem::copy_file(model_path, scene_a_source);
    std::filesystem::copy_file(model_path, scene_b_source);

    const auto script_source = input_dir / "rotate_shared.luau";
    WriteText(script_source, "return 1");

    ASSERT_TRUE(Submit(MakeSceneRequest(scene_a_source, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeSceneRequest(scene_b_source, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto script_asset = FindScriptAssetByDescriptorName(
      inspection_before, "rotate_shared.oscript");
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    auto scene_a = std::optional<AssetRef> {};
    auto scene_b = std::optional<AssetRef> {};
    for (const auto& entry : inspection_before.Assets()) {
      if (static_cast<AssetType>(entry.asset_type) != AssetType::kScene) {
        continue;
      }
      const auto descriptor_name
        = std::filesystem::path(entry.descriptor_relpath).filename().string();
      if (descriptor_name == "scene_a.oscene") {
        scene_a = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .references = entry.references,
          .type = AssetType::kScene,
        };
      } else if (descriptor_name == "scene_b.oscene") {
        scene_b = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .references = entry.references,
          .type = AssetType::kScene,
        };
      }
    }
    ASSERT_HAS_VALUE(scene_a) << "Expected scene a to be present";
    ASSERT_HAS_VALUE(scene_b) << "Expected scene b to be present";

    const auto sidecar_a = input_dir / "scene_a.sidescript.json";
    const auto sidecar_b = input_dir / "scene_b.sidescript.json";
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

    const auto inspection_after = LoadInspection(cooked_root);

    const auto ReadSpeedForScene = [&](const AssetRef& scene_ref) -> float {
      const auto script_state = ReadSceneScriptState(cooked_root, scene_ref);
      const auto& slots = script_state.slots;
      const auto& params = script_state.params;
      if (slots.empty() || params.empty()) {
        ADD_FAILURE() << "Expected script parameters in this scene";
        return 0.0F;
      }
      const auto scene_bytes
        = ReadBytes(cooked_root / scene_ref.descriptor_relpath);
      EXPECT_FALSE(scene_bytes.empty());
      if (scene_bytes.empty()) {
        return 0.0F;
      }
      auto scene = data::SceneAsset(scene_ref.key, scene_bytes);
      const auto components = scene.GetComponents<ScriptingComponentRecord>();
      const auto component
        = std::ranges::find_if(components, [](const auto& candidate) -> auto {
            return candidate.node_index == 0U;
          });
      EXPECT_NE(component, components.end());
      if (component == components.end()) {
        return 0.0F;
      }
      const auto slot_index = static_cast<size_t>(component->slot_start_index);
      EXPECT_LT(slot_index, slots.size());
      if (slot_index >= slots.size()) {
        return 0.0F;
      }
      const auto& slot = slots.at(slot_index);
      EXPECT_EQ((slot.params_array_offset - script_state.parameter_base)
          % sizeof(ScriptParamRecord),
        0U);
      const auto param_index = static_cast<size_t>(
        (slot.params_array_offset - script_state.parameter_base)
        / sizeof(ScriptParamRecord));
      EXPECT_GT(slot.params_count, 0U);
      EXPECT_LT(param_index, params.size());
      if (slot.params_count == 0U || param_index >= params.size()) {
        return 0.0F;
      }
      return params.at(param_index).value.as_float;
    };

    EXPECT_FLOAT_EQ(ReadSpeedForScene(*scene_a), 1.0F);
    EXPECT_FLOAT_EQ(ReadSpeedForScene(*scene_b), 7.5F);
  }

  NOLINT_TEST_F(ScriptingSidecarImportJobParamsTest,
    ConcurrentSidecarImportsKeepDistinctParamsAcrossScenes)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    auto config = AsyncImportService::Config {};
    config.thread_pool_size = 2;
    config.max_in_flight_jobs = 2;
    auto service = ScopedImportService(config);

    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_sidecar_multiscene_distinct_params_parallel";
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto input_dir = cooked_root / "input";
    std::filesystem::create_directories(input_dir);
    const auto scene_a_source = input_dir / "scene_a.glb";
    const auto scene_b_source = input_dir / "scene_b.glb";
    std::filesystem::copy_file(model_path, scene_a_source);
    std::filesystem::copy_file(model_path, scene_b_source);

    const auto script_source = input_dir / "rotate_shared.luau";
    WriteText(script_source, "return 1");

    const auto scene_a_report = SubmitAndWait(
      service.Service(), MakeSceneRequest(scene_a_source, cooked_root));
    ASSERT_HAS_VALUE(scene_a_report);
    ASSERT_TRUE(scene_a_report->success);
    const auto scene_b_report = SubmitAndWait(
      service.Service(), MakeSceneRequest(scene_b_source, cooked_root));
    ASSERT_HAS_VALUE(scene_b_report);
    ASSERT_TRUE(scene_b_report->success);
    const auto script_report = SubmitAndWait(service.Service(),
      MakeScriptRequest(
        script_source, cooked_root, ScriptStorageMode::kExternal, false));
    ASSERT_HAS_VALUE(script_report);
    ASSERT_TRUE(script_report->success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto script_asset = FindScriptAssetByDescriptorName(
      inspection_before, "rotate_shared.oscript");
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    auto scene_a = std::optional<AssetRef> {};
    auto scene_b = std::optional<AssetRef> {};
    for (const auto& entry : inspection_before.Assets()) {
      if (static_cast<AssetType>(entry.asset_type) != AssetType::kScene) {
        continue;
      }
      const auto descriptor_name
        = std::filesystem::path(entry.descriptor_relpath).filename().string();
      if (descriptor_name == "scene_a.oscene") {
        scene_a = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .references = entry.references,
          .type = AssetType::kScene,
        };
      } else if (descriptor_name == "scene_b.oscene") {
        scene_b = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .references = entry.references,
          .type = AssetType::kScene,
        };
      }
    }
    ASSERT_HAS_VALUE(scene_a) << "Expected scene a to be present";
    ASSERT_HAS_VALUE(scene_b) << "Expected scene b to be present";

    const auto sidecar_a = input_dir / "scene_a_parallel.sidescript.json";
    const auto sidecar_b = input_dir / "scene_b_parallel.sidescript.json";
    WriteText(sidecar_a,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path, 1.0F)));
    WriteText(sidecar_b,
      MakeSidecarPayload(MakeSpeedBinding(script_asset->virtual_path, 7.5F)));

    auto report_a = ImportReport {};
    auto report_b = ImportReport {};
    std::latch done(2);

    const auto submit_a = service.Service().SubmitImport(
      MakeSidecarRequest(sidecar_a, cooked_root, scene_a->virtual_path),
      [&report_a, &done](
        const auto /*job_id*/, const ImportReport& report) -> auto {
        report_a = report;
        done.count_down();
      });
    if (!submit_a.has_value()) {
      FAIL() << "Expected submit a to be present";
    }

    const auto submit_b = service.Service().SubmitImport(
      MakeSidecarRequest(sidecar_b, cooked_root, scene_b->virtual_path),
      [&report_b, &done](
        const auto /*job_id*/, const ImportReport& report) -> auto {
        report_b = report;
        done.count_down();
      });
    if (!submit_b.has_value()) {
      FAIL() << "Expected submit b to be present";
    }

    done.wait();
    ASSERT_TRUE(report_a.success);
    ASSERT_TRUE(report_b.success);

    const auto inspection_after = LoadInspection(cooked_root);

    const auto ReadSpeedForScene = [&](const AssetRef& scene_ref) -> float {
      const auto script_state = ReadSceneScriptState(cooked_root, scene_ref);
      const auto& slots = script_state.slots;
      const auto& params = script_state.params;
      if (slots.empty() || params.empty()) {
        ADD_FAILURE() << "Expected script parameters in this scene";
        return 0.0F;
      }
      const auto scene_bytes
        = ReadBytes(cooked_root / scene_ref.descriptor_relpath);
      EXPECT_FALSE(scene_bytes.empty());
      if (scene_bytes.empty()) {
        return 0.0F;
      }
      auto scene = data::SceneAsset(scene_ref.key, scene_bytes);
      const auto components = scene.GetComponents<ScriptingComponentRecord>();
      const auto component
        = std::ranges::find_if(components, [](const auto& candidate) -> auto {
            return candidate.node_index == 0U;
          });
      EXPECT_NE(component, components.end());
      if (component == components.end()) {
        return 0.0F;
      }
      const auto slot_index = static_cast<size_t>(component->slot_start_index);
      EXPECT_LT(slot_index, slots.size());
      if (slot_index >= slots.size()) {
        return 0.0F;
      }
      const auto& slot = slots.at(slot_index);
      EXPECT_EQ((slot.params_array_offset - script_state.parameter_base)
          % sizeof(ScriptParamRecord),
        0U);
      const auto param_index = static_cast<size_t>(
        (slot.params_array_offset - script_state.parameter_base)
        / sizeof(ScriptParamRecord));
      EXPECT_GT(slot.params_count, 0U);
      EXPECT_LT(param_index, params.size());
      if (slot.params_count == 0U || param_index >= params.size()) {
        return 0.0F;
      }
      return params.at(param_index).value.as_float;
    };

    EXPECT_FLOAT_EQ(ReadSpeedForScene(*scene_a), 1.0F);
    EXPECT_FLOAT_EQ(ReadSpeedForScene(*scene_b), 7.5F);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportJobParamsTest, SidecarParsesAllSupportedParamTypes)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptParamType;
    using data::pak::scripting::ScriptSlotRecord;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_all_param_types";
    const auto cooked
      = CookSceneWithScript(cooked_root, "return 123", "param_types.luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    constexpr auto kParamsJson = R"([
        { "key": "enabled", "type": "bool", "value": true },
        { "key": "lives", "type": "int32", "value": 42 },
        { "key": "speed", "type": "float", "value": 3.5 },
        { "key": "title", "type": "string", "value": "agent" },
        { "key": "uv", "type": "vec2", "value": [1.0, 2.0] },
        { "key": "dir", "type": "vec3", "value": [3.0, 4.0, 5.0] },
        { "key": "quat", "type": "vec4", "value": [6.0, 7.0, 8.0, 9.0] }
      ])";

    const auto sidecar_source = cooked_root / "input" / "params_ok.json";
    WriteText(sidecar_source,
      MakeSidecarPayload(SidecarBindingSpec {
        .script_virtual_path = script_asset->virtual_path,
        .execution_order = 2,
        .params = nlohmann::json::parse(kParamsJson),
      }));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(report.success);

    const auto inspection_after = LoadInspection(cooked_root);

    const auto script_state = ReadSceneScriptState(cooked_root, *scene_asset);
    const auto& slots = script_state.slots;
    const auto& params = script_state.params;
    ASSERT_EQ(slots.size(), 1U);
    ASSERT_EQ(slots.at(0).params_count, 7U);
    ASSERT_EQ((slots.at(0).params_array_offset - script_state.parameter_base)
        % sizeof(ScriptParamRecord),
      0U);

    const auto param_start = static_cast<size_t>(
      (slots.at(0).params_array_offset - script_state.parameter_base)
      / sizeof(ScriptParamRecord));
    ASSERT_LE(param_start + slots.at(0).params_count, params.size());
    const auto ParamAt = [&](const size_t i) -> const ScriptParamRecord& {
      return params.at(param_start + i);
    };

    EXPECT_EQ(ParamAt(0).type, ScriptParamType::kBool);
    EXPECT_TRUE(ParamAt(0).value.as_bool);
    EXPECT_EQ(ParamAt(1).type, ScriptParamType::kInt32);
    EXPECT_EQ(ParamAt(1).value.as_int32, 42);
    EXPECT_EQ(ParamAt(2).type, ScriptParamType::kFloat);
    EXPECT_FLOAT_EQ(ParamAt(2).value.as_float, 3.5F);
    EXPECT_EQ(ParamAt(3).type, ScriptParamType::kString);
    EXPECT_STREQ(ParamAt(3).value.as_string, "agent");
    EXPECT_EQ(ParamAt(4).type, ScriptParamType::kVec2);
    EXPECT_FLOAT_EQ(ParamAt(4).value.as_vec[0], 1.0F);
    EXPECT_FLOAT_EQ(ParamAt(4).value.as_vec[1], 2.0F);
    EXPECT_EQ(ParamAt(5).type, ScriptParamType::kVec3);
    EXPECT_FLOAT_EQ(ParamAt(5).value.as_vec[0], 3.0F);
    EXPECT_FLOAT_EQ(ParamAt(5).value.as_vec[1], 4.0F);
    EXPECT_FLOAT_EQ(ParamAt(5).value.as_vec[2], 5.0F);
    EXPECT_EQ(ParamAt(6).type, ScriptParamType::kVec4);
    EXPECT_FLOAT_EQ(ParamAt(6).value.as_vec[0], 6.0F);
    EXPECT_FLOAT_EQ(ParamAt(6).value.as_vec[1], 7.0F);
    EXPECT_FLOAT_EQ(ParamAt(6).value.as_vec[2], 8.0F);
    EXPECT_FLOAT_EQ(ParamAt(6).value.as_vec[3], 9.0F);
  }

  //! One invalid sidecar parameter payload.
  struct RejectedParamCase final {
    const char* name;
    const char* script_stem;
    std::string params_json;
  };

  class ScriptingSidecarRejectedParamTest
    : public ScriptingSidecarImportJobParamsTest,
      public ::testing::WithParamInterface<RejectedParamCase> { };

  NOLINT_TEST_P(ScriptingSidecarRejectedParamTest, SidecarRejectsParam)
  {
    const auto& param = GetParam();
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_sidecar_rejected_param";
    const auto cooked = CookSceneWithScript(
      cooked_root, "return 123", std::string(param.script_stem) + ".luau");
    const auto& scene_asset = cooked.scene;
    const auto& script_asset = cooked.script;
    ASSERT_HAS_VALUE(scene_asset) << "Expected scene asset to be present";
    ASSERT_HAS_VALUE(script_asset) << "Expected script asset to be present";

    const auto sidecar_source
      = cooked_root / "input" / (std::string(param.script_stem) + ".json");
    WriteText(sidecar_source,
      MakeSidecarPayload(SidecarBindingSpec {
        .script_virtual_path = script_asset->virtual_path,
        .execution_order = 2,
        .params = nlohmann::json::parse(param.params_json),
      }));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.sidecar.param_invalid"));
  }

  INSTANTIATE_TEST_SUITE_P(InvalidParams, ScriptingSidecarRejectedParamTest,
    ::testing::Values(
      RejectedParamCase { "UnsupportedParamType", "unsupported_param",
        R"([ { "key": "bad", "type": "uint32", "value": 1 } ])" },
      RejectedParamCase { "OutOfRangeInt32Param", "int32_range",
        R"([ { "key": "bad", "type": "int32", "value": 2147483648 } ])" },
      RejectedParamCase { "InvalidVectorParamShape", "vec_shape",
        R"([ { "key": "bad", "type": "vec3", "value": [1.0, 2.0] } ])" },
      RejectedParamCase { "TooLongStringParam", "long_string",
        std::string(
          "[ { \"key\": \"name\", \"type\": \"string\", \"value\": \"")
          + std::string(60U, 'x') + "\" } ]" }),
    [](const ::testing::TestParamInfo<RejectedParamCase>& info) -> std::string {
      return std::string(info.param.name);
    });

} // namespace

} // namespace oxygen::content::import::test
