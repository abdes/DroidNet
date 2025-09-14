//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/RingBufferStaging.h>

#include <algorithm>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>

using oxygen::engine::upload::Bytes;
using oxygen::engine::upload::FenceValue;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;

namespace oxygen::engine::upload {

auto RingBufferStaging::Allocate(Bytes size, std::string_view debug_name)
  -> Allocation
{
  const auto bytes = size.get();
  if (bytes == 0) {
    return {};
  }

  const auto aligned = AlignUp_(bytes, alignment_);
  EnsureCapacity_(aligned, debug_name);
  if (!buffer_ || !mapped_ptr_) {
    return {};
  }

  auto& head = heads_[active_partition_];
  const auto partition_base
    = static_cast<std::uint64_t>(active_partition_) * capacity_per_partition_;
  const auto offset = partition_base + head;
  head += aligned;

  Allocation out;
  out.buffer = buffer_;
  out.offset = offset;
  out.size = bytes; // requested size (not aligned)
  out.ptr = mapped_ptr_ + offset;

  // Update telemetry
  stats_.total_allocations++;
  stats_.total_bytes_allocated += bytes;
  stats_.allocations_this_frame++;

  // Update moving average (simple exponential moving average with alpha=0.1)
  constexpr double alpha = 0.1;
  if (stats_.avg_allocation_size == 0) {
    stats_.avg_allocation_size = static_cast<std::uint32_t>(bytes);
  } else {
    const auto new_avg = alpha * static_cast<double>(bytes)
      + (1.0 - alpha) * static_cast<double>(stats_.avg_allocation_size);
    stats_.avg_allocation_size = static_cast<std::uint32_t>(new_avg);
  }

  return out;
}

auto RingBufferStaging::RetireCompleted(UploaderTag, FenceValue /*completed*/)
  -> void
{
  // No-op: partition reset happens via SetActivePartition per frame.
}

void RingBufferStaging::EnsureCapacity_(
  std::uint64_t required, std::string_view debug_name)
{
  const auto head = heads_.empty() ? 0ULL : heads_[active_partition_];
  if (capacity_per_partition_ >= head + required && buffer_) {
    stats_.current_buffer_size = buffer_->GetSize();
    return;
  }

  const auto current = capacity_per_partition_;
  const auto grow = current > 0
    ? static_cast<std::uint64_t>(current * (1.0 + static_cast<double>(slack_)))
    : required;
  const auto new_per_partition = std::max(required, grow);
  const auto aligned_per_partition = AlignUp_(new_per_partition, alignment_);
  const auto total_capacity
    = aligned_per_partition * static_cast<std::uint64_t>(partitions_count_);

  BufferDesc desc;
  desc.size_bytes = total_capacity;
  desc.usage = BufferUsage::kNone;
  desc.memory = BufferMemory::kUpload;
  desc.debug_name = std::string(debug_name);

  if (buffer_ && buffer_->IsMapped()) {
    buffer_->UnMap();
    stats_.unmap_calls++;
  }
  buffer_ = gfx_->CreateBuffer(desc);
  stats_.buffer_growth_count++;
  mapped_ptr_ = static_cast<std::byte*>(buffer_->Map());
  stats_.map_calls++;
  capacity_per_partition_ = aligned_per_partition;
  capacity_ = total_capacity;
  stats_.current_buffer_size = buffer_->GetSize();
}

RingBufferStaging::~RingBufferStaging()
{
  if (buffer_ && buffer_->IsMapped()) {
    buffer_->UnMap();
    stats_.unmap_calls++;
  }
  buffer_.reset();
  mapped_ptr_ = nullptr;
}

} // namespace oxygen::engine::upload
