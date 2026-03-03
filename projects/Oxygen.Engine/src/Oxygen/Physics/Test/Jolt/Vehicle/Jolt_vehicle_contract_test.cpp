//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/SoftBody/SoftBodyDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/Test/TestBlobBuilders.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltVehicleContractTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltVehicleContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& vehicles = System().Vehicles();

  EXPECT_TRUE(vehicles.CreateVehicle(kInvalidWorldId, vehicle::VehicleDesc {})
      .has_error());
  EXPECT_TRUE(
    vehicles.DestroyVehicle(kInvalidWorldId, kInvalidAggregateId).has_error());
  EXPECT_TRUE(vehicles
      .SetControlInput(
        kInvalidWorldId, kInvalidAggregateId, vehicle::VehicleControlInput {})
      .has_error());
  EXPECT_TRUE(
    vehicles.GetState(kInvalidWorldId, kInvalidAggregateId).has_error());
  EXPECT_TRUE(
    vehicles.GetAuthority(kInvalidWorldId, kInvalidAggregateId).has_error());
  EXPECT_TRUE(vehicles.FlushStructuralChanges(kInvalidWorldId).has_error());
}

NOLINT_TEST_F(JoltVehicleContractTest, UnknownVehicleReturnsInvalidArgument)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& vehicles = System().Vehicles();
  auto& worlds = System().Worlds();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto get_state = vehicles.GetState(world_id, AggregateId { 9999U });
  ASSERT_TRUE(get_state.has_error());
  EXPECT_EQ(get_state.error(), PhysicsError::kInvalidArgument);

  const auto destroy = vehicles.DestroyVehicle(world_id, AggregateId { 9999U });
  ASSERT_TRUE(destroy.has_error());
  EXPECT_EQ(destroy.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltVehicleContractTest, SetControlInputValidatesNormalizedRanges)
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
  const auto chassis = bodies.CreateBody(world_id, body_desc);
  const auto wheel = bodies.CreateBody(world_id, body_desc);
  const auto wheel2 = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(chassis.has_value());
  ASSERT_TRUE(wheel.has_value());
  ASSERT_TRUE(wheel2.has_value());

  const std::array<vehicle::VehicleWheelDesc, 2> wheel_descs {
    vehicle::VehicleWheelDesc {
      .body_id = wheel.value(),
      .axle_index = 0U,
      .side = vehicle::VehicleWheelSide::kLeft,
    },
    vehicle::VehicleWheelDesc {
      .body_id = wheel2.value(),
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

  const auto invalid = vehicles.SetControlInput(world_id, vehicle.value(),
    vehicle::VehicleControlInput {
      .forward = 2.0F,
      .brake = 0.0F,
      .steering = 0.0F,
      .hand_brake = 0.0F,
    });
  ASSERT_TRUE(invalid.has_error());
  EXPECT_EQ(invalid.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(vehicles.DestroyVehicle(world_id, vehicle.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel2.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltVehicleContractTest, CreateVehicleRejectsSingleWheelTopology)
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
  const auto chassis = bodies.CreateBody(world_id, body_desc);
  const auto wheel = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(chassis.has_value());
  ASSERT_TRUE(wheel.has_value());

  const std::array<vehicle::VehicleWheelDesc, 1> wheel_descs {
    vehicle::VehicleWheelDesc {
      .body_id = wheel.value(),
      .axle_index = 0U,
      .side = vehicle::VehicleWheelSide::kLeft,
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
  ASSERT_TRUE(vehicle.has_error());
  EXPECT_EQ(vehicle.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltVehicleContractTest, CreateVehicleAcceptsOddWheelTopology)
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
  const auto chassis = bodies.CreateBody(world_id, body_desc);
  const auto wheel_a = bodies.CreateBody(world_id, body_desc);
  const auto wheel_b = bodies.CreateBody(world_id, body_desc);
  const auto wheel_c = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(chassis.has_value());
  ASSERT_TRUE(wheel_a.has_value());
  ASSERT_TRUE(wheel_b.has_value());
  ASSERT_TRUE(wheel_c.has_value());

  const std::array<vehicle::VehicleWheelDesc, 3> wheel_descs {
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
    vehicle::VehicleWheelDesc {
      .body_id = wheel_c.value(),
      .axle_index = 1U,
      .side = vehicle::VehicleWheelSide::kLeft,
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

  EXPECT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());

  EXPECT_TRUE(vehicles.DestroyVehicle(world_id, vehicle.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_c.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_b.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_a.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltVehicleContractTest, VehicleAndSoftBodyAggregateIdsAreDisjoint)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& vehicles = System().Vehicles();
  auto& soft_bodies = System().SoftBodies();
  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kDynamic;
  const auto chassis = bodies.CreateBody(world_id, body_desc);
  const auto wheel_a = bodies.CreateBody(world_id, body_desc);
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

  const auto soft_body_settings_blob = MakeSoftBodySettingsBlob(4U);
  const auto soft_body = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .cluster_count = 4U,
      .settings_blob = soft_body_settings_blob,
    });
  ASSERT_TRUE(soft_body.has_value());

  EXPECT_NE(vehicle.value(), soft_body.value());

  EXPECT_TRUE(
    soft_bodies.DestroySoftBody(world_id, soft_body.value()).has_value());
  EXPECT_TRUE(vehicles.DestroyVehicle(world_id, vehicle.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_b.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_a.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltVehicleContractTest, DestroyAfterWorldDestroyedPurgesLocalVehicleState)
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
  const auto chassis = bodies.CreateBody(world_id, body_desc);
  const auto wheel_a = bodies.CreateBody(world_id, body_desc);
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

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());

  const auto destroy_after_world
    = vehicles.DestroyVehicle(world_id, vehicle.value());
  ASSERT_TRUE(destroy_after_world.has_error());
  EXPECT_EQ(destroy_after_world.error(), PhysicsError::kWorldNotFound);

  const auto destroy_again = vehicles.DestroyVehicle(world_id, vehicle.value());
  ASSERT_TRUE(destroy_again.has_error());
  EXPECT_EQ(destroy_again.error(), PhysicsError::kWorldNotFound);
}

} // namespace oxygen::physics::test::jolt
