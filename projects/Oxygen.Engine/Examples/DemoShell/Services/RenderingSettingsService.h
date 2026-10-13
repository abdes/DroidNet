//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>

#include "DemoShell/Services/DomainService.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Types/Color.h>

namespace oxygen::vortex {
class Renderer;
} // namespace oxygen::vortex

namespace oxygen::examples {

//! Renderer diagnostics the Diagnostics panel controls.
/*!
 The view mode belongs to the host toolbar (`host.view_mode`) and GPU timing
 to its GPU work overlay; this service keeps what they do not cover: the
 wireframe color the wireframe view modes draw with, the draw-bounds GPU
 debug pass and the frame diagnostics that validate GPU products. The color
 and the debug pass persist through `SettingsService`.
*/
class RenderingSettingsService : public DomainService {
public:
  RenderingSettingsService() = default;
  ~RenderingSettingsService() override = default;

  OXYGEN_MAKE_NON_COPYABLE(RenderingSettingsService)
  OXYGEN_MAKE_NON_MOVABLE(RenderingSettingsService)

  //! Binds the service to the Vortex runtime.
  virtual auto BindVortexRenderer(observer_ptr<vortex::Renderer> renderer)
    -> void;
  [[nodiscard]] virtual auto IsVortexRuntimeBound() const -> bool;
  //! Whether the renderer has the diagnostics capability.
  [[nodiscard]] virtual auto SupportsDiagnostics() const -> bool;

  //! Whether the renderer validates GPU products each frame.
  [[nodiscard]] virtual auto GetFrameDiagnosticsEnabled() const -> bool;
  virtual auto SetFrameDiagnosticsEnabled(bool enabled) -> void;

  //! The color the wireframe view modes draw with.
  [[nodiscard]] virtual auto GetWireframeColor() const -> graphics::Color;
  virtual auto SetWireframeColor(const graphics::Color& color) -> void;

  //! Whether the GPU debug pass draws the draw bounds.
  [[nodiscard]] virtual auto GetGpuDebugPassEnabled() const -> bool;
  virtual auto SetGpuDebugPassEnabled(bool enabled) -> void;

  //! Returns the current settings epoch.
  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t override;

  auto OnFrameStart(const engine::FrameContext& context) -> void override;
  auto OnSceneActivated(scene::Scene& scene) -> void override;
  auto OnMainViewReady(ViewId view_id, const scene::SceneNode& camera,
    const ViewPort& viewport) -> void override;

private:
  static constexpr auto kWireColorRKey = "rendering.wire_color.r";
  static constexpr auto kWireColorGKey = "rendering.wire_color.g";
  static constexpr auto kWireColorBKey = "rendering.wire_color.b";
  static constexpr auto kGpuDebugPassEnabledKey = "rendering.debug_gpu_pass";

  observer_ptr<vortex::Renderer> vortex_renderer_ { nullptr };
  std::atomic_uint64_t epoch_ { 0 };

  auto ApplyVortexSettings() -> void;
};

} // namespace oxygen::examples
