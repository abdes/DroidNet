//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Physics/Handles.h>

namespace oxygen::physics::articulation {

/*!
 Describes articulation creation using an existing rigid body as root.

 Ownership contract:
 - `root_body_id` must reference an existing body in the target world.
 - The articulation owns aggregate membership topology, not body lifetime.
*/
struct ArticulationDesc final {
  BodyId root_body_id { kInvalidBodyId };
};

/*!
 Describes one articulation link relationship between two existing bodies.

 Ownership contract:
 - `parent_body_id` and `child_body_id` must reference existing world bodies.
 - Link topology ownership belongs to the articulation identified by
   `AggregateId`.
*/
struct ArticulationLinkDesc final {
  BodyId parent_body_id { kInvalidBodyId };
  BodyId child_body_id { kInvalidBodyId };
};

} // namespace oxygen::physics::articulation
