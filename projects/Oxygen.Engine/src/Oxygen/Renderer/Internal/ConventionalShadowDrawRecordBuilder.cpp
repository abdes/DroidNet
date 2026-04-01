//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.h>

#include <limits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

namespace oxygen::engine::internal {

auto IsConventionalShadowRasterPartition(const PassMask pass_mask) noexcept
  -> bool
{
  return pass_mask.IsSet(PassMaskBit::kShadowCaster)
    && (pass_mask.IsSet(PassMaskBit::kOpaque)
      || pass_mask.IsSet(PassMaskBit::kMasked));
}

auto BuildConventionalShadowDrawRecords(
  const PreparedSceneFrame& prepared_frame,
  std::vector<renderer::ConventionalShadowDrawRecord>& out_records) -> void
{
  out_records.clear();

  if (prepared_frame.draw_metadata_bytes.empty()
    || prepared_frame.partitions.empty()
    || prepared_frame.draw_bounding_spheres.empty()) {
    return;
  }

  if (prepared_frame.draw_metadata_bytes.size() % sizeof(DrawMetadata) != 0U) {
    LOG_F(ERROR,
      "ConventionalShadowDrawRecordBuilder: draw metadata byte count {} is not "
      "a multiple of DrawMetadata ({})",
      prepared_frame.draw_metadata_bytes.size(), sizeof(DrawMetadata));
    return;
  }

  const auto draw_count
    = prepared_frame.draw_metadata_bytes.size() / sizeof(DrawMetadata);
  if (prepared_frame.draw_bounding_spheres.size() != draw_count) {
    LOG_F(ERROR,
      "ConventionalShadowDrawRecordBuilder: draw metadata count {} does not "
      "match draw-bounds count {}",
      draw_count, prepared_frame.draw_bounding_spheres.size());
    return;
  }

  std::size_t record_count = 0U;
  for (const auto& partition : prepared_frame.partitions) {
    if (!IsConventionalShadowRasterPartition(partition.pass_mask)) {
      continue;
    }
    if (partition.begin > partition.end || partition.end > draw_count) {
      LOG_F(ERROR,
        "ConventionalShadowDrawRecordBuilder: invalid partition range "
        "[{}, {}) for draw_count {}",
        partition.begin, partition.end, draw_count);
      out_records.clear();
      return;
    }
    record_count += static_cast<std::size_t>(partition.end - partition.begin);
  }

  out_records.reserve(record_count);

  const auto* draw_records = reinterpret_cast<const DrawMetadata*>(
    prepared_frame.draw_metadata_bytes.data());
  for (std::uint32_t partition_index = 0U;
    partition_index < prepared_frame.partitions.size(); ++partition_index) {
    const auto& partition = prepared_frame.partitions[partition_index];
    if (!IsConventionalShadowRasterPartition(partition.pass_mask)) {
      continue;
    }

    for (std::uint32_t draw_index = partition.begin; draw_index < partition.end;
      ++draw_index) {
      out_records.push_back(renderer::ConventionalShadowDrawRecord {
        .world_bounding_sphere
        = prepared_frame.draw_bounding_spheres[draw_index],
        .draw_index = draw_index,
        .partition_index = partition_index,
        .partition_pass_mask = partition.pass_mask,
        .primitive_flags = draw_records[draw_index].primitive_flags,
      });
    }
  }

  CHECK_F(out_records.size() <= (std::numeric_limits<std::uint32_t>::max)(),
    "ConventionalShadowDrawRecordBuilder: output record count overflow");
}

} // namespace oxygen::engine::internal
