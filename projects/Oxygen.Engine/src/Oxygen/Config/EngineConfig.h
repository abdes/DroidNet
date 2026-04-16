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
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Time/Types.h>

namespace oxygen {

//! Runtime-selected physics backend policy.
using EnginePhysicsBackend = core::meta::physics::PhysicsBackend;

//! Runtime-selected renderer module implementation.
enum class RendererImplementation : uint8_t {
  kLegacy,
  kVortex,
};

//! Fixed timestep timing configuration for deterministic simulation
struct TimingConfig {
  //! Fixed timestep delta time for physics and deterministic systems
  /*!
   Standard fixed timestep interval, typically 16.67ms (60Hz).
   Used for physics simulation, networking, and other systems requiring
   deterministic behavior regardless of frame rate.
  */
  time::CanonicalDuration fixed_delta { // NOLINTNEXTLINE(*-magic-numbers)
    time::CanonicalDuration::UnderlyingType(16'666'667)
  };

  //! Maximum accumulated time before clamping to prevent spiral of death
  /*!
   When frame rate drops severely, this prevents the engine from trying
   to catch up with too many fixed timestep iterations, which would make
   the problem worse. Typically 2-3x the fixed_delta.
  */
  time::CanonicalDuration max_accumulator { // NOLINTNEXTLINE(*-magic-numbers)
    time::CanonicalDuration::UnderlyingType(50'000'000)
  };

  //! Maximum fixed timestep iterations per frame
  /*!
   Hard limit on substeps to prevent infinite loops during severe frame
   drops. When this limit is reached, simulation time will run slower
   than real time rather than locking up the engine.
  */
  uint32_t max_substeps { 4 }; // NOLINT(*-magic-numbers)

  //! Safety margin before frame pacing deadline
  /*!
   The engine sleeps until (deadline - safety_margin), then uses cooperative
   yielding to finish. This compensates for OS sleep jitter/overshoot.
   Tune per platform; typical values 150–300 microseconds.
  */
  // NOLINTNEXTLINE(*-magic-numbers)
  std::chrono::microseconds pacing_safety_margin { 200 }; // use chrono here

  //! Cooperative sleep used when the frame loop is uncapped
  /*!
   Uncapped mode should still relinquish the main thread briefly so worker
   threads, OS message pumping, and platform services can make forward
   progress. Keep this small enough to avoid acting like a real frame cap.
  */
  std::chrono::milliseconds uncapped_cooperative_sleep { 1 }; // NOLINT
};

struct EngineConfig {
  //! Maximum allowed target FPS for runtime configuration.
  //! Use 0 for uncapped frame rate. Values above this will be clamped by
  //! AsyncEngine::SetTargetFps.
  static constexpr uint32_t kMaxTargetFps = 240U;

  //! Configuration for renderer module selection only.
  struct RendererModuleConfig {
    RendererImplementation implementation { RendererImplementation::kLegacy };
  } renderer;

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

  //! Configuration for the Physics module/backend selection.
  struct PhysicsConfig {
    //! Backend requested by runtime configuration.
    EnginePhysicsBackend backend { EnginePhysicsBackend::kJolt };
  } physics;

  //! Configuration for the Scripting module.
  struct ScriptingConfig {
    //! Enables asynchronous polling of script source directories for changes.
    //! If true, changes to .lua/.luau files in any registered ScriptSourceRoot
    //! (in PathFinderConfig) will trigger an automatic reload and recompile.
    bool enable_hot_reload { true };

    //! Frequency at which source directories are scanned for changes.
    std::chrono::milliseconds hot_reload_poll_interval { 100 }; // NOLINT

    //! Rules for Script Source Management:
    //! 1. Bytecode Cache: Compiled scripts are stored in a single binary file
    //!    defined by 'ScriptBytecodeCachePath'. This is independent of source.
    //! 2. Source Roots: A list of directories (Engine, Game, Addons) searched
    //!    to resolve script files.
    //! 3. Resolution: When an asset points to 'scripts/my_script.lua', the
    //!    engine searches all roots in order. The first match wins.
    //! 4. Hot Reload: The watcher maps absolute file changes back to assets
    //!     by finding which root contains the file and matching the relative
    //!     path against asset metadata.
  } scripting;

  //! Global engine path resolution config (workspace-root aware).
  PathFinderConfig path_finder_config;

  GraphicsConfig graphics; //!< Graphics configuration.
  TimingConfig timing; //!< Frame timing and fixed timestep configuration.
};

} // namespace oxygen
