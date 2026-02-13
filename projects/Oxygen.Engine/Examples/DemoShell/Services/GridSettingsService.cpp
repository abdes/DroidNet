//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/GridSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/CameraRigController.h"

namespace oxygen::examples {

namespace {
constexpr float kMinPlaneSize = 1.0F;
constexpr float kMinSpacing = 1e-4F;
constexpr float kMinThickness = 0.0F;
constexpr float kMinFadeDistance = 0.0F;

constexpr std::string_view kEnabledKey = "ground_grid.enabled";
constexpr std::string_view kPlaneSizeKey = "ground_grid.plane_size";
constexpr std::string_view kSpacingKey = "ground_grid.spacing";
constexpr std::string_view kMajorEveryKey = "ground_grid.major_every";
constexpr std::string_view kLineThicknessKey = "ground_grid.line_thickness";
constexpr std::string_view kMajorThicknessKey = "ground_grid.major_thickness";
constexpr std::string_view kAxisThicknessKey = "ground_grid.axis_thickness";
constexpr std::string_view kFadeStartKey = "ground_grid.fade_start";
constexpr std::string_view kFadeEndKey = "ground_grid.fade_end";
constexpr std::string_view kFadePowerKey = "ground_grid.fade_power";
constexpr std::string_view kThicknessMaxScaleKey
  = "ground_grid.thickness_max_scale";
constexpr std::string_view kDepthBiasKey = "ground_grid.depth_bias";
constexpr std::string_view kHorizonBoostKey = "ground_grid.horizon_boost";
constexpr std::string_view kMinorColorRKey = "ground_grid.minor_color.r";
constexpr std::string_view kMinorColorGKey = "ground_grid.minor_color.g";
constexpr std::string_view kMinorColorBKey = "ground_grid.minor_color.b";
constexpr std::string_view kMinorColorAKey = "ground_grid.minor_color.a";
constexpr std::string_view kMajorColorRKey = "ground_grid.major_color.r";
constexpr std::string_view kMajorColorGKey = "ground_grid.major_color.g";
constexpr std::string_view kMajorColorBKey = "ground_grid.major_color.b";
constexpr std::string_view kMajorColorAKey = "ground_grid.major_color.a";
constexpr std::string_view kRecenterThresholdKey
  = "ground_grid.recenter_threshold";

auto ClampFloat(float value, float min_value) -> float
{
  return std::max(value, min_value);
}

auto ClampFade(float start, float end) -> std::pair<float, float>
{
  const float clamped_start = std::max(start, kMinFadeDistance);
  const float clamped_end = std::max(end, clamped_start);
  return { clamped_start, clamped_end };
}
} // namespace

auto GridSettingsService::BindCameraRig(
  observer_ptr<ui::CameraRigController> camera_rig) -> void
{
  camera_rig_ = camera_rig;
}

auto GridSettingsService::Initialize(
  observer_ptr<RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;
}

auto GridSettingsService::GetEnabled() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kEnabledKey).value_or(true);
}

auto GridSettingsService::SetEnabled(const bool enabled) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kEnabledKey, enabled);
  epoch_++;
}

auto GridSettingsService::GetPlaneSize() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value = settings->GetFloat(kPlaneSizeKey).value_or(1000.0F);
  return ClampFloat(value, kMinPlaneSize);
}

auto GridSettingsService::SetPlaneSize(const float size) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kPlaneSizeKey, ClampFloat(size, kMinPlaneSize));
  epoch_++;
}

auto GridSettingsService::GetGridSpacing() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value = settings->GetFloat(kSpacingKey).value_or(1.0F);
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
  const float value = settings->GetFloat(kMajorEveryKey).value_or(10.0F);
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
  const float value = settings->GetFloat(kLineThicknessKey).value_or(0.02F);
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
  const float value = settings->GetFloat(kMajorThicknessKey).value_or(0.04F);
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
  const float value = settings->GetFloat(kAxisThicknessKey).value_or(0.06F);
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
  if (const auto stored = settings->GetFloat(kFadeStartKey)) {
    return ClampFloat(*stored, kMinFadeDistance);
  }
  return 0.0F;
}

