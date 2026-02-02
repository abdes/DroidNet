//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/UI/RenderingVm.h"
#include "DemoShell/Services/RenderingSettingsService.h"

namespace oxygen::examples::ui {

RenderingVm::RenderingVm(observer_ptr<RenderingSettingsService> service)
  : service_(service)
{
  Refresh();
}

auto RenderingVm::GetRenderMode() -> RenderMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return render_mode_;
}

auto RenderingVm::GetDebugMode() -> engine::ShaderDebugMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return debug_mode_;
}

auto RenderingVm::SetRenderMode(RenderMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (render_mode_ == mode) {
    return;
  }

  render_mode_ = mode;
  service_->SetRenderMode(mode);
  epoch_ = service_->GetEpoch();
}

auto RenderingVm::SetDebugMode(engine::ShaderDebugMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (debug_mode_ == mode) {
    return;
  }

  debug_mode_ = mode;
  service_->SetDebugMode(mode);
  epoch_ = service_->GetEpoch();
}

auto RenderingVm::GetWireframeColor() -> graphics::Color
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return wire_color_;
}

auto RenderingVm::SetWireframeColor(const graphics::Color& color) -> void
{
  std::lock_guard lock(mutex_);
  if (wire_color_.r == color.r && wire_color_.g == color.g
    && wire_color_.b == color.b && wire_color_.a == color.a) {
    return;
  }

  wire_color_ = color;
  service_->SetWireframeColor(color);
  epoch_ = service_->GetEpoch();
}

auto RenderingVm::Refresh() -> void
{
  // Assume mutex is already held by caller (GetRenderMode/GetDebugMode)
  // or it's called from constructor (which doesn't need it)
  render_mode_ = service_->GetRenderMode();
  debug_mode_ = service_->GetDebugMode();
  wire_color_ = service_->GetWireframeColor();
  epoch_ = service_->GetEpoch();
}

auto RenderingVm::IsStale() const -> bool
{
  // Assume mutex is already held
  return epoch_ != service_->GetEpoch();
}

} // namespace oxygen::examples::ui
