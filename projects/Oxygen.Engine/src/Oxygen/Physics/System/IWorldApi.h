//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::system {

struct ActiveBodyTransform final {
  BodyId body_id { kInvalidBodyId };
  uint64_t user_data { 0 };
  Vec3 position { 0.0F, 0.0F, 0.0F };
  Quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
};

//! World simulation domain API.
/*!
 Responsibilities now:
 - Create and destroy simulation worlds.
 - Step a world with fixed delta time.

 ### Near Future

 - Configure world-level policies (for example broadphase regions, gravity
   profiles, solver tuning, and streaming partition settings).
*/
class IWorldApi {
public:
  IWorldApi() = default;
  virtual ~IWorldApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IWorldApi)
  OXYGEN_MAKE_NON_MOVABLE(IWorldApi)

  virtual auto CreateWorld(const world::WorldDesc& desc)
    -> PhysicsResult<WorldId>
    = 0;
  virtual auto DestroyWorld(WorldId world_id) -> PhysicsResult<void> = 0;
  virtual auto Step(WorldId world_id, float delta_time, int max_sub_steps,
    float fixed_dt_seconds) -> PhysicsResult<void>
    = 0;

  virtual auto GetActiveBodyTransforms(
    WorldId world_id, std::span<ActiveBodyTransform> out_transforms) const
    -> PhysicsResult<size_t>
    = 0;

  virtual auto GetGravity(WorldId world_id) const -> PhysicsResult<Vec3> = 0;
  virtual auto SetGravity(WorldId world_id, const Vec3& gravity)
    -> PhysicsResult<void>
    = 0;
};

} // namespace oxygen::physics::system
