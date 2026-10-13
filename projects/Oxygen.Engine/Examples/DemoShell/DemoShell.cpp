//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Internal/PostProcessConsoleBindings.h"
#include "DemoShell/PanelCommands.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/Runtime/SceneActivationPolicy.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/GridSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/SkyboxService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/EnvironmentVm.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::examples {

struct DemoShell::Impl {
  DemoShellConfig config {};
  bool initialized { false };

  PanelRegistry panel_registry {};
  std::optional<ui::DemoShellUi> demo_shell_ui;
  std::vector<std::shared_ptr<DemoPanel>> pending_panels;

  // Services (owned by DemoShell)
  UiSettingsService ui_settings_service;
  RenderingSettingsService rendering_settings_service;
  CameraSettingsService camera_settings_service;
  ContentSettingsService content_settings_service;
  EnvironmentSettingsService environment_settings_service;
  ui::PostProcessSettingsService post_process_settings_service;
  GridSettingsService grid_settings_service;
  std::unique_ptr<internal::PostProcessConsoleBindings> post_process_console;

  std::unique_ptr<SkyboxService> skybox_service;
  observer_ptr<scene::Scene> skybox_service_scene;

  std::unique_ptr<ui::CameraRigController> camera_rig;
  observer_ptr<ui::ContentVm> content_vm;
  FileBrowserService file_browser_service;

  //! Declared after everything its handlers use, so it is destroyed first.
  std::unique_ptr<PanelCommands> panel_commands;

  //! The stage generation the services were last bound to.
  std::optional<std::uint64_t> synced_generation;
  observer_ptr<scene::Scene> synced_scene;

  bool logged_missing_env_vm_runtime_route { false };

  auto GetInputSystem() const -> observer_ptr<engine::InputSystem>
  {
    if (config.engine) {
      if (auto it = config.engine->GetModule<engine::InputSystem>()) {
        return observer_ptr { &it->get() };
      }
    }
    return nullptr;
  }

  auto TryGetScene() const -> observer_ptr<scene::Scene>
  {
    return config.stage ? config.stage->TryGetScene() : nullptr;
  }

  auto GetSkyboxService(observer_ptr<scene::Scene> scene)
    -> observer_ptr<SkyboxService>
  {
    if (skybox_service_scene != scene) {
      skybox_service.reset();
      skybox_service_scene = nullptr;

      if (scene) {
        if (auto asset_loader
          = config.engine ? config.engine->GetAssetLoader() : nullptr) {
          skybox_service = std::make_unique<SkyboxService>(asset_loader, scene);
          skybox_service_scene = scene;
        }
      }
    }
    return observer_ptr { skybox_service.get() };
  }
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
    impl_->panel_commands.reset();
    impl_->post_process_console.reset();
    impl_->post_process_settings_service.BindScene(nullptr);
    impl_->post_process_settings_service.BindVortexRenderer(nullptr);
    impl_->skybox_service.reset();
    impl_->skybox_service_scene = nullptr;
    impl_->panel_registry.ClearActivePanel();
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "DemoShell teardown threw: {}", ex.what());
  } catch (...) {
    LOG_F(ERROR, "DemoShell teardown threw an unknown exception");
  }
}

auto DemoShell::Initialize(const DemoShellConfig& config) -> bool
{
  impl_->config = config;
  if (config.scene_activation_policy
      != SceneActivationPolicy::kRestorePreferences
    && config.scene_activation_policy
      != SceneActivationPolicy::kExperimentOwned) {
    LOG_F(ERROR, "DemoShell: unknown scene activation policy");
    return false;
  }
  impl_->camera_settings_service.SetSceneActivationPolicy(
    config.scene_activation_policy);
  impl_->post_process_settings_service.SetSceneActivationPolicy(
    config.scene_activation_policy);
  if (config.scene_activation_policy
    == SceneActivationPolicy::kExperimentOwned) {
    impl_->environment_settings_service.SetRuntimeConfig(
      EnvironmentRuntimeConfig {
        .force_environment_override = false,
        .activation_policy = config.scene_activation_policy,
      });
  }
  LOG_F(INFO, "DemoShell scene activation: {}",
    to_string(config.scene_activation_policy));
  DCHECK_NOTNULL_F(impl_->config.engine,
    "DemoShell::Initialize requires a valid engine pointer");
  DCHECK_NOTNULL_F(
    impl_->config.stage, "DemoShell::Initialize requires the scene stage");

  return CompleteInitialization();
}

