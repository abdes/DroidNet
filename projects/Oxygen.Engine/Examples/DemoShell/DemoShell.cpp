//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/ModuleEvent.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Internal/SceneControlBlock.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/SkyboxService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/EnvironmentVm.h"
#include "DemoShell/UI/LightCullingVm.h"
#include "DemoShell/UI/RenderingVm.h"

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
  LightCullingSettingsService light_culling_settings_service;
  CameraSettingsService camera_settings_service;
  ContentSettingsService content_settings_service;
  EnvironmentSettingsService environment_settings_service;
  ui::PostProcessSettingsService post_process_settings_service;

  // Panels still managed by DemoShell (non-MVVM panels)
  std::unique_ptr<SkyboxService> skybox_service;
  observer_ptr<scene::Scene> skybox_service_scene;

  std::unique_ptr<ui::CameraRigController> camera_rig;
  observer_ptr<ui::ContentVm> content_vm;
  CameraLifecycleService camera_lifecycle;
  FileBrowserService file_browser_service;
  internal::SceneControlBlock scene_control;

  // Track the pipeline we've initialized the services with
  observer_ptr<RenderingPipeline> bound_pipeline;

  bool pending_init { false };

  mutable std::once_flag input_system_flag;
  mutable observer_ptr<engine::InputSystem> input_system;
  mutable observer_ptr<engine::Renderer> renderer;
  AsyncEngine::ModuleSubscription renderer_subscription;

  auto GetInputSystem() const -> observer_ptr<engine::InputSystem>
  {
    std::call_once(input_system_flag, [this] {
      if (config.engine) {
        if (auto it = config.engine->GetModule<engine::InputSystem>()) {
          input_system = observer_ptr { &it->get() };
        }
      }
    });
    return input_system;
  }

  auto GetRenderer() const -> observer_ptr<engine::Renderer>
  {
    DCHECK_NOTNULL_F(renderer, "Renderer module not attached");
    return renderer;
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
    impl_->panel_registry.ClearActivePanel();
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "OnUnloaded threw: {}", ex.what());
  } catch (...) {
    LOG_F(ERROR, "OnUnloaded threw unknown exception");
  }
}

auto DemoShell::Initialize(const DemoShellConfig& config) -> bool
{
  impl_->config = config;
  DCHECK_NOTNULL_F(impl_->config.engine,
    "DemoShell::Initialize requires a valid engine pointer");
  impl_->pending_init = true;
  impl_->renderer_subscription = impl_->config.engine->SubscribeModuleAttached(
    [this](const engine::ModuleEvent& event) {
      if (event.type_id != engine::Renderer::ClassTypeId()) {
        return;
      }
      impl_->renderer = observer_ptr {
        // NOLINTNEXTLINE(*-static-cast-downcast)
        static_cast<engine::Renderer*>(event.module.get()),
      };
      if (!impl_->pending_init) {
        return;
      }
      impl_->pending_init = false;
      (void)CompleteInitialization();
    },
    true);

  return true;
}

auto DemoShell::CompleteInitialization() -> bool
{
  if (impl_->initialized) {
    return true;
  }

  const auto settings = SettingsService::Default();
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
    impl_->camera_lifecycle.BindCameraRig(
      observer_ptr { impl_->camera_rig.get() });
  }
  impl_->camera_lifecycle.SetScene(impl_->scene_control.TryGetScene());
  impl_->post_process_settings_service.BindCameraLifecycle(
    observer_ptr { &impl_->camera_lifecycle });

  // Create DemoShellUi with all services
  impl_->demo_shell_ui.emplace(impl_->config.engine,
    observer_ptr { &impl_->panel_registry },
    observer_ptr { &impl_->camera_lifecycle },
    observer_ptr { &impl_->ui_settings_service },
    observer_ptr { &impl_->rendering_settings_service },
    observer_ptr { &impl_->light_culling_settings_service },
    observer_ptr { &impl_->camera_settings_service },
    observer_ptr { &impl_->content_settings_service },
    observer_ptr { &impl_->environment_settings_service },
    observer_ptr { &impl_->post_process_settings_service },
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
    if (impl_->config.on_force_trim) {
      impl_->content_vm->SetOnForceTrim(impl_->config.on_force_trim);
    }
    if (impl_->config.on_pak_mounted) {
      impl_->content_vm->SetOnPakMounted(impl_->config.on_pak_mounted);
    }
    if (impl_->config.on_loose_index_loaded) {
      impl_->content_vm->SetOnIndexLoaded(impl_->config.on_loose_index_loaded);
    }
  }

  if (impl_->demo_shell_ui && !impl_->pending_panels.empty()) {
    for (auto& panel : impl_->pending_panels) {
      (void)impl_->demo_shell_ui->RegisterCustomPanel(panel);
    }
    impl_->pending_panels.clear();
  }
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
    DLOG_F(WARNING, "Zero delta time, skipping panel updates");
    return;
  }

  if (impl_->content_vm) {
    impl_->content_vm->Update();
  }

  if (auto camera = impl_->camera_lifecycle.GetActiveCamera();
    camera.IsAlive()) {
    impl_->camera_settings_service.SetActiveCameraId(camera.GetName());
  } else {
    impl_->camera_settings_service.SetActiveCameraId({});
  }

  if (impl_->camera_rig) {
    impl_->camera_rig->Update(delta_time);
    impl_->camera_lifecycle.PersistActiveCameraSettings();
  }
}

