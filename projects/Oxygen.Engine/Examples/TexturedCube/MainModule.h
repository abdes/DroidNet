//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "DemoShell/Runtime/SceneView.h"
#include "DemoShell/Services/SkyboxService.h"
#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/TextureLoadingService.h"
#include "TexturedCube/UI/TextureBrowserPanel.h"
#include "TexturedCube/UI/TextureBrowserVm.h"

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
 - **TextureBrowserPanel**: Custom UI for texture browsing and assignment

 ### Controls

 - Mouse wheel: zoom in/out
 - RMB + mouse drag: orbit camera (standard DemoShell controls)

 @see DemoShell, TextureLoadingService, SkyboxService, SceneSetup
*/
class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = oxygen::examples::DemoModuleBase;

  explicit MainModule(const oxygen::examples::DemoAppContext& app);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> oxygen::engine::ModulePriority override
  {
    return engine::ModulePriority { 500 };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> oxygen::engine::ModulePhaseMask override
  {
    using namespace core;
    return engine::MakeModuleMask<PhaseId::kFrameStart, PhaseId::kSceneMutation,
      PhaseId::kGameplay, PhaseId::kGuiUpdate, PhaseId::kPreRender,
      PhaseId::kCompositing, PhaseId::kFrameEnd>();
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  auto HandleOnFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

protected:
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

  auto ClearBackbufferReferences() -> void override;

private:
  auto ApplyRenderModeFromPanel() -> void;

  std::unique_ptr<oxygen::examples::DemoShell> shell_;
  // Scene is owned by DemoShell, we keep a value object for safe access
  ActiveScene active_scene_ {};
  std::unique_ptr<TextureLoadingService> texture_service_;
  std::unique_ptr<SkyboxService> skybox_service_;
  std::unique_ptr<SceneSetup> scene_setup_;

  // Hosted view
  observer_ptr<SceneView> main_view_ { nullptr };

  // Custom UI
  std::unique_ptr<ui::TextureBrowserVm> texture_vm_;
  std::shared_ptr<ui::TextureBrowserPanel> texture_panel_;

  // State
  std::filesystem::path cooked_root_;
  std::filesystem::path content_root_;
  oxygen::content::ResourceKey forced_error_key_ { 0U };

  // Track last viewport size to inform camera
  int last_viewport_w_ { 0 };
  int last_viewport_h_ { 0 };
};

} // namespace oxygen::examples::textured_cube
