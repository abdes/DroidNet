//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Time/SimulationClock.h>

using namespace oxygen::time;
using namespace std::chrono_literals;

namespace {

class SimulationClockTest : public ::testing::Test { };

NOLINT_TEST_F(SimulationClockTest, FixedStepExecution)
{
  // Arrange
  SimulationClock clk { CanonicalDuration { 1ms } }; // 1ms step

  // Act: advance by 3.5 ms
  clk.Advance(CanonicalDuration { 3500us });
  auto res = clk.ExecuteFixedSteps(10);

  // Assert
  EXPECT_EQ(res.steps_executed, 3u);
  EXPECT_GE(res.interpolation_alpha, 0.0);
  EXPECT_LE(res.interpolation_alpha, 1.0);
}

//! When paused, advancing the physical clock should not change simulation time.
NOLINT_TEST_F(SimulationClockTest, PausePreventsAdvancement)
{
  // Arrange
  SimulationClock clk { CanonicalDuration { 10ms } };
  clk.SetPaused(true);

  // Act
  clk.Advance(CanonicalDuration { 100ms });
  auto res = clk.ExecuteFixedSteps(10);

  // Assert
  EXPECT_EQ(res.steps_executed, 0u);
  EXPECT_EQ(res.remaining_time.get().count(), 0);
}

//! Time scale multiplies the effective delta applied to the simulation.
NOLINT_TEST_F(SimulationClockTest, TimeScaleApplied)
{
  // Arrange
  SimulationClock clk { CanonicalDuration { 10ms } };
  clk.SetTimeScale(2.0); // double speed

  // Act: advance by 10ms physical -> 20ms simulated -> 2 steps of 10ms
  clk.Advance(CanonicalDuration { 10ms });
  auto res = clk.ExecuteFixedSteps(10);

  // Assert
  EXPECT_EQ(res.steps_executed, 2u);
}

//! ExecuteFixedSteps should respect max_steps and return remaining_time
//! accordingly.
NOLINT_TEST_F(SimulationClockTest, MaxStepsRespected)
{
  // Arrange
  SimulationClock clk { CanonicalDuration { 10ms } };

  // Act: provide enough accumulated time for 5 steps
  clk.Advance(CanonicalDuration { 50ms });
  auto res = clk.ExecuteFixedSteps(3); // limit to 3 steps

  // Assert: only 3 executed and remaining_time should equal 2 steps
  EXPECT_EQ(res.steps_executed, 3u);
  EXPECT_EQ(res.remaining_time, CanonicalDuration { 20ms });
  // Interpolation alpha should be in [0,1]
  EXPECT_GE(res.interpolation_alpha, 0.0);
  EXPECT_LE(res.interpolation_alpha, 1.0);
}

// Negative time scale should be ignored and not apply.
NOLINT_TEST_F(SimulationClockTest, NegativeTimeScaleIgnored)
{
  // Arrange
  SimulationClock clk { CanonicalDuration { 10ms } };
  clk.SetTimeScale(-1.0);

  // Act
  clk.Advance(CanonicalDuration { 30ms });
  auto res = clk.ExecuteFixedSteps(10);

  // Assert: negative scale ignored -> default scale 1.0 so 3 steps executed
  EXPECT_EQ(res.steps_executed, 3u);
}

} // namespace
