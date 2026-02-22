//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IPhysicsSystem.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::physics::test {

namespace detail {

  struct BodyState final {
    body::BodyType type { body::BodyType::kStatic };
    Vec3 position { 0.0F };
    Quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    Vec3 linear_velocity { 0.0F };
    Vec3 angular_velocity { 0.0F };
  };

  struct CharacterState final {
    Vec3 position { 0.0F };
    Quat rotation { 1.0F, 0.0F, 0.0F, 0.0F };
    Vec3 velocity { 0.0F };
  };

  struct BackendState final {
    WorldId world_id { WorldId { 1U } };
    bool world_created { false };
    bool world_destroyed { false };
    std::size_t step_count { 0 };
    float last_step_dt { 0.0F };
    float last_step_fixed_dt { 0.0F };
    Vec3 gravity { 0.0F, -9.81F, 0.0F };
    std::unordered_map<BodyId, BodyState> bodies {};
    std::unordered_map<CharacterId, CharacterState> characters {};
    std::vector<system::ActiveBodyTransform> active_transforms {};
    std::vector<events::PhysicsEvent> pending_events {};
    BodyId next_body_id { BodyId { 1U } };
    CharacterId next_character_id { CharacterId { 1U } };
    ShapeId next_shape_id { ShapeId { 1U } };
    ShapeInstanceId next_shape_instance_id { ShapeInstanceId { 1U } };
    AreaId next_area_id { AreaId { 1U } };
    JointId next_joint_id { JointId { 1U } };
    std::size_t move_kinematic_calls { 0 };
    std::size_t set_body_pose_calls { 0 };
    std::size_t character_create_calls { 0 };
    std::size_t character_destroy_calls { 0 };
    std::size_t character_move_calls { 0 };
    BodyId last_moved_body { kInvalidBodyId };
    Vec3 last_moved_position { 0.0F };
    Quat last_moved_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  };

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
    auto SetLinearVelocity(WorldId world_id, BodyId body_id,
      const Vec3& velocity) -> PhysicsResult<void> override
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

  class FakeQueryApi final : public system::IQueryApi {
  public:
    auto Raycast(WorldId, const query::RaycastDesc&) const
      -> PhysicsResult<query::OptionalRaycastHit> override
    {
      return PhysicsResult<query::OptionalRaycastHit>::Ok(
        query::OptionalRaycastHit {});
    }
    auto Sweep(WorldId, const query::SweepDesc&,
      std::span<query::SweepHit>) const -> PhysicsResult<size_t> override
    {
      return PhysicsResult<size_t>::Ok(size_t { 0 });
    }
    auto Overlap(WorldId, const query::OverlapDesc&, std::span<uint64_t>) const
      -> PhysicsResult<size_t> override
    {
      return PhysicsResult<size_t>::Ok(size_t { 0 });
    }
  };

  class FakeEventApi final : public system::IEventApi {
  public:
    explicit FakeEventApi(BackendState& state)
      : state_(&state)
    {
    }

    auto GetPendingEventCount(WorldId world_id) const
      -> PhysicsResult<size_t> override
    {
      if (world_id != state_->world_id || !state_->world_created) {
        return Err(PhysicsError::kWorldNotFound);
      }
      return PhysicsResult<size_t>::Ok(state_->pending_events.size());
    }

    auto DrainEvents(
      WorldId world_id, std::span<events::PhysicsEvent> out_events)
      -> PhysicsResult<size_t> override
    {
      if (world_id != state_->world_id || !state_->world_created) {
        return Err(PhysicsError::kWorldNotFound);
      }
      const auto count
        = std::min(out_events.size(), state_->pending_events.size());
      for (size_t i = 0; i < count; ++i) {
        out_events[i] = state_->pending_events[i];
      }
      state_->pending_events.erase(
        state_->pending_events.begin(), state_->pending_events.begin() + count);
      return PhysicsResult<size_t>::Ok(count);
    }

  private:
    observer_ptr<BackendState> state_;
  };

  class FakeCharacterApi final : public system::ICharacterApi {
  public:
    explicit FakeCharacterApi(BackendState& state)
      : state_(&state)
    {
    }

