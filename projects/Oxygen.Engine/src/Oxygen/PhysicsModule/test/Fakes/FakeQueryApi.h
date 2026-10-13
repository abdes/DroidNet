//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/System/IQueryApi.h>

namespace oxygen::physics::test::detail {

class FakeQueryApi final : public system::IQueryApi {
public:
  auto Raycast(WorldId, const query::RaycastDesc&) const
    -> PhysicsResult<query::OptionalRaycastHit> override
  {
    return PhysicsResult<query::OptionalRaycastHit>::Ok(
      query::OptionalRaycastHit {});
  }

  auto Sweep(WorldId, const query::SweepDesc&, std::span<query::SweepHit>) const
    -> PhysicsResult<size_t> override
  {
    return PhysicsResult<size_t>::Ok(size_t { 0 });
  }

  auto Overlap(WorldId, const query::OverlapDesc&, std::span<uint64_t>) const
    -> PhysicsResult<size_t> override
  {
    return PhysicsResult<size_t>::Ok(size_t { 0 });
  }
};

} // namespace oxygen::physics::test::detail
