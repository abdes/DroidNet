//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::engine {
struct ShaderPassConfig;
struct LightCullingPassConfig;
} // namespace oxygen::engine

namespace oxygen::examples {

class PanelRegistry;
class UiSettingsService;
class CameraLifecycleService;
class RenderingSettingsService;
class LightCullingSettingsService;
class CameraSettingsService;

namespace ui {

  class RenderingVm;
  class LightCullingVm;
  class CameraVm;
  class CameraRigController;
  enum class RenderingViewMode;

  //! Configuration for pass configs needed by rendering/lighting panels.
  struct PassConfigRefs {
    observer_ptr<engine::ShaderPassConfig> shader_pass_config { nullptr };
    observer_ptr<engine::LightCullingPassConfig> light_culling_pass_config {
      nullptr
    };
    std::function<void()> on_cluster_mode_changed {};
  };

  //! UI shell hosting the side bar and side panel.
  /*!
   Provides a reusable UI layout for demos, consisting of a left-docked
   `PanelSideBar` and a `SidePanel` hosting a single active panel.

   Also owns the ViewModels for rendering and lighting panels, creating them
   lazily when the pass configs become available.
  */
  class DemoShellUi {
  public:
    DemoShellUi(observer_ptr<PanelRegistry> panel_registry,
      observer_ptr<CameraLifecycleService> camera_lifecycle,
      observer_ptr<UiSettingsService> ui_settings_service,
      observer_ptr<RenderingSettingsService> rendering_settings_service,
      observer_ptr<LightCullingSettingsService> light_culling_settings_service,
      observer_ptr<CameraSettingsService> camera_settings_service,
      observer_ptr<CameraRigController> camera_rig);
    ~DemoShellUi();

    OXYGEN_MAKE_NON_COPYABLE(DemoShellUi)
    OXYGEN_DEFAULT_MOVABLE(DemoShellUi)

    //! Draws the side bar and side panel.
    auto Draw() -> void;

    //! Ensures rendering panel is created when pass config is available.
    auto EnsureRenderingPanelReady(const PassConfigRefs& refs) -> void;

    //! Ensures lighting panel is created when pass configs are available.
    auto EnsureLightingPanelReady(const PassConfigRefs& refs) -> void;

    //! Returns the rendering view model (may be null if not yet created).
    [[nodiscard]] auto GetRenderingVm() const -> observer_ptr<RenderingVm>;

    //! Returns the light culling view model (may be null if not yet created).
    [[nodiscard]] auto GetLightCullingVm() const
      -> observer_ptr<LightCullingVm>;

    //! Returns the camera view model.
    [[nodiscard]] auto GetCameraVm() const -> observer_ptr<CameraVm>;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_ {};
  };

} // namespace ui

} // namespace oxygen::examples
