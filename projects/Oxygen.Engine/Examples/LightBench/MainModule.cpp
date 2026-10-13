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
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>
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
  const host::HostContext& host, LightBenchPreset startup_preset)
  : Base(host)
  , active_preset_(startup_preset)
{
}

MainModule::~MainModule() = default;

auto MainModule::BuildWindowProperties() const -> platform::window::Properties
{
  auto props = Base::BuildWindowProperties();
  props.title = "Oxygen LightBench";
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  return props;
}

auto MainModule::StageInitialScene(const LightBenchSettings& settings) -> void
{
  Stage().Stage(light_scene_.CreateScene());
  const auto staged_scene = Stage().GetStagedScene();

  main_camera_ = LightScene::CreateReferenceCamera(*staged_scene);
  reference_width_ = reference_height_ = 0;
  Stage().SetStagedMainCamera(main_camera_);

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
  auto shell_config = MakeShellConfig();
  shell_config.enable_camera_rig = true;
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
    .diagnostics = false,
    .post_process = true,
    .ground_grid = false,
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "LightBench: DemoShell initialization failed");
    return nullptr;
  }

  console_bindings_ = std::make_unique<LightBenchConsoleBindings>(
    engine->GetConsole(),
    LightBenchConsoleBindings::Actions {
      .select = [this](LightBenchPreset preset) -> void {
        pending_settings_ = PresetSettings(preset);
      },
      .reset
      = [this] -> void { pending_settings_ = PresetSettings(active_preset_); },
      .inspect = [this] -> std::string {
        return fmt::format("active={} pending={} modified={}",
          GetPresetInfo(active_preset_).id,
          pending_settings_ ? GetPresetInfo(pending_settings_->preset).id
                            : "none",
          main_camera_.IsAlive()
            && !IsPresetSettings(CaptureCurrentSettings()));
      },
    });
  light_bench_panel_
    = std::make_shared<LightBenchPanel>(observer_ptr { &light_scene_ },
      LightBenchPanel::Actions {
        .selected = [this] -> LightBenchPreset { return active_preset_; },
        .is_reference = [this] -> bool {
          return main_camera_.IsAlive()
            && IsPresetSettings(CaptureCurrentSettings());
        },
        .save = [this](const auto& path) -> auto { return SaveSettings(path); },
        .load = [this](const auto& path) -> auto { return LoadSettings(path); },
      },
      demo_root / "lightbench.saved.json", shell->GetPanelCommands());
  if (!shell->RegisterPanel(light_bench_panel_)) {
    LOG_F(WARNING, "LightBench: failed to register LightBench panel");
  }
  StageInitialScene(PresetSettings(active_preset_));
  shell->SetActivePanel("LightBench");

  main_view_id_ = Publisher().GetOrCreateViewId("MainView");
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
#ifdef OXYGEN_BUILD_UI_TESTS
  StopUiTests();
#endif
  console_bindings_.reset();
  ResetMainViewState();
  Stage().Clear();
  Base::OnShutdown();
  light_scene_.Reset();
  light_bench_panel_.reset();
}

auto MainModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  // A preset or loaded settings replace the scene; the host publishes it in
  // the base frame start below.
  if (pending_settings_) {
    StageInitialScene(*pending_settings_);
    pending_settings_.reset();
  }
  Base::OnFrameStart(context);
}

auto MainModule::OnScenePublished(engine::FrameContext& context) -> void
{
  Base::OnScenePublished(context);
  active_scene_ = Stage().GetActiveScene();
  auto published_camera = Stage().TakePublishedMainCamera();
  if (published_camera.IsAlive()) {
    main_camera_ = std::move(published_camera);
  }
  light_scene_.SetScene(Stage().TryGetScene());
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (!HasRenderableWindow() || !active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
    co_return;
  }

  light_scene_.Update();
  co_await Base::OnSceneMutation(context);
}

auto MainModule::BuildViews(
  engine::FrameContext& context, host::HostViews& views) -> void
{
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
      if (auto renderer = ResolveRenderer()) {
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
  if (Window() && Window()->GetWindow()) {
    const auto extent = Window()->GetWindow()->Size();
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
  views.views.push_back(std::move(main_comp));
  views.main = main_view_id_;
}

auto MainModule::ResetMainViewState(
  const observer_ptr<engine::FrameContext> context) -> void
{
  if (main_view_state_handle_
    != vortex::CompositionView::kInvalidViewStateHandle) {
    if (auto renderer = ResolveRenderer()) {
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

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  co_await Base::OnGuiUpdate(context);
  if (!active_scene_.IsValid() || !main_camera_.IsAlive()) {
    co_return;
  }
  if (const auto imgui_module = ResolveImGuiModule();
    imgui_module && imgui_module->IsFrameActive() && light_bench_panel_) {
    ImGui::SetCurrentContext(imgui_module->GetImGuiContext());
    light_bench_panel_->DrawPresetOverlay(UiTopInset());
  }
  co_return;
}

} // namespace oxygen::examples::light_bench
