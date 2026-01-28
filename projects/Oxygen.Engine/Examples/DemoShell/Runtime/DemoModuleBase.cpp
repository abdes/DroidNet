//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"

using namespace oxygen;

namespace oxygen::examples {

DemoModuleBase::DemoModuleBase(const DemoAppContext& app) noexcept
  : app_(app)
{
  LOG_SCOPE_FUNCTION(INFO);

  // Construct demo components eagerly so derived classes get a
  // fully-configured Composition during OnAttached. The components are
  // responsible for window creation and lifecycle — the base only adds
  // them to the composition.
  if (!app_.headless) {
    auto& wnd = AddComponent<AppWindow>(app_);
    app_window_ = oxygen::observer_ptr<AppWindow>(&wnd);
  }
}

auto DemoModuleBase::BuildDefaultWindowProperties() const
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

auto DemoModuleBase::OnAttached(
  [[maybe_unused]] oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
  -> bool
{
  DCHECK_NOTNULL_F(engine);
  LOG_SCOPE_FUNCTION(INFO);

  // If headless, skip creating a window.
  if (app_.headless) {
    return true;
  }
  DCHECK_NOTNULL_F(app_window_);

  const auto props = BuildDefaultWindowProperties();
  if (!app_window_->CreateAppWindow(props)) {
    LOG_F(ERROR, "-failed- could not create application window");
    return false;
  }

  return true;
}

auto DemoModuleBase::OnFrameStart(engine::FrameContext& context) -> void
{
  DLOG_SCOPE_FUNCTION(2);
  try {
    OnFrameStartCommon(context);
    OnExampleFrameStart(context);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "OnFrameStart error: {}", ex.what());
  } catch (...) {
    LOG_F(ERROR, "OnFrameStart unknown exception");
  }
}

auto DemoModuleBase::OnFrameStartCommon(engine::FrameContext& context) -> void
{
  if (app_.headless) {
    return;
  }
  DCHECK_NOTNULL_F(app_window_);

  // Check the health of our window at every frame start to avoid cascades of
  // errors when a window is abruptly closed.

  if (!app_window_->GetWindow()) {
    // probably closed.
    DLOG_F(1, "AppWindow's platform window has expired");
    return;
  }

  if (app_window_->ShouldResize()) {
    // Clear references to backbuffer textures before applying resize.
    ClearBackbufferReferences();
    app_window_->ApplyPendingResize();
  }

  // Update our surface in the FrameContext if needed.
  auto surfaces = context.GetSurfaces();
  auto surface = app_window_->GetSurface().lock();
  if (surface) {
    // Check if already registered.
    const bool already_registered = std::ranges::any_of(
      surfaces, [&](const auto& s) { return s.get() == surface.get(); });

    if (!already_registered) {
      DLOG_F(INFO, "Registering my surface in the FrameContext");
      // Guaranteed to stay alive until next frame start.
      context.AddSurface(observer_ptr { surface.get() });
    }
    last_surface_ = observer_ptr { surface.get() };
  } else {
    LOG_F(WARNING, "AppWindow has no valid surface at frame start");

    if (last_surface_) {
      // Find and remove expired surface.
      auto it = std::ranges::find_if(surfaces,
        [&](const auto& s) { return s.get() == last_surface_.get(); });

      if (it != surfaces.end()) {
        DLOG_F(INFO, "Unregistering expired surface from FrameContext");
        context.RemoveSurfaceAt(std::distance(surfaces.begin(), it));
      }
      last_surface_ = nullptr;
    }
  }
}

auto DemoModuleBase::MarkSurfacePresentable(engine::FrameContext& context)
  -> void
{
  auto surface = app_window_->GetSurface().lock();
  if (!surface) {
    return;
  }

  const auto surfaces = context.GetSurfaces();
  for (size_t i = 0; i < surfaces.size(); ++i) {
    if (surfaces[i].get() == surface.get()) {
      context.SetSurfacePresentable(i, true);
      break;
    }
  }
}

} // namespace oxygen::examples
