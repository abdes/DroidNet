//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <mutex>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/RenderingSettingsService.h"

namespace oxygen::examples::ui {

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
  explicit RenderingVm(observer_ptr<RenderingSettingsService> service);

  //! Returns the cached view mode.
  [[nodiscard]] auto GetRenderMode() -> RenderMode;

  //! Returns the cached debug mode.
  [[nodiscard]] auto GetDebugMode() -> engine::ShaderDebugMode;

  //! Returns whether the GPU debug pass is currently enabled.
  [[nodiscard]] auto GetGpuDebugPassEnabled() -> bool;

  //! Sets view mode and forwards changes to the service.
  auto SetRenderMode(RenderMode mode) -> void;

  //! Sets debug mode and forwards changes to the service.
  auto SetDebugMode(engine::ShaderDebugMode mode) -> void;

  //! Toggles the GPU debug pass and persists the change.
  auto SetGpuDebugPassEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetWireframeColor() -> graphics::Color;
  auto SetWireframeColor(const graphics::Color& color) -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  mutable std::mutex mutex_ {};
  observer_ptr<RenderingSettingsService> service_;
  std::uint64_t epoch_ { 0 };
  RenderMode render_mode_ { RenderMode::kSolid };
  engine::ShaderDebugMode debug_mode_ { engine::ShaderDebugMode::kDisabled };
  graphics::Color wire_color_ { 1.0F, 1.0F, 1.0F, 1.0F };
  bool gpu_debug_pass_enabled_ { true };
};

} // namespace oxygen::examples::ui
