//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <mutex>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

#include "DemoShell/Services/RenderingSettingsService.h"

namespace oxygen::examples::ui {

//! View mode selection for rendering panel.
enum class RenderingViewMode { kSolid, kWireframe };

//! View model for rendering panel state.
/*!
 Caches rendering settings retrieved from `RenderingSettingsService`,
 invalidating the cache based on the service epoch and applying UI changes back
 to the service.

### Key Features

- **Epoch-driven refresh**: Reacquires state when stale.
- **Immediate persistence**: Setters forward changes to the service.
- **Pass config sync**: Applies debug mode changes to the shader pass config.
- **Thread-safe**: Protected by a mutex for access from UI and render threads.

@see oxygen::examples::RenderingSettingsService
*/
class RenderingVm {
public:
  //! Creates a view model backed by the provided settings service.
  explicit RenderingVm(observer_ptr<RenderingSettingsService> service,
    observer_ptr<engine::ShaderPassConfig> pass_config);

  //! Returns the cached view mode.
  [[nodiscard]] auto GetViewMode() -> RenderingViewMode;

  //! Returns the cached debug mode.
  [[nodiscard]] auto GetDebugMode() -> engine::ShaderDebugMode;

  //! Sets view mode and forwards changes to the service.
  auto SetViewMode(RenderingViewMode mode) -> void;

  //! Sets debug mode and forwards changes to the service and pass config.
  auto SetDebugMode(engine::ShaderDebugMode mode) -> void;

  //! Updates the shader pass config pointer (for late initialization).
  auto SetPassConfig(observer_ptr<engine::ShaderPassConfig> pass_config)
    -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  static auto ToViewMode(RenderingSettingsService::ViewMode mode)
    -> RenderingViewMode;
  static auto FromViewMode(RenderingViewMode mode)
    -> RenderingSettingsService::ViewMode;

  mutable std::mutex mutex_ {};
  observer_ptr<RenderingSettingsService> service_;
  observer_ptr<engine::ShaderPassConfig> pass_config_;
  std::uint64_t epoch_ { 0 };
  RenderingViewMode view_mode_ { RenderingViewMode::kSolid };
  engine::ShaderDebugMode debug_mode_ { engine::ShaderDebugMode::kDisabled };
};

} // namespace oxygen::examples::ui
