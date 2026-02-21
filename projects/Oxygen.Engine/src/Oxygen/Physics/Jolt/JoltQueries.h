//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/System/IQueryApi.h>

namespace oxygen::physics::jolt {

//! Jolt implementation of the query domain.
class JoltQueries final : public system::IQueryApi {
public:
  JoltQueries() = default;
  ~JoltQueries() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltQueries)
  OXYGEN_MAKE_NON_MOVABLE(JoltQueries)

  auto Raycast(WorldId world_id, const query::RaycastDesc& desc) const
    -> PhysicsResult<query::OptionalRaycastHit> override;
  auto Sweep(WorldId world_id, const query::SweepDesc& desc,
    std::span<query::SweepHit> out_hits) const
    -> PhysicsResult<size_t> override;
  auto Overlap(WorldId world_id, const query::OverlapDesc& desc,
    std::span<uint64_t> out_user_data) const -> PhysicsResult<size_t> override;
};

} // namespace oxygen::physics::jolt
