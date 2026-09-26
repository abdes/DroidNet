//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define IMGUI_DEFINE_MATH_OPERATORS
#include <string_view>

#include "RenderScene/MainModule.h"
#include <imgui_internal.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>

namespace oxygen::examples::render_scene {

namespace {
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
          != std::string_view::npos)
        profile = window;
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
}

// NOLINTBEGIN(readability-magic-numbers) - concrete widget values and wait
// bounds.
auto MainModule::RegisterUiTests(ImGuiTestEngine* engine) -> void
{
  auto* test = IM_REGISTER_TEST(engine, "renderscene", "ibl_controls");
  test->UserData = this;
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    for (unsigned wait = 0U;
      wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
      ++wait)
      ctx->Yield();
    IM_CHECK(app.current_scene_key_.has_value());
    IM_CHECK(!app.active_scene_load_key_.has_value());
    app.GetShell().SetActivePanel("Environment");
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
      wait < 120U && !renderer->InspectSkyLight(*scene).usable; ++wait)
      ctx->Yield();
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
    app.GetShell().SetActivePanel("Diagnostics");
    ctx->Yield(3);
    ctx->SetRef("Diagnostics");
    ctx->ItemOpen("**/Sky Lighting");
    ctx->ItemCheck("**/Frame Diagnostics");
    for (unsigned wait = 0U; wait < 120U
      && renderer->InspectSkyLight(*scene).gpu_validation
        != vortex::SkyLightGpuValidation::kValid;
      ++wait)
      ctx->Yield();
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
      ++wait)
      ctx->Yield();
    IM_CHECK(
      renderer->GetDiagnosticsService().GetLatestIblGpuTiming().has_value());
    app.GetShell().SetActivePanel("Environment");
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
  test->TestFunc = [](ImGuiTestContext* ctx) {
    auto& app = *static_cast<MainModule*>(ctx->Test->UserData);
    for (unsigned wait = 0U;
      wait < 600U && (!app.current_scene_key_ || app.active_scene_load_key_);
      ++wait)
      ctx->Yield();
    IM_CHECK(app.current_scene_key_.has_value());
    IM_CHECK(!app.active_scene_load_key_.has_value());
    app.GetShell().SetActivePanel("Environment");
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
}
// NOLINTEND(readability-magic-numbers)
} // namespace oxygen::examples::render_scene
