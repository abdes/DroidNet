//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Physics/Physics.h>
#include <Oxygen/Physics/World/WorldDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::physics {

PhysicsModule::PhysicsModule(engine::ModulePriority priority)
  : priority_(priority)
  , bindings_(std::make_unique<PhysicsBindingTable>(
      kBindingResourceType, kMinBindingReserve))
{
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

auto PhysicsModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept
  -> bool
{
  engine_ = engine;
  CHECK_NOTNULL_F(engine_.get());

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
  CHECK_NOTNULL_F(context.get());
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  SyncSceneObserver(context);

  const auto scene = context->GetScene();
  if (scene == nullptr) {
    pending_transform_updates_.clear();
    co_return;
  }

  auto& body_api = physics_system_->Bodies();
  for (const auto& node_handle : pending_transform_updates_) {
    const auto body_id = GetBodyIdForNode(node_handle);
    if (body_id == kInvalidBodyId) {
      continue;
    }

    const auto node = scene->GetNode(node_handle);
    if (!node.has_value() || !node->IsValid()) {
      continue;
    }

    const auto position
      = node->GetTransform().GetWorldPosition().value_or(Vec3(0.0F));
    const auto rotation = node->GetTransform().GetWorldRotation().value_or(
      Quat(1.0F, 0.0F, 0.0F, 0.0F));

    [[maybe_unused]] const auto result
      = body_api.SetBodyPose(world_id_, body_id, position, rotation);
  }
  pending_transform_updates_.clear();

  co_return;
}

auto PhysicsModule::OnFixedSimulation(
  observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  CHECK_NOTNULL_F(context.get());
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_F(
    world_id_ != kInvalidWorldId, "PhysicsModule world id must be valid.");

  const auto timing = context->GetModuleTimingData();
  using Seconds = std::chrono::duration<float>;
  const auto fixed_delta_seconds
    = std::chrono::duration_cast<Seconds>(timing.fixed_delta_time.get())
        .count();
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
  CHECK_NOTNULL_F(context.get());
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
    const auto& transform = active_transforms[index];

    const auto node_handle = GetNodeForBodyId(transform.body_id);
    if (!node_handle.has_value()) {
      continue;
    }

    const auto node = scene->GetNode(*node_handle);
    if (!node.has_value() || !node->IsValid()) {
      continue;
    }

    [[maybe_unused]] const auto pos_ok
      = node->GetTransform().SetLocalPosition(transform.position);
    [[maybe_unused]] const auto rot_ok
      = node->GetTransform().SetLocalRotation(transform.rotation);
  }

  co_return;
}

auto PhysicsModule::OnTransformChanged(
  const scene::NodeHandle& node_handle) noexcept -> void
{
  pending_transform_updates_.insert(node_handle);
}

auto PhysicsModule::OnNodeDestroyed(
  const scene::NodeHandle& node_handle) noexcept -> void
{
  CHECK_NOTNULL_F(physics_system_.get());
  CHECK_F(
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
  [[maybe_unused]] const auto result
    = physics_system_->Bodies().DestroyBody(world_id_, body_id);
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

  if (observed_scene_ != nullptr) {
    observed_scene_->UnregisterObserver(
      observer_ptr<scene::ISceneObserver> { this });
  }

  if (!body_to_binding_.empty()) {
    DestroyAllTrackedBodies();
  }

  EnsureBindingCapacity(EstimateBindingReserve(scene));
  bindings_->Clear();
  pending_transform_updates_.clear();
  observed_scene_ = scene;

  if (observed_scene_ != nullptr) {
    [[maybe_unused]] const auto registered = observed_scene_->RegisterObserver(
      observer_ptr<scene::ISceneObserver> { this },
      scene::SceneMutationMask::kTransformChanged
        | scene::SceneMutationMask::kNodeDestroyed);
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
    [[maybe_unused]] const auto result
      = body_api.DestroyBody(world_id_, body_id);
  }

  body_to_binding_.clear();
  node_to_binding_.clear();
  bindings_->Clear();
}

} // namespace oxygen::physics
