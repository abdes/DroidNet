//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/ModuleEvent.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/Host/ImGuiLayer.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SurfaceLayerSource.h>

namespace oxygen::host {

struct HostWindow::SubscriptionToken {
  explicit SubscriptionToken(AsyncEngine::ModuleSubscription&& s)
    : sub(std::move(s))
  {
  }

  AsyncEngine::ModuleSubscription sub;
};

HostWindow::HostWindow(const observer_ptr<Platform> platform,
  const observer_ptr<AsyncEngine> engine, std::weak_ptr<Graphics> gfx,
  const std::optional<Extent<std::uint32_t>> framebuffer_extent)
  : platform_(platform)
  , engine_(engine)
  , gfx_weak_(std::move(gfx))
  , framebuffer_extent_(framebuffer_extent)
  , shutdown_event_(std::make_shared<co::Event>())
{
  CHECK_NOTNULL_F(platform_);
  CHECK_NOTNULL_F(engine_);
}

// Teardown only unregisters handlers and releases resources; a failure there
// leaves nothing to recover, so it terminates.
// NOLINTNEXTLINE(bugprone-exception-escape)
HostWindow::~HostWindow() noexcept
{
  Cleanup();
  module_subscription_.reset();
  if (platform_ && window_lifecycle_token_ != 0) {
    platform_->UnregisterWindowAboutToBeDestroyedHandler(
      window_lifecycle_token_);
    window_lifecycle_token_ = 0;
  }
}

auto HostWindow::Create(const platform::window::Properties& props) -> bool
{
  if (gfx_weak_.expired()) {
    LOG_F(ERROR, "Cannot create the host window without a Graphics instance");
    return false;
  }
  if (!window_.expired()) {
    LOG_F(ERROR, "The host window already exists");
    return false;
  }
  DCHECK_F(platform_->IsRunning(), "Platform must run to create a window");

  auto resolved_props = props;
  if (framebuffer_extent_) {
    resolved_props.framebuffer_extent = framebuffer_extent_;
  }
  try {
    window_ = platform_->Windows().MakeWindow(resolved_props);
  } catch (const std::exception& error) {
    LOG_F(ERROR, "Failed to create the host window: {}", error.what());
    return false;
  }
  const auto window = window_.lock();
  if (!window) {
    LOG_F(ERROR, "Failed to create a platform window");
    return false;
  }

  // A weak reference keeps the coroutine from extending the window's life.
  platform_->Async().Nursery().Start(
    [weak_self = weak_from_this()] -> co::Co<> {
      if (auto self = weak_self.lock()) {
        co_await self->ManageLifecycle();
      }
    });

  const auto window_id = window->Id();
  window_lifecycle_token_ = platform_->RegisterWindowAboutToBeDestroyedHandler(
    [weak_self = weak_from_this(), window_id](
      const platform::WindowIdType closing_window_id) -> void {
      if (closing_window_id != window_id) {
        return;
      }
      if (auto self = weak_self.lock()) {
        self->Cleanup();
      }
    });

  if (!CreateSurface() || !EnsureFramebuffers()) {
    return false;
  }

  module_subscription_
    = std::make_unique<SubscriptionToken>(engine_->SubscribeModuleAttached(
      [this](const engine::ModuleEvent& event) -> void {
        if (event.type_id == imgui::ImGuiModule::ClassTypeId()) {
          static_cast<imgui::ImGuiModule&>(*event.module)
            .SetWindowId(GetWindowId());
          RegisterImGuiLayer();
        } else if (event.type_id == vortex::Renderer::ClassTypeId()) {
          RegisterImGuiLayer();
        }
      },
      /*replay_existing=*/true));
  return true;
}

auto HostWindow::RegisterImGuiLayer() -> void
{
  if (imgui_layer_) {
    return;
  }
  const auto imgui_module = engine_->GetModule<imgui::ImGuiModule>();
  const auto renderer = engine_->GetModule<vortex::Renderer>();
  if (!imgui_module || !renderer) {
    return;
  }
  imgui_layer_
    = std::make_unique<ImGuiLayer>(observer_ptr { &imgui_module->get() });
  imgui_layer_token_ = renderer->get().RegisterSurfaceLayerSource(
    vortex::CompositionView::kDefaultSurfaceRoute,
    observer_ptr<vortex::SurfaceLayerSource> { imgui_layer_.get() });
}

auto HostWindow::UnbindImGui() -> void
{
  if (imgui_layer_token_ != vortex::kInvalidSurfaceLayerToken) {
    if (const auto renderer = engine_->GetModule<vortex::Renderer>()) {
      renderer->get().UnregisterSurfaceLayerSource(imgui_layer_token_);
    }
    imgui_layer_token_ = vortex::kInvalidSurfaceLayerToken;
  }
  imgui_layer_.reset();
  if (const auto imgui_module = engine_->GetModule<imgui::ImGuiModule>()) {
    imgui_module->get().SetWindowId(platform::kInvalidWindowId);
  }
}

auto HostWindow::GetWindow() const noexcept -> observer_ptr<platform::Window>
{
  return observer_ptr { window_.lock().get() };
}

auto HostWindow::GetWindowId() const noexcept -> platform::WindowIdType
{
  const auto window = window_.lock();
  return window ? window->Id() : platform::kInvalidWindowId;
}

auto HostWindow::ShouldResize() const noexcept -> bool
{
  return surface_ && surface_->ShouldResize();
}

auto HostWindow::CreateSurface() -> bool
{
  DCHECK_F(!surface_, "the host window surface already exists");
  const auto gfx = gfx_weak_.lock();
  CHECK_NOTNULL_F(gfx);

  auto queue = gfx->GetCommandQueue(graphics::QueueRole::kGraphics);
  if (!queue) {
    LOG_F(
      ERROR, "No graphics queue for the surface of window {}", GetWindowId());
    return false;
  }
  surface_ = gfx->CreateSurface(window_, queue);
  if (!surface_) {
    LOG_F(ERROR, "Failed to create the surface for window {}", GetWindowId());
    return false;
  }
  surface_->SetName("HostWindow Surface");
  LOG_F(INFO, "Surface created for window {}", GetWindowId());
  return true;
}

auto HostWindow::EnsureFramebuffers() -> bool
{
  if (IsShuttingDown()) {
    return false;
  }
  DCHECK_NOTNULL_F(surface_);
  ClearFramebuffers();

  const auto gfx = gfx_weak_.lock();
  if (!gfx) {
    return false;
  }
  // Runtime composition and surface layers write colour only.
  for (auto i = 0U; i < frame::kFramesInFlight.get(); ++i) {
    auto color_attachment = surface_->GetBackBuffer(i);
    if (!color_attachment) {
      LOG_F(ERROR, "No back buffer for framebuffer slot {}", i);
      ClearFramebuffers();
      return false;
    }
    const auto& desc = color_attachment->GetDescriptor();
    if (desc.width != surface_->Width() || desc.height != surface_->Height()) {
      LOG_F(WARNING, "Swapchain size mismatch: window={}x{} back-buffer={}x{}",
        surface_->Width(), surface_->Height(), desc.width, desc.height);
    }
    framebuffers_.at(i) = gfx->CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(color_attachment));
    if (!framebuffers_.at(i)) {
      LOG_F(ERROR, "Failed to create framebuffer slot {}", i);
      ClearFramebuffers();
      return false;
    }
  }
  return true;
}

