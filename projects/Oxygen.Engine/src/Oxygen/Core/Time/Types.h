//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::time {

// Strongly-typed duration wrapper
using CanonicalDuration = NamedType<std::chrono::nanoseconds,
  struct CanonicalDurationTag,
  // clang-format off
  DefaultInitialized,
  Arithmetic,
  ImplicitlyConvertibleTo<std::chrono::nanoseconds>::templ>; // clang-format on

template <typename DomainTag>
using TimePoint = NamedType<std::chrono::time_point<std::chrono::steady_clock>,
  // clang-format off
  DomainTag,
  DefaultInitialized,
  Comparable,
  Hashable,
  ImplicitlyConvertibleTo<
    std::chrono::time_point<std::chrono::steady_clock>>::templ>; // clang-format on

using PhysicalTime = TimePoint<struct PhysicalTag>;
using SimulationTime = TimePoint<struct SimulationTag>;
using PresentationTime = TimePoint<struct PresentationTag>;
using NetworkTime = TimePoint<struct NetworkTag>;
using TimelineTime = TimePoint<struct TimelineTag>;

// Audit time uses system_clock
using AuditTime = NamedType<
  std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>,
  // clang-format off
  struct AuditTag,
  DefaultInitialized,
  Comparable,
  Hashable,
  ImplicitlyConvertibleTo<std::chrono::time_point<std::chrono::system_clock,
    std::chrono::nanoseconds>>::templ>; // clang-format on

} // namespace oxygen::time
