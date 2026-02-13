//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <optional>
#include <string_view>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/Console/CommandPalette.h>
#include <Oxygen/ImGui/Console/ConsolePanel.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>
#include <Oxygen/ImGui/ImGuiModule.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/GridSettingsService.h"
#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/AxesWidget.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraVm.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/EnvironmentDebugPanel.h"
#include "DemoShell/UI/EnvironmentVm.h"
#include "DemoShell/UI/GridPanel.h"
#include "DemoShell/UI/GridVm.h"
#include "DemoShell/UI/LightCullingDebugPanel.h"
#include "DemoShell/UI/LightCullingVm.h"
#include "DemoShell/UI/PanelSideBar.h"
#include "DemoShell/UI/PostProcessPanel.h"
#include "DemoShell/UI/PostProcessVm.h"
#include "DemoShell/UI/RenderingPanel.h"
#include "DemoShell/UI/RenderingVm.h"
#include "DemoShell/UI/SidePanel.h"
#include "DemoShell/UI/StatsOverlay.h"
#include "DemoShell/UI/UiSettingsPanel.h"
#include "DemoShell/UI/UiSettingsVm.h"

namespace oxygen::examples::ui {

namespace {

constexpr float kGeometryEpsilon = 0.5F;
constexpr std::string_view kConsoleXKey = "demo_shell.console.window.x";
constexpr std::string_view kConsoleYKey = "demo_shell.console.window.y";
constexpr std::string_view kConsoleWidthKey = "demo_shell.console.window.width";
constexpr std::string_view kConsoleHeightKey = "demo_shell.console.window.height";
constexpr std::string_view kPaletteXKey = "demo_shell.palette.window.x";
constexpr std::string_view kPaletteYKey = "demo_shell.palette.window.y";
constexpr std::string_view kPaletteWidthKey = "demo_shell.palette.window.width";
constexpr std::string_view kPaletteHeightKey = "demo_shell.palette.window.height";
constexpr std::string_view kConsoleAutoScrollKey = "demo_shell.console.auto_scroll";
constexpr std::string_view kConsoleFilterOkKey = "demo_shell.console.filter.ok";
constexpr std::string_view kConsoleFilterWarningKey
  = "demo_shell.console.filter.warning";
constexpr std::string_view kConsoleFilterErrorKey = "demo_shell.console.filter.error";

struct WindowSettingKeys final {
  std::string_view x;
  std::string_view y;
  std::string_view width;
  std::string_view height;
};

constexpr WindowSettingKeys kConsoleWindowKeys {
  .x = kConsoleXKey,
  .y = kConsoleYKey,
  .width = kConsoleWidthKey,
  .height = kConsoleHeightKey,
};

constexpr WindowSettingKeys kPaletteWindowKeys {
  .x = kPaletteXKey,
  .y = kPaletteYKey,
  .width = kPaletteWidthKey,
  .height = kPaletteHeightKey,
};

auto IsApproximatelyEqual(const float lhs, const float rhs) noexcept -> bool
{
  return std::abs(lhs - rhs) <= kGeometryEpsilon;
}

auto IsPlacementDifferent(const imgui::consoleui::WindowPlacement& lhs,
  const imgui::consoleui::WindowPlacement& rhs) noexcept -> bool
{
  return !IsApproximatelyEqual(lhs.x, rhs.x)
    || !IsApproximatelyEqual(lhs.y, rhs.y)
    || !IsApproximatelyEqual(lhs.width, rhs.width)
    || !IsApproximatelyEqual(lhs.height, rhs.height);
}

auto TryLoadPlacement(observer_ptr<SettingsService> settings,
  const WindowSettingKeys& keys)
  -> std::optional<imgui::consoleui::WindowPlacement>
{
  if (!settings) {
    return std::nullopt;
  }

  const auto x = settings->GetFloat(keys.x);
  const auto y = settings->GetFloat(keys.y);
  const auto width = settings->GetFloat(keys.width);
  const auto height = settings->GetFloat(keys.height);
  if (!x.has_value() || !y.has_value() || !width.has_value()
    || !height.has_value()) {
    return std::nullopt;
  }

  return imgui::consoleui::WindowPlacement {
    .x = *x,
    .y = *y,
    .width = *width,
    .height = *height,
  };
}

auto HandleGlobalConsoleAccelerators(
  imgui::consoleui::ConsoleUiState& console_ui_state) -> void
{
  // "Hard" global accelerators: process regardless of ImGui capture flags.
  if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
    if (!console_ui_state.IsConsoleVisible()) {
      console_ui_state.SetConsoleVisible(true);
    } else if (console_ui_state.ConsoleInput().empty()) {
      console_ui_state.SetConsoleVisible(false);
    } else {
      console_ui_state.RequestConsoleFocus();
    }
  }
  if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift
    && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
    console_ui_state.TogglePalette();
  }
}

} // namespace

