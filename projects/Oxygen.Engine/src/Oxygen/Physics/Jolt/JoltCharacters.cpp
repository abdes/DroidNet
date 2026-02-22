//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltCharacters.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>

oxygen::physics::jolt::JoltCharacters::JoltCharacters(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltCharacters::CreateCharacter(
  const WorldId world_id, const character::CharacterDesc& desc)
  -> PhysicsResult<CharacterId>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto shape_result = MakeShape(desc.shape);
  if (shape_result.has_error()) {
    return Err(shape_result.error());
  }

  JPH::BodyCreationSettings settings(shape_result.value(),
    ToJoltRVec3(desc.initial_position), ToJoltQuat(desc.initial_rotation),
    JPH::EMotionType::Kinematic, ToObjectLayer(body::BodyType::kKinematic));
  settings.mIsSensor = false;
  settings.mGravityFactor = 1.0F;
  settings.mUserData = desc.user_data;
  settings.mMotionQuality = JPH::EMotionQuality::LinearCast;

  const auto jolt_body_id
    = body_interface->CreateAndAddBody(settings, JPH::EActivation::Activate);
  if (jolt_body_id.IsInvalid()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  std::scoped_lock lock(mutex_);
  if (next_character_id_ == std::numeric_limits<uint32_t>::max()) {
    body_interface->RemoveBody(jolt_body_id);
    body_interface->DestroyBody(jolt_body_id);
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto character_id = CharacterId { next_character_id_++ };
  characters_.insert_or_assign(character_id,
    CharacterState {
      .world_id = world_id,
      .jolt_body_id = jolt_body_id,
      .linear_velocity = Vec3 { 0.0F, 0.0F, 0.0F },
    });
  return Ok(character_id);
}

auto oxygen::physics::jolt::JoltCharacters::DestroyCharacter(
  const WorldId world_id, const CharacterId character_id) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }

  CharacterState state {};
  {
    std::scoped_lock lock(mutex_);
    const auto it = characters_.find(character_id);
    if (it == characters_.end()) {
      return Err(PhysicsError::kCharacterNotFound);
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kCharacterNotFound);
    }
    state = it->second;
    characters_.erase(it);
  }

  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  body_interface->RemoveBody(state.jolt_body_id);
  body_interface->DestroyBody(state.jolt_body_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltCharacters::MoveCharacter(
  const WorldId world_id, const CharacterId character_id,
  const character::CharacterMoveInput& input, const float delta_time)
  -> PhysicsResult<character::CharacterMoveResult>
{
  if (delta_time <= 0.0F) {
    return Err(PhysicsError::kInvalidArgument);
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  CharacterState state {};
  {
    std::scoped_lock lock(mutex_);
    const auto it = characters_.find(character_id);
    if (it == characters_.end()) {
      return Err(PhysicsError::kCharacterNotFound);
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kCharacterNotFound);
    }
    state = it->second;
  }

  const auto current_position = body_interface->GetPosition(state.jolt_body_id);
  const auto current_rotation = body_interface->GetRotation(state.jolt_body_id);
  const auto delta = input.desired_velocity * delta_time;
  const auto target_position = Vec3 {
    static_cast<float>(current_position.GetX()) + delta.x,
    static_cast<float>(current_position.GetY()) + delta.y,
    static_cast<float>(current_position.GetZ()) + delta.z,
  };
  body_interface->MoveKinematic(state.jolt_body_id,
    ToJoltRVec3(target_position), current_rotation, delta_time);

  {
    std::scoped_lock lock(mutex_);
    auto it = characters_.find(character_id);
    if (it != characters_.end()) {
      it->second.linear_velocity = input.desired_velocity;
    }
  }

  return Ok(character::CharacterMoveResult {
    .state = character::CharacterState {
      .is_grounded = false,
      .position = target_position,
      .rotation = Quat {
        current_rotation.GetW(),
        current_rotation.GetX(),
        current_rotation.GetY(),
        current_rotation.GetZ(),
      },
      .velocity = input.desired_velocity,
    },
    .hit_body = std::nullopt,
  });
}
