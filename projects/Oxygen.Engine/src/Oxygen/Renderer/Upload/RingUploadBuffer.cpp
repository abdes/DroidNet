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

RingUploadBuffer::RingUploadBuffer(
  Graphics& gfx, const std::uint32_t element_stride, std::string debug_label)
  : gfx_(gfx)
  , element_stride_(element_stride)
  , debug_label_(std::move(debug_label))
{
  DCHECK_F(element_stride_ > 0, "RingUploadBuffer requires non-zero stride");
}

auto RingUploadBuffer::ReserveElements(
  const std::uint64_t desired_elements, const float slack) -> bool
{
  const auto desired = static_cast<std::uint64_t>(desired_elements)
    * static_cast<std::uint64_t>(element_stride_);
  const auto current = buffer_ ? buffer_->GetSize() : 0ULL;
  if (current >= desired) {
    // Keep capacity elements consistent if created externally
    capacity_elements_ = current / element_stride_;
    return false;
  }

  // Grow exponentially and add slack to reduce churn.
  std::uint64_t new_bytes = (std::max)(desired,
    (std::max)(current * 2ULL,
      static_cast<std::uint64_t>(kMinElements)
        * static_cast<std::uint64_t>(element_stride_)));
  if (slack > 0.0f) {
    const float s = std::clamp(slack, 0.0f, 4.0f);
    new_bytes = (std::max)(new_bytes,
      desired + static_cast<std::uint64_t>(desired * s));
  }

  const auto result = resources::internal::EnsureBufferAndSrv(
    gfx_, buffer_, bindless_index_, new_bytes, element_stride_, debug_label_);
  if (!result) {
    LOG_F(ERROR, "RingUploadBuffer: EnsureBufferAndSrv failed for '{}'",
      debug_label_);
    return false;
  }
  capacity_elements_ = new_bytes / element_stride_;
  ++buffer_reallocations_;
  return true;
}

auto RingUploadBuffer::LogTelemetryStats() const -> void
{
  LOG_F(INFO, "buffer reallocations  : {}", buffer_reallocations_);
  LOG_F(INFO, "curr allocations      : {}", curr_allocations_);
  LOG_F(INFO, "capacity elements     : {}", CapacityElements());
  LOG_F(INFO, "capacity bytes        : {} (bytes)", CapacityBytes());
  LOG_F(INFO, "allocations per frame : {}", avg_allocations_per_frame_);
  LOG_F(INFO, "failed allocations    : {}", failed_allocations_);
  LOG_F(INFO, "max used              : {} (bytes)", max_used_bytes_);
  LOG_F(INFO, "currently used        : {} (bytes)", UsedBytes());
}

auto RingUploadBuffer::SetActiveElements(const std::uint64_t active_elements)
  -> bool
{
  if (!buffer_ || bindless_index_ == kInvalidShaderVisibleIndex) {
    return false;
  }
  const auto clamped = (std::min)(active_elements, capacity_elements_);

  graphics::BufferViewDescription view_desc {
    .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .range = { 0, clamped * element_stride_ },
    .stride = element_stride_,
  };

  auto& registry = gfx_.GetResourceRegistry();
  return registry.UpdateView(
    *buffer_, bindless::Handle(bindless_index_.get()), view_desc);
}

auto RingUploadBuffer::MakeCopyAll(const std::span<const std::byte> bytes,
  const std::string_view debug) const noexcept -> UploadRequest
{
  UploadRequest req;
  req.kind = UploadKind::kBuffer;
  req.debug_name = std::string(debug);
  UploadBufferDesc desc {
    .dst = buffer_,
    .size_bytes = (bytes.size()),
    .dst_offset = 0,
  };
  req.desc = desc;
  req.data = UploadDataView { bytes };
  return req;
}

auto RingUploadBuffer::MakeCopyRange(const std::uint64_t element_offset,
  const std::span<const std::byte> bytes,
  const std::string_view debug) const noexcept -> UploadRequest
{
  UploadRequest req;
  req.kind = UploadKind::kBuffer;
  req.debug_name = std::string(debug);
  UploadBufferDesc desc {
    .dst = buffer_,
    .size_bytes = (bytes.size()),
    .dst_offset = static_cast<std::uint64_t>(element_offset)
      * static_cast<std::uint64_t>(element_stride_),
  };
  req.desc = desc;
  req.data = UploadDataView { bytes };
  return req;
}

auto RingUploadBuffer::MakeCopyFor(const Allocation& alloc,
  const std::span<const std::byte> bytes,
  const std::string_view debug) const noexcept -> UploadRequest
{
  return MakeCopyRange(alloc.element_offset, bytes, debug);
}

