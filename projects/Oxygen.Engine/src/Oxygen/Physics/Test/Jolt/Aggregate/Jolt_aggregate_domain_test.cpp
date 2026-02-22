//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>

#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltAggregateDomainTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltAggregateDomainTest, MembershipRoundTripAndRebind)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& aggregates = System().Aggregates();
  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kDynamic;
  const auto body_a = bodies.CreateBody(world_id, desc);
  const auto body_b = bodies.CreateBody(world_id, desc);
  ASSERT_TRUE(body_a.has_value());
  ASSERT_TRUE(body_b.has_value());

  const auto aggregate_a = aggregates.CreateAggregate(world_id);
  const auto aggregate_b = aggregates.CreateAggregate(world_id);
  ASSERT_TRUE(aggregate_a.has_value());
  ASSERT_TRUE(aggregate_b.has_value());

  ASSERT_TRUE(
    aggregates.AddMemberBody(world_id, aggregate_a.value(), body_a.value())
      .has_value());
  ASSERT_TRUE(
    aggregates.AddMemberBody(world_id, aggregate_a.value(), body_b.value())
      .has_value());

  std::vector<BodyId> members(8, kInvalidBodyId);
  auto count
    = aggregates.GetMemberBodies(world_id, aggregate_a.value(), members);
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(count.value(), 2U);

  ASSERT_TRUE(
    aggregates.RemoveMemberBody(world_id, aggregate_a.value(), body_b.value())
      .has_value());
  ASSERT_TRUE(
    aggregates.AddMemberBody(world_id, aggregate_b.value(), body_b.value())
      .has_value());

  std::fill(members.begin(), members.end(), kInvalidBodyId);
  count = aggregates.GetMemberBodies(world_id, aggregate_b.value(), members);
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(count.value(), 1U);
  EXPECT_EQ(members[0], body_b.value());

  const auto flush = aggregates.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush.has_value());
  EXPECT_EQ(flush.value(), 6U);
  const auto flush_again = aggregates.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_again.has_value());
  EXPECT_EQ(flush_again.value(), 0U);
  EXPECT_TRUE(
    aggregates.DestroyAggregate(world_id, aggregate_a.value()).has_value());
  EXPECT_TRUE(
    aggregates.DestroyAggregate(world_id, aggregate_b.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_a.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_b.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
