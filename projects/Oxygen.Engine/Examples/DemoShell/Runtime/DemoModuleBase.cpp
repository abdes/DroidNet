//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "DemoShell/Runtime/DemoView.h"
#include "DemoShell/Runtime/RenderingPipeline.h"

using namespace oxygen;

namespace oxygen::examples {

namespace {

  auto GetRendererFromEngine(AsyncEngine* engine) -> engine::Renderer*
  {
    if (!engine)
      return nullptr;
    auto renderer_opt = engine->GetModule<engine::Renderer>();
    if (renderer_opt) {
      return &renderer_opt->get();
    }
    return nullptr;
  }

} // namespace

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
    app_window_ = observer_ptr(&wnd);
  }
}

DemoModuleBase::~DemoModuleBase()
{
  ClearViews();
  pipeline_.reset();
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
  [[maybe_unused]] observer_ptr<AsyncEngine> engine) noexcept -> bool
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

auto DemoModuleBase::OnShutdown() noexcept -> void
{
  // Ensure pipeline and views are torn down before window/engine shutdown
  pipeline_.reset();
  views_.clear();
  // Base::OnShutdown(); // EngineModule doesn't have OnShutdown or it's handled
  // by manager
}

auto DemoModuleBase::OnFrameStart(engine::FrameContext& context) -> void
{
  DLOG_SCOPE_FUNCTION(2);
  try {
    OnFrameStartCommon(context);

    // Update the pipeline first if it exists
    if (pipeline_) {
      if (auto* renderer = GetRendererFromEngine(app_.engine.get())) {
        pipeline_->OnFrameStart(context, *renderer);
      }
    }

    HandleOnFrameStart(context);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "OnFrameStart error: {}", ex.what());
  } catch (...) {
    LOG_F(ERROR, "OnFrameStart unknown exception");
  }
}

auto DemoModuleBase::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  if (!pipeline_) {
    co_return;
  }

  auto* renderer = GetRendererFromEngine(app_.engine.get());
  if (!renderer) {
    co_return;
  }

  auto scene = context.GetScene();
  if (!scene) {
    co_return;
  }

  // Get the current framebuffer from our window
  observer_ptr<graphics::Framebuffer> current_fb { nullptr };
  if (app_window_) {
    if (auto fb_weak = app_window_->GetCurrentFrameBuffer();
      !fb_weak.expired()) {
      current_fb = observer_ptr { fb_weak.lock().get() };
    }
  }

  // Collect active views and update their FrameContext registration
  std::vector<DemoView*> active_views;
  active_views.reserve(views_.size());

  for (const auto& v : views_) {
    if (!v)
      continue;

    // Determine viewport - use window size if not specified
    ViewPort vp;
    if (auto vp_opt = v->GetViewport()) {
      vp = *vp_opt;
    } else if (app_window_ && app_window_->GetWindow()) {
      const auto extent = app_window_->GetWindow()->Size();
      vp = ViewPort { .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .max_depth = 1.0F };
    } else {
      vp = ViewPort { .width = 1280.0F, .height = 720.0F, .max_depth = 1.0F };
    }

    // Build ViewContext with output framebuffer
    engine::ViewContext view_ctx;
    view_ctx.view.viewport = vp;
    view_ctx.view.scissor = { .left = static_cast<int32_t>(vp.top_left_x),
      .top = static_cast<int32_t>(vp.top_left_y),
      .right = static_cast<int32_t>(vp.top_left_x + vp.width),
      .bottom = static_cast<int32_t>(vp.top_left_y + vp.height) };
    view_ctx.metadata = { .name = "DemoView", .purpose = "primary" };
    view_ctx.output = current_fb;

    // Check if this view was already registered with the FrameContext
    ViewId vid = v->GetViewId();
    if (vid == kInvalidViewId) {
      // First time - register new view
      vid = context.RegisterView(std::move(view_ctx));
      v->SetViewId(vid);
    } else {
      // Already registered - update it for this frame
      view_ctx.id = vid;
      context.UpdateView(vid, std::move(view_ctx));
    }

    active_views.push_back(v.get());
  }

  // Pass to pipeline for render graph registration
  std::span v_span = active_views;
  co_await pipeline_->OnSceneMutation(
    context, *renderer, *scene, v_span, nullptr);
}

auto DemoModuleBase::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  if (pipeline_) {
    if (auto* renderer = GetRendererFromEngine(app_.engine.get())) {
      std::vector<DemoView*> active_views;
      active_views.reserve(views_.size());
      for (const auto& v : views_) {
        active_views.push_back(v.get());
      }
      co_await pipeline_->OnPreRender(context, *renderer, active_views);
    }
  }
  co_return;
}

auto DemoModuleBase::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  if (pipeline_) {
    if (auto* renderer = GetRendererFromEngine(app_.engine.get())) {
      co_await pipeline_->OnCompositing(context, *renderer, nullptr);
    }
  }
  MarkSurfacePresentable(context);
  co_return;
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

auto DemoModuleBase::AddView(std::unique_ptr<DemoView> view) -> DemoView*
{
  auto* ptr = view.get();
  views_.emplace_back(std::move(view));
  return ptr;
}

auto DemoModuleBase::ClearViews() -> void
{
  // Reset registration state before clearing to help prevent dangling
  // references
  for (auto& v : views_) {
    if (v) {
      v->SetViewId(kInvalidViewId);
      v->SetRendererRegistered(false);
    }
  }
  views_.clear();
}

auto DemoModuleBase::GetViews() const
  -> std::span<const std::unique_ptr<DemoView>>
{
  return views_;
}

// --------------------------------------------------------------------------
// Internals
// --------------------------------------------------------------------------

auto DemoModuleBase::OnFrameStartCommon(engine::FrameContext& context) -> void
{
  if (app_.headless) {
    return;
  }
  DCHECK_NOTNULL_F(app_window_);

  if (!app_window_->GetWindow()) {
    DLOG_F(1,
      "AppWindow's platform window has expired -> cleaning up frame context");

    // Remove the surface if it was previously registered
    if (last_surface_) {
      const auto surfaces = context.GetSurfaces();
      for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i] == last_surface_) {
          context.RemoveSurfaceAt(i);
          break;
        }
      }
      last_surface_ = nullptr;
    }

    // Remove all views from the context and unregister them from the renderer
    auto* renderer = GetRendererFromEngine(app_.engine.get());
    for (auto& v : views_) {
      if (v) {
        const auto vid = v->GetViewId();
        if (vid != kInvalidViewId) {
          context.RemoveView(vid);
          if (renderer) {
            renderer->UnregisterView(vid);
          }
          v->SetViewId(kInvalidViewId);
        }
      }
    }
    return;
  }

  if (app_window_->ShouldResize()) {
    ClearBackbufferReferences();
    app_window_->ApplyPendingResize();
  }

  auto surfaces = context.GetSurfaces();
  auto surface = app_window_->GetSurface().lock();
  if (surface) {
    const bool already_registered = std::ranges::any_of(
      surfaces, [&](const auto& s) { return s.get() == surface.get(); });

    if (!already_registered) {
      context.AddSurface(observer_ptr { surface.get() });
    }
    last_surface_ = observer_ptr { surface.get() };
  } else {
    last_surface_ = nullptr;
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
