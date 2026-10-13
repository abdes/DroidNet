//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Vortex/Types/SkyLightRuntimeState.h>

namespace oxygen::examples {
class EnvironmentSettingsService;
class RenderingSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

//! View model of the Diagnostics panel.
/*!
 Reads and writes through `RenderingSettingsService`, and reads the sky-light
 state the environment service observes. The view mode and GPU timing are
 not here: the host toolbar and its GPU work overlay own them.
*/
class DiagnosticsVm {
public:
  explicit DiagnosticsVm(observer_ptr<RenderingSettingsService> service,
    observer_ptr<EnvironmentSettingsService> environment = nullptr);

  [[nodiscard]] auto IsVortexRuntimeBound() const -> bool;
  [[nodiscard]] auto SupportsDiagnostics() const -> bool;

  [[nodiscard]] auto GetSkyLightRuntimeState() const
    -> vortex::SkyLightRuntimeState;

  [[nodiscard]] auto GetFrameDiagnosticsEnabled() const -> bool;
  auto SetFrameDiagnosticsEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetGpuDebugPassEnabled() const -> bool;
  auto SetGpuDebugPassEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetWireframeColor() const -> graphics::Color;
  auto SetWireframeColor(const graphics::Color& color) -> void;

private:
  observer_ptr<RenderingSettingsService> service_;
  observer_ptr<EnvironmentSettingsService> environment_;
};

} // namespace oxygen::examples::ui
