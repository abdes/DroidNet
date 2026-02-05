
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

auto MainModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
{
  DCHECK_NOTNULL_F(engine, "expecting a valid engine");

  if (!Base::OnAttached(engine)) {
    return false;
  }

  // Create Pipeline
  pipeline_
    = std::make_unique<ForwardPipeline>(observer_ptr { app_.engine.get() });

  // Initialize Shell
  shell_ = std::make_unique<DemoShell>();
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

  if (!shell_->Initialize(shell_config)) {
    LOG_F(WARNING, "LightBench: DemoShell initialization failed");
    return false;
  }

  // Create and set the active scene
  auto scene = light_scene_.CreateScene();
  active_scene_ = shell_->SetScene(std::move(scene));
  light_scene_.SetScene(shell_->TryGetScene());

  light_bench_panel_
    = std::make_shared<LightBenchPanel>(observer_ptr { &light_scene_ });
  if (!shell_->RegisterPanel(light_bench_panel_)) {
    LOG_F(WARNING, "LightBench: failed to register LightBench panel");
  }

  // Create Main View ID
  main_view_id_ = GetOrCreateViewId("MainView");
  LOG_F(INFO, "LightBench: MainView ID created: {}", main_view_id_.get());

  LOG_F(INFO, "LightBench: Module initialized");
  return true;
}

auto MainModule::OnShutdown() noexcept -> void
{
  DCHECK_NOTNULL_F(shell_);

  // Clear scene from shell first to ensure controlled destruction
  shell_->SetScene(nullptr);

  light_scene_.Reset();

  light_bench_panel_.reset();
  shell_.reset();

  Base::OnShutdown();
}

auto MainModule::OnFrameStart(oxygen::engine::FrameContext& context) -> void
{
  ApplyRenderModeFromPanel();
  Base::OnFrameStart(context);
}

auto MainModule::HandleOnFrameStart(engine::FrameContext& context) -> void
{
  if (!active_scene_.IsValid()) {
    auto scene = light_scene_.CreateScene();
    active_scene_ = shell_->SetScene(std::move(scene));
    light_scene_.SetScene(shell_->TryGetScene());
  }
  context.SetScene(shell_->TryGetScene());
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(shell_);
  DCHECK_F(active_scene_.IsValid());

  if (!app_window_->GetWindow()) {
    DLOG_F(1, "OnSceneMutation: no valid window - skipping");
    co_return;
  }

  const auto extent = app_window_->GetWindow()->Size();
  auto& camera_lifecycle = shell_->GetCameraLifecycle();
  camera_lifecycle.EnsureViewport(extent);
  camera_lifecycle.ApplyPendingSync();
  camera_lifecycle.ApplyPendingReset();

  shell_->SyncPanels();

  light_scene_.Update();

  // Delegate to pipeline to register views
  co_await Base::OnSceneMutation(context);
}

auto MainModule::UpdateComposition(engine::FrameContext& /*context*/,
  std::vector<CompositionView>& views) -> void
{
  DCHECK_NOTNULL_F(shell_);

  auto& active_camera = shell_->GetCameraLifecycle().GetActiveCamera();
  if (!active_camera.IsAlive()) {
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
  views.push_back(
    CompositionView::ForScene(main_view_id_, view, active_camera));

  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(CompositionView::ForImGui(
    imgui_view_id, view, [](graphics::CommandRecorder&) { }));
}

auto MainModule::OnGameplay(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(shell_);
  shell_->Update(context.GetGameDeltaTime());
  co_return;
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(shell_);

  if (app_window_->IsShuttingDown()) {
    DLOG_F(1, "OnGuiUpdate: window is closed/closing - skipping");
    co_return;
  }

  shell_->Draw(context);

  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
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

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (!app_window_->GetWindow()) {
    DLOG_F(1, "OnCompositing: no valid window - skipping");
    co_return;
  }

  co_await Base::OnCompositing(context);
}

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void { }

auto MainModule::ApplyRenderModeFromPanel() -> void
{
  if (!shell_ || !pipeline_) {
    return;
  }

  const auto render_mode = shell_->GetRenderingViewMode();
  pipeline_->SetRenderMode(render_mode);

  const auto wire_color = shell_->GetRenderingWireframeColor();
  pipeline_->SetWireframeColor(wire_color);

  // Apply debug mode. Rendering debug modes take precedence if set.
  auto debug_mode = shell_->GetRenderingDebugMode();
  if (debug_mode == engine::ShaderDebugMode::kDisabled) {
    debug_mode = shell_->GetLightCullingVisualizationMode();
  }
  pipeline_->SetShaderDebugMode(debug_mode);
}

} // namespace oxygen::examples::light_bench
