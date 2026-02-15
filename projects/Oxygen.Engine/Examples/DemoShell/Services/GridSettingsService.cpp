//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>

#include <Oxygen/Renderer/Passes/GroundGridPass.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>
#include <Oxygen/Renderer/Pipeline/RenderingPipeline.h>

#include "DemoShell/Services/GridSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/CameraRigController.h"

namespace oxygen::examples {

namespace {
  const engine::GroundGridPassConfig kDefaultConfig;

  constexpr float kMinSpacing = 1e-4F;
  constexpr float kMinThickness = 0.0F;
  constexpr float kMinFadeDistance = 0.0F;

  constexpr std::string_view kEnabledKey = "ground_grid.enabled";
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

  auto ClampFloat(float value, float min_value) -> float
  {
    return std::max(value, min_value);
  }

  auto ClampFade(float start) -> float
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
  float fade_start {};
  float fade_power {};
  float horizon_boost {};
  graphics::Color minor_color;
  graphics::Color major_color;
  graphics::Color axis_color_x;
  graphics::Color axis_color_y;
  graphics::Color origin_color;
};

auto GridSettingsService::BindCameraRig(
  observer_ptr<ui::CameraRigController> camera_rig) -> void
{
  camera_rig_ = camera_rig;
}

auto GridSettingsService::Initialize(
  observer_ptr<renderer::RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;
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
  const float value = settings->GetFloat(kLineThicknessKey)
                        .value_or(kDefaultConfig.line_thickness);
  return ClampFloat(value, kMinThickness);
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
  const float value = settings->GetFloat(kMajorThicknessKey)
                        .value_or(kDefaultConfig.major_thickness);
  return ClampFloat(value, kMinThickness);
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
  const float value = settings->GetFloat(kAxisThicknessKey)
                        .value_or(kDefaultConfig.axis_thickness);
  return ClampFloat(value, kMinThickness);
}

auto GridSettingsService::SetAxisThickness(const float thickness) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kAxisThicknessKey, ClampFloat(thickness, kMinThickness));
  epoch_++;
}

auto GridSettingsService::GetFadeStart() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const auto value
    = settings->GetFloat(kFadeStartKey).value_or(kDefaultConfig.fade_start);
  return ClampFade(value);
}

auto GridSettingsService::SetFadeStart(const float distance) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kFadeStartKey, ClampFloat(distance, kMinFadeDistance));
  epoch_++;
}

auto GridSettingsService::GetFadePower() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value
    = settings->GetFloat(kFadePowerKey).value_or(kDefaultConfig.fade_power);
  return ClampFloat(value, 0.0F);
}

auto GridSettingsService::SetFadePower(const float power) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kFadePowerKey, ClampFloat(power, 0.0F));
  epoch_++;
}

auto GridSettingsService::GetHorizonBoost() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value = settings->GetFloat(kHorizonBoostKey)
                        .value_or(kDefaultConfig.horizon_boost);
  return ClampFloat(value, 0.0F);
}

auto GridSettingsService::SetHorizonBoost(const float boost) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kHorizonBoostKey, ClampFloat(boost, 0.0F));
  epoch_++;
}

