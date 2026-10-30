//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <Oxygen/Cooker/Pak/PakStreamingCrc32.h>

namespace oxygen::content::pak {

PakStreamingCrc32::PakStreamingCrc32(Config config) noexcept
  : config_(config)
{
}

auto PakStreamingCrc32::Enabled() const noexcept -> bool
{
  return config_.enabled;
}

auto PakStreamingCrc32::Update(const uint64_t absolute_offset,
  const std::span<const std::byte> bytes) noexcept -> bool
{
  if (!config_.enabled || bytes.empty()) {
    return true;
  }

  const auto byte_count = static_cast<uint64_t>(bytes.size());
  if (byte_count > (std::numeric_limits<uint64_t>::max)() - absolute_offset) {
    return false;
  }

  const auto chunk_start = absolute_offset;
  const auto chunk_end = absolute_offset + byte_count;
  const auto skip_window_end = config_.skip_size
      > (std::numeric_limits<uint64_t>::max)() - config_.skip_offset
    ? (std::numeric_limits<uint64_t>::max)()
    : config_.skip_offset + config_.skip_size;
  const auto skip_start = (std::max)(chunk_start, config_.skip_offset);
  const auto skip_end = (std::min)(chunk_end, skip_window_end);

  if (skip_start < skip_end) {
    skipped_bytes_ += skip_end - skip_start;

    const auto first_len = static_cast<size_t>(skip_start - chunk_start);
    if (first_len > 0U) {
      crc_.Update(bytes.first(first_len));
    }

    const auto suffix_offset = static_cast<size_t>(skip_end - chunk_start);
    const auto suffix_len = static_cast<size_t>(chunk_end - skip_end);
    if (suffix_len > 0U) {
      crc_.Update(bytes.subspan(suffix_offset, suffix_len));
    }
    return true;
  }

  crc_.Update(bytes);
  return true;
}

auto PakStreamingCrc32::SkippedByteCount() const noexcept -> uint64_t
{
  return skipped_bytes_;
}

auto PakStreamingCrc32::Finalize() const noexcept -> uint32_t
{
  if (!config_.enabled) {
    return 0U;
  }
  return crc_.Finalize();
}

} // namespace oxygen::content::pak
