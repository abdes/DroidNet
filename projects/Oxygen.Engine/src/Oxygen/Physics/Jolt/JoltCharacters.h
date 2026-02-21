//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/System/ICharacterApi.h>

namespace oxygen::physics::jolt {

//! Jolt implementation of the character domain.
class JoltCharacters final : public system::ICharacterApi {
public:
  JoltCharacters() = default;
  ~JoltCharacters() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltCharacters)
  OXYGEN_MAKE_NON_MOVABLE(JoltCharacters)

  auto CreateCharacter(WorldId world_id, const character::CharacterDesc& desc)
    -> PhysicsResult<CharacterId> override;
  auto DestroyCharacter(WorldId world_id, CharacterId character_id)
    -> PhysicsResult<void> override;

  auto MoveCharacter(WorldId world_id, CharacterId character_id,
    const character::CharacterMoveInput& input, float delta_time)
    -> PhysicsResult<character::CharacterMoveResult> override;
};

} // namespace oxygen::physics::jolt
