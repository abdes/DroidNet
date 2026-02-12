
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <source_location>
#include <utility>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>

#include "DemoShell/Runtime/CompositionView.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/ForwardPipeline.h"
#include "LightBench/LightBenchPanel.h"
#include "LightBench/MainModule.h"

namespace oxygen::examples::light_bench {

namespace {
  constexpr std::uint32_t kWindowWidth = 2560;
  constexpr std::uint32_t kWindowHeight = 1440;
} // namespace

MainModule::MainModule(const DemoAppContext& app)
  : Base(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen LightBench");
  p.extent = { .width = kWindowWidth, .height = kWindowHeight };
  p.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return p;
}

auto MainModule::ClearBackbufferReferences() -> void
{
  if (pipeline_) {
    pipeline_->ClearBackbufferReferences();
  }
}

auto MainModule::OnAttachedImpl(observer_ptr<AsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  DCHECK_NOTNULL_F(engine, "expecting a valid engine");

  // Create Pipeline
  pipeline_
    = std::make_unique<ForwardPipeline>(observer_ptr { app_.engine.get() });

  // Initialize Shell
  auto shell = std::make_unique<DemoShell>();
  const auto demo_root
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path();
  DemoShellConfig shell_config {
    .engine = observer_ptr { app_.engine.get() },
    .enable_camera_rig = true,
    .content_roots = {
      .content_root = demo_root.parent_path() / "Content",
      .cooked_root = demo_root / ".cooked",
    },
    .panel_config = {
      .content_loader = false,
      .camera_controls = true,
      .environment = true,
      .lighting = true,
      .rendering = true,
      .post_process = true,
    },
    .get_active_pipeline = [this]() -> observer_ptr<RenderingPipeline> {
      return observer_ptr { pipeline_.get() };
    },
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "LightBench: DemoShell initialization failed");
    return nullptr;
  }

  // Create and set the active scene
  auto scene = light_scene_.CreateScene();
  active_scene_ = shell->SetScene(std::move(scene));
  light_scene_.SetScene(shell->TryGetScene());

  light_bench_panel_
    = std::make_shared<LightBenchPanel>(observer_ptr { &light_scene_ });
  if (!shell->RegisterPanel(light_bench_panel_)) {
    LOG_F(WARNING, "LightBench: failed to register LightBench panel");
  }

  // Create Main View ID
  main_view_id_ = GetOrCreateViewId("MainView");
  LOG_F(INFO, "LightBench: MainView ID created: {}", main_view_id_.get());

  LOG_F(INFO, "LightBench: Module initialized");
  return shell;
}

auto MainModule::OnShutdown() noexcept -> void
{
  auto& shell = GetShell();

  // Clear scene from shell first to ensure controlled destruction
  shell.SetScene(nullptr);

  light_scene_.Reset();

  light_bench_panel_.reset();

  Base::OnShutdown();
}

auto MainModule::OnFrameStart(
  observer_ptr<oxygen::engine::FrameContext> context) -> void
{
  DCHECK_NOTNULL_F(context);
  auto& shell = GetShell();
  shell.OnFrameStart(*context);
  Base::OnFrameStart(context);

  if (!active_scene_.IsValid()) {
    auto scene = light_scene_.CreateScene();
    active_scene_ = shell.SetScene(std::move(scene));
    light_scene_.SetScene(shell.TryGetScene());
  }
  if (!main_camera_.IsAlive()) {
    if (const auto scene_ptr = shell.TryGetScene()) {
      main_camera_ = scene_ptr->CreateNode("MainCamera");
      auto camera = std::make_unique<scene::PerspectiveCamera>();
      const bool attached = main_camera_.AttachCamera(std::move(camera));
      CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
    }
  }
  context->SetScene(shell.TryGetScene());
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_F(active_scene_.IsValid());

  if (!app_window_->GetWindow()) {
    DLOG_F(1, "OnSceneMutation: no valid window - skipping");
    co_return;
  }

  light_scene_.Update();

  // Delegate to pipeline to register views
  co_await Base::OnSceneMutation(context);
}

auto MainModule::UpdateComposition(
  engine::FrameContext& context, std::vector<CompositionView>& views) -> void
{
  auto& shell = GetShell();

  if (!main_camera_.IsAlive()) {
    return;
  }

  View view {};
  if (app_window_ && app_window_->GetWindow()) {
    const auto extent = app_window_->GetWindow()->Size();
    view.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(extent.width),
      .height = static_cast<float>(extent.height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
  }

  // Create the main scene view intent
  auto main_comp = CompositionView::ForScene(main_view_id_, view, main_camera_);
  main_comp.with_atmosphere = true;
  shell.OnMainViewReady(context, main_comp);
  views.push_back(std::move(main_comp));

  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(CompositionView::ForImGui(
    imgui_view_id, view, [](graphics::CommandRecorder&) { }));
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  auto& shell = GetShell();
  shell.Update(context->GetGameDeltaTime());
  co_return;
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  auto& shell = GetShell();

  if (app_window_->IsShuttingDown()) {
    DLOG_F(1, "OnGuiUpdate: window is closed/closing - skipping");
    co_return;
  }

  shell.Draw(context);

  co_return;
}

auto MainModule::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (!app_window_->GetWindow()) {
    DLOG_F(1, "OnPreRender: no valid window - skipping");
    co_return;
  }

  auto imgui_module_ref
    = app_.engine ? app_.engine->GetModule<imgui::ImGuiModule>() : std::nullopt;
  if (imgui_module_ref) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  // Delegate to pipeline
  co_await Base::OnPreRender(context);
}

auto MainModule::OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
  -> void
{
}

} // namespace oxygen::examples::light_bench