    auto CreateCharacter(WorldId world_id, const character::CharacterDesc& desc)
      -> PhysicsResult<CharacterId> override
    {
      if (world_id != state_->world_id || !state_->world_created) {
        return Err(PhysicsError::kWorldNotFound);
      }
      const auto character_id = state_->next_character_id;
      state_->next_character_id
        = CharacterId { state_->next_character_id.get() + 1U };
      state_->characters.insert_or_assign(character_id,
        CharacterState {
          .position = desc.initial_position,
          .rotation = desc.initial_rotation,
          .velocity = Vec3 { 0.0F, 0.0F, 0.0F },
        });
      state_->character_create_calls += 1;
      return PhysicsResult<CharacterId>::Ok(character_id);
    }
    auto DestroyCharacter(WorldId world_id, CharacterId character_id)
      -> PhysicsResult<void> override
    {
      if (world_id != state_->world_id || !state_->world_created) {
        return Err(PhysicsError::kWorldNotFound);
      }
      if (!state_->characters.contains(character_id)) {
        return Err(PhysicsError::kCharacterNotFound);
      }
      state_->characters.erase(character_id);
      state_->character_destroy_calls += 1;
      return PhysicsResult<void>::Ok();
    }

    auto MoveCharacter(WorldId world_id, CharacterId character_id,
      const character::CharacterMoveInput& input, float delta_time)
      -> PhysicsResult<character::CharacterMoveResult> override
    {
      if (world_id != state_->world_id || !state_->world_created) {
        return Err(PhysicsError::kWorldNotFound);
      }
      if (delta_time <= 0.0F) {
        return Err(PhysicsError::kInvalidArgument);
      }
      auto it = state_->characters.find(character_id);
      if (it == state_->characters.end()) {
        return Err(PhysicsError::kCharacterNotFound);
      }
      it->second.position += input.desired_velocity * delta_time;
      it->second.velocity = input.desired_velocity;
      state_->character_move_calls += 1;
      return PhysicsResult<character::CharacterMoveResult>::Ok(
        character::CharacterMoveResult {
          .state = character::CharacterState {
            .is_grounded = false,
            .position = it->second.position,
            .rotation = it->second.rotation,
            .velocity = input.desired_velocity,
          },
        });
    }

  private:
    observer_ptr<BackendState> state_;
  };

