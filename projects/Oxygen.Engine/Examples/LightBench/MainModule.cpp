//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <source_location>
#include <utility>
#include <vector>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/SceneActivationPolicy.h"
#include "LightBench/LightBenchConsoleBindings.h"
#include "LightBench/LightBenchPanel.h"
#include "LightBench/LightBenchSettings.h"
#include "LightBench/MainModule.h"
#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::examples::light_bench {

namespace {
  constexpr std::uint32_t kWindowWidth = 2560;
  constexpr std::uint32_t kWindowHeight = 1440;

  auto AllocateMainViewStateHandle() -> vortex::CompositionView::ViewStateHandle
  {
    static std::atomic_uint64_t next_handle { 1U };
    const auto handle = vortex::CompositionView::ViewStateHandle {
      next_handle.fetch_add(1U, std::memory_order_relaxed)
    };
    CHECK_F(handle != vortex::CompositionView::kInvalidViewStateHandle,
      "LightBench temporal view state identity exhausted");
    return handle;
  }
} // namespace

MainModule::MainModule(
  const DemoAppContext& app, LightBenchPreset startup_preset)
  : Base(app)
  , active_preset_(startup_preset)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

MainModule::~MainModule() = default;

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen LightBench");
  p.extent = { .width = kWindowWidth, .height = kWindowHeight };
  p.flags = {
    .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false,
  };
  return p;
}

auto MainModule::ClearBackbufferReferences() -> void { }

auto MainModule::StageInitialScene(
  DemoShell& shell, const LightBenchSettings& settings) -> void
{
  shell.StageScene(light_scene_.CreateScene());
  const auto staged_scene = shell.GetStagedScene();
  CHECK_NOTNULL_F(staged_scene, "LightBench staged scene is null");

  main_camera_ = LightScene::CreateReferenceCamera(*staged_scene);
  reference_width_ = reference_height_ = 0;
  shell.SetStagedMainCamera(main_camera_);

  light_scene_.SetScene(staged_scene);
  ApplySettings(settings, light_scene_, main_camera_);
  active_preset_ = settings.preset;
  initial_auto_ev_ = settings.initial_auto_ev;
  pending_auto_seed_ = settings.exposure.enabled
      && settings.exposure.mode == engine::ExposureMode::kAuto
    ? std::optional { settings.initial_auto_ev }
    : std::nullopt;
  fitted_camera_extents_ = settings.camera_fit_to_view
    ? std::optional { settings.camera_extents }
    : std::nullopt;
}

auto MainModule::OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
try {
  DCHECK_NOTNULL_F(engine, "expecting a valid engine");

  auto shell = std::make_unique<DemoShell>();
  const auto demo_root
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path();
  auto shell_config = DemoShellConfig {};
  shell_config.engine = engine;
  shell_config.enable_camera_rig = true;
  shell_config.enable_renderer_bound_panels = false;
  shell_config.force_environment_override = false;
  shell_config.scene_activation_policy
    = SceneActivationPolicy::kExperimentOwned;
  shell_config.content_roots = {
    .content_root = demo_root.parent_path() / "Content",
    .cooked_root = demo_root / ".cooked",
  };
  shell_config.panel_config = {
    .content_loader = false,
    .camera_controls = true,
    .environment = false,
    .lighting = false,
    .diagnostics = false,
    .post_process = true,
    .ground_grid = false,
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "LightBench: DemoShell initialization failed");
    return nullptr;
  }

  light_bench_panel_ = std::make_shared<LightBenchPanel>(
    observer_ptr { &light_scene_ },
    LightBenchPanel::Actions {
      .reset
      = [this] -> void { pending_settings_ = PresetSettings(active_preset_); },
      .select = [this](LightBenchPreset preset) -> void {
        pending_settings_ = PresetSettings(preset);
      },
      .selected = [this] -> LightBenchPreset { return active_preset_; },
      .is_reference = [this] -> bool {
        return main_camera_.IsAlive()
          && IsPresetSettings(CaptureCurrentSettings());
      },
      .save = [this](const auto& path) -> auto { return SaveSettings(path); },
      .load = [this](const auto& path) -> auto { return LoadSettings(path); },
    },
    demo_root / "lightbench.saved.json");
  if (!shell->RegisterPanel(light_bench_panel_)) {
    LOG_F(WARNING, "LightBench: failed to register LightBench panel");
  }
  StageInitialScene(*shell, PresetSettings(active_preset_));
  shell->SetActivePanel("LightBench");

  main_view_id_ = GetOrCreateViewId("MainView");
  console_bindings_
    = std::make_unique<LightBenchConsoleBindings>(engine->GetConsole(),
      LightBenchConsoleBindings::Actions {
        .select =
          [this](LightBenchPreset preset) {
            pending_settings_ = PresetSettings(preset);
          },
        .reset = [this] { pending_settings_ = PresetSettings(active_preset_); },
        .inspect =
          [this] {
            return fmt::format("active={} pending={} modified={}",
              GetPresetInfo(active_preset_).id,
              pending_settings_ ? GetPresetInfo(pending_settings_->preset).id
                                : "none",
              main_camera_.IsAlive()
                && !IsPresetSettings(CaptureCurrentSettings()));
          },
      });
  LOG_F(INFO, "LightBench: MainView ID created: {}", main_view_id_.get());
  LOG_F(INFO, "LightBench: Module initialized");
  return shell;
} catch (const std::exception& error) {
  console_bindings_.reset();
  LOG_F(ERROR, "LightBench initialization failed: {}", error.what());
  return nullptr;
} catch (...) {
  console_bindings_.reset();
  LOG_F(ERROR, "LightBench initialization failed: unknown exception");
  return nullptr;
}

