//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define IMGUI_DEFINE_MATH_OPERATORS
#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

#include "DemoShell/Test/UiTestAssets.h"
#include "LightBench/LightBenchSettings.h"
#include "LightBench/MainModule.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Vortex/Types/ExposureSettingsStatus.h>

namespace oxygen::examples::light_bench {

// Each named case keeps its inputs and assertions together in this registration
// table. Numeric literals are deliberate test values, not runtime policy.
// NOLINTBEGIN(readability-magic-numbers)
// NOLINTNEXTLINE(google-readability-function-size, readability-function-size)
auto MainModule::RegisterUiTests(ImGuiTestEngine* engine) -> void
{
  auto* test = IM_REGISTER_TEST(engine, "lightbench", "preset_reset");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    ctx->Yield(5);
    ctx->SetRef("##LightBenchPresets");
    ctx->ItemClick("##Scenario");
    ctx->ItemClick("//$FOCUSED/Auto Adaptation");
    ctx->Yield(3);
    IM_CHECK(app.active_preset_ == LightBenchPreset::kAutoAdaptation);
    ctx->ItemClick("Dim");
    ctx->Yield(3);
    IM_CHECK(!IsPresetSettings(app.CaptureCurrentSettings()));
    ctx->ItemClick("##Scenario");
    ctx->Yield(2);
    const auto* popup = ctx->GetWindowByRef("//$FOCUSED");
    IM_CHECK(popup != nullptr);
    IM_CHECK(!popup->ScrollbarY);
    IM_CHECK_EQ(popup->ScrollMax.y, 0.0F);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->ItemClick("Reset");
    ctx->Yield(3);
    IM_CHECK(IsPresetSettings(app.CaptureCurrentSettings()));
  };