  class FakeShapeApi final : public system::IShapeApi {
  public:
    explicit FakeShapeApi(BackendState& state)
      : state_(&state)
    {
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

    auto AddAreaShape(WorldId world_id, AreaId, ShapeId, const Vec3&,
      const Quat&) -> PhysicsResult<ShapeInstanceId> override
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

  class FakeJointApi final : public system::IJointApi {
  public:
    explicit FakeJointApi(BackendState& state)
      : state_(&state)
    {
    }

    auto CreateJoint(WorldId world_id, const joint::JointDesc&)
      -> PhysicsResult<JointId> override
    {
      if (world_id != state_->world_id || !state_->world_created) {
        return Err(PhysicsError::kWorldNotFound);
      }
      const auto joint_id = state_->next_joint_id;
      state_->next_joint_id = JointId { state_->next_joint_id.get() + 1U };
      return PhysicsResult<JointId>::Ok(joint_id);
    }
    auto DestroyJoint(WorldId world_id, JointId) -> PhysicsResult<void> override
    {
      if (world_id != state_->world_id || !state_->world_created) {
        return Err(PhysicsError::kWorldNotFound);
      }
      return PhysicsResult<void>::Ok();
    }
    auto SetJointEnabled(WorldId world_id, JointId, bool)
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

  class FakePhysicsSystem final : public system::IPhysicsSystem {
  public:
    FakePhysicsSystem()
      : worlds_(state_)
      , bodies_(state_)
      , events_(state_)
      , characters_(state_)
      , shapes_(state_)
      , areas_(state_)
      , joints_(state_)
    {
    }

    [[nodiscard]] auto State() noexcept -> BackendState& { return state_; }
    [[nodiscard]] auto State() const noexcept -> const BackendState&
    {
      return state_;
    }

    auto Worlds() noexcept -> system::IWorldApi& override { return worlds_; }
    auto Bodies() noexcept -> system::IBodyApi& override { return bodies_; }
    auto Queries() noexcept -> system::IQueryApi& override { return queries_; }
    auto Events() noexcept -> system::IEventApi& override { return events_; }
    auto Characters() noexcept -> system::ICharacterApi& override
    {
      return characters_;
    }
    auto Shapes() noexcept -> system::IShapeApi& override { return shapes_; }
    auto Areas() noexcept -> system::IAreaApi& override { return areas_; }
    auto Joints() noexcept -> system::IJointApi& override { return joints_; }

    auto Worlds() const noexcept -> const system::IWorldApi& override
    {
      return worlds_;
    }
    auto Bodies() const noexcept -> const system::IBodyApi& override
    {
      return bodies_;
    }
    auto Queries() const noexcept -> const system::IQueryApi& override
    {
      return queries_;
    }
    auto Events() const noexcept -> const system::IEventApi& override
    {
      return events_;
    }
    auto Characters() const noexcept -> const system::ICharacterApi& override
    {
      return characters_;
    }
    auto Shapes() const noexcept -> const system::IShapeApi& override
    {
      return shapes_;
    }
    auto Areas() const noexcept -> const system::IAreaApi& override
    {
      return areas_;
    }
    auto Joints() const noexcept -> const system::IJointApi& override
    {
      return joints_;
    }

  private:
    BackendState state_ {};
    FakeWorldApi worlds_;
    FakeBodyApi bodies_;
    FakeQueryApi queries_ {};
    FakeEventApi events_;
    FakeCharacterApi characters_;
    FakeShapeApi shapes_;
    FakeAreaApi areas_;
    FakeJointApi joints_;
  };

} // namespace detail

class PhysicsModuleSyncTest : public testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<scene::Scene>("PhysicsModuleSyncTest", 128);
    frame_.SetScene(observer_ptr<scene::Scene> { scene_.get() });

    engine::ModuleTimingData timing {};
    timing.fixed_delta_time
      = time::CanonicalDuration { std::chrono::milliseconds(16) };
    frame_.SetModuleTimingData(
      timing, engine::internal::EngineTagFactory::Get());

    auto fake_physics = std::make_unique<detail::FakePhysicsSystem>();
    fake_physics_
      = observer_ptr<detail::FakePhysicsSystem> { fake_physics.get() };
    module_ = std::make_unique<PhysicsModule>(
      engine::ModulePriority { 100U }, std::move(fake_physics));
  }

  void TearDown() override
  {
    if (module_) {
      module_->OnShutdown();
    }
  }

  auto RunPhase(core::PhaseId phase,
    co::Co<> (PhysicsModule::*phase_fn)(observer_ptr<engine::FrameContext>))
    -> void
  {
    frame_.SetCurrentPhase(phase, engine::internal::EngineTagFactory::Get());
    co::Run(loop_, [&]() -> co::Co<> {
      co_await (module_.get()->*phase_fn)(
        observer_ptr<engine::FrameContext> { &frame_ });
      co_return;
    });
  }

  auto RunGameplay() -> void
  {
    RunPhase(core::PhaseId::kGameplay, &PhysicsModule::OnGameplay);
  }

  auto RunSceneMutation() -> void
  {
    RunPhase(core::PhaseId::kSceneMutation, &PhysicsModule::OnSceneMutation);
  }

  auto RunFixedSimulation() -> void
  {
    RunPhase(
      core::PhaseId::kFixedSimulation, &PhysicsModule::OnFixedSimulation);
  }

  [[nodiscard]] auto AttachBody(scene::SceneNode& node,
    const body::BodyType type) -> std::optional<RigidBodyFacade>
  {
    RunGameplay();
    scene_->Update();

    body::BodyDesc desc {};
    desc.type = type;
    return ScenePhysics::AttachRigidBody(
      observer_ptr<PhysicsModule> { module_.get() }, node, desc);
  }

  auto SetActiveScene(std::shared_ptr<scene::Scene> scene) -> void
  {
    scene_ = std::move(scene);
    frame_.SetScene(observer_ptr<scene::Scene> { scene_.get() });
  }

  [[nodiscard]] auto FakeState() noexcept -> detail::BackendState&
  {
    return fake_physics_->State();
  }

  std::shared_ptr<scene::Scene> scene_;
  engine::FrameContext frame_;
  co::testing::TestEventLoop loop_ {};
  std::unique_ptr<PhysicsModule> module_;
  observer_ptr<detail::FakePhysicsSystem> fake_physics_ {};
};

} // namespace oxygen::physics::test
