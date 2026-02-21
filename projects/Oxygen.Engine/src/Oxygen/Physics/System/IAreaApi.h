//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/Area/AreaDesc.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>

namespace oxygen::physics::system {

//! Area/region domain API.
/*!
 Responsibilities now:
 - Create and destroy area regions used for trigger/monitoring logic.
 - Read and write area world pose.
 - Attach and detach reusable shape instances to define the region volume.
 - Emit trigger monitoring events through the event domain.

 ### Near Future

 - Add per-area gravity/damping override policies and fine-grained monitor
   filters and callbacks.
*/
class IAreaApi {
public:
  IAreaApi() = default;
  virtual ~IAreaApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IAreaApi)
  OXYGEN_MAKE_NON_MOVABLE(IAreaApi)

  virtual auto CreateArea(WorldId world_id, const area::AreaDesc& desc)
    -> PhysicsResult<AreaId>
    = 0;
  virtual auto DestroyArea(WorldId world_id, AreaId area_id)
    -> PhysicsResult<void>
    = 0;

  virtual auto GetAreaPosition(WorldId world_id, AreaId area_id) const
    -> PhysicsResult<Vec3>
    = 0;
  virtual auto GetAreaRotation(WorldId world_id, AreaId area_id) const
    -> PhysicsResult<Quat>
    = 0;
  virtual auto SetAreaPose(WorldId world_id, AreaId area_id,
    const Vec3& position, const Quat& rotation) -> PhysicsResult<void>
    = 0;

  virtual auto AddAreaShape(WorldId world_id, AreaId area_id, ShapeId shape_id,
    const Vec3& local_position, const Quat& local_rotation)
    -> PhysicsResult<ShapeInstanceId>
    = 0;

  /*!
   Removal contract:
   - Removes a previously attached area shape instance.
   - Returns not-found style error when `shape_instance_id` does not belong to
     `area_id`.
  */
  virtual auto RemoveAreaShape(WorldId world_id, AreaId area_id,
    ShapeInstanceId shape_instance_id) -> PhysicsResult<void>
    = 0;
};

} // namespace oxygen::physics::system
