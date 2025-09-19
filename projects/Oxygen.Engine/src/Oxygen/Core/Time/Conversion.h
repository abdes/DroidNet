//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Time/AuditClock.h>
#include <Oxygen/Core/Time/NetworkClock.h>
#include <Oxygen/Core/Time/PresentationClock.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen::time::convert {

// Physical <-> Audit (wall clock)
OXGN_CORE_NDAPI auto ToWallClock(
  PhysicalTime physical, const AuditClock& audit_clock) noexcept -> AuditTime;

OXGN_CORE_NDAPI auto FromWallClock(
  AuditTime wall, const AuditClock& audit_clock) noexcept -> PhysicalTime;

// Simulation -> Presentation (explicit sampling)
OXGN_CORE_NDAPI auto ToPresentation(SimulationTime sim_time,
  const PresentationClock& /*pres_clock*/) noexcept -> PresentationTime;

// Network conversions with explicit uncertainty information
struct NetworkConversionResult {
  PhysicalTime local_time {};
  CanonicalDuration uncertainty {};
  bool is_reliable { true };
};

OXGN_CORE_NDAPI auto NetworkToLocal(NetworkTime network_time,
  const NetworkClock& network_clock) noexcept -> NetworkConversionResult;

// Timeline <-> Simulation (deterministic mode only)
// Not implemented yet (TimelineClock pending); provide simple passthrough
// declarations to keep API stable for now.
OXGN_CORE_NDAPI auto TimelineToSimulation(TimelineTime timeline_time) noexcept
  -> SimulationTime;

OXGN_CORE_NDAPI auto SimulationToTimeline(SimulationTime sim_time) noexcept
  -> TimelineTime;

} // namespace oxygen::time::convert
