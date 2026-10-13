//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/UI/DiagnosticsVm.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Vortex/Types/SkyLightRuntimeState.h>

namespace oxygen::examples::ui {

DiagnosticsVm::DiagnosticsVm(observer_ptr<RenderingSettingsService> service,
  observer_ptr<EnvironmentSettingsService> environment)
  : service_(service)
  , environment_(environment)
{
  DCHECK_NOTNULL_F(service_, "DiagnosticsVm requires RenderingSettingsService");
}

auto DiagnosticsVm::IsVortexRuntimeBound() const -> bool
{
  return service_->IsVortexRuntimeBound();
}

auto DiagnosticsVm::SupportsDiagnostics() const -> bool
{
  return service_->SupportsDiagnostics();
}

auto DiagnosticsVm::GetSkyLightRuntimeState() const
  -> vortex::SkyLightRuntimeState
{
  return environment_ ? environment_->GetSkyLightRuntimeState()
                      : vortex::SkyLightRuntimeState {};
}

auto DiagnosticsVm::GetFrameDiagnosticsEnabled() const -> bool
{
  return service_->GetFrameDiagnosticsEnabled();
}

auto DiagnosticsVm::SetFrameDiagnosticsEnabled(const bool enabled) -> void
{
  service_->SetFrameDiagnosticsEnabled(enabled);
}

auto DiagnosticsVm::GetGpuDebugPassEnabled() const -> bool
{
  return service_->GetGpuDebugPassEnabled();
}

auto DiagnosticsVm::SetGpuDebugPassEnabled(const bool enabled) -> void
{
  service_->SetGpuDebugPassEnabled(enabled);
}

auto DiagnosticsVm::GetWireframeColor() const -> graphics::Color
{
  return service_->GetWireframeColor();
}

auto DiagnosticsVm::SetWireframeColor(const graphics::Color& color) -> void
{
  service_->SetWireframeColor(color);
}

} // namespace oxygen::examples::ui
