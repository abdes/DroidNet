//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "Async/AsyncDemoSettingsService.h"
#include "Async/AsyncDemoTypes.h"
#include "Async/AsyncDemoVm.h"
#include <fmt/format.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::async {

AsyncDemoVm::AsyncDemoVm(observer_ptr<AsyncDemoSettingsService> settings,
  observer_ptr<scene::SceneNode> spotlight_node,
  const FrameActionTracker* frame_tracker,
  const std::vector<SphereState>* spheres)
  : settings_(settings)
  , spotlight_node_(spotlight_node)
  , frame_tracker_(frame_tracker)
  , spheres_(spheres)
{
}

auto AsyncDemoVm::GetSceneSectionOpen() -> bool
{
  return settings_->GetSceneSectionOpen();
}

auto AsyncDemoVm::SetSceneSectionOpen(bool open) -> void
{
  settings_->SetSceneSectionOpen(open);
}

auto AsyncDemoVm::GetSpotlightSectionOpen() -> bool
{
  return settings_->GetSpotlightSectionOpen();
}

auto AsyncDemoVm::SetSpotlightSectionOpen(bool open) -> void
{
  settings_->SetSpotlightSectionOpen(open);
}

auto AsyncDemoVm::GetProfilerSectionOpen() -> bool
{
  return settings_->GetProfilerSectionOpen();
}

auto AsyncDemoVm::SetProfilerSectionOpen(bool open) -> void
{
  settings_->SetProfilerSectionOpen(open);
}

auto AsyncDemoVm::GetSphereCount() const -> size_t
{
  return spheres_ ? spheres_->size() : 0;
}

auto AsyncDemoVm::GetAnimationTime() const -> double { return anim_time_; }

auto AsyncDemoVm::GetSphereInfo(size_t index) const -> std::string
{
  if (!spheres_ || index >= spheres_->size()) {
    return {};
  }
  const auto& sphere = spheres_->at(index);
  return fmt::format("Sphere {}: Speed {:.1F}, Radius {:.1F}", index + 1,
    sphere.speed, sphere.radius);
}

auto AsyncDemoVm::IsSpotlightAvailable() const -> bool
{
  return spotlight_node_ && spotlight_node_->IsAlive()
    && spotlight_node_->GetLightAs<scene::SpotLight>().has_value();
}

auto AsyncDemoVm::GetSpotlightIntensity() -> float
{
  // Sync from scene node if available
  if (IsSpotlightAvailable()) {
    auto light = spotlight_node_->GetLightAs<scene::SpotLight>();
    if (light) {
      return light->get().GetLuminousFluxLm();
    }
  }
  return settings_->GetSpotlightIntensity();
}

auto AsyncDemoVm::SetSpotlightIntensity(float intensity) -> void
{
  if (IsSpotlightAvailable()
    && !spotlight_node_->EditLight<scene::SpotLight>(
      [intensity](
        auto& light) -> auto { light.SetLuminousFluxLm(intensity); })) {
    return;
  }
  settings_->SetSpotlightIntensity(intensity);
}

auto AsyncDemoVm::GetSpotlightRange() -> float
{
  if (IsSpotlightAvailable()) {
    auto light = spotlight_node_->GetLightAs<scene::SpotLight>();
    if (light) {
      return light->get().GetRange();
    }
  }
  return settings_->GetSpotlightRange();
}

auto AsyncDemoVm::SetSpotlightRange(float range) -> void
{
  if (IsSpotlightAvailable()
    && !spotlight_node_->EditLight<scene::SpotLight>(
      [range](auto& light) -> auto { light.SetRange(range); })) {
    return;
  }
  settings_->SetSpotlightRange(range);
}

auto AsyncDemoVm::GetSpotlightInnerCone() -> float
{
  if (IsSpotlightAvailable()) {
    auto light = spotlight_node_->GetLightAs<scene::SpotLight>();
    if (light) {
      return light->get().GetInnerConeAngleRadians();
    }
  }
  return settings_->GetSpotlightInnerCone();
}

