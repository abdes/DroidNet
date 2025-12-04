//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

#include <utility>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>

namespace oxygen::engine::upload {

TransientStructuredBuffer::TransientStructuredBuffer(observer_ptr<Graphics> gfx,
  StagingProvider& staging, std::uint32_t stride,
  observer_ptr<InlineTransfersCoordinator> inline_transfers,
  std::string debug_label)
  : gfx_(gfx)
  , staging_(&staging)
  , stride_(stride)
  , inline_transfers_(inline_transfers)
  , debug_label_(std::move(debug_label))
{
  DCHECK_NOTNULL_F(gfx_);
  if (debug_label_.empty()) {
    debug_label_ = "TransientStructuredBuffer";
  }
}

TransientStructuredBuffer::~TransientStructuredBuffer() { Reset(); }

auto TransientStructuredBuffer::OnFrameStart(
  frame::SequenceNumber sequence, frame::Slot slot) -> void
{
  current_slot_ = slot;
  current_frame_ = sequence;
  const auto slot_index = slot.get();
  if (slot_index >= slots_.size()) {
    LOG_F(ERROR,
      "TransientStructuredBuffer::OnFrameStart received invalid slot {}",
      slot_index);
    current_slot_ = frame::kInvalidSlot;
    return;
  }

  LOG_F(
    1, "TransientStructuredBuffer::OnFrameStart slot={} resetting", slot_index);
  ResetSlot(slot_index);
}

auto TransientStructuredBuffer::Allocate(std::uint32_t element_count)
  -> std::expected<TransientAllocation, std::error_code>
{
  if (current_slot_ == frame::kInvalidSlot) {
    LOG_F(ERROR,
      "TransientStructuredBuffer::Allocate called without a valid frame slot");
    return std::unexpected(make_error_code(UploadError::kInvalidRequest));
  }

  const auto slot_index = current_slot_.get();
  if (slot_index >= slots_.size()) {
    LOG_F(ERROR, "TransientStructuredBuffer::Allocate invalid slot index {}",
      slot_index);
    return std::unexpected(make_error_code(UploadError::kInvalidRequest));
  }

  auto& slot = slots_[slot_index];

  if (element_count == 0) {
    LOG_F(1, "TransientStructuredBuffer::Allocate skipped (slot={} count=0)",
      slot_index);
    // return an empty (invalid) transient allocation for callers to check
    TransientAllocation out {};
    out.sequence = current_frame_;
    out.slot = current_slot_;
    return out;
  }

  const auto size_bytes = static_cast<std::uint64_t>(element_count) * stride_;

  auto result = staging_->Allocate(SizeBytes { size_bytes }, "TransientBuffer");
  if (!result) {
    auto ec = make_error_code(result.error());
    LOG_F(ERROR, "Allocation from staging buffer failed: {} (code {})",
      ec.message(), ec.value());
    return std::unexpected(ec);
  }
  slot.allocation = std::move(*result);

  if (inline_transfers_) {
    inline_transfers_->NotifyInlineWrite(
      SizeBytes { size_bytes }, debug_label_);
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);

  if (!handle.IsValid()) {
    LOG_F(ERROR, "Descriptor allocation for transient upload buffer failed!");
    slot.allocation.reset();
    return std::unexpected(make_error_code(UploadError::kResourceAllocFailed));
  }

  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  view_desc.range
    = { slot.allocation->Offset().get(), slot.allocation->Size().get() };
  view_desc.stride = stride_;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;

  DCHECK_F(gfx_->GetResourceRegistry().Contains(slot.allocation->Buffer()),
    "Backing buffer (RingBufferStaging) not registered in ResourceRegistry!");
  try {
    // Register view and append a per-slot allocation record so multiple
    // allocations within the same frame slot are preserved until Next
    // OnFrameStart calls.
    try {
      const auto srv_index = allocator.GetShaderVisibleIndex(handle);
      auto native_view = gfx_->GetResourceRegistry().RegisterView(
        slot.allocation->Buffer(), std::move(handle), view_desc);

      // Build allocation entry and append
      SlotAlloc alloc_entry {};
      alloc_entry.allocation = std::move(slot.allocation);
      alloc_entry.srv_index = srv_index;
      alloc_entry.native_view = std::move(native_view);
      alloc_entry.sequence = current_frame_;
      slot.allocs.emplace_back(std::move(alloc_entry));

      TransientAllocation out {};
      out.srv = srv_index;
      out.mapped_ptr = slot.allocs.back().allocation->Ptr();
      out.sequence = current_frame_;
      out.slot = current_slot_;
      LOG_F(1,
        "TransientStructuredBuffer::Allocate slot={} bytes={} srv_index={} "
        "ptr={}",
        slot_index, size_bytes, out.srv.get(), fmt::ptr(out.mapped_ptr));
      return out;
    } catch (const std::exception& e) {
      LOG_F(ERROR, "TransientStructuredBuffer: Failed to create view: {}",
        e.what());
      slot.allocation.reset();
      return std::unexpected(make_error_code(UploadError::kStagingAllocFailed));
    }
  } catch (const std::exception& e) {
    LOG_F(
      ERROR, "TransientStructuredBuffer: Failed to create view: {}", e.what());
    slot.allocation.reset();
    slot.srv_index = kInvalidShaderVisibleIndex;
    slot.native_view = {};
    return std::unexpected(make_error_code(UploadError::kStagingAllocFailed));
  }

  // Should have returned earlier after successful allocation.
}

auto TransientStructuredBuffer::Reset() -> void
{
  for (std::uint32_t i = 0; i < slots_.size(); ++i) {
    ResetSlot(i);
  }
  current_slot_ = frame::kInvalidSlot;
  current_frame_ = frame::SequenceNumber { 0 };
  LOG_F(1, "TransientStructuredBuffer::Reset cleared all slots");
}

auto TransientStructuredBuffer::ResetSlot(std::uint32_t slot_index) -> void
{
  if (slot_index >= slots_.size()) {
    return;
  }
  auto& slot = slots_[slot_index];
  // Release all per-slot allocations & views
  for (auto& a : slot.allocs) {
    ReleaseAllocView(a);
    if (a.allocation.has_value()) {
      LOG_F(1,
        "TransientStructuredBuffer::ResetSlot releasing allocation slot={} "
        "srv={}",
        slot_index, a.srv_index.get());
    }
    a.allocation.reset();
  }
  slot.allocs.clear();
}

auto TransientStructuredBuffer::ReleaseSlotView(SlotData& slot) -> void
{
  // Deprecated single-view release path left for completeness. New multi-
  // allocation slot model uses ReleaseAllocView for each allocation entry.
  if (!slot.native_view->IsValid()) {
    return;
  }
  try {
    // If we have a stray single view, attempt unregister with the backing
    // buffer if available.
    if (slot.allocation.has_value()) {
      gfx_->GetResourceRegistry().UnRegisterView(
        slot.allocation->Buffer(), slot.native_view);
    }
  } catch (const std::exception& e) {
    LOG_F(ERROR,
      "TransientStructuredBuffer::ReleaseSlotView failed to unregister view: "
      "{}",
      e.what());
  }
}

auto TransientStructuredBuffer::ReleaseAllocView(SlotAlloc& slot) -> void
{
  if (!slot.native_view->IsValid()) {
    return;
  }

  if (!slot.allocation.has_value()) {
    LOG_F(WARNING,
      "TransientStructuredBuffer::ReleaseAllocView view valid but no "
      "allocation;"
      " descriptor will be unregistered without buffer context");
    slot.native_view = {};
    return;
  }

  try {
    gfx_->GetResourceRegistry().UnRegisterView(
      slot.allocation->Buffer(), slot.native_view);
    LOG_F(1, "TransientStructuredBuffer::ReleaseAllocView released srv={}",
      slot.srv_index.get());
  } catch (const std::exception& e) {
    LOG_F(ERROR,
      "TransientStructuredBuffer::ReleaseAllocView failed to unregister view: "
      "{}",
      e.what());
  }

  slot.srv_index = kInvalidShaderVisibleIndex;
  slot.native_view = {};
}

auto TransientStructuredBuffer::ActiveSlot() const noexcept -> SlotData const*
{
  const auto slot_index = current_slot_.get();
  if (slot_index >= slots_.size()) {
    return nullptr;
  }
  return &slots_[slot_index];
}

auto TransientStructuredBuffer::ActiveSlot() noexcept -> SlotData*
{
  const auto slot_index = current_slot_.get();
  if (slot_index >= slots_.size()) {
    return nullptr;
  }
  return &slots_[slot_index];
}

} // namespace oxygen::engine::upload
