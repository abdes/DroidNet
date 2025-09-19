//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen::time {

//! Fixed timestep simulation clock for consistent gameplay and physics.
/*!
 Provides accumulator-driven fixed-step advancement with an explicit separation
 between physical elapsed time (input) and simulation time (output). The clock
 scales incoming physical time (timescale , pause) and accumulates it to
 execute zero or more fixed simulation steps per frame.

 ### Design Rationale

 - Deterministic fixed-step updates, resilient to frame rate variation.
 - Explicit input via Advance(physical_elapsed); no hidden global clock.
 - Minimal surface: fixed timestep, pause/scale, and a single execution API
   returning steps + interpolation alpha for presentation.

 ### Usage Recipes

 1) Per-frame update loop

 ```cpp
 // Measure physical/frame elapsed (from PhysicalClock or platform timer)
 const auto phys_dt = physical_clock.DeltaSinceLastFrame();

 // Feed elapsed into the simulation clock (applies pause/scale)
 sim_clock.Advance(phys_dt);

 // Execute as many fixed steps as allowed and get interpolation alpha
 auto step = sim_clock.ExecuteFixedSteps( 8); // max 8 steps/frame

 for (uint32_t i = 0; i < step.steps_executed; ++i) {
   // Run your fixed simulation update using sim_clock.GetFixedTimestep()
   FixedUpdate();
 }

 // Presentation: pass alpha to PresentationClock for interpolation
 pres_clock.SetInterpolationAlpha(step.interpolation_alpha);
 ```

 2) Time scaling and pause

 ```cpp
 sim_clock.SetTimeScale(0.5); // slow motion
 sim_clock.SetPaused(true);   // pause (no accumulation, zero delta)
 ```

 ### Semantics

 - Now(): current simulation time (steady, domain-typed).
 - DeltaTime(): last scaled physical delta passed to Advance() (not fixed).
 - ExecuteFixedSteps(): consumes the accumulator in fixed-size quanta, advances
   Now() by GetFixedTimestep() per step, and returns remaining fraction as
   interpolation_alpha ∈ [0, 1].

 ### Performance Characteristics

 - O(steps) per call to ExecuteFixedSteps(), O(1) for others; no allocations.
 - Strong types compile to raw chrono operations, zero overhead.

 @see PresentationClock, oxygen::time::presentation::Interpolate
*/
class SimulationClock {
public:
  OXGN_CORE_API explicit SimulationClock(CanonicalDuration fixed_timestep
    = CanonicalDuration { std::chrono::microseconds(16667) }) noexcept;

  OXYGEN_MAKE_NON_COPYABLE(SimulationClock)
  OXYGEN_MAKE_NON_MOVABLE(SimulationClock)

  ~SimulationClock() noexcept = default;

  //! Current simulation time (monotonic, domain-typed).
  OXGN_CORE_NDAPI auto Now() const noexcept -> SimulationTime;
  //! Last scaled physical delta passed to Advance() (not fixed).
  OXGN_CORE_NDAPI auto DeltaTime() const noexcept -> CanonicalDuration;

  //! Pause/unpause accumulation of physical time.
  OXGN_CORE_API auto SetPaused(bool paused) noexcept -> void;
  OXGN_CORE_NDAPI auto IsPaused() const noexcept -> bool;
  //! Scale incoming physical elapsed time (clamped by caller; negative
  //! ignored).
  OXGN_CORE_API auto SetTimeScale(double scale) noexcept -> void;
  OXGN_CORE_NDAPI auto GetTimeScale() const noexcept -> double;

  OXGN_CORE_NDAPI auto GetFixedTimestep() const noexcept -> CanonicalDuration;

  //! Feed physical elapsed time into the accumulator (applies pause/scale).
  OXGN_CORE_API auto Advance(CanonicalDuration physical_elapsed) noexcept
    -> void;

  struct FixedStepResult {
    //! Number of fixed steps executed this frame.
    uint32_t steps_executed { 0 };
    //! Interpolation alpha ∈ [0, 1] (remaining/ fixed) for presentation.
    double interpolation_alpha { 0.0 };
    //! Remaining accumulated time after consuming fixed steps.
    CanonicalDuration remaining_time {};
  };

  //! Consume accumulator in fixed quanta up to max_steps; return step info.
  OXGN_CORE_NDAPI auto ExecuteFixedSteps(uint32_t max_steps = 10) noexcept
    -> FixedStepResult;

private:
  SimulationTime current_time_ {};
  CanonicalDuration accumulated_time_ {};
  const CanonicalDuration fixed_timestep_;
  bool is_paused_ { false };
  double time_scale_ { 1.0 };
  CanonicalDuration last_delta_ {};
};

} // namespace oxygen::time
