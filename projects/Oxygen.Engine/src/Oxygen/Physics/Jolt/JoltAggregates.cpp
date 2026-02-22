//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/PhysicsSystem.h>

#include <Oxygen/Physics/Jolt/JoltAggregates.h>

namespace {

auto AggregateUnknown() -> oxygen::ErrValue<oxygen::physics::PhysicsError>
{
  // Canonical aggregate-id error across this domain.
  return oxygen::Err(oxygen::physics::PhysicsError::kInvalidArgument);
}

} // namespace

oxygen::physics::jolt::JoltAggregates::JoltAggregates(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltAggregates::CreateAggregate(
  const WorldId world_id) -> PhysicsResult<AggregateId>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  if (next_aggregate_id_ == std::numeric_limits<uint32_t>::max()) {
    return Err(PhysicsError::kNotInitialized);
  }

  const auto aggregate_id = AggregateId { next_aggregate_id_++ };
  aggregates_.emplace(aggregate_id, AggregateState { .world_id = world_id });
  NoteStructuralChange(world_id);
  return Ok(aggregate_id);
}

auto oxygen::physics::jolt::JoltAggregates::DestroyAggregate(
  const WorldId world_id, const AggregateId aggregate_id) -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = aggregates_.find(aggregate_id);
  if (it == aggregates_.end()) {
    return AggregateUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  aggregates_.erase(it);
  NoteStructuralChange(world_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltAggregates::AddMemberBody(
  const WorldId world_id, const AggregateId aggregate_id, const BodyId body_id)
  -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }
  auto* world = world_.get();
  if (world == nullptr || !world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = aggregates_.find(aggregate_id);
  if (it == aggregates_.end()) {
    return AggregateUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  const auto [_, inserted] = it->second.body_ids.insert(body_id);
  if (!inserted) {
    return Err(PhysicsError::kAlreadyExists);
  }
  NoteStructuralChange(world_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltAggregates::RemoveMemberBody(
  const WorldId world_id, const AggregateId aggregate_id, const BodyId body_id)
  -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = aggregates_.find(aggregate_id);
  if (it == aggregates_.end()) {
    return AggregateUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (it->second.body_ids.erase(body_id) == 0U) {
    return Err(PhysicsError::kBodyNotFound);
  }
  NoteStructuralChange(world_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltAggregates::GetMemberBodies(
  const WorldId world_id, const AggregateId aggregate_id,
  std::span<BodyId> out_body_ids) const -> PhysicsResult<size_t>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = aggregates_.find(aggregate_id);
  if (it == aggregates_.end()) {
    return AggregateUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (out_body_ids.size() < it->second.body_ids.size()) {
    return Err(PhysicsError::kBufferTooSmall);
  }

  size_t i = 0;
  for (const auto body_id : it->second.body_ids) {
    out_body_ids[i++] = body_id;
  }
  return Ok(i);
}

auto oxygen::physics::jolt::JoltAggregates::FlushStructuralChanges(
  const WorldId world_id) -> PhysicsResult<size_t>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  size_t pending_changes = 0U;
  {
    std::scoped_lock lock(mutex_);
    const auto it = pending_structural_changes_.find(world_id);
    if (it != pending_structural_changes_.end()) {
      pending_changes = it->second;
      it->second = 0U;
    }
  }

  if (pending_changes == 0U) {
    return Ok(size_t { 0 });
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto physics_system = world->TryGetPhysicsSystem(world_id);
  if (physics_system == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  physics_system->OptimizeBroadPhase();
  return Ok(pending_changes);
}

auto oxygen::physics::jolt::JoltAggregates::HasWorld(
  const WorldId world_id) const noexcept -> bool
{
  const auto* world = world_.get();
  return world != nullptr && world->TryGetPhysicsSystem(world_id) != nullptr;
}

auto oxygen::physics::jolt::JoltAggregates::NoteStructuralChange(
  const WorldId world_id, const size_t count) -> void
{
  auto& pending = pending_structural_changes_[world_id];
  pending += count;
}
