//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen {
class Platform;
class Graphics;
class AsyncEngine;
namespace co {
  class Event;
}
namespace platform {
  class Window;
  namespace window {
    struct Properties;
  }
}
namespace graphics {
  class Surface;
  class Framebuffer;
}
} // namespace oxygen

namespace oxygen::examples {

class DemoAppContext;

//! Single component combining native window + surface + framebuffers.
/*!
 AppWindow owns the platform::Window, the graphics::Surface (swapchain) and
 the per-frame Framebuffer objects. It encapsulates the platform-side
 async handlers and the engine-thread-only GPU resource lifecycle (resize,
 framebuffer creation/cleanup, and notifications for ImGui device objects).

 The component is self-contained and intentionally avoids depending on other
 demo components so demo modules can AddComponent<AppWindow>(app) and treat
 this as the single window / render lifecycle owner.
*/
class AppWindow final : public Component,
                        public std::enable_shared_from_this<AppWindow> {
  OXYGEN_COMPONENT(AppWindow)

public:
  explicit AppWindow(const DemoAppContext& app) noexcept;
  ~AppWindow() noexcept override;

  OXYGEN_MAKE_NON_COPYABLE(AppWindow)
  OXYGEN_MAKE_NON_MOVABLE(AppWindow)

  auto CreateAppWindow(const platform::window::Properties& props) -> bool;

  [[nodiscard]] auto GetWindow() const noexcept
    -> observer_ptr<platform::Window>;

  [[nodiscard]] auto GetWindowId() const noexcept -> platform::WindowIdType;

  [[nodiscard]] auto GetSurface() const -> std::weak_ptr<graphics::Surface>;

  [[nodiscard]] auto GetCurrentFrameBuffer() const
    -> std::weak_ptr<graphics::Framebuffer>;

  [[nodiscard]] auto ShouldResize() const noexcept -> bool;

  auto ApplyPendingResize() -> void;

  [[nodiscard]] auto IsShuttingDown() const noexcept -> bool;

private:
  // Surface / framebuffer lifecycle (engine thread usage).
  auto CreateSurface() -> bool;
  auto EnsureFramebuffers() -> bool;
  auto ClearFramebuffers() -> void;

  // Internal lifecycle management.
  auto ManageLifecycle() -> co::Co<>;
  auto Cleanup() -> void;

  // The platform and the engine are guaranteed to outlive this component.
  // We store them as observer_ptr to avoid unnecessarily extending their
  // lifetime.
  observer_ptr<Platform> platform_;
  observer_ptr<AsyncEngine> engine_;

  // The Graphics instance is held weakly, because the engine does not guarantee
  // its stability due to dynamic loading/unloading.
  std::weak_ptr<Graphics> gfx_weak_;

  // The platform owns the window, and will expire the shared pointers it when
  // it is closed.
  std::weak_ptr<platform::Window> window_;

  // Platform destructor token.
  size_t window_lifecycle_token_;

  // Per-instance shutdown event used to cancel async handlers.
  std::shared_ptr<co::Event> shutdown_event_;

  // GPU state owned by this component. Because of the volatile nature of
  // surfaces, these should nvere be shared outside of this component unless via
  // `weak_ptr` or `observer_ptr`.
  std::shared_ptr<graphics::Surface> surface_;
  std::array<std::shared_ptr<graphics::Framebuffer>,
    frame::kFramesInFlight.get()>
    framebuffers_ {};

  // Opaque token type used by the implementation to hold a subscription
  // instance. This keeps the heavy engine header out of the public API.
  struct SubscriptionToken;

  // Per-instance token (opaque) stored as unique_ptr so lifetime of the
  // subscription is tied to this component instance without exposing the
  // concrete type in the header.
  std::unique_ptr<SubscriptionToken> imgui_subscription_token_;
};

} // namespace oxygen::examples
