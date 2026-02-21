//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Character/CharacterController.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>

namespace oxygen::physics::system {

//! Character controller domain API.
/*!
 Responsibilities now:
 - Host character-controller lifecycle and movement and collision behavior.
 - Expose character-specific state and control.

 ### Near Future

 - Support crouch and stand transitions, slope and step tuning, moving
   platforms, and bridges to articulation and ragdoll features.
*/
class ICharacterApi {
public:
  ICharacterApi() = default;
  virtual ~ICharacterApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(ICharacterApi)
  OXYGEN_MAKE_NON_MOVABLE(ICharacterApi)

  virtual auto CreateCharacter(WorldId world_id,
    const character::CharacterDesc& desc) -> PhysicsResult<CharacterId>
    = 0;
  virtual auto DestroyCharacter(WorldId world_id, CharacterId character_id)
    -> PhysicsResult<void>
    = 0;

  virtual auto MoveCharacter(WorldId world_id, CharacterId character_id,
    const character::CharacterMoveInput& input, float delta_time)
    -> PhysicsResult<character::CharacterMoveResult>
    = 0;
};

} // namespace oxygen::physics::system
