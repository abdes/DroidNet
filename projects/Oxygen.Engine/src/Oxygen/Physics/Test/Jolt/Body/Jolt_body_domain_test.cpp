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

  class JoltBodyDomainTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltBodyDomainTest, InitialPoseRoundTrip)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kDynamic;
  desc.initial_position = Vec3 { 1.5F, 2.5F, -3.5F };
  desc.initial_rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F };
  const auto body_result = bodies.CreateBody(world_id, desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  const auto position = bodies.GetBodyPosition(world_id, body_id);
  const auto rotation = bodies.GetBodyRotation(world_id, body_id);
  ASSERT_TRUE(position.has_value());
  ASSERT_TRUE(rotation.has_value());
  EXPECT_FLOAT_EQ(position.value().x, desc.initial_position.x);
  EXPECT_FLOAT_EQ(position.value().y, desc.initial_position.y);
  EXPECT_FLOAT_EQ(position.value().z, desc.initial_position.z);
  EXPECT_FLOAT_EQ(rotation.value().w, desc.initial_rotation.w);
  EXPECT_FLOAT_EQ(rotation.value().x, desc.initial_rotation.x);
  EXPECT_FLOAT_EQ(rotation.value().y, desc.initial_rotation.y);
  EXPECT_FLOAT_EQ(rotation.value().z, desc.initial_rotation.z);

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltBodyDomainTest, SetPoseAndVelocityRoundTrip)
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

  ASSERT_TRUE(bodies
      .SetBodyPose(world_id, body_id, Vec3 { 5.0F, 6.0F, 7.0F },
        Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_value());
  const auto position = bodies.GetBodyPosition(world_id, body_id);
  ASSERT_TRUE(position.has_value());
  EXPECT_FLOAT_EQ(position.value().x, 5.0F);
  EXPECT_FLOAT_EQ(position.value().y, 6.0F);
  EXPECT_FLOAT_EQ(position.value().z, 7.0F);

  ASSERT_TRUE(
    bodies.SetLinearVelocity(world_id, body_id, Vec3 { 0.5F, 1.5F, 2.5F })
      .has_value());
  const auto linear_velocity = bodies.GetLinearVelocity(world_id, body_id);
  ASSERT_TRUE(linear_velocity.has_value());
  EXPECT_FLOAT_EQ(linear_velocity.value().x, 0.5F);
  EXPECT_FLOAT_EQ(linear_velocity.value().y, 1.5F);
  EXPECT_FLOAT_EQ(linear_velocity.value().z, 2.5F);

  ASSERT_TRUE(
    bodies.SetAngularVelocity(world_id, body_id, Vec3 { 3.0F, 4.0F, 5.0F })
      .has_value());
  const auto angular_velocity = bodies.GetAngularVelocity(world_id, body_id);
  ASSERT_TRUE(angular_velocity.has_value());
  EXPECT_FLOAT_EQ(angular_velocity.value().x, 3.0F);
  EXPECT_FLOAT_EQ(angular_velocity.value().y, 4.0F);
  EXPECT_FLOAT_EQ(angular_velocity.value().z, 5.0F);

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltBodyDomainTest, KinematicMoveReachesTargetAfterStep)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kKinematic;
  const auto body_result = bodies.CreateBody(world_id, desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  ASSERT_TRUE(bodies
      .MoveKinematic(world_id, body_id, Vec3 { 2.0F, 3.0F, 4.0F },
        Quat { 1.0F, 0.0F, 0.0F, 0.0F }, 1.0F / 60.0F)
      .has_value());
  ASSERT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());

  const auto position = bodies.GetBodyPosition(world_id, body_id);
  ASSERT_TRUE(position.has_value());
  EXPECT_NEAR(position.value().x, 2.0F, 0.05F);
  EXPECT_NEAR(position.value().y, 3.0F, 0.05F);
  EXPECT_NEAR(position.value().z, 4.0F, 0.05F);

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
