//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstddef>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Shape/ShapeDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltShapeDomainTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltShapeDomainTest, DestroyAttachedShapeReturnsAlreadyExists)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& shapes = System().Shapes();
  auto& bodies = System().Bodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto shape_result = shapes.CreateShape(shape::ShapeDesc {});
  ASSERT_TRUE(shape_result.has_value());
  const auto shape_id = shape_result.value();

  const auto body_result = bodies.CreateBody(world_id, body::BodyDesc {});
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  const auto add_result = bodies.AddBodyShape(world_id, body_id, shape_id,
    Vec3 { 0.0F, 0.0F, 0.0F }, Quat { 1.0F, 0.0F, 0.0F, 0.0F });
  ASSERT_TRUE(add_result.has_value());
  const auto shape_instance_id = add_result.value();

  const auto destroy_attached = shapes.DestroyShape(shape_id);
  ASSERT_TRUE(destroy_attached.has_error());
  EXPECT_EQ(destroy_attached.error(), PhysicsError::kAlreadyExists);

  EXPECT_TRUE(
    bodies.RemoveBodyShape(world_id, body_id, shape_instance_id).has_value());
  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltShapeDomainTest, RepeatedAttachDetachKeepsShapeLifetimeCorrect)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& shapes = System().Shapes();
  auto& bodies = System().Bodies();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto shape_result = shapes.CreateShape(shape::ShapeDesc {});
  ASSERT_TRUE(shape_result.has_value());
  const auto shape_id = shape_result.value();

  const auto body_result = bodies.CreateBody(world_id, body::BodyDesc {});
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  constexpr size_t kIterations = 32;
  for (size_t i = 0; i < kIterations; ++i) {
    const auto add_result = bodies.AddBodyShape(world_id, body_id, shape_id,
      Vec3 { 0.0F, 0.0F, 0.0F }, Quat { 1.0F, 0.0F, 0.0F, 0.0F });
    ASSERT_TRUE(add_result.has_value());

    const auto destroy_attached = shapes.DestroyShape(shape_id);
    ASSERT_TRUE(destroy_attached.has_error());
    EXPECT_EQ(destroy_attached.error(), PhysicsError::kAlreadyExists);

    EXPECT_TRUE(bodies.RemoveBodyShape(world_id, body_id, add_result.value())
        .has_value());
  }

  EXPECT_TRUE(shapes.DestroyShape(shape_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
