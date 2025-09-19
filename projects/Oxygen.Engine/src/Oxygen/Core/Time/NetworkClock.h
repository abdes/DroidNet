//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/api_export.h>
#include <array>

namespace oxygen::time {

//! Network time synchronization and conversion helper.
/*!
 Provides minimal facilities to track and use the measured offset between local
 physical time and a remote peer's network time. The API keeps domain separation
 explicit and avoids hidden global time queries for determinism and testability.

 ### Design Rationale

 - Explicit offset model: conversions use a tracked peer offset and do not embed
   wall-clock or simulation state into the clock.
 - Minimal API surface: a single SyncEvent processor, conversions both ways, and
   a small set of knobs (smoothing, confidence, RTT).
 - Determinism-friendly: prediction requires a caller-provided local time,
   avoiding hidden calls to now().

 ### Usage Recipes

 1) Processing a sync sample from the network

 ```cpp
 oxygen::time::NetworkClock::SyncEvent ev{
   .local_time = physical_now,
   .remote_time = remote_stamp,
   .round_trip_time = measured_rtt,
   .confidence = 0.8,
 };
 net_clock.ProcessSyncEvent(ev);
 ```

 2) Converting timestamps

 ```cpp
 // Map a remote timestamp to local physical time
 const auto local_est = net_clock.RemoteToLocal(remote_stamp);

 // Map a local physical time to the peer's network timeline
 const auto remote_est = net_clock.LocalToRemote(physical_now);
 ```

 3) Predicting a future remote time

 ```cpp
 // Predict remote time prediction_window into the future from local_now
 const auto remote_pred = net_clock.PredictRemoteTime(
   physical_now, CanonicalDuration{std::chrono::milliseconds(50)});
 ```

 ### Performance Characteristics

 - Time Complexity: O(1) per call; no allocations.
 - Memory: small fixed-size circular history (kOffsetHistorySize).
 - Optimization: strong types compiled down to raw chrono operations.

 @see PhysicalTime, NetworkTime, CanonicalDuration
*/
class NetworkClock {
public:
  OXGN_CORE_API NetworkClock() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(NetworkClock)
  OXYGEN_MAKE_NON_MOVABLE(NetworkClock)

  ~NetworkClock() noexcept = default;

  // Offset management
  OXGN_CORE_NDAPI void SetPeerOffset(
    CanonicalDuration offset, double confidence = 1.0) noexcept;
  OXGN_CORE_NDAPI auto GetPeerOffset() const noexcept -> CanonicalDuration;
  OXGN_CORE_NDAPI auto GetOffsetConfidence() const noexcept -> double;

  // RTT management
  OXGN_CORE_NDAPI void SetRoundTripTime(CanonicalDuration rtt) noexcept;
  OXGN_CORE_NDAPI auto GetRoundTripTime() const noexcept -> CanonicalDuration;

  // Conversions
  OXGN_CORE_NDAPI auto RemoteToLocal(NetworkTime remote_time) const noexcept
    -> PhysicalTime;
  OXGN_CORE_NDAPI auto LocalToRemote(PhysicalTime local_time) const noexcept
    -> NetworkTime;

  // Prediction/support
  //! Predict remote time prediction_window into the future from local_now.
  OXGN_CORE_NDAPI auto PredictRemoteTime(PhysicalTime local_now,
    CanonicalDuration prediction_window) const noexcept -> NetworkTime;

  // Smoothing
  OXGN_CORE_NDAPI void SetSmoothingFactor(double factor) noexcept; // [0.0,1.0]
  OXGN_CORE_NDAPI auto GetSmoothingFactor() const noexcept -> double;

  struct SyncEvent {
    PhysicalTime local_time;
    NetworkTime remote_time;
    CanonicalDuration round_trip_time;
    double confidence;
  };

  OXGN_CORE_NDAPI void ProcessSyncEvent(const SyncEvent& event) noexcept;

private:
  CanonicalDuration peer_offset_ {};
  double offset_confidence_ { 0.0 };
  CanonicalDuration round_trip_time_ {};
  double smoothing_factor_ { 0.1 };

  static constexpr size_t kOffsetHistorySize = 16;
  std::array<CanonicalDuration, kOffsetHistorySize> offset_history_ {};
  size_t offset_history_index_ { 0 };
};

} // namespace oxygen::time
