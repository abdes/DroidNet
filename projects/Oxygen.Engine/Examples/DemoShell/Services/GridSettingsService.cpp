//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/GridSettingsService.h"

#include <algorithm>
#include <cmath>

#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/GroundGridConfig.h>

#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/CameraRigController.h"

namespace oxygen::examples {

namespace {
  const vortex::GroundGridConfig kDefaultConfig {};

  constexpr float kMinSpacing = 1e-4F;
  constexpr float kMinThickness = 0.0F;
  constexpr float kMinFadeDistance = 0.0F;
  constexpr float kMinSmoothTime = 0.001F;

  constexpr std::string_view kEnabledKey = "ground_grid.enabled";
  constexpr std::string_view kSmoothMotionKey = "ground_grid.smooth_motion";
  constexpr std::string_view kSmoothTimeKey = "ground_grid.smooth_time";
  constexpr std::string_view kSpacingKey = "ground_grid.spacing";
  constexpr std::string_view kMajorEveryKey = "ground_grid.major_every";
  constexpr std::string_view kLineThicknessKey = "ground_grid.line_thickness";
  constexpr std::string_view kMajorThicknessKey = "ground_grid.major_thickness";
  constexpr std::string_view kAxisThicknessKey = "ground_grid.axis_thickness";
  constexpr std::string_view kFadeStartKey = "ground_grid.fade_start";
  constexpr std::string_view kFadePowerKey = "ground_grid.fade_power";
  constexpr std::string_view kHorizonBoostKey = "ground_grid.horizon_boost";
  constexpr std::string_view kMinorColorRKey = "ground_grid.minor_color.r";
  constexpr std::string_view kMinorColorGKey = "ground_grid.minor_color.g";
  constexpr std::string_view kMinorColorBKey = "ground_grid.minor_color.b";
  constexpr std::string_view kMinorColorAKey = "ground_grid.minor_color.a";
  constexpr std::string_view kMajorColorRKey = "ground_grid.major_color.r";
  constexpr std::string_view kMajorColorGKey = "ground_grid.major_color.g";
  constexpr std::string_view kMajorColorBKey = "ground_grid.major_color.b";
  constexpr std::string_view kMajorColorAKey = "ground_grid.major_color.a";
  constexpr std::string_view kAxisColorXRKey = "ground_grid.axis_color_x.r";
  constexpr std::string_view kAxisColorXGKey = "ground_grid.axis_color_x.g";
  constexpr std::string_view kAxisColorXBKey = "ground_grid.axis_color_x.b";
  constexpr std::string_view kAxisColorXAKey = "ground_grid.axis_color_x.a";
  constexpr std::string_view kAxisColorYRKey = "ground_grid.axis_color_y.r";
  constexpr std::string_view kAxisColorYGKey = "ground_grid.axis_color_y.g";
  constexpr std::string_view kAxisColorYBKey = "ground_grid.axis_color_y.b";
  constexpr std::string_view kAxisColorYAKey = "ground_grid.axis_color_y.a";
  constexpr std::string_view kOriginColorRKey = "ground_grid.origin_color.r";
  constexpr std::string_view kOriginColorGKey = "ground_grid.origin_color.g";
  constexpr std::string_view kOriginColorBKey = "ground_grid.origin_color.b";
  constexpr std::string_view kOriginColorAKey = "ground_grid.origin_color.a";

  auto ClampFloat(const float value, const float min_value) -> float
  {
    return std::max(value, min_value);
  }

  auto ClampFade(const float start) -> float
  {
    return std::max(start, kMinFadeDistance);
  }
} // namespace

struct GridSettingsService::GridConfig {
  bool enabled {};
  float spacing {};
  int major_every {};
  float line_thickness {};
  float major_thickness {};
  float axis_thickness {};
  bool smooth_motion {};
  float smooth_time {};
  float fade_start {};
  float fade_power {};
  float horizon_boost {};
  graphics::Color minor_color {};
  graphics::Color major_color {};
  graphics::Color axis_color_x {};
  graphics::Color axis_color_y {};
  graphics::Color origin_color {};
};

auto GridSettingsService::BindCameraRig(
  observer_ptr<ui::CameraRigController> camera_rig) -> void
{
  camera_rig_ = camera_rig;
}

auto GridSettingsService::BindVortexRenderer(
  observer_ptr<vortex::Renderer> renderer) -> void
{
  DCHECK_NOTNULL_F(renderer);
  vortex_renderer_ = renderer;
  ApplyGridConfig(ReadConfig());
}

auto GridSettingsService::GetEnabled() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kEnabledKey).value_or(kDefaultConfig.enabled);
}

auto GridSettingsService::SetEnabled(const bool enabled) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kEnabledKey, enabled);
  epoch_++;
}

auto GridSettingsService::GetGridSpacing() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value
    = settings->GetFloat(kSpacingKey).value_or(kDefaultConfig.spacing);
  return ClampFloat(value, kMinSpacing);
}

