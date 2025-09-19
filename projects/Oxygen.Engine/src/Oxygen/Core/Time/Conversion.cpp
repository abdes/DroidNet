//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>

#include <Oxygen/Core/Time/Conversion.h>

using namespace std::chrono;

namespace oxygen::time::convert {

auto ToWallClock(PhysicalTime physical, const AuditClock& audit_clock) noexcept
  -> AuditTime
{
  return audit_clock.ToWallClock(physical);
}

auto FromWallClock(AuditTime wall, const AuditClock& audit_clock) noexcept
  -> PhysicalTime
{
  return audit_clock.FromWallClock(wall);
}

auto ToPresentation(SimulationTime sim_time,
  const PresentationClock& /*pres_clock*/) noexcept -> PresentationTime
{
  // Straight tag-cast preserving steady_clock epoch; explicit interpolation is
  // performed elsewhere via presentation::Interpolate
  return PresentationTime { static_cast<time_point<steady_clock, nanoseconds>>(
    sim_time) };
}

auto NetworkToLocal(NetworkTime network_time,
  const NetworkClock& network_clock) noexcept -> NetworkConversionResult
{
  NetworkConversionResult r {};
  r.local_time = network_clock.RemoteToLocal(network_time);
  // Simple heuristic: uncertainty proportional to RTT and (1 - confidence)
  const auto rtt = network_clock.GetRoundTripTime();
  const auto conf = network_clock.GetOffsetConfidence();
  const double factor = std::clamp(1.0 - conf, 0.0, 1.0);
  r.uncertainty = CanonicalDuration { nanoseconds(
    static_cast<long long>(static_cast<nanoseconds>(rtt).count() * factor)) };
  r.is_reliable = conf >= 0.5; // arbitrary threshold
  return r;
}

auto TimelineToSimulation(TimelineTime timeline_time) noexcept -> SimulationTime
{
  // Placeholder until TimelineClock is introduced: preserve tag and epoch.
  return SimulationTime { static_cast<time_point<steady_clock, nanoseconds>>(
    timeline_time) };
}

auto SimulationToTimeline(SimulationTime sim_time) noexcept -> TimelineTime
{
  return TimelineTime { static_cast<time_point<steady_clock, nanoseconds>>(
    sim_time) };
}

} // namespace oxygen::time::convert
