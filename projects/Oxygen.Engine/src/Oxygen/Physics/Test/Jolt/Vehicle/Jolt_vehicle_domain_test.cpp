//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltVehicleDomainTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(
  JoltVehicleDomainTest, LifecycleControlStateAuthorityAndFlushContract)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& vehicles = System().Vehicles();
  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  body_desc.initial_position = Vec3 { 0.0F, 1.0F, 0.0F };
  const auto chassis = bodies.CreateBody(world_id, body_desc);
  body_desc.type = body::BodyType::kKinematic;
  body_desc.initial_position = Vec3 { -0.8F, 0.5F, 1.0F };
  const auto wheel_a = bodies.CreateBody(world_id, body_desc);
  body_desc.initial_position = Vec3 { 0.8F, 0.5F, 1.0F };
  const auto wheel_b = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(chassis.has_value());
  ASSERT_TRUE(wheel_a.has_value());
  ASSERT_TRUE(wheel_b.has_value());

  const std::array<BodyId, 2> wheel_ids {
    wheel_a.value(),
    wheel_b.value(),
  };
  const auto vehicle = vehicles.CreateVehicle(world_id,
    vehicle::VehicleDesc {
      .chassis_body_id = chassis.value(),
      .wheel_body_ids = wheel_ids,
    });
  ASSERT_TRUE(vehicle.has_value());
  const auto vehicle_id = vehicle.value();

  const auto authority = vehicles.GetAuthority(world_id, vehicle_id);
  ASSERT_TRUE(authority.has_value());
  EXPECT_EQ(authority.value(), aggregate::AggregateAuthority::kCommand);

  EXPECT_TRUE(vehicles
      .SetControlInput(world_id, vehicle_id,
        vehicle::VehicleControlInput {
          .forward = 0.65F,
          .brake = 0.0F,
          .steering = 0.2F,
          .handbrake = 0.0F,
        })
      .has_value());

  EXPECT_TRUE(bodies
      .SetLinearVelocity(world_id, chassis.value(), Vec3 { 0.0F, -2.0F, 0.0F })
      .has_value());
  const auto state = vehicles.GetState(world_id, vehicle_id);
  ASSERT_TRUE(state.has_value());
  EXPECT_GT(state.value().forward_speed_mps, 1.0F);
  EXPECT_FALSE(state.value().grounded);
  EXPECT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());

  const auto flush_create = vehicles.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_create.has_value());
  EXPECT_EQ(flush_create.value(), 1U);
  const auto flush_none = vehicles.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_none.has_value());
  EXPECT_EQ(flush_none.value(), 0U);

  EXPECT_TRUE(vehicles.DestroyVehicle(world_id, vehicle_id).has_value());
  const auto flush_destroy = vehicles.FlushStructuralChanges(world_id);
  ASSERT_TRUE(flush_destroy.has_value());
  EXPECT_EQ(flush_destroy.value(), 1U);

  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_b.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_a.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
