//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <mutex>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Types/Color.h>

namespace oxygen::examples {
class GridSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

//! View model for the demo ground grid panel.
class GridVm {
public:
  explicit GridVm(observer_ptr<GridSettingsService> service);

  [[nodiscard]] auto GetEnabled() -> bool;
  auto SetEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetPlaneSize() -> float;
  auto SetPlaneSize(float size) -> void;

  [[nodiscard]] auto GetGridSpacing() -> float;
  auto SetGridSpacing(float spacing) -> void;

  [[nodiscard]] auto GetMajorEvery() -> int;
  auto SetMajorEvery(int major_every) -> void;

  [[nodiscard]] auto GetLineThickness() -> float;
  auto SetLineThickness(float thickness) -> void;

  [[nodiscard]] auto GetMajorThickness() -> float;
  auto SetMajorThickness(float thickness) -> void;

  [[nodiscard]] auto GetAxisThickness() -> float;
  auto SetAxisThickness(float thickness) -> void;

  [[nodiscard]] auto GetFadeStart() -> float;
  auto SetFadeStart(float distance) -> void;

  [[nodiscard]] auto GetFadeEnd() -> float;
  auto SetFadeEnd(float distance) -> void;

  [[nodiscard]] auto GetFadePower() -> float;
  auto SetFadePower(float power) -> void;

  [[nodiscard]] auto GetThicknessMaxScale() -> float;
  auto SetThicknessMaxScale(float scale) -> void;

  [[nodiscard]] auto GetDepthBias() -> float;
  auto SetDepthBias(float bias) -> void;

  [[nodiscard]] auto GetHorizonBoost() -> float;
  auto SetHorizonBoost(float boost) -> void;

  [[nodiscard]] auto GetMinorColor() -> graphics::Color;
  auto SetMinorColor(const graphics::Color& color) -> void;

  [[nodiscard]] auto GetMajorColor() -> graphics::Color;
  auto SetMajorColor(const graphics::Color& color) -> void;

  [[nodiscard]] auto GetRecenterThreshold() -> float;
  auto SetRecenterThreshold(float threshold) -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  mutable std::mutex mutex_;
  observer_ptr<GridSettingsService> service_;
  std::uint64_t epoch_ { std::numeric_limits<std::uint64_t>::max() };

  bool enabled_ { true };
  float plane_size_ { 1000.0F };
  float spacing_ { 1.0F };
  int major_every_ { 10 };
  float line_thickness_ { 0.02F };
  float major_thickness_ { 0.04F };
  float axis_thickness_ { 0.06F };
  float fade_start_ { 0.0F };
  float fade_end_ { 0.0F };
  float fade_power_ { 2.0F };
  float thickness_max_scale_ { 64.0F };
  float depth_bias_ { 1e-4F };
  float horizon_boost_ { 0.35F };
  graphics::Color minor_color_ { 0.30F, 0.30F, 0.30F, 1.0F };
  graphics::Color major_color_ { 0.50F, 0.50F, 0.50F, 1.0F };
  float recenter_threshold_ { 0.0F };
};

} // namespace oxygen::examples::ui
