//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Area/AreaDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltAreaContractTest : public JoltTestFixture {
  protected:
    auto RequireBackend() -> void
    {
      AssertBackendAvailabilityContract();
      if (!HasBackend()) {
        GTEST_SKIP() << "No physics backend available.";
      }
    }
  };

} // namespace

NOLINT_TEST_F(JoltAreaContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  RequireBackend();

  auto& areas = System().Areas();
  const auto create_result
    = areas.CreateArea(kInvalidWorldId, area::AreaDesc {});
  ASSERT_TRUE(create_result.has_error());
  EXPECT_EQ(create_result.error(), PhysicsError::kWorldNotFound);
}

NOLINT_TEST_F(JoltAreaContractTest, InvalidAreaCallsReturnError)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& areas = System().Areas();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  EXPECT_TRUE(areas.GetAreaPosition(world_id, kInvalidAreaId).has_error());
  EXPECT_TRUE(areas.GetAreaRotation(world_id, kInvalidAreaId).has_error());
  EXPECT_TRUE(areas
      .SetAreaPose(world_id, kInvalidAreaId, Vec3 { 1.0F, 2.0F, 3.0F },
        Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_error());
  EXPECT_TRUE(
    areas.RemoveAreaShape(world_id, kInvalidAreaId, kInvalidShapeInstanceId)
      .has_error());
  EXPECT_TRUE(areas.DestroyArea(world_id, kInvalidAreaId).has_error());

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
