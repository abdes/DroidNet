//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen {
class IAsyncEngine;
namespace engine {
  class FrameContext;
} // namespace engine
} // namespace oxygen

namespace oxygen::examples {

class PanelCommands;
class PanelRegistry;
class DemoPanel;
class UiSettingsService;
class RenderingSettingsService;
class CameraSettingsService;
class ContentSettingsService;
class EnvironmentSettingsService;
class GridSettingsService;
class FileBrowserService;
struct DemoShellPanelConfig;

namespace ui {

  class CameraVm;
  class ContentVm;
  class EnvironmentVm;
  class PostProcessSettingsService;
  class CameraRigController;

  //! The side bar, the side panel, the axes widget and the standard panels.
  class DemoShellUi {
  public:
    DemoShellUi(observer_ptr<IAsyncEngine> engine,
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
      const DemoShellPanelConfig& panel_config);
    ~DemoShellUi();

    OXYGEN_MAKE_NON_COPYABLE(DemoShellUi)
    OXYGEN_DEFAULT_MOVABLE(DemoShellUi)

    //! Draw the side bar and the active panel below `top_inset`.
    auto Draw(observer_ptr<engine::FrameContext> fc, float top_inset) -> void;
    auto RegisterCustomPanel(std::shared_ptr<DemoPanel> panel) -> bool;

    [[nodiscard]] auto GetContentVm() const -> observer_ptr<ContentVm>;
    [[nodiscard]] auto GetEnvironmentVm() const -> observer_ptr<EnvironmentVm>;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace ui
} // namespace oxygen::examples
