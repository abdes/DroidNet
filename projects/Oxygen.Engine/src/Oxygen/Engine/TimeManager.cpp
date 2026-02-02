//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Engine/TimeManager.h>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace oxygen::time;

namespace oxygen::engine {

TimeManager::TimeManager(
  const PhysicalClock& physical_clock, const Config& config) noexcept
  : physical_clock_ { &physical_clock }
  , simulation_clock_ { config.fixed_timestep }
  , presentation_clock_ { simulation_clock_, config.animation_scale }
{
  simulation_clock_.SetTimeScale(config.default_time_scale);
  simulation_clock_.SetPaused(config.start_paused);
  network_clock_.SetSmoothingFactor(config.network_smoothing_factor);

  // Initialize last_frame_time_ to "now" to avoid a large first delta
  last_frame_time_ = physical_clock_->Now();
}

auto TimeManager::BeginFrame(time::PhysicalTime now) noexcept -> void
{
  const auto phys_dt = CanonicalDuration { duration_cast<nanoseconds>(
    static_cast<steady_clock::time_point>(now)
    - static_cast<steady_clock::time_point>(last_frame_time_)) };

  // Update last frame time immediately
  last_frame_time_ = now;

  // Advance simulation with physical delta
  simulation_clock_.Advance(phys_dt);

  // Execute fixed steps and get alpha
  const auto step = simulation_clock_.ExecuteFixedSteps(kMaxFixedStepsPerFrame);
  presentation_clock_.SetInterpolationAlpha(step.interpolation_alpha);

  // Update snapshot
  frame_data_.physical_delta = phys_dt;
  frame_data_.simulation_delta = simulation_clock_.DeltaTime();
  frame_data_.fixed_steps_executed = step.steps_executed;
  frame_data_.interpolation_alpha = step.interpolation_alpha;

  // 1) Update the instantaneous and averaged metrics for internal history
  const auto ns = static_cast<nanoseconds>(phys_dt).count();
  const double instantaneous_fps = ns > 0 ? 1e9 / static_cast<double>(ns) : 0.0;

  // 2) Update the Stable FPS Display (Sample-and-Hold)
  // This updates every 500ms to provide a rock-solid display value that
  // doesn't flicker, matching the behavior of NVIDIA/Steam overlays.
  fps_accumulator_ += static_cast<nanoseconds>(phys_dt);
  fps_frame_count_++;

  if (fps_accumulator_ >= 500ms) {
    stable_fps_ = (1e9 * fps_frame_count_)
      / static_cast<double>(fps_accumulator_.count());
    fps_accumulator_ = 0ns;
    fps_frame_count_ = 0;
  }

  // On the very first frames, use instantaneous so we don't show 0.0
  if (frame_counter_ < 10 && stable_fps_ == 0.0) {
    frame_data_.current_fps = instantaneous_fps;
  } else {
    frame_data_.current_fps = stable_fps_;
  }
}

auto TimeManager::EndFrame() noexcept -> void
{
  // Record frame time
  frame_times_[perf_index_] = frame_data_.physical_delta;
  perf_index_ = (perf_index_ + 1) % kPerfHistory;
  ++frame_counter_;
}

auto TimeManager::GetPerformanceMetrics() const noexcept -> PerformanceMetrics
{
  PerformanceMetrics m {};

  // Compute average and max over history window actually filled
  const auto count = std::min<uint64_t>(frame_counter_, kPerfHistory);
  if (count == 0) {
    return m;
  }

  nanoseconds sum { 0 };
  nanoseconds max_v { 0 };
  for (size_t i = 0; i < count; ++i) {
    const auto ns = static_cast<nanoseconds>(frame_times_[i]);
    sum += ns;
    max_v = std::max(max_v, ns);
  }

  const auto avg = sum / static_cast<int64_t>(count);
  m.average_frame_time = CanonicalDuration { avg };
  m.max_frame_time = CanonicalDuration { max_v };
  m.average_fps
    = avg.count() > 0 ? 1e9 / static_cast<double>(avg.count()) : 0.0;
  m.total_frames = frame_counter_;
  m.simulation_time_debt = CanonicalDuration {};
  return m;
}

} // namespace oxygen::time