auto DemoShell::CompleteInitialization() -> bool
{
  if (impl_->initialized) {
    return true;
  }

  const auto settings = SettingsService::ForDemoApp();
  CHECK_NOTNULL_F(settings.get(),
    "DemoShell requires SettingsService before panel registration");

  impl_->file_browser_service.ConfigureContentRoots(
    impl_->config.content_roots);

  if (impl_->config.enable_camera_rig) {
    auto input_system = impl_->GetInputSystem();
    if (!input_system) {
      LOG_F(WARNING, "Input system required for camera rig");
      return false;
    }
    impl_->camera_rig = std::make_unique<ui::CameraRigController>();
    if (!impl_->camera_rig->Initialize(input_system)) {
      LOG_F(WARNING, "CameraRigController initialization failed");
      return false;
    }
    impl_->camera_settings_service.BindCameraRig(
      observer_ptr { impl_->camera_rig.get() });
  }
  impl_->grid_settings_service.BindCameraRig(
    observer_ptr { impl_->camera_rig.get() });
  impl_->post_process_settings_service.BindCameraSettings(
    observer_ptr { &impl_->camera_settings_service });

  impl_->panel_commands = std::make_unique<PanelCommands>(
    observer_ptr { &impl_->config.engine->GetCommands() },
    impl_->config.commands);

  impl_->demo_shell_ui.emplace(impl_->config.engine,
    observer_ptr { impl_->panel_commands.get() },
    observer_ptr { &impl_->panel_registry },
    observer_ptr { &impl_->ui_settings_service },
    observer_ptr { &impl_->rendering_settings_service },
    observer_ptr { &impl_->camera_settings_service },
    observer_ptr { &impl_->content_settings_service },
    observer_ptr { &impl_->environment_settings_service },
    observer_ptr { &impl_->post_process_settings_service },
    observer_ptr { &impl_->grid_settings_service },
    observer_ptr { impl_->camera_rig.get() },
    observer_ptr { &impl_->file_browser_service }, impl_->config.panel_config);

  impl_->content_vm = impl_->demo_shell_ui->GetContentVm();
  if (impl_->content_vm) {
    if (impl_->config.on_scene_load_requested) {
      impl_->content_vm->SetOnSceneLoadRequested(
        impl_->config.on_scene_load_requested);
    }
    if (impl_->config.on_scene_load_cancel_requested) {
      impl_->content_vm->SetOnSceneLoadCancelRequested(
        impl_->config.on_scene_load_cancel_requested);
    }
    if (impl_->config.on_clear_mounts) {
      impl_->content_vm->SetOnClearMounts(impl_->config.on_clear_mounts);
    }
    if (impl_->config.on_pak_mounted) {
      impl_->content_vm->SetOnPakMounted(impl_->config.on_pak_mounted);
    }
    if (impl_->config.on_loose_index_loaded) {
      impl_->content_vm->SetOnIndexLoaded(impl_->config.on_loose_index_loaded);
    }
    impl_->content_vm->SetOnGenerationPublished(
      impl_->config.on_generation_published);
    impl_->content_vm->RestorePersistedLibraryState();
  }

  for (auto& panel : impl_->pending_panels) {
    (void)impl_->demo_shell_ui->RegisterCustomPanel(panel);
  }
  impl_->pending_panels.clear();

  impl_->post_process_console
    = std::make_unique<internal::PostProcessConsoleBindings>(
      impl_->config.engine->GetConsole(), impl_->post_process_settings_service,
      [engine = impl_->config.engine] -> observer_ptr<vortex::Renderer> {
        const auto renderer = engine->GetModule<vortex::Renderer>();
        return renderer ? observer_ptr { &renderer->get() }
                        : observer_ptr<vortex::Renderer> {};
      });
  impl_->initialized = true;

  return true;
}

