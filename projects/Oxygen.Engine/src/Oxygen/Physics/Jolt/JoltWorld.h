//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/System/IWorldApi.h>

namespace oxygen::physics::jolt {

//! Jolt implementation of the world domain.
class JoltWorld final : public system::IWorldApi {
public:
  JoltWorld() = default;
  ~JoltWorld() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltWorld)
  OXYGEN_MAKE_NON_MOVABLE(JoltWorld)

  auto CreateWorld(const world::WorldDesc& desc)
    -> PhysicsResult<WorldId> override;
  auto DestroyWorld(WorldId world_id) -> PhysicsResult<void> override;
  auto Step(WorldId world_id, float delta_time, int max_sub_steps,
    float fixed_dt_seconds) -> PhysicsResult<void> override;

  auto GetActiveBodyTransforms(WorldId world_id,
    std::span<system::ActiveBodyTransform> out_transforms) const
    -> PhysicsResult<size_t> override;

  auto GetGravity(WorldId world_id) const -> PhysicsResult<Vec3> override;
  auto SetGravity(WorldId world_id, const Vec3& gravity)
    -> PhysicsResult<void> override;
};

} // namespace oxygen::physics::jolt
