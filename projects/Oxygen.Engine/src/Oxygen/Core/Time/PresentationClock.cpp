//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Core/Time/PresentationClock.h>
#include <Oxygen/Core/Time/SimulationClock.h>

using namespace std::chrono;

namespace oxygen::time {

PresentationClock::PresentationClock(
  const SimulationClock& sim_clock, double animation_scale) noexcept
  : simulation_clock_ { observer_ptr(&sim_clock) }
  , animation_scale_ { animation_scale }
{
}

auto PresentationClock::Now() const noexcept -> PresentationTime
{
  // Interpolate between current simulation time and the next step using alpha.
  // Without explicit previous frame time storage in SimulationClock, we use
  // the current simulation time as both ends which effectively returns the
  // current time. This keeps the API stable; TimeManager can feed proper
  // previous/current to presentation::Interpolate if desired.
  const auto current_sim = simulation_clock_->Now();
  // Fallback: no stored previous time in this class, so return current.
  return PresentationTime { current_sim.get() };
}

auto PresentationClock::DeltaTime() const noexcept -> CanonicalDuration
{
  return CanonicalDuration { nanoseconds(static_cast<long long>(
    simulation_clock_->DeltaTime().get().count() * animation_scale_)) };
}

auto PresentationClock::SetInterpolationAlpha(double alpha) noexcept -> void
{
  if (alpha < 0.0) {
    interpolation_alpha_ = 0.0;
  } else if (alpha > 1.0) {
    interpolation_alpha_ = 1.0;
  } else {
    interpolation_alpha_ = alpha;
  }
}

auto PresentationClock::GetInterpolationAlpha() const noexcept -> double
{
  return interpolation_alpha_;
}

auto PresentationClock::SetAnimationScale(double scale) noexcept -> void
{
  if (scale < 0.0) {
    animation_scale_ = 0.0;
  } else {
    animation_scale_ = scale;
  }
}

auto PresentationClock::GetAnimationScale() const noexcept -> double
{
  return animation_scale_;
}

namespace presentation {

  auto Interpolate(SimulationTime previous, SimulationTime current,
    double alpha) noexcept -> PresentationTime
  {
    // Linear interpolation on time_since_epoch counts
    const auto prev_ns = previous.get().time_since_epoch().count();
    const auto cur_ns = current.get().time_since_epoch().count();
    const double interp = static_cast<double>(prev_ns)
      + (static_cast<double>(cur_ns) - static_cast<double>(prev_ns)) * alpha;
    const auto result_ns = nanoseconds(static_cast<long long>(interp));
    return PresentationTime { time_point<steady_clock, nanoseconds>(
      result_ns) };
  }

  auto EaseInOut(double t) noexcept -> double
  {
    /*! Smoothstep-like easing function.

     @param t Normalized value in [0, 1]. Values outside are not clamped.
     @return Eased value in [0, 1] with zero slope at endpoints.

     ### Behavior
     - Monotonic on [0, 1]
     - Derivative 0 at t=0 and t=1 (smooth start/end)
     - Suitable for camera moves, crossfades, and tick interpolation
    */
    if (t <= 0.0) {
      return 0.0;
    }
    if (t >= 1.0) {
      return 1.0;
    }
    return t * t * (3.0 - 2.0 * t);
  }

  auto EaseIn(double t) noexcept -> double
  {
    /*! Quadratic ease-in curve.

     @param t Normalized value in [0, 1].
     @return t^2, emphasizing slow start and faster finish.

     ### Usage
     - Use when you want a "wind-up" feel (e.g., accelerating animation)
    */
    return t * t;
  }

  auto EaseOut(double t) noexcept -> double
  {
    /*! Quadratic ease-out curve.

     @param t Normalized value in [0, 1].
     @return 1 - (1 - t)^2, emphasizing fast start and gentle finish.

     ### Usage
     - Use for "settle into place" effects and UI deceleration
    */
    return 1.0 - (1.0 - t) * (1.0 - t);
  }

} // namespace presentation

} // namespace oxygen::time
