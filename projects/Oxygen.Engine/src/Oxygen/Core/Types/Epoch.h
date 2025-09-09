//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string>

#include <Oxygen/Base/NamedType.h>

namespace oxygen {

//! Strong type representing a monotonic counter for resource tracking.
/*!
 Epoch values are incremented at predictable points in the frame lifecycle (for
 example, at the start of a frame) and are attached to resources when they are
 created or modified. Comparing a resource's epoch with the current epoch
 provides a fast check for staleness: if resource_epoch < current_epoch then the
 resource was not touched this frame and may require update or reclamation.

 Use cases:
 - Subsystems tag resources with the current epoch to avoid redundant work
   within the same frame.
 - Local epoch counters may be used by subsystems or modules for finer-grained
   control, while the engine can maintain a global epoch for cross-subsystem
   coordination.

 This type is a strong type over `uint64_t` to prevent accidental mixing of
 unrelated counters.
*/
using Epoch = NamedType<uint64_t, struct EpochTag,
  // clang-format off
  PostIncrementable,
  PreIncrementable,
  Comparable,
  Printable>; // clang-format on

inline constexpr auto to_string(Epoch e) -> std::string
{
  return "Epoch(" + std::to_string(e.get()) + ")";
}

namespace epoch {
  static constexpr Epoch kNever { 0 };
}

} // namespace oxygen
