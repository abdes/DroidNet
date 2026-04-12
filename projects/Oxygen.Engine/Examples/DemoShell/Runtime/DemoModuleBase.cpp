//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
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
  constexpr std::string_view kCommandDumpLightCullingTelemetry
    = "rndr.light_culling.dump_telemetry";

  struct LightCullingConsoleCommandState {
    observer_ptr<renderer::RenderingPipeline> pipeline { nullptr };
  };

  [[nodiscard]] auto GetLightCullingConsoleCommandState()
    -> const std::shared_ptr<LightCullingConsoleCommandState>&
  {
    static const auto state
      = std::make_shared<LightCullingConsoleCommandState>();
    return state;
  }

  auto RegisterLightCullingConsoleCommand(IAsyncEngine& engine) -> void
  {
    static std::once_flag once;
    std::call_once(once, [&engine] {
      std::weak_ptr<LightCullingConsoleCommandState> weak_state {
        GetLightCullingConsoleCommandState(),
      };
      (void)engine.GetConsole().RegisterCommand(console::CommandDefinition {
        .name = std::string(kCommandDumpLightCullingTelemetry),
        .help
        = "Dump LightCullingPass telemetry from the active ForwardPipeline",
        .flags = console::CommandFlags::kDevOnly,
        .handler
        = [weak_state](const std::vector<std::string>& args,
            const console::CommandContext&) -> console::ExecutionResult {
          if (!args.empty()) {
            return console::ExecutionResult {
              .status = console::ExecutionStatus::kInvalidArguments,
              .exit_code = 2,
              .output = {},
              .error = "usage: rndr.light_culling.dump_telemetry",
            };
          }

          const auto state = weak_state.lock();
          if (!state || state->pipeline == nullptr) {
            return console::ExecutionResult {
              .status = console::ExecutionStatus::kError,
              .exit_code = 1,
              .output = {},
              .error = "no active rendering pipeline is bound",
            };
          }

          if (state->pipeline->GetTypeId()
            != renderer::ForwardPipeline::ClassTypeId()) {
            return console::ExecutionResult {
              .status = console::ExecutionStatus::kError,
              .exit_code = 1,
              .output = {},
              .error
              = "active pipeline does not expose LightCullingPass telemetry",
            };
          }
          const auto& forward_pipeline
            = static_cast<const renderer::ForwardPipeline&>(*state->pipeline);

          return console::ExecutionResult {
            .status = console::ExecutionStatus::kOk,
            .exit_code = 0,
            .output = forward_pipeline.DumpLightCullingTelemetry(),
            .error = {},
          };
        },
      });
    });
  }

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
  GetLightCullingConsoleCommandState()->pipeline = nullptr;
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

auto DemoModuleBase::OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
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

  if (pipeline_) {
    const auto renderer_module = engine->GetModule<engine::Renderer>();
    if (!renderer_module.has_value()) {
      LOG_F(ERROR, "-failed- renderer module missing during pipeline bind");
      return false;
    }
    pipeline_->BindToRenderer(renderer_module->get());
  }

  RegisterLightCullingConsoleCommand(*engine);
  GetLightCullingConsoleCommandState()->pipeline
    = observer_ptr<renderer::RenderingPipeline> { pipeline_.get() };
  return true;
}

auto DemoModuleBase::OnShutdown() noexcept -> void
{
  shell_.reset();
  GetLightCullingConsoleCommandState()->pipeline = nullptr;
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
  if (!app_.headless && app_window_ && !app_window_->GetWindow()) {
    LOG_F(INFO, "Skipping UpdateComposition: app window is no longer valid");
    co_return;
  }
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