auto RingUploadBuffer::SetActiveRange(
  const std::uint64_t base_element, const std::uint64_t active_elements) -> bool
{
  if (!buffer_ || bindless_index_ == kInvalidShaderVisibleIndex) {
    return false;
  }
  const auto max_elems = capacity_elements_;
  const auto base = (std::min)(base_element, max_elems);
  const auto count = (std::min)(active_elements, max_elems - base);

  graphics::BufferViewDescription view_desc {
    .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .range = {
      base * static_cast<std::uint64_t>(element_stride_),
      count * static_cast<std::uint64_t>(element_stride_),
    },
    .stride = element_stride_,
  };

  auto& registry = gfx_.GetResourceRegistry();
  return registry.UpdateView(
    *buffer_, bindless::Handle(bindless_index_.get()), view_desc);
}

auto RingUploadBuffer::Allocate(const std::uint64_t elements)
  -> std::optional<Allocation>
{
  if (!buffer_) {
    return std::nullopt;
  }
  const auto stride = static_cast<std::uint64_t>(element_stride_);
  // Allocations are expressed in elements and thus are multiples of the
  // element stride. We therefore keep offsets in bytes and don't need an
  // explicit align-up step: tail_bytes_ is always advanced by multiples of
  // the stride so it remains stride-aligned.
  const auto bytes_needed = elements * stride;
  const auto cap = CapacityBytes();
  if (bytes_needed == 0 || cap == 0 || bytes_needed > cap) {
    ++failed_allocations_;
    return std::nullopt;
  }
  // Try allocate at tail -> end.
  if (tail_bytes_ >= head_bytes_) {
    // [head ... tail_bytes_ ... cap)
    if (tail_bytes_ + bytes_needed <= cap) {
      const auto offset_bytes = tail_bytes_;
      tail_bytes_ = tail_bytes_ + bytes_needed;
      used_bytes_ += bytes_needed;
      curr_frame_bytes_ += bytes_needed;
      max_used_bytes_ = (std::max)(max_used_bytes_, used_bytes_);
      ++curr_allocations_;
      return Allocation {
        .element_offset = offset_bytes / stride,
        .elements = elements,
      };
    }
    // wrap and try from 0..head
    if (bytes_needed <= head_bytes_) {
      tail_bytes_ = bytes_needed;
      used_bytes_ += bytes_needed;
      curr_frame_bytes_ += bytes_needed;
      max_used_bytes_ = (std::max)(max_used_bytes_, used_bytes_);
      ++curr_allocations_;
      return Allocation {
        .element_offset = 0,
        .elements = elements,
      };
    }
  } else {
    // tail < head: free space is [tail_bytes_ .. head)
    if (tail_bytes_ + bytes_needed <= head_bytes_) {
      const auto offset_bytes = tail_bytes_;
      tail_bytes_ = tail_bytes_ + bytes_needed;
      used_bytes_ += bytes_needed;
      curr_frame_bytes_ += bytes_needed;
      max_used_bytes_ = (std::max)(max_used_bytes_, used_bytes_);
      ++curr_allocations_;
      return Allocation {
        .element_offset = offset_bytes / stride,
        .elements = elements,
      };
    }
  }

  ++failed_allocations_;
  return std::nullopt;
}

auto RingUploadBuffer::FinalizeChunk() -> std::optional<ChunkId>
{
  if (curr_frame_bytes_ == 0) {
    return std::nullopt;
  }
  const auto id = next_chunk_id_++;
  completed_frames_.push_back(FrameTail {
    .id = id,
    .tail = tail_bytes_,
    .size = curr_frame_bytes_,
  });

  auto update_telemetry = [this]() noexcept {
    // Integer exponential moving average with alpha = 1/4.
    // new = round( (1/4)*curr + (3/4)*old ). Use 64-bit intermediate for
    // safety and add 2 for rounding (i.e., (curr + 3*old + 2) / 4 ).
    if (avg_allocations_per_frame_ == 0u) {
      avg_allocations_per_frame_ = curr_allocations_;
      return;
    }
    const std::uint64_t tmp = static_cast<std::uint64_t>(curr_allocations_)
      + 3ULL * static_cast<std::uint64_t>(avg_allocations_per_frame_);
    avg_allocations_per_frame_
      = static_cast<std::uint32_t>((tmp + 2ULL) / 4ULL);
  };
  update_telemetry();

  curr_frame_bytes_ = 0;
  curr_allocations_ = 0;
  return id;
}

auto RingUploadBuffer::TryReclaim(const ChunkId id) -> bool
{
  if (completed_frames_.empty()) {
    return false;
  }
  const auto& front = completed_frames_.front();
  if (front.id != id) {
    return false; // enforce FIFO; client passes the exact front id
  }
  completed_frames_.pop_front();
  DCHECK_F(front.size <= used_bytes_, "RingUploadBuffer reclaim size overflow");
  used_bytes_ -= front.size;
  head_bytes_ = front.tail;
  return true;
}

} // namespace oxygen::renderer::upload
