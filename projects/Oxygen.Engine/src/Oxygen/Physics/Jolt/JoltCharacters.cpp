//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltCharacters.h>

auto oxygen::physics::jolt::JoltCharacters::CreateCharacter(
  WorldId /*world_id*/, const character::CharacterDesc& /*desc*/)
  -> PhysicsResult<CharacterId>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltCharacters::DestroyCharacter(
  WorldId /*world_id*/, CharacterId /*character_id*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kCharacterNotFound);
}

auto oxygen::physics::jolt::JoltCharacters::MoveCharacter(WorldId /*world_id*/,
  CharacterId /*character_id*/, const character::CharacterMoveInput& /*input*/,
  float /*delta_time*/) -> PhysicsResult<character::CharacterMoveResult>
{
  return Err(PhysicsError::kCharacterNotFound);
}
