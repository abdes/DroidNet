//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/ISoftBodyApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeSoftBodyApi final : public system::ISoftBodyApi {
public:
  explicit FakeSoftBodyApi(BackendState& state)
    : state_(&state)
  {
  }

  auto CreateSoftBody(WorldId world_id, const softbody::SoftBodyDesc& desc)
    -> PhysicsResult<AggregateId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (desc.cluster_count == 0U || desc.settings_blob.empty()) {
      return Err(PhysicsError::kInvalidArgument);
    }

    const auto soft_body_id = state_->next_aggregate_id;
    state_->next_aggregate_id
      = AggregateId { state_->next_aggregate_id.get() + 1U };
    state_->soft_bodies.insert_or_assign(soft_body_id,
      SoftBodyState {
        .material_params = desc.material_params,
      });
    state_->soft_body_create_calls += 1U;
    return PhysicsResult<AggregateId>::Ok(soft_body_id);
  }

  auto DestroySoftBody(WorldId world_id, AggregateId soft_body_id)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->soft_bodies.contains(soft_body_id)) {
      return Err(PhysicsError::kInvalidArgument);
    }
    state_->soft_bodies.erase(soft_body_id);
    state_->soft_body_destroy_calls += 1U;
    return PhysicsResult<void>::Ok();
  }

  auto SetMaterialParams(WorldId world_id, AggregateId soft_body_id,
    const softbody::SoftBodyMaterialParams& params)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    auto it = state_->soft_bodies.find(soft_body_id);
    if (it == state_->soft_bodies.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    it->second.material_params = params;
    state_->soft_body_set_material_calls += 1U;
    return PhysicsResult<void>::Ok();
  }

  auto GetState(WorldId world_id, AggregateId soft_body_id) const
    -> PhysicsResult<softbody::SoftBodyState> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto it = state_->soft_bodies.find(soft_body_id);
    if (it == state_->soft_bodies.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    return PhysicsResult<softbody::SoftBodyState>::Ok(it->second.state);
  }

  auto GetAuthority(WorldId world_id, AggregateId soft_body_id) const
    -> PhysicsResult<aggregate::AggregateAuthority> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->soft_bodies.contains(soft_body_id)) {
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
    state_->soft_body_flush_structural_calls += 1U;
    return PhysicsResult<size_t>::Ok(size_t { 0U });
  }

private:
  observer_ptr<BackendState> state_ {};
};

} // namespace oxygen::physics::test::detail
