//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Articulation/ArticulationDesc.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>

namespace oxygen::physics::system {

//! Articulation domain API integration point.
/*!
 Responsibilities now:
 - Create and destroy articulated aggregates keyed by `AggregateId`.
 - Manage articulation link topology over existing bodies.
 - Expose root and link membership queries for scene bridge and diagnostics.
 - Expose authority policy and structural mutation flush boundaries.

 ### Near Future

 - Add link construction, drive/limit control, and articulation state queries.
*/
class IArticulationApi {
public:
  IArticulationApi() = default;
  virtual ~IArticulationApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IArticulationApi)
  OXYGEN_MAKE_NON_MOVABLE(IArticulationApi)

  virtual auto CreateArticulation(WorldId world_id,
    const articulation::ArticulationDesc& desc) -> PhysicsResult<AggregateId>
    = 0;
  virtual auto DestroyArticulation(
    WorldId world_id, AggregateId articulation_id) -> PhysicsResult<void>
    = 0;

  virtual auto AddLink(WorldId world_id, AggregateId articulation_id,
    const articulation::ArticulationLinkDesc& link_desc) -> PhysicsResult<void>
    = 0;
  virtual auto RemoveLink(WorldId world_id, AggregateId articulation_id,
    BodyId child_body_id) -> PhysicsResult<void>
    = 0;

  virtual auto GetRootBody(WorldId world_id, AggregateId articulation_id) const
    -> PhysicsResult<BodyId>
    = 0;
  virtual auto GetLinkBodies(WorldId world_id, AggregateId articulation_id,
    std::span<BodyId> out_child_body_ids) const -> PhysicsResult<size_t>
    = 0;
  virtual auto GetAuthority(WorldId world_id, AggregateId articulation_id) const
    -> PhysicsResult<aggregate::AggregateAuthority>
    = 0;
  virtual auto FlushStructuralChanges(WorldId world_id) -> PhysicsResult<size_t>
    = 0;
};

} // namespace oxygen::physics::system
