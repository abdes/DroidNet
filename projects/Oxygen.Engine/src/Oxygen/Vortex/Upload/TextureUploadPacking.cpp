//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

namespace oxygen::vortex::upload {
namespace {
  // Subtraction and division bound the last row without overflowing a pitch
  // multiplication or offset addition. Padding after the last row is optional.
  auto RowsFit(const std::uint32_t rows, const std::uint64_t pitch,
    const std::uint64_t row_bytes, const std::uint64_t capacity) -> bool
  {
    return rows != 0U && row_bytes != 0U && pitch >= row_bytes
      && capacity >= row_bytes
      && static_cast<std::uint64_t>(rows - 1U)
      <= (capacity - row_bytes) / pitch;
  }
} // namespace

auto TextureUploadPlan::Pack2D(const graphics::TextureDesc& destination,
  const UploadTextureSourceView& source, const std::span<std::byte> staging,
  const UploadPolicy::FillerPolicy& filler) const -> bool
{
  if (total_bytes == 0U || total_bytes > staging.size() || regions.empty()
    || regions.size() != source_indices.size()) {
    return false;
  }
  if (filler.enable_default_fill) {
    std::ranges::fill(staging.first(total_bytes), filler.filler_value);
  }
  for (std::size_t index = 0U; index < regions.size(); ++index) {
    const auto& region = regions.at(index);
    const auto source_index = source_indices.at(index);
    if (source_index >= source.subresources.size()
      || region.buffer_offset > total_bytes) {
      return false;
    }
    const auto& input = source.subresources.at(source_index);
    const auto footprint
      = graphics::ComputeLinearTextureCopyFootprint(destination.format,
        graphics::LinearTextureExtent {
          .width = region.dst_slice.width,
          .height = region.dst_slice.height,
          .depth = 1U,
        });
    const auto row_bytes = footprint.row_pitch.get();
    const auto rows = footprint.row_count;
    if (!RowsFit(rows, input.row_pitch, row_bytes, input.bytes.size())
      || !RowsFit(rows, input.row_pitch, row_bytes, input.slice_pitch)
      || !RowsFit(rows, region.buffer_row_pitch, row_bytes,
        total_bytes - region.buffer_offset)) {
      return false;
    }
    for (std::uint32_t row = 0U; row < rows; ++row) {
      const auto source_offset
        = static_cast<std::uint64_t>(row) * input.row_pitch;
      const auto destination_offset = region.buffer_offset
        + (static_cast<std::uint64_t>(row) * region.buffer_row_pitch);
      std::memcpy(staging.subspan(destination_offset, row_bytes).data(),
        input.bytes.subspan(source_offset, row_bytes).data(), row_bytes);
    }
  }
  return true;
}
} // namespace oxygen::vortex::upload
