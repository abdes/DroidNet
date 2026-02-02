//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/ObserverPtr.h>

#include "Async/AsyncDemoTypes.h"

namespace oxygen::scene {
class SceneNode;
}

namespace oxygen::examples::async {

class AsyncDemoSettingsService;

//! View model for the Async Demo panel.
/*!
 Bridges the Async demo panel UI with the underlying simulation state and
 settings.
*/
class AsyncDemoVm {
public:
  explicit AsyncDemoVm(observer_ptr<AsyncDemoSettingsService> settings,
    observer_ptr<scene::SceneNode> spotlight_node,
    const FrameActionTracker* frame_tracker,
    const std::vector<SphereState>* spheres);

  // --- Panel Sections ---

  [[nodiscard]] auto GetSceneSectionOpen() -> bool;
  auto SetSceneSectionOpen(bool open) -> void;

  [[nodiscard]] auto GetSpotlightSectionOpen() -> bool;
  auto SetSpotlightSectionOpen(bool open) -> void;

  [[nodiscard]] auto GetProfilerSectionOpen() -> bool;
  auto SetProfilerSectionOpen(bool open) -> void;

  // --- Scene Info (Read-only) ---

  [[nodiscard]] auto GetSphereCount() const -> size_t;
  [[nodiscard]] auto GetAnimationTime() const -> double;

  // Get details string for a specific sphere (e.g. "Sphere 1: Speed 1.4,
  // Radius 10.0")
  [[nodiscard]] auto GetSphereInfo(size_t index) const -> std::string;

  // --- Spotlight ---

  [[nodiscard]] auto IsSpotlightAvailable() const -> bool;

  [[nodiscard]] auto GetSpotlightIntensity() -> float;
  auto SetSpotlightIntensity(float intensity) -> void;

  [[nodiscard]] auto GetSpotlightRange() -> float;
  auto SetSpotlightRange(float range) -> void;

  [[nodiscard]] auto GetSpotlightInnerCone() -> float;
  auto SetSpotlightInnerCone(float angle_rad) -> void;

  [[nodiscard]] auto GetSpotlightOuterCone() -> float;
  auto SetSpotlightOuterCone(float angle_rad) -> void;

  [[nodiscard]] auto GetSpotlightEnabled() -> bool;
  auto SetSpotlightEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetSpotlightCastsShadows() -> bool;
  auto SetSpotlightCastsShadows(bool casts_shadows) -> void;

  // Ensures the spotlight exists (managed by MainModule)
  using EnsureSpotlightCallback = std::function<void()>;
  void SetEnsureSpotlightCallback(EnsureSpotlightCallback cb);
  void EnsureSpotlight();

  // --- Profiler ---

  [[nodiscard]] auto GetPhaseTimings() const
    -> const std::vector<std::pair<std::string, std::chrono::microseconds>>&;
  [[nodiscard]] auto GetFrameActions() const -> const std::vector<std::string>&;

  // Sync animation time from module
  void SetAnimationTime(double time);

private:
  void Refresh();

  mutable std::mutex mutex_;
  observer_ptr<AsyncDemoSettingsService> settings_;
  observer_ptr<scene::SceneNode> spotlight_node_;
  const FrameActionTracker* frame_tracker_;
  const std::vector<SphereState>* spheres_;

  EnsureSpotlightCallback ensure_spotlight_cb_;

  double anim_time_ { 0.0 };
  std::uint64_t epoch_ { 0 };

  // Cached values
  bool scene_open_ { true };
  bool spotlight_open_ { true };
  bool profiler_open_ { true };
};

} // namespace oxygen::examples::async
