//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <memory>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

#include "DemoShell/Runtime/AppWindow.h"
#include "DemoShell/Runtime/DemoAppContext.h"

namespace {

namespace o = oxygen;

void MaybeUnhookImgui(o::observer_ptr<o::AsyncEngine> engine) noexcept
{
  auto imgui_module_ref = engine->GetModule<o::imgui::ImGuiModule>();
  if (!imgui_module_ref) {
    DLOG_F(INFO, "ImGui module not available; skipping window detach");
    return;
  }

  try {
    imgui_module_ref->get().SetWindowId(o::platform::kInvalidWindowId);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to unhook ImGui from window: {}", e.what());
  }
}

bool MaybeHookImgui(
  o::observer_ptr<o::AsyncEngine> engine, o::platform::WindowIdType window_id)
{
  auto imgui_module_ref = engine->GetModule<o::imgui::ImGuiModule>();
  if (!imgui_module_ref) {
    LOG_F(
      INFO, "ImGui module not available; cannot bind to window {}", window_id);
    return false;
  }

  imgui_module_ref->get().SetWindowId(window_id);

  return true;
}

} // namespace

namespace oxygen::examples {

// Map holding per-AppWindow subscriptions. Kept in translation unit so the
// public header doesn't need to include heavy engine headers.
// SubscriptionToken holds the engine subscription object for a
// specific AppWindow instance. The type is declared in the header as
// an opaque `SubscriptionToken` so we keep this heavy dependency in the
// cpp file only.
struct AppWindow::SubscriptionToken {
  explicit SubscriptionToken(AsyncEngine::ModuleSubscription&& s)
    : sub(std::move(s))
  {
  }

