//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/Runtime/RendererUiTypes.h"

namespace oxygen::examples {
class LightCullingSettingsService;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

// Re-export ShaderDebugMode for convenience in UI code
using ShaderDebugMode = engine::ShaderDebugMode;

//! View model for light culling panel state.
/*!
 Caches light-culling debug visualization settings retrieved from
 `LightCullingSettingsService` and applies UI changes back to the service.

### Key Features

- **Epoch-driven refresh**: Reacquires state when stale.
- **Immediate persistence**: Setters forward changes to the service.
- **Thread-safe**: Protected by a mutex.

@see oxygen::examples::LightCullingSettingsService
*/
class LightCullingVm {
public:
  //! Creates a view model backed by the provided settings service.
  explicit LightCullingVm(observer_ptr<LightCullingSettingsService> service);

  //! Returns the cached visualization mode.
  [[nodiscard]] auto GetVisualizationMode() -> ShaderDebugMode;

  //! Sets visualization mode and forwards to service.
  auto SetVisualizationMode(ShaderDebugMode mode) -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  mutable std::mutex mutex_ {};
  observer_ptr<LightCullingSettingsService> service_;
  std::uint64_t epoch_ { 0 };

  // Cached state
  ShaderDebugMode visualization_mode_ { ShaderDebugMode::kDisabled };
};

} // namespace oxygen::examples::ui
