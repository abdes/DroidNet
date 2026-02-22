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

  class JoltVehicleBasicTest : public JoltTestFixture { };

} // namespace

NOLINT_TEST_F(JoltVehicleBasicTest, CreateAndDestroyVehicleSucceeds)
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

  const std::array<BodyId, 2> wheel_ids {
    wheel.value(),
    wheel2.value(),
  };
  const auto vehicle = vehicles.CreateVehicle(world_id,
    vehicle::VehicleDesc {
      .chassis_body_id = chassis.value(),
      .wheel_body_ids = wheel_ids,
    });
  ASSERT_TRUE(vehicle.has_value());
  EXPECT_TRUE(IsValid(vehicle.value()));

  EXPECT_TRUE(vehicles.DestroyVehicle(world_id, vehicle.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel2.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, wheel.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, chassis.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
