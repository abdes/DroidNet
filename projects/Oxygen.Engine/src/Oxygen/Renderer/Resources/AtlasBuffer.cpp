//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Resources/AtlasBuffer.h>

#include <algorithm>

namespace oxygen::renderer::resources {

// Define the tag used to gate ElementRef construction
struct ElementRefTag { };

AtlasBuffer::AtlasBuffer(observer_ptr<Graphics> gfx, const std::uint32_t stride,
  std::string debug_label)
  : gfx_(std::move(gfx))
  , debug_label_(std::move(debug_label))
  , stride_(stride)
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
}

AtlasBuffer::~AtlasBuffer()
{
  if (primary_buffer_) {
    gfx_->GetResourceRegistry().UnRegisterResource(*primary_buffer_);
  }
}

auto AtlasBuffer::EnsureCapacity(const std::uint32_t min_elements,
  const float slack) -> std::expected<EnsureResult, std::error_code>
{
  stats_.ensure_calls++;
  const std::uint64_t min_bytes
    = static_cast<std::uint64_t>(min_elements) * stride_;
  const std::uint64_t current_bytes
    = primary_buffer_ ? primary_buffer_->GetSize() : 0ull;
  const std::uint64_t target_bytes = primary_buffer_
    ? (std::max)(current_bytes,
        static_cast<std::uint64_t>(min_bytes * (1.0f + slack)))
    : min_bytes;

  using internal::EnsureBufferResult;
  if (!primary_buffer_ || target_bytes > current_bytes) {
    auto result = internal::EnsureBufferAndSrv(*gfx_, primary_buffer_,
      primary_srv_, target_bytes, stride_, debug_label_);
    if (!result) {
      return std::unexpected(result.error());
    }

    // Update capacity on create/resize
    if (*result == EnsureBufferResult::kCreated
      || *result == EnsureBufferResult::kResized) {
      const auto prev_next = next_index_;
      capacity_elements_
        = static_cast<std::uint32_t>(primary_buffer_->GetSize() / stride_);
      if (*result == EnsureBufferResult::kCreated) {
        // Fresh buffer: start from 0
        next_index_ = 0;
      } else {
        // Preserve allocation tail across resizes;
        // clamp to new capacity just in case.
        next_index_ = (std::min)(prev_next, capacity_elements_);
      }
      // (Phase 1) We do not migrate live data here; caller re-uploads.
      stats_.capacity_elements = capacity_elements_;
      stats_.next_index = next_index_;
    }

    switch (*result) {
    case EnsureBufferResult::kUnchanged:
      return EnsureResult::kUnchanged;
    case EnsureBufferResult::kCreated:
      return EnsureResult::kCreated;
    case EnsureBufferResult::kResized:
      return EnsureResult::kResized;
    }
  }
  return EnsureResult::kUnchanged;
}

auto AtlasBuffer::Allocate(const std::uint32_t count)
  -> std::expected<ElementRef, std::error_code>
{
  if (count != 1) {
    // Phase 1: only single-element allocations supported
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  std::uint32_t idx;
  if (!free_list_.empty()) {
    idx = free_list_.back();
    free_list_.pop_back();
  } else {
    // Append new index if capacity allows
    if (next_index_ >= capacity_elements_) {
      return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
    }
    idx = next_index_++;
  }
  stats_.allocations++;
  stats_.next_index = next_index_;
  stats_.free_list_size = static_cast<std::uint32_t>(free_list_.size());
  return ElementRef { ElementRefTag {}, primary_srv_, idx };
}

auto AtlasBuffer::Release(const ElementRef ref, const frame::Slot slot) -> void
{
  if (ref.srv_ != primary_srv_) {
    // Phase 1 invariant: only primary chunk exists
    return;
  }
  retire_lists_[slot.get()].push_back(ref.element_);
  stats_.releases++;
}

auto AtlasBuffer::OnFrameStart(const frame::Slot slot) -> void
{
  auto& list = retire_lists_[slot.get()];
  if (!list.empty()) {
    // Move retired elements into free list
    free_list_.insert(free_list_.end(), list.begin(), list.end());
    list.clear();
    stats_.free_list_size = static_cast<std::uint32_t>(free_list_.size());
  }
}

auto AtlasBuffer::MakeUploadDesc(
  const ElementRef& ref, const std::uint64_t size_bytes) const
  -> std::expected<oxygen::engine::upload::UploadBufferDesc, std::error_code>
{
  // Phase 1 invariant: only primary chunk exists
  if (ref.srv_ != primary_srv_ || !primary_buffer_) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (ref.element_ >= capacity_elements_) {
    return std::unexpected(
      std::make_error_code(std::errc::result_out_of_range));
  }

  oxygen::engine::upload::UploadBufferDesc desc;
  desc.dst = primary_buffer_;
  desc.size_bytes = size_bytes;
  desc.dst_offset = static_cast<std::uint64_t>(ref.element_) * stride_;
  return desc;
}

} // namespace oxygen::renderer::resources
