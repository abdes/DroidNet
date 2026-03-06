//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>
#include <Oxygen/Physics/World/WorldDesc.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltSoftBodyBlobContractTest : public JoltTestFixture { };

  [[nodiscard]] auto MakeInvalidNonCookedSoftBodyBlob() -> std::vector<uint8_t>
  {
    // Intentionally non-cooked payload envelope; strict restore must reject it.
    return std::vector<uint8_t> {
      'O',
      'P',
      'H',
      'B', // magic
      1U, // version
      3U, // kind = soft-body
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
  JoltSoftBodyBlobContractTest, CreateSoftBodyRejectsNonCookedBlobPayload)
{
  AssertBackendAvailabilityContract();
  if (!HasBackend()) {
    return;
  }

  auto& worlds = System().Worlds();
  auto& soft_bodies = System().SoftBodies();

  const auto world = worlds.CreateWorld(world::WorldDesc {});
  ASSERT_TRUE(world.has_value());
  const auto world_id = world.value();

  const auto created = soft_bodies.CreateSoftBody(world_id,
    softbody::SoftBodyDesc {
      .cluster_count = 4U,
      .settings_blob = MakeInvalidNonCookedSoftBodyBlob(),
    });
  ASSERT_TRUE(created.has_error());
  EXPECT_EQ(created.error(), PhysicsError::kInvalidArgument);

  EXPECT_TRUE(worlds.DestroyWorld(world_id).has_value());
}

} // namespace oxygen::physics::test::jolt
