//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Async/AsyncShowcase.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/SceneActivationPolicy.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/EnvironmentSceneSnapshot.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/DroneCameraController.h"
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::examples::async {
namespace {
  constexpr double kTourSeconds = 60.0;
  constexpr double kChapterSeconds = 20.0;
  constexpr float kDaylightLux = 100000.0F;
  constexpr float kDaylightElevation = 50.0F;
  constexpr float kSunsetElevation = 4.0F;
  constexpr float kDaylightEv = 13.0F;
  constexpr float kSunsetEv = 10.0F;
  constexpr float kSpotlightEv = 5.0F;
  constexpr float kShowcaseFluxLm = 30000.0F;
  constexpr float kShowcaseRange = 70.0F;
  constexpr float kCameraFov = 45.0F;

  auto TourRoute() -> std::vector<glm::vec3>
  {
    constexpr glm::vec3 establishing { 28.0F, -28.0F, 18.0F };
    constexpr glm::vec3 material_entry { 18.0F, -22.0F, 10.0F };
    constexpr glm::vec3 material_close { 0.0F, -19.0F, 7.0F };
    constexpr glm::vec3 hero_close { -20.0F, -10.0F, 10.0F };
    constexpr glm::vec3 sunset { -25.0F, 8.0F, 13.0F };
    constexpr glm::vec3 shadows { -12.0F, 24.0F, 12.0F };
    constexpr glm::vec3 shadow_exit { 12.0F, 24.0F, 12.0F };
    constexpr glm::vec3 closing { 28.0F, 8.0F, 18.0F };
    return {
      establishing,
      material_entry,
      material_close,
      hero_close,
      sunset,
      shadows,
      shadow_exit,
      closing,
    };
  }
} // namespace

struct AsyncShowcase::Impl {
  DemoShell& shell;
  observer_ptr<scene::Scene> scene;
  scene::SceneNode camera;
  scene::SceneNode sun;
  scene::SceneNode spotlight;
  EnvironmentSceneSnapshot environment;
  std::optional<scene::SpotLight> original_spot;
  std::optional<scene::ExposureSettings> exposure;
  glm::vec3 position { 0.0F };
  glm::quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  float fov { 0.0F };
  ui::CameraControlMode camera_mode { ui::CameraControlMode::kFly };
  std::vector<glm::vec3> original_path;
  glm::vec3 original_focus { 0.0F };
  double original_speed { 0.0 };
  double original_progress { 0.0 };
  double original_ramp { 0.0 };
  double original_bob { 0.0 };
  double original_noise { 0.0 };
  float original_focus_strength { 0.0F };
  bool original_flying { false };
  bool active { false };
  bool playing { false };
  bool tour_configured { false };
  bool comparing { false };
  bool comparison_original { false };
  bool comparison_resume { false };
  std::optional<ShowcaseLighting> applied_lighting;
  float applied_phase { -1.0F };
  double elapsed { 0.0 };
  ShowcaseLighting lighting { ShowcaseLighting::kDaylight };

  Impl(DemoShell& owner, SceneBindings bindings)
    : shell(owner)
    , scene(bindings.scene)
    , camera(std::move(bindings.camera))
    , sun(std::move(bindings.sun))
    , spotlight(std::move(bindings.spotlight))
  {
    CHECK_NOTNULL_F(scene);
  }

  auto Camera() -> scene::PerspectiveCamera&
  {
    const auto component = camera.GetCameraAs<scene::PerspectiveCamera>();
    if (!component) {
      throw std::logic_error("Showcase camera has no perspective component");
    }
    return component->get();
  }

  [[nodiscard]] auto PostProcess() const
    -> scene::environment::PostProcessVolume&
  {
    auto env = scene->GetEnvironment();
    if (auto post
      = env->TryGetSystem<scene::environment::PostProcessVolume>()) {
      return *post;
    }
    return env->AddSystem<scene::environment::PostProcessVolume>();
  }

  auto Capture() -> void
  {
    if (active) {
      return;
    }
    environment.Capture(*scene);
    const auto spot = spotlight.GetLightAs<scene::SpotLight>();
    if (!spot) {
      throw std::logic_error("Showcase spotlight is unavailable");
    }
    original_spot = spot->get();
    if (const auto post = scene->GetEnvironment()
          ->TryGetSystem<scene::environment::PostProcessVolume>()) {
      exposure = post->GetExposureSettings();
    } else {
      exposure.reset();
    }
    const auto camera_position = camera.GetTransform().GetLocalPosition();
    const auto camera_rotation = camera.GetTransform().GetLocalRotation();
    if (!camera_position || !camera_rotation) {
      throw std::logic_error("Showcase camera transform is unavailable");
    }
    position = *camera_position;
    rotation = *camera_rotation;
    fov = Camera().GetFieldOfView();
    auto rig = shell.GetCameraRig();
    auto drone = rig->GetDroneController();
    camera_mode = rig->GetMode();
    original_path = drone->GetPathPoints();
    original_focus = drone->GetFocusTarget();
    original_speed = drone->GetSpeed();
    original_progress = drone->GetProgress();
    original_ramp = drone->GetRampTime();
    original_bob = drone->GetBobAmplitude();
    original_noise = drone->GetNoiseAmplitude();
    original_focus_strength = drone->GetFocusStrength();
    original_flying = drone->IsFlying();
    shell.GetCameraSettingsService().SetSceneActivationPolicy(
      SceneActivationPolicy::kExperimentOwned);
    active = true;
  }

  auto ApplyLighting(const float phase) -> void
  {
    const auto effective_phase = lighting == ShowcaseLighting::kSunset
      ? std::clamp(phase, 0.0F, 1.0F)
      : 0.0F;
    if (applied_lighting == lighting && applied_phase == effective_phase) {
      return;
    }
    applied_lighting = lighting;
    applied_phase = effective_phase;
    const bool night = lighting == ShowcaseLighting::kSpotlight;
    const float blend = lighting == ShowcaseLighting::kSunset
      ? std::clamp(phase, 0.0F, 1.0F)
      : 0.0F;
    const float elevation
      = std::lerp(kDaylightElevation, kSunsetElevation, blend);
    CHECK_F(
      sun.EditLight<scene::DirectionalLight>([night](auto& light) -> auto {
        light.Common().affects_world = !night;
        light.Common().casts_shadows = true;
        light.SetIntensityLux(kDaylightLux);
      }));
    sun.GetTransform().SetLocalRotation(
      glm::angleAxis(math::Pi - glm::radians(elevation), space::move::Right));
    CHECK_F(spotlight.EditLight<scene::SpotLight>([night](auto& light) -> auto {
      light.Common().affects_world = night;
      light.Common().casts_shadows = true;
      light.SetLuminousFluxLm(kShowcaseFluxLm);
      light.SetRange(kShowcaseRange);
    }));
    auto settings = exposure.value_or(scene::ExposureSettings {});
    settings.enabled = true;
    settings.mode = engine::ExposureMode::kManual;
    settings.compensation_ev = 0.0F;
    settings.manual_ev
      = night ? kSpotlightEv : std::lerp(kDaylightEv, kSunsetEv, blend);
    PostProcess().SetExposureSettings(settings);
  }
};

AsyncShowcase::AsyncShowcase(DemoShell& shell, SceneBindings bindings)
  : impl_(std::make_unique<Impl>(shell, std::move(bindings)))
{
}
AsyncShowcase::~AsyncShowcase()
{
  try {
    Explore();
  } catch (const std::exception& error) {
    LOG_F(ERROR, "Showcase state restoration failed during shutdown: {}",
      error.what());
  } catch (...) {
    LOG_F(ERROR, "Showcase state restoration failed during shutdown");
  }
}

auto AsyncShowcase::Play() -> void
{
  impl_->Capture();
  impl_->elapsed = 0.0;
  impl_->playing = true;
  impl_->tour_configured = true;
  auto rig = impl_->shell.GetCameraRig();
  auto drone = rig->GetDroneController();
  drone->SetPathGenerator(TourRoute);
  rig->SetMode(ui::CameraControlMode::kDrone);
  drone->SyncFromTransform(impl_->camera);
  drone->SetProgress(0.0);
  drone->SetSpeed(drone->GetPathLength() / kTourSeconds);
  drone->SetFocusStrength(1.0F);
  drone->SetRampTime(0.0);
  drone->SetBobAmplitude(0.0);
  drone->SetNoiseAmplitude(0.0);
  drone->Start();
  impl_->Camera().SetFieldOfView(glm::radians(kCameraFov));
  impl_->lighting = ShowcaseLighting::kDaylight;
  impl_->ApplyLighting(0.0F);
}

auto AsyncShowcase::Pause() -> void
{
  if (!impl_->active) {
    return;
  }
  if (!impl_->tour_configured) {
    Play();
    return;
  }
  impl_->playing = !impl_->playing;
  auto drone = impl_->shell.GetCameraRig()->GetDroneController();
  if (impl_->playing) {
    drone->Start();
  } else {
    drone->Stop();
  }
}

auto AsyncShowcase::Preview(const ShowcaseLighting mode) -> void
{
  impl_->Capture();
  impl_->playing = false;
  impl_->lighting = mode;
  impl_->tour_configured = false;
  impl_->shell.GetCameraRig()->GetDroneController()->Stop();
  impl_->ApplyLighting(1.0F);
}

auto AsyncShowcase::Explore() -> void
{
  if (!impl_ || !impl_->active) {
    return;
  }
  impl_->environment.Restore(*impl_->scene);
  if (impl_->original_spot) {
    CHECK_F(impl_->spotlight.ReplaceLight(
      std::make_unique<scene::SpotLight>(*impl_->original_spot)));
  }
  if (const auto saved_exposure = impl_->exposure) {
    impl_->PostProcess().SetExposureSettings(*saved_exposure);
  } else {
    impl_->scene->GetEnvironment()
      ->RemoveSystem<scene::environment::PostProcessVolume>();
  }
  impl_->camera.GetTransform().SetLocalPosition(impl_->position);
  impl_->camera.GetTransform().SetLocalRotation(impl_->rotation);
  impl_->Camera().SetFieldOfView(impl_->fov);
  auto rig = impl_->shell.GetCameraRig();
  auto drone = rig->GetDroneController();
  drone->SetPathGenerator(
    [path = impl_->original_path] -> std::vector<glm::vec3> { return path; });
  drone->SetFocusTarget(impl_->original_focus);
  drone->SetFocusStrength(impl_->original_focus_strength);
  drone->SetSpeed(impl_->original_speed);
  drone->SetRampTime(impl_->original_ramp);
  drone->SetBobAmplitude(impl_->original_bob);
  drone->SetNoiseAmplitude(impl_->original_noise);
  rig->SetMode(impl_->camera_mode);
  rig->SyncFromActiveCamera();
  drone->SetProgress(impl_->original_progress);
  if (impl_->original_flying) {
    drone->Start();
  } else {
    drone->Stop();
  }
  impl_->shell.GetCameraSettingsService().SetSceneActivationPolicy(
    SceneActivationPolicy::kRestorePreferences);
  impl_->active = false;
  impl_->comparing = false;
  impl_->applied_lighting.reset();
  impl_->playing = false;
}

auto AsyncShowcase::Update(const double seconds) -> void
{
  if (!impl_->active || !impl_->playing || !std::isfinite(seconds)
    || seconds <= 0.0) {
    return;
  }
  constexpr double kMaximumStepSeconds = 0.05;
  impl_->elapsed = std::min(
    impl_->elapsed + std::min(seconds, kMaximumStepSeconds), kTourSeconds);
  const auto chapter = static_cast<unsigned>(impl_->elapsed / kChapterSeconds);
  if (chapter == 0U) {
    impl_->lighting = ShowcaseLighting::kDaylight;
  } else if (chapter == 1U) {
    impl_->lighting = ShowcaseLighting::kSunset;
  } else {
    impl_->lighting = ShowcaseLighting::kSpotlight;
  }
  const auto blend
    = static_cast<float>((impl_->elapsed - kChapterSeconds) / kChapterSeconds);
  impl_->ApplyLighting(blend);
  constexpr glm::vec3 material_focus { 0.0F, 0.0F, 3.0F };
  impl_->shell.GetCameraRig()->GetDroneController()->SetFocusTarget(
    material_focus);
  if (impl_->elapsed >= kTourSeconds) {
    Pause();
  }
}

auto AsyncShowcase::SetComparisonHeld(const bool held) -> void
{
  if (!impl_->active || held == impl_->comparing) {
    return;
  }
  const bool shadows = impl_->lighting == ShowcaseLighting::kSpotlight;
  auto sky = impl_->scene->GetEnvironment()
               ->TryGetSystem<scene::environment::SkyLight>();
  if (held) {
    impl_->comparison_resume = impl_->playing;
    impl_->playing = false;
    impl_->shell.GetCameraRig()->GetDroneController()->Stop();
    const auto spot = impl_->spotlight.GetLightAs<scene::SpotLight>();
    if (!spot) {
      throw std::logic_error("Showcase spotlight is unavailable");
    }
    impl_->comparison_original
      = shadows ? spot->get().Common().casts_shadows : sky && sky->IsEnabled();
  }
  const bool value = held ? false : impl_->comparison_original;
  if (shadows) {
    CHECK_F(impl_->spotlight.EditLight<scene::SpotLight>(
      [value](auto& light) -> auto { light.Common().casts_shadows = value; }));
  } else if (sky) {
    sky->SetEnabled(value);
  }
  impl_->comparing = held;
  if (!held && impl_->comparison_resume) {
    impl_->playing = true;
    impl_->shell.GetCameraRig()->GetDroneController()->Start();
  }
}

auto AsyncShowcase::ComparisonLabel() const -> const char*
{
  return impl_->lighting == ShowcaseLighting::kSpotlight
    ? "Hold: no shadows"
    : "Hold: direct light only";
}

auto AsyncShowcase::IsActive() const -> bool { return impl_->active; }
auto AsyncShowcase::IsPaused() const -> bool
{
  return impl_->active
    && (impl_->comparing || (impl_->tour_configured && !impl_->playing));
}
auto AsyncShowcase::IsPlaying() const -> bool { return impl_->playing; }
auto AsyncShowcase::Progress() const -> float
{
  return static_cast<float>(impl_->elapsed / kTourSeconds);
}
auto AsyncShowcase::Caption() const -> const char*
{
  switch (impl_->lighting) {
  case ShowcaseLighting::kDaylight:
    return "Daylight: roughness and metalness shape the sky reflection";
  case ShowcaseLighting::kSunset:
    return "Sunset: atmospheric color and captured-sky lighting update "
           "together";
  case ShowcaseLighting::kSpotlight:
    return "Spotlight: direct lighting and cast shadows without ambient fill";
  }
  return "";
}
} // namespace oxygen::examples::async
