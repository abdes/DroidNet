//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/LightCullingSettingsService.h"
#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/AxesWidget.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraVm.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/LightCullingDebugPanel.h"
#include "DemoShell/UI/LightCullingVm.h"
#include "DemoShell/UI/PanelSideBar.h"
#include "DemoShell/UI/RenderingPanel.h"
#include "DemoShell/UI/RenderingVm.h"
#include "DemoShell/UI/SidePanel.h"
#include "DemoShell/UI/StatsOverlay.h"
#include "DemoShell/UI/UiSettingsPanel.h"
#include "DemoShell/UI/UiSettingsVm.h"

namespace oxygen::examples::ui {

struct DemoShellUi::Impl {
  observer_ptr<PanelRegistry> panel_registry {};
  observer_ptr<RenderingSettingsService> rendering_settings_service {};
  observer_ptr<LightCullingSettingsService> light_culling_settings_service {};
  observer_ptr<CameraSettingsService> camera_settings_service {};
  DemoShellPanelConfig panel_config {};

  // UI Settings (always created)
  UiSettingsVm ui_settings_vm;
  observer_ptr<UiSettingsVm> ui_settings_vm_ptr { &ui_settings_vm };

  // Core UI components
  PanelSideBar side_bar;
  SidePanel side_panel;
  AxesWidget axes_widget;
  StatsOverlay stats_overlay;
  std::shared_ptr<UiSettingsPanel> settings_panel {};

  // Rendering panel (created lazily when pass config is available)
  std::unique_ptr<RenderingVm> rendering_vm {};
  std::shared_ptr<RenderingPanel> rendering_panel {};

  // Lighting panel (created lazily when pass configs are available)
  std::unique_ptr<LightCullingVm> light_culling_vm {};
  std::shared_ptr<LightingPanel> lighting_panel {};

  // Camera panel
  std::unique_ptr<CameraVm> camera_vm {};
  std::shared_ptr<CameraControlPanel> camera_panel {};

  // Content panel
  std::shared_ptr<ContentVm> content_vm {};
  std::shared_ptr<ContentLoaderPanel> content_panel {};

