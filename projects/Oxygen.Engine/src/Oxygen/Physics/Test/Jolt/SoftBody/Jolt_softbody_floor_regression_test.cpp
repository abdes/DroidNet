//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/Test/TestBlobBuilders.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltSoftBodyFloorRegressionTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltSoftBodyFloorRegressionTest,
  SceneLikeUvSphereRemainsFiniteAndDoesNotSinkBelowFloor)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    GTEST_SKIP() << "No physics backend available.";
  }

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& soft_bodies = System().SoftBodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc floor_desc {};
  floor_desc.type = body::BodyType::kStatic;
  floor_desc.shape = BoxShape { .extents = Vec3 { 10.0F, 0.5F, 10.0F } };
  floor_desc.initial_position = Vec3 { 0.0F, -0.5F, 0.0F };
  const auto floor_body = bodies.CreateBody(world_id, floor_desc);
  ASSERT_TRUE(floor_body.has_value());

  const auto settings_blob
    = MakeSoftBodyUvSphereSettingsBlob(10U, 20U, 1.0e-6F, 1.0e-6F, 5.0e-6F);
  ASSERT_FALSE(settings_blob.empty());

  softbody::SoftBodyDesc desc {};
  desc.cluster_count = 16U;
  desc.initial_position = Vec3 { 0.0F, 4.0F, 0.0F };
  desc.settings_blob = settings_blob;
  desc.material_params.damping = 0.2F;
  desc.material_params.edge_compliance = 1.0e-6F;
  desc.material_params.shear_compliance = 1.0e-6F;
  desc.material_params.bend_compliance = 5.0e-6F;
  desc.material_params.volume_compliance = 1.0e-6F;
  desc.material_params.pressure_coefficient = 4.0F;
  desc.material_params.tether_mode = softbody::SoftBodyTetherMode::kGeodesic;
  desc.material_params.tether_max_distance_multiplier = 1.05F;
  desc.solver_iteration_count = 16U;
  desc.restitution = 0.2F;
  desc.friction = 0.6F;
  desc.vertex_radius = 0.04F;

  const auto soft_body = soft_bodies.CreateSoftBody(world_id, desc);
  ASSERT_TRUE(soft_body.has_value());
  ASSERT_TRUE(soft_bodies.FlushStructuralChanges(world_id).has_value());

  auto min_center_of_mass_y = std::numeric_limits<float>::infinity();
  for (int i = 0; i < 600; ++i) {
    ASSERT_TRUE(
      worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
    const auto state = soft_bodies.GetState(world_id, soft_body.value());
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::isfinite(state.value().center_of_mass.x));
    EXPECT_TRUE(std::isfinite(state.value().center_of_mass.y));
    EXPECT_TRUE(std::isfinite(state.value().center_of_mass.z));
    min_center_of_mass_y
      = (std::min)(min_center_of_mass_y, state.value().center_of_mass.y);
  }

  // The floor top is y=0; allow some penetration tolerance but reject
  // sustained deep sink-through.
  EXPECT_GT(min_center_of_mass_y, -0.25F);

  EXPECT_TRUE(
    soft_bodies.DestroySoftBody(world_id, soft_body.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, floor_body.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
