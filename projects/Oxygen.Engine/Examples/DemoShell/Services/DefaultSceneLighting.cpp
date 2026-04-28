//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/DefaultSceneLighting.h"

#include <cmath>
#include <memory>
#include <string>

#include <glm/common.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples {

namespace {

  auto RotationFromDirection(const glm::vec3& direction_ws) -> glm::quat
  {
    if (glm::dot(direction_ws, direction_ws)
      <= math::EpsilonDirection * math::EpsilonDirection) {
      return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    }

    constexpr glm::vec3 from_dir = space::move::Forward;
    const glm::vec3 to_dir = glm::normalize(direction_ws);
    const float cos_theta = glm::clamp(glm::dot(from_dir, to_dir), -1.0F, 1.0F);
    if (cos_theta >= 0.9999F) {
      return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    }
    if (cos_theta <= -0.9999F) {
      return glm::angleAxis(glm::pi<float>(), space::move::Up);
    }

    return glm::angleAxis(std::acos(cos_theta),
      glm::normalize(glm::cross(from_dir, to_dir)));
  }

  auto LookRotation(const glm::vec3& position, const glm::vec3& target)
    -> glm::quat
  {
    return RotationFromDirection(target - position);
  }

  auto EnsureSceneEnvironment(scene::Scene& scene) -> void
  {
    auto env = scene.GetEnvironment();
    if (!env) {
      scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
      env = scene.GetEnvironment();
    }
    CHECK_NOTNULL_F(env, "scene environment is required");

    auto atmosphere = env->TryGetSystem<scene::environment::SkyAtmosphere>();
    if (!atmosphere) {
      atmosphere = observer_ptr {
        &env->AddSystem<scene::environment::SkyAtmosphere>()
      };
    }
    atmosphere->SetEnabled(true);
    atmosphere->SetSunDiskEnabled(true);

    auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
    if (!sky_light) {
      sky_light
        = observer_ptr { &env->AddSystem<scene::environment::SkyLight>() };
    }
    sky_light->SetEnabled(true);
    sky_light->SetSource(scene::environment::SkyLightSource::kCapturedScene);
    sky_light->SetIntensityMul(1.0F);
    sky_light->SetDiffuseIntensity(1.0F);
    sky_light->SetSpecularIntensity(1.0F);
    sky_light->SetTintRgb({ 1.0F, 1.0F, 1.0F });
  }

  auto EnsureSceneSun(scene::Scene& scene, const DefaultSceneLightingDesc& desc)
    -> scene::SceneNode
  {
    scene.Update(false);
    scene.SyncObservers();
    scene.GetDirectionalLightResolver().Validate();
    if (const auto primary
      = scene.GetDirectionalLightResolver().ResolvePrimarySun();
      primary.has_value()) {
      const auto node = scene.GetNode(primary->NodeHandle());
      CHECK_F(node.has_value(), "resolved primary sun node is unavailable");
      return *node;
    }

    auto sun_node = scene.CreateNode(std::string(desc.sun_node_name));

    auto light = std::make_unique<scene::DirectionalLight>();
    light->Common().affects_world = true;
    light->Common().casts_shadows = desc.casts_shadows;
    light->Common().mobility = scene::LightMobility::kRealtime;
    light->Common().color_rgb = desc.sun_color_rgb;
    light->SetAngularSizeRadians(glm::radians(desc.sun_source_angle_degrees));
    light->SetIntensityLux(desc.sun_intensity_lux);
    light->SetEnvironmentContribution(true);
    light->SetIsSunLight(true);
    light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
    light->SetUsePerPixelAtmosphereTransmittance(true);
    light->SetAtmosphereDiskLuminanceScale({ 1.0F, 0.95F, 0.9F, 1.0F });
    CHECK_F(sun_node.AttachLight(std::move(light)),
      "failed to attach DirectionalLight to '{}'",
      std::string(desc.sun_node_name));
    sun_node.GetTransform().SetLocalPosition(desc.sun_position);
    sun_node.GetTransform().SetLocalRotation(
      LookRotation(desc.sun_position, desc.focus_point));
    scene.Update(false);
    scene.SyncObservers();
    return sun_node;
  }

} // namespace

auto EnsureDefaultSceneLighting(
  scene::Scene& scene, const DefaultSceneLightingDesc& desc) -> scene::SceneNode
{
  EnsureSceneEnvironment(scene);
  return EnsureSceneSun(scene, desc);
}

} // namespace oxygen::examples
