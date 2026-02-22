//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <cstddef>
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
 - `body::BodyType::kStatic`: scene transform writes are ignored by the sync
   bridge after attach; no automatic pull from active-body stream.
 - `body::BodyType::kKinematic`: scene owns motion. Deferred transform
   mutations are pushed to physics in `kGameplay`.
 - `body::BodyType::kDynamic`: physics owns motion. Active body transforms are
   pulled from physics in `kSceneMutation`.

 Same-frame precedence:
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

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "PhysicsModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return priority_;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return engine::MakeModuleMask<core::PhaseId::kFixedSimulation,
      core::PhaseId::kGameplay, core::PhaseId::kSceneMutation>();
  }

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
  [[nodiscard]] auto GetSyncDiagnostics() const noexcept -> SyncDiagnostics
  {
    return diagnostics_;
  }

  auto RegisterNodeBodyMapping(const scene::NodeHandle& node_handle,
    BodyId body_id, body::BodyType body_type) -> void
  {
    if (!node_handle.IsValid() || body_id == kInvalidBodyId) {
      return;
    }

    if (const auto existing_node_it = node_to_binding_.find(node_handle);
      existing_node_it != node_to_binding_.end()) {
      (void)RemoveBinding(existing_node_it->second);
    }
    if (const auto existing_body_it = body_to_binding_.find(body_id);
      existing_body_it != body_to_binding_.end()) {
      (void)RemoveBinding(existing_body_it->second);
    }

    CHECK_NOTNULL_F(bindings_.get());
    const auto binding_handle = bindings_->Insert(PhysicsBinding {
      .world_id = world_id_,
      .body_id = body_id,
      .body_type = body_type,
      .node_handle = node_handle,
    });
    node_to_binding_.insert_or_assign(node_handle, binding_handle);
    body_to_binding_.insert_or_assign(body_id, binding_handle);
  }

  [[nodiscard]] auto GetBodyIdForNode(
    const scene::NodeHandle& node_handle) const -> BodyId
  {
    const auto it = node_to_binding_.find(node_handle);
    if (it == node_to_binding_.end()) {
      return kInvalidBodyId;
    }
    CHECK_NOTNULL_F(bindings_.get());
    const auto* binding = bindings_->TryGet(it->second);
    if (binding == nullptr) {
      return kInvalidBodyId;
    }
    return binding->body_id;
  }

  [[nodiscard]] auto GetBodyTypeForBodyId(BodyId body_id) const
    -> std::optional<body::BodyType>;

  [[nodiscard]] auto GetNodeForBodyId(BodyId body_id) const
    -> std::optional<scene::NodeHandle>
  {
    const auto it = body_to_binding_.find(body_id);
    if (it == body_to_binding_.end()) {
      return std::nullopt;
    }
    CHECK_NOTNULL_F(bindings_.get());
    const auto* binding = bindings_->TryGet(it->second);
    if (binding == nullptr || !binding->node_handle.IsValid()) {
      return std::nullopt;
    }
    return binding->node_handle;
  }

  auto RegisterNodeCharacterMapping(
    const scene::NodeHandle& node_handle, CharacterId character_id) -> void
  {
    if (!node_handle.IsValid() || character_id == kInvalidCharacterId) {
      return;
    }

    if (const auto existing_node_it
      = node_to_character_binding_.find(node_handle);
      existing_node_it != node_to_character_binding_.end()) {
      (void)RemoveCharacterBinding(existing_node_it->second);
    }
    if (const auto existing_character_it
      = character_to_binding_.find(character_id);
      existing_character_it != character_to_binding_.end()) {
      (void)RemoveCharacterBinding(existing_character_it->second);
    }

    CHECK_NOTNULL_F(character_bindings_.get());
    const auto binding_handle = character_bindings_->Insert(CharacterBinding {
      .world_id = world_id_,
      .character_id = character_id,
      .node_handle = node_handle,
    });
    node_to_character_binding_.insert_or_assign(node_handle, binding_handle);
    character_to_binding_.insert_or_assign(character_id, binding_handle);
  }

  [[nodiscard]] auto GetCharacterIdForNode(
    const scene::NodeHandle& node_handle) const -> CharacterId
  {
    const auto it = node_to_character_binding_.find(node_handle);
    if (it == node_to_character_binding_.end()) {
      return kInvalidCharacterId;
    }
    CHECK_NOTNULL_F(character_bindings_.get());
    const auto* binding = character_bindings_->TryGet(it->second);
    if (binding == nullptr) {
      return kInvalidCharacterId;
    }
    return binding->character_id;
  }

  [[nodiscard]] auto GetNodeForCharacterId(CharacterId character_id) const
    -> std::optional<scene::NodeHandle>
  {
    const auto it = character_to_binding_.find(character_id);
    if (it == character_to_binding_.end()) {
      return std::nullopt;
    }
    CHECK_NOTNULL_F(character_bindings_.get());
    const auto* binding = character_bindings_->TryGet(it->second);
    if (binding == nullptr || !binding->node_handle.IsValid()) {
      return std::nullopt;
    }
    return binding->node_handle;
  }

  [[nodiscard]] auto ConsumeSceneEvents() -> std::vector<ScenePhysicsEvent>
  {
    std::vector<ScenePhysicsEvent> drained {};
    drained.swap(scene_events_);
    return drained;
  }

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
  std::unordered_set<scene::NodeHandle> pending_transform_updates_;
  std::vector<ScenePhysicsEvent> scene_events_ {};
  SyncDiagnostics diagnostics_ {};
};

} // namespace oxygen::physics