struct DemoShellUi::Impl {
  observer_ptr<AsyncEngine> engine;
  observer_ptr<PanelRegistry> panel_registry;
  observer_ptr<RenderingSettingsService> rendering_settings_service;
  observer_ptr<LightCullingSettingsService> light_culling_settings_service;
  observer_ptr<CameraSettingsService> camera_settings_service;
  observer_ptr<EnvironmentSettingsService> environment_settings_service;
  observer_ptr<PostProcessSettingsService> post_process_settings_service;
  observer_ptr<GridSettingsService> grid_settings_service;
  observer_ptr<FileBrowserService> file_browser_service;
  DemoShellPanelConfig panel_config;

  // UI Settings (always created)
  UiSettingsVm ui_settings_vm;
  observer_ptr<UiSettingsVm> ui_settings_vm_ptr { &ui_settings_vm };

  // Core UI components
  imgui::consoleui::ConsoleUiState console_ui_state;
  imgui::consoleui::ConsolePanel console_panel;
  imgui::consoleui::CommandPalette command_palette;
  PanelSideBar side_bar;
  SidePanel side_panel;
  AxesWidget axes_widget;
  StatsOverlay stats_overlay;
  std::shared_ptr<UiSettingsPanel> settings_panel;
  std::optional<imgui::consoleui::WindowPlacement> last_saved_console_window;
  std::optional<imgui::consoleui::WindowPlacement> last_saved_palette_window;
  bool last_saved_auto_scroll { true };
  std::array<bool, 3> last_saved_severity_filters { true, true, true };

  // Rendering panel (created lazily when pass config is available)
  std::unique_ptr<RenderingVm> rendering_vm;
  std::shared_ptr<RenderingPanel> rendering_panel;

  // Lighting panel (created lazily when pass configs are available)
  std::unique_ptr<LightCullingVm> light_culling_vm;
  std::shared_ptr<LightingPanel> lighting_panel;

  // Camera panel
  std::unique_ptr<CameraVm> camera_vm;
  std::shared_ptr<CameraControlPanel> camera_panel;

  // Content panel
  std::shared_ptr<ContentVm> content_vm;
  std::shared_ptr<ContentLoaderPanel> content_panel;

  // Environment panel
  std::unique_ptr<EnvironmentVm> environment_vm;
  std::shared_ptr<EnvironmentDebugPanel> environment_panel;

  // PostProcess panel
  std::unique_ptr<PostProcessVm> post_process_vm;
  std::shared_ptr<PostProcessPanel> post_process_panel;

  // Ground grid panel
  std::unique_ptr<GridVm> grid_vm;
  std::shared_ptr<GridPanel> grid_panel;

