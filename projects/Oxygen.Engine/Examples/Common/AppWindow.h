//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen {
class Platform;
class Graphics;
class AsyncEngine;
namespace platform {
  class Window;
}
namespace graphics {
  class Surface;
  class Framebuffer;
}
} // namespace oxygen

namespace oxygen::examples::common {

struct AsyncEngineApp;

//! Single component combining native window + surface + framebuffers.
/*!
 AppWindow owns the platform::Window, the graphics::Surface (swapchain) and
 the per-frame Framebuffer objects. It encapsulates the platform-side
 async handlers and the engine-thread-only GPU resource lifecycle (resize,
 framebuffer creation/cleanup, and notifications for ImGui device objects).

 The component is self-contained and intentionally avoids depending on any
 other example components so example modules can AddComponent<AppWindow>(app)
 and treat this as the single window / render lifecycle owner.
*/
class AppWindow final : public oxygen::Component {
  OXYGEN_COMPONENT(AppWindow)

public:
  explicit AppWindow(
    const oxygen::examples::common::AsyncEngineApp& app) noexcept;
  ~AppWindow() noexcept override;

  // Window management
  auto CreateAppWindow(const platform::window::Properties& props) -> bool;
  [[nodiscard]] auto GetWindowWeak() const noexcept
    -> std::weak_ptr<platform::Window>;
  [[nodiscard]] auto GetWindowId() const noexcept -> platform::WindowIdType;

  // Resize coordination
  [[nodiscard]] auto ShouldResize() const noexcept -> bool;
  auto MarkResizeApplied() -> void;

  // Surface / framebuffer lifecycle (engine thread usage)
  auto CreateSurfaceIfNeeded() -> bool;
  auto EnsureFramebuffers() -> bool;
  auto ClearFramebuffers() -> void;

  // Engine-thread handling for an observed pending resize.
  // Accepts observer_ptr to the AsyncEngine so we don't create ownership.
  auto ApplyPendingResizeIfNeeded(observer_ptr<oxygen::AsyncEngine> engine)
    -> void;

  [[nodiscard]] auto GetSurface() const
    -> std::shared_ptr<oxygen::graphics::Surface>;
  [[nodiscard]] auto GetFramebuffers() const
    -> const std::vector<std::shared_ptr<oxygen::graphics::Framebuffer>>&;

  // Best-effort: uninstall platform handlers
  auto UninstallHandlers() noexcept -> void;

private:
  // Platform objects
  std::weak_ptr<oxygen::platform::Window> window_ {};
  std::shared_ptr<oxygen::Platform> platform_ {};

  // Graphics / engine references
  std::weak_ptr<oxygen::Graphics> gfx_ {};
  oxygen::observer_ptr<oxygen::AsyncEngine> engine_ { nullptr };

  // Platform destructor token
  size_t platform_window_destroy_handler_token_ { 0 };

  // Resize flag set by platform watcher
  std::atomic<bool> should_resize_ { false };

  // GPU state owned by this component
  std::shared_ptr<oxygen::graphics::Surface> surface_ {};
  std::vector<std::shared_ptr<oxygen::graphics::Framebuffer>> framebuffers_ {};
};

} // namespace oxygen::examples::common
