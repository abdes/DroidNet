//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Engine/api_export.h>

#include <Oxygen/Core/Time/AuditClock.h>
#include <Oxygen/Core/Time/NetworkClock.h>
#include <Oxygen/Core/Time/PhysicalClock.h>
#include <Oxygen/Core/Time/PresentationClock.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Core/Time/Types.h>

namespace oxygen::engine {

//=== TimeManager -----------------------------------------------------------//

//! TimeManager component coordinating all active timing domains.
/*!
 Manages Physical, Simulation, Presentation, and Network clocks, provides
 per-frame integration (BeginFrame/EndFrame), and exposes basic performance
 metrics. This implementation intentionally omits Timeline and Deterministic
 clocks until those are introduced (see design/Time.md Phase 3).

 ### Configuration Philosophy

 - Immutable configuration at construction (RAII style).
 - Runtime controls limited to legitimate state changes (pause/scale).
 - Minimal API surface; explicit types for domain separation.

 ### Frame Flow

 1) BeginFrame():
    - Read physical delta from PhysicalClock
    - Advance SimulationClock (respecting pause/scale)
    - Execute fixed steps and set interpolation alpha on PresentationClock
    - Update frame timing data snapshot

 2) EndFrame():
    - Update performance history and counters

 @see PhysicalClock, SimulationClock, PresentationClock, NetworkClock
*/
class TimeManager final : public Component {
  OXYGEN_COMPONENT(TimeManager)

public:
  //! Construction configuration.
  struct Config {
    // Simulation timing
    time::CanonicalDuration fixed_timestep {
      std::chrono::microseconds(16667),
    }; // ~60Hz
    double default_time_scale { 1.0 };
    bool start_paused { false };

    // Presentation
    double animation_scale { 1.0 };

    // Network
    double network_smoothing_factor { 0.1 };
  };

  //! Construct with a physical clock and configuration.
  OXGN_NGIN_API explicit TimeManager(
    time::PhysicalClock& physical_clock, const Config& config = {}) noexcept;
  OXGN_NGIN_API ~TimeManager() override = default;

  OXYGEN_MAKE_NON_COPYABLE(TimeManager)
  OXYGEN_DEFAULT_MOVABLE(TimeManager)

  // Clock accessors
  [[nodiscard]] auto GetSimulationClock() const noexcept
    -> const time::SimulationClock&
  {
    return simulation_clock_;
  }
  [[nodiscard]] auto GetSimulationClock() noexcept -> time::SimulationClock&
  {
    return simulation_clock_;
  }
  [[nodiscard]] auto GetPresentationClock() const noexcept
    -> const time::PresentationClock&
  {
    return presentation_clock_;
  }
  [[nodiscard]] auto GetPresentationClock() noexcept -> time::PresentationClock&
  {
    return presentation_clock_;
  }
  [[nodiscard]] auto GetNetworkClock() const noexcept
    -> const time::NetworkClock&
  {
    return network_clock_;
  }
  [[nodiscard]] auto GetNetworkClock() noexcept -> time::NetworkClock&
  {
    return network_clock_;
  }
  [[nodiscard]] auto GetAuditClock() const noexcept -> const time::AuditClock&
  {
    return audit_clock_;
  }
  [[nodiscard]] auto GetAuditClock() noexcept -> time::AuditClock&
  {
    return audit_clock_;
  }

  // Frame integration
  OXGN_NGIN_API auto BeginFrame() noexcept -> void;
  OXGN_NGIN_API auto EndFrame() noexcept -> void;

  //! Snapshot of per-frame timing values (updated in BeginFrame).
  struct FrameTimingData {
    time::CanonicalDuration physical_delta {};
    time::CanonicalDuration simulation_delta {};
    uint32_t fixed_steps_executed { 0 };
    double interpolation_alpha { 0.0 };
    double current_fps { 0.0 };
  };

  OXGN_NGIN_NDAPI auto GetFrameTimingData() const noexcept
    -> const FrameTimingData&
  {
    return frame_data_;
  }

  //! Aggregated performance metrics computed in EndFrame and on demand.
  struct PerformanceMetrics {
    time::CanonicalDuration average_frame_time {};
    time::CanonicalDuration max_frame_time {};
    double average_fps { 0.0 };
    uint64_t total_frames { 0 };
    time::CanonicalDuration simulation_time_debt {}; // Not tracked yet
  };

  [[nodiscard]] OXGN_NGIN_API auto GetPerformanceMetrics() const noexcept
    -> PerformanceMetrics;

private:
  // Dependencies
  observer_ptr<time::PhysicalClock> physical_clock_;

  // Clocks
  time::SimulationClock simulation_clock_;
  time::PresentationClock presentation_clock_;
  time::NetworkClock network_clock_;
  time::AuditClock audit_clock_;

  // Frame state
  FrameTimingData frame_data_ {};
  time::PhysicalTime last_frame_time_ {};
  uint64_t frame_counter_ { 0 };

  // Performance history (ring buffer)
  static constexpr size_t kPerfHistory = 120; // ~2s @60fps
  std::array<time::CanonicalDuration, kPerfHistory> frame_times_ {};
  size_t perf_index_ { 0 };

  // Tuning
  static constexpr uint32_t kMaxFixedStepsPerFrame = 8;
};

} // namespace oxygen::engine
