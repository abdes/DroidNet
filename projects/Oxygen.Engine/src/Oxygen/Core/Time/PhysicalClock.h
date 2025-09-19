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

//! Monotonic physical clock for engine infrastructure and pacing.
/*!
 Thin wrapper over std::chrono::steady_clock providing strongly-typed physical
 time points and utilities commonly needed by the engine core.

 ### Design Rationale

 - Uses steady_clock for monotonic behavior (no wall-clock jumps).
 - Minimal API: query current time, uptime since construction, and compute
   elapsed time since a prior physical timestamp.
 - Strong types prevent domain mixing at compile time.

 ### Usage Recipes

 1) Frame delta measurement

 ```cpp
 const auto frame_start = physical_clock.Now();
 // ... do work ...
 const auto frame_dt = physical_clock.Since(frame_start);
 ```

 2) Deadline-based frame pacing

 ```cpp
 const auto period = CanonicalDuration{std::chrono::nanoseconds(16'666'667)};
 const auto begin = physical_clock.Now();
 // ... simulate & render ...
 const auto elapsed = physical_clock.Since(begin);
 if (elapsed.get() < period.get()) {
   // sleep for (period - elapsed) using platform sleep
 }
 ```

 3) Uptime for profiling/logging

 ```cpp
 const auto uptime = physical_clock.Uptime();
 ```

 ### Performance Characteristics

 - O(1) per call; no allocations.
 - Strong types compile to raw chrono operations.

 @see SimulationClock, NetworkClock
*/
class PhysicalClock {
public:
  OXGN_CORE_API PhysicalClock() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(PhysicalClock)
  OXYGEN_MAKE_NON_MOVABLE(PhysicalClock)

  ~PhysicalClock() noexcept = default;

  OXGN_CORE_NDAPI auto Now() const noexcept -> PhysicalTime;

  OXGN_CORE_NDAPI auto Uptime() const noexcept -> CanonicalDuration;

  //! Elapsed physical time since a prior physical timestamp.
  OXGN_CORE_NDAPI auto Since(PhysicalTime then) const noexcept
    -> CanonicalDuration;

private:
  const PhysicalTime start_time_ {};
};

} // namespace oxygen::time
