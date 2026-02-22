//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltEventBasicTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltEventBasicTest, BackendAvailabilityMatchesContract)
{
  AssertBackendAvailabilityContract();
}

NOLINT_TEST_F(JoltEventBasicTest, EmptyWorldHasNoPendingEvents)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& events = System().Events();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto pending = events.GetPendingEventCount(world_id);
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending.value(), 0U);

  std::array<events::PhysicsEvent, 4> drained {};
  const auto drain_result = events.DrainEvents(world_id, drained);
  ASSERT_TRUE(drain_result.has_value());
  EXPECT_EQ(drain_result.value(), 0U);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