auto GridSettingsService::SetGridSpacing(const float spacing) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kSpacingKey, ClampFloat(spacing, kMinSpacing));
  epoch_++;
}

auto GridSettingsService::GetMajorEvery() const -> int
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value
    = settings->GetFloat(kMajorEveryKey)
        .value_or(static_cast<float>(kDefaultConfig.major_every));
  return std::max(1, static_cast<int>(std::lround(value)));
}

auto GridSettingsService::SetMajorEvery(const int major_every) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(
    kMajorEveryKey, static_cast<float>(std::max(1, major_every)));
  epoch_++;
}

auto GridSettingsService::GetLineThickness() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return ClampFloat(settings->GetFloat(kLineThicknessKey)
                      .value_or(kDefaultConfig.line_thickness),
    kMinThickness);
}

auto GridSettingsService::SetLineThickness(const float thickness) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kLineThicknessKey, ClampFloat(thickness, kMinThickness));
  epoch_++;
}

auto GridSettingsService::GetMajorThickness() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return ClampFloat(settings->GetFloat(kMajorThicknessKey)
                      .value_or(kDefaultConfig.major_thickness),
    kMinThickness);
}

auto GridSettingsService::SetMajorThickness(const float thickness) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kMajorThicknessKey, ClampFloat(thickness, kMinThickness));
  epoch_++;
}

auto GridSettingsService::GetAxisThickness() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return ClampFloat(settings->GetFloat(kAxisThicknessKey)
                      .value_or(kDefaultConfig.axis_thickness),
    kMinThickness);
}

auto GridSettingsService::SetAxisThickness(const float thickness) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAxisThicknessKey, ClampFloat(thickness, kMinThickness));
  epoch_++;
}

auto GridSettingsService::GetSmoothMotion() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kSmoothMotionKey)
    .value_or(kDefaultConfig.smooth_motion);
}

auto GridSettingsService::SetSmoothMotion(const bool enabled) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kSmoothMotionKey, enabled);
  epoch_++;
}

auto GridSettingsService::GetSmoothTime() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return ClampFloat(
    settings->GetFloat(kSmoothTimeKey).value_or(kDefaultConfig.smooth_time),
    kMinSmoothTime);
}

auto GridSettingsService::SetSmoothTime(const float time) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kSmoothTimeKey, ClampFloat(time, kMinSmoothTime));
  epoch_++;
}

auto GridSettingsService::GetFadeStart() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return ClampFade(
    settings->GetFloat(kFadeStartKey).value_or(kDefaultConfig.fade_start));
}

auto GridSettingsService::SetFadeStart(const float distance) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kFadeStartKey, ClampFade(distance));
  epoch_++;
}

auto GridSettingsService::GetFadePower() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kFadePowerKey).value_or(kDefaultConfig.fade_power);
}

auto GridSettingsService::SetFadePower(const float power) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kFadePowerKey, power);
  epoch_++;
}

auto GridSettingsService::GetHorizonBoost() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kHorizonBoostKey)
    .value_or(kDefaultConfig.horizon_boost);
}

auto GridSettingsService::SetHorizonBoost(const float boost) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kHorizonBoostKey, boost);
  epoch_++;
}

auto GridSettingsService::GetMinorColor() const -> graphics::Color
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return {
    settings->GetFloat(kMinorColorRKey).value_or(kDefaultConfig.minor_color.r),
    settings->GetFloat(kMinorColorGKey).value_or(kDefaultConfig.minor_color.g),
    settings->GetFloat(kMinorColorBKey).value_or(kDefaultConfig.minor_color.b),
    settings->GetFloat(kMinorColorAKey).value_or(kDefaultConfig.minor_color.a),
  };
}

auto GridSettingsService::SetMinorColor(const graphics::Color& color) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kMinorColorRKey, color.r);
  settings->SetFloat(kMinorColorGKey, color.g);
  settings->SetFloat(kMinorColorBKey, color.b);
  settings->SetFloat(kMinorColorAKey, color.a);
  epoch_++;
}

auto GridSettingsService::GetMajorColor() const -> graphics::Color
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return {
    settings->GetFloat(kMajorColorRKey).value_or(kDefaultConfig.major_color.r),
    settings->GetFloat(kMajorColorGKey).value_or(kDefaultConfig.major_color.g),
    settings->GetFloat(kMajorColorBKey).value_or(kDefaultConfig.major_color.b),
    settings->GetFloat(kMajorColorAKey).value_or(kDefaultConfig.major_color.a),
  };
}

auto GridSettingsService::SetMajorColor(const graphics::Color& color) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kMajorColorRKey, color.r);
  settings->SetFloat(kMajorColorGKey, color.g);
  settings->SetFloat(kMajorColorBKey, color.b);
  settings->SetFloat(kMajorColorAKey, color.a);
  epoch_++;
}

