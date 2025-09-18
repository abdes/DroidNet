//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include <Oxygen/Config/GraphicsConfig.h>

namespace oxygen {

//! Fixed timestep timing configuration for deterministic simulation
struct TimingConfig {
  //! Fixed timestep delta time for physics and deterministic systems
  /*!
   Standard fixed timestep interval, typically 16.67ms (60Hz).
   Used for physics simulation, networking, and other systems requiring
   deterministic behavior regardless of frame rate.
  */
  std::chrono::microseconds fixed_delta { 16667 }; // 60Hz default

  //! Maximum accumulated time before clamping to prevent spiral of death
  /*!
   When frame rate drops severely, this prevents the engine from trying
   to catch up with too many fixed timestep iterations, which would make
   the problem worse. Typically 2-3x the fixed_delta.
  */
  std::chrono::microseconds max_accumulator { 50000 }; // ~3 frames worth

  //! Maximum fixed timestep iterations per frame
  /*!
   Hard limit on substeps to prevent infinite loops during severe frame
   drops. When this limit is reached, simulation time will run slower
   than real time rather than locking up the engine.
  */
  uint32_t max_substeps { 4 };

  //! Safety margin before frame pacing deadline
  /*!
   The engine sleeps until (deadline - safety_margin), then uses cooperative
   yielding to finish. This compensates for OS sleep jitter/overshoot.
   Tune per platform; typical values 150–300 microseconds.
  */
  std::chrono::microseconds pacing_safety_margin { 200 }; // µs
};

struct EngineConfig {
  struct {
    std::string name;
    uint32_t version;
  } application;

  uint32_t target_fps { 0 }; //!< 0 = uncapped
  uint32_t frame_count { 0 }; //!< 0 = unlimited

  GraphicsConfig graphics; //!< Graphics configuration.
  TimingConfig timing; //!< Frame timing and fixed timestep configuration.
};

} // namespace oxygen
