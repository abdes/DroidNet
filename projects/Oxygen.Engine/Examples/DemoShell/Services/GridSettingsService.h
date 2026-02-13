//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/DomainService.h"

namespace oxygen::examples::ui {
class CameraRigController;
} // namespace oxygen::examples::ui

namespace oxygen::examples {
class RenderingPipeline;

//! Settings persistence and runtime wiring for the demo ground grid.
class GridSettingsService final : public DomainService {
public:
  GridSettingsService() = default;
  ~GridSettingsService() override = default;

  OXYGEN_MAKE_NON_COPYABLE(GridSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(GridSettingsService)

  //! Bind the demo camera rig used to position the grid.
  auto BindCameraRig(observer_ptr<ui::CameraRigController> camera_rig) -> void;
  auto Initialize(observer_ptr<RenderingPipeline> pipeline) -> void;

  // Settings accessors
  [[nodiscard]] auto GetEnabled() const -> bool;
  auto SetEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetPlaneSize() const -> float;
  auto SetPlaneSize(float size) -> void;

  [[nodiscard]] auto GetGridSpacing() const -> float;
  auto SetGridSpacing(float spacing) -> void;

  [[nodiscard]] auto GetMajorEvery() const -> int;
  auto SetMajorEvery(int major_every) -> void;

  [[nodiscard]] auto GetLineThickness() const -> float;
  auto SetLineThickness(float thickness) -> void;

  [[nodiscard]] auto GetMajorThickness() const -> float;
  auto SetMajorThickness(float thickness) -> void;

  [[nodiscard]] auto GetAxisThickness() const -> float;
  auto SetAxisThickness(float thickness) -> void;

  [[nodiscard]] auto GetFadeStart() const -> float;
  auto SetFadeStart(float distance) -> void;

  [[nodiscard]] auto GetFadeEnd() const -> float;
  auto SetFadeEnd(float distance) -> void;

  [[nodiscard]] auto GetFadePower() const -> float;
  auto SetFadePower(float power) -> void;

  [[nodiscard]] auto GetThicknessMaxScale() const -> float;
  auto SetThicknessMaxScale(float scale) -> void;

  [[nodiscard]] auto GetDepthBias() const -> float;
  auto SetDepthBias(float bias) -> void;

  [[nodiscard]] auto GetHorizonBoost() const -> float;
  auto SetHorizonBoost(float boost) -> void;

  [[nodiscard]] auto GetMinorColor() const -> graphics::Color;
  auto SetMinorColor(const graphics::Color& color) -> void;

  [[nodiscard]] auto GetMajorColor() const -> graphics::Color;
  auto SetMajorColor(const graphics::Color& color) -> void;

  [[nodiscard]] auto GetRecenterThreshold() const -> float;
  auto SetRecenterThreshold(float threshold) -> void;

  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t override;

  // DomainService
  auto OnFrameStart(const engine::FrameContext& context) -> void override;
  auto OnSceneActivated(scene::Scene& scene) -> void override;
  auto OnMainViewReady(const engine::FrameContext& context,
    const CompositionView& view) -> void override;

private:
  struct GridConfig {
    bool enabled { true };
    float plane_size { 1000.0F };
    float spacing { 1.0F };
    int major_every { 10 };
    float line_thickness { 0.02F };
    float major_thickness { 0.04F };
    float axis_thickness { 0.06F };
    float fade_start { 0.0F };
    float fade_end { 0.0F };
    float fade_power { 1.0F };
    float thickness_max_scale { 32.0F };
    float depth_bias { 1e-4F };
    float horizon_boost { 0.0F };
    float recenter_threshold { 0.0F };
    graphics::Color minor_color { 0.35F, 0.35F, 0.35F, 1.0F };
    graphics::Color major_color { 0.55F, 0.55F, 0.55F, 1.0F };
  };

  auto ReadConfig() const -> GridConfig;
  auto UpdateGridOrigin(const GridConfig& config) -> void;
  auto ApplyGridConfig(const GridConfig& config) -> void;

  observer_ptr<RenderingPipeline> pipeline_ { nullptr };
  observer_ptr<ui::CameraRigController> camera_rig_ { nullptr };
  Vec2 grid_origin_ { 0.0F, 0.0F };
  bool has_origin_ { false };

  mutable std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples
