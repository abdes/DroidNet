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

using namespace oxygen::time;
using namespace std::chrono_literals;

using oxygen::engine::TimeManager;

namespace {

TEST(TimeManager, FrameLifecycleBasic)
{
  PhysicalClock phys;
  TimeManager::Config cfg {};
  cfg.fixed_timestep = CanonicalDuration { 1ms };
  TimeManager tm { phys, cfg };

  // Run a few frames; we cannot sleep here, but Begin/EndFrame should update
  // state
  for (int i = 0; i < 3; ++i) {
    tm.BeginFrame();
    tm.EndFrame();
  }

  const auto& data = tm.GetFrameTimingData();
  EXPECT_GE(data.physical_delta, CanonicalDuration {});
  EXPECT_GE(data.simulation_delta, CanonicalDuration {});
  EXPECT_GE(data.interpolation_alpha, 0.0);
  EXPECT_LE(data.interpolation_alpha, 1.0);

  const auto metrics = tm.GetPerformanceMetrics();
  EXPECT_GE(metrics.average_frame_time, CanonicalDuration {});
  EXPECT_GE(metrics.max_frame_time, CanonicalDuration {});
  EXPECT_GE(metrics.total_frames, 3u);
}

//! Ensure interpolation alpha flows from SimulationClock to PresentationClock
//! and produces a presentation time strictly between previous and current.
TEST(TimeManager, InterpolationAlphaFlow)
{
  // Arrange
  PhysicalClock phys;
  TimeManager::Config cfg {};
  cfg.fixed_timestep = CanonicalDuration { std::chrono::microseconds(1000) };
  TimeManager tm { phys, cfg };

  // Simulate a couple of frames to accumulate some time
  tm.BeginFrame();
  tm.EndFrame();
  const auto prev_sim = tm.GetSimulationClock().Now();

  tm.BeginFrame();
  // After BeginFrame, interpolation alpha is computed and set on
  // PresentationClock
  const auto curr_sim = tm.GetSimulationClock().Now();

  // Act
  const auto alpha = tm.GetFrameTimingData().interpolation_alpha;
  const auto t = presentation::Interpolate(prev_sim, curr_sim, alpha);
  const auto prev_pres
    = convert::ToPresentation(prev_sim, tm.GetPresentationClock());
  const auto curr_pres
    = convert::ToPresentation(curr_sim, tm.GetPresentationClock());

  // Assert
  // t should be within [prev, curr] in Presentation domain (no casts needed)
  EXPECT_LE(prev_pres, t);
  EXPECT_LE(t, curr_pres);

  // Also ensure that PresentationClock stores the same alpha
  EXPECT_DOUBLE_EQ(alpha, tm.GetPresentationClock().GetInterpolationAlpha());

  tm.EndFrame();
}

//! Renderer-facing smoke test: end-to-end sampling at interpolated
//! PresentationTime.
TEST(TimeManager, RendererSmoke_InterpolatedSampling)
{
  // Arrange
  PhysicalClock phys;
  TimeManager::Config cfg {};
  // Use a tiny fixed timestep to ensure progress without sleeps.
  cfg.fixed_timestep = CanonicalDuration { 1ns };
  TimeManager tm { phys, cfg };

  // Simulate frame N
  tm.BeginFrame();
  tm.EndFrame();
  auto prev_sim = tm.GetSimulationClock().Now();

  // Simulate frame N+1 (update side)
  tm.BeginFrame();
  auto curr_sim = tm.GetSimulationClock().Now();

  // Renderer samples interpolated presentation time using stored alpha
  const auto alpha = tm.GetPresentationClock().GetInterpolationAlpha();
  const auto t = presentation::Interpolate(prev_sim, curr_sim, alpha);

  // Dummy render hook capturing the time
  auto RenderAt = [](PresentationTime when) {
    // no-op, but ensures type is PresentationTime without assuming epoch > 0
    (void)when;
  };
  RenderAt(t);

  // Verify sampling bounds in presentation domain
  const auto prev_pres
    = convert::ToPresentation(prev_sim, tm.GetPresentationClock());
  const auto curr_pres
    = convert::ToPresentation(curr_sim, tm.GetPresentationClock());
  EXPECT_LE(prev_pres, t);
  EXPECT_LE(t, curr_pres);

  tm.EndFrame();
}

} // namespace
