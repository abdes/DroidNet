//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "DemoShell/ActiveScene.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/SingleViewModuleBase.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/SkyboxService.h"

namespace oxygen {
class AsyncEngine;
namespace data {
  class SceneAsset;
} // namespace data
namespace content {
  class PakFile;
  class LooseCookedInspection;
} // namespace content
namespace content::import {
  class AssetImporter;
} // namespace content::import
} // namespace oxygen

namespace oxygen::examples {
class SceneLoaderService;
} // namespace oxygen::examples

namespace oxygen::examples::render_scene {

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

  ~MainModule() override;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  void OnShutdown() noexcept override;

  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

private:
  auto EnsureViewCameraRegistered() -> void;
  auto ReleaseCurrentSceneAsset(const char* reason) -> void;

  auto ApplyRenderModeFromPanel() -> void;
  auto ClearSceneRuntime(const char* reason) -> void;

  // Scene and rendering.
  ActiveScene active_scene_ {};
  scene::NodeHandle registered_view_camera_ {};

  std::shared_ptr<oxygen::examples::SceneLoaderService> scene_loader_;
  std::unique_ptr<FileBrowserService> file_browser_service_;
  std::unique_ptr<SkyboxService> skybox_service_;
  scene::Scene* skybox_service_scene_ { nullptr };

  std::unique_ptr<DemoShell> shell_ {};

  // Content and scene state
  std::filesystem::path cooked_root_;
  bool pending_load_scene_ { false };
  std::optional<data::AssetKey> pending_scene_key_;
  std::optional<data::AssetKey> current_scene_key_;
  std::optional<data::AssetKey> last_released_scene_key_;
  int last_viewport_w_ { 0 };
  int last_viewport_h_ { 0 };

  // Debug/instrumentation.
  bool logged_gameplay_tick_ { false };
  bool was_orbiting_last_frame_ { false };
};

} // namespace oxygen::examples::render_scene
