//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <optional>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Internal/SceneControlBlock.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/EnvironmentDebugPanel.h"
#include "DemoShell/UI/LightCullingDebugPanel.h"
#include "DemoShell/UI/RenderingPanel.h"

namespace {
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
  std::optional<ui::DemoShellUi> demo_shell_ui {};

  UiSettingsService ui_settings_service {};

  std::shared_ptr<ui::ContentLoaderPanel> content_loader_panel {};
  std::shared_ptr<ui::CameraControlPanel> camera_control_panel {};
  std::shared_ptr<ui::LightingPanel> lighting_panel {};
  std::shared_ptr<ui::RenderingPanel> rendering_panel {};
  std::shared_ptr<ui::EnvironmentDebugPanel> environment_debug_panel {};

  std::unique_ptr<ui::CameraRigController> camera_rig {};
  CameraLifecycleService camera_lifecycle {};
  internal::SceneControlBlock scene_control {};
};

DemoShell::DemoShell()
  : impl_(std::make_unique<Impl>())
{
}

DemoShell::~DemoShell() noexcept
{
  if (!impl_) {
    return;
  }

  try {
    impl_->panel_registry.ClearActivePanel();
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "DemoShell: OnUnloaded threw: {}", ex.what());
  } catch (...) {
    LOG_F(ERROR, "DemoShell: OnUnloaded threw unknown exception");
  }
}

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

  const auto settings = SettingsService::Default();
  CHECK_NOTNULL_F(settings.get(),
    "DemoShell requires SettingsService before panel registration");

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
  impl_->camera_lifecycle.SetScene(impl_->scene_control.TryGetScene());

  impl_->demo_shell_ui.emplace(observer_ptr { &impl_->panel_registry },
    observer_ptr { &impl_->camera_lifecycle },
    observer_ptr { &impl_->ui_settings_service });

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
    impl_->camera_lifecycle.PersistActiveCameraSettings();
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

  if (impl_->demo_shell_ui) {
    impl_->demo_shell_ui->Draw();
  }
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
 shell.RegisterPanel(std::make_shared<MyPanel>());
 ```

 @note Panels must remain alive while registered.
 @warning Call only after DemoShell::Initialize.
 @see PanelRegistry::RegisterPanel
*/
auto DemoShell::RegisterPanel(std::shared_ptr<DemoPanel> panel) -> bool
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
 Sets the active scene, transferring ownership to the shell.

 @param scene Scene ownership to transfer (may be null to clear).
 @return ActiveScene value for accessing the new active scene.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Avoids redundant initialization by reusing panel instances.

### Usage Examples

 ```cpp
 auto active_scene = shell.SetScene(std::move(scene));
 ```

 @note Call this when the active scene is replaced or cleared.
 @see CameraLifecycleService::SetScene
*/
auto DemoShell::SetScene(std::unique_ptr<scene::Scene> scene) -> ActiveScene
{
  impl_->scene_control.SetScene(std::move(scene));
  impl_->camera_lifecycle.SetScene(impl_->scene_control.TryGetScene());
  return ActiveScene { observer_ptr { &impl_->scene_control } };
}

auto DemoShell::GetActiveScene() const -> ActiveScene
{
  return ActiveScene { observer_ptr { &impl_->scene_control } };
}

auto DemoShell::TryGetScene() const -> observer_ptr<scene::Scene>
{
  return impl_->scene_control.TryGetScene();
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
  if (impl_->content_loader_panel) {
    impl_->content_loader_panel->GetImportPanel().CancelImport();
  }
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
  if (!impl_->rendering_panel) {
    return ui::RenderingViewMode::kSolid;
  }
  return impl_->rendering_panel->GetViewMode();
}

auto DemoShell::SetActivePanel(std::string_view panel_name) -> void
{
  if (!impl_->initialized) {
    return;
  }
  if (panel_name.empty()) {
    impl_->panel_registry.ClearActivePanel();
    impl_->ui_settings_service.SetActivePanelName(std::nullopt);
    return;
  }

  const auto result = impl_->panel_registry.SetActivePanelByName(panel_name);
  if (!result) {
    return;
  }
  impl_->ui_settings_service.SetActivePanelName(std::string(panel_name));
}

auto DemoShell::InitializePanels() -> void
{
  if (impl_->config.panel_config.content_loader) {
    if (!impl_->content_loader_panel) {
      impl_->content_loader_panel = std::make_shared<ui::ContentLoaderPanel>();
    }
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
    impl_->content_loader_panel->Initialize(loader_config);
  }

  if (impl_->config.panel_config.camera_controls) {
    if (!impl_->camera_control_panel) {
      impl_->camera_control_panel = std::make_shared<ui::CameraControlPanel>();
    }
    UpdateCameraControlPanelConfig();
  }

  if (impl_->config.panel_config.lighting
    || impl_->config.panel_config.rendering) {
    if (impl_->config.get_light_culling_debug_config) {
      auto debug_config = impl_->config.get_light_culling_debug_config();
      ApplyDefaultClusterCallback(debug_config);
      if (HasLightCullingConfig(debug_config)) {
        if (impl_->config.panel_config.lighting) {
          if (!impl_->lighting_panel) {
            impl_->lighting_panel = std::make_shared<ui::LightingPanel>();
          }
          impl_->lighting_panel->Initialize(debug_config);
        }
        if (impl_->config.panel_config.rendering) {
          if (!impl_->rendering_panel) {
            impl_->rendering_panel = std::make_shared<ui::RenderingPanel>();
          }
          impl_->rendering_panel->Initialize(debug_config);
        }
      }
    }
  }

  if (impl_->config.panel_config.environment) {
    if (!impl_->environment_debug_panel) {
      impl_->environment_debug_panel
        = std::make_shared<ui::EnvironmentDebugPanel>();
    }
    ui::EnvironmentDebugConfig env_config;
    env_config.scene = TryGetScene();
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

    impl_->environment_debug_panel->Initialize(env_config);
  }
}

auto DemoShell::UpdatePanels() -> void
{
  if (impl_->config.panel_config.camera_controls) {
    UpdateCameraControlPanelConfig();
  }

  if (impl_->config.panel_config.content_loader) {
    if (impl_->content_loader_panel) {
      impl_->content_loader_panel->Update();
    }
  }

  if (impl_->config.panel_config.lighting
    || impl_->config.panel_config.rendering) {
    if (impl_->config.get_light_culling_debug_config) {
      auto debug_config = impl_->config.get_light_culling_debug_config();
      ApplyDefaultClusterCallback(debug_config);
      if (HasLightCullingConfig(debug_config)) {
        if (impl_->config.panel_config.lighting) {
          if (impl_->lighting_panel) {
            impl_->lighting_panel->UpdateConfig(debug_config);
          }
        }
        if (impl_->config.panel_config.rendering) {
          if (impl_->rendering_panel) {
            impl_->rendering_panel->UpdateConfig(debug_config);
          }
        }
      }
    }
  }

  if (impl_->config.panel_config.environment && TryGetScene()) {
    ui::EnvironmentDebugConfig env_config;
    env_config.scene = TryGetScene();
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
    if (impl_->environment_debug_panel) {
      impl_->environment_debug_panel->UpdateConfig(env_config);

      if (impl_->environment_debug_panel->HasPendingChanges()) {
        impl_->environment_debug_panel->ApplyPendingChanges();
      }
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
    camera_config.current_mode = impl_->camera_rig->GetMode();
  }

  camera_config.on_mode_changed = [this](ui::CameraControlMode mode) {
    if (impl_->camera_rig) {
      impl_->camera_rig->SetMode(mode);
    }
    impl_->camera_lifecycle.RequestSyncFromActive();
  };

  camera_config.on_reset_requested
    = [this]() { impl_->camera_lifecycle.RequestReset(); };

  camera_config.on_panel_closed
    = [this]() { impl_->camera_lifecycle.PersistActiveCameraSettings(); };

  if (impl_->camera_control_panel) {
    impl_->camera_control_panel->UpdateConfig(camera_config);
  }
}

auto DemoShell::RegisterDemoPanels() -> void
{
  const auto register_panel = [this](std::shared_ptr<DemoPanel> panel) {
    const auto name = panel ? std::string_view(panel->GetName()) : "<null>";
    const auto result = impl_->panel_registry.RegisterPanel(panel);
    if (!result) {
      LOG_F(WARNING, "DemoShell: failed to register panel '{}'", name);
    }
  };

  if (impl_->config.panel_config.content_loader) {
    register_panel(impl_->content_loader_panel);
  }
  if (impl_->config.panel_config.camera_controls) {
    register_panel(impl_->camera_control_panel);
  }
  if (impl_->config.panel_config.environment) {
    register_panel(impl_->environment_debug_panel);
  }
  if (impl_->config.panel_config.lighting) {
    register_panel(impl_->lighting_panel);
  }
  if (impl_->config.panel_config.rendering) {
    register_panel(impl_->rendering_panel);
  }
}

} // namespace oxygen::examples