auto MainModule::CaptureCurrentSettings() -> LightBenchSettings
{
  auto settings = CaptureSettings(light_scene_, main_camera_);
  settings.preset = active_preset_;
  settings.initial_auto_ev = initial_auto_ev_;
  settings.camera_fit_to_view = fitted_camera_extents_
    && settings.camera_extents == *fitted_camera_extents_;
  if (settings.camera_fit_to_view) {
    settings.camera_extents = ReferenceSettings().camera_extents;
  }
  return settings;
}

auto MainModule::SaveSettings(const std::filesystem::path& path)
  -> Result<void, std::string>
{
  if (!main_camera_.IsAlive() || path.empty()) {
    return Err(std::string("No active scene or file path."));
  }
  const auto saved = SaveSettingsFile(path, CaptureCurrentSettings());
  if (!saved) {
    return Err(saved.error().message);
  }
  return {};
}

auto MainModule::LoadSettings(const std::filesystem::path& path)
  -> Result<void, std::string>
{
  const auto decoded = LoadSettingsFile(path);
  if (!decoded) {
    return Err(decoded.error().message);
  }
  pending_settings_ = *decoded;
  return {};
}

auto MainModule::OnShutdown() noexcept -> void
{
  console_bindings_.reset();
  ResetMainViewState();
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

  if (pending_settings_) {
    StageInitialScene(shell, *pending_settings_);
    pending_settings_.reset();
  }

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
    ResetMainViewState(observer_ptr { &context });
    return;
  }

  if (!main_view_state_camera_.IsAlive()
    || main_view_state_camera_.GetHandle() != main_camera_.GetHandle()) {
    ResetMainViewState(observer_ptr { &context });
    main_view_state_handle_ = AllocateMainViewStateHandle();
    main_view_state_camera_ = main_camera_;
    if (pending_auto_seed_) {
      if (auto renderer = ResolveVortexRenderer()) {
        const auto queued
          = renderer->QueueExposureTransition(main_view_state_handle_,
            vortex::ExposureTransitionPolicy::kSeedFromEv100,
            *pending_auto_seed_);
        CHECK_F(
          queued.has_value(), "LightBench Auto startup seed was rejected");
        pending_auto_seed_.reset();
      }
    }
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

  const auto width = static_cast<std::uint32_t>(view.viewport.width);
  const auto height = static_cast<std::uint32_t>(view.viewport.height);
  if (width != reference_width_ || height != reference_height_) {
    reference_width_ = width;
    reference_height_ = height;
    const auto lens = main_camera_.GetCameraAs<scene::OrthographicCamera>();
    if (lens && fitted_camera_extents_
      && lens->get().GetExtents() == *fitted_camera_extents_ && height > 0) {
      LightScene::FitReferenceCamera(
        main_camera_, static_cast<float>(width) / static_cast<float>(height));
      fitted_camera_extents_ = lens->get().GetExtents();
    } else {
      fitted_camera_extents_.reset();
    }
  }

  auto main_comp
    = vortex::CompositionView::ForScene(main_view_id_, view, main_camera_);
  main_comp.view_state_handle = main_view_state_handle_;
  main_comp.with_atmosphere = false;
  main_comp.render_settings.render_mode = vortex::RenderMode::kSolid;
  main_comp.render_settings.shader_debug_mode
    = vortex::ShaderDebugMode::kDisabled;
  shell.OnMainViewReady(context, main_comp);
  views.push_back(std::move(main_comp));

  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(vortex::CompositionView::ForImGui(
    imgui_view_id, view, [](graphics::CommandRecorder&) -> void { }));
}

auto MainModule::ResetMainViewState(
  const observer_ptr<engine::FrameContext> context) -> void
{
  if (main_view_state_handle_
    != vortex::CompositionView::kInvalidViewStateHandle) {
    if (auto renderer = ResolveVortexRenderer()) {
      if (context) {
        renderer->RemovePublishedRuntimeView(*context, main_view_id_);
      } else {
        renderer->RemovePublishedRuntimeView(main_view_id_);
      }
    }
  }
  main_view_state_handle_ = vortex::CompositionView::kInvalidViewStateHandle;
  main_view_state_camera_ = {};
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
  if (const auto renderer = ResolveVortexRenderer();
    renderer && renderer->IsImGuiFrameActive() && light_bench_panel_) {
    ImGui::SetCurrentContext(renderer->GetImGuiContext());
    light_bench_panel_->DrawPresetOverlay();
  }
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

auto MainModule::OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void
{
  static_cast<void>(context);
}

} // namespace oxygen::examples::light_bench