auto AsyncDemoVm::SetSpotlightInnerCone(float angle_rad) -> void
{
  const auto outer = std::max(GetSpotlightOuterCone(), angle_rad);
  if (IsSpotlightAvailable()
    && !spotlight_node_->EditLight<scene::SpotLight>(
      [angle_rad, outer](auto& light) -> auto {
        light.SetConeAnglesRadians(angle_rad, outer);
      })) {
    return;
  }
  settings_->SetSpotlightInnerCone(angle_rad);
  settings_->SetSpotlightOuterCone(outer);
}

auto AsyncDemoVm::GetSpotlightOuterCone() -> float
{
  if (IsSpotlightAvailable()) {
    auto light = spotlight_node_->GetLightAs<scene::SpotLight>();
    if (light) {
      return light->get().GetOuterConeAngleRadians();
    }
  }
  return settings_->GetSpotlightOuterCone();
}

auto AsyncDemoVm::SetSpotlightOuterCone(float angle_rad) -> void
{
  const auto inner = std::min(GetSpotlightInnerCone(), angle_rad);
  if (IsSpotlightAvailable()
    && !spotlight_node_->EditLight<scene::SpotLight>(
      [inner, angle_rad](auto& light) -> auto {
        light.SetConeAnglesRadians(inner, angle_rad);
      })) {
    return;
  }
  settings_->SetSpotlightInnerCone(inner);
  settings_->SetSpotlightOuterCone(angle_rad);
}

auto AsyncDemoVm::GetSpotlightEnabled() -> bool
{
  if (IsSpotlightAvailable()) {
    auto light = spotlight_node_->GetLightAs<scene::SpotLight>();
    if (light) {
      return light->get().Common().affects_world;
    }
  }
  return settings_->GetSpotlightEnabled();
}

auto AsyncDemoVm::SetSpotlightEnabled(bool enabled) -> void
{
  if (IsSpotlightAvailable()
    && !spotlight_node_->EditLight<scene::SpotLight>(
      [enabled](
        auto& light) -> auto { light.Common().affects_world = enabled; })) {
    return;
  }
  settings_->SetSpotlightEnabled(enabled);
}

auto AsyncDemoVm::GetSpotlightCastsShadows() -> bool
{
  if (IsSpotlightAvailable()) {
    auto light = spotlight_node_->GetLightAs<scene::SpotLight>();
    if (light) {
      return light->get().Common().casts_shadows;
    }
  }
  return settings_->GetSpotlightCastsShadows();
}

auto AsyncDemoVm::SetSpotlightCastsShadows(bool casts_shadows) -> void
{
  if (IsSpotlightAvailable()
    && !spotlight_node_->EditLight<scene::SpotLight>(
      [casts_shadows](auto& light) -> auto {
        light.Common().casts_shadows = casts_shadows;
      })) {
    return;
  }
  settings_->SetSpotlightCastsShadows(casts_shadows);
}

void AsyncDemoVm::SetEnsureSpotlightCallback(EnsureSpotlightCallback cb)
{
  ensure_spotlight_cb_ = std::move(cb);
}

void AsyncDemoVm::EnsureSpotlight()
{
  if (ensure_spotlight_cb_) {
    ensure_spotlight_cb_();
  }
}

auto AsyncDemoVm::GetPhaseTimings() const
  -> const std::vector<std::pair<std::string, std::chrono::microseconds>>&
{
  static const std::vector<std::pair<std::string, std::chrono::microseconds>>
    empty;
  return frame_tracker_ ? frame_tracker_->phase_timings : empty;
}

auto AsyncDemoVm::GetFrameActions() const -> const std::vector<std::string>&
{
  static const std::vector<std::string> empty;
  return frame_tracker_ ? frame_tracker_->frame_actions : empty;
}

void AsyncDemoVm::SetAnimationTime(double time) { anim_time_ = time; }

} // namespace oxygen::examples::async
