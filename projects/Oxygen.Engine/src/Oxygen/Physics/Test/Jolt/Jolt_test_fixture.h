//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Physics.h>

namespace oxygen::physics::test::jolt {

class JoltTestFixture : public testing::Test {
protected:
  auto SetUp() -> void override { system_result_ = CreatePhysicsSystem(); }

  [[nodiscard]] auto HasBackend() const noexcept -> bool
  {
#if defined(OXGN_PHYS_BACKEND_JOLT)
    return true;
#elif defined(OXGN_PHYS_BACKEND_NONE)
    return false;
#else
#  error "No physics backend compile definition is set"
#endif
  }

  auto AssertBackendAvailabilityContract() const -> void
  {
#if defined(OXGN_PHYS_BACKEND_JOLT)
    ASSERT_TRUE(system_result_.has_value());
    ASSERT_TRUE(system_result_.value() != nullptr);
#elif defined(OXGN_PHYS_BACKEND_NONE)
    ASSERT_TRUE(system_result_.has_error());
    EXPECT_EQ(system_result_.error(), PhysicsError::kBackendUnavailable);
#else
#  error "No physics backend compile definition is set"
#endif
  }

  [[nodiscard]] auto System() -> system::IPhysicsSystem&
  {
    return *system_result_.value();
  }

private:
  PhysicsResult<std::unique_ptr<system::IPhysicsSystem>> system_result_
    = Err(PhysicsError::kBackendUnavailable);
};

} // namespace oxygen::physics::test::jolt