auto GridSettingsService::GetMinorColor() const -> graphics::Color
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float r = settings->GetFloat(kMinorColorRKey)
                    .value_or(kDefaultConfig.minor_color.r);
  const float g = settings->GetFloat(kMinorColorGKey)
                    .value_or(kDefaultConfig.minor_color.g);
  const float b = settings->GetFloat(kMinorColorBKey)
                    .value_or(kDefaultConfig.minor_color.b);
  const float a = settings->GetFloat(kMinorColorAKey)
                    .value_or(kDefaultConfig.minor_color.a);
  return graphics::Color { r, g, b, a };
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
  const float r = settings->GetFloat(kMajorColorRKey)
                    .value_or(kDefaultConfig.major_color.r);
  const float g = settings->GetFloat(kMajorColorGKey)
                    .value_or(kDefaultConfig.major_color.g);
  const float b = settings->GetFloat(kMajorColorBKey)
                    .value_or(kDefaultConfig.major_color.b);
  const float a = settings->GetFloat(kMajorColorAKey)
                    .value_or(kDefaultConfig.major_color.a);
  return graphics::Color { r, g, b, a };
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
  const float r = settings->GetFloat(kAxisColorXRKey)
                    .value_or(kDefaultConfig.axis_color_x.r);
  const float g = settings->GetFloat(kAxisColorXGKey)
                    .value_or(kDefaultConfig.axis_color_x.g);
  const float b = settings->GetFloat(kAxisColorXBKey)
                    .value_or(kDefaultConfig.axis_color_x.b);
  const float a = settings->GetFloat(kAxisColorXAKey)
                    .value_or(kDefaultConfig.axis_color_x.a);
  return graphics::Color { r, g, b, a };
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
  const float r = settings->GetFloat(kAxisColorYRKey)
                    .value_or(kDefaultConfig.axis_color_y.r);
  const float g = settings->GetFloat(kAxisColorYGKey)
                    .value_or(kDefaultConfig.axis_color_y.g);
  const float b = settings->GetFloat(kAxisColorYBKey)
                    .value_or(kDefaultConfig.axis_color_y.b);
  const float a = settings->GetFloat(kAxisColorYAKey)
                    .value_or(kDefaultConfig.axis_color_y.a);
  return graphics::Color { r, g, b, a };
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
  const float r = settings->GetFloat(kOriginColorRKey)
                    .value_or(kDefaultConfig.origin_color.r);
  const float g = settings->GetFloat(kOriginColorGKey)
                    .value_or(kDefaultConfig.origin_color.g);
  const float b = settings->GetFloat(kOriginColorBKey)
                    .value_or(kDefaultConfig.origin_color.b);
  const float a = settings->GetFloat(kOriginColorAKey)
                    .value_or(kDefaultConfig.origin_color.a);
  return graphics::Color { r, g, b, a };
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
  if (!pipeline_) {
    return;
  }

  GridConfig config = ReadConfig();

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
  const renderer::CompositionView& /*view*/) -> void
{
  if (!pipeline_) {
    return;
  }
  GridConfig config = ReadConfig();
  ApplyGridConfig(config);
}

auto GridSettingsService::ReadConfig() const -> GridConfig
{
  GridConfig config {};
  config.enabled = GetEnabled();
  config.spacing = GetGridSpacing();
  config.major_every = GetMajorEvery();
  config.line_thickness = GetLineThickness();
  config.major_thickness = GetMajorThickness();
  config.axis_thickness = GetAxisThickness();
  config.fade_power = GetFadePower();
  config.horizon_boost = GetHorizonBoost();

  const float fade_start = GetFadeStart();
  config.fade_start = ClampFade(fade_start);

  config.minor_color = GetMinorColor();
  config.major_color = GetMajorColor();
  config.axis_color_x = GetAxisColorX();
  config.axis_color_y = GetAxisColorY();
  config.origin_color = GetOriginColor();
  return config;
}

auto GridSettingsService::UpdateGridOrigin(const GridConfig& config) -> void
{
  (void)config;
  grid_origin_ = { 0.0F, 0.0F };
  has_origin_ = true;
}

auto GridSettingsService::ApplyGridConfig(const GridConfig& config) -> void
{
  if (!pipeline_) {
    LOG_F(WARNING,
      "GridSettingsService: no pipeline bound; cannot apply grid config");
    return;
  }

  engine::GroundGridPassConfig pass_config {};
  pass_config.enabled = config.enabled;
  pass_config.spacing = config.spacing;
  pass_config.major_every
    = static_cast<uint32_t>(std::max(1, config.major_every));
  pass_config.line_thickness = config.line_thickness;
  pass_config.major_thickness = config.major_thickness;
  pass_config.axis_thickness = config.axis_thickness;
  pass_config.fade_start = config.fade_start;
  pass_config.fade_power = config.fade_power;
  pass_config.horizon_boost = config.horizon_boost;
  pass_config.origin = grid_origin_;

  pass_config.minor_color = config.minor_color;
  pass_config.major_color = config.major_color;
  pass_config.axis_color_x = config.axis_color_x;
  pass_config.axis_color_y = config.axis_color_y;
  pass_config.origin_color = config.origin_color;

  pipeline_->SetGroundGridConfig(pass_config);
}

} // namespace oxygen::examples
