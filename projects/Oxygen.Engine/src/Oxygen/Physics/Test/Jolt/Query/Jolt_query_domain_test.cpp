//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Query/Overlap.h>
#include <Oxygen/Physics/Query/Raycast.h>
#include <Oxygen/Physics/Query/Sweep.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltQueryDomainTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltQueryDomainTest, RaycastHitsBodyInFront)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& queries = System().Queries();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kStatic;
  body_desc.initial_position = Vec3 { 0.0F, 0.0F, 5.0F };
  const auto body_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  query::RaycastDesc ray_desc {};
  ray_desc.origin = Vec3 { 0.0F, 0.0F, 0.0F };
  ray_desc.direction = Vec3 { 0.0F, 0.0F, 1.0F };
  ray_desc.max_distance = 10.0F;
  const auto raycast = queries.Raycast(world_id, ray_desc);
  ASSERT_TRUE(raycast.has_value());
  ASSERT_TRUE(raycast.value().has_value());
  EXPECT_EQ(raycast.value()->body_id, body_id);
  EXPECT_GT(raycast.value()->distance, 0.0F);
  EXPECT_LT(raycast.value()->distance, 10.0F);

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltQueryDomainTest, RaycastRespectsIgnoreBodies)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& queries = System().Queries();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kStatic;
  body_desc.initial_position = Vec3 { 0.0F, 0.0F, 5.0F };
  const auto body_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  const std::array<BodyId, 1> ignored { body_id };
  query::RaycastDesc ray_desc {};
  ray_desc.origin = Vec3 { 0.0F, 0.0F, 0.0F };
  ray_desc.direction = Vec3 { 0.0F, 0.0F, 1.0F };
  ray_desc.max_distance = 10.0F;
  ray_desc.ignore_bodies = ignored;
  const auto raycast = queries.Raycast(world_id, ray_desc);
  ASSERT_TRUE(raycast.has_value());
  EXPECT_FALSE(raycast.value().has_value());

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltQueryDomainTest, SweepAndOverlapDetectBody)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& bodies = System().Bodies();
  auto& queries = System().Queries();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  body::BodyDesc body_desc {};
  body_desc.type = body::BodyType::kStatic;
  body_desc.initial_position = Vec3 { 0.0F, 0.0F, 5.0F };
  const auto body_result = bodies.CreateBody(world_id, body_desc);
  ASSERT_TRUE(body_result.has_value());
  const auto body_id = body_result.value();

  query::SweepDesc sweep_desc {};
  sweep_desc.origin = Vec3 { 0.0F, 0.0F, 0.0F };
  sweep_desc.direction = Vec3 { 0.0F, 0.0F, 1.0F };
  sweep_desc.max_distance = 10.0F;
  std::array<query::SweepHit, 8> sweep_hits {};
  const auto sweep = queries.Sweep(world_id, sweep_desc, sweep_hits);
  ASSERT_TRUE(sweep.has_value());
  ASSERT_GT(sweep.value(), 0U);
  EXPECT_EQ(sweep_hits[0].body_id, body_id);

  query::OverlapDesc overlap_desc {};
  overlap_desc.center = Vec3 { 0.0F, 0.0F, 5.0F };
  std::array<uint64_t, 8> overlap_hits {};
  const auto overlap = queries.Overlap(world_id, overlap_desc, overlap_hits);
  ASSERT_TRUE(overlap.has_value());
  EXPECT_GT(overlap.value(), 0U);

  EXPECT_TRUE(bodies.DestroyBody(world_id, body_id).has_value());
  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
