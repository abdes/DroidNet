//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Platform/Window.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "DemoShell/UI/ContentVm.h"

namespace oxygen {
class AsyncEngine;
namespace engine {
  class FrameContext;
}
} // namespace oxygen

namespace oxygen::examples {
class SceneLoaderService;
} // namespace oxygen::examples

namespace oxygen::examples::render_scene {

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
    constexpr oxygen::engine::ModulePriority kPriority { 500 };
    return kPriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> oxygen::engine::ModulePhaseMask override
  {
    using enum core::PhaseId;
    return engine::MakeModuleMask<kFrameStart, kSceneMutation, kGameplay,
      kGuiUpdate, kPreRender, kCompositing, kFrameEnd>();
  }

  ~MainModule() override;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

  auto ClearBackbufferReferences() -> void override;
  auto UpdateComposition(engine::FrameContext& context,
    std::vector<CompositionView>& views) -> void override;

  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  void OnShutdown() noexcept override;

  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  auto HandleOnFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

private:
  auto ReleaseCurrentSceneAsset(const char* reason) -> void;

  auto ApplyRenderModeFromPanel() -> void;
  auto ClearSceneRuntime(const char* reason) -> void;

  struct SceneLoadRequest {
    data::AssetKey key {};
    ui::SceneSourceKind source_kind { ui::SceneSourceKind::kPak };
    std::filesystem::path source_path;
    std::string scene_name;
  };

  // Scene and rendering.
  ActiveScene active_scene_;
  ViewId main_view_id_ { kInvalidViewId };

  std::shared_ptr<oxygen::examples::SceneLoaderService> scene_loader_;
  bool scene_load_cancel_requested_ { false };

  std::unique_ptr<DemoShell> shell_;

  // Content and scene state
  std::optional<data::AssetKey> current_scene_key_;
  std::optional<data::AssetKey> last_released_scene_key_;
  std::optional<data::AssetKey> active_scene_load_key_;
  Extent<uint32_t> last_viewport_;

  // Debug/instrumentation.
  bool logged_gameplay_tick_ { false };
  bool was_orbiting_last_frame_ { false };

  // Deferred lifecycle actions (applied in OnFrameStart)
  enum class PendingSourceAction : uint8_t {
    kNone,
    kClear,
    kTrimCache,
    kMountPak,
    kMountIndex,
  };
  PendingSourceAction pending_source_action_ { PendingSourceAction::kNone };
  std::filesystem::path pending_path_;
  std::optional<SceneLoadRequest> pending_scene_load_;
  std::vector<std::filesystem::path> mounted_loose_roots_;
};

} // namespace oxygen::examples::render_scene
