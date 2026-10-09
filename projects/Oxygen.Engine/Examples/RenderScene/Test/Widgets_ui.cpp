//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/DemoShell.h"
#include "DemoShell/UI/ContentVm.h"

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Environment/Types/StaticSkyLightProducts.h>
#include <Oxygen/Vortex/Types/SkyLightRuntimeState.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Test/UiTestSession.h"
#include "RenderScene/MainModule.h"
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Cooker/Import/RetainedModelImport.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::examples::render_scene {

namespace {
  auto SelectPanel(ImGuiTestContext* ctx, DemoShell& shell,
    const std::string_view name, const std::string_view icon) -> void
  {
    const auto active = shell.GetActivePanelName();
    if (!active || *active != name) {
      ctx->SetRef("//DemoPanelSideBar");
      ctx->ItemClick(icon.data());
    }
    ctx->Yield(3);
    ctx->SetRef(std::string(name).c_str());
  }

  auto SelectOutdoorProfile(ImGuiTestContext* ctx) -> void
  {
    ctx->ScrollToTop("//Environment");
    ctx->Yield(2);
    auto* parent = ctx->GetWindowByRef("//Environment");
    IM_CHECK(parent != nullptr);
    ImGuiWindow* profile = nullptr;
    for (auto* window : ctx->UiContext->Windows) {
      if (window->RootWindow == parent->RootWindow
        && std::string_view(window->Name).find("##EnvironmentProfile_")
          != std::string_view::npos) {
        profile = window;
      }
    }
    IM_CHECK(profile != nullptr);
    // The profile is a child window; bind its real ID rather than hashing its
    // slash-containing generated name as an item path.
    ctx->SetRef(profile);
    ctx->ItemClick("Profile");
    ctx->ItemClick("//$FOCUSED/Outdoor Sunny");
    ctx->SetRef("Environment");
    ctx->Yield(3);
  }

  auto FindChild(ImGuiTestContext* ctx, ImGuiWindow* root,
    const std::string_view name) -> ImGuiWindow*
  {
    for (auto* window : ctx->UiContext->Windows) {
      if (window->RootWindow == root->RootWindow
        && std::string_view(window->Name).find(name)
          != std::string_view::npos) {
        return window;
      }
    }
    return nullptr;
  }

  auto ClickScene(ImGuiTestContext* ctx, const ui::SceneEntry& entry) -> void
  {
    ctx->Yield(3);
    auto* root = ctx->GetWindowByRef("//Content Loader");
    IM_CHECK(root);
    auto* main = FindChild(ctx, root, "ContentLoaderMain");
    IM_CHECK(main);
    ctx->SetRef(main);
    ctx->ItemClick("**/Library");
    ctx->Yield(3);
    ctx->ItemInputValue("**/##SceneFilter", entry.name.c_str());
    ctx->Yield(3);
    auto* scenes = FindChild(ctx, root, "LibraryScenes");
    IM_CHECK(scenes);
    const auto source
      = std::string(
          entry.source.kind == ui::SceneSourceKind::kPak ? "PAK: " : "Index: ")
      + entry.source.path.filename().string();
    const auto label = entry.name + " (" + source + ")##"
      + std::string(nostd::to_string(entry.key)) + "-"
      + entry.source.path.string();
    ctx->SetRef(scenes);
    ctx->ItemClick(scenes->GetID(label.c_str()));
  }

  auto CheckPersistedLighting(const scene::Scene& scene) -> void
  {
    const auto sky
      = scene.GetEnvironment()->TryGetSystem<scene::environment::SkyLight>();
    const auto fog
      = scene.GetEnvironment()->TryGetSystem<scene::environment::Fog>();
    IM_CHECK(sky && fog);
    IM_CHECK(sky->IsEnabled());
    IM_CHECK(
      sky->GetSource() == scene::environment::SkyLightSource::kCapturedScene);
    IM_CHECK_EQ(sky->GetIntensityMul(), 1.25F);
    IM_CHECK_EQ(sky->GetDiffuseIntensity(), 0.65F);
    IM_CHECK_EQ(sky->GetSpecularIntensity(), 1.35F);
    IM_CHECK(!sky->GetAffectReflections());
    IM_CHECK(!sky->GetLowerHemisphereIsSolidColor());
    IM_CHECK_EQ(sky->GetLowerHemisphereBlendAlpha(), 0.35F);
    IM_CHECK(fog->IsEnabled() && fog->GetEnableHeightFog());
    IM_CHECK(!fog->GetRenderInMainPass());
    IM_CHECK(fog->GetVisibleInRealTimeSkyCaptures());
    IM_CHECK_EQ(fog->GetExtinctionSigmaTPerMeter(), 0.01F);
  }

  auto CheckGpuLighting(ImGuiTestContext* ctx, vortex::Renderer& renderer,
    const scene::Scene& scene) -> void
  {
    auto& diagnostics = renderer.GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(diagnostics.GetRequestedFeatures()
      | vortex::DiagnosticsFeature::kFrameLedger);
    for (unsigned wait = 0U; wait < 600U; ++wait) {
      const auto state = renderer.InspectSkyLight(scene);
      if (state.usable && state.source_age_frames == 0U
        && state.gpu_validation == vortex::SkyLightGpuValidation::kValid) {
        break;
      }
      ctx->Yield();
    }
    const auto state = renderer.InspectSkyLight(scene);
    IM_CHECK(state.usable && state.observed && !state.empty_capture);
    IM_CHECK_EQ(state.scene_lifetime, scene.GetLifetimeId().get());
    IM_CHECK_EQ(state.source_age_frames, 0U);
    IM_CHECK_EQ(state.published_source_revision, state.desired_source_revision);
    IM_CHECK(state.gpu_validation == vortex::SkyLightGpuValidation::kValid);
    IM_CHECK_EQ(state.validated_revision, state.published_revision);
  }
} // namespace

// NOLINTBEGIN(readability-magic-numbers) - concrete widget values and wait
// bounds.
auto MainModule::RegisterUiTests(ImGuiTestEngine* engine) -> void
{
  if (testing::UiTestSession::UsesIsolatedSettings()
    && SettingsService::ForDemoApp()
      ->GetBool("tests.expect_empty_scene")
      .value_or(false)) {
    auto* test = IM_REGISTER_TEST(engine, "renderscene", "empty_scene");
    test->UserData = this;
    test->TestFunc = [](ImGuiTestContext* ctx) -> void {
      auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
      for (unsigned wait = 0U;
        wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
        ++wait) {
        ctx->Yield();
      }
      IM_CHECK(app.current_scene_key_ && !app.active_scene_load_key_);
      IM_CHECK(app.active_scene_asset_pin_);
      IM_CHECK(app.active_scene_asset_pin_->GetNodes().empty());
      IM_CHECK(app.active_scene_asset_pin_->HasEnvironmentBlock());
      const auto scene = app.GetShell().TryGetScene();
      IM_CHECK(scene && scene->GetEnvironment());
      IM_CHECK(app.ResolveVortexRenderer());
      ctx->Yield(30);
    };
  }
  if (!ContentSettingsService {}.GetMountedImportRecords().empty()) {
    auto* test = IM_REGISTER_TEST(engine, "renderscene", "retained_library");
    test->UserData = this;
    test->TestFunc = [](ImGuiTestContext* ctx) -> void {
      auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
      for (unsigned wait = 0U;
        wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
        ++wait) {
        ctx->Yield();
      }
      IM_CHECK(app.current_scene_key_ && !app.active_scene_load_key_);
      const ContentSettingsService settings;
      const auto records = settings.GetMountedImportRecords();
      const auto selection = settings.GetActiveSceneSelection();
      IM_CHECK(!records.empty() && selection);
      IM_CHECK(std::string(nostd::to_string(*app.current_scene_key_))
        == selection->scene_key);
      const auto vm = app.GetShell().GetContentVm();
      IM_CHECK(vm);
      for (const auto& record : records) {
        const auto root
          = content::import::RetainedModelImport::SelectedGeneration(record);
        IM_CHECK(root);
        const auto index = (*root / "container.index.bin").lexically_normal();
        const auto count = std::ranges::count_if(
          vm->GetLoadedIndices(), [&](const auto& loaded) -> auto {
            return loaded.lexically_normal() == index;
          });
        IM_CHECK_EQ(count, 1);
      }
      const auto scene = app.GetShell().TryGetScene();
      const auto renderer = app.ResolveVortexRenderer();
      IM_CHECK(scene && renderer);
      for (unsigned wait = 0U;
        wait < 120U && !renderer->InspectSkyLight(*scene).usable; ++wait) {
        ctx->Yield();
      }
      IM_CHECK(renderer->InspectSkyLight(*scene).usable);
      ctx->Yield(30);
    };
  }

  auto* test = IM_REGISTER_TEST(engine, "renderscene", "ibl_controls");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    for (unsigned wait = 0U;
      wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
      ++wait) {
      ctx->Yield();
    }
    IM_CHECK(app.current_scene_key_.has_value());
    IM_CHECK(!app.active_scene_load_key_.has_value());
    SelectPanel(
      ctx, app.GetShell(), "Environment", imgui::icons::kIconEnvironment);
    ctx->Yield(3);
    SelectOutdoorProfile(ctx);
    ctx->ItemClose("**/Sun");
    ctx->ItemClose("**/Sky Atmosphere");
    ctx->ItemOpen("**/Sky Light (IBL)");
    ctx->ItemCheck("**/Enabled##SkyLight");
    const auto scene = app.GetShell().TryGetScene();
    const auto renderer = app.ResolveVortexRenderer();
    IM_CHECK(scene && renderer);
    auto sky
      = scene->GetEnvironment()->TryGetSystem<scene::environment::SkyLight>();
    IM_CHECK(sky);
    for (unsigned wait = 0U;
      wait < 120U && !renderer->InspectSkyLight(*scene).usable; ++wait) {
      ctx->Yield();
    }
    IM_CHECK(renderer->InspectSkyLight(*scene).usable);
    const auto original_revision
      = renderer->InspectSkyLight(*scene).published_revision;
    ctx->ItemInputValue("**/Intensity##SkyLight", "1.25");
    ctx->ItemOpen("**/Advanced##SkyLight");
    ctx->ItemInputValue("**/Diffuse Indirect", "0.65");
    ctx->ItemInputValue("**/Specular Indirect", "1.35");
    ctx->ItemUncheck("**/Affect Reflections");
    ctx->Yield(3);
    IM_CHECK_EQ(sky->GetIntensityMul(), 1.25F);
    IM_CHECK_EQ(sky->GetDiffuseIntensity(), 0.65F);
    IM_CHECK_EQ(sky->GetSpecularIntensity(), 1.35F);
    IM_CHECK(!sky->GetAffectReflections());
    IM_CHECK_EQ(
      renderer->InspectSkyLight(*scene).published_revision, original_revision);
    ctx->ItemCheck("**/Affect Reflections");
    ctx->ItemUncheck("**/Override Lower Hemisphere");
    ctx->Yield(3);
    IM_CHECK(!sky->GetLowerHemisphereIsSolidColor());
    IM_CHECK(
      renderer->InspectSkyLight(*scene).published_revision > original_revision);
    ctx->ItemCheck("**/Override Lower Hemisphere");
    ctx->ItemInputValue("**/Hemisphere Blend", "0.35");
    ctx->Yield(3);
    IM_CHECK(sky->GetLowerHemisphereIsSolidColor());
    IM_CHECK_EQ(sky->GetLowerHemisphereBlendAlpha(), 0.35F);
    SelectPanel(
      ctx, app.GetShell(), "Diagnostics", imgui::icons::kIconRendering);
    ctx->Yield(3);
    ctx->SetRef("Diagnostics");
    ctx->ItemOpen("**/Sky Lighting");
    ctx->ItemCheck("**/Frame Diagnostics");
    for (unsigned wait = 0U; wait < 120U
      && renderer->InspectSkyLight(*scene).gpu_validation
        != vortex::SkyLightGpuValidation::kValid;
      ++wait) {
      ctx->Yield();
    }
    const auto validated = renderer->InspectSkyLight(*scene);
    IM_CHECK(validated.gpu_validation == vortex::SkyLightGpuValidation::kValid);
    IM_CHECK_EQ(validated.validated_revision, validated.published_revision);
    IM_CHECK(validated.source_radiance_scale >= 1.0F);
    ctx->ItemUncheck("**/Frame Diagnostics");
    IM_CHECK(!vortex::HasAnyFeature(
      renderer->GetDiagnosticsService().GetEnabledFeatures(),
      vortex::DiagnosticsFeature::kFrameLedger));
    ctx->ItemCheck("**/GPU Timeline");
    for (unsigned wait = 0U;
      wait < 120U && !renderer->GetDiagnosticsService().GetLatestIblGpuTiming();
      ++wait) {
      ctx->Yield();
    }
    IM_CHECK(
      renderer->GetDiagnosticsService().GetLatestIblGpuTiming().has_value());
    SelectPanel(
      ctx, app.GetShell(), "Environment", imgui::icons::kIconEnvironment);
    ctx->Yield(3);
    ctx->SetRef("Environment");
    IM_CHECK(ctx->ItemExists("**/Specular Indirect"));
    IM_CHECK_EQ(sky->GetSpecularIntensity(), 1.35F);
    ctx->ItemUncheck("**/Enabled##SkyLight");
    ctx->Yield(3);
    IM_CHECK(!renderer->InspectSkyLight(*scene).enabled);
    ctx->ItemCheck("**/Enabled##SkyLight");
    ctx->Yield(3);
    IM_CHECK(renderer->InspectSkyLight(*scene).usable);
  };

  test = IM_REGISTER_TEST(engine, "renderscene", "ibl_source_and_fog");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    for (unsigned wait = 0U;
      wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
      ++wait) {
      ctx->Yield();
    }
    IM_CHECK(app.current_scene_key_.has_value());
    IM_CHECK(!app.active_scene_load_key_.has_value());
    SelectPanel(
      ctx, app.GetShell(), "Environment", imgui::icons::kIconEnvironment);
    ctx->Yield(3);
    SelectOutdoorProfile(ctx);
    ctx->ItemOpen("**/Sky Light (IBL)");
    ctx->ItemClick("Source##SkyLight");
    ctx->ItemClick("//$FOCUSED/$$1/Specified Cubemap");
    ctx->Yield(3);
    const auto scene = app.GetShell().TryGetScene();
    const auto renderer = app.ResolveVortexRenderer();
    IM_CHECK(scene && renderer);
    IM_CHECK(!renderer->InspectSkyLight(*scene).usable);
    IM_CHECK(renderer->InspectSkyLight(*scene).unavailable_reason
      == vortex::environment::StaticSkyLightUnavailableReason::kMissingCubemap);
    ctx->ItemClick("Source##SkyLight");
    ctx->ItemClick("//$FOCUSED/$$0/Captured Scene");
    ctx->Yield(3);
    IM_CHECK(renderer->InspectSkyLight(*scene).usable);
    ctx->ItemOpen("**/Sky Atmosphere");
    ctx->ItemUncheck("**/Enabled##SkyAtmo");
    ctx->ItemClose("**/Sky Atmosphere");
    ctx->ItemOpen("**/Height Fog");
    ctx->ItemCheck("**/Enable Height Fog");
    ctx->ItemUncheck("**/Render In Main Pass##Fog");
    ctx->ItemInputValue(R"(Density \/ Extinction (1\/m))", "0.01");
    ctx->ItemCheck("**/Include in Sky Lighting");
    ctx->Yield(3);
    const auto fog
      = scene->GetEnvironment()->TryGetSystem<scene::environment::Fog>();
    IM_CHECK(fog);
    IM_CHECK(!fog->GetRenderInMainPass());
    IM_CHECK(fog->GetVisibleInRealTimeSkyCaptures());
    IM_CHECK(renderer->InspectSkyLight(*scene).usable);
    IM_CHECK(!renderer->InspectSkyLight(*scene).empty_capture);
    const auto fog_revision
      = renderer->InspectSkyLight(*scene).published_revision;
    const auto density = fog->GetExtinctionSigmaTPerMeter();
    ctx->MouseMove(R"(Density \/ Extinction (1\/m))");
    ctx->MouseDragWithDelta(ImVec2 { 20.0F, 0.0F });
    ctx->Yield(3);
    IM_CHECK(fog->GetExtinctionSigmaTPerMeter() != density);
    IM_CHECK(
      renderer->InspectSkyLight(*scene).published_revision > fog_revision);
    IM_CHECK_EQ(renderer->InspectSkyLight(*scene).source_age_frames, 0U);
    ctx->ItemUncheck("**/Include in Sky Lighting");
    ctx->Yield(3);
    IM_CHECK(renderer->InspectSkyLight(*scene).empty_capture);
    ctx->ItemCheck("**/Include in Sky Lighting");
    ctx->Yield(3);
    IM_CHECK(!renderer->InspectSkyLight(*scene).empty_capture);
    ctx->ItemClose("**/Height Fog");
    ctx->ItemOpen("**/Sky Atmosphere");
    ctx->ItemCheck("**/Enabled##SkyAtmo");
    ctx->Yield(3);
    IM_CHECK(renderer->InspectSkyLight(*scene).usable);
    ctx->ItemClose("**/Sky Atmosphere");
    ctx->ItemOpen("**/Sun");
    const auto sun_revision
      = renderer->InspectSkyLight(*scene).published_revision;
    ctx->MouseMove("Azimuth (deg)");
    ctx->MouseDragWithDelta(ImVec2 { 20.0F, 0.0F });
    ctx->Yield(3);
    IM_CHECK(
      renderer->InspectSkyLight(*scene).published_revision > sun_revision);
    IM_CHECK_EQ(renderer->InspectSkyLight(*scene).source_age_frames, 0U);
    ctx->ItemClose("**/Sun");
    SelectOutdoorProfile(ctx);
    const auto sky
      = scene->GetEnvironment()->TryGetSystem<scene::environment::SkyLight>();
    IM_CHECK_EQ(sky->GetIntensityMul(), 1.0F);
    IM_CHECK_EQ(sky->GetDiffuseIntensity(), 1.0F);
    IM_CHECK_EQ(sky->GetSpecularIntensity(), 1.0F);
    ctx->ItemOpen("**/Sky Light (IBL)");
  };

  // Runs only when RenderScene starts with --startup-skybox <HDR image>.
  if (!app_.startup_skybox_path.empty()) {
    test = IM_REGISTER_TEST(engine, "renderscene", "ibl_startup_skybox");
    test->UserData = this;
    test->TestFunc = [](ImGuiTestContext* ctx) -> void {
      auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
      for (unsigned wait = 0U;
        wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
        ++wait) {
        ctx->Yield();
      }
      IM_CHECK(app.current_scene_key_ && !app.active_scene_load_key_);
      const auto scene = app.GetShell().TryGetScene();
      const auto renderer = app.ResolveVortexRenderer();
      IM_CHECK(scene && renderer);
      const auto env = scene->GetEnvironment();
      IM_CHECK(env != nullptr);
      // The startup skybox cooks and equips the Sky Sphere asynchronously.
      const auto sphere_cubemap = [&]() -> content::ResourceKey {
        const auto sphere = env->TryGetSystem<scene::environment::SkySphere>();
        return sphere ? sphere->GetCubemapResource() : content::ResourceKey {};
      };
      for (unsigned wait = 0U; wait < 1800U
        && (sphere_cubemap().get() == 0U || sphere_cubemap().IsPlaceholder());
        ++wait) {
        ctx->Yield();
      }
      const auto cubemap = sphere_cubemap();
      IM_CHECK(cubemap.get() != 0U && !cubemap.IsPlaceholder());

      // Switching the source after the load equips the loaded cubemap; an
      // HDR skybox cooked to a float format then lights the scene.
      SelectPanel(
        ctx, app.GetShell(), "Environment", imgui::icons::kIconEnvironment);
      ctx->ItemOpen("**/Sky Light (IBL)");
      ctx->ItemCheck("**/Enabled##SkyLight");
      ctx->ItemClick("Source##SkyLight");
      ctx->ItemClick("//$FOCUSED/$$1/Specified Cubemap");
      ctx->Yield(3);
      const auto light = env->TryGetSystem<scene::environment::SkyLight>();
      IM_CHECK(light != nullptr);
      IM_CHECK(light->GetSource()
        == scene::environment::SkyLightSource::kSpecifiedCubemap);
      IM_CHECK(light->GetCubemapResource() == cubemap);
      for (unsigned wait = 0U;
        wait < 600U && !renderer->InspectSkyLight(*scene).usable; ++wait) {
        ctx->Yield();
      }
      const auto state = renderer->InspectSkyLight(*scene);
      IM_CHECK(state.usable);
      IM_CHECK(state.unavailable_reason
        == vortex::environment::StaticSkyLightUnavailableReason::kNone);
      ctx->ItemClick("Source##SkyLight");
      ctx->ItemClick("//$FOCUSED/$$0/Captured Scene");
      ctx->Yield(3);
    };
  }

  if (!testing::UiTestSession::UsesIsolatedSettings()) {
    return;
  }

  test = IM_REGISTER_TEST(engine, "renderscene", "ibl_editor_reference");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    for (unsigned wait = 0U;
      wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
      ++wait) {
      ctx->Yield();
    }
    IM_CHECK(app.current_scene_key_ && !app.active_scene_load_key_);
    const auto scene = app.GetShell().TryGetScene();
    const auto renderer = app.ResolveVortexRenderer();
    IM_CHECK(scene && renderer);
    CheckGpuLighting(ctx, *renderer, *scene);
    if (ctx->IsError()) {
      return;
    }
    const auto exposure = renderer->InspectExposureSettings(
      renderer->ResolvePublishedRuntimeViewId(app.main_view_id_));
    IM_CHECK(exposure && exposure->active_settings);
    IM_CHECK(exposure->active_settings->mode == engine::ExposureMode::kManual);
    IM_CHECK_EQ(exposure->active_settings->manual_ev, 13.0F);
    IM_CHECK_EQ(exposure->active_settings->key, 12.5F);
    IM_CHECK_EQ(exposure->active_settings->compensation_ev, 0.0F);
    ctx->Yield(30);
    const auto graphics = app.app_.gfx_weak.lock();
    IM_CHECK(graphics);
    if (const auto capture = graphics->GetFrameCaptureController();
      capture && capture->IsAvailable()) {
      IM_CHECK(capture->TriggerNextFrame());
      ctx->Yield(8);
      IM_CHECK(!capture->IsCapturing());
    }
    const auto state = renderer->InspectSkyLight(*scene);
    std::ofstream record(
      SettingsService::ForDemoApp()->GetStoragePath().parent_path()
      / "native-reference.json");
    IM_CHECK(record.good());
    record << nlohmann::json {
      { "scene_key", std::string(nostd::to_string(*app.current_scene_key_)) },
      { "published_revision", state.published_revision },
      { "validated_revision", state.validated_revision },
      { "source_radiance_scale", state.source_radiance_scale },
      { "average_brightness", state.average_brightness },
    }.dump(2);
    record.flush();
    IM_CHECK(record.good());
  };

  for (const auto* name : { "ibl_appearance_off", "ibl_appearance_on" }) {
    test = IM_REGISTER_TEST(engine, "renderscene", name);
    test->UserData = this;
    struct AppearanceState {
      std::string post_process_scope;
    };
    test->SetVarsDataType<AppearanceState>();
    test->GuiFunc = [](ImGuiTestContext* ctx) -> void {
      auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
      const auto targets
        = app.app_.engine->GetConsole().Execute("pp.targets").output;
      if (targets.starts_with("scene:")) {
        constexpr auto prefix = std::string_view("scene:").size();
        ctx->GetVars<AppearanceState>().post_process_scope = "Post Process/"
          + targets.substr(prefix, targets.find(' ') - prefix);
      }
    };
    test->TestFunc = [](ImGuiTestContext* ctx) -> void {
      auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
      for (unsigned wait = 0U;
        wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
        ++wait) {
        ctx->Yield();
      }
      IM_CHECK(app.current_scene_key_ && !app.active_scene_load_key_);
      const auto scene = app.GetShell().TryGetScene();
      const auto renderer = app.ResolveVortexRenderer();
      IM_CHECK(scene && renderer);
      SelectPanel(
        ctx, app.GetShell(), "Environment", imgui::icons::kIconEnvironment);
      SelectOutdoorProfile(ctx);
      ctx->ItemClose("**/Sun");
      ctx->ItemClose("**/Sky Atmosphere");
      ctx->ItemOpen("**/Sky Light (IBL)");
      ctx->ItemCheck("**/Enabled##SkyLight");
      CheckGpuLighting(ctx, *renderer, *scene);
      if (ctx->IsError()) {
        return;
      }
      const bool enabled
        = std::string_view(ctx->Test->Name) == "ibl_appearance_on";
      if (!enabled) {
        ctx->ItemUncheck("**/Enabled##SkyLight");
      }
      SelectPanel(
        ctx, app.GetShell(), "Post Process", imgui::icons::kIconHdrTonemap);
      ctx->ItemCheck("**/Enable exposure");
      // BeginCombo lacks a discovery hook; use the panel's actual scene scope.
      const auto& scope = ctx->GetVars<AppearanceState>().post_process_scope;
      IM_CHECK(!scope.empty());
      ctx->SetRef(scope.c_str());
      ctx->ItemClick("##Exposure mode");
      ctx->ItemClick("//$FOCUSED/Manual EV100");
      ctx->ItemInputValue("**/EV100/value/##value", "12");
      ctx->ItemInputValue("**/Compensation (EV)/value/##value", "0");
      if (ctx->IsError()) {
        return;
      }
      ctx->Yield(5);
      const auto exposure = renderer->InspectExposureSettings(
        renderer->ResolvePublishedRuntimeViewId(app.main_view_id_));
      IM_CHECK(exposure && exposure->active_settings);
      const auto& settings = *exposure->active_settings;
      IM_CHECK(
        settings.enabled && settings.mode == engine::ExposureMode::kManual);
      IM_CHECK_EQ(settings.manual_ev, 12.0F);
      IM_CHECK_EQ(settings.compensation_ev, 0.0F);
      const auto state = renderer->InspectSkyLight(*scene);
      IM_CHECK_EQ(state.enabled, enabled);
      if (enabled) {
        CheckGpuLighting(ctx, *renderer, *scene);
        if (ctx->IsError()) {
          return;
        }
      }
      // Close the panel through the sidebar so both images have the same area.
      ctx->SetRef("//DemoPanelSideBar");
      ctx->ItemClick(imgui::icons::kIconHdrTonemap.data());
      ctx->Yield(120);
      const auto graphics = app.app_.gfx_weak.lock();
      IM_CHECK(graphics);
      if (const auto capture = graphics->GetFrameCaptureController();
        capture && capture->IsAvailable()) {
        IM_CHECK(capture->TriggerNextFrame());
        ctx->Yield(8);
        IM_CHECK(!capture->IsCapturing());
      }
      std::ofstream record(
        SettingsService::ForDemoApp()->GetStoragePath().parent_path()
        / (std::string(ctx->Test->Name) + ".json"));
      IM_CHECK(record.good());
      record << nlohmann::json {
        { "scene_key", std::string(nostd::to_string(*app.current_scene_key_)) },
        { "ibl_enabled", enabled }, { "manual_ev", settings.manual_ev },
        { "exposure_key", settings.key },
        { "compensation_ev", settings.compensation_ev },
        { "scene_lifetime", state.scene_lifetime },
        { "published_revision", state.published_revision },
        { "validated_revision", state.validated_revision },
        { "source_age_frames", state.source_age_frames },
      }.dump(2);
      record.flush();
      IM_CHECK(record.good());
    };
  }

  test = IM_REGISTER_TEST(engine, "renderscene", "ibl_persist_and_replace");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    for (unsigned wait = 0U;
      wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
      ++wait) {
      ctx->Yield();
    }
    IM_CHECK(app.current_scene_key_ && !app.active_scene_load_key_);
    const auto renderer = app.ResolveVortexRenderer();
    IM_CHECK(renderer);
    SelectPanel(
      ctx, app.GetShell(), "Environment", imgui::icons::kIconEnvironment);
    ctx->Yield(3);
    SelectOutdoorProfile(ctx);
    ctx->ItemClose("**/Sun");
    ctx->ItemClose("**/Sky Atmosphere");
    ctx->ItemOpen("**/Sky Light (IBL)");
    ctx->ItemCheck("**/Enabled##SkyLight");
    ctx->ItemInputValue("**/Intensity##SkyLight", "1.25");
    ctx->ItemOpen("**/Advanced##SkyLight");
    ctx->ItemInputValue("**/Diffuse Indirect", "0.65");
    ctx->ItemInputValue("**/Specular Indirect", "1.35");
    ctx->ItemUncheck("**/Affect Reflections");
    ctx->ItemCheck("**/Override Lower Hemisphere");
    ctx->ItemInputValue("**/Hemisphere Blend", "0.35");
    ctx->ItemUncheck("**/Override Lower Hemisphere");
    ctx->ItemClose("**/Sky Light (IBL)");
    ctx->ItemOpen("**/Height Fog");
    ctx->ItemCheck("**/Enable Height Fog");
    ctx->ItemUncheck("**/Render In Main Pass##Fog");
    ctx->ItemInputValue(R"(Density \/ Extinction (1\/m))", "0.01");
    ctx->ItemCheck("**/Include in Sky Lighting");
    ctx->Yield(5);
    CheckPersistedLighting(*app.GetShell().TryGetScene());
    if (ctx->IsError()) {
      return;
    }
    CheckGpuLighting(ctx, *renderer, *app.GetShell().TryGetScene());
    if (ctx->IsError()) {
      return;
    }
    const auto vm = app.GetShell().GetContentVm();
    IM_CHECK(vm);
    const auto& scenes = vm->GetAvailableScenes();
    const auto original
      = std::ranges::find_if(scenes, [&](const auto& entry) -> auto {
          return entry.key == *app.current_scene_key_;
        });
    const auto other
      = std::ranges::find_if(scenes, [&](const auto& entry) -> auto {
          return std::filesystem::path(entry.name).stem() == "Lantern";
        });
    IM_CHECK(original != scenes.end() && other != scenes.end());
    const auto original_entry = *original;
    const auto other_entry = *other;
    IM_CHECK(original_entry.key != other_entry.key);
    for (const auto& target : { other_entry, original_entry }) {
      const auto previous_lifetime
        = app.GetShell().TryGetScene()->GetLifetimeId();
      SelectPanel(ctx, app.GetShell(), "Content Loader",
        imgui::icons::kIconContentLoader);
      ClickScene(ctx, target);
      if (ctx->IsError()) {
        return;
      }
      for (unsigned wait = 0U; wait < 600U; ++wait) {
        const auto current = app.GetShell().TryGetScene();
        if (app.current_scene_key_ == target.key && !app.active_scene_load_key_
          && current && current->GetLifetimeId() != previous_lifetime) {
          break;
        }
        ctx->Yield();
      }
      IM_CHECK(
        app.current_scene_key_ == target.key && !app.active_scene_load_key_);
      const auto current = app.GetShell().TryGetScene();
      IM_CHECK(current && current->GetLifetimeId() != previous_lifetime);
      CheckPersistedLighting(*current);
      if (ctx->IsError()) {
        return;
      }
      CheckGpuLighting(ctx, *renderer, *current);
      if (ctx->IsError()) {
        return;
      }
    }
    const auto state = renderer->InspectSkyLight(*app.GetShell().TryGetScene());
    const auto expected_path
      = SettingsService::ForDemoApp()->GetStoragePath().parent_path()
      / "expected.json";
    std::ofstream expected(expected_path);
    IM_CHECK(expected.good());
    expected << nlohmann::json {
      { "scene_key", std::string(nostd::to_string(*app.current_scene_key_)) },
      { "writer_runtime_source_revision", state.published_source_revision },
      { "radiance_scale", state.source_radiance_scale },
      { "average_brightness", state.average_brightness },
    }.dump(2);
    expected.flush();
    IM_CHECK(expected.good());
    SelectPanel(
      ctx, app.GetShell(), "Environment", imgui::icons::kIconEnvironment);
    ctx->Yield(5);
  };

  test = IM_REGISTER_TEST(engine, "renderscene", "ibl_reopen");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    std::ifstream expected_file(
      SettingsService::ForDemoApp()->GetStoragePath().parent_path()
      / "expected.json");
    IM_CHECK(expected_file.good());
    const auto expected = nlohmann::json::parse(expected_file);
    for (unsigned wait = 0U;
      wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
      ++wait) {
      ctx->Yield();
    }
    IM_CHECK(app.current_scene_key_ && !app.active_scene_load_key_);
    IM_CHECK(std::string(nostd::to_string(*app.current_scene_key_))
      == expected.at("scene_key").get<std::string>());
    const auto scene = app.GetShell().TryGetScene();
    const auto renderer = app.ResolveVortexRenderer();
    IM_CHECK(scene && renderer);
    CheckPersistedLighting(*scene);
    if (ctx->IsError()) {
      return;
    }
    CheckGpuLighting(ctx, *renderer, *scene);
    if (ctx->IsError()) {
      return;
    }
    const auto state = renderer->InspectSkyLight(*scene);
    IM_CHECK_EQ(
      state.source_radiance_scale, expected.at("radiance_scale").get<float>());
    const auto brightness = expected.at("average_brightness").get<float>();
    IM_CHECK(std::abs(state.average_brightness - brightness)
      <= std::max(0.00001F, brightness * 0.00001F));
    SelectPanel(
      ctx, app.GetShell(), "Environment", imgui::icons::kIconEnvironment);
    ctx->Yield(3);
    ctx->SetRef("Environment");
    ctx->ItemOpen("**/Sky Light (IBL)");
    ctx->ItemOpen("**/Advanced##SkyLight");
    IM_CHECK(ctx->ItemExists("**/Specular Indirect"));
  };
}
// NOLINTEND(readability-magic-numbers)
} // namespace oxygen::examples::render_scene
