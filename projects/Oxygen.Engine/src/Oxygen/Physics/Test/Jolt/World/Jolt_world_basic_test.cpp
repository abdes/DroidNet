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

  class JoltWorldBasicTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltWorldBasicTest, BackendAvailabilityMatchesContract)
{
  AssertBackendAvailabilityContract();
}

NOLINT_TEST_F(JoltWorldBasicTest, CreateAndDestroyWorldSucceeds)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();
  EXPECT_NE(world_id, kInvalidWorldId);
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltWorldBasicTest, CreateWorldReturnsUniqueIds)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  const auto world_a_result = worlds.CreateWorld(world::WorldDesc {});
  const auto world_b_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_a_result.has_value());
  ASSERT_TRUE(world_b_result.has_value());
  EXPECT_NE(world_a_result.value(), world_b_result.value());
  EXPECT_TRUE(worlds.DestroyWorld(world_a_result.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_b_result.value()).has_value());
}

} // namespace oxygen::physics::test::jolt
