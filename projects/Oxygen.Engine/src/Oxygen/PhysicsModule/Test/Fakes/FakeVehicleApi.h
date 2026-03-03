//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IVehicleApi.h>
#include <Oxygen/Physics/Vehicle/VehicleDesc.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeVehicleApi final : public system::IVehicleApi {
public:
  explicit FakeVehicleApi(BackendState& state)
    : state_(&state)
  {
  }

  auto CreateVehicle(WorldId world_id, const vehicle::VehicleDesc& desc)
    -> PhysicsResult<AggregateId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->bodies.contains(desc.chassis_body_id)
      || desc.wheels.size() < 2U || desc.constraint_settings_blob.empty()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    for (const auto& wheel : desc.wheels) {
      if (!state_->bodies.contains(wheel.body_id)
        || wheel.body_id == desc.chassis_body_id) {
        return Err(PhysicsError::kInvalidArgument);
      }
    }

    const auto vehicle_id = state_->next_aggregate_id;
    state_->next_aggregate_id
      = AggregateId { state_->next_aggregate_id.get() + 1U };
    state_->vehicles.insert_or_assign(vehicle_id,
      VehicleState {
        .chassis_body_id = desc.chassis_body_id,
        .wheels = std::vector<vehicle::VehicleWheelDesc>(
          desc.wheels.begin(), desc.wheels.end()),
      });
    state_->vehicle_create_calls += 1;
    return PhysicsResult<AggregateId>::Ok(vehicle_id);
  }

  auto DestroyVehicle(WorldId world_id, AggregateId vehicle_id)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->vehicles.contains(vehicle_id)) {
      return Err(PhysicsError::kInvalidArgument);
    }
    state_->vehicles.erase(vehicle_id);
    state_->vehicle_destroy_calls += 1;
    return PhysicsResult<void>::Ok();
  }

  auto SetControlInput(WorldId world_id, AggregateId vehicle_id,
    const vehicle::VehicleControlInput& input) -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    auto it = state_->vehicles.find(vehicle_id);
    if (it == state_->vehicles.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    it->second.control_input = input;
    state_->vehicle_set_control_calls += 1;
    return PhysicsResult<void>::Ok();
  }

  auto GetState(WorldId world_id, AggregateId vehicle_id) const
    -> PhysicsResult<vehicle::VehicleState> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto it = state_->vehicles.find(vehicle_id);
    if (it == state_->vehicles.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    return PhysicsResult<vehicle::VehicleState>::Ok(vehicle::VehicleState {
      .forward_speed_mps = 0.0F,
      .grounded = !it->second.wheels.empty(),
    });
  }

  auto GetAuthority(WorldId world_id, AggregateId vehicle_id) const
    -> PhysicsResult<aggregate::AggregateAuthority> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->vehicles.contains(vehicle_id)) {
      return Err(PhysicsError::kInvalidArgument);
    }
    return PhysicsResult<aggregate::AggregateAuthority>::Ok(
      aggregate::AggregateAuthority::kCommand);
  }

  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    state_->vehicle_flush_structural_calls += 1;
    return PhysicsResult<size_t>::Ok(size_t { 0 });
  }

private:
  observer_ptr<BackendState> state_;
};

} // namespace oxygen::physics::test::detail
