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

  auto InverseScaleSafe(const Vec3& scale) -> Vec3
  {
    constexpr float kScaleEpsilon = 1.0e-6F;
    auto inv = Vec3 { 1.0F, 1.0F, 1.0F };
    inv.x = std::abs(scale.x) > kScaleEpsilon ? 1.0F / scale.x : 1.0F;
    inv.y = std::abs(scale.y) > kScaleEpsilon ? 1.0F / scale.y : 1.0F;
    inv.z = std::abs(scale.z) > kScaleEpsilon ? 1.0F / scale.z : 1.0F;
    return inv;
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

    const auto parent_world_position
      = parent->GetTransform().GetWorldPosition().value_or(Vec3 { 0.0F });
    const auto parent_world_rotation
      = parent->GetTransform().GetWorldRotation().value_or(
        Quat { 1.0F, 0.0F, 0.0F, 0.0F });
    const auto parent_world_scale
      = parent->GetTransform().GetWorldScale().value_or(Vec3 { 1.0F });
    const auto inverse_parent_rotation = glm::inverse(parent_world_rotation);
    const auto inverse_parent_scale = InverseScaleSafe(parent_world_scale);
    const auto relative_position = world_position - parent_world_position;

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
{
}

PhysicsModule::PhysicsModule(engine::ModulePriority priority,
  std::unique_ptr<system::IPhysicsSystem> physics_system)
  : priority_(priority)
  , physics_system_(std::move(physics_system))
  , bindings_(std::make_unique<PhysicsBindingTable>(
      kBindingResourceType, kMinBindingReserve))
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
  CHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  if (observed_scene_ != nullptr) {
    observed_scene_->UnregisterObserver(
      observer_ptr<scene::ISceneObserver> { this });
    observed_scene_ = nullptr;
  }

  DestroyAllTrackedBodies();

  [[maybe_unused]] const auto result
    = physics_system_->Worlds().DestroyWorld(world_id_);

  world_id_ = kInvalidWorldId;
  physics_system_.reset();
  engine_ = nullptr;
  pending_transform_updates_.clear();
  bindings_->Clear();
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

  const auto scene = context->GetScene();
  if (scene == nullptr) {
    pending_transform_updates_.clear();
    co_return;
  }

  const auto fixed_delta_seconds = FixedDeltaSeconds(context);
  auto& body_api = physics_system_->Bodies();
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

    const auto result = fixed_delta_seconds > 0.0F
      ? body_api.MoveKinematic(
          world_id_, binding->body_id, position, rotation, fixed_delta_seconds)
      : body_api.SetBodyPose(world_id_, binding->body_id, position, rotation);
    CHECK_F(result.has_value(),
      "PhysicsModule failed to push kinematic body pose to physics.");
    diagnostics_.gameplay_push_success += 1;
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

  const auto scene = context->GetScene();
  if (scene == nullptr || body_to_binding_.empty()) {
    co_return;
  }

  auto& world_api = physics_system_->Worlds();
  std::vector<system::ActiveBodyTransform> active_transforms(
    body_to_binding_.size());

  const auto result
    = world_api.GetActiveBodyTransforms(world_id_, active_transforms);
  if (!result.has_value()) {
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

    auto node = scene->GetNode(*node_handle);
    if (!node.has_value() || !node->IsValid()) {
      diagnostics_.scene_pull_skipped_missing_node += 1;
      continue;
    }

    const auto [local_position, local_rotation]
      = ToLocalPose(*node, transform.position, transform.rotation);
    [[maybe_unused]] const auto pos_ok
      = node->GetTransform().SetLocalPosition(local_position);
    [[maybe_unused]] const auto rot_ok
      = node->GetTransform().SetLocalRotation(local_rotation);
    diagnostics_.scene_pull_success += 1;
  }

  co_return;
}

auto PhysicsModule::OnTransformChanged(
  const scene::NodeHandle& node_handle) noexcept -> void
{
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

  const auto it = node_to_binding_.find(node_handle);
  if (it == node_to_binding_.end()) {
    return;
  }
  CHECK_NOTNULL_F(bindings_.get());
  const auto* binding = bindings_->TryGet(it->second);
  if (binding == nullptr) {
    return;
  }
  const auto body_id = binding->body_id;
  const auto result = physics_system_->Bodies().DestroyBody(world_id_, body_id);
  CHECK_F(result.has_value(),
    "PhysicsModule failed to destroy tracked body on node destruction.");
  (void)RemoveBinding(it->second);
}

auto PhysicsModule::SyncSceneObserver(
  observer_ptr<engine::FrameContext> context) -> void
{
  CHECK_NOTNULL_F(bindings_.get());
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

  EnsureBindingCapacity(EstimateBindingReserve(scene));
  // DestroyAllTrackedBodies() already clears bindings and indices.
  pending_transform_updates_.clear();
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
  bindings_->Reserve(std::max(min_reserve, kMinBindingReserve));
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

} // namespace oxygen::physics
