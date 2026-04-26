//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/UI/DiagnosticsVm.h"

namespace oxygen::examples::ui {

DiagnosticsVm::DiagnosticsVm(observer_ptr<RenderingSettingsService> service)
  : service_(service)
{
  Refresh();
}

auto DiagnosticsVm::GetRenderMode() -> renderer::RenderMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return render_mode_;
}

auto DiagnosticsVm::GetRequestedDebugMode() -> engine::ShaderDebugMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return requested_debug_mode_;
}

auto DiagnosticsVm::GetEffectiveDebugMode() -> engine::ShaderDebugMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return effective_debug_mode_;
}

auto DiagnosticsVm::GetRendererCapabilities() -> vortex::CapabilitySet
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return renderer_capabilities_;
}

auto DiagnosticsVm::GetGpuDebugPassEnabled() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return gpu_debug_pass_enabled_;
}

auto DiagnosticsVm::GetAtmosphereBlueNoiseEnabled() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return atmosphere_blue_noise_enabled_;
}

auto DiagnosticsVm::SupportsRenderModeControls() const -> bool
{
  return service_->SupportsRenderModeControls();
}

auto DiagnosticsVm::SupportsWireframeColorControl() const -> bool
{
  return service_->SupportsWireframeColorControl();
}

auto DiagnosticsVm::SupportsGpuDebugPassControl() const -> bool
{
  return service_->SupportsGpuDebugPassControl();
}

auto DiagnosticsVm::SupportsAtmosphereBlueNoiseControl() const -> bool
{
  return service_->SupportsAtmosphereBlueNoiseControl();
}

auto DiagnosticsVm::SupportsDebugMode(const engine::ShaderDebugMode mode) const
  -> bool
{
  return service_->SupportsDebugMode(mode);
}

auto DiagnosticsVm::IsVortexRuntimeBound() const -> bool
{
  return service_->IsVortexRuntimeBound();
}

auto DiagnosticsVm::SetRenderMode(renderer::RenderMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (render_mode_ == mode) {
    return;
  }

  render_mode_ = mode;
  service_->SetRenderMode(mode);
  epoch_ = service_->GetEpoch();
}

auto DiagnosticsVm::SetDebugMode(engine::ShaderDebugMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (requested_debug_mode_ == mode) {
    return;
  }

  requested_debug_mode_ = mode;
  service_->SetDebugMode(mode);
  effective_debug_mode_ = service_->GetEffectiveDebugMode();
  renderer_capabilities_ = service_->GetRendererCapabilities();
  epoch_ = service_->GetEpoch();
}

auto DiagnosticsVm::SetGpuDebugPassEnabled(bool enabled) -> void
{
  std::lock_guard lock(mutex_);
  if (gpu_debug_pass_enabled_ == enabled) {
    return;
  }

  gpu_debug_pass_enabled_ = enabled;
  service_->SetGpuDebugPassEnabled(enabled);
  epoch_ = service_->GetEpoch();
}

auto DiagnosticsVm::SetAtmosphereBlueNoiseEnabled(bool enabled) -> void
{
  std::lock_guard lock(mutex_);
  if (atmosphere_blue_noise_enabled_ == enabled) {
    return;
  }

  atmosphere_blue_noise_enabled_ = enabled;
  service_->SetAtmosphereBlueNoiseEnabled(enabled);
  epoch_ = service_->GetEpoch();
}

auto DiagnosticsVm::GetWireframeColor() -> graphics::Color
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return wire_color_;
}

auto DiagnosticsVm::SetWireframeColor(const graphics::Color& color) -> void
{
  std::lock_guard lock(mutex_);
  if (wire_color_.r == color.r && wire_color_.g == color.g
    && wire_color_.b == color.b && wire_color_.a == color.a) {
    return;
  }

  LOG_F(INFO, "DiagnosticsVm: SetWireframeColor ({}, {}, {}, {})", color.r,
    color.g, color.b, color.a);
  wire_color_ = color;
  service_->SetWireframeColor(color);
  epoch_ = service_->GetEpoch();
}

auto DiagnosticsVm::Refresh() -> void
{
  // Assume mutex is already held by caller (GetRenderMode/GetDebugMode)
  // or it's called from constructor (which doesn't need it)
  render_mode_ = service_->GetRenderMode();
  requested_debug_mode_ = service_->GetDebugMode();
  effective_debug_mode_ = service_->GetEffectiveDebugMode();
  renderer_capabilities_ = service_->GetRendererCapabilities();
  wire_color_ = service_->GetWireframeColor();
  gpu_debug_pass_enabled_ = service_->GetGpuDebugPassEnabled();
  atmosphere_blue_noise_enabled_ = service_->GetAtmosphereBlueNoiseEnabled();
  epoch_ = service_->GetEpoch();
}

auto DiagnosticsVm::IsStale() const -> bool
{
  // Assume mutex is already held
  return epoch_ != service_->GetEpoch();
}

} // namespace oxygen::examples::ui
