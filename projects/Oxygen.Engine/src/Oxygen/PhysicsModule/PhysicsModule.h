//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Base/ResourceTable.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/System/IBodyApi.h>
#include <Oxygen/Physics/System/ICharacterApi.h>
#include <Oxygen/Physics/System/IEventApi.h>
#include <Oxygen/Physics/System/IPhysicsSystem.h>
#include <Oxygen/Physics/System/IQueryApi.h>
#include <Oxygen/PhysicsModule/api_export.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::physics {

/*!
 Scene/physics bridge module with strict phase ownership.

 Responsibilities:
 - Own physics backend lifetime (`IPhysicsSystem`) and one simulation world.
 - Maintain a physics-managed subset of scene nodes via a generation-safe
   binding table (`ResourceTable`) indexed by `ResourceHandle`, with O(1)
   lookup indices by node and body.
 - Reconcile scene lifecycle changes (transform changed / node destroyed)
   through the deferred scene mutation stream.

 Phase contract:
 - `kFixedSimulation`: step the physics world only.
 - `kGameplay`: push scene-authored transforms to tracked bodies.
 - `kSceneMutation`: pull active physics transforms to tracked scene nodes and
   reconcile deferred lifecycle events.

 Integration contract:
 - Producers (hydration, scripts, game modules) mutate scene only in
   `kGameplay` or `kSceneMutation`.
 - Physics does not replace `kTransformPropagation`; scene transform propagation
   remains exclusively `Scene::Update()`.
 - Newly attached/changed bodies are allowed to become simulation-visible on the
   next fixed-simulation step (one-frame latency by design).

 Motion authority contract:
 - `body::BodyType::kStatic`: scene transform writes
 are ignored by the sync
   bridge after attach; no automatic pull from
 active-body stream.
 - `body::BodyType::kKinematic`: scene owns motion.
 Deferred transform
   mutations are pushed to physics in `kGameplay`.
 -
 `body::BodyType::kDynamic`: physics owns motion. Active body transforms are

 pulled from physics in `kSceneMutation`.
 - Character controllers are
 command-authoritative:
   `ScenePhysics::CharacterFacade::Move` drives
 character motion intent.
   Successful move results are converted from world to
 local and applied to the
   scene node through this module.
   After character
 attachment, scene-authored transform writes on that node are
   contract
 violations (debug-asserted) and are never enqueued into the
   rigid-body push
 path.
 - A scene node can be managed by exactly one motion authority source:
 either
   rigid-body mapping or character mapping, never both.
 Same-frame
 precedence:
 - If a dynamic body also receives scene-authored transform writes in the same
   frame, physics remains authoritative and the pulled dynamic pose wins.
*/
class PhysicsModule final : public engine::EngineModule,
                            public scene::ISceneObserver {
  OXYGEN_TYPED(PhysicsModule)

public:
  struct SyncDiagnostics final {
    uint64_t gameplay_push_attempts { 0 };
    uint64_t gameplay_push_success { 0 };
    uint64_t gameplay_push_skipped_untracked { 0 };
    uint64_t gameplay_push_skipped_non_kinematic { 0 };
    uint64_t gameplay_push_skipped_missing_node { 0 };
    uint64_t scene_pull_attempts { 0 };
    uint64_t scene_pull_success { 0 };
    uint64_t scene_pull_skipped_non_dynamic { 0 };
    uint64_t scene_pull_skipped_unmapped { 0 };
    uint64_t scene_pull_skipped_missing_node { 0 };
    uint64_t event_drain_calls { 0 };
    uint64_t event_drain_count { 0 };
  };

  struct ScenePhysicsEvent final {
    events::PhysicsEventType type { events::PhysicsEventType::kContactBegin };
    std::optional<scene::NodeHandle> node_a {};
    std::optional<scene::NodeHandle> node_b {};
    events::PhysicsEvent raw_event {};
  };

  //! Priority contract: must run after gameplay mutators (including
  //! ScriptingModule) and before RendererModule.
  OXGN_PHSYNC_API explicit PhysicsModule(engine::ModulePriority priority);
  /*! Testing/integration constructor that injects a prebuilt physics system.

   * Creates and owns one simulation world immediately. */
  OXGN_PHSYNC_API PhysicsModule(engine::ModulePriority priority,
    std::unique_ptr<system::IPhysicsSystem> physics_system);
  OXGN_PHSYNC_API ~PhysicsModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PhysicsModule)
  OXYGEN_MAKE_NON_MOVABLE(PhysicsModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override;

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override;

  OXGN_PHSYNC_API auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool override;
  OXGN_PHSYNC_API auto OnShutdown() noexcept -> void override;

  /*! Fixed simulation authority:
      - Step the physics world once per fixed-simulation substep.
      - No scene graph transform writes in this phase. */
  OXGN_PHSYNC_API auto OnFixedSimulation(
    observer_ptr<engine::FrameContext> context) -> co::Co<> override;

  /*! Gameplay staging:
      - Consume deferred scene transform mutations and push scene-authored
        body poses into physics (kinematic authority path). */
  OXGN_PHSYNC_API auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

  /*! Scene mutation reconciliation:
      - Pull active body transforms from physics and write back scene local
        transforms for physics-authored bodies (dynamic authority path).
      - Apply lifecycle reconciliation through deferred observer events. */
  OXGN_PHSYNC_API auto OnSceneMutation(
    observer_ptr<engine::FrameContext> context) -> co::Co<> override;

  // ISceneObserver implementation
  auto OnTransformChanged(const scene::NodeHandle& node_handle) noexcept
    -> void override;
  auto OnNodeDestroyed(const scene::NodeHandle& node_handle) noexcept
    -> void override;

  [[nodiscard]] auto GetBodyApi() noexcept -> system::IBodyApi&;
  [[nodiscard]] auto GetCharacterApi() noexcept -> system::ICharacterApi&;
  [[nodiscard]] auto GetEventApi() noexcept -> system::IEventApi&;
  [[nodiscard]] auto GetQueryApi() noexcept -> system::IQueryApi&;
  [[nodiscard]] auto GetWorldId() const noexcept -> WorldId;
  [[nodiscard]] auto IsNodeInObservedScene(
    const scene::NodeHandle& node_handle) const noexcept -> bool;
  OXGN_PHSYNC_NDAPI auto GetSyncDiagnostics() const noexcept -> SyncDiagnostics;

  OXGN_PHSYNC_API auto RegisterNodeBodyMapping(
    const scene::NodeHandle& node_handle, BodyId body_id,
    body::BodyType body_type) -> void;

  OXGN_PHSYNC_NDAPI auto GetBodyIdForNode(
    const scene::NodeHandle& node_handle) const -> BodyId;
  OXGN_PHSYNC_NDAPI auto HasBodyForNode(
    const scene::NodeHandle& node_handle) const -> bool;

  [[nodiscard]] auto GetBodyTypeForBodyId(BodyId body_id) const
    -> std::optional<body::BodyType>;

  OXGN_PHSYNC_NDAPI auto GetNodeForBodyId(BodyId body_id) const
    -> std::optional<scene::NodeHandle>;

  OXGN_PHSYNC_API auto RegisterNodeCharacterMapping(
    const scene::NodeHandle& node_handle, CharacterId character_id) -> void;

  OXGN_PHSYNC_NDAPI auto GetCharacterIdForNode(
    const scene::NodeHandle& node_handle) const -> CharacterId;
  OXGN_PHSYNC_NDAPI auto HasCharacterForNode(
    const scene::NodeHandle& node_handle) const -> bool;

  OXGN_PHSYNC_NDAPI auto GetNodeForCharacterId(CharacterId character_id) const
    -> std::optional<scene::NodeHandle>;

  /*! Convert a world-space pose into node-local space using the current parent

   * chain and apply it to the scene node transform.
      Returns false when
   * scene/node preconditions are not met. */
  OXGN_PHSYNC_API auto ApplyWorldPoseToNode(
    const scene::NodeHandle& node_handle, const Vec3& world_position,
    const Quat& world_rotation) -> bool;

  OXGN_PHSYNC_NDAPI auto ConsumeSceneEvents() -> std::vector<ScenePhysicsEvent>;

private:
  struct PhysicsBinding final {
    WorldId world_id { kInvalidWorldId };
    BodyId body_id { kInvalidBodyId };
    body::BodyType body_type { body::BodyType::kStatic };
    scene::NodeHandle node_handle {};
  };
  using PhysicsBindingTable = ResourceTable<PhysicsBinding>;
  struct CharacterBinding final {
    WorldId world_id { kInvalidWorldId };
    CharacterId character_id { kInvalidCharacterId };
    scene::NodeHandle node_handle {};
  };
  using CharacterBindingTable = ResourceTable<CharacterBinding>;
  static constexpr ResourceHandle::ResourceTypeT kBindingResourceType { 0xB1 };
  static constexpr ResourceHandle::ResourceTypeT kCharacterBindingResourceType {
    0xB2,
  };
  static constexpr std::size_t kMinBindingReserve { 64 };

  auto SyncSceneObserver(observer_ptr<engine::FrameContext> context) -> void;
  auto RemoveBinding(const ResourceHandle& binding_handle) -> bool;
  auto RemoveCharacterBinding(const ResourceHandle& binding_handle) -> bool;
  auto EnsureBindingCapacity(std::size_t min_reserve) -> void;
  [[nodiscard]] static auto EstimateBindingReserve(
    observer_ptr<scene::Scene> scene) -> std::size_t;
  auto DestroyAllTrackedBodies() -> void;
  auto DestroyAllTrackedCharacters() -> void;
  auto DrainPhysicsEvents() -> void;

  engine::ModulePriority priority_;
  observer_ptr<AsyncEngine> engine_;
  observer_ptr<scene::Scene> observed_scene_;
  std::unique_ptr<system::IPhysicsSystem> physics_system_;
  WorldId world_id_ { kInvalidWorldId };
  std::unique_ptr<PhysicsBindingTable> bindings_;
  std::unique_ptr<CharacterBindingTable> character_bindings_;
  std::unordered_map<scene::NodeHandle, ResourceHandle> node_to_binding_;
  std::unordered_map<BodyId, ResourceHandle> body_to_binding_;
  std::unordered_map<scene::NodeHandle, ResourceHandle>
    node_to_character_binding_;
  std::unordered_map<CharacterId, ResourceHandle> character_to_binding_;
  std::unordered_set<scene::NodeHandle> expected_character_transform_updates_;
  std::unordered_set<scene::NodeHandle> pending_transform_updates_;
  std::vector<ScenePhysicsEvent> scene_events_ {};
  SyncDiagnostics diagnostics_ {};
};

} // namespace oxygen::physics
