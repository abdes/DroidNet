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

  class JoltQueryBasicTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltQueryBasicTest, BackendAvailabilityMatchesContract)
{
  AssertBackendAvailabilityContract();
}

NOLINT_TEST_F(JoltQueryBasicTest, EmptyWorldReturnsNoHits)
{
  RequireBackend();

  auto& worlds = System().Worlds();
  auto& queries = System().Queries();
  const auto world_result = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world_result.has_value());
  const auto world_id = world_result.value();

  const auto raycast = queries.Raycast(world_id, query::RaycastDesc {});
  ASSERT_TRUE(raycast.has_value());
  EXPECT_FALSE(raycast.value().has_value());

  std::array<query::SweepHit, 8> sweep_hits {};
  const auto sweep = queries.Sweep(world_id, query::SweepDesc {}, sweep_hits);
  ASSERT_TRUE(sweep.has_value());
  EXPECT_EQ(sweep.value(), 0U);

  std::array<uint64_t, 8> overlap_hits {};
  const auto overlap
    = queries.Overlap(world_id, query::OverlapDesc {}, overlap_hits);
  ASSERT_TRUE(overlap.has_value());
  EXPECT_EQ(overlap.value(), 0U);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
