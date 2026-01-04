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
#include <Oxygen/Core/Time/Types.h>

namespace oxygen {

//! Fixed timestep timing configuration for deterministic simulation
struct TimingConfig {
  //! Fixed timestep delta time for physics and deterministic systems
  /*!
   Standard fixed timestep interval, typically 16.67ms (60Hz).
   Used for physics simulation, networking, and other systems requiring
   deterministic behavior regardless of frame rate.
  */
  time::CanonicalDuration fixed_delta { // 60Hz default}
    time::CanonicalDuration::UnderlyingType(16'666'667)
  };

  //! Maximum accumulated time before clamping to prevent spiral of death
  /*!
   When frame rate drops severely, this prevents the engine from trying
   to catch up with too many fixed timestep iterations, which would make
   the problem worse. Typically 2-3x the fixed_delta.
  */
  time::CanonicalDuration max_accumulator { // ~3 frames worth
    time::CanonicalDuration::UnderlyingType(50'000)
  };

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
  std::chrono::microseconds pacing_safety_margin { 200 }; // use chrono here
};

struct EngineConfig {
  // Maximum allowed target FPS for runtime configuration.
  // Use 0 for uncapped frame rate. Values above this will be clamped by
  // AsyncEngine::SetTargetFps.
  static constexpr uint32_t kMaxTargetFps = 240u;
  struct {
    std::string name;
    uint32_t version;
  } application;

  uint32_t target_fps { 0 }; //!< 0 = uncapped
  uint32_t frame_count { 0 }; //!< 0 = unlimited

  //! When true, AsyncEngine will construct the shared AssetLoader service.
  bool enable_asset_loader { false }; //!< Default false for test suites.

  //! Configuration for the AssetLoader service when enabled.
  struct AssetLoaderServiceConfig {
    //! Enable hash-based content integrity verification during mounts.
    bool verify_content_hashes { false };
  } asset_loader;

  GraphicsConfig graphics; //!< Graphics configuration.
  TimingConfig timing; //!< Frame timing and fixed timestep configuration.
};

} // namespace oxygen
