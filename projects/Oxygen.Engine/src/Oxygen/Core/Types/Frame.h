//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen {

//! Strong type representing the index of an in-flight frame slot used by the
//! renderer (e.g. frames-in-flight index).
using FrameSlotNumber = oxygen::NamedType<uint32_t, struct FrameSlotNumberTag,
  // clang-format off
  oxygen::DefaultInitialized,
  oxygen::PreIncrementable,
  oxygen::PostIncrementable,
  oxygen::Addable,
  oxygen::Subtractable,
  oxygen::Comparable,
  oxygen::Printable,
  oxygen::Hashable>; // clang-format on

//! Convert a FrameSlotNumber to a human-readable string.
OXGN_CORE_NDAPI auto to_string(FrameSlotNumber s) -> std::string;

// Strong type representing a count of frame slots (engine-level type).
using FrameSlotCount = oxygen::NamedType<uint32_t, struct FrameSlotCountTag,
  // clang-format off
  oxygen::DefaultInitialized,
  oxygen::Comparable,
  oxygen::Printable,
  oxygen::Hashable>; // clang-format on

//! Convert a FrameSlotCount to a human-readable string.
OXGN_CORE_NDAPI auto to_string(FrameSlotCount sc) -> std::string;

namespace frame {
  //! Compact alias
  using Slot = FrameSlotNumber;

  //! Alias for the engine-level count type.
  using SlotCount = FrameSlotCount;

  //! The number of frame buffers we manage (count form)
  constexpr SlotCount kFramesInFlight { 3 };

  //! Sentinel representing an invalid frame slot.
  inline constexpr Slot kInvalidSlot { (std::numeric_limits<uint32_t>::max)() };

  //! Maximum exclusive slot value (one past the last valid slot) matching the
  //! underlying storage.
  inline constexpr Slot kMaxSlot { kFramesInFlight.get() };
} // namespace frame

//! Strong type representing a monotonically increasing frame sequence number.
/*!
 The sequence number increases for each frame presentation/submission and is
 intended to be a global, ever-increasing counter. Use a 64-bit underlying type
 to avoid wraparound in long-running processes.
*/
using FrameSequenceNumber = oxygen::NamedType<uint64_t,
  struct FrameSequenceNumberTag,
  // clang-format off
  oxygen::DefaultInitialized,
  oxygen::PreIncrementable,
  oxygen::PostIncrementable,
  oxygen::Addable,
  oxygen::Subtractable,
  oxygen::Comparable,
  oxygen::Printable,
  oxygen::Hashable>; // clang-format on

//! Convert a FrameSequenceNumber to a human-readable string.
OXGN_CORE_NDAPI auto to_string(FrameSequenceNumber seq) -> std::string;

namespace frame {
  //! Alias for clarity
  using SequenceNumber = FrameSequenceNumber;

  //! Maximum exclusive sequence value (sentinel). This is a reserved/unusable
  //! value used to represent "invalid" or "uninitialized" sequence numbers.
  //! Valid sequence numbers are strictly less than this sentinel.
  inline constexpr SequenceNumber kMaxSequenceNumber { (
    std::numeric_limits<uint64_t>::max)() };

  //! Sentinel representing an invalid/uninitialized sequence number.
  inline constexpr SequenceNumber kInvalidSequenceNumber { kMaxSequenceNumber };
}

} // namespace oxygen
