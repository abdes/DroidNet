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

namespace oxygen::physics::system {

//! Aggregate simulation domain API.
/*!
 Responsibilities now:
 - Provide aggregate-level identity and lifecycle (`AggregateId`).
 - Manage aggregate membership over rigid bodies.
 - Expose aggregate membership queries for scene-bridge and event routing.

 Aggregate model:
 - Supports 1:N mapping (`AggregateId` -> many `BodyId` members).
 - Enables N:1 scene mapping through `PhysicsModule` side tables.

 ### Near Future

 - Add aggregate-local metadata, role/tag channels, and hierarchy helpers.
*/
class IAggregateApi {
public:
  IAggregateApi() = default;
  virtual ~IAggregateApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IAggregateApi)
  OXYGEN_MAKE_NON_MOVABLE(IAggregateApi)

  virtual auto CreateAggregate(WorldId world_id) -> PhysicsResult<AggregateId>
    = 0;
  virtual auto DestroyAggregate(WorldId world_id, AggregateId aggregate_id)
    -> PhysicsResult<void>
    = 0;

  virtual auto AddMemberBody(WorldId world_id, AggregateId aggregate_id,
    BodyId body_id) -> PhysicsResult<void>
    = 0;
  virtual auto RemoveMemberBody(WorldId world_id, AggregateId aggregate_id,
    BodyId body_id) -> PhysicsResult<void>
    = 0;

  virtual auto GetMemberBodies(WorldId world_id, AggregateId aggregate_id,
    std::span<BodyId> out_body_ids) const -> PhysicsResult<size_t>
    = 0;
};

} // namespace oxygen::physics::system