auto DemoShell::Update(const time::CanonicalDuration delta_time) -> void
{
  if (!impl_->initialized) {
    return;
  }

  if (impl_->content_vm) {
    impl_->content_vm->Update();
  }

  if (impl_->camera_rig) {
    impl_->camera_rig->Update(delta_time);
    impl_->camera_settings_service.PersistActiveCameraSettings();
  }
}

auto DemoShell::Draw(
  const observer_ptr<engine::FrameContext> fc, const float top_inset) -> void
{
  if (!impl_->initialized || !impl_->demo_shell_ui) {
    return;
  }
  impl_->demo_shell_ui->Draw(fc, top_inset);
}

auto DemoShell::OnFrameStart(const engine::FrameContext& context) -> void
{
  if (!impl_->initialized) {
    return;
  }

  // Bind a newly published scene before flushing its profile and exposure.
  SyncActiveScene();
  SyncRuntimeState();
  impl_->camera_settings_service.OnFrameStart(context);
  impl_->rendering_settings_service.OnFrameStart(context);
  impl_->environment_settings_service.OnFrameStart(context);
  impl_->post_process_settings_service.OnFrameStart();
  if (impl_->config.panel_config.ground_grid) {
    impl_->grid_settings_service.OnFrameStart(context);
  }
}

auto DemoShell::SyncActiveScene() -> void
{
  if (!impl_->initialized) {
    return;
  }
  const auto generation = impl_->config.stage->GetGeneration();
  if (impl_->synced_generation == generation) {
    return;
  }
  impl_->synced_generation = generation;
  const auto scene = impl_->TryGetScene();
  if (scene == impl_->synced_scene) {
    return;
  }
  impl_->synced_scene = scene;
  if (scene) {
    OnSceneActivated(*scene);
  } else {
    OnSceneCleared();
  }
}

auto DemoShell::OnMainViewReady(const ViewId view_id,
  const scene::SceneNode& camera, const ViewPort& viewport) -> void
{
  if (!impl_->initialized) {
    return;
  }

  SyncRuntimeState();
  impl_->camera_settings_service.OnMainViewReady(view_id, camera, viewport);
  impl_->rendering_settings_service.OnMainViewReady(view_id, camera, viewport);
  impl_->environment_settings_service.OnMainViewReady(
    view_id, camera, viewport);
  impl_->post_process_settings_service.BindMainView(view_id);
  if (impl_->config.panel_config.ground_grid) {
    impl_->grid_settings_service.OnMainViewReady(view_id, camera, viewport);
  }
}

auto DemoShell::RegisterPanel(std::shared_ptr<DemoPanel> panel) -> bool
{
  if (!panel) {
    LOG_F(WARNING, "Cannot register null panel");
    return false;
  }

  if (!impl_->initialized) {
    const auto name = std::string_view(panel->GetName());
    if (name.empty()) {
      LOG_F(WARNING, "Cannot register panel with empty name");
      return false;
    }
    const auto duplicate = std::ranges::any_of(impl_->pending_panels,
      [&](const std::shared_ptr<DemoPanel>& existing) -> bool {
        return existing && existing->GetName() == name;
      });
    if (duplicate) {
      LOG_F(WARNING, "Duplicate pending panel '{}'", name);
      return false;
    }
    impl_->pending_panels.push_back(std::move(panel));
    return true;
  }

  if (!impl_->demo_shell_ui) {
    LOG_F(WARNING, "UI not available for panel registration");
    return false;
  }
  return impl_->demo_shell_ui->RegisterCustomPanel(std::move(panel));
}

auto DemoShell::GetPanelCommands() const -> observer_ptr<PanelCommands>
{
  return observer_ptr { impl_->panel_commands.get() };
}

auto DemoShell::TryGetScene() const -> observer_ptr<scene::Scene>
{
  return impl_->TryGetScene();
}