auto GridSettingsService::SetFadeStart(const float distance) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kFadeStartKey, ClampFloat(distance, kMinFadeDistance));
  epoch_++;
}

auto GridSettingsService::GetFadeEnd() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  if (const auto stored = settings->GetFloat(kFadeEndKey)) {
    return ClampFloat(*stored, kMinFadeDistance);
  }
  return 0.0F;
}

auto GridSettingsService::SetFadeEnd(const float distance) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kFadeEndKey, ClampFloat(distance, kMinFadeDistance));
  epoch_++;
}

auto GridSettingsService::GetFadePower() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value = settings->GetFloat(kFadePowerKey).value_or(2.0F);
  return ClampFloat(value, 0.0F);
}

auto GridSettingsService::SetFadePower(const float power) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kFadePowerKey, ClampFloat(power, 0.0F));
  epoch_++;
}

auto GridSettingsService::GetThicknessMaxScale() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value
    = settings->GetFloat(kThicknessMaxScaleKey).value_or(64.0F);
  return ClampFloat(value, 1.0F);
}

auto GridSettingsService::SetThicknessMaxScale(const float scale) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kThicknessMaxScaleKey, ClampFloat(scale, 1.0F));
  epoch_++;
}

auto GridSettingsService::GetDepthBias() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value = settings->GetFloat(kDepthBiasKey).value_or(1e-4F);
  return ClampFloat(value, 0.0F);
}

auto GridSettingsService::SetDepthBias(const float bias) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kDepthBiasKey, ClampFloat(bias, 0.0F));
  epoch_++;
}

auto GridSettingsService::GetHorizonBoost() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value = settings->GetFloat(kHorizonBoostKey).value_or(0.35F);
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
  const float r = settings->GetFloat(kMinorColorRKey).value_or(0.30F);
  const float g = settings->GetFloat(kMinorColorGKey).value_or(0.30F);
  const float b = settings->GetFloat(kMinorColorBKey).value_or(0.30F);
  const float a = settings->GetFloat(kMinorColorAKey).value_or(1.0F);
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
  const float r = settings->GetFloat(kMajorColorRKey).value_or(0.50F);
  const float g = settings->GetFloat(kMajorColorGKey).value_or(0.50F);
  const float b = settings->GetFloat(kMajorColorBKey).value_or(0.50F);
  const float a = settings->GetFloat(kMajorColorAKey).value_or(1.0F);
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

auto GridSettingsService::GetRecenterThreshold() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float value
    = settings->GetFloat(kRecenterThresholdKey).value_or(0.0F);
  return ClampFloat(value, 0.0F);
}

auto GridSettingsService::SetRecenterThreshold(const float threshold) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kRecenterThresholdKey, ClampFloat(threshold, 0.0F));
  epoch_++;
}

auto GridSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto GridSettingsService::OnFrameStart(
  const engine::FrameContext& /*context*/) -> void
{
  if (!pipeline_) {
    return;
  }

  GridConfig config = ReadConfig();

  if (!camera_rig_) {
    config.enabled = false;
    ApplyGridConfig(config);
    return;
  }

  const auto camera = camera_rig_->GetActiveCamera();
  if (!camera || !camera->IsAlive()) {
    config.enabled = false;
    ApplyGridConfig(config);
    return;
  }

  UpdateGridOrigin(config);
  ApplyGridConfig(config);
}

auto GridSettingsService::OnSceneActivated(scene::Scene& /*scene*/) -> void
{
  grid_origin_ = { 0.0F, 0.0F };
  has_origin_ = false;
}

