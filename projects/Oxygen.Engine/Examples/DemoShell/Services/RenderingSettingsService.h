//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>

namespace oxygen::examples {

class SettingsService;

//! Settings persistence for rendering panel options.
/*!
 Owns UI-facing settings for view mode (solid/wireframe) and debug mode,
 delegating persistence to `SettingsService` and exposing an epoch for cache
 invalidation.

### Key Features

- **Passive state**: Reads and writes via SettingsService without caching.
- **Epoch tracking**: Increments on each effective change.
- **Testable**: Virtual getters and setters for overrides in tests.

@see SettingsService
*/
class RenderingSettingsService {
public:
  //! View mode for rendering.
  enum class ViewMode { kSolid, kWireframe };

  RenderingSettingsService() = default;
  virtual ~RenderingSettingsService() = default;

  OXYGEN_MAKE_NON_COPYABLE(RenderingSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(RenderingSettingsService)

  //! Returns the persisted view mode.
  [[nodiscard]] virtual auto GetViewMode() const -> ViewMode;

  //! Sets the view mode.
  virtual auto SetViewMode(ViewMode mode) -> void;

  //! Returns the persisted debug mode.
  [[nodiscard]] virtual auto GetDebugMode() const -> engine::ShaderDebugMode;

  //! Sets the debug mode.
  virtual auto SetDebugMode(engine::ShaderDebugMode mode) -> void;

  //! Returns the current settings epoch.
  [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

protected:
  //! Returns the settings service used for persistence.
  [[nodiscard]] virtual auto ResolveSettings() const noexcept
    -> observer_ptr<SettingsService>;

private:
  static constexpr const char* kViewModeKey = "rendering.view_mode";
  static constexpr const char* kDebugModeKey = "rendering.debug_mode";

  mutable std::atomic_uint64_t epoch_ { 0 };
};

} // namespace oxygen::examples
