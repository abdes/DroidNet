//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/SingleViewModuleBase.h"
#include "DemoShell/Services/SkyboxService.h"
#include "TexturedCube/CameraController.h"
#include "TexturedCube/DebugUI.h"
#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/TextureLoadingService.h"

namespace oxygen::examples::textured_cube {

//! Main module for the TexturedCube demo.
/*!
 This module demonstrates the engine's bindless material/texture binding path
 by rendering a textured cube with orbit camera controls.

 ### Architecture

 The module is structured into several focused components:
 - **CameraController**: Handles orbit camera with mouse controls
 - **TextureLoadingService**: Loads and uploads textures asynchronously
 - **SkyboxService**: Manages skybox loading and scene environment
 - **SceneSetup**: Creates and configures scene objects (cube, lights)
 - **DebugUI**: ImGui-based debug overlay for runtime tweaking

 ### Controls

 - Mouse wheel: zoom in/out
 - RMB + mouse drag: orbit camera

 @see CameraController, TextureLoadingService, SkyboxService, SceneSetup,
 DebugUI
*/
class MainModule final : public SingleViewModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = oxygen::examples::SingleViewModuleBase;

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
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

protected:
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

private:
  struct TextureSlotSelection {
    SceneSetup::TextureIndexMode mode {
      SceneSetup::TextureIndexMode::kFallback
    };
    std::uint32_t resource_index { 0U };
    oxygen::content::ResourceKey resource_key { 0U };
  };

  std::shared_ptr<scene::Scene> scene_;

  // Component modules
  std::unique_ptr<CameraController> camera_controller_;
  std::unique_ptr<TextureLoadingService> texture_service_;
  std::unique_ptr<SkyboxService> skybox_service_;
  std::unique_ptr<SceneSetup> scene_setup_;
  std::unique_ptr<DebugUI> debug_ui_;

  // State
  TextureSlotSelection sphere_texture_ {};
  TextureSlotSelection cube_texture_ {};
  oxygen::content::ResourceKey forced_error_key_ { 0U };
  bool cube_needs_rebuild_ { true };
};

} // namespace oxygen::examples::textured_cube
