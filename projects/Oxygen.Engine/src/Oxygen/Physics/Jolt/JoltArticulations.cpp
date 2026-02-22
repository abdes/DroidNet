//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Oxygen/Physics/Jolt/JoltArticulations.h>

namespace {

auto ArticulationUnknown() -> oxygen::ErrValue<oxygen::physics::PhysicsError>
{
  return oxygen::Err(oxygen::physics::PhysicsError::kInvalidArgument);
}

} // namespace

oxygen::physics::jolt::JoltArticulations::JoltArticulations(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltArticulations::CreateArticulation(
  const WorldId world_id, const articulation::ArticulationDesc& desc)
  -> PhysicsResult<AggregateId>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!IsBodyKnown(world_id, desc.root_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  std::scoped_lock lock(mutex_);
  if (next_articulation_id_ == std::numeric_limits<uint32_t>::max()) {
    return Err(PhysicsError::kNotInitialized);
  }

  const auto articulation_id = AggregateId { next_articulation_id_++ };
  articulations_.emplace(articulation_id,
    ArticulationState {
      .world_id = world_id,
      .root_body = desc.root_body_id,
    });
  NoteStructuralChange(world_id);
  return Ok(articulation_id);
}

auto oxygen::physics::jolt::JoltArticulations::DestroyArticulation(
  const WorldId world_id, const AggregateId articulation_id)
  -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = articulations_.find(articulation_id);
  if (it == articulations_.end()) {
    return ArticulationUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  articulations_.erase(it);
  NoteStructuralChange(world_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltArticulations::AddLink(const WorldId world_id,
  const AggregateId articulation_id,
  const articulation::ArticulationLinkDesc& link_desc) -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!IsBodyKnown(world_id, link_desc.parent_body_id)
    || !IsBodyKnown(world_id, link_desc.child_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }
  if (link_desc.parent_body_id == link_desc.child_body_id) {
    return Err(PhysicsError::kInvalidArgument);
  }

  std::scoped_lock lock(mutex_);
  const auto it = articulations_.find(articulation_id);
  if (it == articulations_.end()) {
    return ArticulationUnknown();
  }
  auto& articulation = it->second;
  if (articulation.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto parent_known = link_desc.parent_body_id == articulation.root_body
    || articulation.child_to_parent.contains(link_desc.parent_body_id);
  if (!parent_known) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (link_desc.child_body_id == articulation.root_body) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (articulation.child_to_parent.contains(link_desc.child_body_id)) {
    return Err(PhysicsError::kAlreadyExists);
  }
  if (IsAncestor(
        articulation, link_desc.child_body_id, link_desc.parent_body_id)) {
    return Err(PhysicsError::kInvalidArgument);
  }

  articulation.child_to_parent.emplace(
    link_desc.child_body_id, link_desc.parent_body_id);
  NoteStructuralChange(world_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltArticulations::RemoveLink(
  const WorldId world_id, const AggregateId articulation_id,
  const BodyId child_body_id) -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = articulations_.find(articulation_id);
  if (it == articulations_.end()) {
    return ArticulationUnknown();
  }
  auto& articulation = it->second;
  if (articulation.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (articulation.child_to_parent.erase(child_body_id) == 0U) {
    return Err(PhysicsError::kBodyNotFound);
  }
  NoteStructuralChange(world_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltArticulations::GetRootBody(
  const WorldId world_id, const AggregateId articulation_id) const
  -> PhysicsResult<BodyId>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = articulations_.find(articulation_id);
  if (it == articulations_.end()) {
    return ArticulationUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  return Ok(it->second.root_body);
}

auto oxygen::physics::jolt::JoltArticulations::GetLinkBodies(
  const WorldId world_id, const AggregateId articulation_id,
  std::span<BodyId> out_child_body_ids) const -> PhysicsResult<size_t>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = articulations_.find(articulation_id);
  if (it == articulations_.end()) {
    return ArticulationUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (out_child_body_ids.size() < it->second.child_to_parent.size()) {
    return Err(PhysicsError::kBufferTooSmall);
  }

  size_t i = 0;
  for (const auto& [child_body, _] : it->second.child_to_parent) {
    out_child_body_ids[i++] = child_body;
  }
  return Ok(i);
}

auto oxygen::physics::jolt::JoltArticulations::GetAuthority(
  const WorldId world_id, const AggregateId articulation_id) const
  -> PhysicsResult<aggregate::AggregateAuthority>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = articulations_.find(articulation_id);
  if (it == articulations_.end()) {
    return ArticulationUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  return Ok(it->second.authority);
}

auto oxygen::physics::jolt::JoltArticulations::FlushStructuralChanges(
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

  // TODO: Materialize dirty articulation sub-trees into concrete Jolt
  // constraint graphs during this flush once the articulation solver layer is
  // introduced.
  return Ok(pending_changes);
}

auto oxygen::physics::jolt::JoltArticulations::HasWorld(
  const WorldId world_id) const noexcept -> bool
{
  const auto* world = world_.get();
  return world != nullptr && world->TryGetPhysicsSystem(world_id) != nullptr;
}

auto oxygen::physics::jolt::JoltArticulations::IsBodyKnown(
  const WorldId world_id, const BodyId body_id) const noexcept -> bool
{
  const auto* world = world_.get();
  return world != nullptr && world->HasBody(world_id, body_id);
}

auto oxygen::physics::jolt::JoltArticulations::IsAncestor(
  const ArticulationState& articulation, const BodyId possible_ancestor,
  BodyId body) const noexcept -> bool
{
  while (true) {
    const auto it = articulation.child_to_parent.find(body);
    if (it == articulation.child_to_parent.end()) {
      return false;
    }
    if (it->second == possible_ancestor) {
      return true;
    }
    body = it->second;
  }
}

auto oxygen::physics::jolt::JoltArticulations::NoteStructuralChange(
  const WorldId world_id, const size_t count) -> void
{
  auto& pending = pending_structural_changes_[world_id];
  pending += count;
}
