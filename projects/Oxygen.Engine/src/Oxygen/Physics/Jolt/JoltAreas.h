//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/System/IAreaApi.h>

namespace oxygen::physics::jolt {

//! Jolt implementation of the area domain.
class JoltAreas final : public system::IAreaApi {
public:
  JoltAreas() = default;
  ~JoltAreas() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltAreas)
  OXYGEN_MAKE_NON_MOVABLE(JoltAreas)

  auto CreateArea(WorldId world_id, const area::AreaDesc& desc)
    -> PhysicsResult<AreaId> override;
  auto DestroyArea(WorldId world_id, AreaId area_id)
    -> PhysicsResult<void> override;

  auto GetAreaPosition(WorldId world_id, AreaId area_id) const
    -> PhysicsResult<Vec3> override;
  auto GetAreaRotation(WorldId world_id, AreaId area_id) const
    -> PhysicsResult<Quat> override;
  auto SetAreaPose(WorldId world_id, AreaId area_id, const Vec3& position,
    const Quat& rotation) -> PhysicsResult<void> override;

  auto AddAreaShape(WorldId world_id, AreaId area_id, ShapeId shape_id,
    const Vec3& local_position, const Quat& local_rotation)
    -> PhysicsResult<ShapeInstanceId> override;
  auto RemoveAreaShape(WorldId world_id, AreaId area_id,
    ShapeInstanceId shape_instance_id) -> PhysicsResult<void> override;
};

} // namespace oxygen::physics::jolt
