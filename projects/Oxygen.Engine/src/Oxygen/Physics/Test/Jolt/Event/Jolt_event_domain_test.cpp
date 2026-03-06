//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <memory>
#include <utility>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/Test/TestBlobBuilders.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class Layer0OnlyCollisionFilter final : public ICollisionFilter {
  public:
    [[nodiscard]] auto GetBroadPhaseLayer(
      const CollisionLayer layer) const noexcept -> BroadPhaseLayer override
    {
      return layer.get() == 0U ? 0U : 1U;
    }

    [[nodiscard]] auto ShouldCollide(const CollisionLayer layer1,
      const CollisionLayer layer2) const noexcept -> bool override
    {
      return layer1.get() == 0U && layer2.get() == 0U;
    }

    [[nodiscard]] auto ShouldCollide(const CollisionLayer layer1,
      const BroadPhaseLayer bp_layer) const noexcept -> bool override
    {
      if (layer1.get() != 0U) {
        return false;
      }
      return bp_layer == 0U;
    }
  };

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

NOLINT_TEST_F(JoltEventDomainTest, CollisionMasksCanBlockContactGeneration)
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
  static_desc.collision_layer = CollisionLayer { 0U };
  static_desc.collision_mask = CollisionMask { 1U << 0U };
  const auto static_body = bodies.CreateBody(world_id, static_desc);
  ASSERT_TRUE(static_body.has_value());

  body::BodyDesc dynamic_desc {};
  dynamic_desc.type = body::BodyType::kDynamic;
  dynamic_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
  dynamic_desc.collision_layer = CollisionLayer { 1U };
  dynamic_desc.collision_mask = CollisionMask { 1U << 1U };
  const auto dynamic_body = bodies.CreateBody(world_id, dynamic_desc);
  ASSERT_TRUE(dynamic_body.has_value());

  ASSERT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());

  const auto pending = events.GetPendingEventCount(world_id);
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending.value(), 0U);

  std::array<events::PhysicsEvent, 8> drained {};
  const auto drain = events.DrainEvents(world_id, drained);
  ASSERT_TRUE(drain.has_value());
  EXPECT_EQ(drain.value(), 0U);

  EXPECT_TRUE(bodies.DestroyBody(world_id, dynamic_body.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, static_body.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltEventDomainTest, WorldCollisionFilterCanBlockLayerPairs)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& events = System().Events();

  world::WorldDesc desc {};
  desc.collision_filter = std::make_shared<Layer0OnlyCollisionFilter>();
  const auto world_result = worlds.CreateWorld(desc);
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc static_desc {};
  static_desc.type = body::BodyType::kStatic;
  static_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
  static_desc.collision_layer = CollisionLayer { 0U };
  const auto static_body = bodies.CreateBody(world_id, static_desc);
  ASSERT_TRUE(static_body.has_value());

  body::BodyDesc blocked_dynamic_desc {};
  blocked_dynamic_desc.type = body::BodyType::kDynamic;
  blocked_dynamic_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
  blocked_dynamic_desc.collision_layer = CollisionLayer { 1U };
  const auto blocked_dynamic
    = bodies.CreateBody(world_id, blocked_dynamic_desc);
  ASSERT_TRUE(blocked_dynamic.has_value());

  ASSERT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  const auto blocked_pending = events.GetPendingEventCount(world_id);
  ASSERT_TRUE(blocked_pending.has_value());
  EXPECT_EQ(blocked_pending.value(), 0U);

  EXPECT_TRUE(
    bodies.DestroyBody(world_id, blocked_dynamic.value()).has_value());

  body::BodyDesc allowed_dynamic_desc {};
  allowed_dynamic_desc.type = body::BodyType::kDynamic;
  allowed_dynamic_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
  allowed_dynamic_desc.collision_layer = CollisionLayer { 0U };
  const auto allowed_dynamic
    = bodies.CreateBody(world_id, allowed_dynamic_desc);
  ASSERT_TRUE(allowed_dynamic.has_value());

  ASSERT_TRUE(worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  const auto allowed_pending = events.GetPendingEventCount(world_id);
  ASSERT_TRUE(allowed_pending.has_value());
  EXPECT_GT(allowed_pending.value(), 0U);

  EXPECT_TRUE(
    bodies.DestroyBody(world_id, allowed_dynamic.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, static_body.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltEventDomainTest, SoftBodyTriggerOverlapReportsTriggerWithoutFloorLeakage)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& soft_bodies = System().SoftBodies();
  auto& events = System().Events();

  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc floor_desc {};
  floor_desc.type = body::BodyType::kStatic;
  floor_desc.shape = BoxShape { .extents = Vec3 { 6.0F, 0.5F, 6.0F } };
  floor_desc.initial_position = Vec3 { 0.0F, -1.5F, 0.0F };
  const auto floor_body = bodies.CreateBody(world_id, floor_desc);
  ASSERT_TRUE(floor_body.has_value());

  body::BodyDesc trigger_desc {};
  trigger_desc.type = body::BodyType::kStatic;
  trigger_desc.flags = body::BodyFlags::kIsTrigger;
  trigger_desc.shape = BoxShape { .extents = Vec3 { 4.0F, 4.0F, 4.0F } };
  trigger_desc.initial_position = Vec3 { 0.0F, 0.5F, 0.0F };
  const auto trigger_body = bodies.CreateBody(world_id, trigger_desc);
  ASSERT_TRUE(trigger_body.has_value());

  const auto soft_body_settings_blob = MakeSoftBodySettingsBlob(4U);
  const auto soft_body = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .cluster_count = 4U,
      .initial_position = Vec3 { 0.0F, 3.0F, 0.0F },
      .settings_blob = soft_body_settings_blob,
      .solver_iteration_count = 8U,
    });
  ASSERT_TRUE(soft_body.has_value());
  ASSERT_TRUE(soft_bodies.FlushStructuralChanges(world_id).has_value());

  for (int i = 0; i < 300; ++i) {
    ASSERT_TRUE(
      worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
  }

  std::array<events::PhysicsEvent, 512> drained {};
  const auto drain = events.DrainEvents(world_id, drained);
  ASSERT_TRUE(drain.has_value());
  auto found_trigger_begin_for_trigger = false;
  auto found_contact_begin_for_trigger = false;
  for (size_t i = 0; i < drain.value(); ++i) {
    const auto& event = drained[i];
    if (event.type == events::PhysicsEventType::kTriggerBegin
      && (event.body_b == trigger_body.value()
        || event.body_a == trigger_body.value())) {
      found_trigger_begin_for_trigger = true;
    } else if (event.type == events::PhysicsEventType::kContactBegin
      && (event.body_b == trigger_body.value()
        || event.body_a == trigger_body.value())) {
      found_contact_begin_for_trigger = true;
    }
  }
  EXPECT_TRUE(found_trigger_begin_for_trigger);
  EXPECT_FALSE(found_contact_begin_for_trigger);

  const auto state_after = soft_bodies.GetState(world_id, soft_body.value());
  ASSERT_TRUE(state_after.has_value());
  EXPECT_TRUE(std::isfinite(state_after.value().center_of_mass.x));
  EXPECT_TRUE(std::isfinite(state_after.value().center_of_mass.y));
  EXPECT_TRUE(std::isfinite(state_after.value().center_of_mass.z));
  EXPECT_GT(state_after.value().center_of_mass.y, -0.25F);

  EXPECT_TRUE(
    soft_bodies.DestroySoftBody(world_id, soft_body.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, floor_body.value()).has_value());
  EXPECT_TRUE(bodies.DestroyBody(world_id, trigger_body.value()).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(
  JoltEventDomainTest, TriggerBodiesReportOverlapWithoutBlockingMotion)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& events = System().Events();

  const auto run_scenario
    = [&](const bool static_is_trigger) -> std::pair<float, bool> {
    const auto world_result = worlds.CreateWorld(world::WorldDesc {});
    EXPECT_TRUE(world_result.has_value());
    if (!world_result.has_value()) {
      return { 0.0F, false };
    }
    const auto world_id = world_result.value();

    body::BodyDesc static_desc {};
    static_desc.type = body::BodyType::kStatic;
    static_desc.initial_position = Vec3 { 0.0F, 0.0F, 0.0F };
    static_desc.flags = static_is_trigger ? body::BodyFlags::kIsTrigger
                                          : body::BodyFlags::kNone;
    const auto static_body = bodies.CreateBody(world_id, static_desc);
    EXPECT_TRUE(static_body.has_value());
    if (!static_body.has_value()) {
      (void)worlds.DestroyWorld(world_id);
      return { 0.0F, false };
    }

    body::BodyDesc dynamic_desc {};
    dynamic_desc.type = body::BodyType::kDynamic;
    dynamic_desc.flags = body::BodyFlags::kNone;
    dynamic_desc.gravity_factor = 0.0F;
    dynamic_desc.initial_position = Vec3 { 0.0F, 2.0F, 0.0F };
    const auto dynamic_body = bodies.CreateBody(world_id, dynamic_desc);
    EXPECT_TRUE(dynamic_body.has_value());
    if (!dynamic_body.has_value()) {
      (void)bodies.DestroyBody(world_id, static_body.value());
      (void)worlds.DestroyWorld(world_id);
      return { 0.0F, false };
    }
    EXPECT_TRUE(bodies
        .SetLinearVelocity(
          world_id, dynamic_body.value(), Vec3 { 0.0F, -2.0F, 0.0F })
        .has_value());

    for (int i = 0; i < 60; ++i) {
      EXPECT_TRUE(
        worlds.Step(world_id, 1.0F / 60.0F, 1, 1.0F / 60.0F).has_value());
    }

    const auto position
      = bodies.GetBodyPosition(world_id, dynamic_body.value());
    EXPECT_TRUE(position.has_value());
    const auto y = position.has_value() ? position.value().y : 0.0F;

    std::array<events::PhysicsEvent, 64> drained {};
    const auto drain = events.DrainEvents(world_id, drained);
    EXPECT_TRUE(drain.has_value());
    auto found_begin = false;
    if (drain.has_value()) {
      for (size_t i = 0; i < drain.value(); ++i) {
        const auto expected_type = static_is_trigger
          ? events::PhysicsEventType::kTriggerBegin
          : events::PhysicsEventType::kContactBegin;
        if (drained[i].type == expected_type) {
          found_begin = true;
          break;
        }
      }
    }

    EXPECT_TRUE(bodies.DestroyBody(world_id, dynamic_body.value()).has_value());
    EXPECT_TRUE(bodies.DestroyBody(world_id, static_body.value()).has_value());
    EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
    return { y, found_begin };
  };

  const auto [solid_final_y, solid_found_begin] = run_scenario(false);
  const auto [trigger_final_y, trigger_found_begin] = run_scenario(true);

  EXPECT_TRUE(solid_found_begin);
  EXPECT_TRUE(trigger_found_begin);
  EXPECT_LT(trigger_final_y, solid_final_y - 0.25F);
}

} // namespace oxygen::physics::test::jolt
