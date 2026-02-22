//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Physics/Physics.h>
#include <Oxygen/Physics/World/WorldDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::physics {
namespace {

  struct Trs final {
    Vec3 position { 0.0F, 0.0F, 0.0F };
    Quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    Vec3 scale { 1.0F, 1.0F, 1.0F };
  };

  auto InverseScaleSafe(const Vec3& scale) -> Vec3
  {
    constexpr float kScaleEpsilon = 1.0e-6F;
    auto inv = Vec3 { 1.0F, 1.0F, 1.0F };
    inv.x = std::abs(scale.x) > kScaleEpsilon ? 1.0F / scale.x : 1.0F;
    inv.y = std::abs(scale.y) > kScaleEpsilon ? 1.0F / scale.y : 1.0F;
    inv.z = std::abs(scale.z) > kScaleEpsilon ? 1.0F / scale.z : 1.0F;
    return inv;
  }

  auto ComposeTrs(const Trs& parent, const Trs& local) -> Trs
  {
    return Trs {
      .position
      = parent.position + parent.rotation * (local.position * parent.scale),
      .rotation = glm::normalize(parent.rotation * local.rotation),
      .scale = parent.scale * local.scale,
    };
  }

  auto BuildWorldTrsFromLocalChain(scene::SceneNode node) -> std::optional<Trs>
  {
    std::vector<scene::SceneNode> ancestry {};
    auto current = std::optional<scene::SceneNode> { node };
    while (current.has_value() && current->IsValid()) {
      ancestry.push_back(*current);
      current = current->GetParent();
    }

    Trs world {};
    for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it) {
      const auto local_position
        = it->GetTransform().GetLocalPosition().value_or(Vec3 { 0.0F });
      const auto local_rotation
        = it->GetTransform().GetLocalRotation().value_or(
          Quat { 1.0F, 0.0F, 0.0F, 0.0F });
      const auto local_scale
        = it->GetTransform().GetLocalScale().value_or(Vec3 { 1.0F });
      world = ComposeTrs(world,
        Trs {
          .position = local_position,
          .rotation = local_rotation,
          .scale = local_scale,
        });
    }
    return world;
  }

  auto ToLocalPose(scene::SceneNode& node, const Vec3& world_position,
    const Quat& world_rotation) -> std::pair<Vec3, Quat>
  {
    auto local_position = world_position;
    auto local_rotation = world_rotation;

    const auto parent = node.GetParent();
    if (!parent.has_value() || !parent->IsValid()) {
      return { local_position, local_rotation };
    }

    const auto parent_world = BuildWorldTrsFromLocalChain(*parent);
    if (!parent_world.has_value()) {
      return { local_position, local_rotation };
    }

    const auto inverse_parent_rotation = glm::inverse(parent_world->rotation);
    const auto inverse_parent_scale = InverseScaleSafe(parent_world->scale);
    const auto relative_position = world_position - parent_world->position;

    local_position
      = inverse_parent_rotation * (relative_position * inverse_parent_scale);
    local_rotation = glm::normalize(inverse_parent_rotation * world_rotation);
    return { local_position, local_rotation };
  }

  auto FixedDeltaSeconds(observer_ptr<engine::FrameContext> context) -> float
  {
    using Seconds = std::chrono::duration<float>;
    const auto timing = context->GetModuleTimingData();
    return std::chrono::duration_cast<Seconds>(timing.fixed_delta_time.get())
      .count();
  }

} // namespace

PhysicsModule::PhysicsModule(engine::ModulePriority priority)
  : priority_(priority)
  , bindings_(std::make_unique<PhysicsBindingTable>(
      kBindingResourceType, kMinBindingReserve))
  , character_bindings_(std::make_unique<CharacterBindingTable>(
      kCharacterBindingResourceType, kMinBindingReserve))
  , aggregate_bindings_(std::make_unique<AggregateBindingTable>(
      kAggregateBindingResourceType, kMinBindingReserve))
{
}

auto PhysicsModule::GetName() const noexcept -> std::string_view
{
  return "PhysicsModule";
}

auto PhysicsModule::GetPriority() const noexcept -> engine::ModulePriority
{
  return priority_;
}

auto PhysicsModule::GetSupportedPhases() const noexcept
  -> engine::ModulePhaseMask
{
  return engine::MakeModuleMask<core::PhaseId::kFixedSimulation,
    core::PhaseId::kGameplay, core::PhaseId::kSceneMutation>();
}