auto GridSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/, const CompositionView& /*view*/)
  -> void
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
  config.plane_size = GetPlaneSize();
  config.spacing = GetGridSpacing();
  config.major_every = GetMajorEvery();
  config.line_thickness = GetLineThickness();
  config.major_thickness = GetMajorThickness();
  config.axis_thickness = GetAxisThickness();
  config.fade_power = GetFadePower();
  config.thickness_max_scale = GetThicknessMaxScale();
  config.depth_bias = GetDepthBias();
  config.horizon_boost = GetHorizonBoost();

  const float fade_start = GetFadeStart();
  const float fade_end = GetFadeEnd();
  const auto [clamped_start, clamped_end] = ClampFade(fade_start, fade_end);
  config.fade_start = clamped_start;
  config.fade_end = clamped_end;

  config.recenter_threshold = GetRecenterThreshold();
  config.minor_color = GetMinorColor();
  config.major_color = GetMajorColor();
  return config;
}

auto GridSettingsService::UpdateGridOrigin(const GridConfig& config) -> void
{
  if (!camera_rig_) {
    return;
  }

  const auto camera = camera_rig_->GetActiveCamera();
  if (!camera || !camera->IsAlive()) {
    return;
  }

  auto cam_tf = camera->GetTransform();
  const auto world_pos = cam_tf.GetWorldPosition();
  const auto local_pos = cam_tf.GetLocalPosition();
  const Vec3 cam_pos = world_pos.value_or(local_pos.value_or(Vec3 {}));
  const Vec2 target { cam_pos.x, cam_pos.y };

  if (!has_origin_) {
    grid_origin_ = target;
    has_origin_ = true;
    return;
  }

  const float threshold = config.recenter_threshold;
  if (threshold <= 0.0F) {
    grid_origin_ = target;
    return;
  }

  const glm::vec2 delta { target.x - grid_origin_.x,
    target.y - grid_origin_.y };
  const float dist2 = glm::dot(delta, delta);
  if (dist2 >= threshold * threshold) {
    grid_origin_ = target;
  }
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
  pass_config.plane_size = config.plane_size;
  pass_config.spacing = config.spacing;
  pass_config.major_every = static_cast<uint32_t>(std::max(1, config.major_every));
  pass_config.line_thickness = config.line_thickness;
  pass_config.major_thickness = config.major_thickness;
  pass_config.axis_thickness = config.axis_thickness;
  pass_config.fade_start = config.fade_start;
  pass_config.fade_end = config.fade_end;
  pass_config.fade_power = config.fade_power;
  pass_config.thickness_max_scale = config.thickness_max_scale;
  pass_config.depth_bias = config.depth_bias;
  pass_config.horizon_boost = config.horizon_boost;
  pass_config.origin = grid_origin_;

  pass_config.minor_color = config.minor_color;
  pass_config.major_color = config.major_color;
  pass_config.axis_color_x = { 0.90F, 0.20F, 0.20F, 1.0F };
  pass_config.axis_color_y = { 0.20F, 0.90F, 0.20F, 1.0F };
  pass_config.origin_color = { 1.0F, 1.0F, 1.0F, 1.0F };

  static std::atomic<bool> logged_once { false };
  if (!logged_once.exchange(true)) {
    LOG_F(INFO,
      "GridSettingsService: ApplyGridConfig spacing={} major_every={} "
      "line_thickness={} major_thickness={} minor_color=({}, {}, {}, {}) "
      "major_color=({}, {}, {}, {})",
      pass_config.spacing, pass_config.major_every, pass_config.line_thickness,
      pass_config.major_thickness, pass_config.minor_color.r,
      pass_config.minor_color.g, pass_config.minor_color.b,
      pass_config.minor_color.a, pass_config.major_color.r,
      pass_config.major_color.g, pass_config.major_color.b,
      pass_config.major_color.a);
  }

  pipeline_->SetGroundGridConfig(pass_config);
}

} // namespace oxygen::examples
