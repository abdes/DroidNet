
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

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/ForwardPipeline.h"
#include "DemoShell/Runtime/SceneView.h"
#include "DemoShell/Services/FileBrowserService.h"
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

  // Create Main View (placeholder camera, updated later)
  auto view = std::make_unique<SceneView>(scene::SceneNode {});
  main_view_ = observer_ptr(static_cast<SceneView*>(AddView(std::move(view))));

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

  main_view_ = nullptr; // Owned by base, just clear ptr

  Base::OnShutdown();
}

auto MainModule::OnFrameStart(oxygen::engine::FrameContext& context) -> void
{
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
  camera_lifecycle.EnsureViewport(extent.width, extent.height);
  camera_lifecycle.ApplyPendingSync();
  camera_lifecycle.ApplyPendingReset();

  shell_->Update(time::CanonicalDuration {});

  light_scene_.Update();

  // Ensure camera is up to date in the view
  EnsureViewCameraRegistered(context);

  // Delegate to pipeline to register views
  co_await Base::OnSceneMutation(context);
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

auto MainModule::EnsureViewCameraRegistered(engine::FrameContext& /*context*/)
  -> void
{
  DCHECK_NOTNULL_F(shell_);
  if (!main_view_) {
    DLOG_F(1, "EnsureViewCameraRegistered: no valid window - skipping");
    return;
  }
  auto& active_camera = shell_->GetCameraLifecycle().GetActiveCamera();
  if (!active_camera.IsAlive()) {
    return;
  }

  // Just update the view's camera. The pipeline will pick it up.
  main_view_->SetCamera(active_camera);
}

} // namespace oxygen::examples::light_bench
