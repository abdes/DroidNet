//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>
#include <Oxygen/Physics/System/IVehicleApi.h>

namespace oxygen::physics::jolt {

class JoltVehicles final : public system::IVehicleApi {
public:
  explicit JoltVehicles(JoltWorld& world);
  ~JoltVehicles() override;

  OXYGEN_MAKE_NON_COPYABLE(JoltVehicles)
  OXYGEN_MAKE_NON_MOVABLE(JoltVehicles)

  auto CreateVehicle(WorldId world_id, const vehicle::VehicleDesc& desc)
    -> PhysicsResult<AggregateId> override;
  auto DestroyVehicle(WorldId world_id, AggregateId vehicle_id)
    -> PhysicsResult<void> override;

  auto SetControlInput(WorldId world_id, AggregateId vehicle_id,
    const vehicle::VehicleControlInput& input) -> PhysicsResult<void> override;
  auto GetState(WorldId world_id, AggregateId vehicle_id) const
    -> PhysicsResult<vehicle::VehicleState> override;
  auto GetAuthority(WorldId world_id, AggregateId vehicle_id) const
    -> PhysicsResult<aggregate::AggregateAuthority> override;
  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override;

private:
  struct Impl;
  struct VehicleState final {
    WorldId world_id { kInvalidWorldId };
    BodyId chassis_body_id { kInvalidBodyId };
    std::vector<BodyId> wheel_body_ids {};
    vehicle::VehicleControlInput control_input {};
    aggregate::AggregateAuthority authority {
      aggregate::AggregateAuthority::kCommand,
    };
  };

  [[nodiscard]] auto HasWorld(WorldId world_id) const noexcept -> bool;
  [[nodiscard]] auto IsBodyKnown(
    WorldId world_id, BodyId body_id) const noexcept -> bool;
  auto NoteStructuralChange(WorldId world_id, size_t count = 1U) -> void;

  observer_ptr<JoltWorld> world_ {};
  mutable std::mutex mutex_ {};
  uint32_t next_vehicle_id_ { 1U };
  std::unordered_map<AggregateId, VehicleState> vehicles_ {};
  std::unordered_map<WorldId, size_t> pending_structural_changes_ {};
  std::unique_ptr<Impl> impl_ {};
};

} // namespace oxygen::physics::jolt
