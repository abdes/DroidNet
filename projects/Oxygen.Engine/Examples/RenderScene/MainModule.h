//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include "../Common/AsyncEngineApp.h"
#include "../Common/SingleViewExample.h"

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
  auto SyncOrbitFromActiveCamera() -> void;
  auto SyncTurntableFromActiveCamera() -> void;
  auto ApplyOrbitAndZoom() -> void;
  auto EnsureViewCameraRegistered() -> void;

  auto DrawDebugOverlay(engine::FrameContext& context) -> void;

  struct SceneListItem {
    std::string virtual_path;
    data::AssetKey key {};
  };

  // Scene and rendering.
  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode active_camera_;
  scene::NodeHandle registered_view_camera_ {};

  scene::NodeHandle orbit_camera_ {};

  std::optional<PendingSceneSwap> pending_scene_swap_;
  std::shared_ptr<SceneLoader> scene_loader_;

  // Camera input.
  std::shared_ptr<oxygen::input::Action> zoom_in_action_;
  std::shared_ptr<oxygen::input::Action> zoom_out_action_;
  std::shared_ptr<oxygen::input::Action> rmb_action_;
  std::shared_ptr<oxygen::input::Action> orbit_action_;
  std::shared_ptr<oxygen::input::InputMappingContext> camera_controls_ctx_;

  glm::vec3 camera_target_ { 0.0f, 0.0f, 0.0f };

  enum class OrbitMode {
    kTrackball = 0,
    kTurntable = 1,
  };

  OrbitMode orbit_mode_ { OrbitMode::kTrackball };

  // Trackball-style orbit state (Blender-like): rotate the view quaternion,
  // and derive camera position from a fixed local offset so the target stays
  // centered.
  glm::quat orbit_rot_ { 1.0f, 0.0f, 0.0f, 0.0f }; // Camera local rotation
  glm::vec3 orbit_offset_local_ { 0.0f, 1.0f, 0.0f };
  float orbit_distance_ { 6.0f };
  float orbit_sensitivity_ { 0.01f };

  // Turntable orbit state (Blender-like horizon lock): yaw/pitch around a fixed
  // world-up axis (Z). Allows crossing the pole by flipping the up-vector.
  float turntable_yaw_ { 0.0f };
  float turntable_pitch_ { 0.0f };
  bool turntable_inverted_ { false };

  float zoom_step_ { 0.75f };
  float min_cam_distance_ { 1.25f };
  float max_cam_distance_ { 40.0f };

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

  bool pending_load_scene_ { false };
  std::optional<data::AssetKey> pending_scene_key_;

  int last_viewport_w_ { 0 };
  int last_viewport_h_ { 0 };

  // Debug/instrumentation.
  bool logged_gameplay_tick_ { false };
  bool was_orbiting_last_frame_ { false };
};

} // namespace oxygen::examples::render_scene
