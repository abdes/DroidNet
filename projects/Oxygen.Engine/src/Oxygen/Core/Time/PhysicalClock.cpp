//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Core/Time/PhysicalClock.h>

using namespace std::chrono;

namespace oxygen::time {

PhysicalClock::PhysicalClock() noexcept
  : start_time_ { time_point_cast<nanoseconds>(steady_clock::now()) }
{
}

// ReSharper disable once CppMemberFunctionMayBeStatic
auto PhysicalClock::Now() const noexcept -> PhysicalTime
{
  return PhysicalTime { time_point_cast<nanoseconds>(steady_clock::now()) };
}

auto PhysicalClock::Uptime() const noexcept -> CanonicalDuration
{
  const auto now = time_point_cast<nanoseconds>(steady_clock::now());
  const auto start_ns = start_time_.get().time_since_epoch();
  return CanonicalDuration { duration_cast<nanoseconds>(
    now.time_since_epoch() - start_ns) };
}

auto PhysicalClock::Since(PhysicalTime then) const noexcept -> CanonicalDuration
{
  const auto now = Now().get().time_since_epoch();
  const auto tns = then.get().time_since_epoch();
  return CanonicalDuration { duration_cast<nanoseconds>(now - tns) };
}

} // namespace oxygen::time
