//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "../Common/AsyncEngineApp.h"
#include "../Common/SingleViewExample.h"
#include "FlyCameraController.h"
#include "OrbitCameraController.h"

namespace oxygen::data {
class SceneAsset;
} // namespace oxygen::data

namespace oxygen::content {
class PakFile;
class LooseCookedInspection;
} // namespace oxygen::content

namespace oxygen::content::import {
class AssetImporter;
} // namespace oxygen::content::import

namespace oxygen::examples::render_scene {

class SceneLoader;

class MainModule final : public common::SingleViewExample {
  OXYGEN_TYPED(MainModule)
public:
  using Base = oxygen::examples::common::SingleViewExample;

  explicit MainModule(const oxygen::examples::common::AsyncEngineApp& app);

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

  struct PendingSceneSwap {
    std::shared_ptr<scene::Scene> scene;
    scene::SceneNode active_camera;
    data::AssetKey scene_key {};
  };

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
  auto InitInputBindings() noexcept -> bool;
  auto EnsureFallbackCamera(const int width, const int height) -> void;
  auto EnsureActiveCameraViewport(const int width, const int height) -> void;
  auto ApplyOrbitAndZoom(time::CanonicalDuration delta_time) -> void;
  auto EnsureViewCameraRegistered() -> void;

  auto DrawDebugOverlay(engine::FrameContext& context) -> void;
  auto DrawCameraControls(engine::FrameContext& context) -> void;

  struct SceneListItem {
    std::string virtual_path;
    data::AssetKey key {};
  };

  // Scene and rendering.
  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode active_camera_;
  scene::NodeHandle registered_view_camera_ {};

  std::optional<PendingSceneSwap> pending_scene_swap_;
  std::shared_ptr<SceneLoader> scene_loader_;

  // Camera input.
  std::shared_ptr<oxygen::input::Action> zoom_in_action_;
  std::shared_ptr<oxygen::input::Action> zoom_out_action_;
  std::shared_ptr<oxygen::input::Action> rmb_action_;
  std::shared_ptr<oxygen::input::Action> orbit_action_;
  std::shared_ptr<oxygen::input::Action> move_fwd_action_;
  std::shared_ptr<oxygen::input::Action> move_bwd_action_;
  std::shared_ptr<oxygen::input::Action> move_left_action_;
  std::shared_ptr<oxygen::input::Action> move_right_action_;
  std::shared_ptr<oxygen::input::Action> move_up_action_;
  std::shared_ptr<oxygen::input::Action> move_down_action_;
  std::shared_ptr<oxygen::input::Action> fly_plane_lock_action_;
  std::shared_ptr<oxygen::input::Action> fly_boost_action_;
  std::shared_ptr<oxygen::input::InputMappingContext> orbit_controls_ctx_;
  std::shared_ptr<oxygen::input::InputMappingContext> fly_controls_ctx_;

  enum class CameraMode { kOrbit, kFly };
  CameraMode camera_mode_ { CameraMode::kOrbit };
  std::unique_ptr<OrbitCameraController> orbit_controller_;
  std::unique_ptr<FlyCameraController> fly_controller_;

  auto UpdateActiveCameraInputContext() -> void;

  // UI state.
  std::array<char, 512> pak_path_ {};
  bool pending_mount_pak_ { false };
  std::unique_ptr<content::PakFile> ui_pak_;
  std::vector<SceneListItem> pak_scenes_;

  std::array<char, 512> loose_index_path_ {};
  bool pending_load_loose_index_ { false };
  std::unique_ptr<content::LooseCookedInspection> loose_inspection_;
  std::vector<SceneListItem> loose_scenes_;

  std::filesystem::path content_root_;
  std::optional<std::filesystem::path> pending_fbx_import_path_;
  std::unique_ptr<content::import::AssetImporter> asset_importer_;

  std::future<std::optional<data::AssetKey>> fbx_import_future_;
  std::atomic<bool> is_importing_fbx_ { false };
  std::string importing_fbx_path_;

  bool pending_load_scene_ { false };
  std::optional<data::AssetKey> pending_scene_key_;

  int last_viewport_w_ { 0 };
  int last_viewport_h_ { 0 };

  // Debug/instrumentation.
  bool logged_gameplay_tick_ { false };
  bool was_orbiting_last_frame_ { false };

  bool pending_sync_active_camera_ { false };
};

} // namespace oxygen::examples::render_scene
