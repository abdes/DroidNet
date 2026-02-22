//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>

#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltAggregateBasicTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltAggregateBasicTest, AggregateApiIsAvailable)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }
  EXPECT_NE(System().Aggregates(), nullptr);
}

NOLINT_TEST_F(JoltAggregateBasicTest, CreateAndDestroyAggregateSucceeds)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* aggregates = System().Aggregates();
  ASSERT_NE(aggregates, nullptr);
  auto& worlds = System().Worlds();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto aggregate = aggregates->CreateAggregate(world_id);
  ASSERT_TRUE(aggregate.has_value());
  EXPECT_NE(aggregate.value(), kInvalidAggregateId);
  EXPECT_TRUE(
    aggregates->DestroyAggregate(world_id, aggregate.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