  test = IM_REGISTER_TEST(engine, "lightbench", "numeric_commit_cancel");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    ctx->SetRef("##LightBenchPresets");
    ctx->ItemClick("##Scenario");
    ctx->ItemClick("//$FOCUSED/Neutral Reference");
    ctx->Yield(3);
    app.GetShell().SetActivePanel("Post Process");
    ctx->Yield(3);
    ctx->SetRef("Post Process");
    constexpr auto field = "**/EV100/value/##value";
    ctx->ItemInputValue(field, "9.25");
    ctx->Yield(2);
    IM_CHECK_EQ(app.CaptureCurrentSettings().exposure.manual_ev, 9.25F);
    ctx->ItemInput(field);
    ctx->KeyCharsReplace("10.5");
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    IM_CHECK_EQ(app.CaptureCurrentSettings().exposure.manual_ev, 9.25F);
    ctx->ItemInput(field);
    ctx->KeyCharsReplace("10.5");
    ctx->KeyPress(ImGuiKey_Tab);
    ctx->Yield(2);
    IM_CHECK_EQ(app.CaptureCurrentSettings().exposure.manual_ev, 10.5F);
    ctx->ItemInput(field);
    ctx->KeyCharsReplace("11.75");
    ctx->MouseClickOnVoid();
    ctx->Yield(2);
    IM_CHECK_EQ(app.CaptureCurrentSettings().exposure.manual_ev, 11.75F);
    ctx->MouseMove(field);
    ctx->MouseDragWithDelta(ImVec2 { 45, 0 });
    ctx->Yield(2);
    IM_CHECK(app.CaptureCurrentSettings().exposure.manual_ev > 11.75F);
  };

  test = IM_REGISTER_TEST(engine, "lightbench", "mode_curve_output");
  test->UserData = this;
  struct SceneScope {
    std::string path;
  };
  test->SetVarsDataType<SceneScope>();
  test->GuiFunc = [](ImGuiTestContext* ctx) -> void {
    auto& path = ctx->GetVars<SceneScope>().path;
    if (!path.empty()) {
      return;
    }
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    const auto targets
      = app.app_.engine->GetConsole().Execute("pp.targets").output;
    IM_CHECK(targets.starts_with("scene:"));
    constexpr auto prefix_length = std::string_view("scene:").size();
    path = std::string("Post Process/")
      + targets.substr(prefix_length, targets.find(' ') - prefix_length);
  };
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    app.GetShell().SetActivePanel("Post Process");
    ctx->Yield(2);
    // BeginCombo has no label hook in ImGui 1.92.5. Resolve its exact scoped
    // ID; wildcard discovery is only available for instrumented items.
    ctx->SetRef(ctx->GetVars<SceneScope>().path.c_str());
    ctx->ItemClick("##Exposure mode");
    ctx->ItemClick("//$FOCUSED/Physical camera");
    ctx->ItemInputValue("**/ISO/value/##value", "200");
    ctx->Yield(2);
    IM_CHECK(app.CaptureCurrentSettings().exposure.mode
      == engine::ExposureMode::kManualCamera);
    IM_CHECK_EQ(app.CaptureCurrentSettings().camera_exposure.iso, 200.0F);
    ctx->ItemClick("##Exposure mode");
    ctx->ItemClick("//$FOCUSED/Automatic");
    ctx->Yield(2);
    IM_CHECK(app.CaptureCurrentSettings().exposure.mode
      == engine::ExposureMode::kAuto);
    ctx->ItemOpen("**/Advanced exposure");
    ctx->ItemOpen("**/Compensation curve");
    ctx->ItemClick("**/Add key");
    ctx->ItemInputValue("**/##Added EV", "1.5");
    IM_CHECK(app.CaptureCurrentSettings().exposure.compensation_curve.empty());
    ctx->ItemClick("**/Apply curve");
    ctx->Yield(2);
    const auto curve = app.CaptureCurrentSettings().exposure.compensation_curve;
    IM_CHECK_EQ(curve.size(), 1U);
    IM_CHECK_EQ(curve.front().compensation_ev, 1.5F);
    ctx->ItemClick("**/Reset auto controls");
    ctx->Yield(2);
    IM_CHECK(app.CaptureCurrentSettings().exposure.compensation_curve.empty());
    ctx->ItemClose("**/Advanced exposure");
    const auto gamma = app.CaptureCurrentSettings().gamma;
    ctx->ItemInputValue("**/Display gamma/value/##value", "-1");
    ctx->Yield(2);
    IM_CHECK_EQ(app.CaptureCurrentSettings().gamma, gamma);
    ctx->ItemInputValue("**/Display gamma/value/##value", "2.4");
    ctx->Yield(2);
    IM_CHECK_EQ(app.CaptureCurrentSettings().gamma, 2.4F);
  };

  test = IM_REGISTER_TEST(engine, "lightbench", "authored_mask_toggle");
  test->UserData = this;
  struct MaskFixture {
    bool initialized {};
    std::optional<content::ResourceKey> key;
    std::optional<vortex::ExposureSettingsStatus> status;
  };
  test->SetVarsDataType<MaskFixture>();
  test->GuiFunc = [](ImGuiTestContext* ctx) -> void {
    auto& fixture = ctx->GetVars<MaskFixture>();
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    if (fixture.initialized) {
      if (const auto renderer = app.ResolveVortexRenderer()) {
        fixture.status = renderer->InspectExposureSettings(
          renderer->ResolvePublishedRuntimeViewId(app.main_view_id_));
      }
      return;
    }
    fixture.initialized = true;
    // AssetLoader mutations belong to the engine thread. GuiFunc runs there;
    // TestFunc runs on Test Engine's cooperative driver thread.
    const auto root = app.UiTestOutputDirectory() / "mask";
    testing::WriteUiTextureFixture(root);
    const auto loader = app.app_.engine->GetAssetLoader();
    IM_CHECK(loader != nullptr);
    loader->AddLooseCookedRoot(root);
    const auto mounts = loader->EnumerateMountedSources();
    const auto mounted
      = std::ranges::find_if(mounts, [&root](const auto& entry) -> bool {
          return std::filesystem::equivalent(entry.source_path, root);
        });
    IM_CHECK(mounted != mounts.end());
    const auto key = loader->MakeTextureResourceKey(
      mounted->source_key, data::pak::core::ResourceIndexT { 1 });
    IM_CHECK(key.has_value());
    fixture.key = key;
    // Setup authors a real mounted texture into a new scene. The behavior
    // under test is the panel's off/on edits and asynchronous acceptance.
    app.pending_settings_ = PresetSettings(LightBenchPreset::kAutoAdaptation);
    app.pending_settings_->exposure.metering_mask = *key;
  };
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    const auto& fixture = ctx->GetVars<MaskFixture>();
    const auto key = fixture.key;
    IM_CHECK(key.has_value());
    ctx->Yield(3);
    const auto& status = fixture.status;
    for (int i = 0; i < 240
      && (!status
        || status->mask_status == vortex::ExposureMaskStatus::kPending);
      ++i) {
      ctx->Yield();
    }
    IM_CHECK(status.has_value());
    IM_CHECK(status->mask_status == vortex::ExposureMaskStatus::kReady);
    IM_CHECK(status->active_settings.has_value());
    IM_CHECK(status->active_settings->metering_mask == *key);
    app.GetShell().SetActivePanel("Post Process");
    ctx->Yield(2);
    ctx->SetRef("Post Process");
    ctx->ItemOpen("**/Advanced exposure");
    ctx->ItemUncheck("**/Use scene mask");
    ctx->Yield(3);
    IM_CHECK(app.CaptureCurrentSettings().exposure.metering_mask.get() == 0);
    IM_CHECK(
      status && status->mask_status == vortex::ExposureMaskStatus::kAbsent);
    ctx->ItemCheck("**/Use scene mask");
    ctx->Yield(3);
    IM_CHECK(app.CaptureCurrentSettings().exposure.metering_mask == *key);
    for (int i = 0; i < 240
      && (!status
        || status->mask_status == vortex::ExposureMaskStatus::kPending);
      ++i) {
      ctx->Yield();
    }
    IM_CHECK(
      status && status->mask_status == vortex::ExposureMaskStatus::kReady);
  };

  test = IM_REGISTER_TEST(engine, "lightbench", "save_load_and_panel_return");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    // This replaces the native test's simulated complete-reset operation.
    // Load a fixture through the real panel, then exercise Save/Reset/Load.
    auto modified = PresetSettings(LightBenchPreset::kAutoAdaptation);
    modified.objects.at(0).scale.x = 1.5F;
    modified.objects.at(1).enabled = false;
    modified.directional.illuminance_lux = 725;
    modified.point.enabled = true;
    modified.spot.color_rgb = { .5F, .75F, 1.0F };
    modified.camera_position.x = 3;
    modified.camera_exposure.iso = 400;
    modified.camera_fit_to_view = false;
    modified.camera_extents.at(4) = .25F;
    modified.exposure.compensation_ev = 1.25F;
    modified.exposure.compensation_curve = {
      { .metered_ev = -2, .compensation_ev = 1 },
      { .metered_ev = 4, .compensation_ev = -1 },
    };
    modified.tone_mapper = engine::ToneMapper::kReinhard;
    modified.gamma = 2.4F;
    const auto input
      = (app.UiTestOutputDirectory() / "modified-input.json").string();
    IM_CHECK(SaveSettingsFile(input, modified).has_value());
    app.GetShell().SetActivePanel("LightBench");
    ctx->Yield(2);
    ctx->SetRef("LightBench");
    ctx->ItemOpen("Save \\/ Load settings");
    ctx->ItemInputValue("##settings_path", input.c_str());
    ctx->ItemClick("Load settings");
    ctx->Yield(3);
    IM_CHECK(!IsPresetSettings(app.CaptureCurrentSettings()));
    IM_CHECK_EQ(app.CaptureCurrentSettings().camera_position.x, 3.0F);
    IM_CHECK_EQ(app.CaptureCurrentSettings().objects.at(0).scale.x, 1.5F);
    IM_CHECK_EQ(
      app.CaptureCurrentSettings().directional.illuminance_lux, 725.0F);
    const auto saved = EncodeSettings(app.CaptureCurrentSettings());
    IM_CHECK(saved.has_value());
    const auto path
      = (app.UiTestOutputDirectory() / "saved-settings.json").string();
    ctx->ItemInputValue("##settings_path", path.c_str());
    ctx->ItemClick("Save settings");
    IM_CHECK(std::filesystem::exists(path));
    ctx->SetRef("##LightBenchPresets");
    ctx->ItemClick("Reset");
    ctx->Yield(3);
    IM_CHECK(IsPresetSettings(app.CaptureCurrentSettings()));
    ctx->SetRef("LightBench");
    ctx->ItemClick("Load settings");
    ctx->Yield(3);
    const auto loaded = EncodeSettings(app.CaptureCurrentSettings());
    IM_CHECK(loaded.has_value());
    IM_CHECK(*saved == *loaded);
    const auto camera = app.CaptureCurrentSettings().camera_extents;
    const auto camera_button = std::string("//DemoPanelSideBar/")
      + std::string(imgui::icons::kIconCameraControls);
    ctx->ItemClick(camera_button.c_str());
    ctx->Yield(2);
    IM_CHECK(app.GetShell().GetActivePanelName()
      != std::optional<std::string>("LightBench"));
    const auto settings_button = std::string("//DemoPanelSideBar/")
      + std::string(imgui::icons::kIconDemoPanel) + "##LightBench";
    ctx->ItemClick(settings_button.c_str());
    ctx->Yield(2);
    IM_CHECK(app.GetShell().GetActivePanelName()
      == std::optional<std::string>("LightBench"));
    IM_CHECK(app.CaptureCurrentSettings().camera_extents == camera);
    IM_CHECK(*EncodeSettings(app.CaptureCurrentSettings()) == *saved);
  };

  test = IM_REGISTER_TEST(engine, "lightbench", "console_preset_and_settings");
  test->UserData = this;
  struct ConsoleState {
    std::string initial_target;
    console::ExecutionResult last;
  };
  test->SetVarsDataType<ConsoleState>();
  test->GuiFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    auto& state = ctx->GetVars<ConsoleState>();
    auto& console = app.app_.engine->GetConsole();
    if (state.initial_target.empty()) {
      const auto targets = console.Execute("pp.targets").output;
      IM_CHECK(targets.starts_with("scene:"));
      state.initial_target = targets.substr(0, targets.find(' '));
    }
    const auto& records = console.GetExecutionRecords();
    if (!records.empty()) {
      state.last = records.back().result;
    }
  };
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    const auto& state = ctx->GetVars<ConsoleState>();
    ctx->KeyPress(ImGuiKey_GraveAccent);
    ctx->SetRef("Console");
    const auto run = [ctx](const std::string& command) -> void {
      ctx->ItemInputValue("##ConsoleInput", command.c_str());
      ctx->Yield(3);
    };
    run("pp.exposure " + state.initial_target + " mode=manual manual_ev=9.25");
    IM_CHECK(state.last.status == console::ExecutionStatus::kOk);
    IM_CHECK_EQ(app.CaptureCurrentSettings().exposure.manual_ev, 9.25F);
    const auto before = EncodeSettings(app.CaptureCurrentSettings());
    IM_CHECK(before.has_value());
    run("pp.exposure " + state.initial_target
      + " manual_ev=4 min_ev=12 max_ev=2");
    IM_CHECK(state.last.status == console::ExecutionStatus::kInvalidArguments);
    IM_CHECK(*EncodeSettings(app.CaptureCurrentSettings()) == *before);
    run("lightbench.preset indoor");
    IM_CHECK(state.last.output.starts_with("queued"));
    IM_CHECK(app.active_preset_ == LightBenchPreset::kIndoor);
    run("pp.exposure " + state.initial_target + " manual_ev=3");
    IM_CHECK(state.last.status == console::ExecutionStatus::kInvalidArguments);
    run("pp.targets");
    const auto current
      = state.last.output.substr(0, state.last.output.find(' '));
    run("pp.exposure " + current + " mode=manual manual_ev=3");
    IM_CHECK(!IsPresetSettings(app.CaptureCurrentSettings()));
    run("lightbench.reset");
    IM_CHECK(state.last.output.starts_with("queued"));
    IM_CHECK(IsPresetSettings(app.CaptureCurrentSettings()));
    ctx->KeyPress(ImGuiKey_GraveAccent);
  };
}
// NOLINTEND(readability-magic-numbers)
} // namespace oxygen::examples::light_bench
