//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>
#include <Oxygen/Physics/System/IAggregateApi.h>

namespace oxygen::physics::jolt {

class JoltAggregates final : public system::IAggregateApi {
public:
  explicit JoltAggregates(JoltWorld& world);
  ~JoltAggregates() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltAggregates)
  OXYGEN_MAKE_NON_MOVABLE(JoltAggregates)

  auto CreateAggregate(WorldId world_id) -> PhysicsResult<AggregateId> override;
  auto DestroyAggregate(WorldId world_id, AggregateId aggregate_id)
    -> PhysicsResult<void> override;

  auto AddMemberBody(WorldId world_id, AggregateId aggregate_id, BodyId body_id)
    -> PhysicsResult<void> override;
  auto RemoveMemberBody(WorldId world_id, AggregateId aggregate_id,
    BodyId body_id) -> PhysicsResult<void> override;

  auto GetMemberBodies(WorldId world_id, AggregateId aggregate_id,
    std::span<BodyId> out_body_ids) const -> PhysicsResult<size_t> override;
  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override;

private:
  struct AggregateState final {
    WorldId world_id { kInvalidWorldId };
    std::unordered_set<BodyId> body_ids {};
  };

  [[nodiscard]] auto HasWorld(WorldId world_id) const noexcept -> bool;
  auto NoteStructuralChange(WorldId world_id, size_t count = 1U) -> void;

  observer_ptr<JoltWorld> world_ {};
  mutable std::mutex mutex_ {};
  static constexpr uint32_t kGenericAggregateIdBase { 0x40000000U };
  static constexpr uint32_t kGenericAggregateIdMax { 0x4FFFFFFFU };
  uint32_t next_aggregate_id_ { kGenericAggregateIdBase };
  std::unordered_map<AggregateId, AggregateState> aggregates_ {};
  std::unordered_map<WorldId, size_t> pending_structural_changes_ {};
};

} // namespace oxygen::physics::jolt
