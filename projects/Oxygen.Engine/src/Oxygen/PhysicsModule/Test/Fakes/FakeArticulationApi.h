//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IArticulationApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeArticulationApi final : public system::IArticulationApi {
public:
  explicit FakeArticulationApi(BackendState& state)
    : state_(&state)
  {
  }

  auto CreateArticulation(
    WorldId world_id, const articulation::ArticulationDesc& desc)
    -> PhysicsResult<AggregateId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (desc.root_body_id == kInvalidBodyId
      || !state_->bodies.contains(desc.root_body_id)) {
      return Err(PhysicsError::kInvalidArgument);
    }

    const auto articulation_id = state_->next_aggregate_id;
    state_->next_aggregate_id
      = AggregateId { state_->next_aggregate_id.get() + 1U };
    state_->aggregates.emplace(
      articulation_id, std::unordered_set<BodyId> { desc.root_body_id });
    state_->aggregate_create_calls += 1;
    return PhysicsResult<AggregateId>::Ok(articulation_id);
  }

  auto DestroyArticulation(WorldId world_id, AggregateId articulation_id)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->aggregates.contains(articulation_id)) {
      return Err(PhysicsError::kInvalidArgument);
    }
    state_->aggregates.erase(articulation_id);
    state_->aggregate_destroy_calls += 1;
    return PhysicsResult<void>::Ok();
  }

  auto AddLink(WorldId world_id, AggregateId articulation_id,
    const articulation::ArticulationLinkDesc& link_desc)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->bodies.contains(link_desc.child_body_id)) {
      return Err(PhysicsError::kBodyNotFound);
    }
    auto it = state_->aggregates.find(articulation_id);
    if (it == state_->aggregates.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    const auto [_, inserted] = it->second.insert(link_desc.child_body_id);
    if (!inserted) {
      return Err(PhysicsError::kAlreadyExists);
    }
    return PhysicsResult<void>::Ok();
  }

  auto RemoveLink(WorldId world_id, AggregateId articulation_id,
    BodyId child_body_id) -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    auto it = state_->aggregates.find(articulation_id);
    if (it == state_->aggregates.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (it->second.erase(child_body_id) == 0U) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto GetRootBody(WorldId world_id, AggregateId articulation_id) const
    -> PhysicsResult<BodyId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto it = state_->aggregates.find(articulation_id);
    if (it == state_->aggregates.end() || it->second.empty()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    return PhysicsResult<BodyId>::Ok(*it->second.begin());
  }

  auto GetLinkBodies(WorldId world_id, AggregateId articulation_id,
    std::span<BodyId> out_child_body_ids) const
    -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto it = state_->aggregates.find(articulation_id);
    if (it == state_->aggregates.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (out_child_body_ids.size() < it->second.size()) {
      return Err(PhysicsError::kBufferTooSmall);
    }
    size_t i = 0;
    for (const auto body_id : it->second) {
      out_child_body_ids[i++] = body_id;
    }
    return PhysicsResult<size_t>::Ok(i);
  }

  auto GetAuthority(WorldId world_id, AggregateId articulation_id) const
    -> PhysicsResult<aggregate::AggregateAuthority> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->aggregates.contains(articulation_id)) {
      return Err(PhysicsError::kInvalidArgument);
    }
    return PhysicsResult<aggregate::AggregateAuthority>::Ok(
      aggregate::AggregateAuthority::kSimulation);
  }

  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    state_->articulation_flush_structural_calls += 1;
    return PhysicsResult<size_t>::Ok(size_t { 0 });
  }

private:
  observer_ptr<BackendState> state_ {};
};

} // namespace oxygen::physics::test::detail