auto HostWindow::ClearFramebuffers() -> void
{
  const auto gfx = gfx_weak_.lock();
  if (!gfx) {
    framebuffers_.fill(nullptr);
    return;
  }
  for (auto& framebuffer : framebuffers_) {
    // The back buffers stay in use by frames in flight.
    graphics::DeferredObjectRelease(framebuffer, gfx->GetDeferredReclaimer());
  }
}

auto HostWindow::ApplyPendingResize() -> void
{
  if (IsShuttingDown()) {
    return;
  }
  DCHECK_NOTNULL_F(surface_);
  DCHECK_F(ShouldResize(), "ApplyPendingResize without a pending resize");

  const auto gfx = gfx_weak_.lock();
  if (!gfx) {
    LOG_F(WARNING, "Graphics expired; cannot apply the pending resize");
    return;
  }
  try {
    // The surface cannot resize while framebuffers reference its buffers.
    ClearFramebuffers();
    gfx->Flush();
    surface_->Resize();
    if (!EnsureFramebuffers()) {
      LOG_F(WARNING, "Failed to recreate framebuffers after resize");
    }
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "Resize failed: {}", ex.what());
  }
  surface_->ShouldResize(false);
}

auto HostWindow::GetSurface() const -> std::weak_ptr<graphics::Surface>
{
  return surface_;
}

auto HostWindow::GetCurrentFrameBuffer() const
  -> std::weak_ptr<graphics::Framebuffer>
{
  if (!surface_ || IsShuttingDown()) {
    return {};
  }
  return framebuffers_.at(surface_->GetCurrentBackBufferIndex());
}

auto HostWindow::ManageLifecycle() -> co::Co<>
{
  auto weak_self = weak_from_this();
  bool term_signaled = false;

  while (true) {
    auto self = weak_self.lock();
    if (!self) {
      co_return;
    }
    const auto window = self->window_.lock();
    if (!window) {
      co_return;
    }

    // Once termination was signaled, stop listening for it.
    auto [close, event, terminal, shutdown] = co_await co::AnyOf(
      [window] -> co::Co<> { co_await window->CloseRequested(); },
      [window] -> co::Co<platform::window::Event> {
        const auto [from, to] = co_await window->Events().UntilChanged();
        co_return to;
      },
      [platform = self->platform_, term_signaled] -> co::Co<> {
        if (term_signaled) {
          co_await co::kSuspendForever;
        }
        co_await platform->Async().OnTerminate();
      },
      *self->shutdown_event_);

    if (shutdown) {
      co_return;
    }
    self = weak_self.lock();
    if (!self) {
      co_return;
    }

    if (terminal) {
      term_signaled = true;
      LOG_F(INFO, "platform OnTerminate -> requesting window close");
      if (auto current = self->window_.lock()) {
        current->RequestClose();
      }
    } else if (close) {
      if (auto current = self->window_.lock()) {
        current->VoteToClose();
      }
    } else if (event && *event == platform::window::Event::kResized
      && self->surface_) {
      self->surface_->ShouldResize(true);
    }
  }
}

auto HostWindow::Cleanup() -> void
{
  if (!surface_ && window_.expired()) {
    return;
  }
  // Stops the lifecycle coroutine and blocks rendering.
  if (shutdown_event_ && !shutdown_event_->Triggered()) {
    shutdown_event_->Trigger();
  }
  LOG_F(INFO, "Release the host window (window_id={})", GetWindowId());

  UnbindImGui();
  ClearFramebuffers();
  if (const auto gfx = gfx_weak_.lock(); gfx && surface_) {
    graphics::DeferredObjectRelease(surface_, gfx->GetDeferredReclaimer());
  }
  surface_.reset();
  window_.reset();
}

auto HostWindow::IsShuttingDown() const noexcept -> bool
{
  return shutdown_event_ && shutdown_event_->Triggered();
}

} // namespace oxygen::host