  AsyncEngine::ModuleSubscription sub;
};

AppWindow::AppWindow(const DemoAppContext& app) noexcept
  : platform_(app.platform.get()) // observe only
  , engine_(app.engine.get()) // observe only
  , gfx_weak_(app.gfx_weak)
  , window_lifecycle_token_(0)
{
  // Sanity checks only; heavyweight initialization is explicit and deferred.
  CHECK_NOTNULL_F(platform_);
  CHECK_NOTNULL_F(engine_);

  shutdown_event_ = std::make_shared<co::Event>();

  DLOG_F(INFO, "AppWindow constructed");
}

AppWindow::~AppWindow() noexcept
{
  DLOG_SCOPE_FUNCTION(INFO);

  // Ensure all resources are detached and cleaned up.
  Cleanup();

  // Remove any stored subscription for this instance.
  imgui_subscription_token_.reset();

  // Unregister any platform-level handler we previously installed.
  if (platform_ && window_lifecycle_token_ != 0) {
    platform_->UnregisterWindowAboutToBeDestroyedHandler(
      window_lifecycle_token_);
    window_lifecycle_token_ = 0;
  }
}

auto AppWindow::CreateAppWindow(const platform::window::Properties& props)
  -> bool
{
  DLOG_SCOPE_FUNCTION(INFO);

  // No Graphics -> cannot create surface/framebuffers later.
  if (gfx_weak_.expired()) {
    LOG_F(ERROR, "Cannot create AppWindow without a valid Graphics instance");
    return false;
  }

  // This is a single-window component; refuse to recreate if already present.
  if (!window_.expired()) {
    LOG_F(
      ERROR, "AppWindow is a single window component, and it already has one.");
    return false;
  }

  // This is a programmatic error.
  DCHECK_F(
    platform_->IsRunning(), "Platform is not running, cannot create a window.");

  window_ = platform_->Windows().MakeWindow(props);
  if (window_.expired()) {
    LOG_F(ERROR, "Failed to create a platform window");
    return false;
  }

  // Start the consolidated window lifecycle manager using a weak pointer
  // to avoid circular references that prevent destruction.
  platform_->Async().Nursery().Start(
    [weak_self = weak_from_this()]() -> co::Co<> {
      if (auto self = weak_self.lock()) {
        co_await self->ManageLifecycle();
      }
    });

  // Register pre-destroy handler.
  const auto win_ref = window_.lock();
  if (!win_ref) {
    LOG_F(ERROR, "Failed to lock platform window handle");
    return false;
  }
  const auto win_id = win_ref->Id();
  window_lifecycle_token_ = platform_->RegisterWindowAboutToBeDestroyedHandler(
    [weak_self = weak_from_this(), win_id](
      platform::WindowIdType closing_window_id) {
      if (closing_window_id != win_id) {
        return;
      }
      if (auto self = weak_self.lock()) {
        self->Cleanup();
      }
    });

  if (CreateSurface() && EnsureFramebuffers()) {
    auto sub = engine_->SubscribeModuleAttached(
      [this](engine::ModuleEvent const& ev) {
        if (ev.type_id == imgui::ImGuiModule::ClassTypeId()) {
          MaybeHookImgui(engine_, GetWindowId());
        }
      },
      /*replay_existing=*/true);

    imgui_subscription_token_
      = std::make_unique<SubscriptionToken>(std::move(sub));
    return true;
  }

  return false;
}

auto AppWindow::GetWindow() const noexcept -> observer_ptr<platform::Window>
{
  return window_.expired() ? nullptr : observer_ptr { window_.lock().get() };
}

auto AppWindow::GetWindowId() const noexcept -> platform::WindowIdType
{
  return window_.expired() ? platform::kInvalidWindowId : window_.lock()->Id();
}

auto AppWindow::ShouldResize() const noexcept -> bool
{
  return surface_ ? surface_->ShouldResize() : false;
}

auto AppWindow::CreateSurface() -> bool
{
  // Sanity checks - all these are programming errors.
  DCHECK_F(!surface_,
    "Surface already exists, properly reset (at frame start) before you "
    "recreate.");
  DCHECK_F(!window_.expired(),
    "Cannot create surface without a valid platform window.");
  DCHECK_F(!gfx_weak_.expired(),
    "Cannot create surface without a valid Graphics instance.");

  const auto gfx = gfx_weak_.lock();
  CHECK_NOTNULL_F(gfx); // see above.

  auto queue = gfx->GetCommandQueue(graphics::QueueRole::kGraphics);
  if (!queue) {
    LOG_F(ERROR,
      "Failed to acquire graphics command queue for surface creation for "
      "window {}",
      GetWindowId());
    return false;
  }

  surface_ = gfx->CreateSurface(window_, queue);
  if (!surface_) {
    LOG_F(ERROR, "Failed to create surface for window {}", GetWindowId());
    return false;
  }
  surface_->SetName("AppWindow Surface");
  LOG_F(INFO, "Surface created for window {}", GetWindowId());
  return true;
}

auto AppWindow::EnsureFramebuffers() -> bool
{
  if (IsShuttingDown()) {
    DLOG_F(INFO, "EnsureFramebuffers: skipping due to shutdown in progress");
    return false;
  }
  DCHECK_NOTNULL_F(surface_, "Cannot ensure framebuffers without a surface");
  DCHECK_F(!gfx_weak_.expired(),
    "Cannot ensure framebuffers without a Graphics instance");

  // We will always clear existing framebuffers and recreate them anew.
  ClearFramebuffers();

  DLOG_SCOPE_FUNCTION(INFO);
  const auto surface_width = surface_->Width();
  const auto surface_height = surface_->Height();
  DLOG_F(INFO, "surface w={} h={}", surface_width, surface_height);

  auto failed = false;
  for (auto i = 0U; i < frame::kFramesInFlight.get(); ++i) {
    DLOG_SCOPE_F(INFO, fmt::format("framebuffer slot {}", i).c_str());
    auto color_attachment = surface_->GetBackBuffer(i);
    if (!color_attachment) {
      LOG_F(ERROR, "Failed to get back buffer for slot {}", i);
      failed = true;
      break;
    }

    const auto& rt_desc = color_attachment->GetDescriptor();
    if (rt_desc.width != surface_width || rt_desc.height != surface_height) {
      LOG_F(WARNING, "Swapchain size mismatch: window={}x{} back-buffer={}x{}",
        surface_width, surface_height, rt_desc.width, rt_desc.height);
    }

    graphics::TextureDesc depth_desc;
    depth_desc.width = rt_desc.width;
    depth_desc.height = rt_desc.height;
    depth_desc.format = Format::kDepth32;
    depth_desc.texture_type = TextureType::kTexture2D;
    depth_desc.is_shader_resource = true;
    depth_desc.is_render_target = true;
    depth_desc.use_clear_value = true;
    depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
    depth_desc.initial_state = graphics::ResourceStates::kDepthWrite;

    const auto gfx = gfx_weak_.lock();
    const auto depth_tex = gfx->CreateTexture(depth_desc);
    if (!depth_tex) {
      LOG_F(ERROR, "Failed to create depth texture for framebuffer slot {}", i);
      failed = true;
      break;
    }

    auto desc = graphics::FramebufferDesc {}
                  .AddColorAttachment(color_attachment)
                  .SetDepthAttachment(depth_tex);
    DLOG_F(1, "color attachment {}", color_attachment ? "valid" : "null");
    DLOG_F(1, "depth attachment {}",
      desc.depth_attachment.texture ? "valid" : "null");

    framebuffers_[i] = gfx->CreateFramebuffer(desc);
    if (!framebuffers_[i]) {
      LOG_F(ERROR, "Failed to create framebuffer for slot {}", i);
      failed = true;
      break;
    }
  }

  if (failed) {
    ClearFramebuffers();
  }

  return !failed;
}

auto AppWindow::ClearFramebuffers() -> void
{
  DLOG_SCOPE_FUNCTION(1);

  // co::detail::ScopeGuard guard([&]() noexcept { framebuffers_.fill(nullptr);
  // });
  try {
    if (gfx_weak_.expired()) {
      LOG_F(WARNING, "gfx expired, cannot properly release framebuffers");
      framebuffers_.fill(nullptr);
      return;
    }
    auto gfx = gfx_weak_.lock();
    for (auto& fb : framebuffers_) {
      using graphics::DeferredObjectRelease;
      // We are the sole owner of the framebuffer resources;
      // resetting the shared_ptr will trigger destruction of the Framebuffer,
      // which in turn releases GPU resources.
      DeferredObjectRelease(fb, gfx->GetDeferredReclaimer());
    }
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "ClearFramebuffers threw: {}", ex.what());
  }
}

