//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#if defined(OXGN_PHYS_BACKEND_JOLT)
#  include <Jolt/Jolt.h> // Must always be first (keep separate)
#endif

#include <limits>
#include <mutex>
#include <sstream>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#if defined(OXGN_PHYS_BACKEND_JOLT)
#  include <Jolt/Core/Memory.h>
#  include <Jolt/Core/StreamWrapper.h>
#  include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#endif

#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/Test/TestBlobBuilders.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltSoftBodyCollisionContractTest : public JoltTestFixture { };

  [[nodiscard]] auto MakeZeroVolumeSurfaceSettingsBlob() -> std::vector<uint8_t>
  {
#if defined(OXGN_PHYS_BACKEND_JOLT)
    static std::once_flag once {};
    std::call_once(once, [] { JPH::RegisterDefaultAllocator(); });

    auto shared = JPH::Ref<JPH::SoftBodySharedSettings> {
      new JPH::SoftBodySharedSettings {}
    };
    shared->mVertices.emplace_back(JPH::Float3 { -0.5F, -0.5F, 0.0F },
      JPH::Float3 { 0.0F, 0.0F, 0.0F }, 1.0F);
    shared->mVertices.emplace_back(JPH::Float3 { 0.5F, -0.5F, 0.0F },
      JPH::Float3 { 0.0F, 0.0F, 0.0F }, 1.0F);
    shared->mVertices.emplace_back(
      JPH::Float3 { 0.5F, 0.5F, 0.0F }, JPH::Float3 { 0.0F, 0.0F, 0.0F }, 1.0F);
    shared->mVertices.emplace_back(JPH::Float3 { -0.5F, 0.5F, 0.0F },
      JPH::Float3 { 0.0F, 0.0F, 0.0F }, 1.0F);
    shared->mFaces.emplace_back(0U, 1U, 2U);
    shared->mFaces.emplace_back(0U, 2U, 3U);

    const auto attrs = JPH::SoftBodySharedSettings::VertexAttributes(1.0e-4F,
      1.0e-4F, 1.0e-3F, JPH::SoftBodySharedSettings::ELRAType::None, 1.0F);
    shared->CreateConstraints(&attrs, 1U);
    shared->CalculateEdgeLengths();
    shared->CalculateBendConstraintConstants();
    shared->CalculateVolumeConstraintVolumes();
    shared->Optimize();

    auto stream = std::ostringstream(std::ios::out | std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    shared->SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return {};
    }
    const auto serialized = stream.str();
    return std::vector<uint8_t>(
      reinterpret_cast<const uint8_t*>(serialized.data()),
      reinterpret_cast<const uint8_t*>(serialized.data()) + serialized.size());
#else
    return std::vector<uint8_t> { 0x1U };
#endif
  }

  [[nodiscard]] auto MakeZeroVolumeComplianceCubeSettingsBlob()
    -> std::vector<uint8_t>
  {
#if defined(OXGN_PHYS_BACKEND_JOLT)
    static std::once_flag once {};
    std::call_once(once, [] { JPH::RegisterDefaultAllocator(); });

    auto shared = JPH::SoftBodySharedSettings::sCreateCube(3U, 0.1F);
    if (shared == nullptr) {
      return {};
    }
    const auto attrs = JPH::SoftBodySharedSettings::VertexAttributes(0.0F, 0.0F,
      std::numeric_limits<float>::max(),
      JPH::SoftBodySharedSettings::ELRAType::None, 1.0F);
    shared->CreateConstraints(&attrs, 1U);
    for (auto& volume : shared->mVolumeConstraints) {
      volume.mCompliance = 0.0F;
    }
    shared->CalculateVolumeConstraintVolumes();
    shared->Optimize();

    auto stream = std::ostringstream(std::ios::out | std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    shared->SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return {};
    }
    const auto serialized = stream.str();
    return std::vector<uint8_t>(
      reinterpret_cast<const uint8_t*>(serialized.data()),
      reinterpret_cast<const uint8_t*>(serialized.data()) + serialized.size());
#else
    return std::vector<uint8_t> { 0x1U };
#endif
  }

} // namespace

NOLINT_TEST_F(JoltSoftBodyCollisionContractTest,
  CreateSoftBodyRejectsNonBitAddressableLayer)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& soft_bodies = System().SoftBodies();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto created = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .cluster_count = 4U,
      .settings_blob = MakeSoftBodySettingsBlob(4U),
      .collision_layer = CollisionLayer { 32U },
      .collision_mask = kCollisionMaskAll,
    });
  ASSERT_TRUE(created.has_error());
  EXPECT_EQ(created.error(), PhysicsError::kInvalidCollisionMask);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltSoftBodyCollisionContractTest,
  CreateSoftBodyRejectsVolumeComplianceBelowStabilityFloor)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  const auto blob = MakeZeroVolumeComplianceCubeSettingsBlob();
  ASSERT_FALSE(blob.empty());

  auto& worlds = System().Worlds();
  auto& soft_bodies = System().SoftBodies();
  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  softbody::SoftBodyDesc desc {};
  desc.cluster_count = 4U;
  desc.settings_blob = blob;

  const auto created = soft_bodies.CreateSoftBody(world_id, desc);
  ASSERT_TRUE(created.has_error());
  EXPECT_EQ(created.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltSoftBodyCollisionContractTest,
  CreateSoftBodyRejectsPressureWithZeroRestVolumeSurface)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  const auto blob = MakeZeroVolumeSurfaceSettingsBlob();
  ASSERT_FALSE(blob.empty());

  auto& worlds = System().Worlds();
  auto& soft_bodies = System().SoftBodies();
  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  softbody::SoftBodyDesc desc {};
  desc.cluster_count = 4U;
  desc.settings_blob = blob;
  desc.material_params.edge_compliance = 1.0e-4F;
  desc.material_params.shear_compliance = 1.0e-4F;
  desc.material_params.bend_compliance = 1.0e-3F;
  desc.material_params.tether_mode = softbody::SoftBodyTetherMode::kNone;
  desc.material_params.tether_max_distance_multiplier = 1.0F;
  desc.material_params.pressure_coefficient = 1.0F;

  const auto created = soft_bodies.CreateSoftBody(world_id, desc);
  ASSERT_TRUE(created.has_error());
  EXPECT_EQ(created.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
