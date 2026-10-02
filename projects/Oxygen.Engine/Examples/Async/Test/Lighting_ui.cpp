//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>

#include "Async/AsyncDemoVm.h"
#include "Async/AsyncShowcase.h"
#include "Async/MainModule.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Test/UiTestSession.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraRigController.h"
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/geometric.hpp>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::examples::async {

auto MainModule::RegisterUiTests(ImGuiTestEngine* engine) -> void
{
  constexpr float kTourWarningSeconds = 90.0F;
  constexpr float kTourTimeoutSeconds = 120.0F;
  auto& test_io = ImGuiTestEngine_GetIO(engine);
  test_io.ConfigWatchdogWarning = kTourWarningSeconds;
  test_io.ConfigWatchdogKillTest = kTourTimeoutSeconds;
  auto* test = IM_REGISTER_TEST(engine, "async", "lighting");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    IM_CHECK(testing::UiTestSession::UsesIsolatedSettings());
    constexpr unsigned kReadyFrames = 120U;
    for (unsigned wait = 0U;
      wait < kReadyFrames && !app.vm_->IsSpotlightAvailable(); ++wait) {
      ctx->Yield();
    }
    IM_CHECK(app.vm_->IsSpotlightAvailable());
    IM_CHECK(app.vm_->GetSpotlightCastsShadows());
    app.GetShell().GetCameraRig()->SetMode(ui::CameraControlMode::kFly);
    app.vm_->SetAnimationEnabled(false);
    const auto time = app.vm_->GetAnimationTime();
    ctx->Yield(3);
    IM_CHECK_EQ(app.vm_->GetAnimationTime(), time);
    const auto camera
      = app.main_camera_.GetCameraAs<scene::PerspectiveCamera>();
    IM_CHECK(camera);
    const auto original_fov = camera->get().GetFieldOfView();
    constexpr float kEditedFovRadians = 1.0F;
    camera->get().SetFieldOfView(kEditedFovRadians);
    ctx->Yield(3);
    IM_CHECK_EQ(camera->get().GetFieldOfView(), kEditedFovRadians);
    camera->get().SetFieldOfView(original_fov);

    const auto scene = app.GetShell().TryGetScene();
    const auto renderer = app.ResolveVortexRenderer();
    const auto graphics = app.app_.gfx_weak.lock();
    IM_CHECK(scene && renderer && graphics);
    auto post = scene->GetEnvironment()
                  ->TryGetSystem<scene::environment::PostProcessVolume>();
    IM_CHECK(post);
    auto exposure = post->GetExposureSettings();
    exposure.mode = engine::ExposureMode::kManual;
    constexpr float kDaylightEv = 13.0F;
    constexpr float kSpotlightEv = 5.0F;
    exposure.manual_ev = kDaylightEv;
    post->SetExposureSettings(exposure);
    ctx->Yield(3);
    const auto view
      = renderer->ResolvePublishedRuntimeViewId(app.main_view_id_);
    const auto applied = renderer->InspectExposureSettings(view);
    IM_CHECK(applied && applied->active_settings);
    IM_CHECK_EQ(applied->active_settings->manual_ev, kDaylightEv);

    const auto capture = graphics->GetFrameCaptureController();
    const auto save_capture = [&](const char* name) -> void {
      if (capture && capture->IsAvailable()) {
        const auto path = app.UiTestOutputDirectory() / name;
        IM_CHECK(capture->SetCaptureFileTemplate(path.string()));
        IM_CHECK(capture->TriggerNextFrame());
        ctx->Yield(8);
        IM_CHECK(!capture->IsCapturing());
      }
    };
    save_capture("daylight");

    // Exercise the actual sun control, including captured-sky invalidation.
    const auto active_panel = app.GetShell().GetActivePanelName();
    if (!active_panel || *active_panel != "Environment") {
      ctx->SetRef("//DemoPanelSideBar");
      ctx->ItemClick(imgui::icons::kIconEnvironment.data());
    }
    ctx->Yield(3);
    ctx->SetRef("//Environment");
    ctx->ItemOpen("**/Sun");
    ctx->ItemUncheck("**/Enabled##Sun");
    ctx->Yield(3);
    const auto sun = app.sun_light_.GetLightAs<scene::DirectionalLight>();
    IM_CHECK(sun && !sun->get().Common().affects_world);
    IM_CHECK(!scene->GetDirectionalLightResolver().ResolvePrimarySun());

    exposure.manual_ev = kSpotlightEv;
    post->SetExposureSettings(exposure);
    constexpr float kTestFluxLm = 30000.0F;
    constexpr float kTestRangeMeters = 60.0F;
    app.vm_->SetSpotlightIntensity(kTestFluxLm);
    app.vm_->SetSpotlightRange(kTestRangeMeters);
    ctx->Yield(8);
    save_capture("spot-shadowed");
    app.vm_->SetSpotlightCastsShadows(false);
    ctx->Yield(3);
    save_capture("spot-unshadowed");
    app.vm_->SetSpotlightEnabled(false);
    ctx->Yield(8);
    save_capture("lights-off");
    IM_CHECK(!app.vm_->GetSpotlightEnabled());

    app.vm_->SetSpotlightEnabled(true);
    app.vm_->SetSpotlightCastsShadows(true);
    ctx->ItemCheck("**/Enabled##Sun");
    app.vm_->SetAnimationEnabled(true);
    ctx->Yield(3);
    IM_CHECK_GT(app.vm_->GetAnimationTime(), time);
  };
  test = IM_REGISTER_TEST(engine, "async", "tour_controls");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    IM_CHECK(testing::UiTestSession::UsesIsolatedSettings());
    constexpr unsigned kReadyFrames = 120U;
    constexpr int kControlSettleFrames = 6;
    for (unsigned wait = 0U; wait < kReadyFrames && !app.showcase_; ++wait) {
      ctx->Yield();
    }
    IM_CHECK(app.showcase_);
    auto& show = *app.showcase_;
    const auto original_position
      = app.main_camera_.GetTransform().GetLocalPosition().value();
    const auto original_rotation
      = app.main_camera_.GetTransform().GetLocalRotation().value();
    const auto original_mode
      = SettingsService::ForDemoApp()->GetString("camera_rig.MainCamera.mode");
    const auto original_flux = app.vm_->GetSpotlightIntensity();
    const auto original_range = app.vm_->GetSpotlightRange();
    const auto scene = app.GetShell().TryGetScene();
    IM_CHECK(scene);
    const auto sky
      = scene->GetEnvironment()->TryGetSystem<scene::environment::SkyLight>();
    IM_CHECK(sky);
    const bool original_sky = sky->IsEnabled();
    const auto active_panel = app.GetShell().GetActivePanelName();
    if (!active_panel || *active_panel != "Async Demo") {
      ctx->SetRef("//DemoPanelSideBar");
      ctx->ItemClick(imgui::icons::kIconDemoPanel.data());
      ctx->Yield(3);
    }
    ctx->SetRef("//Async Demo");
    ctx->ItemClick("Play tour");
    ctx->Yield(kControlSettleFrames);
    IM_CHECK(show.IsPlaying());
    ctx->SetRef("//Async presentation");
    ctx->ItemClick("Pause");
    const auto paused_progress = show.Progress();
    const auto paused_position
      = app.main_camera_.GetTransform().GetLocalPosition();
    ctx->Yield(kControlSettleFrames);
    IM_CHECK(show.IsPaused());
    IM_CHECK_EQ(show.Progress(), paused_progress);
    IM_CHECK(
      app.main_camera_.GetTransform().GetLocalPosition() == paused_position);
    ctx->MouseMove("Hold: direct light only");
    ctx->MouseDown();
    ctx->Yield(3);
    IM_CHECK(!sky->IsEnabled());
    ctx->MouseUp();
    ctx->Yield(2);
    IM_CHECK_EQ(sky->IsEnabled(), original_sky);
    ctx->ItemClick("Resume");
    ctx->Yield(kControlSettleFrames);
    IM_CHECK_GT(show.Progress(), paused_progress);
    ctx->ItemClick("Spotlight##LightingPreset");
    ctx->Yield(4);
    const auto sun = app.sun_light_.GetLightAs<scene::DirectionalLight>();
    IM_CHECK(sun && !sun->get().Common().affects_world);
    IM_CHECK(app.vm_->GetSpotlightEnabled());
    ctx->MouseMove("Hold: no shadows");
    ctx->MouseDown();
    ctx->Yield(3);
    IM_CHECK(!app.vm_->GetSpotlightCastsShadows());
    ctx->MouseUp();
    ctx->Yield(2);
    IM_CHECK(app.vm_->GetSpotlightCastsShadows());
    ctx->ItemClick("Explore");
    ctx->Yield(3);
    IM_CHECK(!show.IsActive());
    constexpr float kPoseTolerance = 0.0001F;
    IM_CHECK_LT(
      glm::distance(app.main_camera_.GetTransform().GetLocalPosition().value(),
        original_position),
      kPoseTolerance);
    IM_CHECK_GT(std::abs(glm::dot(
                  app.main_camera_.GetTransform().GetLocalRotation().value(),
                  original_rotation)),
      1.0F - kPoseTolerance);
    IM_CHECK_EQ(app.vm_->GetSpotlightIntensity(), original_flux);
    IM_CHECK_EQ(app.vm_->GetSpotlightRange(), original_range);
    IM_CHECK(
      SettingsService::ForDemoApp()->GetString("camera_rig.MainCamera.mode")
      == original_mode);
  };

  test = IM_REGISTER_TEST(engine, "async", "tour_playback");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) -> void {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    IM_CHECK(testing::UiTestSession::UsesIsolatedSettings());
    constexpr unsigned kReadyFrames = 120U;
    for (unsigned wait = 0U; wait < kReadyFrames && !app.showcase_; ++wait) {
      ctx->Yield();
    }
    IM_CHECK(app.showcase_);
    app.showcase_->Play();
    const auto graphics = app.app_.gfx_weak.lock();
    IM_CHECK(graphics);
    const auto capture = graphics->GetFrameCaptureController();
    constexpr std::array kCaptureProgress { 0.15F, 0.5F, 0.85F };
    constexpr std::array kCaptureNames {
      "tour-daylight",
      "tour-sunset",
      "tour-spotlight",
    };
    std::size_t next_capture = 0U;
    constexpr unsigned kMaximumFrames = 30000U;
    for (unsigned frame = 0U;
      frame < kMaximumFrames && app.showcase_->IsPlaying(); ++frame) {
      if (next_capture < kCaptureProgress.size()
        && app.showcase_->Progress() >= kCaptureProgress.at(next_capture)) {
        if (capture && capture->IsAvailable()) {
          const auto path
            = app.UiTestOutputDirectory() / kCaptureNames.at(next_capture);
          IM_CHECK(capture->SetCaptureFileTemplate(path.string()));
          IM_CHECK(capture->TriggerNextFrame());
          ctx->Yield(8);
        }
        ++next_capture;
      }
      ctx->Yield();
    }
    IM_CHECK(app.showcase_->IsPaused());
    IM_CHECK_EQ(app.showcase_->Progress(), 1.0F);
    IM_CHECK_EQ(next_capture, kCaptureProgress.size());
    app.showcase_->Explore();
    IM_CHECK(!app.showcase_->IsActive());
  };
}

} // namespace oxygen::examples::async
