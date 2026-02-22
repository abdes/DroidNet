//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/ICharacterApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

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

} // namespace oxygen::physics::test::detail