auto PhysicsModule::GetSyncDiagnostics() const noexcept -> SyncDiagnostics
{
  return diagnostics_;
}

PhysicsModule::PhysicsModule(engine::ModulePriority priority,
  std::unique_ptr<system::IPhysicsSystem> physics_system)
  : priority_(priority)
  , physics_system_(std::move(physics_system))
  , bindings_(std::make_unique<PhysicsBindingTable>(
      kBindingResourceType, kMinBindingReserve))
  , character_bindings_(std::make_unique<CharacterBindingTable>(
      kCharacterBindingResourceType, kMinBindingReserve))
  , aggregate_bindings_(std::make_unique<AggregateBindingTable>(
      kAggregateBindingResourceType, kMinBindingReserve))
{
  CHECK_NOTNULL_F(physics_system_.get());
  const auto world_result
    = physics_system_->Worlds().CreateWorld(world::WorldDesc {});
  CHECK_F(world_result.has_value(),
    "PhysicsModule failed to create simulation world.");
  world_id_ = world_result.value();
  CHECK_F(world_id_ != kInvalidWorldId,
    "PhysicsModule created an invalid world handle.");
}

auto PhysicsModule::GetBodyApi() noexcept -> system::IBodyApi&
{
  CHECK_NOTNULL_F(physics_system_.get());
  return physics_system_->Bodies();
}

auto PhysicsModule::GetCharacterApi() noexcept -> system::ICharacterApi&
{
  CHECK_NOTNULL_F(physics_system_.get());
  return physics_system_->Characters();
}

auto PhysicsModule::GetAggregateApi() noexcept -> system::IAggregateApi*
{
  CHECK_NOTNULL_F(physics_system_.get());
  return physics_system_->Aggregates();
}

auto PhysicsModule::GetEventApi() noexcept -> system::IEventApi&
{
  CHECK_NOTNULL_F(physics_system_.get());
  return physics_system_->Events();
}

auto PhysicsModule::GetQueryApi() noexcept -> system::IQueryApi&
{
  CHECK_NOTNULL_F(physics_system_.get());
  return physics_system_->Queries();
}

auto PhysicsModule::GetWorldId() const noexcept -> WorldId { return world_id_; }

auto PhysicsModule::IsNodeInObservedScene(
  const scene::NodeHandle& node_handle) const noexcept -> bool
{
  if (observed_scene_ == nullptr || !node_handle.IsValid()) {
    return false;
  }
  return node_handle.GetSceneId() == observed_scene_->GetId();
}

auto PhysicsModule::GetBodyTypeForBodyId(BodyId body_id) const
  -> std::optional<body::BodyType>
{
  const auto it = body_to_binding_.find(body_id);
  if (it == body_to_binding_.end()) {
    return std::nullopt;
  }
  CHECK_NOTNULL_F(bindings_.get());
  const auto* binding = bindings_->TryGet(it->second);
  if (binding == nullptr) {
    return std::nullopt;
  }
  return binding->body_type;
}