auto DemoShell::Draw(engine::FrameContext& fc) -> void
{
  if (!impl_->initialized) {
    return;
  }

  if (impl_->demo_shell_ui) {
    impl_->demo_shell_ui->Draw(fc);
  }
}

auto DemoShell::SyncPanels() -> void
{
  if (!impl_->initialized) {
    return;
  }

  UpdatePanels();
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
    const auto duplicate = std::ranges::any_of(
      impl_->pending_panels, [&](const std::shared_ptr<DemoPanel>& existing) {
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
  LOG_SCOPE_FUNCTION(INFO);
  impl_->camera_lifecycle.SetActiveCamera(std::move(camera));

  if (impl_->camera_rig) {
    impl_->camera_rig->SetActiveCamera(
      observer_ptr { &impl_->camera_lifecycle.GetActiveCamera() });
  }
}
auto DemoShell::GetSkyboxService() -> observer_ptr<SkyboxService>
{
  return impl_->GetSkyboxService(TryGetScene());
}

auto DemoShell::CancelContentImport() -> void
{
  if (impl_->content_vm) {
    impl_->content_vm->CancelActiveImport();
  }
}

auto DemoShell::GetCameraLifecycle() -> CameraLifecycleService&
{
  return impl_->camera_lifecycle;
}

auto DemoShell::GetFileBrowserService() const -> FileBrowserService&
{
  return impl_->file_browser_service;
}

auto DemoShell::GetCameraRig() const -> observer_ptr<ui::CameraRigController>
{
  return observer_ptr { impl_->camera_rig.get() };
}

auto DemoShell::GetRenderingViewMode() const -> RenderMode
{
  return impl_->rendering_settings_service.GetRenderMode();
}

auto DemoShell::GetContentVm() const -> observer_ptr<ui::ContentVm>
{
  if (!impl_) {
    return observer_ptr<ui::ContentVm> { nullptr };
  }
  return impl_->content_vm;
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
  return impl_->rendering_settings_service.GetDebugMode();
}

auto DemoShell::GetRenderingWireframeColor() const -> graphics::Color
{
  const auto color = impl_->rendering_settings_service.GetWireframeColor();
  LOG_F(INFO, "DemoShell: GetRenderingWireframeColor ({}, {}, {}, {})",
    color.r, color.g, color.b, color.a);
  return color;
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

auto DemoShell::UpdatePanels() -> void
{
  auto pipeline = impl_->config.get_active_pipeline
    ? impl_->config.get_active_pipeline()
    : observer_ptr<RenderingPipeline> { nullptr };

  if (pipeline && impl_->demo_shell_ui) {
    // If the pipeline has changed, re-initialize services
    if (impl_->bound_pipeline != pipeline) {
      impl_->rendering_settings_service.Initialize(pipeline);
      impl_->light_culling_settings_service.Initialize(pipeline);
      impl_->post_process_settings_service.Initialize(pipeline);
      impl_->bound_pipeline = pipeline;
    }

    if (impl_->config.panel_config.rendering) {
      impl_->demo_shell_ui->EnsureRenderingPanelReady(*pipeline);
    }
    if (impl_->config.panel_config.lighting) {
      impl_->demo_shell_ui->EnsureLightingPanelReady(*pipeline);
    }
  }

  EnvironmentRuntimeConfig runtime_config {};
  runtime_config.scene = TryGetScene();
  runtime_config.skybox_service = impl_->GetSkyboxService(runtime_config.scene);
  const auto renderer = impl_->GetRenderer();
  runtime_config.renderer = renderer;
  runtime_config.on_atmosphere_params_changed = [renderer] {
    LOG_F(INFO, "Atmosphere parameters changed, LUTs will regenerate");
    if (renderer) {
      if (auto lut_mgr = renderer->GetSkyAtmosphereLutManager()) {
        lut_mgr->MarkDirty();
      }
    }
  };
  runtime_config.on_exposure_changed
    = [] { LOG_F(INFO, "Exposure settings changed"); };
  impl_->environment_settings_service.SetRuntimeConfig(runtime_config);
  if (runtime_config.scene
    && impl_->environment_settings_service.HasPendingChanges()) {
    impl_->environment_settings_service.ApplyPendingChanges();
  }

  if (impl_->demo_shell_ui) {
    if (const auto env_vm = impl_->demo_shell_ui->GetEnvironmentVm()) {
      env_vm->SetRuntimeConfig(runtime_config);
    }
  }
}

} // namespace oxygen::examples
