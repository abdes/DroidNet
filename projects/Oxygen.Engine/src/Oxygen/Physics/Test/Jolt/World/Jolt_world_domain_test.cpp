//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/System/IWorldApi.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltWorldDomainTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltWorldDomainTest, GravityRoundTripIsPerWorld)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  const auto world_a_result = worlds.CreateWorld(world::WorldDesc {});
  const auto world_b_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_a_result.has_value());
  ASSERT_TRUE(world_b_result.has_value());
  const auto world_a = world_a_result.value();
  const auto world_b = world_b_result.value();

  ASSERT_TRUE(
    worlds.SetGravity(world_a, Vec3 { 0.0F, -4.0F, 0.0F }).has_value());
  ASSERT_TRUE(
    worlds.SetGravity(world_b, Vec3 { 0.0F, -9.8F, 0.0F }).has_value());

  const auto gravity_a = worlds.GetGravity(world_a);
  const auto gravity_b = worlds.GetGravity(world_b);
  ASSERT_TRUE(gravity_a.has_value());
  ASSERT_TRUE(gravity_b.has_value());
  EXPECT_FLOAT_EQ(gravity_a.value().y, -4.0F);
  EXPECT_FLOAT_EQ(gravity_b.value().y, -9.8F);

  EXPECT_TRUE(worlds.DestroyWorld(world_a).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_b).has_value());
}

NOLINT_TEST_F(JoltWorldDomainTest, StepOnEmptyWorldSucceeds)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  EXPECT_TRUE(
    worlds.Step(world_id, 1.0F / 60.0F, 4, 1.0F / 120.0F).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltWorldDomainTest, ActiveBodyTransformsEmptyWhenNoBodies)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  std::array<system::ActiveBodyTransform, 8> transforms {};
  const auto query = worlds.GetActiveBodyTransforms(world_id, transforms);
  ASSERT_TRUE(query.has_value());
  EXPECT_EQ(query.value(), 0U);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
