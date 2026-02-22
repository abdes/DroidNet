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

  class JoltSoftBodyContractTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltSoftBodyContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* soft_bodies = System().SoftBodies();
  ASSERT_NE(soft_bodies, nullptr);

  EXPECT_TRUE(
    soft_bodies->CreateSoftBody(kInvalidWorldId, softbody::SoftBodyDesc {})
      .has_error());
  EXPECT_TRUE(soft_bodies->DestroySoftBody(kInvalidWorldId, kInvalidAggregateId)
      .has_error());
  EXPECT_TRUE(soft_bodies
      ->SetMaterialParams(kInvalidWorldId, kInvalidAggregateId,
        softbody::SoftBodyMaterialParams {})
      .has_error());
  EXPECT_TRUE(
    soft_bodies->GetState(kInvalidWorldId, kInvalidAggregateId).has_error());
  EXPECT_TRUE(soft_bodies->GetAuthority(kInvalidWorldId, kInvalidAggregateId)
      .has_error());
  EXPECT_TRUE(soft_bodies->FlushStructuralChanges(kInvalidWorldId).has_error());
}

NOLINT_TEST_F(JoltSoftBodyContractTest, UnknownSoftBodyReturnsInvalidArgument)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* soft_bodies = System().SoftBodies();
  ASSERT_NE(soft_bodies, nullptr);
  auto& worlds = System().Worlds();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto unknown_id = AggregateId { 9999U };
  const auto destroy = soft_bodies->DestroySoftBody(world_id, unknown_id);
  ASSERT_TRUE(destroy.has_error());
  EXPECT_EQ(destroy.error(), PhysicsError::kInvalidArgument);

  const auto state = soft_bodies->GetState(world_id, unknown_id);
  ASSERT_TRUE(state.has_error());
  EXPECT_EQ(state.error(), PhysicsError::kInvalidArgument);

  const auto authority = soft_bodies->GetAuthority(world_id, unknown_id);
  ASSERT_TRUE(authority.has_error());
  EXPECT_EQ(authority.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltSoftBodyContractTest, InvalidDescriptorAndMaterialParamsReturnError)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* soft_bodies = System().SoftBodies();
  ASSERT_NE(soft_bodies, nullptr);
  auto& worlds = System().Worlds();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto invalid_desc = soft_bodies->CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 0U,
    });
  ASSERT_TRUE(invalid_desc.has_error());
  EXPECT_EQ(invalid_desc.error(), PhysicsError::kInvalidArgument);

  const auto valid_soft_body = soft_bodies->CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 3U,
    });
  ASSERT_TRUE(valid_soft_body.has_value());

  const auto invalid_params
    = soft_bodies->SetMaterialParams(world_id, valid_soft_body.value(),
      softbody::SoftBodyMaterialParams {
        .stiffness = -0.1F,
        .damping = 0.2F,
      });
  ASSERT_TRUE(invalid_params.has_error());
  EXPECT_EQ(invalid_params.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(soft_bodies->DestroySoftBody(world_id, valid_soft_body.value())
      .has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltSoftBodyContractTest, RuntimeTopologyMaterialChangesAreAppliedOnFlush)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto* soft_bodies = System().SoftBodies();
  ASSERT_NE(soft_bodies, nullptr);
  auto& worlds = System().Worlds();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto created = soft_bodies->CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .anchor_body_id = kInvalidBodyId,
      .cluster_count = 4U,
      .material_params = softbody::SoftBodyMaterialParams {
        .stiffness = 0.2F,
        .damping = 0.05F,
        .edge_compliance = 0.0F,
        .shear_compliance = 0.0F,
        .bend_compliance = 1.0F,
        .tether_mode = softbody::SoftBodyTetherMode::kNone,
        .tether_max_distance_multiplier = 1.0F,
      },
    });
  ASSERT_TRUE(created.has_value());
  EXPECT_TRUE(soft_bodies->FlushStructuralChanges(world_id).has_value());

  const auto set_runtime_topology
    = soft_bodies->SetMaterialParams(world_id, created.value(),
      softbody::SoftBodyMaterialParams {
        .stiffness = 0.3F,
        .damping = 0.1F,
        .edge_compliance = 0.2F,
        .shear_compliance = 0.0F,
        .bend_compliance = 1.0F,
        .tether_mode = softbody::SoftBodyTetherMode::kEuclidean,
        .tether_max_distance_multiplier = 1.1F,
      });
  ASSERT_TRUE(set_runtime_topology.has_value());

  const auto flush_topology = soft_bodies->FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_topology.has_value());
  EXPECT_EQ(flush_topology.value(), 1U);
  const auto flush_none = soft_bodies->FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_none.has_value());
  EXPECT_EQ(flush_none.value(), 0U);

  EXPECT_TRUE(
    soft_bodies->DestroySoftBody(world_id, created.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
