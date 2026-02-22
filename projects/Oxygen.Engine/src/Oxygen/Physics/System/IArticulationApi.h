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

//! Articulation domain API integration point.
/*!
 Responsibilities now:
 - Define a backend-agnostic articulation entry point keyed by `AggregateId`.
 - Reserve contract surface for articulated multi-body lifecycle.

 ### Near Future

 - Add link construction, drive/limit control, and articulation state queries.
*/
class IArticulationApi {
public:
  IArticulationApi() = default;
  virtual ~IArticulationApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IArticulationApi)
  OXYGEN_MAKE_NON_MOVABLE(IArticulationApi)

  virtual auto CreateArticulation(WorldId world_id, BodyId root_body_id)
    -> PhysicsResult<AggregateId>
    = 0;
  virtual auto DestroyArticulation(
    WorldId world_id, AggregateId articulation_id) -> PhysicsResult<void>
    = 0;
};

} // namespace oxygen::physics::system