auto DemoShell::CancelContentImport() -> void
{
  if (impl_->content_vm) {
    impl_->content_vm->CancelActiveImport();
  }
}

auto DemoShell::GetCameraSettingsService() -> CameraSettingsService&
{
  return impl_->camera_settings_service;
}

auto DemoShell::GetFileBrowserService() const -> FileBrowserService&
{
  return impl_->file_browser_service;
}

auto DemoShell::GetCameraRig() const -> observer_ptr<ui::CameraRigController>
{
  return observer_ptr { impl_->camera_rig.get() };
}

auto DemoShell::GetContentVm() const -> observer_ptr<ui::ContentVm>
{
  if (!impl_) {
    return observer_ptr<ui::ContentVm> { nullptr };
  }
  return impl_->content_vm;
}

auto DemoShell::ReapplyPostProcessSettingsToScene() -> void
{
  if (!impl_ || !impl_->initialized) {
    return;
  }

  const auto scene = TryGetScene();
  if (!scene) {
    return;
  }

  if (impl_->config.scene_activation_policy
    == SceneActivationPolicy::kExperimentOwned) {
    impl_->post_process_settings_service.OnFrameStart();
  } else {
    impl_->post_process_settings_service.BindScene(scene);
  }
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

auto DemoShell::IsHeightFogPassRequested() const -> bool
{
  return impl_->environment_settings_service.GetHeightFogPassRequested();
}

auto DemoShell::IsLocalFogPassRequested() const -> bool
{
  return impl_->environment_settings_service.GetLocalFogPassRequested();
}

auto DemoShell::SyncRuntimeState() -> void
{
  if (!impl_->initialized) {
    return;
  }

  EnvironmentRuntimeConfig runtime_config {};
  runtime_config.scene = TryGetScene();
  runtime_config.skybox_service = impl_->GetSkyboxService(runtime_config.scene);
  runtime_config.activation_policy = impl_->config.scene_activation_policy;
  runtime_config.force_environment_override
    = impl_->config.force_environment_override;
  runtime_config.restore_environment_profile
    = impl_->config.restore_environment_profile;
  runtime_config.initial_environment_profile
    = impl_->config.initial_environment_profile;
  runtime_config.startup_skybox_path = impl_->config.startup_skybox_path;
  runtime_config.initial_preview_sun_enabled
    = impl_->config.initial_preview_sun_enabled;
  runtime_config.preview_scene_ready
    = !impl_->config.preview_scene_ready || impl_->config.preview_scene_ready();
  if (auto renderer_ref = impl_->config.engine->GetModule<vortex::Renderer>()) {
    runtime_config.renderer = observer_ptr { &renderer_ref->get() };
    impl_->post_process_settings_service.BindVortexRenderer(
      runtime_config.renderer);
  }
  if (const auto env_vm = impl_->demo_shell_ui
      ? impl_->demo_shell_ui->GetEnvironmentVm()
      : nullptr) {
    env_vm->SetRuntimeConfig(runtime_config);
    return;
  }
  // Without the environment panel the service takes the configuration.
  impl_->environment_settings_service.SetRuntimeConfig(runtime_config);
  if (!impl_->logged_missing_env_vm_runtime_route) {
    LOG_F(INFO,
      "DemoShell: no environment panel; the environment service takes the "
      "runtime configuration directly");
    impl_->logged_missing_env_vm_runtime_route = true;
  }
}

auto DemoShell::OnSceneActivated(scene::Scene& scene) -> void
{
  impl_->camera_settings_service.OnSceneActivated(scene);
  impl_->rendering_settings_service.OnSceneActivated(scene);
  impl_->environment_settings_service.OnSceneActivated(scene);
  impl_->post_process_settings_service.BindScene(observer_ptr { &scene });
  if (impl_->config.panel_config.ground_grid) {
    impl_->grid_settings_service.OnSceneActivated(scene);
  }
}

auto DemoShell::OnSceneCleared() -> void
{
  impl_->post_process_settings_service.BindScene(nullptr);
}

} // namespace oxygen::examples
