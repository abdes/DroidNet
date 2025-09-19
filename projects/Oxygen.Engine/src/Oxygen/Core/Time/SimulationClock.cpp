//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Core/Time/SimulationClock.h>

namespace oxygen::time {

SimulationClock::SimulationClock(CanonicalDuration fixed_timestep) noexcept
  : fixed_timestep_(fixed_timestep)
{
}

auto SimulationClock::Now() const noexcept -> SimulationTime
{
  return current_time_;
}

auto SimulationClock::DeltaTime() const noexcept -> CanonicalDuration
{
  return last_delta_;
}

auto SimulationClock::SetPaused(bool paused) noexcept -> void
{
  is_paused_ = paused;
}

auto SimulationClock::IsPaused() const noexcept -> bool { return is_paused_; }

auto SimulationClock::SetTimeScale(double scale) noexcept -> void
{
  if (scale < 0.0) {
    return; // ignore invalid
  }
  time_scale_ = scale;
}

auto SimulationClock::GetTimeScale() const noexcept -> double
{
  return time_scale_;
}

auto SimulationClock::GetFixedTimestep() const noexcept -> CanonicalDuration
{
  return fixed_timestep_;
}

auto SimulationClock::Advance(CanonicalDuration physical_elapsed) noexcept
  -> void
{
  if (is_paused_) {
    last_delta_ = CanonicalDuration {};
    return;
  }

  // scale the incoming physical elapsed time (work with underlying ns)
  const auto physical_ns = physical_elapsed.get();
  const auto scaled_count
    = static_cast<long long>(physical_ns.count() * time_scale_);
  const auto sd
    = CanonicalDuration { CanonicalDuration::UnderlyingType(scaled_count) };
  accumulated_time_ = CanonicalDuration { CanonicalDuration::UnderlyingType(
    accumulated_time_.get().count() + sd.get().count()) };
  last_delta_ = sd;
}

auto SimulationClock::ExecuteFixedSteps(uint32_t max_steps) noexcept
  -> FixedStepResult
{
  FixedStepResult result;
  if (is_paused_) {
    result.interpolation_alpha = 0.0;
    result.remaining_time = accumulated_time_;
    return result;
  }

  uint32_t steps = 0u;
  while (
    steps < max_steps && accumulated_time_.get() >= fixed_timestep_.get()) {
    // advance simulation time by fixed_timestep using strong types
    auto cur_tp = current_time_.get();
    cur_tp += fixed_timestep_.get();
    current_time_ = SimulationTime { cur_tp };

    accumulated_time_ -= CanonicalDuration { fixed_timestep_.get() };
    ++steps;
  }

  result.steps_executed = steps;
  // interpolation alpha = remaining / fixed_timestep (clamped 0..1)
  const double alpha = fixed_timestep_.get().count() > 0
    ? static_cast<double>(accumulated_time_.get().count())
      / static_cast<double>(fixed_timestep_.get().count())
    : 0.0;
  result.interpolation_alpha = std::clamp(alpha, 0.0, 1.0);
  result.remaining_time = accumulated_time_;
  return result;
}

} // namespace oxygen::time
