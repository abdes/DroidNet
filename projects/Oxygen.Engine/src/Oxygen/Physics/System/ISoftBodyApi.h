//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/SoftBody/SoftBodyDesc.h>

namespace oxygen::physics::system {

//! Soft-body domain API integration point.
/*!
 Responsibilities now:
 - Create and destroy soft-body aggregates.
 - Define backend-agnostic soft-body tuning/state query contracts.
 - Expose authority policy and structural mutation flush boundaries.

 ### Near Future

 - Add particle/cluster attachment, material/tether tuning, and state queries.
*/
class ISoftBodyApi {
public:
  ISoftBodyApi() = default;
  virtual ~ISoftBodyApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(ISoftBodyApi)
  OXYGEN_MAKE_NON_MOVABLE(ISoftBodyApi)

  virtual auto CreateSoftBody(WorldId world_id,
    const softbody::SoftBodyDesc& desc) -> PhysicsResult<AggregateId>
    = 0;
  virtual auto DestroySoftBody(WorldId world_id, AggregateId soft_body_id)
    -> PhysicsResult<void>
    = 0;

  virtual auto SetMaterialParams(WorldId world_id, AggregateId soft_body_id,
    const softbody::SoftBodyMaterialParams& params) -> PhysicsResult<void>
    = 0;
  virtual auto GetState(WorldId world_id, AggregateId soft_body_id) const
    -> PhysicsResult<softbody::SoftBodyState>
    = 0;
  virtual auto GetAuthority(WorldId world_id, AggregateId soft_body_id) const
    -> PhysicsResult<aggregate::AggregateAuthority>
    = 0;
  virtual auto FlushStructuralChanges(WorldId world_id) -> PhysicsResult<size_t>
    = 0;
};

} // namespace oxygen::physics::system
