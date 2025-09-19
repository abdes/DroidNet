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

//! Wall-clock (system_clock) access and conversions for auditing/logging.
/*!
 Thin, strongly-typed wrapper around std::chrono::system_clock for
 producing human-meaningful timestamps and converting between the engine's
 monotonic physical clock and wall-clock time.

 ### Design Rationale

 - Keep wall-clock access isolated to the Audit domain; simulation and
   rendering should not depend on wall time.
 - Provide explicit conversions to/from PhysicalTime using a fixed offset
   captured at construction (system_now - steady_now). This is simple,
   fast, and adequate for logging and analytics.
 - Strong types prevent domain mixing at compile time.

 ### Usage Recipes

 1) Emit a log timestamp

 ```cpp
 const auto ts = audit_clock.Now();
 // format ts with your logging/telemetry layer
 ```

 2) Convert a captured PhysicalTime to wall clock for logs

 ```cpp
 const auto begin_phys = physical_clock.Now();
 // ... do work ...
 const auto begin_wall = audit_clock.ToWallClock(begin_phys);
 ```

 3) Convert a known wall-clock time into the engine's physical domain
 ```cpp
 const auto wall_time = ...; // parsed from a log or external source

 const auto approx_phys = audit_clock.FromWallClock(wall_time);
 ```

 ## #Semantics& Caveats

 - Now()
 : returns current wall - clock time(system_clock).
 - Conversions use a fixed offset computed at construction; subsequent
   system clock adjustments (NTP, DST changes) are not reflected in the
   offset. This is typically acceptable for logging/analytics.
 - If your application must reflect live wall-clock adjustments in
   conversions, prefer recomputing the offset on demand or constructing a
   fresh AuditClock instance.

 ### Performance Characteristics

 - O(1) per call;
 - no allocations.
 - Strong types compile to raw chrono operations.

  @see PhysicalClock
*/
class AuditClock {
public:
  OXGN_CORE_API AuditClock() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(AuditClock)
  OXYGEN_MAKE_NON_MOVABLE(AuditClock)

  ~AuditClock() noexcept = default;

  //! Current wall-clock timestamp (system_clock, domain-typed).
  OXGN_CORE_NDAPI auto Now() const noexcept -> AuditTime;

  //! Convert a physical (steady) time point to wall-clock using the fixed
  //! offset captured at construction.
  OXGN_CORE_NDAPI auto ToWallClock(PhysicalTime physical) const noexcept
    -> AuditTime;

  //! Convert a wall-clock time point to an approximate physical time using
  //! the fixed offset captured at construction.
  OXGN_CORE_NDAPI auto FromWallClock(AuditTime wall) const noexcept
    -> PhysicalTime;

private:
  // offset = system_now - steady_now
  const CanonicalDuration offset_ {};
};

} // namespace oxygen::time
