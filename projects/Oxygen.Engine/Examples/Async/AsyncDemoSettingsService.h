//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class SettingsService;
}

namespace oxygen::examples::async {

//! Settings persistence for Async demo specific options.
/*!
 Manages settings for the Async demo panel, including section collapse states
 and spotlight configuration. Persists changes to the SettingsService.
*/
class AsyncDemoSettingsService {
public:
  AsyncDemoSettingsService() = default;
  virtual ~AsyncDemoSettingsService() = default;

  OXYGEN_MAKE_NON_COPYABLE(AsyncDemoSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(AsyncDemoSettingsService)

  // --- Panel State ---

  [[nodiscard]] virtual auto GetSceneSectionOpen() const -> bool;
  virtual auto SetSceneSectionOpen(bool open) -> void;

  [[nodiscard]] virtual auto GetSpotlightSectionOpen() const -> bool;
  virtual auto SetSpotlightSectionOpen(bool open) -> void;

  [[nodiscard]] virtual auto GetProfilerSectionOpen() const -> bool;
  virtual auto SetProfilerSectionOpen(bool open) -> void;

  // --- Spotlight Settings ---

  [[nodiscard]] virtual auto GetSpotlightIntensity() const -> float;
  virtual auto SetSpotlightIntensity(float intensity) -> void;

  [[nodiscard]] virtual auto GetSpotlightRange() const -> float;
  virtual auto SetSpotlightRange(float range) -> void;

  [[nodiscard]] virtual auto GetSpotlightColor() const -> glm::vec3;
  virtual auto SetSpotlightColor(glm::vec3 color) -> void;

  [[nodiscard]] virtual auto GetSpotlightInnerCone() const -> float;
  virtual auto SetSpotlightInnerCone(float angle_rad) -> void;

  [[nodiscard]] virtual auto GetSpotlightOuterCone() const -> float;
  virtual auto SetSpotlightOuterCone(float angle_rad) -> void;

  [[nodiscard]] virtual auto GetSpotlightEnabled() const -> bool;
  virtual auto SetSpotlightEnabled(bool enabled) -> void;

  [[nodiscard]] virtual auto GetSpotlightCastsShadows() const -> bool;
  virtual auto SetSpotlightCastsShadows(bool casts_shadows) -> void;

  // --- Epoch ---

  [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

protected:
  [[nodiscard]] virtual auto ResolveSettings() const noexcept
    -> observer_ptr<SettingsService>;

private:
  static constexpr const char* kSceneOpenKey = "async_demo.scene_open";
  static constexpr const char* kSpotlightOpenKey = "async_demo.spotlight_open";
  static constexpr const char* kProfilerOpenKey = "async_demo.profiler_open";

  static constexpr const char* kSpotlightIntensityKey
    = "async_demo.spotlight_intensity";
  static constexpr const char* kSpotlightRangeKey
    = "async_demo.spotlight_range";
  static constexpr const char* kSpotlightColorRKey
    = "async_demo.spotlight_color_r";
  static constexpr const char* kSpotlightColorGKey
    = "async_demo.spotlight_color_g";
  static constexpr const char* kSpotlightColorBKey
    = "async_demo.spotlight_color_b";
  static constexpr const char* kSpotlightInnerConeKey
    = "async_demo.spotlight_inner_cone";
  static constexpr const char* kSpotlightOuterConeKey
    = "async_demo.spotlight_outer_cone";
  static constexpr const char* kSpotlightEnabledKey
    = "async_demo.spotlight_enabled";
  static constexpr const char* kSpotlightShadowsKey
    = "async_demo.spotlight_shadows";

  mutable std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples::async
