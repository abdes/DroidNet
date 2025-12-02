//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Upload/Errors.h>

namespace oxygen::engine::upload {

TransientStructuredBuffer::TransientStructuredBuffer(
  observer_ptr<Graphics> gfx, StagingProvider& staging, std::uint32_t stride)
  : gfx_(gfx)
  , staging_(&staging)
  , stride_(stride)
{
  DCHECK_NOTNULL_F(gfx_);
}

TransientStructuredBuffer::~TransientStructuredBuffer() { Reset(); }

auto TransientStructuredBuffer::Allocate(std::uint32_t element_count)
  -> std::expected<void, std::error_code>
{
  // Reset previous allocation if any
  Reset();

  if (element_count == 0) {
    return {};
  }

  const auto size_bytes = static_cast<std::uint64_t>(element_count) * stride_;

  // Allocate from staging (RingBuffer)
  // Note: StagingProvider::Allocate returns std::expected. We handle error by
  // check.
  auto result = staging_->Allocate(SizeBytes { size_bytes }, "TransientBuffer");
  if (!result) {
    auto ec = make_error_code(result.error());
    LOG_F(ERROR, "Allocation from staging buffer failed: {} (code {})",
      ec.message(), ec.value());
    return std::unexpected(ec);
  }
  allocation_ = std::move(*result);

  // Create Transient SRV
  auto& allocator = gfx_->GetDescriptorAllocator();
  // Allocate a descriptor handle
  // TODO: In the future, we might want a FrameDescriptorAllocator to avoid
  // fragmentation/overhead of the main allocator for transient things.
  // For now, the main allocator is fine as long as we Free() it.
  auto handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);

  if (!handle.IsValid()) {
    LOG_F(ERROR, "Descriptor allocation for transient upload buffer failed!");
    // Clean up staging allocation on failure to allocate descriptor.
    allocation_.reset();
    return std::unexpected(make_error_code(UploadError::kResourceAllocFailed));
  }

  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  view_desc.range = { allocation_->Offset().get(), allocation_->Size().get() };
  view_desc.stride = stride_;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;

  // Create the view on the underlying buffer.
  DCHECK_F(gfx_->GetResourceRegistry().Contains(allocation_->Buffer()),
    "Backing buffer (RingBufferStaging) not registered in ResourceRegistry!");
  try {
    // Query shader-visible index before transferring the handle to the registry
    srv_index_ = allocator.GetShaderVisibleIndex(handle);
    native_view_ = gfx_->GetResourceRegistry().RegisterView(
      allocation_->Buffer(), std::move(handle), view_desc);
  } catch (const std::exception& e) {
    LOG_F(
      ERROR, "TransientStructuredBuffer: Failed to create view: {}", e.what());
    // Attempt to release the staging allocation on failure and report error
    allocation_.reset();
    return std::unexpected(make_error_code(UploadError::kStagingAllocFailed));
  }

  return {};
}

auto TransientStructuredBuffer::Reset() -> void
{
  if (srv_index_ != kInvalidShaderVisibleIndex) {
    // Unregister the view from the resource registry so the descriptor handle
    // is released and index mapping is cleared. We need the original buffer
    // reference which is still available via `allocation_` until we reset it.
    try {
      if (native_view_->IsValid() && allocation_.has_value()) {
        gfx_->GetResourceRegistry().UnRegisterView(
          allocation_->Buffer(), native_view_);
      }
    } catch (const std::exception& e) {
      LOG_F(WARNING,
        "UnRegisterView failed while resetting transient upload buffer: {}",
        e.what());
      // We can simply report the error and continue. There is not much else we
      // can do here.
    }
    native_view_ = {};
    srv_index_ = kInvalidShaderVisibleIndex;
  }
  // Allocation memory is managed by StagingProvider (RingBuffer) and
  // auto-recycled. We just clear our pointer.
  allocation_.reset();
}

} // namespace oxygen::engine::upload
