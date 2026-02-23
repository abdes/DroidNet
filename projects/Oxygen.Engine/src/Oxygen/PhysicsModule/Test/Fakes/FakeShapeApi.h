//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/System/IShapeApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeShapeApi final : public system::IShapeApi {
public:
  explicit FakeShapeApi(BackendState& state)
    : state_(&state)
  {
  }

  auto GetShapeDesc(ShapeId) const -> PhysicsResult<shape::ShapeDesc> override
  {
    return PhysicsResult<shape::ShapeDesc>::Ok(shape::ShapeDesc {});
  }

  auto CreateShape(const shape::ShapeDesc&) -> PhysicsResult<ShapeId> override
  {
    const auto shape_id = state_->next_shape_id;
    state_->next_shape_id = ShapeId { state_->next_shape_id.get() + 1U };
    return PhysicsResult<ShapeId>::Ok(shape_id);
  }

  auto DestroyShape(ShapeId) -> PhysicsResult<void> override
  {
    return PhysicsResult<void>::Ok();
  }

private:
  observer_ptr<BackendState> state_;
};

} // namespace oxygen::physics::test::detail
