//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>

#include <Oxygen/Core/Time/NetworkClock.h>

using namespace std::chrono;

namespace oxygen::time {

NetworkClock::NetworkClock() noexcept = default;

auto NetworkClock::SetPeerOffset(
  CanonicalDuration offset, double confidence) noexcept -> void
{
  peer_offset_ = offset;
  offset_confidence_ = confidence;
  // store in circular history for smoothing
  offset_history_[offset_history_index_] = offset;
  offset_history_index_ = (offset_history_index_ + 1) % kOffsetHistorySize;
}

auto NetworkClock::GetPeerOffset() const noexcept -> CanonicalDuration
{
  return peer_offset_;
}

auto NetworkClock::GetOffsetConfidence() const noexcept -> double
{
  return offset_confidence_;
}

auto NetworkClock::SetRoundTripTime(CanonicalDuration rtt) noexcept -> void
{
  round_trip_time_ = rtt;
}

auto NetworkClock::GetRoundTripTime() const noexcept -> CanonicalDuration
{
  return round_trip_time_;
}

auto NetworkClock::RemoteToLocal(NetworkTime remote_time) const noexcept
  -> PhysicalTime
{
  // remote_time (steady) + peer_offset => local physical (steady)
  const auto remote_ns = remote_time.get().time_since_epoch();
  const auto local_ns
    = nanoseconds(remote_ns.count() + peer_offset_.get().count());
  return PhysicalTime { PhysicalTime::UnderlyingType(local_ns) };
}

auto NetworkClock::LocalToRemote(PhysicalTime local_time) const noexcept
  -> NetworkTime
{
  const auto local_ns = local_time.get().time_since_epoch();
  const auto remote_ns
    = nanoseconds(local_ns.count() - peer_offset_.get().count());
  return NetworkTime { NetworkTime::UnderlyingType(remote_ns) };
}

auto NetworkClock::PredictRemoteTime(PhysicalTime local_now,
  CanonicalDuration prediction_window) const noexcept -> NetworkTime
{
  // Predict by adding the window to the provided local_now and then mapping
  // to the remote timeline using the current peer offset.
  const auto base_ns = local_now.get().time_since_epoch().count();
  const auto pred_local = steady_clock::now()
    + nanoseconds(base_ns + prediction_window.get().count());
  const auto pred_remote = nanoseconds(
    pred_local.time_since_epoch().count() - peer_offset_.get().count());
  return NetworkTime { NetworkTime::UnderlyingType(pred_remote) };
}

auto NetworkClock::SetSmoothingFactor(double factor) noexcept -> void
{
  factor = std::max(factor, 0.0);
  factor = std::min(factor, 1.0);
  smoothing_factor_ = factor;
}

auto NetworkClock::GetSmoothingFactor() const noexcept -> double
{
  return smoothing_factor_;
}

auto NetworkClock::ProcessSyncEvent(const SyncEvent& event) noexcept -> void
{
  // Basic offset estimate: remote_time corresponds to local_time - rtt/2
  const auto remote_ns = event.remote_time.get().time_since_epoch().count();
  const auto local_ns = event.local_time.get().time_since_epoch().count();
  const auto rtt_half = event.round_trip_time.get().count() / 2;
  const auto estimate = nanoseconds(local_ns - remote_ns - rtt_half);

  // Blend with current peer_offset_ using smoothing factor
  const double factor = smoothing_factor_;
  const auto blended_count = static_cast<long long>(
    peer_offset_.get().count() * (1.0 - factor) + estimate.count() * factor);
  peer_offset_ = CanonicalDuration { nanoseconds(blended_count) };

  // Store the estimate in history
  offset_history_[offset_history_index_] = CanonicalDuration { estimate };
  offset_history_index_ = (offset_history_index_ + 1) % kOffsetHistorySize;

  // Update RTT and confidence
  round_trip_time_ = event.round_trip_time;
  offset_confidence_ = event.confidence;
}

} // namespace oxygen::time
