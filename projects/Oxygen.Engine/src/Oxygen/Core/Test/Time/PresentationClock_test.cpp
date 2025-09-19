//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Time/PresentationClock.h>
#include <Oxygen/Core/Time/SimulationClock.h>

using namespace oxygen::time;
using namespace std::chrono_literals;

namespace {

// ReSharper disable once CppRedundantQualifier
class PresentationClockTest : public ::testing::Test { };

NOLINT_TEST_F(PresentationClockTest, InterpolationClamping)
{
  SimulationClock sim { CanonicalDuration { 16ms } };
  PresentationClock pres { sim, 1.0 };

  pres.SetInterpolationAlpha(-0.5);
  EXPECT_DOUBLE_EQ(pres.GetInterpolationAlpha(), 0.0);

  pres.SetInterpolationAlpha(1.2);
  EXPECT_DOUBLE_EQ(pres.GetInterpolationAlpha(), 1.0);

  pres.SetInterpolationAlpha(0.5);
  EXPECT_DOUBLE_EQ(pres.GetInterpolationAlpha(), 0.5);
}

NOLINT_TEST_F(PresentationClockTest, SmoothDeltaScales)
{
  SimulationClock sim { CanonicalDuration { 1ms } };
  PresentationClock pres { sim, 1.0 };

  // Advance simulation by 10ms physical -> default timescale 1.0 -> 10ms
  // simulation
  sim.Advance(CanonicalDuration { 10ms });
  // Initially, scales by 1.0
  EXPECT_EQ(pres.DeltaTime().get().count(), sim.DeltaTime().get().count());

  // Change scale dynamically and verify scaled delta
  pres.SetAnimationScale(2.0);
  EXPECT_EQ(pres.DeltaTime().get().count(), sim.DeltaTime().get().count() * 2);
}

} // namespace
