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

  class JoltCharacterContractTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltCharacterContractTest, CharacterLifecycleContract)
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
  EXPECT_NE(character_id, kInvalidCharacterId);

  const auto move = characters.MoveCharacter(
    world_id, character_id, character::CharacterMoveInput {}, 1.0F / 60.0F);
  EXPECT_TRUE(move.has_value());

  EXPECT_TRUE(characters.DestroyCharacter(world_id, character_id).has_value());
  EXPECT_TRUE(characters
      .MoveCharacter(
        world_id, character_id, character::CharacterMoveInput {}, 1.0F / 60.0F)
      .has_error());
  EXPECT_TRUE(characters.DestroyCharacter(world_id, character_id).has_error());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltCharacterContractTest, InvalidCharacterCallsReturnError)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& characters = System().Characters();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  EXPECT_TRUE(
    characters.DestroyCharacter(world_id, kInvalidCharacterId).has_error());
  EXPECT_TRUE(characters
      .MoveCharacter(world_id, kInvalidCharacterId,
        character::CharacterMoveInput {}, 1.0F / 60.0F)
      .has_error());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
