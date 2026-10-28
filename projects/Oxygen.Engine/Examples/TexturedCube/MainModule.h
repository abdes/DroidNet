//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "DemoShell/Runtime/MainViewState.h"
#include "DemoShell/Services/SkyboxService.h"
#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/TextureLoadingService.h"
#include "TexturedCube/UI/MaterialsSandboxPanel.h"
#include "TexturedCube/UI/MaterialsSandboxVm.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::textured_cube {

//! Main module for the TexturedCube demo.
/*!
 This module demonstrates the engine's bindless material/texture binding path
 by rendering a textured cube with orbit camera controls.

 ### Architecture

 The module is structured into several focused components:
 - **DemoShell**: Handles camera, standard panels, and UI framework
 - **TextureLoadingService**: Loads and uploads textures asynchronously
 - **SkyboxService**: Manages skybox loading and scene environment
 - **SceneSetup**: Creates and configures scene objects (cube, lights)
 - **MaterialsSandboxPanel**: Custom UI for texture browsing and assignment

 ### Controls

 - Mouse wheel: zoom in/out
 - RMB + mouse drag: orbit camera (standard DemoShell controls)

 @see DemoShell, TextureLoadingService, SkyboxService, SceneSetup
*/
class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = oxygen::examples::DemoModuleBase;

  explicit MainModule(const host::HostContext& host);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> oxygen::engine::ModulePriority override
  {
    constexpr engine::ModulePriority kPriority { 500 };
    return kPriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> oxygen::engine::ModulePhaseMask override
  {
    return kDemoPhases;
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto OnAttachedImpl(
    oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  auto OnShutdown() noexcept -> void override;

  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

protected:
#if defined(OXYGEN_BUILD_UI_TESTS)
  auto RegisterUiTests(ImGuiTestEngine* engine) -> void override;
#endif
  [[nodiscard]] auto BuildWindowProperties() const
    -> platform::window::Properties override;
  auto OnScenePublished(engine::FrameContext& context) -> void override;
  auto BuildViews(engine::FrameContext& context, host::HostViews& views)
    -> void override;

private:
  //! Stages the scene with its camera and builds the services that use it.
  auto StageScene(DemoShell& shell) -> void;
  //! Creates the texture service and the materials sandbox panel, once.
  auto CreateMaterialsSandbox(
    DemoShell& shell, observer_ptr<content::IAssetLoader> loader) -> void;
  auto ResolveSelectedCustomMaterial()
    -> std::shared_ptr<const data::MaterialAsset>;
  auto RequestCustomMaterialLoad(const data::AssetKey& key) -> void;

  host::ActiveScene active_scene_;
  std::unique_ptr<TextureLoadingService> texture_service_;
  std::unique_ptr<SkyboxService> skybox_service_;
  std::unique_ptr<SceneSetup> scene_setup_;

  // Hosted view
  ViewId main_view_id_ { kInvalidViewId };
  MainViewState main_view_state_;
  scene::SceneNode main_camera_ {};

  // Custom UI
  std::unique_ptr<ui::MaterialsSandboxVm> texture_vm_;
  std::shared_ptr<ui::MaterialsSandboxPanel> texture_panel_;

  // State
  std::filesystem::path cooked_root_;
  std::filesystem::path content_root_;
  oxygen::content::ResourceKey forced_error_key_ { 0U };
  std::shared_ptr<const data::MaterialAsset> selected_custom_material_;
  std::optional<data::AssetKey> selected_custom_material_key_;
  std::optional<data::AssetKey> custom_material_load_inflight_key_;
};

} // namespace oxygen::examples::textured_cube
