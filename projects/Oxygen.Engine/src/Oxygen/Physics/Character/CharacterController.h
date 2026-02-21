//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/CollisionLayers.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/Shape.h>

namespace oxygen::physics::character {

struct CharacterDesc final {
  CollisionShape shape { CapsuleShape { 0.5F, 1.0F } };
  Vec3 initial_position { 0.0F, 0.0F, 0.0F };
  Quat initial_rotation { 1.0F, 0.0F, 0.0F, 0.0F };
  float mass_kg { 80.0F };
  float max_slope_angle_radians { 0.785398F }; // 45 degrees
  float max_strength { 100.0F };
  float character_padding { 0.02F };
  float penetration_recovery_speed { 1.0F };
  float predictive_contact_distance { 0.1F };
  CollisionLayer collision_layer { kCollisionLayerDefault };
  CollisionMask collision_mask { kCollisionMaskAll };
  uint64_t user_data { 0 };
};

struct CharacterMoveInput final {
  Vec3 desired_velocity { 0.0F, 0.0F, 0.0F };
  bool jump_pressed { false };
};

struct CharacterState final {
  bool is_grounded { false };
  Vec3 velocity { 0.0F, 0.0F, 0.0F };
};

struct CharacterMoveResult final {
  CharacterState state {};
  std::optional<BodyId> hit_body {};
};

class ICharacterController {
public:
  ICharacterController() = default;
  virtual ~ICharacterController() = default;

  OXYGEN_MAKE_NON_COPYABLE(ICharacterController)
  OXYGEN_MAKE_NON_MOVABLE(ICharacterController)

  virtual auto GetId() const noexcept -> CharacterId = 0;
};

} // namespace oxygen::physics::character
