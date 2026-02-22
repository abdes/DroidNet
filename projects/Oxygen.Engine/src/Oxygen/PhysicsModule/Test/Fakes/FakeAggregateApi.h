//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <unordered_set>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IAggregateApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeAggregateApi final : public system::IAggregateApi {
public:
  explicit FakeAggregateApi(BackendState& state)
    : state_(&state)
  {
  }

  auto CreateAggregate(WorldId world_id) -> PhysicsResult<AggregateId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto aggregate_id = state_->next_aggregate_id;
    state_->next_aggregate_id
      = AggregateId { state_->next_aggregate_id.get() + 1U };
    state_->aggregates.emplace(aggregate_id, std::unordered_set<BodyId> {});
    state_->aggregate_create_calls += 1;
    return PhysicsResult<AggregateId>::Ok(aggregate_id);
  }

  auto DestroyAggregate(WorldId world_id, AggregateId aggregate_id)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->aggregates.contains(aggregate_id)) {
      return Err(PhysicsError::kInvalidArgument);
    }
    state_->aggregates.erase(aggregate_id);
    state_->aggregate_destroy_calls += 1;
    return PhysicsResult<void>::Ok();
  }

  auto AddMemberBody(WorldId world_id, AggregateId aggregate_id, BodyId body_id)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->bodies.contains(body_id)) {
      return Err(PhysicsError::kBodyNotFound);
    }
    const auto it = state_->aggregates.find(aggregate_id);
    if (it == state_->aggregates.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    const auto [_, inserted] = it->second.insert(body_id);
    if (!inserted) {
      return Err(PhysicsError::kAlreadyExists);
    }
    return PhysicsResult<void>::Ok();
  }

  auto RemoveMemberBody(WorldId world_id, AggregateId aggregate_id,
    BodyId body_id) -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto it = state_->aggregates.find(aggregate_id);
    if (it == state_->aggregates.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (it->second.erase(body_id) == 0U) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto GetMemberBodies(WorldId world_id, AggregateId aggregate_id,
    std::span<BodyId> out_body_ids) const -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto it = state_->aggregates.find(aggregate_id);
    if (it == state_->aggregates.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (out_body_ids.size() < it->second.size()) {
      return Err(PhysicsError::kBufferTooSmall);
    }
    size_t i = 0;
    for (const auto member : it->second) {
      out_body_ids[i++] = member;
    }
    return PhysicsResult<size_t>::Ok(i);
  }

  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    state_->aggregate_flush_structural_calls += 1;
    return PhysicsResult<size_t>::Ok(size_t { 0 });
  }

private:
  observer_ptr<BackendState> state_;
};

} // namespace oxygen::physics::test::detail
