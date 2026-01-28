//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/DemoShell.h"

#include <chrono>
#include <string>
#include <string_view>
#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/DemoShellUi.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/EnvironmentDebugPanel.h"
#include "DemoShell/UI/LightCullingDebugPanel.h"
#include "DemoShell/UI/RenderingPanel.h"
#include "DemoShell/UI/SettingsPanel.h"

namespace {

template <typename PanelType>
class PanelAdapter final : public oxygen::examples::DemoPanel {
public:
  PanelAdapter(std::string_view name, oxygen::observer_ptr<PanelType> panel,
    std::string_view icon = {}, float preferred_width = 420.0F)
    : name_(name)
    , panel_(panel)
    , icon_(icon)
    , preferred_width_(preferred_width)
  {
  }

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return name_;
  }

  auto DrawContents() -> void override
  {
    if (panel_) {
      panel_->DrawContents();
    }
  }

  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override
  {
    return preferred_width_;
  }

  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override
  {
    return icon_;
  }

private:
  std::string name_ {};
  oxygen::observer_ptr<PanelType> panel_ { nullptr };
  std::string icon_ {};
  float preferred_width_ { 420.0F };
};

auto HasLightCullingConfig(
  const oxygen::examples::ui::LightCullingDebugConfig& config) -> bool
{
  return config.shader_pass_config && config.light_culling_pass_config;
}

auto ApplyDefaultClusterCallback(
  oxygen::examples::ui::LightCullingDebugConfig& config) -> void
{
  if (!config.on_cluster_mode_changed) {
    config.on_cluster_mode_changed = []() {
      LOG_F(INFO, "Light culling mode changed, PSO will rebuild next frame");
    };
  }
}

} // namespace

namespace oxygen::examples {

struct DemoShell::Impl {
  DemoShellConfig config {};
  bool initialized { false };

  PanelRegistry panel_registry {};
  DemoShellUi demo_shell_ui {};
  std::vector<std::unique_ptr<DemoPanel>> demo_panels {};

  ui::ContentLoaderPanel content_loader_panel {};
  ui::CameraControlPanel camera_control_panel {};
  ui::LightingPanel lighting_panel {};
  ui::RenderingPanel rendering_panel {};
  ui::SettingsPanel settings_panel {};
  ui::EnvironmentDebugPanel environment_debug_panel {};

