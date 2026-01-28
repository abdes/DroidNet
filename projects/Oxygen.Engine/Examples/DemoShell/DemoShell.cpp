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

#include "DemoShell/Services/FileBrowserService.h"

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
  config_ = config;

  CHECK_NOTNULL_F(
    config_.file_browser_service, "DemoShell requires a FileBrowserService");

  camera_rig_ = std::make_unique<ui::CameraRigController>();
  if (!camera_rig_->Initialize(observer_ptr { config_.input_system })) {
    LOG_F(WARNING, "DemoShell: CameraRigController initialization failed");
    return false;
  }
  camera_lifecycle_.BindCameraRig(observer_ptr { camera_rig_.get() });
  camera_lifecycle_.SetScene(config_.scene);

  demo_shell_ui_.Initialize(DemoShellUiConfig {
    .panel_registry = observer_ptr { &panel_registry_ },
    .active_camera = observer_ptr { &camera_lifecycle_.GetActiveCamera() },
  });

  InitializePanels();
  RegisterDemoPanels();
  initialized_ = true;

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
  if (!initialized_) {
    return;
  }

  const auto u_delta_time = delta_time.get();
  if (u_delta_time == std::chrono::nanoseconds::zero()) {
    UpdatePanels();
    return;
  }

  if (camera_rig_) {
    camera_rig_->Update(delta_time);
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
  if (!initialized_) {
    return;
  }

  demo_shell_ui_.Draw();
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
  config_.scene = std::move(scene);
  camera_lifecycle_.SetScene(config_.scene);
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
  camera_lifecycle_.SetActiveCamera(std::move(camera));

  if (camera_rig_) {
    camera_rig_->SetActiveCamera(
      observer_ptr { &camera_lifecycle_.GetActiveCamera() });
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
  config_.skybox_service = skybox_service;
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
  content_loader_panel_.GetImportPanel().CancelImport();
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
  return camera_lifecycle_;
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
  return rendering_panel_.GetViewMode();
}

auto DemoShell::InitializePanels() -> void
{
  ui::ContentLoaderPanel::Config loader_config;
  loader_config.file_browser_service = config_.file_browser_service;
  loader_config.cooked_root = config_.cooked_root;
  loader_config.on_scene_load_requested = config_.on_scene_load_requested;
  loader_config.on_dump_texture_memory = config_.on_dump_texture_memory;
  loader_config.get_last_released_scene_key
    = config_.get_last_released_scene_key;
  loader_config.on_force_trim = config_.on_force_trim;
  loader_config.on_pak_mounted = config_.on_pak_mounted;
  loader_config.on_loose_index_loaded = config_.on_loose_index_loaded;
  content_loader_panel_.Initialize(loader_config);

  UpdateCameraControlPanelConfig();

  if (config_.get_light_culling_debug_config) {
    auto debug_config = config_.get_light_culling_debug_config();
    ApplyDefaultClusterCallback(debug_config);
    if (HasLightCullingConfig(debug_config)) {
      lighting_panel_.Initialize(debug_config);
      rendering_panel_.Initialize(debug_config);
    }
  }

  ui::SettingsPanelConfig settings_config;
  settings_config.axes_widget
    = observer_ptr { &demo_shell_ui_.GetAxesWidget() };
  settings_config.stats_overlay
    = observer_ptr { &demo_shell_ui_.GetStatsOverlay() };
  settings_panel_.Initialize(settings_config);

  ui::EnvironmentDebugConfig env_config;
  env_config.scene = config_.scene;
  env_config.file_browser_service = config_.file_browser_service;
  env_config.skybox_service = config_.skybox_service;

  const auto renderer = config_.get_renderer
    ? config_.get_renderer()
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

  environment_debug_panel_.Initialize(env_config);
}

auto DemoShell::UpdatePanels() -> void
{
  content_loader_panel_.Update();

  if (config_.get_light_culling_debug_config) {
    auto debug_config = config_.get_light_culling_debug_config();
    ApplyDefaultClusterCallback(debug_config);
    if (HasLightCullingConfig(debug_config)) {
      lighting_panel_.UpdateConfig(debug_config);
      rendering_panel_.UpdateConfig(debug_config);
    }
  }

  ui::SettingsPanelConfig settings_config;
  settings_config.axes_widget
    = observer_ptr { &demo_shell_ui_.GetAxesWidget() };
  settings_config.stats_overlay
    = observer_ptr { &demo_shell_ui_.GetStatsOverlay() };
  settings_panel_.UpdateConfig(settings_config);

  if (config_.scene) {
    ui::EnvironmentDebugConfig env_config;
    env_config.scene = config_.scene;
    env_config.file_browser_service = config_.file_browser_service;
    env_config.skybox_service = config_.skybox_service;
    const auto renderer = config_.get_renderer
      ? config_.get_renderer()
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
    environment_debug_panel_.UpdateConfig(env_config);

    if (environment_debug_panel_.HasPendingChanges()) {
      environment_debug_panel_.ApplyPendingChanges();
    }
  }
}

auto DemoShell::UpdateCameraControlPanelConfig() -> void
{
  ui::CameraControlConfig camera_config;
  camera_config.active_camera
    = observer_ptr { &camera_lifecycle_.GetActiveCamera() };
  if (camera_rig_) {
    camera_config.orbit_controller = camera_rig_->GetOrbitController();
    camera_config.fly_controller = camera_rig_->GetFlyController();
    camera_config.move_fwd_action = camera_rig_->GetMoveForwardAction();
    camera_config.move_bwd_action = camera_rig_->GetMoveBackwardAction();
    camera_config.move_left_action = camera_rig_->GetMoveLeftAction();
    camera_config.move_right_action = camera_rig_->GetMoveRightAction();
    camera_config.fly_boost_action = camera_rig_->GetFlyBoostAction();
    camera_config.fly_plane_lock_action = camera_rig_->GetFlyPlaneLockAction();
    camera_config.rmb_action = camera_rig_->GetRmbAction();
    camera_config.orbit_action = camera_rig_->GetOrbitAction();
  }

  camera_config.on_mode_changed = [this](ui::CameraControlMode mode) {
    if (camera_rig_) {
      camera_rig_->SetMode(mode);
    }
    camera_lifecycle_.RequestSyncFromActive();
  };

  camera_config.on_reset_requested
    = [this]() { camera_lifecycle_.RequestReset(); };

  camera_control_panel_.UpdateConfig(camera_config);

  const auto ui_mode = camera_control_panel_.GetMode();
  if (camera_rig_) {
    camera_rig_->SetMode(ui_mode);
  }
}

auto DemoShell::RegisterDemoPanels() -> void
{
  panel_registry_ = PanelRegistry {};
  demo_panels_.clear();

  const auto register_panel
    = [this](auto panel_ptr, std::string_view name, std::string_view icon = {},
        float preferred_width = 420.0F) {
        using PanelType = std::remove_reference_t<decltype(*panel_ptr)>;
        auto adapter = std::make_unique<PanelAdapter<PanelType>>(
          name, observer_ptr { panel_ptr }, icon, preferred_width);
        auto* adapter_ptr = adapter.get();
        demo_panels_.push_back(std::move(adapter));

        const auto result
          = panel_registry_.RegisterPanel(observer_ptr { adapter_ptr });
        if (!result) {
          LOG_F(WARNING, "DemoShell: failed to register panel '{}'", name);
        }
      };

  namespace icons = oxygen::imgui::icons;
  register_panel(&content_loader_panel_, "Content Loader",
    icons::kIconContentLoader, 520.0F);
  register_panel(&camera_control_panel_, "Camera Controls",
    icons::kIconCameraControls, 360.0F);
  register_panel(
    &environment_debug_panel_, "Environment", icons::kIconEnvironment, 420.0F);
  register_panel(&lighting_panel_, "Lighting", icons::kIconLighting, 360.0F);
  register_panel(&rendering_panel_, "Rendering", icons::kIconRendering, 320.0F);
  register_panel(&settings_panel_, "Settings", icons::kIconSettings, 320.0F);
}

} // namespace oxygen::examples
