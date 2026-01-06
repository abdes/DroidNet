//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Content/Import/util/Constants.h>

namespace oxygen::content::import::util {

//! Aligns a value up to the specified alignment.
/*!
 @param value The value to align.
 @param alignment The alignment boundary (must be > 0).
 @return The aligned value.
*/
[[nodiscard]] inline auto AlignUp(
  const uint64_t value, const uint64_t alignment) -> uint64_t
{
  if (alignment <= 1) {
    return value;
  }

  const auto remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }
  return value + (alignment - remainder);
}

//! Repacks tightly-packed RGBA8 pixels to row-pitch-aligned layout.
/*!
 D3D12 texture uploads require rows to be aligned to a specific pitch.
 This function inserts padding at the end of each row to meet that
 requirement.

 @param rgba8_tight Tightly-packed RGBA8 pixel data.
 @param width Image width in pixels.
 @param height Image height in pixels.
 @param row_pitch_alignment Row pitch alignment in bytes.
 @return Row-pitch-aligned pixel data.
*/
[[nodiscard]] inline auto RepackRgba8ToRowPitchAligned(
  const std::span<const std::byte> rgba8_tight, const uint32_t width,
  const uint32_t height, const uint64_t row_pitch_alignment)
  -> std::vector<std::byte>
{
  const auto tight_row_bytes
    = static_cast<uint64_t>(width) * kBytesPerPixelRgba8;
  const auto row_pitch = AlignUp(tight_row_bytes, row_pitch_alignment);
  const auto total_bytes = row_pitch * static_cast<uint64_t>(height);

  std::vector<std::byte> out;
  out.resize(static_cast<size_t>(total_bytes), std::byte { 0 });

  const auto tight_total_bytes
    = tight_row_bytes * static_cast<uint64_t>(height);
  if (rgba8_tight.size() < static_cast<size_t>(tight_total_bytes)) {
    return out;
  }

  for (uint32_t y = 0; y < height; ++y) {
    const auto src_row_offset
      = static_cast<size_t>(static_cast<uint64_t>(y) * tight_row_bytes);
    const auto dst_row_offset
      = static_cast<size_t>(static_cast<uint64_t>(y) * row_pitch);
    std::copy_n(rgba8_tight.data() + src_row_offset,
      static_cast<size_t>(tight_row_bytes), out.data() + dst_row_offset);
  }
  return out;
}

//! Appends bytes to a blob with alignment padding.
/*!
 @param blob The blob to append to.
 @param bytes The bytes to append.
 @param alignment The alignment for the appended data.
 @return The offset where the data was written.
*/
[[nodiscard]] inline auto AppendAligned(std::vector<std::byte>& blob,
  const std::span<const std::byte> bytes, const uint64_t alignment) -> uint64_t
{
  const auto begin = static_cast<uint64_t>(blob.size());
  const auto aligned = AlignUp(begin, alignment);
  if (aligned > begin) {
    blob.resize(static_cast<size_t>(aligned), std::byte { 0 });
  }
  const auto offset = static_cast<uint64_t>(blob.size());
  blob.insert(blob.end(), bytes.begin(), bytes.end());
  return offset;
}

} // namespace oxygen::content::import::util
