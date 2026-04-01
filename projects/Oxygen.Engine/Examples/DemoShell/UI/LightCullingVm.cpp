//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/UI/LightCullingVm.h"

namespace oxygen::examples::ui {

LightCullingVm::LightCullingVm(
  observer_ptr<LightCullingSettingsService> service)
  : service_(service)
{
  Refresh();
}

auto LightCullingVm::GetVisualizationMode() -> ShaderDebugMode
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return visualization_mode_;
}

auto LightCullingVm::SetVisualizationMode(ShaderDebugMode mode) -> void
{
  std::lock_guard lock(mutex_);
  if (visualization_mode_ == mode) {
    return;
  }

  visualization_mode_ = mode;
  service_->SetVisualizationMode(mode);
  epoch_ = service_->GetEpoch();
}

auto LightCullingVm::Refresh() -> void
{
  visualization_mode_ = service_->GetVisualizationMode();
  epoch_ = service_->GetEpoch();
}

auto LightCullingVm::IsStale() const -> bool
{
  return epoch_ != service_->GetEpoch();
}

} // namespace oxygen::examples::ui
