//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <memory>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>
#include <Oxygen/Renderer/Pipeline/ForwardPipeline.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"

namespace oxygen::examples {

namespace {
  auto GetRendererFromEngine(AsyncEngine* engine) -> engine::Renderer*
  {
    DCHECK_NOTNULL_F(engine);
    auto renderer_opt = engine->GetModule<engine::Renderer>();
    return renderer_opt ? &renderer_opt->get() : nullptr;
  }
} // namespace

DemoModuleBase::DemoModuleBase(const DemoAppContext& app) noexcept
  : app_(app)
{
  LOG_SCOPE_FUNCTION(1);
  if (!app_.headless) {
    auto& wnd = AddComponent<AppWindow>(app_);
    app_window_ = observer_ptr(&wnd);
  }
}

DemoModuleBase::~DemoModuleBase()
{
  ClearViewIds();
  pipeline_.reset();
}

auto DemoModuleBase::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Example");
  p.extent = { .width = 1280U, .height = 720U };
  p.flags = { .hidden = false, .resizable = true };
  if (app_.fullscreen)
    p.flags.full_screen = true;
  return p;
}

auto DemoModuleBase::OnAttached(observer_ptr<AsyncEngine> engine) noexcept
  -> bool
{
  DCHECK_NOTNULL_F(engine);
  LOG_SCOPE_FUNCTION(1);

  if (!app_.headless) {
    DCHECK_NOTNULL_F(app_window_);

    const auto props = BuildDefaultWindowProperties();
    if (!app_window_->CreateAppWindow(props)) {
      LOG_F(ERROR, "-failed- could not create application window");
      return false;
    }
  }

  shell_ = OnAttachedImpl(engine);
  if (!shell_) {
    LOG_F(ERROR, "-failed- DemoShell initialization");
    return false;
  }
  return true;
}

auto DemoModuleBase::OnShutdown() noexcept -> void
{
  shell_.reset();
  pipeline_.reset();
  view_registry_.clear();
}

auto DemoModuleBase::GetShell() -> DemoShell&
{
  DCHECK_NOTNULL_F(shell_);
  return *shell_;
}

auto DemoModuleBase::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  try {
    OnFrameStartCommon(*context);
    if (pipeline_) {
      if (auto* renderer = GetRendererFromEngine(app_.engine.get())) {
        pipeline_->OnFrameStart(context, *renderer);
      }
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Report OnFrameStart error: {}", ex.what());
  }
}

auto DemoModuleBase::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (!pipeline_) {
    co_return;
  }

  // Gather composition intent during SceneMutation so any camera/view-node
  // edits happen before TransformPropagation.
  active_views_.clear();
  UpdateComposition(*context, active_views_);
  co_return;
}

auto DemoModuleBase::OnPublishViews(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (!pipeline_) {
    co_return;
  }
  auto* renderer = GetRendererFromEngine(app_.engine.get());
  if (renderer == nullptr) {
    co_return;
  }
  auto scene = context->GetScene();
  if (!scene) {
    co_return;
  }
  graphics::Framebuffer* target_fb = nullptr;
  if (app_window_) {
    if (auto fb_weak = app_window_->GetCurrentFrameBuffer();
      !fb_weak.expired()) {
      target_fb = fb_weak.lock().get();
    }
  }
  co_await pipeline_->OnPublishViews(
    context, *renderer, *scene, active_views_, target_fb);
}

auto DemoModuleBase::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (pipeline_) {
    if (auto* renderer = GetRendererFromEngine(app_.engine.get())) {
      co_await pipeline_->OnPreRender(context, *renderer, active_views_);
    }
  }
  co_return;
}

auto DemoModuleBase::OnCompositing(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (!app_window_->GetWindow()) {
    DLOG_F(1, "Skip compositing: no valid window");
    co_return;
  }

  if (pipeline_) {
    if (auto* renderer = GetRendererFromEngine(app_.engine.get())) {
      // Get the current framebuffer from our window for final composite
      std::shared_ptr<graphics::Framebuffer> target_fb;
      if (app_window_) {
        target_fb = app_window_->GetCurrentFrameBuffer().lock();
      }
      if (!target_fb) {
        LOG_F(WARNING, "Skip compositing: no valid framebuffer target");
        co_return;
      }
      auto submission = co_await pipeline_->OnCompositing(context, target_fb);
      if (!submission.tasks.empty() && submission.composite_target) {
        std::shared_ptr<graphics::Surface> surface;
        if (app_window_) {
          surface = app_window_->GetSurface().lock();
        }
        renderer->RegisterComposition(std::move(submission), surface);
        if (surface) {
          MarkSurfacePresentable(*context);
        }
      }
    }
  }
  co_return;
}

auto DemoModuleBase::GetOrCreateViewId(std::string_view name) -> ViewId
{
  const std::string name_str(name);
  if (auto it = view_registry_.find(name_str); it != view_registry_.end()) {
    return it->second;
  }

  // Generate a stable ID for this view name. We use a simple monotonic
  // counter starting from a high base to avoid collision with engine-internal
  // views if they exist.
  static std::atomic<uint64_t> s_next_view_id { 1000 };
  const ViewId new_id { s_next_view_id++ };
  view_registry_[name_str] = new_id;
  return new_id;
}

auto DemoModuleBase::ClearViewIds() -> void { view_registry_.clear(); }

auto DemoModuleBase::OnFrameStartCommon(engine::FrameContext& context) -> void
{
  if (app_.headless || !app_window_) {
    return;
  }
  if (!app_window_->GetWindow()) {
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
      DLOG_F(1, "Add surface: '{}'", surface->GetName());
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
    DLOG_F(1, "Skip marking presentable: surface=null");
    return;
  }
  const auto surfaces = context.GetSurfaces();
  bool found = false;
  for (size_t i = 0; i < surfaces.size(); ++i) {
    if (surfaces[i].get() == surface.get()) {
      context.SetSurfacePresentable(i, true);
      DLOG_F(1, "Mark surface presentable: index={}, surface='{}'", i,
        surface->GetName());
      found = true;
      break;
    }
  }
  if (surfaces.empty()) {
    DLOG_F(1, "Skip marking presentable: no surfaces in FrameContext");
  } else if (!found) {
    DLOG_F(1, "Skip marking presentable: surface not found: '{}'",
      surface->GetName());
  }
}

} // namespace oxygen::examples
