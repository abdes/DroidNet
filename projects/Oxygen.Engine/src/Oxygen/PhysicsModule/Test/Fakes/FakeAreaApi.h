//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IAreaApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeAreaApi final : public system::IAreaApi {
public:
  explicit FakeAreaApi(BackendState& state)
    : state_(&state)
  {
  }

  auto CreateArea(WorldId world_id, const area::AreaDesc&)
    -> PhysicsResult<AreaId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto area_id = state_->next_area_id;
    state_->next_area_id = AreaId { state_->next_area_id.get() + 1U };
    return PhysicsResult<AreaId>::Ok(area_id);
  }

  auto DestroyArea(WorldId world_id, AreaId) -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto GetAreaPosition(WorldId world_id, AreaId) const
    -> PhysicsResult<Vec3> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<Vec3>::Ok(Vec3 { 0.0F, 0.0F, 0.0F });
  }

  auto GetAreaRotation(WorldId world_id, AreaId) const
    -> PhysicsResult<Quat> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<Quat>::Ok(Quat { 1.0F, 0.0F, 0.0F, 0.0F });
  }

  auto SetAreaPose(WorldId world_id, AreaId, const Vec3&, const Quat&)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto AddAreaShape(WorldId world_id, AreaId, ShapeId, const Vec3&, const Quat&)
    -> PhysicsResult<ShapeInstanceId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto shape_instance_id = state_->next_shape_instance_id;
    state_->next_shape_instance_id
      = ShapeInstanceId { state_->next_shape_instance_id.get() + 1U };
    return PhysicsResult<ShapeInstanceId>::Ok(shape_instance_id);
  }

  auto RemoveAreaShape(WorldId world_id, AreaId, ShapeInstanceId)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

private:
  observer_ptr<BackendState> state_;
};

} // namespace oxygen::physics::test::detail
