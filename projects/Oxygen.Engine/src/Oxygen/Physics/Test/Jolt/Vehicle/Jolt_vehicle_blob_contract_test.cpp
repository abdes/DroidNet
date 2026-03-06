//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltVehicleBlobContractTest : public JoltTestFixture { };

  [[nodiscard]] auto MakeInvalidNonCookedVehicleBlob() -> std::vector<uint8_t>
  {
    // Intentionally non-cooked payload envelope; strict restore must reject it.
    return std::vector<uint8_t> {
      'O',
      'P',
      'H',
      'B', // magic
      1U, // version
      2U, // kind = vehicle
      0U, // flavor
      0U, // reserved
      2U,
      0U,
      0U,
      0U, // payload_size = 2 ("{}")
      '{',
      '}',
    };
  }

} // namespace

NOLINT_TEST_F(
  JoltVehicleBlobContractTest, CreateVehicleRejectsNonCookedBlobPayload)
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

  const auto vehicle = vehicles.CreateVehicle(world_id,
    vehicle::VehicleDesc {
      .chassis_body_id = chassis.value(),
      .wheels = wheel_descs,
      .constraint_settings_blob = MakeInvalidNonCookedVehicleBlob(),
      .controller_type = vehicle::VehicleControllerType::kWheeled,
    });
  ASSERT_TRUE(vehicle.has_error());
  EXPECT_EQ(vehicle.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_b.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel_a.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
