//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>
#include <Oxygen/Physics/System/IArticulationApi.h>

namespace oxygen::physics::jolt {

class JoltArticulations final : public system::IArticulationApi {
public:
  explicit JoltArticulations(JoltWorld& world);
  ~JoltArticulations() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltArticulations)
  OXYGEN_MAKE_NON_MOVABLE(JoltArticulations)

  auto CreateArticulation(
    WorldId world_id, const articulation::ArticulationDesc& desc)
    -> PhysicsResult<AggregateId> override;
  auto DestroyArticulation(WorldId world_id, AggregateId articulation_id)
    -> PhysicsResult<void> override;

  auto AddLink(WorldId world_id, AggregateId articulation_id,
    const articulation::ArticulationLinkDesc& link_desc)
    -> PhysicsResult<void> override;
  auto RemoveLink(WorldId world_id, AggregateId articulation_id,
    BodyId child_body_id) -> PhysicsResult<void> override;

  auto GetRootBody(WorldId world_id, AggregateId articulation_id) const
    -> PhysicsResult<BodyId> override;
  auto GetLinkBodies(WorldId world_id, AggregateId articulation_id,
    std::span<BodyId> out_child_body_ids) const
    -> PhysicsResult<size_t> override;
  auto GetAuthority(WorldId world_id, AggregateId articulation_id) const
    -> PhysicsResult<aggregate::AggregateAuthority> override;
  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override;

private:
  struct ArticulationState final {
    WorldId world_id { kInvalidWorldId };
    BodyId root_body { kInvalidBodyId };
    aggregate::AggregateAuthority authority {
      aggregate::AggregateAuthority::kSimulation,
    };
    std::unordered_map<BodyId, BodyId> child_to_parent {};
  };

  [[nodiscard]] auto HasWorld(WorldId world_id) const noexcept -> bool;
  [[nodiscard]] auto IsBodyKnown(
    WorldId world_id, BodyId body_id) const noexcept -> bool;
  [[nodiscard]] auto IsAncestor(const ArticulationState& articulation,
    BodyId possible_ancestor, BodyId body) const noexcept -> bool;
  auto NoteStructuralChange(WorldId world_id, size_t count = 1U) -> void;

  observer_ptr<JoltWorld> world_ {};
  mutable std::mutex mutex_ {};
  static constexpr uint32_t kArticulationAggregateIdBase { 0x20000000U };
  static constexpr uint32_t kArticulationAggregateIdMax { 0x2FFFFFFFU };
  uint32_t next_articulation_id_ { kArticulationAggregateIdBase };
  std::unordered_map<AggregateId, ArticulationState> articulations_ {};
  std::unordered_map<WorldId, size_t> pending_structural_changes_ {};
};

} // namespace oxygen::physics::jolt
