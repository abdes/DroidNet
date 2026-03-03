//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/Test/TestBlobBuilders.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltSoftBodyDomainTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(
  JoltSoftBodyDomainTest, LifecycleMaterialStateAuthorityAndFlushContract)
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

  const auto soft_body_settings_blob = MakeSoftBodySettingsBlob(6U);
  const auto soft_body = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 6U,
      .material_params = softbody::SoftBodyMaterialParams {
        .stiffness = 0.0F,
        .damping = 0.0F,
        .edge_compliance = 0.0F,
        .shear_compliance = 0.0F,
        .bend_compliance = 1.0F,
        .tether_mode = softbody::SoftBodyTetherMode::kNone,
        .tether_max_distance_multiplier = 1.0F,
      },
      .settings_blob = soft_body_settings_blob,
    });
  ASSERT_TRUE(soft_body.has_value());
  const auto soft_body_id = soft_body.value();

  const auto authority = soft_bodies.GetAuthority(world_id, soft_body_id);
  ASSERT_TRUE(authority.has_value());
  EXPECT_EQ(authority.value(), aggregate::AggregateAuthority::kSimulation);

  const auto state_before = soft_bodies.GetState(world_id, soft_body_id);
  ASSERT_TRUE(state_before.has_value());
  EXPECT_TRUE(std::isfinite(state_before.value().center_of_mass.x));
  EXPECT_TRUE(std::isfinite(state_before.value().center_of_mass.y));
  EXPECT_TRUE(std::isfinite(state_before.value().center_of_mass.z));
  EXPECT_NEAR(state_before.value().center_of_mass.x, 0.0F, 0.75F);
  EXPECT_NEAR(state_before.value().center_of_mass.y, 0.0F, 0.75F);
  EXPECT_NEAR(state_before.value().center_of_mass.z, 0.0F, 0.75F);

  EXPECT_TRUE(soft_bodies
      .SetMaterialParams(world_id, soft_body_id,
        softbody::SoftBodyMaterialParams {
          .stiffness = 0.6F,
          .damping = 0.3F,
          .edge_compliance = 0.0F,
          .shear_compliance = 0.0F,
          .bend_compliance = 1.0F,
          .tether_mode = softbody::SoftBodyTetherMode::kNone,
          .tether_max_distance_multiplier = 1.0F,
        })
      .has_value());

  const auto flush_create = soft_bodies.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_create.has_value());
  EXPECT_EQ(flush_create.value(), 2U);
  const auto flush_none = soft_bodies.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_none.has_value());
  EXPECT_EQ(flush_none.value(), 0U);

  EXPECT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  const auto state_after = soft_bodies.GetState(world_id, soft_body_id);
  ASSERT_TRUE(state_after.has_value());
  EXPECT_TRUE(std::isfinite(state_after.value().center_of_mass.x));
  EXPECT_TRUE(std::isfinite(state_after.value().center_of_mass.y));
  EXPECT_TRUE(std::isfinite(state_after.value().center_of_mass.z));

  EXPECT_TRUE(soft_bodies.DestroySoftBody(world_id, soft_body_id).has_value());
  const auto flush_destroy = soft_bodies.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_destroy.has_value());
  EXPECT_EQ(flush_destroy.value(), 1U);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltSoftBodyDomainTest, AnchorBodyCreateReturnsNotImplemented)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& soft_bodies = System().SoftBodies();
  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  body::BodyDesc anchor_desc {};
  anchor_desc.type = body::BodyType::kKinematic;
  anchor_desc.initial_position = Vec3 { 0.0F, 0.0F, 1.0F };
  const auto anchor = bodies.CreateBody(world_id, anchor_desc);
  ASSERT_TRUE(anchor.has_value());

  const auto soft_body_settings_blob = MakeSoftBodySettingsBlob(4U);
  const auto soft_body = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = anchor.value(),
      .cluster_count = 4U,
      .settings_blob = soft_body_settings_blob,
    });
  ASSERT_TRUE(soft_body.has_error());
  EXPECT_EQ(soft_body.error(), PhysicsError::kNotImplemented);
  EXPECT_TRUE(bodies.DestroyBody(world_id, anchor.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltSoftBodyDomainTest, TopologyMaterialChangesAreCoalescedAndAppliedOnFlush)
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

  const auto soft_body_settings_blob = MakeSoftBodySettingsBlob(5U);
  const auto soft_body = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 5U,
      .material_params = softbody::SoftBodyMaterialParams {
        .stiffness = 0.1F,
        .damping = 0.03F,
        .edge_compliance = 0.0F,
        .shear_compliance = 0.0F,
        .bend_compliance = 1.0F,
        .tether_mode = softbody::SoftBodyTetherMode::kNone,
        .tether_max_distance_multiplier = 1.0F,
      },
      .settings_blob = soft_body_settings_blob,
    });
  ASSERT_TRUE(soft_body.has_value());
  const auto soft_body_id = soft_body.value();

  EXPECT_TRUE(soft_bodies.FlushStructuralChanges(world_id).has_value());

  EXPECT_TRUE(soft_bodies
      .SetMaterialParams(world_id, soft_body_id,
        softbody::SoftBodyMaterialParams {
          .stiffness = 0.2F,
          .damping = 0.05F,
          .edge_compliance = 0.2F,
          .shear_compliance = 0.0F,
          .bend_compliance = 1.0F,
          .tether_mode = softbody::SoftBodyTetherMode::kEuclidean,
          .tether_max_distance_multiplier = 1.1F,
        })
      .has_value());
  EXPECT_TRUE(soft_bodies
      .SetMaterialParams(world_id, soft_body_id,
        softbody::SoftBodyMaterialParams {
          .stiffness = 0.35F,
          .damping = 0.1F,
          .edge_compliance = 0.3F,
          .shear_compliance = 0.1F,
          .bend_compliance = 0.9F,
          .tether_mode = softbody::SoftBodyTetherMode::kGeodesic,
          .tether_max_distance_multiplier = 1.2F,
        })
      .has_value());

  const auto flush = soft_bodies.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush.has_value());
  EXPECT_EQ(flush.value(), 1U);

  EXPECT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  const auto state = soft_bodies.GetState(world_id, soft_body_id);
  ASSERT_TRUE(state.has_value());
  EXPECT_TRUE(std::isfinite(state.value().center_of_mass.x));
  EXPECT_TRUE(std::isfinite(state.value().center_of_mass.y));
  EXPECT_TRUE(std::isfinite(state.value().center_of_mass.z));

  EXPECT_TRUE(soft_bodies.DestroySoftBody(world_id, soft_body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltSoftBodyDomainTest, SetMaterialParamsIsSafeAcrossRepeatedStepBoundaries)
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

  const auto soft_body_settings_blob = MakeSoftBodySettingsBlob(5U);
  const auto soft_body = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 5U,
      .material_params = softbody::SoftBodyMaterialParams {
        .stiffness = 0.1F,
        .damping = 0.01F,
        .edge_compliance = 0.0F,
        .shear_compliance = 0.0F,
        .bend_compliance = 1.0F,
        .tether_mode = softbody::SoftBodyTetherMode::kNone,
        .tether_max_distance_multiplier = 1.0F,
      },
      .settings_blob = soft_body_settings_blob,
    });
  ASSERT_TRUE(soft_body.has_value());
  const auto soft_body_id = soft_body.value();

  for (int i = 0; i < 32; ++i) {
    const auto stiffness = 0.1F + static_cast<float>(i) * 0.02F;
    const auto damping = 0.01F + static_cast<float>(i % 5) * 0.03F;
    EXPECT_TRUE(soft_bodies
        .SetMaterialParams(world_id, soft_body_id,
          softbody::SoftBodyMaterialParams {
            .stiffness = stiffness,
            .damping = damping,
            .edge_compliance = 0.0F,
            .shear_compliance = 0.0F,
            .bend_compliance = 1.0F,
            .tether_mode = softbody::SoftBodyTetherMode::kNone,
            .tether_max_distance_multiplier = 1.0F,
          })
        .has_value());

    EXPECT_TRUE(
      worlds.Step(world_id, 1.0F / 120.0F, 1, 1.0F / 120.0F).has_value());

    const auto state = soft_bodies.GetState(world_id, soft_body_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(std::isfinite(state.value().center_of_mass.x));
    EXPECT_TRUE(std::isfinite(state.value().center_of_mass.y));
    EXPECT_TRUE(std::isfinite(state.value().center_of_mass.z));
  }

  EXPECT_TRUE(soft_bodies.DestroySoftBody(world_id, soft_body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
