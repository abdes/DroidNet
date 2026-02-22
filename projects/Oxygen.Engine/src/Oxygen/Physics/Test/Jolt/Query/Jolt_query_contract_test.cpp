//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Query/Overlap.h>
#include <Oxygen/Physics/Query/Raycast.h>
#include <Oxygen/Physics/Query/Sweep.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltQueryContractTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltQueryContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  RequireBackend();

  auto& queries = System().Queries();
  std::array<query::SweepHit, 1> sweep_hits {};
  std::array<uint64_t, 1> overlap_hits {};

  const auto raycast = queries.Raycast(kInvalidWorldId, query::RaycastDesc {});
  ASSERT_TRUE(raycast.has_error());
  EXPECT_EQ(raycast.error(), PhysicsError::kWorldNotFound);

  const auto sweep
    = queries.Sweep(kInvalidWorldId, query::SweepDesc {}, sweep_hits);
  ASSERT_TRUE(sweep.has_error());
  EXPECT_EQ(sweep.error(), PhysicsError::kWorldNotFound);

  const auto overlap
    = queries.Overlap(kInvalidWorldId, query::OverlapDesc {}, overlap_hits);
  ASSERT_TRUE(overlap.has_error());
  EXPECT_EQ(overlap.error(), PhysicsError::kWorldNotFound);
}

NOLINT_TEST_F(JoltQueryContractTest, RaycastRejectsInvalidDistance)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& queries = System().Queries();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  query::RaycastDesc desc {};
  desc.max_distance = 0.0F;
  const auto raycast = queries.Raycast(world_id, desc);
  ASSERT_TRUE(raycast.has_error());
  EXPECT_EQ(raycast.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

NOLINT_TEST_F(JoltQueryContractTest, SweepRejectsInvalidDistance)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& queries = System().Queries();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  query::SweepDesc desc {};
  desc.max_distance = 0.0F;
  std::array<query::SweepHit, 4> sweep_hits {};
  const auto sweep = queries.Sweep(world_id, desc, sweep_hits);
  ASSERT_TRUE(sweep.has_error());
  EXPECT_EQ(sweep.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
