//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Time/PhysicalClock.h>
#include <thread>

using namespace oxygen::time;
using namespace std::chrono_literals;

namespace {

// ReSharper disable once CppRedundantQualifier
class PhysicalClockTest : public ::testing::Test { };

//! Verify that Now() returns a valid time and Uptime() is non-negative.
NOLINT_TEST_F(PhysicalClockTest, NowAndUptime)
{
  // Arrange
  PhysicalClock clock;

  // Act: sample, wait a little, sample again
  auto now1 = clock.Now();
  auto uptime1 = clock.Uptime();

  std::this_thread::sleep_for(5ms);

  auto now2 = clock.Now();
  auto uptime2 = clock.Uptime();

  // Assert: time should move forward
  EXPECT_GT(now2, now1);
  EXPECT_GE(uptime2, uptime1);

  // There should be at least some positive forward progress (allow 1ms)
  EXPECT_GE(uptime2 - uptime1, CanonicalDuration { 1ms });
}

} // namespace
