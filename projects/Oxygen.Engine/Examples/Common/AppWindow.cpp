//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "AppWindow.h"

#include "AsyncEngineApp.h"
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

using namespace oxygen;

namespace oxygen::examples::common {

AppWindow::AppWindow(
  const oxygen::examples::common::AsyncEngineApp& app) noexcept
  : platform_(app.platform)
  , gfx_(app.gfx_weak)
  , engine_(app.engine.get())
{
  // Lightweight constructor — AppWindow intentionally defers work until
  // explicit lifecycle calls so examples can control creation timing.
}

AppWindow::~AppWindow() noexcept { UninstallHandlers(); }

auto AppWindow::CreateAppWindow(const platform::window::Properties& props)
  -> bool
{
  DCHECK_NOTNULL_F(platform_);

  window_ = platform_->Windows().MakeWindow(props);
  if (window_.expired()) {
    LOG_F(ERROR, "AppWindow::CreateAppWindow - MakeWindow failed");
    return false;
  }

  // Install platform async watchers on the platform's nursery. These
  // are expected to succeed for correctly-wired example apps — if the
  // platform async system isn't running the watchers are a no-op.
  if (platform_->Async().IsRunning()) {
    // Close-request handler
    platform_->Async().Nursery().Start([this]() -> co::Co<> {
      while (!window_.expired()) {
        const auto w = window_.lock();
        if (!w)
          break;
        co_await w->CloseRequested();
        if (auto sw = window_.lock())
          sw->VoteToClose();
      }
      co_return;
    });

    // Resize/expose handler
    platform_->Async().Nursery().Start([this]() -> co::Co<> {
      using WindowEvent = platform::window::Event;
      while (!window_.expired()) {
        const auto w = window_.lock();
        if (!w)
          break;
        const auto [from, to] = co_await w->Events().UntilChanged();
        if (to == WindowEvent::kResized) {
          LOG_F(INFO, "AppWindow: window was resized");
          should_resize_.store(true, std::memory_order_relaxed);
        }
      }
      co_return;
    });

    // Platform termination -> request close
    platform_->Async().Nursery().Start([this]() -> co::Co<> {
      co_await platform_->Async().OnTerminate();
      LOG_F(INFO, "AppWindow: platform OnTerminate -> RequestClose()");
      if (auto w = window_.lock())
        w->RequestClose();
      co_return;
    });
  }

  // Register pre-destroy handler
  const auto win_id = window_.lock()->Id();
  platform_window_destroy_handler_token_
    = platform_->RegisterWindowAboutToBeDestroyedHandler(
      [this, win_id](platform::WindowIdType closing_window_id) {
        if (closing_window_id == win_id) {
          LOG_F(INFO,
            "AppWindow: platform about to destroy window {} -> detaching state",
            win_id);
          // clear our strong reference to platform window
          window_.reset();
        }
      });

  return true;
}

auto AppWindow::GetWindowWeak() const noexcept
  -> std::weak_ptr<platform::Window>
{
  return window_;
}

auto AppWindow::GetWindowId() const noexcept -> platform::WindowIdType
{
  if (auto w = window_.lock())
    return w->Id();
  return platform::kInvalidWindowId;
}

auto AppWindow::ShouldResize() const noexcept -> bool
{
  return should_resize_.load(std::memory_order_relaxed);
}

auto AppWindow::MarkResizeApplied() -> void
{
  should_resize_.store(false, std::memory_order_relaxed);
}

auto AppWindow::UninstallHandlers() noexcept -> void
{
  // Unregister any platform-level handler we previously installed. This
  // should be safe, and failures indicate platform state is already
  // being torn down; in that case there's nothing we can do here.
  if (platform_ && platform_window_destroy_handler_token_ != 0) {
    platform_->UnregisterWindowAboutToBeDestroyedHandler(
      platform_window_destroy_handler_token_);
    platform_window_destroy_handler_token_ = 0;
  }
}

// -----------------------------------------------------------
// Surface & framebuffer lifecycle (engine thread)
// -----------------------------------------------------------

auto AppWindow::CreateSurfaceIfNeeded() -> bool
{
  if (surface_)
    return true;

  if (gfx_.expired() || window_.expired()) {
    LOG_F(ERROR, "AppWindow::CreateSurfaceIfNeeded - missing gfx or window");
    return false;
  }

  const auto gfx = gfx_.lock();
  if (!gfx)
    return false;

  auto queue = gfx->GetCommandQueue(oxygen::graphics::QueueRole::kGraphics);
  if (!queue) {
    LOG_F(ERROR, "AppWindow::CreateSurfaceIfNeeded - no graphics queue");
    return false;
  }

  surface_ = gfx->CreateSurface(window_, queue);
  if (!surface_) {
    LOG_F(ERROR, "AppWindow::CreateSurfaceIfNeeded - CreateSurface failed");
    return false;
  }
  surface_->SetName("AppWindow Surface");
  LOG_F(INFO, "AppWindow: surface created for window {}",
    window_.lock() ? window_.lock()->Id() : platform::kInvalidWindowId);
  return true;
}

auto AppWindow::EnsureFramebuffers() -> bool
{
  if (!surface_) {
    LOG_F(WARNING, "AppWindow::EnsureFramebuffers - no surface");
    return false;
  }

  if (!gfx_.lock()) {
    LOG_F(ERROR, "AppWindow::EnsureFramebuffers - Graphics not available");
    return false;
  }

  const auto surface_width = surface_->Width();
  const auto surface_height = surface_->Height();

  framebuffers_.clear();
  for (auto i = 0U; i < oxygen::frame::kFramesInFlight.get(); ++i) {
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

    const auto gfx = gfx_.lock();
    const auto depth_tex = gfx->CreateTexture(depth_desc);

    auto desc = oxygen::graphics::FramebufferDesc {}
                  .AddColorAttachment(surface_->GetBackBuffer(i))
                  .SetDepthAttachment(depth_tex);

    framebuffers_.push_back(gfx->CreateFramebuffer(desc));
    if (!framebuffers_.back()) {
      LOG_F(ERROR,
        "AppWindow::EnsureFramebuffers - failed to create framebuffer {}", i);
    }
  }

  return !framebuffers_.empty();
}

auto AppWindow::ClearFramebuffers() -> void { framebuffers_.clear(); }

auto AppWindow::ApplyPendingResizeIfNeeded(
  observer_ptr<oxygen::AsyncEngine> engine) -> void
{
  if (!surface_)
    return;

  // Combine surface internal flag with controller-observed flag
  bool should_resize = false;
  if (surface_->ShouldResize())
    should_resize = true;
  if (ShouldResize())
    should_resize = true;

  if (!should_resize)
    return;

  LOG_F(INFO, "AppWindow: Applying pending surface resize");

  try {
    if (!gfx_.expired()) {
      if (const auto gfx = gfx_.lock()) {
        gfx->Flush();
      }
    }

    // Drop owned framebuffer references so Resize() can succeed
    framebuffers_.clear();

    if (!gfx_.expired()) {
      if (const auto gfx = gfx_.lock()) {
        gfx->Flush();
      }
    }

    surface_->Resize();
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "AppWindow: Resize threw: {}", ex.what());
  }

  // Notify ImGui module (safe no-op for non-D3D backends)
  try {
    if (engine) {
      auto imgui_module_ref = engine->GetModule<oxygen::imgui::ImGuiModule>();
      if (imgui_module_ref) {
        imgui_module_ref->get().RecreateDeviceObjects();
      }
    }
  } catch (const std::exception& e) {
    LOG_F(WARNING, "AppWindow: notify ImGui RecreateDeviceObjects failed: {}",
      e.what());
  } catch (...) {
    LOG_F(WARNING,
      "AppWindow: notify ImGui RecreateDeviceObjects failed (unknown)");
  }

  // Acknowledge the resize
  try {
    MarkResizeApplied();
  } catch (...) {
  }
}

auto AppWindow::GetSurface() const -> std::shared_ptr<oxygen::graphics::Surface>
{
  return surface_;
}

auto AppWindow::GetFramebuffers() const
  -> const std::vector<std::shared_ptr<oxygen::graphics::Framebuffer>>&
{
  return framebuffers_;
}

} // namespace oxygen::examples::common
