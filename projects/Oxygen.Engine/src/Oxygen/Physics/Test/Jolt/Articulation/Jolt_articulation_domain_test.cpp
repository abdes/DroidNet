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
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltArticulationDomainTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltArticulationDomainTest, LinkLifecycleAndAuthorityContract)
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

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto root = bodies.CreateBody(world_id, body_desc);
  const auto child_a = bodies.CreateBody(world_id, body_desc);
  const auto child_b = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(root.has_value());
  ASSERT_TRUE(child_a.has_value());
  ASSERT_TRUE(child_b.has_value());

  const auto articulation = articulations->CreateArticulation(world_id,
    articulation::ArticulationDesc {
      .root_body_id = root.value(),
    });
  ASSERT_TRUE(articulation.has_value());
  const auto articulation_id = articulation.value();

  const auto queried_root
    = articulations->GetRootBody(world_id, articulation_id);
  ASSERT_TRUE(queried_root.has_value());
  EXPECT_EQ(queried_root.value(), root.value());

  const auto authority = articulations->GetAuthority(world_id, articulation_id);
  ASSERT_TRUE(authority.has_value());
  EXPECT_EQ(authority.value(), aggregate::AggregateAuthority::kSimulation);

  EXPECT_TRUE(articulations
      ->AddLink(world_id, articulation_id,
        articulation::ArticulationLinkDesc {
          .parent_body_id = root.value(),
          .child_body_id = child_a.value(),
        })
      .has_value());
  EXPECT_TRUE(articulations
      ->AddLink(world_id, articulation_id,
        articulation::ArticulationLinkDesc {
          .parent_body_id = child_a.value(),
          .child_body_id = child_b.value(),
        })
      .has_value());

  std::vector<BodyId> links(4, kInvalidBodyId);
  const auto get_links
    = articulations->GetLinkBodies(world_id, articulation_id, links);
  ASSERT_TRUE(get_links.has_value());
  ASSERT_EQ(get_links.value(), 2U);
  EXPECT_NE(std::find(links.begin(), links.begin() + 2, child_a.value()),
    links.begin() + 2);
  EXPECT_NE(std::find(links.begin(), links.begin() + 2, child_b.value()),
    links.begin() + 2);

  EXPECT_TRUE(
    articulations->RemoveLink(world_id, articulation_id, child_b.value())
      .has_value());

  const auto flush = articulations->FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush.has_value());
  EXPECT_EQ(flush.value(), 4U);
  const auto flush_again = articulations->FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_again.has_value());
  EXPECT_EQ(flush_again.value(), 0U);

  EXPECT_TRUE(
    articulations->DestroyArticulation(world_id, articulation_id).has_value());
  const auto flush_destroy = articulations->FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_destroy.has_value());
  EXPECT_EQ(flush_destroy.value(), 1U);
  EXPECT_TRUE(bodies.DestroyBody(world_id, child_b.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, child_a.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, root.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
