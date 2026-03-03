//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/Test/TestBlobBuilders.h>
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

  const std::array<vehicle::VehicleWheelDesc, 2> wheel_descs {
    vehicle::VehicleWheelDesc {
      .body_id = wheel_a.value(),
      .axle_index = 0U,
      .side = vehicle::VehicleWheelSide::kLeft,
    },
    vehicle::VehicleWheelDesc {
      .body_id = wheel_b.value(),
      .axle_index = 0U,
      .side = vehicle::VehicleWheelSide::kRight,
    },
  };
  const auto vehicle_settings_blob
    = MakeVehicleConstraintSettingsBlob(wheel_descs.size());
  const auto vehicle = vehicles.CreateVehicle(world_id,
    vehicle::VehicleDesc {
      .chassis_body_id = chassis.value(),
      .wheels = wheel_descs,
      .constraint_settings_blob = vehicle_settings_blob,
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
          .hand_brake = 0.0F,
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

NOLINT_TEST_F(JoltVehicleDomainTest, ProgrammaticControlInputMovesChassis)
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

  body::BodyDesc ground_desc {};
  ground_desc.type = body::BodyType::kStatic;
  ground_desc.shape = BoxShape { .extents = Vec3 { 50.0F, 50.0F, 1.0F } };
  ground_desc.initial_position = Vec3 { 0.0F, 0.0F, -0.5F };
  const auto ground = bodies.CreateBody(world_id, ground_desc);
  ASSERT_TRUE(ground.has_value());

  body::BodyDesc chassis_desc {};
  chassis_desc.type = body::BodyType::kDynamic;
  chassis_desc.initial_position = Vec3 { 0.0F, 0.0F, 1.0F };
  const auto chassis = bodies.CreateBody(world_id, chassis_desc);
  ASSERT_TRUE(chassis.has_value());

  body::BodyDesc wheel_desc {};
  wheel_desc.type = body::BodyType::kKinematic;
  wheel_desc.initial_position = Vec3 { -0.8F, 0.8F, 0.5F };
  const auto wheel_a = bodies.CreateBody(world_id, wheel_desc);
  wheel_desc.initial_position = Vec3 { 0.8F, 0.8F, 0.5F };
  const auto wheel_b = bodies.CreateBody(world_id, wheel_desc);
  ASSERT_TRUE(wheel_a.has_value());
  ASSERT_TRUE(wheel_b.has_value());

  const std::array<vehicle::VehicleWheelDesc, 2> wheel_descs {
    vehicle::VehicleWheelDesc {
      .body_id = wheel_a.value(),
      .axle_index = 0U,
      .side = vehicle::VehicleWheelSide::kLeft,
    },
    vehicle::VehicleWheelDesc {
      .body_id = wheel_b.value(),
      .axle_index = 0U,
      .side = vehicle::VehicleWheelSide::kRight,
    },
  };
  const auto vehicle_settings_blob
    = MakeVehicleConstraintSettingsBlob(wheel_descs.size());
  ASSERT_FALSE(vehicle_settings_blob.empty());

  const auto vehicle = vehicles.CreateVehicle(world_id,
    vehicle::VehicleDesc {
      .chassis_body_id = chassis.value(),
      .wheels = wheel_descs,
      .constraint_settings_blob = vehicle_settings_blob,
    });
  ASSERT_TRUE(vehicle.has_value());
  const auto vehicle_id = vehicle.value();

  for (int i = 0; i < 15; ++i) {
    ASSERT_TRUE(
      worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  }
  const auto before_position
    = bodies.GetBodyPosition(world_id, chassis.value());
  ASSERT_TRUE(before_position.has_value());

  ASSERT_TRUE(vehicles
      .SetControlInput(world_id, vehicle_id,
        vehicle::VehicleControlInput {
          .forward = 1.0F,
          .brake = 0.0F,
          .steering = 0.0F,
          .hand_brake = 0.0F,
        })
      .has_value());

  for (int i = 0; i < 120; ++i) {
    ASSERT_TRUE(
      worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  }

  const auto after_position = bodies.GetBodyPosition(world_id, chassis.value());
  ASSERT_TRUE(after_position.has_value());
  const auto after_velocity
    = bodies.GetLinearVelocity(world_id, chassis.value());
  ASSERT_TRUE(after_velocity.has_value());

  const auto delta = after_position.value() - before_position.value();
  const auto planar_displacement
    = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));

  EXPECT_GT(planar_displacement, 0.05F);
  EXPECT_GT(
    std::fabs(after_velocity.value().x) + std::fabs(after_velocity.value().y),
    0.05F);

  EXPECT_TRUE(vehicles.DestroyVehicle(world_id, vehicle_id).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_b.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_a.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, ground.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
