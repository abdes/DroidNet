//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Physics/Test/Jolt/Jolt_test_fixture.h>

namespace oxygen::physics::test::jolt {
namespace {

  class JoltJointBasicTest : public JoltTestFixture {
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

NOLINT_TEST_F(JoltJointBasicTest, BackendAvailabilityMatchesContract)
{
  AssertBackendAvailabilityContract();
}

} // namespace oxygen::physics::test::jolt
