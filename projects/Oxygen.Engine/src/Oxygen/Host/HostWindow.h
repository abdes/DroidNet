//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Host/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Vortex/SurfaceLayerSource.h>

namespace oxygen {
class AsyncEngine;
class Graphics;
class Platform;
namespace co {
  class Event;
} // namespace co
namespace graphics {
  class Framebuffer;
  class Surface;
} // namespace graphics
namespace platform {
  class Window;
  namespace window {
    struct Properties;
  } // namespace window
} // namespace platform
} // namespace oxygen

namespace oxygen::host {

class ImGuiLayer;

//! The host's main window with its surface and per-frame framebuffers.
/*!
 Owns the platform window, the swapchain surface and one colour-only
 framebuffer per frame in flight. A platform-side coroutine turns close
 requests, resizes and platform termination into window votes and pending
 resizes; GPU resources change only on the engine thread.

 Once the ImGui module is attached the window binds it and, when the renderer
 is attached too, registers an `ImGuiLayer` for the window's surface. Both
 are undone when the window is cleaned up.

 Must be owned by a `std::shared_ptr`: the lifecycle coroutine holds a weak
 reference to it.
*/
class HostWindow final : public std::enable_shared_from_this<HostWindow> {
public:
  //! The platform and the engine must outlive the window.
  OXGN_HOST_API HostWindow(observer_ptr<Platform> platform,
    observer_ptr<AsyncEngine> engine, std::weak_ptr<Graphics> gfx,
    std::optional<Extent<std::uint32_t>> framebuffer_extent = std::nullopt);
  OXGN_HOST_API ~HostWindow() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(HostWindow)
  OXYGEN_MAKE_NON_MOVABLE(HostWindow)

  //! Create the window, its surface and framebuffers; false on failure.
  OXGN_HOST_NDAPI auto Create(const platform::window::Properties& props)
    -> bool;

  OXGN_HOST_NDAPI auto GetWindow() const noexcept
    -> observer_ptr<platform::Window>;

  OXGN_HOST_NDAPI auto GetWindowId() const noexcept -> platform::WindowIdType;

  OXGN_HOST_NDAPI auto GetSurface() const -> std::weak_ptr<graphics::Surface>;

  //! The framebuffer of the surface's current back buffer.
  OXGN_HOST_NDAPI auto GetCurrentFrameBuffer() const
    -> std::weak_ptr<graphics::Framebuffer>;

  OXGN_HOST_NDAPI auto ShouldResize() const noexcept -> bool;

  //! Resize the surface and recreate the framebuffers; engine thread only.
  OXGN_HOST_API auto ApplyPendingResize() -> void;

  //! True once cleanup started; the window renders nothing after that.
  OXGN_HOST_NDAPI auto IsShuttingDown() const noexcept -> bool;

private:
  auto CreateSurface() -> bool;
  auto EnsureFramebuffers() -> bool;
  auto ClearFramebuffers() -> void;

  auto ManageLifecycle() -> co::Co<>;
  auto Cleanup() -> void;
  auto RegisterImGuiLayer() -> void;
  auto UnbindImGui() -> void;

  observer_ptr<Platform> platform_;
  observer_ptr<AsyncEngine> engine_;
  // Held weakly: the backend can be unloaded while the engine runs.
  std::weak_ptr<Graphics> gfx_weak_;
  std::optional<Extent<std::uint32_t>> framebuffer_extent_;

  // The platform owns the window and expires it when the window closes.
  std::weak_ptr<platform::Window> window_;
  std::size_t window_lifecycle_token_ { 0 };
  std::shared_ptr<co::Event> shutdown_event_;

  std::shared_ptr<graphics::Surface> surface_;
  std::array<std::shared_ptr<graphics::Framebuffer>,
    frame::kFramesInFlight.get()>
    framebuffers_ {};

  // Keeps the engine subscription type out of this header.
  struct SubscriptionToken;
  std::unique_ptr<SubscriptionToken> module_subscription_;

  std::unique_ptr<ImGuiLayer> imgui_layer_;
  vortex::SurfaceLayerToken imgui_layer_token_ {
    vortex::kInvalidSurfaceLayerToken
  };
};

} // namespace oxygen::host