  Impl(observer_ptr<AsyncEngine> engine_ptr,
    observer_ptr<PanelRegistry> registry,
    observer_ptr<UiSettingsService> ui_settings_service,
    observer_ptr<RenderingSettingsService> rendering_settings,
    observer_ptr<LightCullingSettingsService> light_culling_settings,
    observer_ptr<CameraSettingsService> camera_settings,
    observer_ptr<ContentSettingsService> content_settings,
    observer_ptr<EnvironmentSettingsService> environment_settings,
    observer_ptr<PostProcessSettingsService> post_process_settings,
    observer_ptr<GridSettingsService> grid_settings,
    observer_ptr<CameraRigController> camera_rig,
    observer_ptr<FileBrowserService> file_browser,
    const DemoShellPanelConfig& panel_config_in)
    : engine(engine_ptr)
    , panel_registry(registry)
    , rendering_settings_service(rendering_settings)
    , light_culling_settings_service(light_culling_settings)
    , camera_settings_service(camera_settings)
    , environment_settings_service(environment_settings)
    , post_process_settings_service(post_process_settings)
    , grid_settings_service(grid_settings)
    , file_browser_service(file_browser)
    , panel_config(panel_config_in)
    , ui_settings_vm(ui_settings_service, camera_settings)
    , side_bar(panel_registry, ui_settings_vm_ptr)
    , side_panel(panel_registry)
    , axes_widget(ui_settings_vm_ptr)
    , stats_overlay(ui_settings_vm_ptr)
  {
    DCHECK_NOTNULL_F(panel_registry, "expecting valid PanelRegistry");
    DCHECK_NOTNULL_F(ui_settings_service, "expecting valid UiSettingsService");

    // Create Camera VM and Panel
    if (panel_config.camera_controls && camera_settings && camera_rig) {
      camera_vm = std::make_unique<CameraVm>(camera_settings, camera_rig);
      camera_panel = std::make_shared<CameraControlPanel>(
        observer_ptr { camera_vm.get() });
      if (panel_registry->RegisterPanel(camera_panel)) {
        LOG_F(INFO, "Registered Camera panel");
      }
    }

    // Create Content VM and Panel
    if (panel_config.content_loader && content_settings && file_browser) {
      content_vm = std::make_shared<ContentVm>(content_settings, file_browser);
      content_panel = std::make_shared<ContentLoaderPanel>(
        observer_ptr { content_vm.get() });
      if (panel_registry->RegisterPanel(content_panel)) {
        LOG_F(INFO, "Registered Content panel");
      }
    }

    // Create Environment VM and Panel
    if (panel_config.environment && environment_settings) {
      environment_vm = std::make_unique<EnvironmentVm>(
        environment_settings, post_process_settings, file_browser);
      environment_panel = std::make_shared<EnvironmentDebugPanel>();

      EnvironmentDebugConfig env_config;
      env_config.environment_vm = observer_ptr { environment_vm.get() };
      environment_panel->Initialize(env_config);
      if (panel_registry->RegisterPanel(environment_panel)) {
        LOG_F(INFO, "Registered Environment panel");
      }
    }

    // Create PostProcess VM and Panel
    if (panel_config.post_process && post_process_settings) {
      post_process_vm = std::make_unique<PostProcessVm>(post_process_settings);
      post_process_panel = std::make_shared<PostProcessPanel>(
        observer_ptr { post_process_vm.get() });
      if (panel_registry->RegisterPanel(post_process_panel)) {
        LOG_F(INFO, "Registered PostProcess panel");
      }
    }

    // Create Ground Grid VM and Panel
    if (panel_config.ground_grid && grid_settings) {
      grid_vm = std::make_unique<GridVm>(grid_settings);
      grid_panel = std::make_shared<GridPanel>(observer_ptr { grid_vm.get() });
      if (panel_registry->RegisterPanel(grid_panel)) {
        LOG_F(INFO, "Registered Ground Grid panel");
      }
    }

    settings_panel = std::make_shared<UiSettingsPanel>(ui_settings_vm_ptr);
    const auto result = panel_registry->RegisterPanel(settings_panel);
    if (!result) {
      LOG_F(WARNING, "Failed to register Settings panel");
    }

    LoadConsoleUiSettingsFromStorage();
  }

  auto LoadConsoleUiSettingsFromStorage() -> void
  {
    const auto settings = SettingsService::ForDemoApp();
    if (!settings) {
      return;
    }

    if (const auto console_window
      = TryLoadPlacement(settings, kConsoleWindowKeys);
      console_window.has_value()) {
      console_ui_state.SetConsoleWindowPlacement(*console_window);
      last_saved_console_window = console_window;
    }
    if (const auto palette_window
      = TryLoadPlacement(settings, kPaletteWindowKeys);
      palette_window.has_value()) {
      console_ui_state.SetPaletteWindowPlacement(*palette_window);
      last_saved_palette_window = palette_window;
    }

    last_saved_auto_scroll
      = settings->GetBool(kConsoleAutoScrollKey).value_or(true);
    console_ui_state.SetAutoScrollEnabled(last_saved_auto_scroll);

    const std::array<imgui::consoleui::LogSeverity, 3> severities {
      imgui::consoleui::LogSeverity::kSuccess,
      imgui::consoleui::LogSeverity::kWarning,
      imgui::consoleui::LogSeverity::kError,
    };
    const std::array<std::string_view, 3> keys {
      kConsoleFilterOkKey,
      kConsoleFilterWarningKey,
      kConsoleFilterErrorKey,
    };
    for (size_t i = 0; i < severities.size(); ++i) {
      const bool value = settings->GetBool(keys[i]).value_or(true);
      console_ui_state.SetSeverityEnabled(severities[i], value);
      last_saved_severity_filters[i] = value;
    }
  }

