//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/PreviewSunController.h"

#include <algorithm>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/DefaultSceneLighting.h"

namespace oxygen::examples {
namespace {

  auto CollectDirectionals(scene::Scene& scene) -> std::vector<scene::SceneNode>
  {
    auto result = std::vector<scene::SceneNode> {};
    auto stack = scene.GetRootNodes();
    std::ranges::reverse(stack);
    while (!stack.empty()) {
      auto node = stack.back();
      stack.pop_back();
      auto children = std::vector<scene::SceneNode> {};
      for (auto child = node.GetFirstChild(); child.has_value();
        child = child->GetNextSibling()) {
        children.push_back(*child);
      }
      stack.insert(stack.end(), children.rbegin(), children.rend());
      if (node.GetLightAs<scene::DirectionalLight>().has_value()) {
        result.push_back(node);
      }
    }
    return result;
  }

  auto ApplyPreviewRole(scene::Scene& scene, scene::SceneNode& node) -> void
  {
    auto& light = node.GetLightAs<scene::DirectionalLight>()->get();
    if (!light.Common().affects_world || !light.GetEnvironmentContribution()
      || !light.IsSunLight()
      || light.GetAtmosphereLightSlot()
        != scene::AtmosphereLightSlot::kPrimary) {
      light.Common().affects_world = true;
      light.SetEnvironmentContribution(true);
      light.SetIsSunLight(true);
      light.SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
      scene.GetDirectionalLightResolver().OnLightChanged(node.GetHandle());
    }
  }

} // namespace

auto PreviewSunController::InspectSources(scene::Scene& scene) const
  -> PreviewSunSources
{
  auto result = PreviewSunSources {};
  auto first_directional = scene::SceneNode {};
  auto first_visible = scene::SceneNode {};
  for (auto node : CollectDirectionals(scene)) {
    if (bound_scene_ == &scene && node.GetHandle() == sun_.GetHandle()) {
      if (!injected_) {
        result.candidate = node;
      }
      continue;
    }
    const auto& light = node.GetLightAs<scene::DirectionalLight>()->get();
    if (!result.authored_sun.IsAlive()
      && (light.IsSunLight()
        || light.GetAtmosphereLightSlot()
          != scene::AtmosphereLightSlot::kNone)) {
      result.authored_sun = node;
    }
    if (!first_directional.IsAlive()) {
      first_directional = node;
    }
    if (!first_visible.IsAlive()) {
      const auto flags = node.GetFlags();
      if (flags.has_value()
        && flags->get().GetEffectiveValue(scene::SceneNodeFlags::kVisible)) {
        first_visible = node;
      }
    }
  }
  if (!result.candidate.IsAlive()) {
    result.candidate
      = first_visible.IsAlive() ? first_visible : first_directional;
  }
  return result;
}

auto PreviewSunController::CanEnable(scene::Scene& scene) const -> bool
{
  return !InspectSources(scene).authored_sun.IsAlive();
}

auto PreviewSunController::Update(scene::Scene& scene, const bool enabled,
  const std::string_view preferred_node_name) -> void
{
  if (bound_scene_ != &scene) {
    Reset();
    bound_scene_ = &scene;
  }
  if (!enabled) {
    ReleaseSun(scene);
    status_ = PreviewSunStatus::kDisabled;
    return;
  }
  const auto sources = InspectSources(scene);
  if (sources.authored_sun.IsAlive()) {
    ReleaseSun(scene);
    status_ = PreviewSunStatus::kBlockedByAuthoredSun;
    return;
  }
  if (preferred_node_name_ != preferred_node_name) {
    ReleaseSun(scene);
  }
  if (sun_.IsAlive()
    && sun_.GetLightAs<scene::DirectionalLight>().has_value()) {
    if (!injected_ || !sources.candidate.IsAlive()) {
      ApplyPreviewRole(scene, sun_);
      yielded_ = false;
      status_ = injected_ ? PreviewSunStatus::kInjected
                          : PreviewSunStatus::kReusedDirectional;
      return;
    }
  }
  ReleaseSun(scene);
  preferred_node_name_ = preferred_node_name;

  auto candidate = scene::SceneNode {};
  if (!preferred_node_name.empty()) {
    auto directionals = CollectDirectionals(scene);
    std::erase_if(directionals, [preferred_node_name](const auto& node) {
      return node.GetName() != preferred_node_name;
    });
    if (directionals.size() != 1U) {
      status_ = PreviewSunStatus::kSourceUnavailable;
      return;
    }
    candidate = directionals.front();
  } else {
    candidate = InspectSources(scene).candidate;
  }
  if (!candidate.IsAlive()) {
    sun_ = CreatePreviewSun(
      scene, DefaultSceneLightingDesc { .sun_node_name = "Preview Sun" });
    injected_ = true;
    status_ = PreviewSunStatus::kInjected;
    return;
  }

  sun_ = candidate;
  auto& light = sun_.GetLightAs<scene::DirectionalLight>()->get();
  authored_state_ = AuthoredDirectionalState { light.Common().affects_world,
    light.GetEnvironmentContribution(), light.IsSunLight(),
    light.GetAtmosphereLightSlot() };
  ApplyPreviewRole(scene, sun_);
  status_ = PreviewSunStatus::kReusedDirectional;
}

auto PreviewSunController::YieldToAuthoredSun(scene::Scene& scene) -> void
{
  if (bound_scene_ != &scene || !sun_.IsAlive() || yielded_
    || !InspectSources(scene).authored_sun.IsAlive()) {
    return;
  }
  if (injected_) {
    if (auto light = sun_.GetLightAs<scene::DirectionalLight>()) {
      light->get().Common().affects_world = false;
      light->get().SetEnvironmentContribution(false);
      light->get().SetIsSunLight(false);
      light->get().SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kNone);
      scene.GetDirectionalLightResolver().OnLightChanged(sun_.GetHandle());
    }
    yielded_ = true;
  } else {
    // A borrowed node needs only scalar restoration; ReleaseSun does not
    // destroy or sync in this branch.
    ReleaseSun(scene);
  }
  status_ = PreviewSunStatus::kBlockedByAuthoredSun;
}

auto PreviewSunController::Reset() noexcept -> void
{
  bound_scene_ = nullptr;
  sun_ = {};
  authored_state_.reset();
  preferred_node_name_.clear();
  injected_ = false;
  yielded_ = false;
  status_ = PreviewSunStatus::kDisabled;
}

auto PreviewSunController::ReleaseSun(scene::Scene& scene) -> void
{
  if (sun_.IsAlive()) {
    if (injected_) {
      CHECK_F(
        scene.DestroyNode(sun_), "failed to destroy the owned preview sun");
      scene.SyncObservers();
    } else if (const auto light = sun_.GetLightAs<scene::DirectionalLight>();
      light.has_value() && authored_state_.has_value()) {
      auto& directional = light->get();
      directional.Common().affects_world = authored_state_->affects_world;
      directional.SetEnvironmentContribution(
        authored_state_->environment_contribution);
      directional.SetIsSunLight(authored_state_->is_sun_light);
      directional.SetAtmosphereLightSlot(authored_state_->atmosphere_slot);
      scene.GetDirectionalLightResolver().OnLightChanged(sun_.GetHandle());
    }
  }
  sun_ = {};
  authored_state_.reset();
  preferred_node_name_.clear();
  injected_ = false;
  yielded_ = false;
}

} // namespace oxygen::examples
