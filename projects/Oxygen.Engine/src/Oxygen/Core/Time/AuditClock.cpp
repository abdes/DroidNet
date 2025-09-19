//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>

#include <Oxygen/Core/Time/AuditClock.h>

using namespace std::chrono;

namespace oxygen::time {

AuditClock::AuditClock() noexcept
  : offset_ { duration_cast<nanoseconds>(
      time_point_cast<nanoseconds>(system_clock::now()).time_since_epoch()
      - std::chrono::time_point_cast<nanoseconds>(steady_clock::now())
        .time_since_epoch()) }
{
}

// ReSharper disable once CppMemberFunctionMayBeStatic
auto AuditClock::Now() const noexcept -> AuditTime
{
  return AuditTime { time_point_cast<nanoseconds>(system_clock::now()) };
}

auto AuditClock::ToWallClock(PhysicalTime physical) const noexcept -> AuditTime
{
  const auto steady_since_epoch = physical.get().time_since_epoch();
  const auto sys_ns
    = nanoseconds(steady_since_epoch.count() + offset_.get().count());
  const auto sys_time = time_point<system_clock, nanoseconds>(sys_ns);
  return AuditTime { sys_time };
}

auto AuditClock::FromWallClock(AuditTime wall) const noexcept -> PhysicalTime
{
  const auto sys_since_epoch = wall.get().time_since_epoch();
  const auto steady_ns
    = nanoseconds(sys_since_epoch.count() - offset_.get().count());
  return PhysicalTime { time_point<steady_clock, nanoseconds>(steady_ns) };
}

} // namespace oxygen::time