  auto PersistConsoleUiSettingsToStorage() -> void
  {
    const auto settings = SettingsService::ForDemoApp();
    if (!settings) {
      return;
    }

    if (const auto& current = console_ui_state.ConsoleWindowPlacement();
      current.has_value()) {
      const bool save_geometry = !last_saved_console_window.has_value()
        || IsPlacementDifferent(*current, *last_saved_console_window);
      if (save_geometry && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        settings->SetFloat(kConsoleXKey, current->x);
        settings->SetFloat(kConsoleYKey, current->y);
        settings->SetFloat(kConsoleWidthKey, current->width);
        settings->SetFloat(kConsoleHeightKey, current->height);
        last_saved_console_window = current;
      }
    }

    if (const auto& current = console_ui_state.PaletteWindowPlacement();
      current.has_value()) {
      const bool save_geometry = !last_saved_palette_window.has_value()
        || IsPlacementDifferent(*current, *last_saved_palette_window);
      if (save_geometry && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        settings->SetFloat(kPaletteXKey, current->x);
        settings->SetFloat(kPaletteYKey, current->y);
        settings->SetFloat(kPaletteWidthKey, current->width);
        settings->SetFloat(kPaletteHeightKey, current->height);
        last_saved_palette_window = current;
      }
    }

    const bool auto_scroll = console_ui_state.IsAutoScrollEnabled();
    if (auto_scroll != last_saved_auto_scroll) {
      settings->SetBool(kConsoleAutoScrollKey, auto_scroll);
      last_saved_auto_scroll = auto_scroll;
    }

    const std::array<imgui::consoleui::LogSeverity, 3> severities {
      imgui::consoleui::LogSeverity::kSuccess,
      imgui::consoleui::LogSeverity::kWarning,
      imgui::consoleui::LogSeverity::kError,
    };
    const std::array<std::string_view, 3> keys {
      kConsoleFilterOkKey,
      kConsoleFilterWarningKey,
      kConsoleFilterErrorKey,
    };
    for (size_t i = 0; i < severities.size(); ++i) {
      const bool enabled = console_ui_state.IsSeverityEnabled(severities[i]);
      if (enabled != last_saved_severity_filters[i]) {
        settings->SetBool(keys[i], enabled);
        last_saved_severity_filters[i] = enabled;
      }
    }
  }
};

DemoShellUi::DemoShellUi(observer_ptr<AsyncEngine> engine,
  observer_ptr<PanelRegistry> panel_registry,
  observer_ptr<UiSettingsService> ui_settings_service,
  observer_ptr<RenderingSettingsService> rendering_settings_service,
  observer_ptr<LightCullingSettingsService> light_culling_settings_service,
  observer_ptr<CameraSettingsService> camera_settings_service,
  observer_ptr<ContentSettingsService> content_settings_service,
  observer_ptr<EnvironmentSettingsService> environment_settings_service,
  observer_ptr<PostProcessSettingsService> post_process_settings_service,
  observer_ptr<GridSettingsService> grid_settings_service,
  observer_ptr<CameraRigController> camera_rig,
  observer_ptr<FileBrowserService> file_browser_service,
  const DemoShellPanelConfig& panel_config)
  : impl_(std::make_unique<Impl>(engine, panel_registry, ui_settings_service,
      rendering_settings_service, light_culling_settings_service,
      camera_settings_service, content_settings_service,
      environment_settings_service, post_process_settings_service,
      grid_settings_service, camera_rig, file_browser_service, panel_config))
{
}

DemoShellUi::~DemoShellUi() = default;

auto DemoShellUi::Draw(observer_ptr<engine::FrameContext> fc) -> void
{
  if (!impl_->engine) {
    return;
  }

  auto imgui_module_ref = impl_->engine->GetModule<imgui::ImGuiModule>();
  if (!imgui_module_ref) {
    return;
  }

  auto& imgui_module = imgui_module_ref->get();
  if (!imgui_module.IsWitinFrameScope()) {
    return;
  }

  auto* imgui_context = imgui_module.GetImGuiContext();
  if (imgui_context == nullptr) {
    return;
  }

  ImGui::SetCurrentContext(imgui_context);

  const auto& io = ImGui::GetIO();
  HandleGlobalConsoleAccelerators(impl_->console_ui_state);

  auto& console = impl_->engine->GetConsole();
  impl_->console_panel.Draw(console, impl_->console_ui_state);
  impl_->command_palette.Draw(console, impl_->console_ui_state);
  impl_->PersistConsoleUiSettingsToStorage();

  if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    last_mouse_down_position_ = SubPixelPosition {
      .x = io.MousePos.x,
      .y = io.MousePos.y,
    };
  }

  impl_->side_bar.Draw();
  impl_->side_panel.Draw(impl_->side_bar.GetWidth());

  // Settings now flow through UiSettingsVm instead of view-owned state.
  impl_->axes_widget.Draw(impl_->ui_settings_vm.GetActiveCamera());
  impl_->stats_overlay.Draw(fc);

  if (impl_->file_browser_service) {
    impl_->file_browser_service->UpdateAndDraw();
  }
}

