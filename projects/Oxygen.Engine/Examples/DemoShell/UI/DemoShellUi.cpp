//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <memory>
#include <string_view>
#include <utility>

#include "DemoShell/DemoShell.h"
#include "DemoShell/PanelCommands.h"
#include "DemoShell/PanelRegistry.h"
#include "DemoShell/Services/CameraSettingsService.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/GridSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/AxesWidget.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraVm.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/DiagnosticsPanel.h"
#include "DemoShell/UI/DiagnosticsVm.h"
#include "DemoShell/UI/EnvironmentDebugPanel.h"
#include "DemoShell/UI/EnvironmentVm.h"
#include "DemoShell/UI/GridPanel.h"
#include "DemoShell/UI/GridVm.h"
#include "DemoShell/UI/PanelSideBar.h"
#include "DemoShell/UI/PostProcessPanel.h"
#include "DemoShell/UI/PostProcessVm.h"
#include "DemoShell/UI/SidePanel.h"
#include "DemoShell/UI/UiSettingsVm.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::examples::ui {

struct DemoShellUi::Impl {
  observer_ptr<IAsyncEngine> engine;
  observer_ptr<PanelCommands> commands;
  observer_ptr<PanelRegistry> panel_registry;
  observer_ptr<RenderingSettingsService> rendering_settings_service;
  observer_ptr<EnvironmentSettingsService> environment_settings_service;
  observer_ptr<PostProcessSettingsService> post_process_settings_service;
  observer_ptr<GridSettingsService> grid_settings_service;
  observer_ptr<FileBrowserService> file_browser_service;
  DemoShellPanelConfig panel_config;

  UiSettingsVm ui_settings_vm;
  observer_ptr<UiSettingsVm> ui_settings_vm_ptr { &ui_settings_vm };

  PanelSideBar side_bar;
  SidePanel side_panel;
  AxesWidget axes_widget;

  // Created once the renderer is attached.
  std::unique_ptr<DiagnosticsVm> diagnostics_vm;
  std::shared_ptr<DiagnosticsPanel> diagnostics_panel;

  std::unique_ptr<CameraVm> camera_vm;
  std::shared_ptr<CameraControlPanel> camera_panel;

  std::shared_ptr<ContentVm> content_vm;
  std::shared_ptr<ContentLoaderPanel> content_panel;

  std::unique_ptr<EnvironmentVm> environment_vm;
  std::shared_ptr<EnvironmentDebugPanel> environment_panel;

  std::unique_ptr<PostProcessVm> post_process_vm;
  std::shared_ptr<PostProcessPanel> post_process_panel;

  std::unique_ptr<GridVm> grid_vm;
  std::shared_ptr<GridPanel> grid_panel;

  Impl(observer_ptr<IAsyncEngine> engine_ptr,
    observer_ptr<PanelCommands> commands_ptr,
    observer_ptr<PanelRegistry> registry,
    observer_ptr<UiSettingsService> ui_settings_service,
    observer_ptr<RenderingSettingsService> rendering_settings,
    observer_ptr<CameraSettingsService> camera_settings,
    observer_ptr<ContentSettingsService> content_settings,
    observer_ptr<EnvironmentSettingsService> environment_settings,
    observer_ptr<PostProcessSettingsService> post_process_settings,
    observer_ptr<GridSettingsService> grid_settings,
    observer_ptr<CameraRigController> camera_rig,
    observer_ptr<FileBrowserService> file_browser,
    const DemoShellPanelConfig& panel_config_in)
    : engine(engine_ptr)
    , commands(commands_ptr)
    , panel_registry(registry)
    , rendering_settings_service(rendering_settings)
    , environment_settings_service(environment_settings)
    , post_process_settings_service(post_process_settings)
    , grid_settings_service(grid_settings)
    , file_browser_service(file_browser)
    , panel_config(panel_config_in)
    , ui_settings_vm(ui_settings_service, camera_settings)
    , side_bar(panel_registry, ui_settings_vm_ptr)
    , side_panel(panel_registry)
    , axes_widget(ui_settings_vm_ptr)
  {
    DCHECK_NOTNULL_F(commands, "expecting valid PanelCommands");
    DCHECK_NOTNULL_F(panel_registry, "expecting valid PanelRegistry");
    DCHECK_NOTNULL_F(ui_settings_service, "expecting valid UiSettingsService");

    if (panel_config.camera_controls && camera_settings && camera_rig) {
      camera_vm = std::make_unique<CameraVm>(camera_settings, camera_rig);
      camera_panel = std::make_shared<CameraControlPanel>(
        observer_ptr { camera_vm.get() }, ui_settings_vm_ptr, commands);
      Register(camera_panel);
    }

    if (panel_config.content_loader && content_settings && file_browser) {
      const auto asset_loader = engine ? engine->GetAssetLoader()
                                       : observer_ptr<content::IAssetLoader> {};
      content_vm = std::make_shared<ContentVm>(
        content_settings, file_browser, asset_loader);
      content_panel = std::make_shared<ContentLoaderPanel>(
        observer_ptr { content_vm.get() }, commands);
      Register(content_panel);
    }

    if (panel_config.environment && environment_settings) {
      environment_vm = std::make_unique<EnvironmentVm>(
        environment_settings, post_process_settings, file_browser);
      environment_panel = std::make_shared<EnvironmentDebugPanel>(
        observer_ptr { environment_vm.get() }, commands);
      Register(environment_panel);
    }

    if (panel_config.post_process && post_process_settings) {
      post_process_vm = std::make_unique<PostProcessVm>(post_process_settings);
      post_process_panel = std::make_shared<PostProcessPanel>(
        observer_ptr { post_process_vm.get() }, commands);
      Register(post_process_panel);
    }

    if (panel_config.ground_grid && grid_settings) {
      grid_vm = std::make_unique<GridVm>(grid_settings);
      grid_panel
        = std::make_shared<GridPanel>(observer_ptr { grid_vm.get() }, commands);
      Register(grid_panel);
    }
  }

