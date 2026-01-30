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
#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/EnvironmentDebugPanel.h"
#include "DemoShell/UI/RenderingVm.h"
#include "DemoShell/UI/LightCullingVm.h"

namespace oxygen::examples {

struct DemoShell::Impl {
  DemoShellConfig config {};
  bool initialized { false };

  PanelRegistry panel_registry {};
  std::optional<ui::DemoShellUi> demo_shell_ui {};

  // Services (owned by DemoShell)
  UiSettingsService ui_settings_service {};
  RenderingSettingsService rendering_settings_service {};
  LightCullingSettingsService light_culling_settings_service {};
  CameraSettingsService camera_settings_service {};

  // Panels still managed by DemoShell (non-MVVM panels)
  std::shared_ptr<ui::ContentLoaderPanel> content_loader_panel {};
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

  // Create DemoShellUi with all services
  impl_->demo_shell_ui.emplace(observer_ptr { &impl_->panel_registry },
    observer_ptr { &impl_->camera_lifecycle },
    observer_ptr { &impl_->ui_settings_service },
    observer_ptr { &impl_->rendering_settings_service },
    observer_ptr { &impl_->light_culling_settings_service },
    observer_ptr { &impl_->camera_settings_service },
    observer_ptr { impl_->camera_rig.get() });

  InitializePanels();
  RegisterDemoPanels();
  impl_->initialized = true;

  return true;
}

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

auto DemoShell::Draw() -> void
{
  if (!impl_->initialized) {
    return;
  }

  if (impl_->demo_shell_ui) {
    impl_->demo_shell_ui->Draw();
  }
}

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

auto DemoShell::SetActiveCamera(scene::SceneNode camera) -> void
{
  impl_->camera_lifecycle.SetActiveCamera(std::move(camera));

  if (impl_->camera_rig) {
    impl_->camera_rig->SetActiveCamera(
      observer_ptr { &impl_->camera_lifecycle.GetActiveCamera() });
  }
}

auto DemoShell::SetSkyboxService(observer_ptr<SkyboxService> skybox_service)
  -> void
{
  impl_->config.skybox_service = skybox_service;
}

auto DemoShell::CancelContentImport() -> void
{
  if (!impl_->config.panel_config.content_loader) {
    return;
  }
  if (impl_->content_loader_panel) {
    impl_->content_loader_panel->GetImportPanel().CancelImport();
  }
}

auto DemoShell::GetCameraLifecycle() -> CameraLifecycleService&
{
  return impl_->camera_lifecycle;
}

auto DemoShell::GetRenderingViewMode() const -> ui::RenderingViewMode
{
  if (!impl_->demo_shell_ui) {
    return ui::RenderingViewMode::kSolid;
  }
  auto vm = impl_->demo_shell_ui->GetRenderingVm();
  if (!vm) {
    return ui::RenderingViewMode::kSolid;
  }
  return vm->GetViewMode();
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

auto DemoShell::GetActivePanelName() const -> std::optional<std::string>
{
  return impl_->ui_settings_service.GetActivePanelName();
}

auto DemoShell::GetRenderingDebugMode() const -> engine::ShaderDebugMode
{
  if (!impl_->demo_shell_ui) {
    return engine::ShaderDebugMode::kDisabled;
  }
  auto vm = impl_->demo_shell_ui->GetRenderingVm();
  if (!vm) {
    return engine::ShaderDebugMode::kDisabled;
  }
  return vm->GetDebugMode();
}

auto DemoShell::GetLightCullingVisualizationMode() const
  -> engine::ShaderDebugMode
{
  if (!impl_->demo_shell_ui) {
    return engine::ShaderDebugMode::kDisabled;
  }
  auto vm = impl_->demo_shell_ui->GetLightCullingVm();
  if (!vm) {
    return engine::ShaderDebugMode::kDisabled;
  }
  return vm->GetVisualizationMode();
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

  // Camera panel is created by DemoShellUi if requested.
  // Rendering and Lighting panels are created lazily by DemoShellUi
  // when pass configs become available (via EnsureXxxPanelReady)

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
  if (impl_->config.panel_config.content_loader) {
    if (impl_->content_loader_panel) {
      impl_->content_loader_panel->Update();
    }
  }

  // Lazily create rendering and lighting panels via DemoShellUi
  if ((impl_->config.panel_config.lighting
        || impl_->config.panel_config.rendering)
    && impl_->demo_shell_ui && impl_->config.get_pass_config_refs) {
    auto refs = impl_->config.get_pass_config_refs();

    if (impl_->config.panel_config.rendering) {
      impl_->demo_shell_ui->EnsureRenderingPanelReady(refs);
    }
    if (impl_->config.panel_config.lighting) {
      impl_->demo_shell_ui->EnsureLightingPanelReady(refs);
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
  if (impl_->config.panel_config.environment) {
    register_panel(impl_->environment_debug_panel);
  }

  // Rendering and Lighting panels are registered by DemoShellUi
  // when they are lazily created via EnsureXxxPanelReady
}

} // namespace oxygen::examples
