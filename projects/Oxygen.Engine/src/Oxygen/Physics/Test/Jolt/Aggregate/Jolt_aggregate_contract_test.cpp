//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltAggregateContractTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltAggregateContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* aggregates = System().Aggregates();
  ASSERT_NE(aggregates, nullptr);

  EXPECT_TRUE(aggregates->CreateAggregate(kInvalidWorldId).has_error());
  EXPECT_TRUE(aggregates->DestroyAggregate(kInvalidWorldId, kInvalidAggregateId)
      .has_error());
  EXPECT_TRUE(aggregates
      ->AddMemberBody(kInvalidWorldId, kInvalidAggregateId, kInvalidBodyId)
      .has_error());
  EXPECT_TRUE(aggregates
      ->RemoveMemberBody(kInvalidWorldId, kInvalidAggregateId, kInvalidBodyId)
      .has_error());
  std::vector<BodyId> out(4, kInvalidBodyId);
  EXPECT_TRUE(
    aggregates->GetMemberBodies(kInvalidWorldId, kInvalidAggregateId, out)
      .has_error());
  EXPECT_TRUE(aggregates->FlushStructuralChanges(kInvalidWorldId).has_error());
}

NOLINT_TEST_F(JoltAggregateContractTest, GetMemberBodiesReportsBufferTooSmall)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* aggregates = System().Aggregates();
  ASSERT_NE(aggregates, nullptr);
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

  const auto aggregate = aggregates->CreateAggregate(world_id);
  ASSERT_TRUE(aggregate.has_value());
  ASSERT_TRUE(
    aggregates->AddMemberBody(world_id, aggregate.value(), body_a.value())
      .has_value());
  ASSERT_TRUE(
    aggregates->AddMemberBody(world_id, aggregate.value(), body_b.value())
      .has_value());

  std::vector<BodyId> out(1, kInvalidBodyId);
  const auto members
    = aggregates->GetMemberBodies(world_id, aggregate.value(), out);
  EXPECT_TRUE(members.has_error());
  EXPECT_EQ(members.error(), PhysicsError::kBufferTooSmall);

  EXPECT_TRUE(
    aggregates->DestroyAggregate(world_id, aggregate.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_a.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_b.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltAggregateContractTest, UnknownAggregateIdReturnsInvalidArgument)
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

  const auto destroy
    = aggregates->DestroyAggregate(world_id, AggregateId { 123456U });
  ASSERT_TRUE(destroy.has_error());
  EXPECT_EQ(destroy.error(), PhysicsError::kInvalidArgument);

  std::vector<BodyId> out(2, kInvalidBodyId);
  const auto members
    = aggregates->GetMemberBodies(world_id, AggregateId { 123456U }, out);
  ASSERT_TRUE(members.has_error());
  EXPECT_EQ(members.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
