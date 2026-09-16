//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/EnvironmentSceneSnapshot.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::examples {

namespace {

  template <typename Callback>
  auto VisitNodes(scene::Scene& scene, Callback&& callback) -> void
  {
    auto stack = scene.GetRootNodes();
    while (!stack.empty()) {
      auto node = stack.back();
      stack.pop_back();
      callback(node);
      for (auto child = node.GetFirstChild(); child.has_value();
        child = child->GetNextSibling()) {
        stack.push_back(*child);
      }
    }
  }

  template <typename T>
  auto CaptureSystem(const scene::SceneEnvironment& environment)
    -> std::optional<T>
  {
    if (const auto system = environment.TryGetSystem<T>()) {
      return *system;
    }
    return std::nullopt;
  }

  template <typename T>
  auto RestoreSystem(
    scene::SceneEnvironment& environment, const std::optional<T>& saved) -> void
  {
    if (!saved.has_value()) {
      environment.RemoveSystem<T>();
    } else if (environment.HasSystem<T>()) {
      environment.ReplaceSystem<T>(*saved);
    } else {
      environment.AddSystem<T>(*saved);
    }
  }

} // namespace

struct EnvironmentSceneSnapshot::State {
  struct DirectionalState {
    scene::NodeHandle node;
    scene::DirectionalLight light;
    glm::quat local_rotation;
  };

  struct LocalFogState {
    scene::NodeHandle node;
    scene::environment::LocalFogVolume fog;
  };

  std::weak_ptr<scene::Scene> owner_scene;
  bool had_environment { false };
  std::optional<scene::environment::SkyAtmosphere> atmosphere;
  std::optional<scene::environment::SkySphere> sky_sphere;
  std::optional<scene::environment::SkyLight> sky_light;
  std::optional<scene::environment::Fog> fog;
  std::vector<DirectionalState> directional_lights;
  std::vector<LocalFogState> local_fog_volumes;
};

EnvironmentSceneSnapshot::EnvironmentSceneSnapshot() = default;
EnvironmentSceneSnapshot::~EnvironmentSceneSnapshot() = default;

auto EnvironmentSceneSnapshot::Capture(scene::Scene& scene) -> void
{
  auto state = std::make_unique<State>();
  state->owner_scene = scene.weak_from_this();
  CHECK_F(!state->owner_scene.expired(),
    "environment snapshots require a shared-owned scene");
  if (const auto environment = scene.GetEnvironment()) {
    state->had_environment = true;
    state->atmosphere
      = CaptureSystem<scene::environment::SkyAtmosphere>(*environment);
    state->sky_sphere
      = CaptureSystem<scene::environment::SkySphere>(*environment);
    state->sky_light
      = CaptureSystem<scene::environment::SkyLight>(*environment);
    state->fog = CaptureSystem<scene::environment::Fog>(*environment);
  }

  VisitNodes(scene, [&](scene::SceneNode& node) {
    if (const auto light = node.GetLightAs<scene::DirectionalLight>()) {
      const auto rotation = node.GetTransform().GetLocalRotation();
      CHECK_F(rotation.has_value(),
        "directional light node must have a local rotation");
      state->directional_lights.push_back(State::DirectionalState {
        .node = node.GetHandle(),
        .light = light->get(),
        .local_rotation = *rotation,
      });
    }
    const auto impl = node.GetImpl();
    if (impl.has_value()
      && impl->get().HasComponent<scene::environment::LocalFogVolume>()) {
      state->local_fog_volumes.push_back(State::LocalFogState {
        .node = node.GetHandle(),
        .fog = impl->get().GetComponent<scene::environment::LocalFogVolume>(),
      });
    }
  });
  state_ = std::move(state);
}

auto EnvironmentSceneSnapshot::Restore(scene::Scene& scene) const -> void
{
  if (!state_) {
    return;
  }
  const auto owner = state_->owner_scene.lock();
  if (owner.get() != &scene) {
    return;
  }

  auto environment = scene.GetEnvironment();
  if (!environment && state_->had_environment) {
    scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    environment = scene.GetEnvironment();
  }
  if (environment) {
    RestoreSystem(*environment, state_->atmosphere);
    RestoreSystem(*environment, state_->sky_sphere);
    RestoreSystem(*environment, state_->sky_light);
    RestoreSystem(*environment, state_->fog);
    if (!state_->had_environment && environment->GetSystemCount() == 0U) {
      scene.SetEnvironment(nullptr);
    }
  }

  for (const auto& saved : state_->directional_lights) {
    if (auto node = scene.GetNode(saved.node)) {
      // Replacement rebinds the copied light's transform dependency and queues
      // the light-change notification consumed by the directional resolver.
      CHECK_F(node->ReplaceLight(
                std::make_unique<scene::DirectionalLight>(saved.light)),
        "failed to restore authored directional light");
      CHECK_F(node->GetTransform().SetLocalRotation(saved.local_rotation),
        "failed to restore authored directional light rotation");
    }
  }

  VisitNodes(scene, [&](scene::SceneNode& node) {
    const auto impl = node.GetImpl();
    if (!impl.has_value()) {
      return;
    }
    auto& node_impl = impl->get();
    const auto saved = std::ranges::find_if(
      state_->local_fog_volumes, [&](const State::LocalFogState& entry) {
        return entry.node == node.GetHandle();
      });
    const bool has_fog
      = node_impl.HasComponent<scene::environment::LocalFogVolume>();
    if (saved == state_->local_fog_volumes.end()) {
      if (has_fog) {
        node_impl.RemoveComponent<scene::environment::LocalFogVolume>();
      }
    } else if (has_fog) {
      node_impl.ReplaceComponent<scene::environment::LocalFogVolume>(
        saved->fog);
    } else {
      node_impl.AddComponent<scene::environment::LocalFogVolume>(saved->fog);
    }
  });

  scene.Update(false);
  scene.SyncObservers();
}

auto EnvironmentSceneSnapshot::Reset() -> void { state_.reset(); }

} // namespace oxygen::examples
