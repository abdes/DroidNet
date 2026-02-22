//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltEventContractTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltEventContractTest, InvalidWorldCallsReturnWorldNotFound)
{
  RequireBackend();

  auto& events = System().Events();
  const auto pending = events.GetPendingEventCount(kInvalidWorldId);
  ASSERT_TRUE(pending.has_error());
  EXPECT_EQ(pending.error(), PhysicsError::kWorldNotFound);

  std::array<events::PhysicsEvent, 2> buffer {};
  const auto drain = events.DrainEvents(kInvalidWorldId, buffer);
  ASSERT_TRUE(drain.has_error());
  EXPECT_EQ(drain.error(), PhysicsError::kWorldNotFound);
}

} // namespace oxygen::physics::test::jolt