  Impl(observer_ptr<PanelRegistry> registry,
    observer_ptr<CameraLifecycleService> camera_lifecycle,
    observer_ptr<UiSettingsService> ui_settings_service,
    observer_ptr<RenderingSettingsService> rendering_settings,
    observer_ptr<LightCullingSettingsService> light_culling_settings,
    observer_ptr<CameraSettingsService> camera_settings,
    observer_ptr<ContentSettingsService> content_settings,
    observer_ptr<CameraRigController> camera_rig,
    observer_ptr<FileBrowserService> file_browser,
    const DemoShellPanelConfig& panel_config_in)
    : panel_registry(registry)
    , rendering_settings_service(rendering_settings)
    , light_culling_settings_service(light_culling_settings)
    , camera_settings_service(camera_settings)
    , panel_config(panel_config_in)
    , ui_settings_vm(ui_settings_service, camera_lifecycle)
    , side_bar(panel_registry, ui_settings_vm_ptr)
    , side_panel(panel_registry)
    , axes_widget(ui_settings_vm_ptr)
    , stats_overlay(ui_settings_vm_ptr)
  {
    DCHECK_NOTNULL_F(panel_registry, "expecting valid PanelRegistry");
    DCHECK_NOTNULL_F(ui_settings_service, "expecting valid UiSettingsService");

    // Create Camera VM and Panel
    if (panel_config.camera_controls && camera_settings && camera_rig) {
      camera_vm = std::make_unique<CameraVm>(
        camera_settings, camera_lifecycle, camera_rig);
      camera_panel = std::make_shared<CameraControlPanel>(
        observer_ptr { camera_vm.get() });
      if (panel_registry->RegisterPanel(camera_panel)) {
        LOG_F(INFO, "DemoShellUi: registered Camera panel");
      }
    }

    // Create Content VM and Panel
    if (panel_config.content_loader && content_settings && file_browser) {
      content_vm = std::make_shared<ContentVm>(content_settings, file_browser);
      content_panel = std::make_shared<ContentLoaderPanel>();
      content_panel->Initialize(observer_ptr { content_vm.get() });
      if (panel_registry->RegisterPanel(content_panel)) {
        LOG_F(INFO, "DemoShellUi: registered Content panel");
      }
    }

    settings_panel = std::make_shared<UiSettingsPanel>(ui_settings_vm_ptr);
    const auto result = panel_registry->RegisterPanel(settings_panel);
    if (!result) {
      LOG_F(WARNING, "DemoShellUi: failed to register Settings panel");
    }
  }
};

DemoShellUi::DemoShellUi(observer_ptr<PanelRegistry> panel_registry,
  observer_ptr<CameraLifecycleService> camera_lifecycle,
  observer_ptr<UiSettingsService> ui_settings_service,
  observer_ptr<RenderingSettingsService> rendering_settings_service,
  observer_ptr<LightCullingSettingsService> light_culling_settings_service,
  observer_ptr<CameraSettingsService> camera_settings_service,
  observer_ptr<ContentSettingsService> content_settings_service,
  observer_ptr<CameraRigController> camera_rig,
  observer_ptr<FileBrowserService> file_browser_service,
  const DemoShellPanelConfig& panel_config)
  : impl_(std::make_unique<Impl>(panel_registry, camera_lifecycle,
      ui_settings_service, rendering_settings_service,
      light_culling_settings_service, camera_settings_service,
      content_settings_service, camera_rig, file_browser_service, panel_config))
{
}

DemoShellUi::~DemoShellUi() = default;

auto DemoShellUi::Draw() -> void
{
  impl_->side_bar.Draw();
  impl_->side_panel.Draw(impl_->side_bar.GetWidth());

  // Settings now flow through UiSettingsVm instead of view-owned state.
  impl_->axes_widget.Draw(impl_->ui_settings_vm.GetActiveCamera());
  impl_->stats_overlay.Draw();
}

auto DemoShellUi::EnsureRenderingPanelReady(const PassConfigRefs& refs) -> void
{
  if (!impl_->panel_config.rendering) {
    return;
  }
  if (impl_->rendering_panel) {
    // Already created, just update pass config if needed
    if (impl_->rendering_vm) {
      impl_->rendering_vm->SetPassConfig(refs.shader_pass_config);
    }
    return;
  }

  if (!refs.shader_pass_config) {
    return; // Pass config not yet available
  }

  if (!impl_->rendering_settings_service) {
    LOG_F(WARNING,
      "DemoShellUi: cannot create RenderingPanel without "
      "RenderingSettingsService");
    return;
  }

  // Create the ViewModel
  impl_->rendering_vm = std::make_unique<RenderingVm>(
    impl_->rendering_settings_service, refs.shader_pass_config);

  // Create the Panel with the ViewModel
  impl_->rendering_panel = std::make_shared<RenderingPanel>(
    observer_ptr { impl_->rendering_vm.get() });

  // Register with panel registry
  if (impl_->panel_registry->RegisterPanel(impl_->rendering_panel)) {
    LOG_F(INFO, "DemoShellUi: registered Rendering panel");
  } else {
    LOG_F(WARNING, "DemoShellUi: failed to register Rendering panel");
  }
}

auto DemoShellUi::EnsureLightingPanelReady(const PassConfigRefs& refs) -> void
{
  if (!impl_->panel_config.lighting) {
    return;
  }
  if (impl_->lighting_panel) {
    // Already created, just update pass configs if needed
    if (impl_->light_culling_vm) {
      impl_->light_culling_vm->SetPassConfigs(
        refs.shader_pass_config, refs.light_culling_pass_config);
    }
    return;
  }

  if (!refs.shader_pass_config || !refs.light_culling_pass_config) {
    return; // Pass configs not yet available
  }

  if (!impl_->light_culling_settings_service) {
    LOG_F(WARNING,
      "DemoShellUi: cannot create LightingPanel without "
      "LightCullingSettingsService");
    return;
  }

  // Create the ViewModel
  impl_->light_culling_vm = std::make_unique<LightCullingVm>(
    impl_->light_culling_settings_service, refs.shader_pass_config,
    refs.light_culling_pass_config, refs.on_cluster_mode_changed);

  // Create the Panel with the ViewModel
  impl_->lighting_panel = std::make_shared<LightingPanel>(
    observer_ptr { impl_->light_culling_vm.get() });

  // Register with panel registry
  if (impl_->panel_registry->RegisterPanel(impl_->lighting_panel)) {
    LOG_F(INFO, "DemoShellUi: registered Lighting panel");
  } else {
    LOG_F(WARNING, "DemoShellUi: failed to register Lighting panel");
  }
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

} // namespace oxygen::examples::ui
