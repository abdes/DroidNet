//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Area/AreaDesc.h>
#include <Oxygen/Physics/Shape/ShapeDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltAreaDomainTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltAreaDomainTest, AreaPoseRoundTrip)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& areas = System().Areas();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  area::AreaDesc desc {};
  desc.initial_position = Vec3 { 1.0F, 2.0F, 3.0F };
  const auto area_result = areas.CreateArea(world_id, desc);
  ASSERT_TRUE(area_result.has_value());
  const auto area_id = area_result.value();

  ASSERT_TRUE(areas
      .SetAreaPose(world_id, area_id, Vec3 { 4.0F, 5.0F, 6.0F },
        Quat { 1.0F, 0.0F, 0.0F, 0.0F })
      .has_value());
  const auto position = areas.GetAreaPosition(world_id, area_id);
  ASSERT_TRUE(position.has_value());
  EXPECT_FLOAT_EQ(position.value().x, 4.0F);
  EXPECT_FLOAT_EQ(position.value().y, 5.0F);
  EXPECT_FLOAT_EQ(position.value().z, 6.0F);

  EXPECT_TRUE(areas.DestroyArea(world_id, area_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltAreaDomainTest, AttachedShapeCannotBeDestroyedUntilDetached)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& areas = System().Areas();
  auto& shapes = System().Shapes();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto area_result = areas.CreateArea(world_id, area::AreaDesc {});
  ASSERT_TRUE(area_result.has_value());
  const auto area_id = area_result.value();

  const auto shape_result = shapes.CreateShape(shape::ShapeDesc {});
  ASSERT_TRUE(shape_result.has_value());
  const auto shape_id = shape_result.value();

  const auto instance_result = areas.AddAreaShape(world_id, area_id, shape_id,
    Vec3 { 0.0F, 0.0F, 0.0F }, Quat { 1.0F, 0.0F, 0.0F, 0.0F });
  ASSERT_TRUE(instance_result.has_value());
  const auto instance_id = instance_result.value();

  const auto destroy_attached = shapes.DestroyShape(shape_id);
  ASSERT_TRUE(destroy_attached.has_error());
  EXPECT_EQ(destroy_attached.error(), PhysicsError::kAlreadyExists);

  ASSERT_TRUE(
    areas.RemoveAreaShape(world_id, area_id, instance_id).has_value());
  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_value());
  EXPECT_TRUE(areas.DestroyArea(world_id, area_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
