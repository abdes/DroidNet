//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Time/Conversion.h>
#include <Oxygen/Core/Time/PhysicalClock.h>
#include <Oxygen/Engine/TimeManager.h>

using namespace std::chrono_literals;

using oxygen::engine::TimeManager;

namespace {

TEST(TimeManager, FrameLifecycleBasic)
{
  namespace t = oxygen::time;
  t::PhysicalClock phys;
  TimeManager::Config cfg {};
  cfg.timing.fixed_delta = t::CanonicalDuration { 1ms };
  TimeManager tm { phys, cfg };

  // Run a few frames; we cannot sleep here, but Begin/EndFrame should update
  // state
  for (int i = 0; i < 3; ++i) {
    tm.BeginFrame(phys.Now());
    tm.EndFrame();
  }

  const auto& data = tm.GetFrameTimingData();
  EXPECT_GE(data.physical_delta, t::CanonicalDuration {});
  EXPECT_GE(data.simulation_delta, t::CanonicalDuration {});
  EXPECT_GE(data.interpolation_alpha, 0.0);
  EXPECT_LE(data.interpolation_alpha, 1.0);

  const auto metrics = tm.GetPerformanceMetrics();
  EXPECT_GE(metrics.average_frame_time, t::CanonicalDuration {});
  EXPECT_GE(metrics.max_frame_time, t::CanonicalDuration {});
  EXPECT_GE(metrics.total_frames, 3U);
}

//! Ensure interpolation alpha flows from SimulationClock to PresentationClock
//! and produces a presentation time strictly between previous and current.
TEST(TimeManager, InterpolationAlphaFlow)
{
  constexpr auto kFixedDelta = std::chrono::microseconds(1000);
  // Arrange
  namespace t = oxygen::time;
  t::PhysicalClock phys;
  TimeManager::Config cfg {};
  cfg.timing.fixed_delta = t::CanonicalDuration { kFixedDelta };
  TimeManager tm { phys, cfg };

  // Simulate a couple of frames to accumulate some time
  tm.BeginFrame(phys.Now());
  tm.EndFrame();
  const auto prev_sim = tm.GetSimulationClock().Now();

  tm.BeginFrame(phys.Now());
  // After BeginFrame, interpolation alpha is computed and set on
  // PresentationClock
  const auto curr_sim = tm.GetSimulationClock().Now();

  // Act
  const auto alpha = tm.GetFrameTimingData().interpolation_alpha;
  const auto t_interp = t::presentation::Interpolate(prev_sim, curr_sim, alpha);
  const auto prev_pres
    = t::convert::ToPresentation(prev_sim, tm.GetPresentationClock());
  const auto curr_pres
    = t::convert::ToPresentation(curr_sim, tm.GetPresentationClock());

  // Assert
  // t should be within [prev, curr] in Presentation domain (no casts needed)
  EXPECT_LE(prev_pres, t_interp);
  EXPECT_LE(t_interp, curr_pres);

  // Also ensure that PresentationClock stores the same alpha
  EXPECT_DOUBLE_EQ(alpha, tm.GetPresentationClock().GetInterpolationAlpha());

  tm.EndFrame();
}

//! Renderer-facing smoke test: end-to-end sampling at interpolated
//! PresentationTime.
TEST(TimeManager, RendererSmokeInterpolatedSampling)
{
  // Arrange
  namespace t = oxygen::time;
  t::PhysicalClock phys;
  TimeManager::Config cfg {};
  // Use a tiny fixed timestep to ensure progress without sleeps.
  cfg.timing.fixed_delta = t::CanonicalDuration { 1ns };
  TimeManager tm { phys, cfg };

  // Simulate frame N
  tm.BeginFrame(phys.Now());
  tm.EndFrame();
  auto prev_sim = tm.GetSimulationClock().Now();

  // Simulate frame N+1 (update side)
  tm.BeginFrame(phys.Now());
  auto curr_sim = tm.GetSimulationClock().Now();

  // Renderer samples interpolated presentation time using stored alpha
  const auto alpha = tm.GetPresentationClock().GetInterpolationAlpha();
  const auto t_interp = t::presentation::Interpolate(prev_sim, curr_sim, alpha);

  // Dummy render hook capturing the time
  auto RenderAt = [](t::PresentationTime when) {
    // no-op, but ensures type is PresentationTime without assuming epoch > 0
    (void)when;
  };
  RenderAt(t_interp);

  // Verify sampling bounds in presentation domain
  const auto prev_pres
    = t::convert::ToPresentation(prev_sim, tm.GetPresentationClock());
  const auto curr_pres
    = t::convert::ToPresentation(curr_sim, tm.GetPresentationClock());
  EXPECT_LE(prev_pres, t_interp);
  EXPECT_LE(t_interp, curr_pres);

  tm.EndFrame();
}

//! Ensure that a large physical time jump is clamped by max_accumulator
//! to prevent the "spiral of death".
TEST(TimeManager, SpiralOfDeathProtection)
{
  namespace t = oxygen::time;
  t::PhysicalClock phys;

  static constexpr auto kFixedDelta = 10ms;
  static constexpr auto kMaxAccumulator = 50ms;
  static constexpr uint32_t kMaxSubsteps = 10;
  static constexpr auto kHangDuration = 5s;

  TimeManager::Config cfg {};
  cfg.timing.fixed_delta = t::CanonicalDuration { kFixedDelta };
  cfg.timing.max_accumulator = t::CanonicalDuration { kMaxAccumulator };
  cfg.timing.max_substeps = kMaxSubsteps;
  TimeManager tm { phys, cfg };

  // 1. Initial frame to establish baseline
  auto start_time = phys.Now();
  tm.BeginFrame(start_time);
  tm.EndFrame();

  // 2. Simulate a massive hang (5 seconds)
  auto hang_time = t::PhysicalTime { start_time.get() + kHangDuration };

  tm.BeginFrame(hang_time);

  // 3. Verify clamping:
  // Even though 5s passed, the accumulator should have been clamped to 50ms.
  // We execute 5 steps (50ms worth) and have ~0 remaining.
  const auto& data = tm.GetFrameTimingData();
  EXPECT_EQ(data.fixed_steps_executed, 5U);
  EXPECT_LT(tm.GetSimulationClock().ExecuteFixedSteps(1).steps_executed, 1U);

  tm.EndFrame();
}

//! Ensure that max_substeps is respected even if the accumulator has more time.
TEST(TimeManager, MaxSubstepsEnforcement)
{
  namespace t = oxygen::time;
  t::PhysicalClock phys;

  static constexpr auto kFixedDelta = 10ms;
  static constexpr auto kMaxAccumulator = 100ms;
  static constexpr uint32_t kMaxSubsteps = 2;
  static constexpr auto kJumpDuration = 50ms;

  TimeManager::Config cfg {};
  cfg.timing.fixed_delta = t::CanonicalDuration { kFixedDelta };
  cfg.timing.max_accumulator = t::CanonicalDuration { kMaxAccumulator };
  cfg.timing.max_substeps = kMaxSubsteps;
  TimeManager tm { phys, cfg };

  tm.BeginFrame(phys.Now());
  tm.EndFrame();

  // Advance by 50ms (5 steps worth)
  auto jump = t::PhysicalTime { phys.Now().get() + kJumpDuration };
  tm.BeginFrame(jump);

  // Should only execute 2 steps
  EXPECT_EQ(tm.GetFrameTimingData().fixed_steps_executed, kMaxSubsteps);

  tm.EndFrame();
}

//! Ensure time scaling correctly affects the simulation progress.
TEST(TimeManager, TimeScaling)
{
  namespace t = oxygen::time;
  t::PhysicalClock phys;

  static constexpr auto kFixedDelta = 10ms;
  static constexpr double kSlowMotionScale = 0.5;
  static constexpr auto kPhysicalDelta = 20ms;

  TimeManager::Config cfg {};
  cfg.timing.fixed_delta = t::CanonicalDuration { kFixedDelta };
  cfg.default_time_scale = kSlowMotionScale;
  TimeManager tm { phys, cfg };

  auto start = phys.Now();
  tm.BeginFrame(start);
  tm.EndFrame();

  // Pass 20ms of physical time
  tm.BeginFrame(t::PhysicalTime { start.get() + kPhysicalDelta });

  // scaled(20ms) = 10ms. 10ms / fixed(10ms) = 1 step.
  EXPECT_EQ(tm.GetFrameTimingData().fixed_steps_executed, 1U);
  tm.EndFrame();

  // Switch to double speed
  static constexpr double kDoubleSpeedScale = 2.0;
  tm.GetSimulationClock().SetTimeScale(kDoubleSpeedScale);
  tm.BeginFrame(t::PhysicalTime { start.get() + (kPhysicalDelta * 2) });

  // scaled(20ms) = 40ms. 40ms / fixed(10ms) = 4 steps.
  EXPECT_EQ(tm.GetFrameTimingData().fixed_steps_executed, 4U);
}

//! Ensure no simulation progress occurs when paused.
TEST(TimeManager, PausedState)
{
  namespace t = oxygen::time;
  t::PhysicalClock phys;

  static constexpr auto kFixedDelta = 10ms;
  static constexpr auto kLongDuration = 1s;

  TimeManager::Config cfg {};
  cfg.timing.fixed_delta = t::CanonicalDuration { kFixedDelta };
  cfg.start_paused = true;
  TimeManager tm { phys, cfg };

  auto start = phys.Now();
  tm.BeginFrame(start);
  tm.EndFrame();

  // Pass 1 second physical time
  tm.BeginFrame(t::PhysicalTime { start.get() + kLongDuration });

  // Zero steps should be executed
  EXPECT_EQ(tm.GetFrameTimingData().fixed_steps_executed, 0U);
  EXPECT_EQ(tm.GetSimulationClock().Now(), t::SimulationTime {});

  // Unpause
  tm.GetSimulationClock().SetPaused(false);
  tm.BeginFrame(t::PhysicalTime { start.get() + kLongDuration + kFixedDelta });
  EXPECT_EQ(tm.GetFrameTimingData().fixed_steps_executed, 1U);
}

//! Verify performance metrics history tracking.
TEST(TimeManager, PerformanceMetricsHistory)
{
  namespace t = oxygen::time;
  t::PhysicalClock phys;

  static constexpr auto kFixedDelta = 16ms;
  static constexpr auto kPhysDeltaPerFrame = 20ms;
  static constexpr uint32_t kNumFrames = 5;

  TimeManager::Config cfg {};
  cfg.timing.fixed_delta = t::CanonicalDuration { kFixedDelta };
  TimeManager tm { phys, cfg };

  // Run 5 frames with fixed 20ms delta
  auto now = phys.Now();
  for (uint32_t i = 0; i < kNumFrames; ++i) {
    now = t::PhysicalTime { now.get() + kPhysDeltaPerFrame };
    tm.BeginFrame(now);
    tm.EndFrame();
  }

  auto metrics = tm.GetPerformanceMetrics();
  EXPECT_EQ(metrics.total_frames, kNumFrames);
  static constexpr auto kFrameTimeTolerance = 1ms;
  const auto average_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    metrics.average_frame_time.get())
                            .count();
  const auto max_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    metrics.max_frame_time.get())
                        .count();
  const auto expected_ns
    = std::chrono::duration_cast<std::chrono::nanoseconds>(kPhysDeltaPerFrame)
        .count();
  const auto tolerance_ns
    = std::chrono::duration_cast<std::chrono::nanoseconds>(kFrameTimeTolerance)
        .count();
  EXPECT_NEAR(static_cast<double>(average_ns), static_cast<double>(expected_ns),
    static_cast<double>(tolerance_ns));
  EXPECT_NEAR(static_cast<double>(max_ns), static_cast<double>(expected_ns),
    static_cast<double>(tolerance_ns));
  EXPECT_NEAR(metrics.average_fps, 50.0, 0.1);
}

} // namespace
