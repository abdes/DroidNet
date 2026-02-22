//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Character/CharacterController.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltCharacterDomainTest : public JoltTestFixture {
  protected:
    auto RequireBackend() -> void
    {
      AssertBackendAvailabilityContract();
      if (!HasBackend()) {
        GTEST_SKIP() << "No physics backend available.";
      }
    }
  };

} // namespace

NOLINT_TEST_F(JoltCharacterDomainTest, MoveReturnsDesiredVelocity)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& characters = System().Characters();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  character::CharacterDesc desc {};
  desc.initial_position = Vec3 { 1.0F, 2.0F, 3.0F };
  const auto create_character = characters.CreateCharacter(world_id, desc);
  ASSERT_TRUE(create_character.has_value());
  const auto character_id = create_character.value();

  const auto move_result = characters.MoveCharacter(world_id, character_id,
    character::CharacterMoveInput {
      .desired_velocity = Vec3 { 2.0F, 0.0F, -1.0F }, .jump_pressed = false },
    1.0F / 60.0F);
  ASSERT_TRUE(move_result.has_value());
  EXPECT_FLOAT_EQ(move_result.value().state.velocity.x, 2.0F);
  EXPECT_FLOAT_EQ(move_result.value().state.velocity.y, 0.0F);
  EXPECT_FLOAT_EQ(move_result.value().state.velocity.z, -1.0F);
  EXPECT_GT(move_result.value().state.position.x, 1.0F);
  EXPECT_FLOAT_EQ(move_result.value().state.position.y, 2.0F);
  EXPECT_LT(move_result.value().state.position.z, 3.0F);
  EXPECT_FLOAT_EQ(move_result.value().state.rotation.w, 1.0F);
  EXPECT_FLOAT_EQ(move_result.value().state.rotation.x, 0.0F);
  EXPECT_FLOAT_EQ(move_result.value().state.rotation.y, 0.0F);
  EXPECT_FLOAT_EQ(move_result.value().state.rotation.z, 0.0F);
  EXPECT_FALSE(move_result.value().state.is_grounded);
  EXPECT_FALSE(move_result.value().hit_body.has_value());

  EXPECT_TRUE(characters.DestroyCharacter(world_id, character_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltCharacterDomainTest, MoveWithNonPositiveDeltaTimeReturnsError)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& characters = System().Characters();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto create_character
    = characters.CreateCharacter(world_id, character::CharacterDesc {});
  ASSERT_TRUE(create_character.has_value());
  const auto character_id = create_character.value();

  EXPECT_TRUE(characters
      .MoveCharacter(
        world_id, character_id, character::CharacterMoveInput {}, 0.0F)
      .has_error());
  EXPECT_TRUE(characters
      .MoveCharacter(
        world_id, character_id, character::CharacterMoveInput {}, -1.0F)
      .has_error());

  EXPECT_TRUE(characters.DestroyCharacter(world_id, character_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
