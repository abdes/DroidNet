//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltArticulationContractTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(
  JoltArticulationContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* articulations = System().Articulations();
  ASSERT_NE(articulations, nullptr);

  EXPECT_TRUE(articulations
      ->CreateArticulation(kInvalidWorldId, articulation::ArticulationDesc {})
      .has_error());
  EXPECT_TRUE(
    articulations->DestroyArticulation(kInvalidWorldId, kInvalidAggregateId)
      .has_error());
  EXPECT_TRUE(articulations
      ->AddLink(kInvalidWorldId, kInvalidAggregateId,
        articulation::ArticulationLinkDesc {})
      .has_error());
  EXPECT_TRUE(articulations
      ->RemoveLink(kInvalidWorldId, kInvalidAggregateId, kInvalidBodyId)
      .has_error());
  EXPECT_TRUE(articulations->GetRootBody(kInvalidWorldId, kInvalidAggregateId)
      .has_error());
  std::vector<BodyId> out(2, kInvalidBodyId);
  EXPECT_TRUE(
    articulations->GetLinkBodies(kInvalidWorldId, kInvalidAggregateId, out)
      .has_error());
  EXPECT_TRUE(articulations->GetAuthority(kInvalidWorldId, kInvalidAggregateId)
      .has_error());
  EXPECT_TRUE(
    articulations->FlushStructuralChanges(kInvalidWorldId).has_error());
}

NOLINT_TEST_F(
  JoltArticulationContractTest, UnknownArticulationReturnsInvalidArgument)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* articulations = System().Articulations();
  ASSERT_NE(articulations, nullptr);
  auto& worlds = System().Worlds();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto destroy
    = articulations->DestroyArticulation(world_id, AggregateId { 9999U });
  ASSERT_TRUE(destroy.has_error());
  EXPECT_EQ(destroy.error(), PhysicsError::kInvalidArgument);
  const auto root_query
    = articulations->GetRootBody(world_id, AggregateId { 9999U });
  ASSERT_TRUE(root_query.has_error());
  EXPECT_EQ(root_query.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltArticulationContractTest, GetLinkBodiesReportsBufferTooSmall)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* articulations = System().Articulations();
  ASSERT_NE(articulations, nullptr);
  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  body::BodyDesc desc {};
  desc.type = body::BodyType::kDynamic;
  const auto root = bodies.CreateBody(world_id, desc);
  const auto child = bodies.CreateBody(world_id, desc);
  ASSERT_TRUE(root.has_value());
  ASSERT_TRUE(child.has_value());

  const auto articulation = articulations->CreateArticulation(world_id,
    articulation::ArticulationDesc {
      .root_body_id = root.value(),
    });
  ASSERT_TRUE(articulation.has_value());
  ASSERT_TRUE(articulations
      ->AddLink(world_id, articulation.value(),
        articulation::ArticulationLinkDesc {
          .parent_body_id = root.value(),
          .child_body_id = child.value(),
        })
      .has_value());

  std::vector<BodyId> out(0);
  const auto get_links
    = articulations->GetLinkBodies(world_id, articulation.value(), out);
  ASSERT_TRUE(get_links.has_error());
  EXPECT_EQ(get_links.error(), PhysicsError::kBufferTooSmall);

  EXPECT_TRUE(articulations->DestroyArticulation(world_id, articulation.value())
      .has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, child.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, root.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