auto DemoShellUi::EnsureRenderingPanelReady(RenderingPipeline& pipeline) -> void
{
  if (!impl_->panel_config.rendering) {
    return;
  }
  if (impl_->rendering_panel) {
    return;
  }

  const auto features = pipeline.GetSupportedFeatures();
  if ((features & PipelineFeature::kOpaqueShading) == PipelineFeature::kNone) {
    return;
  }

  if (!impl_->rendering_settings_service) {
    LOG_F(
      WARNING, "Cannot create RenderingPanel without RenderingSettingsService");
    return;
  }

  // Create the ViewModel
  impl_->rendering_vm
    = std::make_unique<RenderingVm>(impl_->rendering_settings_service);

  // Create the Panel with the ViewModel
  impl_->rendering_panel = std::make_shared<RenderingPanel>(
    observer_ptr { impl_->rendering_vm.get() });

  // Register with panel registry
  if (impl_->panel_registry->RegisterPanel(impl_->rendering_panel)) {
    LOG_F(INFO, "Registered Rendering panel");
  } else {
    LOG_F(WARNING, "Failed to register Rendering panel");
  }
}

auto DemoShellUi::EnsureLightingPanelReady(RenderingPipeline& pipeline) -> void
{
  if (!impl_->panel_config.lighting) {
    return;
  }
  if (impl_->lighting_panel) {
    return;
  }

  const auto features = pipeline.GetSupportedFeatures();
  if ((features & PipelineFeature::kLightCulling) == PipelineFeature::kNone) {
    return;
  }

  if (!impl_->light_culling_settings_service) {
    LOG_F(WARNING,
      "Cannot create LightingPanel without LightCullingSettingsService");
    return;
  }

  // Create the ViewModel
  impl_->light_culling_vm = std::make_unique<LightCullingVm>(
    impl_->light_culling_settings_service, nullptr /* No callback needed */);

  // Create the Panel with the ViewModel
  impl_->lighting_panel = std::make_shared<LightingPanel>(
    observer_ptr { impl_->light_culling_vm.get() });

  // Register with panel registry
  if (impl_->panel_registry->RegisterPanel(impl_->lighting_panel)) {
    LOG_F(INFO, "Registered Lighting panel");
  } else {
    LOG_F(WARNING, "Failed to register Lighting panel");
  }
}

auto DemoShellUi::RegisterCustomPanel(std::shared_ptr<DemoPanel> panel) -> bool
{
  if (!panel) {
    LOG_F(WARNING, "Cannot register null panel");
    return false;
  }
  if (!impl_->panel_registry) {
    LOG_F(WARNING, "Panel registry not available");
    return false;
  }

  const auto name = std::string_view(panel->GetName());
  if (name.empty()) {
    LOG_F(WARNING, "Cannot register panel with empty name");
    return false;
  }

  try {
    const auto result = impl_->panel_registry->RegisterPanel(std::move(panel));
    if (!result) {
      LOG_F(WARNING, "Failed to register panel '{}'", name);
      return false;
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Failed to register panel '{}': {}", name, ex.what());
    return false;
  }

  return true;
}

auto DemoShellUi::GetRenderingVm() const -> observer_ptr<RenderingVm>
{
  return observer_ptr { impl_->rendering_vm.get() };
}

auto DemoShellUi::GetLightCullingVm() const -> observer_ptr<LightCullingVm>
{
  return observer_ptr { impl_->light_culling_vm.get() };
}

auto DemoShellUi::GetCameraVm() const -> observer_ptr<CameraVm>
{
  return observer_ptr { impl_->camera_vm.get() };
}

auto DemoShellUi::GetContentVm() const -> observer_ptr<ContentVm>
{
  return observer_ptr { impl_->content_vm.get() };
}

auto DemoShellUi::GetEnvironmentVm() const -> observer_ptr<EnvironmentVm>
{
  return observer_ptr { impl_->environment_vm.get() };
}

/*!
 Returns the last mouse-down position captured by the UI.

 @return The most recent mouse-down position if captured, otherwise an empty
   optional.

### Performance Characteristics

- Time Complexity: O(1)
- Memory: None
- Optimization: None
*/
auto DemoShellUi::GetLastMouseDownPosition() const
  -> std::optional<SubPixelPosition>
{
  return last_mouse_down_position_;
}

} // namespace oxygen::examples::ui
