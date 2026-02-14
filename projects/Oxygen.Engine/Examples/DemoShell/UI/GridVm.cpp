//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/UI/GridVm.h"

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/GridSettingsService.h"

namespace oxygen::examples::ui {

GridVm::GridVm(observer_ptr<GridSettingsService> service)
  : service_(service)
{
  DCHECK_NOTNULL_F(service, "GridVm requires GridSettingsService");
}

auto GridVm::GetEnabled() -> bool
{
  Refresh();
  return enabled_;
}

auto GridVm::SetEnabled(const bool enabled) -> void
{
  service_->SetEnabled(enabled);
  enabled_ = enabled;
}


auto GridVm::GetGridSpacing() -> float
{
  Refresh();
  return spacing_;
}

auto GridVm::SetGridSpacing(const float spacing) -> void
{
  service_->SetGridSpacing(spacing);
  spacing_ = spacing;
}

auto GridVm::GetMajorEvery() -> int
{
  Refresh();
  return major_every_;
}

auto GridVm::SetMajorEvery(const int major_every) -> void
{
  service_->SetMajorEvery(major_every);
  major_every_ = major_every;
}

auto GridVm::GetLineThickness() -> float
{
  Refresh();
  return line_thickness_;
}

auto GridVm::SetLineThickness(const float thickness) -> void
{
  service_->SetLineThickness(thickness);
  line_thickness_ = thickness;
}

auto GridVm::GetMajorThickness() -> float
{
  Refresh();
  return major_thickness_;
}

auto GridVm::SetMajorThickness(const float thickness) -> void
{
  service_->SetMajorThickness(thickness);
  major_thickness_ = thickness;
}

auto GridVm::GetAxisThickness() -> float
{
  Refresh();
  return axis_thickness_;
}

auto GridVm::SetAxisThickness(const float thickness) -> void
{
  service_->SetAxisThickness(thickness);
  axis_thickness_ = thickness;
}

auto GridVm::GetFadeStart() -> float
{
  Refresh();
  return fade_start_;
}

auto GridVm::SetFadeStart(const float distance) -> void
{
  service_->SetFadeStart(distance);
  fade_start_ = distance;
}


auto GridVm::GetFadePower() -> float
{
  Refresh();
  return fade_power_;
}

auto GridVm::SetFadePower(const float power) -> void
{
  service_->SetFadePower(power);
  fade_power_ = power;
}



auto GridVm::GetHorizonBoost() -> float
{
  Refresh();
  return horizon_boost_;
}

auto GridVm::SetHorizonBoost(const float boost) -> void
{
  service_->SetHorizonBoost(boost);
  horizon_boost_ = boost;
}

auto GridVm::GetMinorColor() -> graphics::Color
{
  Refresh();
  return minor_color_;
}

auto GridVm::SetMinorColor(const graphics::Color& color) -> void
{
  service_->SetMinorColor(color);
  minor_color_ = color;
}

auto GridVm::GetMajorColor() -> graphics::Color
{
  Refresh();
  return major_color_;
}

auto GridVm::SetMajorColor(const graphics::Color& color) -> void
{
  service_->SetMajorColor(color);
  major_color_ = color;
}

auto GridVm::GetAxisColorX() -> graphics::Color
{
  Refresh();
  return axis_color_x_;
}

auto GridVm::SetAxisColorX(const graphics::Color& color) -> void
{
  service_->SetAxisColorX(color);
  axis_color_x_ = color;
}

auto GridVm::GetAxisColorY() -> graphics::Color
{
  Refresh();
  return axis_color_y_;
}

auto GridVm::SetAxisColorY(const graphics::Color& color) -> void
{
  service_->SetAxisColorY(color);
  axis_color_y_ = color;
}

auto GridVm::GetOriginColor() -> graphics::Color
{
  Refresh();
  return origin_color_;
}

auto GridVm::SetOriginColor(const graphics::Color& color) -> void
{
  service_->SetOriginColor(color);
  origin_color_ = color;
}

auto GridVm::Refresh() -> void
{
  std::scoped_lock lock(mutex_);
  if (!IsStale()) {
    return;
  }

  epoch_ = service_->GetEpoch();
  enabled_ = service_->GetEnabled();
  spacing_ = service_->GetGridSpacing();
  major_every_ = service_->GetMajorEvery();
  line_thickness_ = service_->GetLineThickness();
  major_thickness_ = service_->GetMajorThickness();
  axis_thickness_ = service_->GetAxisThickness();
  fade_start_ = service_->GetFadeStart();
  fade_power_ = service_->GetFadePower();
  horizon_boost_ = service_->GetHorizonBoost();
  minor_color_ = service_->GetMinorColor();
  major_color_ = service_->GetMajorColor();
  axis_color_x_ = service_->GetAxisColorX();
  axis_color_y_ = service_->GetAxisColorY();
  origin_color_ = service_->GetOriginColor();
}

auto GridVm::IsStale() const -> bool
{
  return epoch_ != service_->GetEpoch();
}

} // namespace oxygen::examples::ui
