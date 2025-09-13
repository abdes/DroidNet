//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/UploadHelpers.h>
#include <Oxygen/Renderer/Upload/RingUploadBuffer.h>

using oxygen::engine::upload::UploadBufferDesc;
using oxygen::engine::upload::UploadDataView;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadRequest;

namespace oxygen::renderer::upload {

RingUploadBuffer::RingUploadBuffer(Graphics& gfx, frame::SlotCount partitions,
  const std::uint32_t element_stride, std::string debug_label)
  : gfx_(gfx)
  , element_stride_(element_stride)
  , debug_label_(std::move(debug_label))
  , partitions_count_(partitions)
{
  DCHECK_F(element_stride_ > 0, "RingUploadBuffer requires non-zero stride");
  // Initialize per-partition ring state; SRV index created on first reserve
  head_bytes_.assign(partitions_count_, 0ULL);
  tail_bytes_.assign(partitions_count_, 0ULL);
  used_bytes_.assign(partitions_count_, 0ULL);
}

auto RingUploadBuffer::ReserveElements(
  const std::uint64_t desired_elements, const float slack) -> bool
{
  // Per-partition desired elements and total buffer bytes
  const auto desired_per_partition = desired_elements;
  const auto desired_total_bytes
    = desired_per_partition * element_stride_ * partitions_count_.get();
  const auto current_bytes = buffer_ ? buffer_->GetSize() : 0ULL;

  if (current_bytes >= desired_total_bytes) {
    // Keep capacity elements per partition consistent
    capacity_elements_per_partition_
      = (current_bytes / element_stride_) / partitions_count_.get();
    return false;
  }

  // Simplified growth logic: only check if active partition is safe to reset
  if (buffer_ && used_bytes_[active_partition_] > 0) {
    DLOG_F(1,
      "RingUploadBuffer('{}'): growth deferred - active partition {} not empty",
      debug_label_, active_partition_);
    return false;
  }

  // Compute new per-partition size with exponential growth and slack
  const auto current_single
    = current_bytes / (partitions_count_ ? partitions_count_ : 1u);
  const auto desired_single = static_cast<std::uint64_t>(desired_per_partition)
    * static_cast<std::uint64_t>(element_stride_);
  std::uint64_t new_single = (std::max)(desired_single,
    (std::max)(current_single * 2ULL,
      static_cast<std::uint64_t>(kMinElements)
        * static_cast<std::uint64_t>(element_stride_)));
  if (slack > 0.0f) {
    const float s = std::clamp(slack, 0.0f, 4.0f);
    new_single = (std::max)(new_single,
      desired_single + static_cast<std::uint64_t>(desired_single * s));
  }
  const std::uint64_t new_total_bytes
    = new_single * static_cast<std::uint64_t>(partitions_count_);

  // Create/resize buffer resource and SRV using the proper helper
  const auto result = resources::internal::EnsureBufferAndSrv(gfx_, buffer_,
    bindless_index_, new_total_bytes, element_stride_, debug_label_);
  if (!result) {
    LOG_F(ERROR, "RingUploadBuffer: EnsureBufferAndSrv failed for '{}'",
      debug_label_);
    return false;
  }

  capacity_elements_per_partition_ = (new_total_bytes / element_stride_)
    / static_cast<std::uint64_t>(partitions_count_);
  ++buffer_reallocations_;

  // EnsureBufferAndSrv already created/updated the SRV properly
  // Reset ring state on safe growth across partitions
  std::fill(head_bytes_.begin(), head_bytes_.end(), 0ULL);
  std::fill(tail_bytes_.begin(), tail_bytes_.end(), 0ULL);
  std::fill(used_bytes_.begin(), used_bytes_.end(), 0ULL);
  return true;
}

auto RingUploadBuffer::LogTelemetryStats() const -> void
{
  LOG_F(INFO, "buffer reallocations  : {}", buffer_reallocations_);
  LOG_F(INFO, "capacity elements     : {}", CapacityElements());
  LOG_F(INFO, "capacity bytes        : {} (bytes)", CapacityBytes());
  LOG_F(INFO, "failed allocations    : {}", failed_allocations_);
  LOG_F(INFO, "max used              : {} (bytes)", max_used_bytes_);

  // Calculate total used across all partitions for accurate reporting
  std::uint64_t total_used = 0;
  for (const auto& partition_used : used_bytes_) {
    total_used += partition_used;
  }
  LOG_F(INFO, "currently used        : {} (bytes)", total_used);
}

auto RingUploadBuffer::SetActivePartition(
  const oxygen::frame::Slot slot) noexcept -> void
{
  if (slot == active_partition_) {
    return;
  }
  if (slot >= partitions_count_) {
    LOG_F(ERROR,
      "RingUploadBuffer('{}'): invalid partition {} >= {}, keeping current {}",
      debug_label_, slot, partitions_count_, active_partition_);
    return;
  }

  DLOG_F(1, "RingUploadBuffer('{}'): switching from partition {} to {}",
    debug_label_, active_partition_, slot);

  // Update telemetry before resetting partition
  UpdatePerFrameTelemetry();

  active_partition_ = slot;

  // Reset the newly active partition's ring state for frame cycling
  head_bytes_[active_partition_] = 0ULL;
  tail_bytes_[active_partition_] = 0ULL;
  used_bytes_[active_partition_] = 0ULL;
}

auto RingUploadBuffer::UpdatePerFrameTelemetry() -> void
{
  std::uint64_t total_used = 0;
  for (const auto& partition_used : used_bytes_) {
    total_used += partition_used;
  }
  max_used_bytes_ = (std::max)(max_used_bytes_, total_used);
}

auto RingUploadBuffer::MakeCopyFor(const Allocation& alloc,
  const std::span<const std::byte> bytes,
  const std::string_view debug) const noexcept -> UploadRequest
{
  UploadRequest req;
  req.kind = UploadKind::kBuffer;
  req.debug_name = std::string(debug);
  UploadBufferDesc desc {
    .dst = buffer_,
    .size_bytes = (bytes.size()),
    .dst_offset = static_cast<std::uint64_t>(alloc.first_index)
      * static_cast<std::uint64_t>(element_stride_),
  };
  req.desc = desc;
  req.data = UploadDataView { bytes };
  return req;
}

auto RingUploadBuffer::MakeUploadRequestForAllocatedRange(
  const std::span<const std::byte> bytes,
  const std::string_view debug) const noexcept -> std::optional<UploadRequest>
{
  if (!buffer_ || active_partition_ >= used_bytes_.size()) {
    return std::nullopt;
  }

  const auto used = used_bytes_[active_partition_];
  if (used == 0) {
    return std::nullopt; // No allocations in this partition
  }

  // Calculate the base offset for this partition
  const auto partition_base_offset
    = static_cast<std::uint64_t>(active_partition_)
    * capacity_elements_per_partition_
    * static_cast<std::uint64_t>(element_stride_);

  UploadRequest req;
  req.kind = UploadKind::kBuffer;
  req.debug_name = std::string(debug);
  UploadBufferDesc desc {
    .dst = buffer_,
    .size_bytes = bytes.size(),
    .dst_offset = partition_base_offset,
  };
  req.desc = desc;
  req.data = UploadDataView { bytes };
  return req;
}

auto RingUploadBuffer::Allocate(const std::uint64_t elements)
  -> std::optional<Allocation>
{
  if (!buffer_) {
    return std::nullopt;
  }

  // Handle zero-element request gracefully
  if (elements == 0) {
    return Allocation { .first_index = 0, .count = 0 };
  }

  const auto stride = static_cast<std::uint64_t>(element_stride_);
  // We maintain head/tail in bytes to keep allocations aligned to the
  // element stride. We therefore keep offsets in bytes and don't need an
  // explicit align-up step: tail_bytes_ is always advanced by multiples of
  // the stride so it remains stride-aligned.
  const auto bytes_needed = elements * stride;
  const auto cap_single = capacity_elements_per_partition_ * stride;
  if (cap_single == 0 || bytes_needed > cap_single) {
    ++failed_allocations_;
    return std::nullopt;
  }
  // Try allocate at tail -> end.
  auto& head = head_bytes_[active_partition_];
  auto& tail = tail_bytes_[active_partition_];
  auto& used = used_bytes_[active_partition_];
  if (tail >= head) {
    // [head ... tail_bytes_ ... cap)
    if (tail + bytes_needed <= cap_single) {
      const auto offset_bytes = tail;
      tail += bytes_needed;
      used += bytes_needed;

      return Allocation {
        .first_index = (static_cast<std::uint64_t>(active_partition_)
                         * capacity_elements_per_partition_)
          + (offset_bytes / stride),
        .count = elements,
      };
    }
    // wrap and try from 0..head
    if (bytes_needed <= head) {
      tail = bytes_needed;
      used += bytes_needed;

      return Allocation {
        .first_index = static_cast<std::uint64_t>(active_partition_)
          * capacity_elements_per_partition_,
        .count = elements,
      };
    }
  } else {
    // tail < head: free space is [tail_bytes_ .. head)
    if (tail + bytes_needed <= head) {
      const auto offset_bytes = tail;
      tail += bytes_needed;
      used += bytes_needed;

      return Allocation {
        .first_index = (static_cast<std::uint64_t>(active_partition_)
                         * capacity_elements_per_partition_)
          + (offset_bytes / stride),
        .count = elements,
      };
    }
  }

  ++failed_allocations_;
  return std::nullopt;
}

} // namespace oxygen::renderer::upload
