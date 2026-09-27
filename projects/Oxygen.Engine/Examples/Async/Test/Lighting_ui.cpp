//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <string>

#include "Async/AsyncDemoVm.h"
#include "Async/MainModule.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Test/UiTestSession.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraRigController.h"
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::examples::async {

auto MainModule::RegisterUiTests(ImGuiTestEngine* engine) -> void
{
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
    ctx->SetRef("//DemoPanelSideBar");
    const auto environment_icon = std::string(imgui::icons::kIconEnvironment);
    ctx->ItemClick(environment_icon.c_str());
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
}

} // namespace oxygen::examples::async