auto PhysicsModule::RegisterNodeBodyMapping(
  const scene::NodeHandle& node_handle, const BodyId body_id,
  const body::BodyType body_type) -> void
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
  DCHECK_F(!node_to_character_binding_.contains(node_handle),
    "Physics authority conflict: node already has a character mapping.");
  if (node_to_character_binding_.contains(node_handle)) {
    return;
  }
  DCHECK_F(!node_to_aggregate_binding_.contains(node_handle),
    "Physics authority conflict: node already has an aggregate mapping.");
  if (node_to_aggregate_binding_.contains(node_handle)) {
    return;
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

auto PhysicsModule::GetBodyIdForNode(const scene::NodeHandle& node_handle) const
  -> BodyId
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

auto PhysicsModule::HasBodyForNode(const scene::NodeHandle& node_handle) const
  -> bool
{
  return GetBodyIdForNode(node_handle) != kInvalidBodyId;
}

auto PhysicsModule::GetNodeForBodyId(const BodyId body_id) const
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

auto PhysicsModule::RegisterNodeCharacterMapping(
  const scene::NodeHandle& node_handle, const CharacterId character_id) -> void
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
  DCHECK_F(!node_to_binding_.contains(node_handle),
    "Physics authority conflict: node already has a rigid body mapping.");
  if (node_to_binding_.contains(node_handle)) {
    return;
  }
  DCHECK_F(!node_to_aggregate_binding_.contains(node_handle),
    "Physics authority conflict: node already has an aggregate mapping.");
  if (node_to_aggregate_binding_.contains(node_handle)) {
    return;
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

auto PhysicsModule::GetCharacterIdForNode(
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

auto PhysicsModule::HasCharacterForNode(
  const scene::NodeHandle& node_handle) const -> bool
{
  return GetCharacterIdForNode(node_handle) != kInvalidCharacterId;
}

auto PhysicsModule::GetNodeForCharacterId(const CharacterId character_id) const
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

auto PhysicsModule::RegisterNodeAggregateMapping(
  const scene::NodeHandle& node_handle, const AggregateId aggregate_id,
  const aggregate::AggregateAuthority authority) -> void
{
  if (!node_handle.IsValid() || aggregate_id == kInvalidAggregateId) {
    return;
  }
  DCHECK_F(IsNodeInObservedScene(node_handle),
    "Aggregate mapping contract violated: node must belong to currently "
    "observed scene.");
  if (!IsNodeInObservedScene(node_handle)) {
    return;
  }

  if (const auto existing_node_it
    = node_to_aggregate_binding_.find(node_handle);
    existing_node_it != node_to_aggregate_binding_.end()) {
    (void)RemoveAggregateBinding(existing_node_it->second);
  }
  if (const auto existing_aggregate_it
    = aggregate_to_binding_.find(aggregate_id);
    existing_aggregate_it != aggregate_to_binding_.end()) {
    (void)RemoveAggregateBinding(existing_aggregate_it->second);
  }
  DCHECK_F(!node_to_binding_.contains(node_handle),
    "Physics authority conflict: node already has a rigid body mapping.");
  if (node_to_binding_.contains(node_handle)) {
    return;
  }
  DCHECK_F(!node_to_character_binding_.contains(node_handle),
    "Physics authority conflict: node already has a character mapping.");
  if (node_to_character_binding_.contains(node_handle)) {
    return;
  }

  CHECK_NOTNULL_F(aggregate_bindings_.get());
  const auto binding_handle = aggregate_bindings_->Insert(AggregateBinding {
    .world_id = world_id_,
    .aggregate_id = aggregate_id,
    .authority = authority,
    .node_handle = node_handle,
  });
  node_to_aggregate_binding_.insert_or_assign(node_handle, binding_handle);
  aggregate_to_binding_.insert_or_assign(aggregate_id, binding_handle);
}

auto PhysicsModule::GetAggregateIdForNode(
  const scene::NodeHandle& node_handle) const -> AggregateId
{
  const auto it = node_to_aggregate_binding_.find(node_handle);
  if (it == node_to_aggregate_binding_.end()) {
    return kInvalidAggregateId;
  }
  CHECK_NOTNULL_F(aggregate_bindings_.get());
  const auto* binding = aggregate_bindings_->TryGet(it->second);
  if (binding == nullptr) {
    return kInvalidAggregateId;
  }
  return binding->aggregate_id;
}

auto PhysicsModule::HasAggregateForNode(
  const scene::NodeHandle& node_handle) const -> bool
{
  return GetAggregateIdForNode(node_handle) != kInvalidAggregateId;
}

auto PhysicsModule::GetNodeForAggregateId(const AggregateId aggregate_id) const
  -> std::optional<scene::NodeHandle>
{
  const auto it = aggregate_to_binding_.find(aggregate_id);
  if (it == aggregate_to_binding_.end()) {
    return std::nullopt;
  }
  CHECK_NOTNULL_F(aggregate_bindings_.get());
  const auto* binding = aggregate_bindings_->TryGet(it->second);
  if (binding == nullptr || !binding->node_handle.IsValid()) {
    return std::nullopt;
  }
  return binding->node_handle;
}

auto PhysicsModule::GetAggregateAuthorityForAggregateId(
  const AggregateId aggregate_id) const
  -> std::optional<aggregate::AggregateAuthority>
{
  const auto it = aggregate_to_binding_.find(aggregate_id);
  if (it == aggregate_to_binding_.end()) {
    return std::nullopt;
  }
  CHECK_NOTNULL_F(aggregate_bindings_.get());
  const auto* binding = aggregate_bindings_->TryGet(it->second);
  if (binding == nullptr) {
    return std::nullopt;
  }
  return binding->authority;
}

auto PhysicsModule::ApplyWorldPoseToNode(const scene::NodeHandle& node_handle,
  const Vec3& world_position, const Quat& world_rotation) -> bool
{
  if (observed_scene_ == nullptr || !node_handle.IsValid()) {
    return false;
  }
  auto node = observed_scene_->GetNode(node_handle);
  if (!node.has_value() || !node->IsValid()) {
    return false;
  }
  if (node_to_character_binding_.contains(node_handle)) {
    expected_character_transform_updates_.insert(node_handle);
  }

  const auto [local_position, local_rotation]
    = ToLocalPose(*node, world_position, world_rotation);
  const auto pos_ok = node->GetTransform().SetLocalPosition(local_position);
  const auto rot_ok = node->GetTransform().SetLocalRotation(local_rotation);
  return pos_ok && rot_ok;
}

auto PhysicsModule::ConsumeSceneEvents() -> std::vector<ScenePhysicsEvent>
{
  std::vector<ScenePhysicsEvent> drained {};
  drained.swap(scene_events_);
  return drained;
}

auto PhysicsModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept
  -> bool
{
  engine_ = engine;
  CHECK_NOTNULL_F(engine_.get());

  if (physics_system_ != nullptr) {
    CHECK_F(world_id_ != kInvalidWorldId,
      "Injected PhysicsModule must have a valid world id.");
    return true;
  }

  auto physics_system_result = CreatePhysicsSystem();
  CHECK_F(physics_system_result.has_value(),
    "PhysicsModule requires a valid physics system backend.");
  physics_system_ = std::move(physics_system_result.value());
  CHECK_NOTNULL_F(physics_system_.get());

  const auto world_result
    = physics_system_->Worlds().CreateWorld(world::WorldDesc {});
  CHECK_F(world_result.has_value(),
    "PhysicsModule failed to create simulation world.");
  world_id_ = world_result.value();
  CHECK_F(world_id_ != kInvalidWorldId,
    "PhysicsModule created an invalid world handle.");

  return true;
}

auto PhysicsModule::OnShutdown() noexcept -> void
{
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_NOTNULL_F(bindings_.get());
  CHECK_NOTNULL_F(character_bindings_.get());
  CHECK_NOTNULL_F(aggregate_bindings_.get());
  CHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  if (observed_scene_ != nullptr) {
    observed_scene_->UnregisterObserver(
      observer_ptr<scene::ISceneObserver> { this });
    observed_scene_ = nullptr;
  }

  DestroyAllTrackedBodies();
  DestroyAllTrackedCharacters();
  DestroyAllTrackedAggregates();

  [[maybe_unused]] const auto result
    = physics_system_->Worlds().DestroyWorld(world_id_);

  world_id_ = kInvalidWorldId;
  physics_system_.reset();
  engine_ = nullptr;
  pending_transform_updates_.clear();
  expected_character_transform_updates_.clear();
  scene_events_.clear();
  bindings_->Clear();
  character_bindings_->Clear();
  aggregate_bindings_->Clear();
}

auto PhysicsModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(context.get());
  DCHECK_EQ_F(context->GetCurrentPhase(), core::PhaseId::kGameplay,
    "PhysicsModule::OnGameplay must run only during kGameplay.");
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  SyncSceneObserver(context);

  const auto flush_result
    = physics_system_->Bodies().FlushStructuralChanges(world_id_);
  CHECK_F(flush_result.has_value(),
    "PhysicsModule failed to flush deferred body structural changes.");
  if (auto* aggregate_api = physics_system_->Aggregates();
    aggregate_api != nullptr) {
    const auto aggregate_flush_result
      = aggregate_api->FlushStructuralChanges(world_id_);
    CHECK_F(aggregate_flush_result.has_value(),
      "PhysicsModule failed to flush deferred aggregate structural changes.");
  }
  if (auto* vehicle_api = physics_system_->Vehicles(); vehicle_api != nullptr) {
    const auto vehicle_flush_result
      = vehicle_api->FlushStructuralChanges(world_id_);
    CHECK_F(vehicle_flush_result.has_value(),
      "PhysicsModule failed to flush deferred vehicle structural changes.");
  }
  if (auto* articulation_api = physics_system_->Articulations();
    articulation_api != nullptr) {
    const auto articulation_flush_result
      = articulation_api->FlushStructuralChanges(world_id_);
    CHECK_F(articulation_flush_result.has_value(),
      "PhysicsModule failed to flush deferred articulation structural "
      "changes.");
  }
  if (auto* soft_body_api = physics_system_->SoftBodies();
    soft_body_api != nullptr) {
    const auto soft_body_flush_result
      = soft_body_api->FlushStructuralChanges(world_id_);
    CHECK_F(soft_body_flush_result.has_value(),
      "PhysicsModule failed to flush deferred soft-body structural changes.");
  }

  const auto scene = context->GetScene();
  if (scene == nullptr) {
    pending_transform_updates_.clear();
    co_return;
  }

  const auto fixed_delta_seconds = FixedDeltaSeconds(context);
  auto& body_api = physics_system_->Bodies();
  std::vector<BodyId> batched_body_ids {};
  std::vector<Vec3> batched_positions {};
  std::vector<Quat> batched_rotations {};
  batched_body_ids.reserve(pending_transform_updates_.size());
  batched_positions.reserve(pending_transform_updates_.size());
  batched_rotations.reserve(pending_transform_updates_.size());
  for (const auto& node_handle : pending_transform_updates_) {
    diagnostics_.gameplay_push_attempts += 1;

    const auto node_it = node_to_binding_.find(node_handle);
    if (node_it == node_to_binding_.end()) {
      diagnostics_.gameplay_push_skipped_untracked += 1;
      continue;
    }

    CHECK_NOTNULL_F(bindings_.get());
    const auto* binding = bindings_->TryGet(node_it->second);
    if (binding == nullptr) {
      diagnostics_.gameplay_push_skipped_untracked += 1;
      continue;
    }
    if (binding->body_type != body::BodyType::kKinematic) {
      diagnostics_.gameplay_push_skipped_non_kinematic += 1;
      continue;
    }

    const auto node = scene->GetNode(node_handle);
    if (!node.has_value() || !node->IsValid()) {
      diagnostics_.gameplay_push_skipped_missing_node += 1;
      continue;
    }

    const auto position
      = node->GetTransform().GetWorldPosition().value_or(Vec3(0.0F));
    const auto rotation = node->GetTransform().GetWorldRotation().value_or(
      Quat(1.0F, 0.0F, 0.0F, 0.0F));
    batched_body_ids.push_back(binding->body_id);
    batched_positions.push_back(position);
    batched_rotations.push_back(rotation);
  }

  if (batched_body_ids.empty()) {
    pending_transform_updates_.clear();
    co_return;
  }

  if (fixed_delta_seconds > 0.0F) {
    const auto batch_result
      = body_api.MoveKinematicBatch(world_id_, batched_body_ids,
        batched_positions, batched_rotations, fixed_delta_seconds);
    CHECK_F(batch_result.has_value(),
      "PhysicsModule failed to batch-push kinematic body poses to physics.");
    CHECK_EQ_F(batch_result.value(), batched_body_ids.size(),
      "PhysicsModule batch kinematic push count mismatch.");
    diagnostics_.gameplay_push_success += batch_result.value();
  } else {
    for (size_t i = 0; i < batched_body_ids.size(); ++i) {
      const auto result = body_api.SetBodyPose(world_id_, batched_body_ids[i],
        batched_positions[i], batched_rotations[i]);
      CHECK_F(result.has_value(),
        "PhysicsModule failed to push kinematic body pose to physics.");
      diagnostics_.gameplay_push_success += 1;
    }
  }
  pending_transform_updates_.clear();

  co_return;
}

auto PhysicsModule::OnFixedSimulation(
  observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  DCHECK_NOTNULL_F(context.get());
  DCHECK_EQ_F(context->GetCurrentPhase(), core::PhaseId::kFixedSimulation,
    "PhysicsModule::OnFixedSimulation must run only during kFixedSimulation.");
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  const auto fixed_delta_seconds = FixedDeltaSeconds(context);
  if (fixed_delta_seconds <= 0.0F) {
    co_return;
  }

  const auto step_result = physics_system_->Worlds().Step(
    world_id_, fixed_delta_seconds, 1, fixed_delta_seconds);
  CHECK_F(step_result.has_value(),
    "PhysicsModule failed to step physics world in fixed simulation phase.");

  co_return;
}

auto PhysicsModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(context.get());
  DCHECK_EQ_F(context->GetCurrentPhase(), core::PhaseId::kSceneMutation,
    "PhysicsModule::OnSceneMutation must run only during kSceneMutation.");
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  SyncSceneObserver(context);

  if (context->GetScene() == nullptr) {
    expected_character_transform_updates_.clear();
    co_return;
  }
  if (body_to_binding_.empty()) {
    DrainPhysicsEvents();
    expected_character_transform_updates_.clear();
    co_return;
  }

  auto& world_api = physics_system_->Worlds();
  std::vector<system::ActiveBodyTransform> active_transforms(
    body_to_binding_.size());

  const auto result
    = world_api.GetActiveBodyTransforms(world_id_, active_transforms);
  if (!result.has_value()) {
    DrainPhysicsEvents();
    expected_character_transform_updates_.clear();
    co_return;
  }

  const auto count = std::min(result.value(), active_transforms.size());
  for (size_t index = 0; index < count; ++index) {
    diagnostics_.scene_pull_attempts += 1;

    const auto& transform = active_transforms[index];
    const auto body_type = GetBodyTypeForBodyId(transform.body_id);
    if (!body_type.has_value() || *body_type != body::BodyType::kDynamic) {
      diagnostics_.scene_pull_skipped_non_dynamic += 1;
      continue;
    }

    const auto node_handle = GetNodeForBodyId(transform.body_id);
    if (!node_handle.has_value()) {
      diagnostics_.scene_pull_skipped_unmapped += 1;
      continue;
    }

    if (!ApplyWorldPoseToNode(
          *node_handle, transform.position, transform.rotation)) {
      diagnostics_.scene_pull_skipped_missing_node += 1;
      continue;
    }
    diagnostics_.scene_pull_success += 1;
  }

  DrainPhysicsEvents();
  expected_character_transform_updates_.clear();

  co_return;
}

auto PhysicsModule::OnTransformChanged(
  const scene::NodeHandle& node_handle) noexcept -> void
{
  if (expected_character_transform_updates_.contains(node_handle)) {
    return;
  }
  DCHECK_F(!node_to_character_binding_.contains(node_handle),
    "Character authority contract violated: scene transform writes on "
    "character-managed nodes are not allowed. Use CharacterFacade::Move.");
  if (node_to_character_binding_.contains(node_handle)) {
    return;
  }
  if (node_to_aggregate_binding_.contains(node_handle)) {
    return;
  }
  if (node_to_binding_.contains(node_handle)) {
    pending_transform_updates_.insert(node_handle);
  }
}

auto PhysicsModule::OnNodeDestroyed(
  const scene::NodeHandle& node_handle) noexcept -> void
{
  DCHECK_NOTNULL_F(physics_system_.get());
  DCHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  pending_transform_updates_.erase(node_handle);
  expected_character_transform_updates_.erase(node_handle);

  const auto it = node_to_binding_.find(node_handle);
  if (it == node_to_binding_.end()) {
  } else {
    CHECK_NOTNULL_F(bindings_.get());
    const auto* binding = bindings_->TryGet(it->second);
    if (binding != nullptr) {
      const auto body_id = binding->body_id;
      const auto result
        = physics_system_->Bodies().DestroyBody(world_id_, body_id);
      CHECK_F(result.has_value(),
        "PhysicsModule failed to destroy tracked body on node destruction.");
      (void)RemoveBinding(it->second);
    }
  }

  const auto character_it = node_to_character_binding_.find(node_handle);
  if (character_it != node_to_character_binding_.end()) {
    CHECK_NOTNULL_F(character_bindings_.get());
    const auto* character_binding
      = character_bindings_->TryGet(character_it->second);
    if (character_binding != nullptr) {
      const auto character_id = character_binding->character_id;
      const auto character_result
        = physics_system_->Characters().DestroyCharacter(
          world_id_, character_id);
      CHECK_F(character_result.has_value(),
        "PhysicsModule failed to destroy tracked character on node "
        "destruction.");
      (void)RemoveCharacterBinding(character_it->second);
    }
  }

  const auto aggregate_it = node_to_aggregate_binding_.find(node_handle);
  if (aggregate_it == node_to_aggregate_binding_.end()) {
    return;
  }
  CHECK_NOTNULL_F(aggregate_bindings_.get());
  const auto* aggregate_binding
    = aggregate_bindings_->TryGet(aggregate_it->second);
  if (aggregate_binding == nullptr) {
    return;
  }
  const auto aggregate_result = DestroyAggregateByDomain(
    aggregate_binding->aggregate_id, aggregate_binding->authority);
  CHECK_F(aggregate_result.has_value(),
    "PhysicsModule failed to destroy tracked aggregate on node destruction.");
  (void)RemoveAggregateBinding(aggregate_it->second);
}

auto PhysicsModule::SyncSceneObserver(
  observer_ptr<engine::FrameContext> context) -> void
{
  CHECK_NOTNULL_F(bindings_.get());
  CHECK_NOTNULL_F(character_bindings_.get());
  if (context == nullptr) {
    return;
  }

  const auto scene = context->GetScene();
  if (scene == observed_scene_) {
    return;
  }

  const auto had_previous_scene = observed_scene_ != nullptr;

  if (observed_scene_ != nullptr) {
    observed_scene_->UnregisterObserver(
      observer_ptr<scene::ISceneObserver> { this });
  }

  if (had_previous_scene && !body_to_binding_.empty()) {
    DestroyAllTrackedBodies();
  }
  if (had_previous_scene && !character_to_binding_.empty()) {
    DestroyAllTrackedCharacters();
  }
  if (had_previous_scene && !aggregate_to_binding_.empty()) {
    DestroyAllTrackedAggregates();
  }

  EnsureBindingCapacity(EstimateBindingReserve(scene));
  // DestroyAllTrackedBodies() already clears bindings and indices.
  pending_transform_updates_.clear();
  expected_character_transform_updates_.clear();
  observed_scene_ = scene;

  if (observed_scene_ != nullptr) {
    [[maybe_unused]] const auto registered = observed_scene_->RegisterObserver(
      observer_ptr<scene::ISceneObserver> { this },
      scene::SceneMutationMask::kTransformChanged
        | scene::SceneMutationMask::kNodeDestroyed);
    DCHECK_F(registered,
      "PhysicsModule failed to register scene observer for mutations.");
  }
}

auto PhysicsModule::RemoveBinding(const ResourceHandle& binding_handle) -> bool
{
  CHECK_NOTNULL_F(bindings_.get());
  const auto* binding = bindings_->TryGet(binding_handle);
  if (binding == nullptr) {
    return false;
  }

  body_to_binding_.erase(binding->body_id);
  node_to_binding_.erase(binding->node_handle);
  return bindings_->Erase(binding_handle) > 0;
}

auto PhysicsModule::RemoveCharacterBinding(const ResourceHandle& binding_handle)
  -> bool
{
  CHECK_NOTNULL_F(character_bindings_.get());
  const auto* binding = character_bindings_->TryGet(binding_handle);
  if (binding == nullptr) {
    return false;
  }

  character_to_binding_.erase(binding->character_id);
  node_to_character_binding_.erase(binding->node_handle);
  return character_bindings_->Erase(binding_handle) > 0;
}

auto PhysicsModule::RemoveAggregateBinding(const ResourceHandle& binding_handle)
  -> bool
{
  CHECK_NOTNULL_F(aggregate_bindings_.get());
  const auto* binding = aggregate_bindings_->TryGet(binding_handle);
  if (binding == nullptr) {
    return false;
  }

  aggregate_to_binding_.erase(binding->aggregate_id);
  node_to_aggregate_binding_.erase(binding->node_handle);
  return aggregate_bindings_->Erase(binding_handle) > 0;
}

auto PhysicsModule::EstimateBindingReserve(observer_ptr<scene::Scene> scene)
  -> std::size_t
{
  if (scene == nullptr) {
    return kMinBindingReserve;
  }
  return std::max<std::size_t>(kMinBindingReserve, scene->GetNodes().Size());
}

auto PhysicsModule::EnsureBindingCapacity(const std::size_t min_reserve) -> void
{
  CHECK_NOTNULL_F(bindings_.get());
  CHECK_NOTNULL_F(character_bindings_.get());
  CHECK_NOTNULL_F(aggregate_bindings_.get());
  bindings_->Reserve(std::max(min_reserve, kMinBindingReserve));
  character_bindings_->Reserve(std::max(min_reserve, kMinBindingReserve));
  aggregate_bindings_->Reserve(std::max(min_reserve, kMinBindingReserve));
}

auto PhysicsModule::DestroyAllTrackedBodies() -> void
{
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_NOTNULL_F(bindings_.get());
  CHECK_F(world_id_ != kInvalidWorldId,
    "PhysicsModule world id must be valid while destroying tracked bodies.");

  auto& body_api = physics_system_->Bodies();
  for (const auto& [body_id, binding_handle] : body_to_binding_) {
    (void)binding_handle;
    const auto result = body_api.DestroyBody(world_id_, body_id);
    CHECK_F(result.has_value(),
      "PhysicsModule failed to destroy tracked body during shutdown.");
  }

  body_to_binding_.clear();
  node_to_binding_.clear();
  bindings_->Clear();
}

auto PhysicsModule::DestroyAllTrackedCharacters() -> void
{
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_NOTNULL_F(character_bindings_.get());
  CHECK_F(world_id_ != kInvalidWorldId,
    "PhysicsModule world id must be valid while destroying tracked "
    "characters.");

  auto& character_api = physics_system_->Characters();
  for (const auto& [character_id, binding_handle] : character_to_binding_) {
    (void)binding_handle;
    const auto result = character_api.DestroyCharacter(world_id_, character_id);
    CHECK_F(result.has_value(),
      "PhysicsModule failed to destroy tracked character during shutdown.");
  }

  character_to_binding_.clear();
  node_to_character_binding_.clear();
  character_bindings_->Clear();
}

auto PhysicsModule::DestroyAggregateByDomain(const AggregateId aggregate_id,
  const aggregate::AggregateAuthority authority) -> PhysicsResult<void>
{
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_F(world_id_ != kInvalidWorldId,
    "PhysicsModule world id must be valid while destroying tracked "
    "aggregates.");

  const auto is_unknown = [](const PhysicsError error) {
    return error == PhysicsError::kInvalidArgument;
  };

  if (authority == aggregate::AggregateAuthority::kCommand) {
    if (auto* vehicle_api = physics_system_->Vehicles();
      vehicle_api != nullptr) {
      const auto vehicle_result
        = vehicle_api->DestroyVehicle(world_id_, aggregate_id);
      if (vehicle_result.has_value()) {
        return PhysicsResult<void>::Ok();
      }
      if (!is_unknown(vehicle_result.error())) {
        return Err(vehicle_result.error());
      }
    }
  }

  if (auto* articulation_api = physics_system_->Articulations();
    articulation_api != nullptr) {
    const auto articulation_result
      = articulation_api->DestroyArticulation(world_id_, aggregate_id);
    if (articulation_result.has_value()) {
      return PhysicsResult<void>::Ok();
    }
    if (!is_unknown(articulation_result.error())) {
      return Err(articulation_result.error());
    }
  }

  if (auto* soft_body_api = physics_system_->SoftBodies();
    soft_body_api != nullptr) {
    const auto soft_body_result
      = soft_body_api->DestroySoftBody(world_id_, aggregate_id);
    if (soft_body_result.has_value()) {
      return PhysicsResult<void>::Ok();
    }
    if (!is_unknown(soft_body_result.error())) {
      return Err(soft_body_result.error());
    }
  }

  if (auto* aggregate_api = physics_system_->Aggregates();
    aggregate_api != nullptr) {
    return aggregate_api->DestroyAggregate(world_id_, aggregate_id);
  }

  return Err(PhysicsError::kNotImplemented);
}

auto PhysicsModule::DestroyAllTrackedAggregates() -> void
{
  CHECK_NOTNULL_F(aggregate_bindings_.get());
  for (const auto& [aggregate_id, binding_handle] : aggregate_to_binding_) {
    (void)aggregate_id;
    const auto* binding = aggregate_bindings_->TryGet(binding_handle);
    if (binding == nullptr) {
      continue;
    }
    const auto result
      = DestroyAggregateByDomain(binding->aggregate_id, binding->authority);
    CHECK_F(result.has_value(),
      "PhysicsModule failed to destroy tracked aggregate during shutdown.");
  }

  aggregate_to_binding_.clear();
  node_to_aggregate_binding_.clear();
  aggregate_bindings_->Clear();
}

auto PhysicsModule::DrainPhysicsEvents() -> void
{
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  auto& event_api = physics_system_->Events();
  const auto pending_result = event_api.GetPendingEventCount(world_id_);
  if (!pending_result.has_value() || pending_result.value() == 0U) {
    return;
  }

  std::vector<events::PhysicsEvent> raw_events(pending_result.value());
  diagnostics_.event_drain_calls += 1;
  const auto drain_result = event_api.DrainEvents(world_id_, raw_events);
  if (!drain_result.has_value()) {
    return;
  }

  const auto drained_count = std::min(drain_result.value(), raw_events.size());
  diagnostics_.event_drain_count += drained_count;
  scene_events_.reserve(scene_events_.size() + drained_count);
  for (size_t i = 0; i < drained_count; ++i) {
    const auto& raw_event = raw_events[i];
    scene_events_.push_back(ScenePhysicsEvent {
      .type = raw_event.type,
      .node_a = GetNodeForBodyId(raw_event.body_a),
      .node_b = GetNodeForBodyId(raw_event.body_b),
      .raw_event = raw_event,
    });
  }
}

} // namespace oxygen::physics
