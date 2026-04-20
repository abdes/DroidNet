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
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Vortex/CompositionView.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "LightBench/LightBenchPanel.h"
#include "LightBench/MainModule.h"

namespace oxygen::examples::light_bench {

namespace {
  constexpr std::uint32_t kWindowWidth = 2560;
  constexpr std::uint32_t kWindowHeight = 1440;
}

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

auto MainModule::ClearBackbufferReferences() -> void { }

auto MainModule::StageInitialScene(DemoShell& shell) -> void
{
  shell.StageScene(light_scene_.CreateScene());
  const auto staged_scene = shell.GetStagedScene();
  CHECK_NOTNULL_F(staged_scene, "LightBench staged scene is null");

  main_camera_ = staged_scene->CreateNode("MainCamera");
  auto camera = std::make_unique<scene::PerspectiveCamera>();
  const bool attached = main_camera_.AttachCamera(std::move(camera));
  CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  shell.SetStagedMainCamera(main_camera_);

  light_scene_.SetScene(staged_scene);
}

auto MainModule::OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  DCHECK_NOTNULL_F(engine, "expecting a valid engine");

  auto shell = std::make_unique<DemoShell>();
  const auto demo_root
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path();
  DemoShellConfig shell_config {
    .engine = observer_ptr { app_.engine.get() },
    .enable_camera_rig = true,
    .enable_renderer_bound_panels = false,
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
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "LightBench: DemoShell initialization failed");
    return nullptr;
  }

  light_bench_panel_
    = std::make_shared<LightBenchPanel>(observer_ptr { &light_scene_ });
  if (!shell->RegisterPanel(light_bench_panel_)) {
    LOG_F(WARNING, "LightBench: failed to register LightBench panel");
  }

  StageInitialScene(*shell);

  main_view_id_ = GetOrCreateViewId("MainView");
  LOG_F(INFO, "LightBench: MainView ID created: {}", main_view_id_.get());
  LOG_F(INFO, "LightBench: Module initialized");
  return shell;
}

auto MainModule::OnShutdown() noexcept -> void
{
  auto& shell = GetShell();
  shell.SetScene(nullptr);
  light_scene_.Reset();
  light_bench_panel_.reset();
  Base::OnShutdown();
}

auto MainModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  auto& shell = GetShell();

  if (shell.HasStagedScene()) {
    CHECK_F(shell.PublishStagedScene(),
      "expected staged scene before frame-start publish");
    active_scene_ = shell.GetActiveScene();
    auto published_camera = shell.TakePublishedMainCamera();
    if (published_camera.IsAlive()) {
      main_camera_ = std::move(published_camera);
    }
    light_scene_.SetScene(shell.TryGetScene());
  }

  shell.OnFrameStart(*context);
  Base::OnFrameStart(context);

  if (!HasRenderableWindow()) {
    return;
  }

  if (const auto scene_ptr = shell.TryGetScene()) {
    context->SetScene(scene_ptr);
  }
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    DLOG_F(1, "OnSceneMutation: no valid window - skipping");
    co_return;
  }

  if (!active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
    co_return;
  }

  light_scene_.Update();
  co_await Base::OnSceneMutation(context);
}

auto MainModule::UpdateComposition(engine::FrameContext& context,
  std::vector<vortex::CompositionView>& views) -> void
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

  auto main_comp
    = vortex::CompositionView::ForScene(main_view_id_, view, main_camera_);
  main_comp.with_atmosphere = true;
  shell.OnMainViewReady(context, main_comp);
  views.push_back(std::move(main_comp));

  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(vortex::CompositionView::ForImGui(
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
  if (!active_scene_.IsValid() || !main_camera_.IsAlive()) {
    co_return;
  }

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

  co_await Base::OnPreRender(context);
}

auto MainModule::OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
  -> void
{
}

} // namespace oxygen::examples::light_bench
