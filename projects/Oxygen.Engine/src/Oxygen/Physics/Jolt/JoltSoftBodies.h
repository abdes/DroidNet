//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <Jolt/Physics/Body/BodyID.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>
#include <Oxygen/Physics/System/ISoftBodyApi.h>

namespace oxygen::physics::jolt {

class JoltSoftBodies final : public system::ISoftBodyApi {
public:
  explicit JoltSoftBodies(JoltWorld& world);
  ~JoltSoftBodies() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltSoftBodies)
  OXYGEN_MAKE_NON_MOVABLE(JoltSoftBodies)

  auto CreateSoftBody(WorldId world_id, const softbody::SoftBodyDesc& desc)
    -> PhysicsResult<AggregateId> override;
  auto DestroySoftBody(WorldId world_id, AggregateId soft_body_id)
    -> PhysicsResult<void> override;

  auto SetMaterialParams(WorldId world_id, AggregateId soft_body_id,
    const softbody::SoftBodyMaterialParams& params)
    -> PhysicsResult<void> override;
  auto GetState(WorldId world_id, AggregateId soft_body_id) const
    -> PhysicsResult<softbody::SoftBodyState> override;
  auto GetAuthority(WorldId world_id, AggregateId soft_body_id) const
    -> PhysicsResult<aggregate::AggregateAuthority> override;
  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override;

private:
  struct SoftBodyState final {
    WorldId world_id { kInvalidWorldId };
    JPH::BodyID jolt_body_id {};
    BodyId registered_body_id { kInvalidBodyId };
    uint32_t cluster_count { 0U };
    softbody::SoftBodyMaterialParams material_params {};
    std::vector<uint8_t> settings_blob {};
    aggregate::AggregateAuthority authority {
      aggregate::AggregateAuthority::kSimulation,
    };
  };

  [[nodiscard]] auto HasWorld(WorldId world_id) const noexcept -> bool;
  [[nodiscard]] auto IsBodyKnown(
    WorldId world_id, BodyId body_id) const noexcept -> bool;
  auto NoteStructuralChange(WorldId world_id, size_t count = 1U) -> void;
  auto ApplyMaterialParams(WorldId world_id, JPH::BodyID soft_body_id,
    const softbody::SoftBodyMaterialParams& params) const
    -> PhysicsResult<void>;
  auto RebuildSoftBodyForMaterialParams(WorldId world_id,
    AggregateId soft_body_id, const softbody::SoftBodyMaterialParams& params)
    -> PhysicsResult<void>;
  auto FlushPendingMaterialRebuilds(WorldId world_id) -> PhysicsResult<size_t>;
  [[nodiscard]] auto ConsumePendingStructuralChangeCount(WorldId world_id)
    -> size_t;

  observer_ptr<JoltWorld> world_ {};
  mutable std::mutex mutex_ {};
  static constexpr uint32_t kSoftBodyAggregateIdBase { 0x30000000U };
  static constexpr uint32_t kSoftBodyAggregateIdMax { 0x3FFFFFFFU };
  uint32_t next_soft_body_id_ { kSoftBodyAggregateIdBase };
  std::unordered_map<AggregateId, SoftBodyState> soft_bodies_ {};
  std::unordered_map<WorldId, size_t> pending_structural_changes_ {};
  std::unordered_map<AggregateId, softbody::SoftBodyMaterialParams>
    pending_material_rebuilds_ {};
};

} // namespace oxygen::physics::jolt
