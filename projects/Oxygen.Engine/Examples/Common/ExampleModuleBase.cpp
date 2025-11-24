//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ExampleModuleBase.h"

#include "AsyncEngineApp.h"
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>

using namespace oxygen;

namespace oxygen::examples::common {

ExampleModuleBase::ExampleModuleBase(
  const oxygen::examples::common::AsyncEngineApp& app) noexcept
  : app_(app)
{
  // Construct example components eagerly so derived classes get a
  // fully-configured Composition during OnAttached. The components are
  // responsible for window creation and lifecycle — the base only adds
  // them to the composition.
  try {
    if (!app_.headless) {
      auto& wnd = AddComponent<AppWindow>(app_);
      app_window_ = oxygen::observer_ptr<AppWindow>(&wnd);

      auto& rg = AddComponent<RenderGraph>(app_);
      render_graph_ = oxygen::observer_ptr<RenderGraph>(&rg);
    }
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "ExampleModuleBase ctor - failed to add components: {}",
      ex.what());
  }
}

auto ExampleModuleBase::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Example");
  p.extent = { .width = 1280U, .height = 720U };
  p.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return p;
}

auto ExampleModuleBase::OnAttached(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool
{
  // If engine is not provided, bail out
  if (!engine) {
    return false;
  }

  // If headless, skip creating a window
  if (app_.headless) {
    return true;
  }

  // Ensure composed components and window state are exposed to the engine.
  // Example wiring guarantees AppWindow is present for non-headless apps.
  const auto props = BuildDefaultWindowProperties();
  if (!app_window_->CreateAppWindow(props)) {
    LOG_F(ERROR, "ExampleModuleBase::OnAttached - CreateAppWindow failed");
    return false;
  }

  // Let the AppWindow manage its surface/framebuffer lifecycle. Creation
  // attempt failures are logged by the component itself; treat failures
  // as non-fatal for module attachment.
  (void)app_window_->CreateSurfaceIfNeeded();
  (void)app_window_->EnsureFramebuffers();

  return true;
}

auto ExampleModuleBase::OnFrameStart(engine::FrameContext& context) -> void
{
  LOG_SCOPE_F(3, "ExampleModuleBase::OnFrameStart");

  // First, allow derived classes to run example-specific setup (e.g.
  // create a scene and call context.SetScene). Keeping the hook call
  // first mirrors existing example ordering.
  try {
    OnExampleFrameStart(context);
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "OnExampleFrameStart threw: {}", ex.what());
  } catch (...) {
    DLOG_F(1, "OnExampleFrameStart threw unknown exception");
  }

  // If the example window expired/was destroyed, clear surface and ImGui
  // assignment and avoid further frame work.
  DCHECK_NOTNULL_F(app_window_);
  const auto wnd_weak = app_window_->GetWindowWeak();
  if (wnd_weak.expired()) {
    LOG_F(WARNING, "Window expired in OnFrameStart - resetting surface");
    app_window_->ClearFramebuffers();
    // Best effort removal of any surface from frame context
    try {
      context.RemoveSurfaceAt(0);
    } catch (...) {
      // ignore - conservative best-effort
    }

    try {
      auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
      if (imgui_module_ref) {
        imgui_module_ref->get().SetWindowId(platform::kInvalidWindowId);
      }
    } catch (...) {
      // ignore
    }

    return;
  }

  // Resize handling — only perform work if a resize is pending.
  if (auto w = wnd_weak.lock()) {
    bool pending_resize = false;
    if (const auto surface = app_window_->GetSurface()) {
      if (surface->ShouldResize())
        pending_resize = true;
    }
    if (app_window_->ShouldResize())
      pending_resize = true;

    if (pending_resize) {
      try {
        if (render_graph_) {
          render_graph_->ClearBackbufferReferences();
        }
      } catch (const std::exception& ex) {
        LOG_F(WARNING,
          "ExampleModuleBase::OnFrameStart - clearing refs before resize "
          "threw: {}",
          ex.what());
      } catch (...) {
        DLOG_F(1,
          "ExampleModuleBase::OnFrameStart - unknown exception while "
          "clearing refs before resize");
      }

      app_window_->ApplyPendingResizeIfNeeded(
        oxygen::observer_ptr<oxygen::AsyncEngine>(app_.engine.get()));
    }
  }

  // Register surface with the frame context
  if (const auto surface = app_window_->GetSurface()) {
    context.AddSurface(surface);
  }

  // Configure ImGui to use our main window once after initialization
  try {
    if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
      auto& imgui_module = imgui_module_ref->get();
      const auto wnd = app_window_->GetWindowWeak();
      if (!wnd.expired())
        imgui_module.SetWindowId(wnd.lock()->Id());
      else
        imgui_module.SetWindowId(platform::kInvalidWindowId);
    }
  } catch (...) {
    // ignore
  }
}

} // namespace oxygen::examples::common
