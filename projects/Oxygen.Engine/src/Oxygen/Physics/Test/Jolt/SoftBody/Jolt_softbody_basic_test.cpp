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

  class JoltSoftBodyBasicTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltSoftBodyBasicTest, CreateAndDestroySoftBodySucceeds)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& soft_bodies = System().SoftBodies();
  auto& worlds = System().Worlds();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto soft_body = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 4U,
    });
  ASSERT_TRUE(soft_body.has_value());
  EXPECT_TRUE(IsValid(soft_body.value()));

  EXPECT_TRUE(
    soft_bodies.DestroySoftBody(world_id, soft_body.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
