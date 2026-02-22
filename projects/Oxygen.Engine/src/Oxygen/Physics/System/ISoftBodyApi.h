//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>

namespace oxygen::physics::system {

//! Soft-body domain API integration point.
/*!
 Responsibilities now:
 - Define a backend-agnostic soft-body aggregate identity entry point.
 - Reserve lifecycle contract for deformable simulation objects.

 ### Near Future

 - Add particle/cluster attachment, material/tether tuning, and state queries.
*/
class ISoftBodyApi {
public:
  ISoftBodyApi() = default;
  virtual ~ISoftBodyApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(ISoftBodyApi)
  OXYGEN_MAKE_NON_MOVABLE(ISoftBodyApi)

  virtual auto CreateSoftBody(WorldId world_id) -> PhysicsResult<AggregateId>
    = 0;
  virtual auto DestroySoftBody(WorldId world_id, AggregateId soft_body_id)
    -> PhysicsResult<void>
    = 0;
};

} // namespace oxygen::physics::system
