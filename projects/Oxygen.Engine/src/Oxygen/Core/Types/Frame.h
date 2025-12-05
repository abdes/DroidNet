//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen {

//! Strong type representing the index of an in-flight frame slot used by the
//! renderer (e.g. frames-in-flight index).
using FrameSlotNumber = NamedType<uint32_t, struct FrameSlotNumberTag,
  // clang-format off
  DefaultInitialized,
  ImplicitlyConvertibleTo<uint32_t>::templ,
  PreIncrementable,
  PostIncrementable,
  Addable,
  Subtractable,
  Comparable,
  Printable,
  Hashable>; // clang-format on

//! Convert a FrameSlotNumber to a human-readable string.
OXGN_CORE_NDAPI auto to_string(FrameSlotNumber s) -> std::string;

// Strong type representing a count of frame slots (engine-level type).
/*!
 A thin wrapper around NamedType that enforces a minimum value of 1 and
 default-initializes to 1. This preserves the same traits used by the
 previous typedef (Comparable, Printable, Hashable) while adding runtime
 validation for construction and assignment.
*/
class FrameSlotCount
  : public NamedType<uint32_t, struct FrameSlotCountTag,
      // clang-format off
      DefaultInitialized,
      oxygen::ImplicitlyConvertibleTo<uint32_t>::templ,
      Comparable,
      Printable,
      Hashable> // clang-format on
{
public:
  using Base = NamedType<uint32_t, struct FrameSlotCountTag,
    // clang-format off
    oxygen::DefaultInitialized,
    oxygen::ImplicitlyConvertibleTo<uint32_t>::templ,
    oxygen::Comparable,
    oxygen::Printable,
    oxygen::Hashable>; // clang-format on

  // Default construct to 1 (one frame slot) rather than zero.
  constexpr FrameSlotCount() noexcept
    : Base { 1u }
  {
  }

  // Construct from an explicit value; enforce value >= 1.
  explicit constexpr FrameSlotCount(uint32_t v)
    : Base { [](uint32_t v) {
      if (v >= 1u)
        return v;
      throw std::invalid_argument("FrameSlotCount must be >= 1");
    }(v) }
  {
  }

  // Allow implicit conversion from Base where safe via explicit factory.
  static constexpr FrameSlotCount FromUnderlying(uint32_t v)
  {
    return FrameSlotCount { v };
  }

  // Keep the base's get() accessible.
  using Base::get;
  using Base::ref;
};

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
using FrameSequenceNumber = NamedType<uint64_t, struct FrameSequenceNumberTag,
  // clang-format off
  DefaultInitialized,
  PreIncrementable,
  Addable,
  Subtractable,
  Comparable,
  Printable,
  Hashable>; // clang-format on

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
