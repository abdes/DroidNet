//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IWorldApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeWorldApi final : public system::IWorldApi {
public:
  explicit FakeWorldApi(BackendState& state)
    : state_(&state)
  {
  }

  auto CreateWorld(const world::WorldDesc&) -> PhysicsResult<WorldId> override
  {
    state_->world_created = true;
    return PhysicsResult<WorldId>::Ok(state_->world_id);
  }

  auto DestroyWorld(const WorldId world_id) -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    state_->world_destroyed = true;
    return PhysicsResult<void>::Ok();
  }

  auto Step(const WorldId world_id, const float delta_time, int,
    const float fixed_dt_seconds) -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    state_->step_count += 1;
    state_->last_step_dt = delta_time;
    state_->last_step_fixed_dt = fixed_dt_seconds;
    return PhysicsResult<void>::Ok();
  }

  auto GetActiveBodyTransforms(WorldId world_id,
    std::span<system::ActiveBodyTransform> out_transforms) const
    -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto count
      = std::min(out_transforms.size(), state_->active_transforms.size());
    for (size_t i = 0; i < count; ++i) {
      out_transforms[i] = state_->active_transforms[i];
    }
    return PhysicsResult<size_t>::Ok(count);
  }

  auto GetGravity(WorldId world_id) const -> PhysicsResult<Vec3> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<Vec3>::Ok(state_->gravity);
  }

  auto SetGravity(WorldId world_id, const Vec3& gravity)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    state_->gravity = gravity;
    return PhysicsResult<void>::Ok();
  }

private:
  observer_ptr<BackendState> state_;
};

} // namespace oxygen::physics::test::detail
