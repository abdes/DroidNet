//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "DemoShell/Services/SkyboxService.h"
#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/TextureLoadingService.h"
#include "TexturedCube/UI/MaterialsSandboxPanel.h"
#include "TexturedCube/UI/MaterialsSandboxVm.h"

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

  explicit MainModule(const oxygen::examples::DemoAppContext& app);

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
    using enum core::PhaseId;
    return engine::MakeModuleMask<kFrameStart, kSceneMutation, kGameplay,
      kGuiUpdate, kPreRender, kCompositing, kFrameEnd>();
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto OnAttachedImpl(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(observer_ptr<oxygen::engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

protected:
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

  auto ClearBackbufferReferences() -> void override;
  auto UpdateComposition(engine::FrameContext& context,
    std::vector<CompositionView>& views) -> void override;

private:
  // Scene is owned by DemoShell, we keep a value object for safe access
  ActiveScene active_scene_;
  std::unique_ptr<TextureLoadingService> texture_service_;
  std::unique_ptr<SkyboxService> skybox_service_;
  std::unique_ptr<SceneSetup> scene_setup_;

  // Hosted view
  ViewId main_view_id_ { kInvalidViewId };
  scene::SceneNode main_camera_ {};

  // Custom UI
  std::unique_ptr<ui::MaterialsSandboxVm> texture_vm_;
  std::shared_ptr<ui::MaterialsSandboxPanel> texture_panel_;

  // State
  std::filesystem::path cooked_root_;
  std::filesystem::path content_root_;
  oxygen::content::ResourceKey forced_error_key_ { 0U };

  // Track last viewport size to inform camera
  platform::window::ExtentT last_viewport_ { 0, 0 };
};

} // namespace oxygen::examples::textured_cube
