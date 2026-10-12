//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Vortex/Upload/AtlasBuffer.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadHelpers.h>

namespace oxygen::vortex::upload {

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
  std::string debug_label, const bindless::DomainToken domain)
  : gfx_(gfx)
  , debug_label_(std::move(debug_label))
  , stride_(stride)
  , domain_(domain)
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
}

AtlasBuffer::~AtlasBuffer()
{
  if (!primary_buffer_) {
    return;
  }

  gfx_->RetireResource(primary_buffer_);
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
auto AtlasBuffer::EnsureCapacity(const CapacityRequest& request)
  -> std::expected<EnsureBufferResult, std::error_code>
{
  const auto min_elements = request.min_elements;
  const auto slack = request.growth_slack;
  stats_.ensure_calls++;

  const std::uint64_t min_bytes
    = static_cast<std::uint64_t>(min_elements) * stride_;
  const std::uint64_t current_bytes
    = primary_buffer_ ? primary_buffer_->GetSize() : 0ULL;
  if (primary_buffer_ && min_bytes <= current_bytes) {
    return EnsureBufferResult::kUnchanged;
  }
  // Grow with slack so a run of single allocations does not resize each time.
  const std::uint64_t target_bytes = primary_buffer_
    ? (std::max)(min_bytes,
        static_cast<std::uint64_t>(
          static_cast<double>(min_bytes) * (1.0 + static_cast<double>(slack)))
          / stride_ * stride_)
    : min_bytes;

  // Every allocated element can be returned to any frame bucket. Reserve before
  // publishing capacity so release (including failed caller activation) and
  // frame reclamation cannot allocate.
  const auto return_capacity = static_cast<std::size_t>(target_bytes / stride_);
  free_list_.reserve(return_capacity);
  for (auto& retired : retire_lists_) {
    retired.reserve(return_capacity);
  }

  auto created = internal::CreateBufferAndSrv(*gfx_,
    internal::StructuredBufferSpec {
      .size_bytes = target_bytes,
      .stride = stride_,
      .debug_label = debug_label_,
      .domain = domain_,
    });
  if (!created) {
    return std::unexpected(created.error());
  }
  const auto result = primary_buffer_ ? EnsureBufferResult::kResized
                                      : EnsureBufferResult::kCreated;
  if (primary_buffer_) {
    // In-flight frames keep reading the old buffer through its old SRV; the
    // new SRV is published only once the GPU copy carries the live elements.
    if (!CopyLiveElements(*primary_buffer_, *created->buffer)) {
      gfx_->RetireResource(created->buffer);
      return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    gfx_->RetireResource(primary_buffer_);
  }
  primary_buffer_ = std::move(created->buffer);
  primary_srv_ = created->srv;

  const auto prev_next = next_index_;
  capacity_elements_
    = static_cast<std::uint32_t>(primary_buffer_->GetSize() / stride_);
  next_index_ = result == EnsureBufferResult::kCreated
    ? 0U
    : (std::min)(prev_next, capacity_elements_);
  stats_.capacity_elements = capacity_elements_;
  stats_.next_index = next_index_;
  return result;
}

auto AtlasBuffer::CopyLiveElements(
  const graphics::Buffer& source, graphics::Buffer& destination) -> bool
{
  auto recorder = gfx_->AcquireCommandRecorder(
    gfx_->QueueKeyFor(graphics::QueueRole::kGraphics), "AtlasBuffer.Grow");
  if (!recorder) {
    LOG_F(
      ERROR, "AtlasBuffer '{}': cannot record the growth copy", debug_label_);
    return false;
  }
  if (!recorder->AdoptKnownResourceState(source)) {
    recorder->BeginTrackingResourceState(
      source, graphics::ResourceStates::kCommon, false);
  }
  recorder->BeginTrackingResourceState(
    destination, graphics::ResourceStates::kCommon, false);
  recorder->RequireResourceState(source, graphics::ResourceStates::kCopySource);
  recorder->RequireResourceState(
    destination, graphics::ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBuffer(destination, 0U, source, 0U, source.GetSize());
  recorder->RequireResourceStateFinal(
    destination, graphics::ResourceStates::kShaderResource);
  return recorder.Submit();
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
    // FIXME: only single-element allocations supported
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  std::uint32_t idx { 0U };
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
  return ElementRef { ElementRefTag {}, idx };
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
 - Memory: No allocation; return capacity is reserved before allocation.

 @note Releasing a default-constructed ElementRef is ignored.
 @see AtlasBuffer::OnFrameStart
*/
auto AtlasBuffer::Release(const ElementRef ref, const frame::Slot slot) -> void
{
  if (!ref.valid_) {
    return;
  }
  retire_lists_.at(slot.get()).push_back(ref.element_);
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
  auto& list = retire_lists_.at(slot.get());
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
  -> std::expected<oxygen::vortex::upload::UploadBufferDesc, std::error_code>
{
  // Phase 1 invariant: only primary chunk exists
  if (!ref.valid_ || !primary_buffer_) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (ref.element_ >= capacity_elements_) {
    return std::unexpected(
      std::make_error_code(std::errc::result_out_of_range));
  }

  oxygen::vortex::upload::UploadBufferDesc desc;
  desc.dst = primary_buffer_;
  desc.size_bytes = size_bytes;
  desc.dst_offset = static_cast<std::uint64_t>(ref.element_) * stride_;
  return desc;
}

/*!
 Builds an UploadBufferDesc for a raw element index.

 @param element_index Element slot index (0-based) inside primary buffer.
 @return UploadBufferDesc for one element (stride bytes) on success;
   unexpected with std::errc::invalid_argument if buffer not yet created;
   std::errc::result_out_of_range if index >= current capacity.

 @see AtlasBuffer::MakeUploadDesc
*/
auto AtlasBuffer::MakeUploadDescForIndex(
  const std::uint32_t element_index) const
  -> std::expected<oxygen::vortex::upload::UploadBufferDesc, std::error_code>
{
  if (!primary_buffer_) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (element_index >= capacity_elements_) {
    return std::unexpected(
      std::make_error_code(std::errc::result_out_of_range));
  }

  oxygen::vortex::upload::UploadBufferDesc desc;
  desc.dst = primary_buffer_;
  desc.size_bytes = stride_;
  desc.dst_offset = static_cast<std::uint64_t>(element_index) * stride_;
  return desc;
}

} // namespace oxygen::vortex::upload