  auto Register(const std::shared_ptr<DemoPanel>& panel) const -> void
  {
    if (!panel_registry->RegisterPanel(panel)) {
      LOG_F(WARNING, "Failed to register the {} panel", panel->GetName());
    }
  }

  //! Bind the renderer-backed services, and create the diagnostics panel the
  //! first time the renderer is there.
  auto BindRenderer(vortex::Renderer& renderer) -> void
  {
    const auto bound = observer_ptr { &renderer };
    if (post_process_settings_service) {
      post_process_settings_service->BindVortexRenderer(bound);
    }
    if (panel_config.ground_grid && grid_settings_service) {
      grid_settings_service->BindVortexRenderer(bound);
    }
    if (!panel_config.diagnostics || diagnostics_panel
      || !rendering_settings_service) {
      return;
    }
    rendering_settings_service->BindVortexRenderer(bound);
    diagnostics_vm = std::make_unique<DiagnosticsVm>(
      rendering_settings_service, environment_settings_service);
    diagnostics_panel = std::make_shared<DiagnosticsPanel>(
      observer_ptr { diagnostics_vm.get() }, commands);
    Register(diagnostics_panel);
  }
};

DemoShellUi::DemoShellUi(observer_ptr<IAsyncEngine> engine,
  observer_ptr<PanelCommands> commands,
  observer_ptr<PanelRegistry> panel_registry,
  observer_ptr<UiSettingsService> ui_settings_service,
  observer_ptr<RenderingSettingsService> rendering_settings_service,
  observer_ptr<CameraSettingsService> camera_settings_service,
  observer_ptr<ContentSettingsService> content_settings_service,
  observer_ptr<EnvironmentSettingsService> environment_settings_service,
  observer_ptr<PostProcessSettingsService> post_process_settings_service,
  observer_ptr<GridSettingsService> grid_settings_service,
  observer_ptr<CameraRigController> camera_rig,
  observer_ptr<FileBrowserService> file_browser_service,
  const DemoShellPanelConfig& panel_config)
  : impl_(std::make_unique<Impl>(engine, commands, panel_registry,
      ui_settings_service, rendering_settings_service, camera_settings_service,
      content_settings_service, environment_settings_service,
      post_process_settings_service, grid_settings_service, camera_rig,
      file_browser_service, panel_config))
{
}

DemoShellUi::~DemoShellUi() = default;

auto DemoShellUi::Draw(
  observer_ptr<engine::FrameContext> /*fc*/, const float top_inset) -> void
{
  if (!impl_->engine) {
    return;
  }
  const auto renderer = impl_->engine->GetModule<vortex::Renderer>();
  if (!renderer) {
    return;
  }
  impl_->BindRenderer(renderer->get());

  impl_->side_bar.Draw(top_inset);
  impl_->side_panel.Draw(impl_->side_bar.GetWidth(), top_inset);
  impl_->axes_widget.Draw(impl_->ui_settings_vm.GetActiveCamera());

  if (impl_->file_browser_service) {
    impl_->file_browser_service->UpdateAndDraw();
  }
}

auto DemoShellUi::RegisterCustomPanel(std::shared_ptr<DemoPanel> panel) -> bool
{
  if (!panel) {
    LOG_F(WARNING, "Cannot register null panel");
    return false;
  }

  const auto name = std::string_view(panel->GetName());
  if (name.empty()) {
    LOG_F(WARNING, "Cannot register panel with empty name");
    return false;
  }

  try {
    if (!impl_->panel_registry->RegisterPanel(std::move(panel))) {
      LOG_F(WARNING, "Failed to register panel '{}'", name);
      return false;
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Failed to register panel '{}': {}", name, ex.what());
    return false;
  }

  return true;
}

auto DemoShellUi::GetContentVm() const -> observer_ptr<ContentVm>
{
  return observer_ptr { impl_->content_vm.get() };
}

auto DemoShellUi::GetEnvironmentVm() const -> observer_ptr<EnvironmentVm>
{
  return observer_ptr { impl_->environment_vm.get() };
}

} // namespace oxygen::examples::ui
