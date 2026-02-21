//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include <Oxygen/Physics/Physics.h>

TEST(Physics, ReturnsDefaultBackendName)
{
#if defined(OXGN_PHYS_BACKEND_JOLT)
  EXPECT_EQ(oxygen::physics::GetBackendName(), "jolt");
  EXPECT_EQ(oxygen::physics::GetSelectedBackend(),
    oxygen::physics::PhysicsBackend::kJolt);
#elif defined(OXGN_PHYS_BACKEND_NONE)
  EXPECT_EQ(oxygen::physics::GetBackendName(), "none");
  EXPECT_EQ(oxygen::physics::GetSelectedBackend(),
    oxygen::physics::PhysicsBackend::kNone);
#else
  FAIL() << "No physics backend compile definition is set";
#endif
}

TEST(Physics, CreatesBackendSystemOrReturnsAvailabilityError)
{
  const auto result = oxygen::physics::CreatePhysicsSystem();
#if defined(OXGN_PHYS_BACKEND_JOLT)
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result.value() != nullptr);
  auto* physics = result.value().get();
  const auto pending_count
    = physics->GetPendingEventCount(oxygen::physics::kInvalidWorldId);
  ASSERT_TRUE(pending_count.has_error());
  EXPECT_EQ(pending_count.error(), oxygen::physics::PhysicsError::kWorldNotFound);
#elif defined(OXGN_PHYS_BACKEND_NONE)
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ(result.error(), oxygen::physics::PhysicsError::kBackendUnavailable);
#else
  FAIL() << "No physics backend compile definition is set";
#endif
}
