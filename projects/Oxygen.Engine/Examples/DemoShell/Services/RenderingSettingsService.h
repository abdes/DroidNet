//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Types/Color.h>

#include "DemoShell/Runtime/RenderingPipeline.h"

namespace oxygen {
namespace engine {
  enum class ShaderDebugMode : int;
} // namespace engine

namespace examples {

  class RenderingPipeline;
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
    RenderingSettingsService() = default;
    virtual ~RenderingSettingsService() = default;

    OXYGEN_MAKE_NON_COPYABLE(RenderingSettingsService)
    OXYGEN_MAKE_NON_MOVABLE(RenderingSettingsService)

    //! Associates the service with a rendering pipeline and synchronizes
    //! initial state.
    virtual auto Initialize(observer_ptr<RenderingPipeline> pipeline) -> void;

    //! Returns the persisted render mode.
    [[nodiscard]] virtual auto GetRenderMode() const -> RenderMode;

    //! Sets the render mode.
    virtual auto SetRenderMode(RenderMode mode) -> void;

    //! Returns the persisted wireframe color.
    [[nodiscard]] virtual auto GetWireframeColor() const -> graphics::Color;

    //! Sets the wireframe color.
    virtual auto SetWireframeColor(const graphics::Color& color) -> void;

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
    static constexpr auto kViewModeKey = "rendering.view_mode";
    static constexpr auto kWireColorRKey = "rendering.wire_color.r";
    static constexpr auto kWireColorGKey = "rendering.wire_color.g";
    static constexpr auto kWireColorBKey = "rendering.wire_color.b";
    static constexpr auto kDebugModeKey = "rendering.debug_mode";

    observer_ptr<RenderingPipeline> pipeline_ {};
    mutable std::atomic_uint64_t epoch_ { 0 };
  };

} // namespace examples
} // namespace oxygen
