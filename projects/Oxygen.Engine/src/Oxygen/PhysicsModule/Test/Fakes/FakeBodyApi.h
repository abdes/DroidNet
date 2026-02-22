//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IBodyApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeBodyApi final : public system::IBodyApi {
public:
  explicit FakeBodyApi(BackendState& state)
    : state_(&state)
  {
  }

  auto CreateBody(WorldId world_id, const body::BodyDesc& desc)
    -> PhysicsResult<BodyId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto body_id = state_->next_body_id;
    state_->next_body_id = BodyId { state_->next_body_id.get() + 1U };
    state_->bodies.insert_or_assign(body_id,
      BodyState {
        .type = desc.type,
        .position = desc.initial_position,
        .rotation = desc.initial_rotation,
      });
    return PhysicsResult<BodyId>::Ok(body_id);
  }

  auto DestroyBody(WorldId world_id, BodyId body_id)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    if (!state_->bodies.contains(body_id)) {
      return Err(PhysicsError::kBodyNotFound);
    }
    state_->bodies.erase(body_id);
    return PhysicsResult<void>::Ok();
  }

  auto GetBodyPosition(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override
  {
    const auto* body = TryBody(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<Vec3>::Ok(body->position);
  }

  auto GetBodyRotation(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Quat> override
  {
    const auto* body = TryBody(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<Quat>::Ok(body->rotation);
  }

  auto SetBodyPosition(WorldId world_id, BodyId body_id, const Vec3& position)
    -> PhysicsResult<void> override
  {
    auto* body = TryBodyMutable(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    body->position = position;
    return PhysicsResult<void>::Ok();
  }

  auto SetBodyRotation(WorldId world_id, BodyId body_id, const Quat& rotation)
    -> PhysicsResult<void> override
  {
    auto* body = TryBodyMutable(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    body->rotation = rotation;
    return PhysicsResult<void>::Ok();
  }

  auto SetBodyPose(WorldId world_id, BodyId body_id, const Vec3& position,
    const Quat& rotation) -> PhysicsResult<void> override
  {
    auto* body = TryBodyMutable(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    body->position = position;
    body->rotation = rotation;
    state_->set_body_pose_calls += 1;
    return PhysicsResult<void>::Ok();
  }

  auto GetBodyPoses(WorldId world_id, std::span<const BodyId> body_ids,
    std::span<Vec3> out_positions, std::span<Quat> out_rotations) const
    -> PhysicsResult<size_t> override
  {
    if (out_positions.size() < body_ids.size()
      || out_rotations.size() < body_ids.size()) {
      return Err(PhysicsError::kBufferTooSmall);
    }

    for (size_t i = 0; i < body_ids.size(); ++i) {
      const auto* body = TryBody(world_id, body_ids[i]);
      if (body == nullptr) {
        return Err(PhysicsError::kBodyNotFound);
      }
      out_positions[i] = body->position;
      out_rotations[i] = body->rotation;
    }
    return PhysicsResult<size_t>::Ok(body_ids.size());
  }

  auto GetLinearVelocity(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override
  {
    const auto* body = TryBody(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<Vec3>::Ok(body->linear_velocity);
  }

  auto GetAngularVelocity(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override
  {
    const auto* body = TryBody(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<Vec3>::Ok(body->angular_velocity);
  }

  auto SetLinearVelocity(WorldId world_id, BodyId body_id, const Vec3& velocity)
    -> PhysicsResult<void> override
  {
    auto* body = TryBodyMutable(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    body->linear_velocity = velocity;
    return PhysicsResult<void>::Ok();
  }

  auto SetAngularVelocity(WorldId world_id, BodyId body_id,
    const Vec3& velocity) -> PhysicsResult<void> override
  {
    auto* body = TryBodyMutable(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    body->angular_velocity = velocity;
    return PhysicsResult<void>::Ok();
  }

  auto AddForce(WorldId world_id, BodyId body_id, const Vec3&)
    -> PhysicsResult<void> override
  {
    if (TryBody(world_id, body_id) == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto AddImpulse(WorldId world_id, BodyId body_id, const Vec3&)
    -> PhysicsResult<void> override
  {
    if (TryBody(world_id, body_id) == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto AddTorque(WorldId world_id, BodyId body_id, const Vec3&)
    -> PhysicsResult<void> override
  {
    if (TryBody(world_id, body_id) == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto MoveKinematic(WorldId world_id, BodyId body_id,
    const Vec3& target_position, const Quat& target_rotation, float)
    -> PhysicsResult<void> override
  {
    auto* body = TryBodyMutable(world_id, body_id);
    if (body == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    body->position = target_position;
    body->rotation = target_rotation;
    state_->move_kinematic_calls += 1;
    state_->last_moved_body = body_id;
    state_->last_moved_position = target_position;
    state_->last_moved_rotation = target_rotation;
    return PhysicsResult<void>::Ok();
  }

  auto MoveKinematicBatch(WorldId world_id, std::span<const BodyId> body_ids,
    std::span<const Vec3> target_positions,
    std::span<const Quat> target_rotations, float delta_time)
    -> PhysicsResult<size_t> override
  {
    if (delta_time <= 0.0F) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (target_positions.size() < body_ids.size()
      || target_rotations.size() < body_ids.size()) {
      return Err(PhysicsError::kBufferTooSmall);
    }

    for (size_t i = 0; i < body_ids.size(); ++i) {
      auto* body = TryBodyMutable(world_id, body_ids[i]);
      if (body == nullptr) {
        return Err(PhysicsError::kBodyNotFound);
      }
      body->position = target_positions[i];
      body->rotation = target_rotations[i];
      state_->move_kinematic_calls += 1;
      state_->last_moved_body = body_ids[i];
      state_->last_moved_position = target_positions[i];
      state_->last_moved_rotation = target_rotations[i];
    }
    return PhysicsResult<size_t>::Ok(body_ids.size());
  }

  auto AddBodyShape(WorldId world_id, BodyId body_id, ShapeId, const Vec3&,
    const Quat&) -> PhysicsResult<ShapeInstanceId> override
  {
    if (TryBody(world_id, body_id) == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    const auto shape_instance_id = state_->next_shape_instance_id;
    state_->next_shape_instance_id
      = ShapeInstanceId { state_->next_shape_instance_id.get() + 1U };
    return PhysicsResult<ShapeInstanceId>::Ok(shape_instance_id);
  }

  auto RemoveBodyShape(WorldId world_id, BodyId body_id, ShapeInstanceId)
    -> PhysicsResult<void> override
  {
    if (TryBody(world_id, body_id) == nullptr) {
      return Err(PhysicsError::kBodyNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto FlushStructuralChanges(WorldId world_id)
    -> PhysicsResult<size_t> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    state_->flush_structural_calls += 1;
    return PhysicsResult<size_t>::Ok(0U);
  }

private:
  [[nodiscard]] auto TryBody(WorldId world_id, BodyId body_id) const
    -> const BodyState*
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return nullptr;
    }
    const auto it = state_->bodies.find(body_id);
    return it != state_->bodies.end() ? &it->second : nullptr;
  }

  [[nodiscard]] auto TryBodyMutable(WorldId world_id, BodyId body_id)
    -> BodyState*
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return nullptr;
    }
    const auto it = state_->bodies.find(body_id);
    return it != state_->bodies.end() ? &it->second : nullptr;
  }

  observer_ptr<BackendState> state_;
};

} // namespace oxygen::physics::test::detail
