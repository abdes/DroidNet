//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen::time {

class SimulationClock;

//! Presentation clock providing interpolated time and scaled deltas.
/*! Brief facade over SimulationClock for presentation layer needs.

  - Returns a presentation time aligned with simulation time. Use
    presentation::Interpolate(previous, current, alpha) for explicit
    interpolation when you have both timestamps.
  - Scales simulation delta time by an animation scale (configurable) for
    UI/animation timing.

  ### Usage Recipes

  1) Fixed-step simulation with interpolated rendering

  ```cpp
  // Update phase
  auto step = sim_clock.ExecuteFixedSteps(8); // max 8 steps/frame
  pres_clock.SetInterpolationAlpha(step.interpolation_alpha);

  // Render phase
  const auto prev_sim = frame_state.prev_sim_time; // stored per frame
  const auto curr_sim = sim_clock.Now();
  const auto t = oxygen::time::presentation::Interpolate(
    prev_sim, curr_sim, pres_clock.GetInterpolationAlpha());

  // Use scaled delta for animation/UI tweening
  animations.Update(pres_clock.DeltaTime());

  frame_state.prev_sim_time = curr_sim;
  ```

  2) Animation/UI speed control

  ```cpp
  pres_clock.SetAnimationScale(0.5); // half-speed UI
  pres_clock.SetAnimationScale(2.0); // double-speed UI
  auto dt = pres_clock.DeltaTime();  // already scaled
  ```

  3) Pause behavior

  - When SimulationClock is paused, DeltaTime() becomes zero; presentation
    animations stop. If UI should continue, drive those animations from the
    physical clock or a separate UI clock instead of PresentationClock.

  ### Semantics

  - Now(): returns the current simulation time as a PresentationTime. This
    class does not store previous time internally. Use the free function
    presentation::Interpolate(previous, current, alpha) for explicit, clear
    interpolation.
  - DeltaTime(): equals SimulationClock::DeltaTime() scaled by
    GetAnimationScale().
  - Interpolation alpha is stored via SetInterpolationAlpha() and read via
    GetInterpolationAlpha(); callers typically pass it to the interpolation
    helper.

  ### Performance Characteristics

  - Time Complexity: O(1) per call; no allocations.
  - Memory: trivial; holds an observer_ptr and two doubles.
  - Optimization: strong types compile to raw chrono types, zero overhead.

  @see SimulationClock, oxygen::time::presentation::Interpolate
  @warning Do not mix timing domains; use the provided conversion helpers.
  @note Keep interpolation explicit to avoid hidden coupling to frame state.
*/
class PresentationClock {
public:
  OXGN_CORE_API explicit PresentationClock(
    const SimulationClock& sim_clock, double animation_scale = 1.0) noexcept;

  OXYGEN_MAKE_NON_COPYABLE(PresentationClock)
  OXYGEN_MAKE_NON_MOVABLE(PresentationClock)

  ~PresentationClock() noexcept = default;

  //! Interpolated presentation time within the next simulation step.
  OXGN_CORE_NDAPI auto Now() const noexcept -> PresentationTime;
  //! Scaled delta time derived from SimulationClock::DeltaTime().
  OXGN_CORE_NDAPI auto DeltaTime() const noexcept -> CanonicalDuration;

  OXGN_CORE_API auto SetInterpolationAlpha(double alpha) noexcept -> void;
  OXGN_CORE_NDAPI auto GetInterpolationAlpha() const noexcept -> double;

  //! Update the animation scale (clamped to [0, +inf)).
  OXGN_CORE_API auto SetAnimationScale(double scale) noexcept -> void;
  OXGN_CORE_NDAPI auto GetAnimationScale() const noexcept -> double;

private:
  observer_ptr<const SimulationClock> simulation_clock_;
  double animation_scale_;
  double interpolation_alpha_ { 0.0 };
};

namespace presentation {
  //! Interpolate simulation time for presentation sampling.
  /*!
   Renderer-facing utility that computes an in-between presentation time
   within a fixed-step simulation interval. This is intentionally a free
   function instead of implicit state inside PresentationClock so that the
   renderer (or TimeManager) stays in control of which two time stamps are
   used, making frame boundaries explicit and testable.

   @param previous Simulation time at the start of the current frame's
                   render interval. Typically, the simulation time returned at
                   the end of the previous frame.
   @param current  Simulation time at the end of the current frame's update
                   phase (after ExecuteFixedSteps). Must be >= previous.
   @param alpha    Normalized interpolation factor in [0, 1]. Usually taken
                   from SimulationClock::FixedStepResult::interpolation_alpha
                   and optionally clamped by the caller.
   @return Presentation time positioned between previous and current based on
           alpha, expressed as PresentationTime (strongly-typed steady_clock
           time_point at nanosecond precision).

   ### Rationale

   - Keeps interpolation explicit at the call site where both timestamps are
     naturally available (renderer/TimeManager), avoiding hidden mutable state
     in PresentationClock.
   - Preserves domain separation: inputs are SimulationTime, output is
     PresentationTime, preventing accidental domain mixing.
   - Encourages deterministic, testable render sampling without storing
     frame history in the clock.

   ### Usage (Renderer)

   ```cpp
   // Update phase
   auto step = sim_clock.ExecuteFixedSteps(8); // max 8 steps/frame
   pres_clock.SetInterpolationAlpha(step.interpolation_alpha);

   // Render phase
   const auto prev_sim = frame_state.prev_sim_time;  // stored per frame
   const auto curr_sim = sim_clock.Now();
   const auto t = oxygen::time::presentation::Interpolate(
     prev_sim, curr_sim, pres_clock.GetInterpolationAlpha());

   // Drive visuals
   animations.Update(pres_clock.DeltaTime());  // scaled delta
   systems.Render(t);                          // sample at interpolated time

   frame_state.prev_sim_time = curr_sim;
   ```

   ### Edge Cases

   - If previous == current or alpha == 0, returns previous (i.e., current).
   - If alpha == 1, returns current exactly.
   - Callers should ensure previous <= current and alpha ∈ [0, 1].

   ### Performance Characteristics

   - Time Complexity: O(1).
   - Memory: none; no allocations.
   - Optimization: operates on the underlying nanosecond counts and constructs
     a strongly-typed PresentationTime; compiles down to simple arithmetic.

   @see oxygen::time::PresentationClock, oxygen::time::SimulationClock
   @note Use SetInterpolationAlpha on PresentationClock to store alpha per
         frame, then pass it here for clarity and testability.
  */
  OXGN_CORE_NDAPI auto Interpolate(SimulationTime previous,
    SimulationTime current, double alpha) noexcept -> PresentationTime;

  //! Smoothstep-like easing: slow start/end, fast mid-range.
  OXGN_CORE_NDAPI auto EaseInOut(double t) noexcept -> double;
  //! Quadratic ease-in: slow start, accelerates toward 1.
  OXGN_CORE_NDAPI auto EaseIn(double t) noexcept -> double;
  //! Quadratic ease-out: fast start, slows down approaching 1.
  OXGN_CORE_NDAPI auto EaseOut(double t) noexcept -> double;
} // namespace presentation

} // namespace oxygen::time
