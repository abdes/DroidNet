//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Renderer/Upload/AtlasBuffer.h>

namespace oxygen::engine::upload {

// Define the tag used to gate ElementRef construction
struct ElementRefTag { };

/*!
 Creates an instance without allocating GPU memory until the first
 EnsureCapacity() call.

 @param gfx Non-null graphics system pointer.
 @param stride Size in bytes of each fixed element.
 @param debug_label Human-readable label used for buffer naming/logging.

 @warning `gfx` must outlive the AtlasBuffer.
*/
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

/*!
 Ensures capacity for at least `min_elements` elements in the primary structured
 buffer. May create (kCreated) or resize (kResized) the underlying GPU buffer.
 If existing capacity suffices, returns kUnchanged.

 Growth factor can be influenced by `slack` (fractional extra space). Caller is
 responsible for re-uploading existing live data after a resize (Phase 1 does
 not migrate contents).

 @param min_elements Minimum required element capacity.
 @param slack Additional fractional growth hint (e.g. 0.2 = +20%).
 @return EnsureBufferResult enum wrapped in std::expected; unexpected carries
         allocation / driver errors from helper.

 ### Performance Characteristics

 - Time Complexity: O(1) when unchanged; O(1) plus GPU allocation when
   creating/resizing.
 - Memory: Potential allocation of a new buffer; old buffer released.
 - Optimization: Slack reduces frequency of future reallocations.

 ### Usage Examples

 ```cpp
 auto res = atlas.EnsureCapacity(128, 0.25f);
 if (res && *res != AtlasBuffer::EnsureBufferResult::kUnchanged) {
   // (Re)upload existing element data if needed.
 }
 ```

 @note Safe to call redundantly; inexpensive when capacity is adequate.
 @see AtlasBuffer::Allocate
*/
auto AtlasBuffer::EnsureCapacity(const std::uint32_t min_elements,
  const float slack) -> std::expected<EnsureBufferResult, std::error_code>
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

  // Fast path: if a primary buffer exists and the target size is not
  // larger than current, nothing to do.
  if (primary_buffer_ && target_bytes <= current_bytes) {
    return EnsureBufferResult::kUnchanged;
  }

  auto result = internal::EnsureBufferAndSrv(
    *gfx_, primary_buffer_, primary_srv_, target_bytes, stride_, debug_label_);
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

  return result;
}

/*!
 Allocates one element slot and returns an ElementRef.

 @param count Must be 1 in Phase 1; any other value returns
              std::errc::invalid_argument.
 @return ElementRef on success; unexpected with std::errc::invalid_argument for
         unsupported count, or std::errc::no_buffer_space when capacity
         exhausted.

 ### Performance Characteristics

 - Time Complexity: Amortized O(1).
 - Memory: No additional allocation (pure index management).
 - Optimization: Reuses indices from internal free list (order agnostic).

 ### Usage Examples

 ```cpp
 if (auto ref = atlas.Allocate(); ref) {
   // Use *ref ... later release
 }
 ```

 @warning Caller must invoke EnsureCapacity() beforehand if needed.
 @see AtlasBuffer::Release, AtlasBuffer::EnsureCapacity
*/
auto AtlasBuffer::Allocate(const std::uint32_t count)
  -> std::expected<ElementRef, std::error_code>
{
  if (count != 1) {
    // FIXME: Phase 1: only single-element allocations supported
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

/*!
 Releases an allocated element: the element index is appended to the retire list
 of the specified frame::Slot and only becomes reusable after OnFrameStart(slot)
 is invoked for that same slot.

 @param ref Element reference previously returned by Allocate().
 @param slot Frame slot representing the in-flight frame that last used the
     element.

 ### Performance Characteristics

 - Time Complexity: O(1) push into retire list.
 - Memory: No allocation; vector may occasionally grow.

 @note Releasing an ElementRef whose SRV does not match the primary is ignored
   (Phase 1 invariant enforcement).
 @see AtlasBuffer::OnFrameStart
*/
auto AtlasBuffer::Release(const ElementRef ref, const frame::Slot slot) -> void
{
  if (ref.srv_ != primary_srv_) {
    // Phase 1 invariant: only primary chunk exists
    return;
  }
  retire_lists_[slot.get()].push_back(ref.element_);
  stats_.releases++;
}

/*!
 Recycles all elements retired for `slot` into the free list, making them
 immediately available for future Allocate() calls.

 @param slot Frame slot to recycle.

 ### Performance Characteristics

 - Time Complexity: O(k) where k = retired elements for slot; elements are
   appended to free list without deduplication.
 - Memory: No deallocation; vectors may retain capacity.

 @see AtlasBuffer::Release
*/
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

/*!
 Builds an UploadBufferDesc for a specific ElementRef.

 @param ref Element reference to validate.
 @param size_bytes Byte size of payload to upload for this element.
 @return UploadBufferDesc with dst pointer, offset and size populated;
         unexpected with std::errc::invalid_argument if the reference is invalid
         or buffer not created; std::errc::result_out_of_range if the element
         index exceeds current capacity.

 @see AtlasBuffer::MakeUploadDescForIndex
*/
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

/*!
 Builds an UploadBufferDesc for a raw element index.

 @param element_index Element slot index (0-based) inside primary buffer.
 @param size_bytes Byte size of payload to upload for this element.
 @return UploadBufferDesc on success; unexpected with
   std::errc::invalid_argument if buffer not yet created;
   std::errc::result_out_of_range if index >= current capacity.

 @see AtlasBuffer::MakeUploadDesc
*/
auto AtlasBuffer::MakeUploadDescForIndex(
  const std::uint32_t element_index, const std::uint64_t size_bytes) const
  -> std::expected<oxygen::engine::upload::UploadBufferDesc, std::error_code>
{
  if (!primary_buffer_) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (element_index >= capacity_elements_) {
    return std::unexpected(
      std::make_error_code(std::errc::result_out_of_range));
  }

  oxygen::engine::upload::UploadBufferDesc desc;
  desc.dst = primary_buffer_;
  desc.size_bytes = size_bytes;
  desc.dst_offset = static_cast<std::uint64_t>(element_index) * stride_;
  return desc;
}

} // namespace oxygen::engine::upload