  std::unique_ptr<ui::CameraRigController> camera_rig {};
  CameraLifecycleService camera_lifecycle {};
};

DemoShell::DemoShell()
  : impl_(std::make_unique<Impl>())
{
}

DemoShell::~DemoShell() = default;

/*!
 Initializes the demo shell and registers the standard panel set.

 @param config Shell configuration with dependencies and callbacks.
 @return True when the shell is ready for use.

### Performance Characteristics

- Time Complexity: $O(n)$ for registering $n$ panels.
- Memory: $O(n)$ for panel adapters.
- Optimization: Reuses persistent panel instances across frames.

### Usage Examples

 ```cpp
 DemoShell shell;
 DemoShellConfig config;
 config.input_system = observer_ptr { input_system };
 if (!shell.Initialize(config)) {
   return false;
 }
 ```

 @note Initialization config is cached for subsequent updates.
 @warning The input system must outlive the demo shell.
 @see DemoShell::Update, DemoShell::Draw
*/
auto DemoShell::Initialize(const DemoShellConfig& config) -> bool
{
  impl_->config = config;

  const bool needs_file_browser = impl_->config.panel_config.content_loader
    || impl_->config.panel_config.environment;
  if (needs_file_browser) {
    CHECK_NOTNULL_F(impl_->config.file_browser_service,
      "DemoShell requires a FileBrowserService");
  }

  if (impl_->config.enable_camera_rig) {
    if (!impl_->config.input_system) {
      LOG_F(WARNING, "DemoShell: input system required for camera rig");
      return false;
    }
    impl_->camera_rig = std::make_unique<ui::CameraRigController>();
    if (!impl_->camera_rig->Initialize(
          observer_ptr { impl_->config.input_system })) {
      LOG_F(WARNING, "DemoShell: CameraRigController initialization failed");
      return false;
    }
    impl_->camera_lifecycle.BindCameraRig(
      observer_ptr { impl_->camera_rig.get() });
  }
  impl_->camera_lifecycle.SetScene(impl_->config.scene);

  impl_->demo_shell_ui.Initialize(DemoShellUiConfig {
    .panel_registry = observer_ptr { &impl_->panel_registry },
    .active_camera
    = observer_ptr { &impl_->camera_lifecycle.GetActiveCamera() },
  });

  InitializePanels();
  RegisterDemoPanels();
  impl_->initialized = true;

  return true;
}

/*!
 Updates the demo shell for the current frame phase.

 @param delta_time Frame delta. Pass a zero duration for scene mutation
   updates and a non-zero duration for gameplay camera updates.

### Performance Characteristics

- Time Complexity: $O(p)$ for $p$ panel updates during mutation.
- Memory: $O(1)$ additional allocations.
- Optimization: Skips redundant work based on the update phase.

### Usage Examples

 ```cpp
 shell.Update(time::CanonicalDuration {}); // Scene mutation
 shell.Update(context.GetGameDeltaTime()); // Gameplay
 ```

 @note Panel orchestration runs only when the delta time is zero.
 @warning Do not call with non-zero delta twice per frame.
 @see DemoShell::Draw, DemoShell::UpdateScene
*/
auto DemoShell::Update(time::CanonicalDuration delta_time) -> void
{
  if (!impl_->initialized) {
    return;
  }

  const auto u_delta_time = delta_time.get();
  if (u_delta_time == std::chrono::nanoseconds::zero()) {
    UpdatePanels();
    return;
  }

  if (impl_->camera_rig) {
    impl_->camera_rig->Update(delta_time);
  }
}

/*!
 Draws the demo shell UI layout and active panel contents.

### Performance Characteristics

- Time Complexity: $O(1)$ plus active panel draw cost.
- Memory: $O(1)$ additional allocations.
- Optimization: Delegates layout to the shared DemoShellUi.

### Usage Examples

 ```cpp
 shell.Draw();
 ```

 @note Must be called within an active ImGui frame scope.
 @see DemoShellUi::Draw
*/
auto DemoShell::Draw() -> void
{
  if (!impl_->initialized) {
    return;
  }

  impl_->demo_shell_ui.Draw();
}

/*!
 Registers a demo-specific panel with the shell registry.

 @param panel Panel instance to register.
 @return True when the panel was registered successfully.

### Performance Characteristics

- Time Complexity: $O(n)$ for $n$ registered panels.
- Memory: $O(1)$ additional allocations.
- Optimization: Uses the existing panel registry storage.

### Usage Examples

 ```cpp
 shell.RegisterPanel(observer_ptr { &my_panel });
 ```

 @note Panels must remain alive while registered.
 @warning Call only after DemoShell::Initialize.
 @see PanelRegistry::RegisterPanel
*/
auto DemoShell::RegisterPanel(observer_ptr<DemoPanel> panel) -> bool
{
  if (!impl_->initialized) {
    LOG_F(WARNING, "DemoShell: cannot register panel before initialization");
    return false;
  }
  if (!panel) {
    LOG_F(WARNING, "DemoShell: cannot register null panel");
    return false;
  }

  const auto result = impl_->panel_registry.RegisterPanel(panel);
  if (!result) {
    LOG_F(
      WARNING, "DemoShell: failed to register panel '{}'", panel->GetName());
    return false;
  }

  return true;
}

/*!
 Updates the scene reference used by panels and camera lifecycle.

 @param scene Shared scene pointer (may be null when clearing).

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Avoids redundant initialization by reusing panel instances.

### Usage Examples

 ```cpp
 shell.UpdateScene(scene_);
 ```

 @note Call this when the active scene is replaced or cleared.
 @see CameraLifecycleService::SetScene
*/
auto DemoShell::UpdateScene(std::shared_ptr<scene::Scene> scene) -> void
{
  impl_->config.scene = std::move(scene);
  impl_->camera_lifecycle.SetScene(impl_->config.scene);
}

/*!
 Assigns the active camera node used by the rig and camera panels.

 @param camera Camera node to control and inspect.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Reuses existing camera controllers.

### Usage Examples

 ```cpp
 shell.SetActiveCamera(std::move(active_camera));
 ```

 @note This refreshes the camera control panel bindings.
 @see CameraControlPanel::UpdateConfig
*/
auto DemoShell::SetActiveCamera(scene::SceneNode camera) -> void
{
  impl_->camera_lifecycle.SetActiveCamera(std::move(camera));

  if (impl_->camera_rig) {
    impl_->camera_rig->SetActiveCamera(
      observer_ptr { &impl_->camera_lifecycle.GetActiveCamera() });
  }

  UpdateCameraControlPanelConfig();
}

/*!
 Updates the skybox service reference for environment panels.

 @param skybox_service Non-owning pointer to the skybox service.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Uses non-owning references to avoid lifetime coupling.

### Usage Examples

 ```cpp
 shell.SetSkyboxService(observer_ptr { skybox_service_.get() });
 ```

 @note Call this whenever the skybox service is recreated.
 @see EnvironmentDebugPanel::UpdateConfig
*/
auto DemoShell::SetSkyboxService(observer_ptr<SkyboxService> skybox_service)
  -> void
{
  impl_->config.skybox_service = skybox_service;
}

/*!
 Cancels any in-flight content import operations.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Delegates to the content loader panel.

### Usage Examples

 ```cpp
 shell.CancelContentImport();
 ```

 @note Intended for shutdown or scene reset workflows.
 @see ui::ImportPanel::CancelImport
*/
auto DemoShell::CancelContentImport() -> void
{
  if (!impl_->config.panel_config.content_loader) {
    return;
  }
  impl_->content_loader_panel.GetImportPanel().CancelImport();
}

/*!
 Returns the camera lifecycle service for advanced control.

 @return Reference to the internal camera lifecycle helper.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Returns a reference without copying.

### Usage Examples

 ```cpp
 shell.GetCameraLifecycle().EnsureViewport(width, height);
 ```

 @note The reference remains valid for the shell lifetime.
 @see CameraLifecycleService
*/
auto DemoShell::GetCameraLifecycle() -> CameraLifecycleService&
{
  return impl_->camera_lifecycle;
}

/*!
 Returns the current rendering view mode from the rendering panel.

 @return The selected view mode.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Directly queries the cached UI state.

### Usage Examples

 ```cpp
 const auto mode = shell.GetRenderingViewMode();
 ```

 @note Used to apply wireframe or solid render modes.
 @see ui::RenderingPanel
*/
auto DemoShell::GetRenderingViewMode() const -> ui::RenderingViewMode
{
  return impl_->rendering_panel.GetViewMode();
}

auto DemoShell::InitializePanels() -> void
{
  if (impl_->config.panel_config.content_loader) {
    ui::ContentLoaderPanel::Config loader_config;
    loader_config.file_browser_service = impl_->config.file_browser_service;
    loader_config.cooked_root = impl_->config.cooked_root;
    loader_config.on_scene_load_requested
      = impl_->config.on_scene_load_requested;
    loader_config.on_dump_texture_memory = impl_->config.on_dump_texture_memory;
    loader_config.get_last_released_scene_key
      = impl_->config.get_last_released_scene_key;
    loader_config.on_force_trim = impl_->config.on_force_trim;
    loader_config.on_pak_mounted = impl_->config.on_pak_mounted;
    loader_config.on_loose_index_loaded = impl_->config.on_loose_index_loaded;
    impl_->content_loader_panel.Initialize(loader_config);
  }

  if (impl_->config.panel_config.camera_controls) {
    UpdateCameraControlPanelConfig();
  }

  if (impl_->config.panel_config.lighting
    || impl_->config.panel_config.rendering) {
    if (impl_->config.get_light_culling_debug_config) {
      auto debug_config = impl_->config.get_light_culling_debug_config();
      ApplyDefaultClusterCallback(debug_config);
      if (HasLightCullingConfig(debug_config)) {
        if (impl_->config.panel_config.lighting) {
          impl_->lighting_panel.Initialize(debug_config);
        }
        if (impl_->config.panel_config.rendering) {
          impl_->rendering_panel.Initialize(debug_config);
        }
      }
    }
  }

  if (impl_->config.panel_config.settings) {
    ui::SettingsPanelConfig settings_config;
    settings_config.axes_widget
      = observer_ptr { &impl_->demo_shell_ui.GetAxesWidget() };
    settings_config.stats_overlay
      = observer_ptr { &impl_->demo_shell_ui.GetStatsOverlay() };
    impl_->settings_panel.Initialize(settings_config);
  }

  if (impl_->config.panel_config.environment) {
    ui::EnvironmentDebugConfig env_config;
    env_config.scene = impl_->config.scene;
    env_config.file_browser_service = impl_->config.file_browser_service;
    env_config.skybox_service = impl_->config.skybox_service;

    const auto renderer = impl_->config.get_renderer
      ? impl_->config.get_renderer()
      : observer_ptr<engine::Renderer> { nullptr };
    env_config.renderer = renderer;
    env_config.on_atmosphere_params_changed = [renderer]() {
      LOG_F(INFO, "Atmosphere parameters changed, LUTs will regenerate");
      if (renderer) {
        if (auto lut_mgr = renderer->GetSkyAtmosphereLutManager()) {
          lut_mgr->MarkDirty();
        }
      }
    };
    env_config.on_exposure_changed
      = []() { LOG_F(INFO, "Exposure settings changed"); };

    impl_->environment_debug_panel.Initialize(env_config);
  }
}

auto DemoShell::UpdatePanels() -> void
{
  if (impl_->config.panel_config.content_loader) {
    impl_->content_loader_panel.Update();
  }

  if (impl_->config.panel_config.lighting
    || impl_->config.panel_config.rendering) {
    if (impl_->config.get_light_culling_debug_config) {
      auto debug_config = impl_->config.get_light_culling_debug_config();
      ApplyDefaultClusterCallback(debug_config);
      if (HasLightCullingConfig(debug_config)) {
        if (impl_->config.panel_config.lighting) {
          impl_->lighting_panel.UpdateConfig(debug_config);
        }
        if (impl_->config.panel_config.rendering) {
          impl_->rendering_panel.UpdateConfig(debug_config);
        }
      }
    }
  }

  if (impl_->config.panel_config.settings) {
    ui::SettingsPanelConfig settings_config;
    settings_config.axes_widget
      = observer_ptr { &impl_->demo_shell_ui.GetAxesWidget() };
    settings_config.stats_overlay
      = observer_ptr { &impl_->demo_shell_ui.GetStatsOverlay() };
    impl_->settings_panel.UpdateConfig(settings_config);
  }

  if (impl_->config.panel_config.environment && impl_->config.scene) {
    ui::EnvironmentDebugConfig env_config;
    env_config.scene = impl_->config.scene;
    env_config.file_browser_service = impl_->config.file_browser_service;
    env_config.skybox_service = impl_->config.skybox_service;
    const auto renderer = impl_->config.get_renderer
      ? impl_->config.get_renderer()
      : observer_ptr<engine::Renderer> { nullptr };
    env_config.renderer = renderer;
    env_config.on_atmosphere_params_changed = [renderer]() {
      LOG_F(INFO, "Atmosphere parameters changed, LUTs will regenerate");
      if (renderer) {
        if (auto lut_mgr = renderer->GetSkyAtmosphereLutManager()) {
          lut_mgr->MarkDirty();
        }
      }
    };
    env_config.on_exposure_changed
      = []() { LOG_F(INFO, "Exposure settings changed"); };
    impl_->environment_debug_panel.UpdateConfig(env_config);

    if (impl_->environment_debug_panel.HasPendingChanges()) {
      impl_->environment_debug_panel.ApplyPendingChanges();
    }
  }
}

auto DemoShell::UpdateCameraControlPanelConfig() -> void
{
  if (!impl_->config.panel_config.camera_controls) {
    return;
  }

  ui::CameraControlConfig camera_config;
  camera_config.active_camera
    = observer_ptr { &impl_->camera_lifecycle.GetActiveCamera() };
  if (impl_->camera_rig) {
    camera_config.orbit_controller = impl_->camera_rig->GetOrbitController();
    camera_config.fly_controller = impl_->camera_rig->GetFlyController();
    camera_config.move_fwd_action = impl_->camera_rig->GetMoveForwardAction();
    camera_config.move_bwd_action = impl_->camera_rig->GetMoveBackwardAction();
    camera_config.move_left_action = impl_->camera_rig->GetMoveLeftAction();
    camera_config.move_right_action = impl_->camera_rig->GetMoveRightAction();
    camera_config.fly_boost_action = impl_->camera_rig->GetFlyBoostAction();
    camera_config.fly_plane_lock_action
      = impl_->camera_rig->GetFlyPlaneLockAction();
    camera_config.rmb_action = impl_->camera_rig->GetRmbAction();
    camera_config.orbit_action = impl_->camera_rig->GetOrbitAction();
  }

  camera_config.on_mode_changed = [this](ui::CameraControlMode mode) {
    if (impl_->camera_rig) {
      impl_->camera_rig->SetMode(mode);
    }
    impl_->camera_lifecycle.RequestSyncFromActive();
  };

  camera_config.on_reset_requested
    = [this]() { impl_->camera_lifecycle.RequestReset(); };

  impl_->camera_control_panel.UpdateConfig(camera_config);

  const auto ui_mode = impl_->camera_control_panel.GetMode();
  if (impl_->camera_rig) {
    impl_->camera_rig->SetMode(ui_mode);
  }
}

auto DemoShell::RegisterDemoPanels() -> void
{
  impl_->panel_registry = PanelRegistry {};
  impl_->demo_panels.clear();

  const auto register_panel
    = [this](auto panel_ptr, std::string_view name, std::string_view icon = {},
        float preferred_width = 420.0F) {
        using PanelType = std::remove_reference_t<decltype(*panel_ptr)>;
        auto adapter = std::make_unique<PanelAdapter<PanelType>>(
          name, observer_ptr { panel_ptr }, icon, preferred_width);
        auto* adapter_ptr = adapter.get();
        impl_->demo_panels.push_back(std::move(adapter));

        const auto result
          = impl_->panel_registry.RegisterPanel(observer_ptr { adapter_ptr });
        if (!result) {
          LOG_F(WARNING, "DemoShell: failed to register panel '{}'", name);
        }
      };

  namespace icons = oxygen::imgui::icons;
  if (impl_->config.panel_config.content_loader) {
    register_panel(&impl_->content_loader_panel, "Content Loader",
      icons::kIconContentLoader, 520.0F);
  }
  if (impl_->config.panel_config.camera_controls) {
    register_panel(&impl_->camera_control_panel, "Camera Controls",
      icons::kIconCameraControls, 360.0F);
  }
  if (impl_->config.panel_config.environment) {
    register_panel(&impl_->environment_debug_panel, "Environment",
      icons::kIconEnvironment, 420.0F);
  }
  if (impl_->config.panel_config.lighting) {
    register_panel(
      &impl_->lighting_panel, "Lighting", icons::kIconLighting, 360.0F);
  }
  if (impl_->config.panel_config.rendering) {
    register_panel(
      &impl_->rendering_panel, "Rendering", icons::kIconRendering, 320.0F);
  }
  if (impl_->config.panel_config.settings) {
    register_panel(
      &impl_->settings_panel, "Settings", icons::kIconSettings, 320.0F);
  }
}

} // namespace oxygen::examples
