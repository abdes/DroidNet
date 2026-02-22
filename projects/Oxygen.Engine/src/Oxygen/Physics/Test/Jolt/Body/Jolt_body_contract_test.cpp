//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltBodyContractTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltBodyContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  RequireBackend();

  auto& bodies = System().Bodies();
  const auto create_result
    = bodies.CreateBody(kInvalidWorldId, body::BodyDesc {});
  ASSERT_TRUE(create_result.has_error());
  EXPECT_EQ(create_result.error(), PhysicsError::kWorldNotFound);

  const auto get_result
    = bodies.GetBodyPosition(kInvalidWorldId, kInvalidBodyId);
  ASSERT_TRUE(get_result.has_error());
  EXPECT_EQ(get_result.error(), PhysicsError::kWorldNotFound);

  const auto destroy_result
    = bodies.DestroyBody(kInvalidWorldId, kInvalidBodyId);
  ASSERT_TRUE(destroy_result.has_error());
  EXPECT_EQ(destroy_result.error(), PhysicsError::kWorldNotFound);
}

NOLINT_TEST_F(JoltBodyContractTest, InvalidBodyCallsReturnBodyNotFound)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto get_position = bodies.GetBodyPosition(world_id, kInvalidBodyId);
  ASSERT_TRUE(get_position.has_error());
  EXPECT_EQ(get_position.error(), PhysicsError::kBodyNotFound);

  const auto destroy_body = bodies.DestroyBody(world_id, kInvalidBodyId);
  ASSERT_TRUE(destroy_body.has_error());
  EXPECT_EQ(destroy_body.error(), PhysicsError::kBodyNotFound);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltBodyContractTest, DestroyBodyTwiceReturnsBodyNotFound)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();
  const auto body_result = bodies.CreateBody(world_id, body::BodyDesc {});
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());

  const auto destroy_again = bodies.DestroyBody(world_id, body_id);
  ASSERT_TRUE(destroy_again.has_error());
  EXPECT_EQ(destroy_again.error(), PhysicsError::kBodyNotFound);
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltBodyContractTest, MoveKinematicRejectsNonKinematicBody)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kDynamic;
  const auto body_result = bodies.CreateBody(world_id, desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  const auto move_result = bodies.MoveKinematic(world_id, body_id,
    Vec3 { 1.0F, 2.0F, 3.0F }, Quat { 1.0F, 0.0F, 0.0F, 0.0F }, 1.0F / 60.0F);
  ASSERT_TRUE(move_result.has_error());
  EXPECT_EQ(move_result.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
