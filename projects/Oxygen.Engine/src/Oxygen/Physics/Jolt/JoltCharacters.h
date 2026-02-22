//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/System/ICharacterApi.h>

namespace oxygen::physics::jolt {

class JoltWorld;

//! Jolt implementation of the character domain.
class JoltCharacters final : public system::ICharacterApi {
public:
  explicit JoltCharacters(JoltWorld& world);
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

private:
  struct CharacterState final {
    WorldId world_id { kInvalidWorldId };
    JPH::BodyID jolt_body_id {};
    Vec3 linear_velocity { 0.0F, 0.0F, 0.0F };
  };

  observer_ptr<JoltWorld> world_ {};
  mutable std::mutex mutex_ {};
  uint32_t next_character_id_ { 1U };
  std::unordered_map<CharacterId, CharacterState> characters_ {};
};

} // namespace oxygen::physics::jolt
