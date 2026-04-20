//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/ActiveScene.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "Physics/PhysicsDemoPanel.h"

namespace oxygen::physics {
class PhysicsModule;
}

namespace oxygen::renderer {
struct CompositionView;
} // namespace oxygen::renderer

namespace oxygen::examples::ui {
class CameraRigController;
}

namespace oxygen::examples::physics_demo {

class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)

public:
  using Base = DemoModuleBase;

  explicit MainModule(const DemoAppContext& app);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    constexpr engine::ModulePriority kPriority { 500 };
    return kPriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    using enum core::PhaseId;
    return engine::MakeModuleMask<kFrameStart, kSceneMutation, kGameplay,
      kPublishViews, kGuiUpdate, kPreRender, kCompositing, kFrameEnd>();
  }

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true;
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  void OnShutdown() noexcept override;

protected:
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;
  auto ClearBackbufferReferences() -> void override;
  auto UpdateComposition(engine::FrameContext& context,
    std::vector<vortex::CompositionView>& views) -> void override;

  auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPreRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void override;

private:
  struct DynamicObstacleState final {
    scene::SceneNode node {};
    Vec3 spawn_position { 0.0F, 0.0F, 0.0F };
    Quat spawn_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    physics::BodyId body_id { physics::kInvalidBodyId };
  };

  struct FlipperState final {
    scene::SceneNode node {};
    physics::BodyId body_id { physics::kInvalidBodyId };
    Vec3 position { 0.0F, 0.0F, 0.0F };
    float rest_angle_rad { 0.0F };
    float max_swing_rad { 0.0F };
    float elapsed_sec { 999.0F };
    float swing_duration_sec { 0.45F };
    float direction_sign { 1.0F };
  };

  auto InitInputBindings() noexcept -> bool;
  auto UpdatePhysicsDemoPanelConfig(
    observer_ptr<ui::CameraRigController> camera_rig) -> void;

  auto ResolvePhysicsModule() -> observer_ptr<physics::PhysicsModule>;
  auto BuildProceduralScene() -> bool;
  auto InitializePhysicsScenario() -> bool;
  auto StageScenarioScene() -> bool;
  auto ResetGameplayState() -> bool;
  auto ResetScenario() -> void;
  auto LaunchSphere() -> void;
  auto UpdateFlippers(float dt_seconds) -> void;

  auto SpawnRenderableNode(std::string_view name,
    const std::shared_ptr<data::GeometryAsset>& geometry, const Vec3& position,
    const glm::quat& rotation, const Vec3& scale) -> scene::SceneNode;
  auto AttachRigidBody(scene::SceneNode& node,
    const physics::body::BodyDesc& desc) -> std::optional<physics::BodyId>;

  ActiveScene active_scene_;
  scene::SceneNode main_camera_ {};

  ViewId main_view_id_ { kInvalidViewId };

  std::shared_ptr<input::Action> launch_action_;
  std::shared_ptr<input::Action> reset_action_;
  std::shared_ptr<input::Action> nudge_left_action_;
  std::shared_ptr<input::Action> nudge_right_action_;
  std::shared_ptr<input::InputMappingContext> gameplay_input_ctx_;

  std::shared_ptr<PhysicsDemoPanel> physics_panel_;

  observer_ptr<ui::CameraRigController> last_camera_rig_ { nullptr };
  std::shared_ptr<data::GeometryAsset> cube_geometry_;
  std::shared_ptr<data::GeometryAsset> sphere_geometry_;
  std::shared_ptr<data::GeometryAsset> player_sphere_geometry_;

  scene::SceneNode player_node_ {};
  std::optional<physics::BodyId> player_body_ {};
  std::vector<scene::SceneNode> static_nodes_ {};
  std::vector<DynamicObstacleState> dynamic_obstacles_ {};
  std::vector<FlipperState> flippers_ {};
  Vec3 player_spawn_position_ { 0.0F, -12.6F, 8.3F };
  Quat player_spawn_rotation_ { 1.0F, 0.0F, 0.0F, 0.0F };
  Vec3 bowl_center_ { 0.0F, 11.0F, 0.8F };

  bool scene_ready_ { false };
  bool physics_ready_ { false };

  bool pending_launch_ { false };
  bool pending_reset_ { false };

  float launch_impulse_ { 42.0F };
  float player_speed_ { 0.0F };
  float settle_timer_sec_ { 0.0F };
  float settle_display_sec_ { 0.0F };
  bool player_settled_ { false };
  std::optional<Vec3> previous_player_world_position_ {};

  uint64_t launches_count_ { 0 };
  uint64_t resets_count_ { 0 };
  uint64_t contact_events_count_ { 0 };
  uint64_t flipper_trigger_count_ { 0 };

  static constexpr float kSettleSpeedThreshold = 0.35F;
  static constexpr float kSettleTimeRequiredSec = 1.2F;
};

} // namespace oxygen::examples::physics_demo
