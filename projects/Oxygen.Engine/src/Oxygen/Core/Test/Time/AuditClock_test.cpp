//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Time/AuditClock.h>
#include <Oxygen/Core/Time/PhysicalClock.h>

using namespace oxygen::time;
using namespace std::chrono_literals;

namespace {

// ReSharper disable once CppRedundantQualifier
class AuditClockTest : public ::testing::Test { };

//! Ensure conversion between physical steady time and wall-clock time is
//! consistent.
NOLINT_TEST_F(AuditClockTest, NowAndRoundTrip)
{
  // Arrange
  AuditClock clock;

  // Act
  PhysicalClock pclock;
  auto phys_now = pclock.Now();
  auto wall = clock.ToWallClock(phys_now);
  auto phys_back = clock.FromWallClock(wall);

  // Assert: round-trip should be close — allow small differences due to
  // conversion/resolution.
  const auto diff = CanonicalDuration { std::chrono::nanoseconds(
    std::llabs(phys_now.get().time_since_epoch().count()
      - phys_back.get().time_since_epoch().count())) };
  EXPECT_LE(diff, CanonicalDuration { 1ms });
  // Wall time should be representable
  EXPECT_GE(clock.Now(), AuditTime {});
}

} // namespace
