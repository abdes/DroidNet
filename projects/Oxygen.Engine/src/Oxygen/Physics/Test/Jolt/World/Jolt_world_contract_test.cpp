//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltWorldContractTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltWorldContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  const auto step = worlds.Step(kInvalidWorldId, 1.0F / 60.0F, 1, 1.0F / 60.0F);
  ASSERT_TRUE(step.has_error());
  EXPECT_EQ(step.error(), PhysicsError::kWorldNotFound);

  const auto gravity = worlds.GetGravity(kInvalidWorldId);
  ASSERT_TRUE(gravity.has_error());
  EXPECT_EQ(gravity.error(), PhysicsError::kWorldNotFound);

  const auto destroy = worlds.DestroyWorld(kInvalidWorldId);
  ASSERT_TRUE(destroy.has_error());
  EXPECT_EQ(destroy.error(), PhysicsError::kWorldNotFound);
}

NOLINT_TEST_F(JoltWorldContractTest, StepRejectsInvalidArguments)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto zero_delta = worlds.Step(world_id, 0.0F, 1, 1.0F / 60.0F);
  ASSERT_TRUE(zero_delta.has_error());
  EXPECT_EQ(zero_delta.error(), PhysicsError::kInvalidArgument);

  const auto zero_fixed = worlds.Step(world_id, 1.0F / 60.0F, 1, 0.0F);
  ASSERT_TRUE(zero_fixed.has_error());
  EXPECT_EQ(zero_fixed.error(), PhysicsError::kInvalidArgument);

  const auto zero_sub_steps
    = worlds.Step(world_id, 1.0F / 60.0F, 0, 1.0F / 60.0F);
  ASSERT_TRUE(zero_sub_steps.has_error());
  EXPECT_EQ(zero_sub_steps.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltWorldContractTest, DestroyWorldTwiceReturnsWorldNotFound)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
  const auto destroy_again = worlds.DestroyWorld(world_id);
  ASSERT_TRUE(destroy_again.has_error());
  EXPECT_EQ(destroy_again.error(), PhysicsError::kWorldNotFound);
}

} // namespace oxygen::physics::test::jolt
