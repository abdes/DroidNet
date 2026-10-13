//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::time::CanonicalDuration;
using oxygen::time::SimulationClock;
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
  EXPECT_EQ(res.steps_executed, 3U);
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
  EXPECT_EQ(res.steps_executed, 0U);
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
  EXPECT_EQ(res.steps_executed, 2U);
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
  EXPECT_EQ(res.steps_executed, 3U);
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
  EXPECT_EQ(res.steps_executed, 3U);
}

//! A frame step runs exactly one fixed step while paused, with the fixed
//! timestep as the delta, and leaves the clock paused.
NOLINT_TEST_F(SimulationClockTest, StepFixedRunsOneStepWhilePaused)
{
  // Arrange
  SimulationClock clk { CanonicalDuration { 10ms } };
  clk.SetPaused(true);

  // Act
  clk.StepFixed();
  const auto stepped = clk.ExecuteFixedSteps(10);
  clk.Advance(CanonicalDuration { 100ms });
  const auto after = clk.ExecuteFixedSteps(10);

  // Assert
  EXPECT_EQ(stepped.steps_executed, 1U);
  EXPECT_EQ(stepped.interpolation_alpha, 0.0);
  EXPECT_EQ(clk.Now().get().time_since_epoch(), 10ms);
  EXPECT_TRUE(clk.IsPaused());
  EXPECT_EQ(after.steps_executed, 0U);
  EXPECT_EQ(clk.DeltaTime().get().count(), 0);
}

//! A frame step ignores the time scale and discards any accumulated
//! remainder, so it does not depend on wall time.
NOLINT_TEST_F(SimulationClockTest, StepFixedIgnoresScaleAndRemainder)
{
  // Arrange
  SimulationClock clk { CanonicalDuration { 10ms } };
  clk.SetTimeScale(4.0);
  clk.Advance(CanonicalDuration { 7ms });

  // Act
  clk.StepFixed();
  const auto delta = clk.DeltaTime();
  const auto stepped = clk.ExecuteFixedSteps(10);

  // Assert
  EXPECT_EQ(delta.get(), std::chrono::nanoseconds { 10ms });
  EXPECT_EQ(stepped.steps_executed, 1U);
  EXPECT_EQ(stepped.remaining_time.get().count(), 0);
}

//! Reset returns to time zero, unpaused, and keeps the time scale.
NOLINT_TEST_F(SimulationClockTest, ResetReturnsToTimeZero)
{
  // Arrange
  SimulationClock clk { CanonicalDuration { 10ms } };
  clk.SetTimeScale(2.0);
  clk.Advance(CanonicalDuration { 25ms });
  (void)clk.ExecuteFixedSteps(10);
  clk.SetPaused(true);

  // Act
  clk.Reset();

  // Assert
  EXPECT_EQ(clk.Now().get().time_since_epoch().count(), 0);
  EXPECT_FALSE(clk.IsPaused());
  EXPECT_EQ(clk.DeltaTime().get().count(), 0);
  EXPECT_EQ(clk.GetTimeScale(), 2.0);
  EXPECT_EQ(clk.ExecuteFixedSteps(10).steps_executed, 0U);
}

} // namespace
