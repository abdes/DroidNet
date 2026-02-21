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
#include <Oxygen/Physics/Query/Overlap.h>
#include <Oxygen/Physics/Query/Raycast.h>
#include <Oxygen/Physics/Query/Sweep.h>

namespace oxygen::physics::system {

//! Scene query domain API.
/*!
 Responsibilities now:
 - Execute read-only raycast, sweep, and overlap queries.
 - Return query results in caller-owned spans and values.

 ### Near Future

 - Add richer filters, hit flags, batched queries, and collector policies.
*/
class IQueryApi {
public:
  IQueryApi() = default;
  virtual ~IQueryApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IQueryApi)
  OXYGEN_MAKE_NON_MOVABLE(IQueryApi)

  virtual auto Raycast(WorldId world_id, const query::RaycastDesc& desc) const
    -> PhysicsResult<query::OptionalRaycastHit>
    = 0;
  virtual auto Sweep(WorldId world_id, const query::SweepDesc& desc,
    std::span<query::SweepHit> out_hits) const -> PhysicsResult<size_t>
    = 0;
  virtual auto Overlap(WorldId world_id, const query::OverlapDesc& desc,
    std::span<uint64_t> out_user_data) const -> PhysicsResult<size_t>
    = 0;
};

} // namespace oxygen::physics::system