auto GridSettingsService::GetAxisColorX() const -> graphics::Color
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return {
    settings->GetFloat(kAxisColorXRKey).value_or(kDefaultConfig.axis_color_x.r),
    settings->GetFloat(kAxisColorXGKey).value_or(kDefaultConfig.axis_color_x.g),
    settings->GetFloat(kAxisColorXBKey).value_or(kDefaultConfig.axis_color_x.b),
    settings->GetFloat(kAxisColorXAKey).value_or(kDefaultConfig.axis_color_x.a),
  };
}

auto GridSettingsService::SetAxisColorX(const graphics::Color& color) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAxisColorXRKey, color.r);
  settings->SetFloat(kAxisColorXGKey, color.g);
  settings->SetFloat(kAxisColorXBKey, color.b);
  settings->SetFloat(kAxisColorXAKey, color.a);
  epoch_++;
}

auto GridSettingsService::GetAxisColorY() const -> graphics::Color
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return {
    settings->GetFloat(kAxisColorYRKey).value_or(kDefaultConfig.axis_color_y.r),
    settings->GetFloat(kAxisColorYGKey).value_or(kDefaultConfig.axis_color_y.g),
    settings->GetFloat(kAxisColorYBKey).value_or(kDefaultConfig.axis_color_y.b),
    settings->GetFloat(kAxisColorYAKey).value_or(kDefaultConfig.axis_color_y.a),
  };
}

auto GridSettingsService::SetAxisColorY(const graphics::Color& color) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAxisColorYRKey, color.r);
  settings->SetFloat(kAxisColorYGKey, color.g);
  settings->SetFloat(kAxisColorYBKey, color.b);
  settings->SetFloat(kAxisColorYAKey, color.a);
  epoch_++;
}

auto GridSettingsService::GetOriginColor() const -> graphics::Color
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return {
    settings->GetFloat(kOriginColorRKey)
      .value_or(kDefaultConfig.origin_color.r),
    settings->GetFloat(kOriginColorGKey)
      .value_or(kDefaultConfig.origin_color.g),
    settings->GetFloat(kOriginColorBKey)
      .value_or(kDefaultConfig.origin_color.b),
    settings->GetFloat(kOriginColorAKey)
      .value_or(kDefaultConfig.origin_color.a),
  };
}

auto GridSettingsService::SetOriginColor(const graphics::Color& color) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kOriginColorRKey, color.r);
  settings->SetFloat(kOriginColorGKey, color.g);
  settings->SetFloat(kOriginColorBKey, color.b);
  settings->SetFloat(kOriginColorAKey, color.a);
  epoch_++;
}

auto GridSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto GridSettingsService::OnFrameStart(const engine::FrameContext& /*context*/)
  -> void
{
  auto config = ReadConfig();
  UpdateGridOrigin(config);
  ApplyGridConfig(config);
}

auto GridSettingsService::OnSceneActivated(scene::Scene& /*scene*/) -> void
{
  grid_origin_ = { 0.0F, 0.0F };
  has_origin_ = false;
}

auto GridSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/,
  const vortex::CompositionView& /*view*/) -> void
{
  ApplyGridConfig(ReadConfig());
}

auto GridSettingsService::ReadConfig() const -> GridConfig
{
  return {
    .enabled = GetEnabled(),
    .spacing = GetGridSpacing(),
    .major_every = GetMajorEvery(),
    .line_thickness = GetLineThickness(),
    .major_thickness = GetMajorThickness(),
    .axis_thickness = GetAxisThickness(),
    .smooth_motion = GetSmoothMotion(),
    .smooth_time = GetSmoothTime(),
    .fade_start = ClampFade(GetFadeStart()),
    .fade_power = GetFadePower(),
    .horizon_boost = GetHorizonBoost(),
    .minor_color = GetMinorColor(),
    .major_color = GetMajorColor(),
    .axis_color_x = GetAxisColorX(),
    .axis_color_y = GetAxisColorY(),
    .origin_color = GetOriginColor(),
  };
}

auto GridSettingsService::UpdateGridOrigin(const GridConfig& /*config*/) -> void
{
  grid_origin_ = { 0.0F, 0.0F };
  has_origin_ = true;
}

auto GridSettingsService::ApplyGridConfig(const GridConfig& config) -> void
{
  if (!vortex_renderer_) {
    return;
  }

  vortex_renderer_->SetGroundGridConfig(vortex::GroundGridConfig {
    .enabled = config.enabled,
    .spacing = config.spacing,
    .major_every = static_cast<std::uint32_t>(std::max(1, config.major_every)),
    .line_thickness = config.line_thickness,
    .major_thickness = config.major_thickness,
    .axis_thickness = config.axis_thickness,
    .fade_start = config.fade_start,
    .fade_power = config.fade_power,
    .horizon_boost = config.horizon_boost,
    .origin = grid_origin_,
    .smooth_motion = config.smooth_motion,
    .smooth_time = config.smooth_time,
    .minor_color = config.minor_color,
    .major_color = config.major_color,
    .axis_color_x = config.axis_color_x,
    .axis_color_y = config.axis_color_y,
    .origin_color = config.origin_color,
  });
}

} // namespace oxygen::examples
