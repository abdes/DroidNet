//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <mutex>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

#include "DemoShell/Runtime/AppWindow.h"
#include "DemoShell/Runtime/DemoAppContext.h"

using namespace oxygen;

namespace {

void MaybeUnhookImgui(observer_ptr<oxygen::AsyncEngine> engine)
{
  try {
    auto imgui_module_ref = engine->GetModule<imgui::ImGuiModule>();
    if (imgui_module_ref) {
      imgui_module_ref->get().SetWindowId(platform::kInvalidWindowId);
    }
  } catch (...) {
    // ignore
  }
}

bool MaybeHookImgui(
  observer_ptr<oxygen::AsyncEngine> engine, platform::WindowIdType window_id)
{
  try {
    auto imgui_module_ref = engine->GetModule<imgui::ImGuiModule>();
    if (imgui_module_ref) {
      imgui_module_ref->get().SetWindowId(window_id);
    }
  } catch (...) {
    // ignore
    return false;
  }

  return true;
}

} // namespace

namespace oxygen::examples {

AppWindow::AppWindow(const DemoAppContext& app) noexcept
  : platform_(app.platform.get()) // observe only
  , engine_(app.engine.get()) // observe only
  , gfx_weak_(app.gfx_weak)
{
  // Sanity checks only; heavyweight initialization is explicit and deferred.
  CHECK_NOTNULL_F(platform_);
  CHECK_NOTNULL_F(engine_);

  DLOG_F(INFO, "AppWindow constructed");
}

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

AppWindow::~AppWindow() noexcept
{
  DLOG_SCOPE_FUNCTION(INFO);
  // Remove any stored subscription for this instance (subscription dtor
  // will call Cancel()). Use best-effort and swallow exceptions.
  // Destroy subscription token (if present) so the subscription is
  // cancelled before this instance is torn down.
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

  const auto weak_self = weak_from_this();

  // Close-request handler.
  platform_->Async().Nursery().Start([weak_self]() -> co::Co<> {
    while (true) {
      auto self = weak_self.lock();
      if (!self) {
        co_return;
      }
      const auto w = self->window_.lock();
      if (!w) {
        co_return;
      }
      co_await w->CloseRequested();
      self = weak_self.lock();
      if (!self) {
        co_return;
      }
      if (auto sw = self->window_.lock()) {
        sw->VoteToClose();
      }
    }
  });

  // Resize/expose handler.
  platform_->Async().Nursery().Start([weak_self]() -> co::Co<> {
    using WindowEvent = platform::window::Event;
    while (true) {
      auto self = weak_self.lock();
      if (!self) {
        co_return;
      }
      const auto w = self->window_.lock();
      if (!w) {
        co_return;
      }
      const auto [from, to] = co_await w->Events().UntilChanged();
      self = weak_self.lock();
      if (!self) {
        co_return;
      }
      if (to == WindowEvent::kResized) {
        LOG_F(1, "Window resized -> marking surface for resize");
        if (self->surface_) {
          self->surface_->ShouldResize(true);
        }
      }
    }
  });

  // Platform termination -> request close.
  auto platform = platform_;
  platform_->Async().Nursery().Start([weak_self, platform]() -> co::Co<> {
    co_await platform->Async().OnTerminate();
    LOG_F(INFO, "platform OnTerminate -> requesting window close");
    if (auto self = weak_self.lock()) {
      if (auto w = self->window_.lock()) {
        w->RequestClose();
      }
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
    [weak_self, win_id](platform::WindowIdType closing_window_id) {
      if (closing_window_id != win_id) {
        return;
      }
      if (auto self = weak_self.lock()) {
        LOG_F(INFO, "Platform about to destroy window {} -> detaching state",
          win_id);
        // Release resources and clear the state.
        MaybeUnhookImgui(self->engine_);
        self->ClearFramebuffers();
        self->surface_.reset();
        self->window_.reset();
      }
    });

  if (CreateSurface() && EnsureFramebuffers()) {

    // Subscribe for any ImGui module attachments; replay_existing=true ensures
    // already-attached ImGui modules will be hooked up now that we have a
    // window available.
    // Install per-instance subscription and keep it in the local map to avoid
    // exposing the subscription type in the header.
    auto sub = engine_->SubscribeModuleAttached(
      [this](::oxygen::engine::ModuleEvent const& ev) {
        if (ev.type_id == oxygen::imgui::ImGuiModule::ClassTypeId()) {
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

  auto queue = gfx->GetCommandQueue(oxygen::graphics::QueueRole::kGraphics);
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
  DCHECK_NOTNULL_F(surface_, "Cannot ensure framebuffers without a surface");
  DCHECK_F(!gfx_weak_.expired(),
    "Cannot ensure framebuffers without a Graphics instance");

  // We will always clear existing framebuffers and recreate them anew.
  ClearFramebuffers();

  DLOG_SCOPE_FUNCTION(INFO);
  const auto surface_width = surface_->Width();
  const auto surface_height = surface_->Height();
  DLOG_F(INFO, "surface w={} h={}", surface_->Width(), surface_->Height());

  auto failed = false;
  for (auto i = 0U; i < oxygen::frame::kFramesInFlight.get(); ++i) {
    DLOG_SCOPE_F(INFO, fmt::format("framebuffer slot {}", i).c_str());
    oxygen::graphics::TextureDesc depth_desc;
    depth_desc.width = surface_width;
    depth_desc.height = surface_height;
    depth_desc.format = oxygen::Format::kDepth32;
    depth_desc.texture_type = oxygen::TextureType::kTexture2D;
    depth_desc.is_shader_resource = true;
    depth_desc.is_render_target = true;
    depth_desc.use_clear_value = true;
    depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
    depth_desc.initial_state = oxygen::graphics::ResourceStates::kDepthWrite;

    const auto gfx = gfx_weak_.lock();
    const auto depth_tex = gfx->CreateTexture(depth_desc);
    if (!depth_tex) {
      LOG_F(ERROR, "Failed to create depth texture for framebuffer slot {}", i);
      failed = true;
      break;
    }

    auto color_attachment = surface_->GetBackBuffer(i);
    auto desc = oxygen::graphics::FramebufferDesc {}
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
  for (auto& fb : framebuffers_) {
    // We are the sole owner of the framebuffer resources;
    // resetting the shared_ptr will trigger destruction of the Framebuffer,
    // which in turn releases GPU resources.
    fb.reset();
  }
  framebuffers_.fill(nullptr);
}

auto AppWindow::ApplyPendingResize() -> void
{
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
  // gfx->Flush();

  try {
    // Drop owned framebuffer references so Resize() can succeed.
    ClearFramebuffers();
    surface_->Resize();
    EnsureFramebuffers();
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "-failed- resize threw: {}", ex.what());
  }

  // Acknowledge the resize.
  surface_->ShouldResize(false);
}

auto AppWindow::GetSurface() const -> std::weak_ptr<oxygen::graphics::Surface>
{
  return surface_;
}

auto AppWindow::GetCurrentFrameBuffer() const
  -> std::weak_ptr<graphics::Framebuffer>
{
  if (!surface_) {
    return {};
  }
  return framebuffers_.at(surface_->GetCurrentBackBufferIndex());
}

} // namespace oxygen::examples
