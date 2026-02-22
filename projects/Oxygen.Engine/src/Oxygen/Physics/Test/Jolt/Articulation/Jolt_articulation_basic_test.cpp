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

  class JoltArticulationBasicTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltArticulationBasicTest, CreateAndDestroyArticulationSucceeds)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& articulations = System().Articulations();
  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto root_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(root_result.has_value());

  const auto articulation = articulations.CreateArticulation(world_id,
    articulation::ArticulationDesc {
      .root_body_id = root_result.value(),
    });
  ASSERT_TRUE(articulation.has_value());
  EXPECT_TRUE(IsValid(articulation.value()));
  EXPECT_TRUE(articulations.DestroyArticulation(world_id, articulation.value())
      .has_value());

  EXPECT_TRUE(bodies.DestroyBody(world_id, root_result.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