auto AppWindow::ApplyPendingResize() -> void
{
  if (IsShuttingDown()) {
    return;
  }
  DCHECK_NOTNULL_F(surface_, "Cannot apply resize without a surface");
  DCHECK_F(
    ShouldResize(), "ApplyPendingResize called but no resize is pending");

  DLOG_SCOPE_FUNCTION(INFO);

  if (gfx_weak_.expired()) {
    LOG_F(
      WARNING, "AppWindow gfx instance expired, cannot apply pending resize");
    return;
  }
  auto gfx = gfx_weak_.lock();

  try {
    // Drop owned framebuffer references so Resize() can succeed.
    ClearFramebuffers();
    gfx->Flush();
    surface_->Resize();
    EnsureFramebuffers();
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "-failed- resize threw: {}", ex.what());
  }

  // Acknowledge the resize.
  surface_->ShouldResize(false);
}

auto AppWindow::GetSurface() const -> std::weak_ptr<graphics::Surface>
{
  return surface_;
}

auto AppWindow::GetCurrentFrameBuffer() const
  -> std::weak_ptr<graphics::Framebuffer>
{
  if (!surface_ || IsShuttingDown()) {
    return {};
  }
  return framebuffers_.at(surface_->GetCurrentBackBufferIndex());
}

auto AppWindow::ManageLifecycle() -> co::Co<>
{
  auto weak_self = weak_from_this();
  bool term_signaled = false;

  while (true) {
    auto self = weak_self.lock();
    if (!self) {
      co_return;
    }

    const auto w = self->window_.lock();
    if (!w) {
      co_return;
    }

    // We race: window close, window events (resize), system termination, and
    // component shutdown. Use lambda for terminal to ensure we stop listening
    // once signaled.
    auto [close, event, terminal, shutdown]
      = co_await co::AnyOf([w]() -> co::Co<> { co_await w->CloseRequested(); },
        [w]() -> co::Co<platform::window::Event> {
          const auto [from, to] = co_await w->Events().UntilChanged();
          co_return to;
        },
        [p = self->platform_, term_signaled]() -> co::Co<> {
          if (term_signaled) {
            co_await co::kSuspendForever;
          }
          co_await p->Async().OnTerminate();
        },
        *self->shutdown_event_);

    if (shutdown) {
      co_return;
    }

    // Re-lock to ensure we haven't been destroyed while waiting.
    self = weak_self.lock();
    if (!self) {
      co_return;
    }

    if (terminal) {
      term_signaled = true;
      LOG_F(INFO, "platform OnTerminate -> requesting window close");
      if (auto sw = self->window_.lock()) {
        sw->RequestClose();
      }
    } else if (close) {
      if (auto sw = self->window_.lock()) {
        sw->VoteToClose();
      }
    } else if (event) {
      if (*event == platform::window::Event::kResized) {
        DLOG_F(INFO, "Window resized -> marking surface for resize");
        if (self->surface_) {
          self->surface_->ShouldResize(true);
        }
      }
    }
  }
}

auto AppWindow::Cleanup() -> void
{
  if (!surface_ && window_.expired()) {
    return; // Already cleaned up
  }

  // Trigger shutdown event if not already done to stop coroutines and block
  // rendering.
  if (shutdown_event_ && !shutdown_event_->Triggered()) {
    shutdown_event_->Trigger();
  }

  LOG_F(INFO, "Cleanup and release resources (window_id={})", GetWindowId());

  // Release resources and clear the state.
  MaybeUnhookImgui(engine_);
  ClearFramebuffers();

  if (!gfx_weak_.expired()) {
    if (auto gfx = gfx_weak_.lock()) {
      using graphics::DeferredObjectRelease;
      if (surface_) {
        DeferredObjectRelease(surface_, gfx->GetDeferredReclaimer());
      }
    }
  }

  surface_.reset();
  window_.reset();
}

auto AppWindow::IsShuttingDown() const noexcept -> bool
{
  return shutdown_event_ && shutdown_event_->Triggered();
}

} // namespace oxygen::examples
