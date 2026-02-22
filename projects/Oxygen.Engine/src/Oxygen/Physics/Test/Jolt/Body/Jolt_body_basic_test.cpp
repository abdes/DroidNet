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

  class JoltBodyBasicTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltBodyBasicTest, CreateAndDestroyBodySucceeds)
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
  EXPECT_NE(body_id, kInvalidBodyId);

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltBodyBasicTest, BodyIdsAreUniqueWithinWorld)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto body_a_result = bodies.CreateBody(world_id, body::BodyDesc {});
  const auto body_b_result = bodies.CreateBody(world_id, body::BodyDesc {});
  ASSERT_TRUE(body_a_result.has_value());
  ASSERT_TRUE(body_b_result.has_value());
  EXPECT_NE(body_a_result.value(), body_b_result.value());

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_a_result.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_b_result.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
