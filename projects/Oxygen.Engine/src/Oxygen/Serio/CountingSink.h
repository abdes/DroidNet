//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! An output stream that keeps no bytes and counts them, so writing a value
//! to it measures the value's encoding exactly.
/*!
 Measure, then write once into a buffer of the exact size:

 ```cpp
 auto sink = CountingSink {};
 CHECK_RESULT(Writer(sink).Write(record));
 auto stream = MemoryStream {};
 stream.Reserve(sink.Count());
 ```

 It patches like any patchable stream, so a writer that completes a header
 measures the same through it.
*/
class CountingSink {
public:
  //! A sink with nothing counted.
  CountingSink() noexcept = default;

  //! A sink that starts with `counted` bytes counted: the measure of a value
  //! written after a prefix of known size, at the offset it will have.
  explicit CountingSink(const ByteCount counted) noexcept
    : count_(counted)
  {
  }

  //! Counts `in`; `value_too_large` when the count would pass the largest
  //! offset.
  [[nodiscard]] auto Write(const std::span<const std::byte> in) noexcept
    -> Result<void>
  {
    const auto range = ByteRange::Make(Position(), CountOf(in.size()));
    if (!range) {
      return ::oxygen::Err(range.error());
    }
    count_ = ByteCount { range->End().get() };
    return {};
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void> { return {}; }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return ByteOffset { count_.get() };
  }

  //! Checks that `in` at `at` lies within the bytes counted;
  //! `kOutOfBounds` otherwise.
  [[nodiscard]] auto PatchAt(const ByteOffset at,
    const std::span<const std::byte> in) const noexcept -> Result<void>
  {
    if (at.get() > count_.get() || in.size() > count_.get() - at.get()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    return {};
  }

  //! The number of bytes written.
  [[nodiscard]] auto Count() const noexcept -> ByteCount { return count_; }

private:
  ByteCount count_ { 0 };
};

} // namespace oxygen::serio
