//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltEventDomainTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltEventDomainTest, ContactBeginIsEmittedForOverlappingBodies)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& events = System().Events();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc static_desc {};
  static_desc.type = body::BodyType::kStatic;
  static_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
  const auto static_body = bodies.CreateBody(world_id, static_desc);
  ASSERT_TRUE(static_body.has_value());

  body::BodyDesc dynamic_desc {};
  dynamic_desc.type = body::BodyType::kDynamic;
  dynamic_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
  const auto dynamic_body = bodies.CreateBody(world_id, dynamic_desc);
  ASSERT_TRUE(dynamic_body.has_value());

  ASSERT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  const auto pending = events.GetPendingEventCount(world_id);
  ASSERT_TRUE(pending.has_value());
  ASSERT_GT(pending.value(), 0U);

  std::array<events::PhysicsEvent, 32> drained {};
  const auto drain = events.DrainEvents(world_id, drained);
  ASSERT_TRUE(drain.has_value());
  ASSERT_GT(drain.value(), 0U);
  auto found_begin = false;
  for (size_t i = 0; i < drain.value(); ++i) {
    if (drained[i].type == events::PhysicsEventType::kContactBegin) {
      found_begin = true;
      break;
    }
  }
  EXPECT_TRUE(found_begin);

  EXPECT_TRUE(bodies.DestroyBody(world_id, dynamic_body.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, static_body.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltEventDomainTest, ContactEndIsEmittedAfterBodyRemoval)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& events = System().Events();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc static_desc {};
  static_desc.type = body::BodyType::kStatic;
  static_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
  const auto static_body = bodies.CreateBody(world_id, static_desc);
  ASSERT_TRUE(static_body.has_value());

  body::BodyDesc dynamic_desc {};
  dynamic_desc.type = body::BodyType::kDynamic;
  dynamic_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
  const auto dynamic_body = bodies.CreateBody(world_id, dynamic_desc);
  ASSERT_TRUE(dynamic_body.has_value());

  ASSERT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  std::array<events::PhysicsEvent, 32> clear_buffer {};
  ASSERT_TRUE(events.DrainEvents(world_id, clear_buffer).has_value());

  ASSERT_TRUE(bodies.DestroyBody(world_id, dynamic_body.value()).has_value());
  ASSERT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());

  const auto pending = events.GetPendingEventCount(world_id);
  ASSERT_TRUE(pending.has_value());
  ASSERT_GT(pending.value(), 0U);

  std::array<events::PhysicsEvent, 32> drained {};
  const auto drain = events.DrainEvents(world_id, drained);
  ASSERT_TRUE(drain.has_value());
  ASSERT_GT(drain.value(), 0U);

  auto found_end = false;
  for (size_t i = 0; i < drain.value(); ++i) {
    if (drained[i].type == events::PhysicsEventType::kContactEnd) {
      found_end = true;
      break;
    }
  }
  EXPECT_TRUE(found_end);

  EXPECT_TRUE(bodies.DestroyBody(world_id, static_body.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
