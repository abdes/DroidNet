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

auto TransientStructuredBuffer::OnFrameStart(frame::Slot slot) -> void
{
  current_slot_ = slot;
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
  -> std::expected<void, std::error_code>
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
  const bool had_allocation = slot.allocation.has_value();
  if (had_allocation || slot.native_view->IsValid()) {
    LOG_F(1,
      "TransientStructuredBuffer::Allocate releasing previous resources for "
      "slot {}",
      slot_index);
    ResetSlot(slot_index);
  }

  if (element_count == 0) {
    LOG_F(1, "TransientStructuredBuffer::Allocate skipped (slot={} count=0)",
      slot_index);
    return {};
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
    slot.srv_index = allocator.GetShaderVisibleIndex(handle);
    slot.native_view = gfx_->GetResourceRegistry().RegisterView(
      slot.allocation->Buffer(), std::move(handle), view_desc);
  } catch (const std::exception& e) {
    LOG_F(
      ERROR, "TransientStructuredBuffer: Failed to create view: {}", e.what());
    slot.allocation.reset();
    slot.srv_index = kInvalidShaderVisibleIndex;
    slot.native_view = {};
    return std::unexpected(make_error_code(UploadError::kStagingAllocFailed));
  }

  LOG_F(1,
    "TransientStructuredBuffer::Allocate slot={} bytes={} srv_index={} ptr={}",
    slot_index, size_bytes, slot.srv_index.get(),
    fmt::ptr(slot.allocation->Ptr()));

  return {};
}

auto TransientStructuredBuffer::Reset() -> void
{
  for (std::uint32_t i = 0; i < slots_.size(); ++i) {
    ResetSlot(i);
  }
  current_slot_ = frame::kInvalidSlot;
  LOG_F(1, "TransientStructuredBuffer::Reset cleared all slots");
}

auto TransientStructuredBuffer::ResetSlot(std::uint32_t slot_index) -> void
{
  if (slot_index >= slots_.size()) {
    return;
  }
  auto& slot = slots_[slot_index];
  ReleaseSlotView(slot);
  if (slot.allocation.has_value()) {
    LOG_F(1,
      "TransientStructuredBuffer::ResetSlot releasing allocation slot={}",
      slot_index);
  }
  slot.allocation.reset();
}

auto TransientStructuredBuffer::ReleaseSlotView(SlotData& slot) -> void
{
  if (!slot.native_view->IsValid()) {
    slot.srv_index = kInvalidShaderVisibleIndex;
    slot.native_view = {};
    return;
  }

  if (!slot.allocation.has_value()) {
    LOG_F(WARNING,
      "TransientStructuredBuffer::ReleaseSlotView view valid but no allocation;"
      " descriptor will be unregistered without buffer context");
    // We cannot unregister without buffer information, so just reset state to
    // avoid crashes. The descriptor allocator will clean up after lifetime.
    slot.srv_index = kInvalidShaderVisibleIndex;
    slot.native_view = {};
    return;
  }

  try {
    gfx_->GetResourceRegistry().UnRegisterView(
      slot.allocation->Buffer(), slot.native_view);
    LOG_F(1, "TransientStructuredBuffer::ReleaseSlotView released srv={}",
      slot.srv_index.get());
  } catch (const std::exception& e) {
    LOG_F(ERROR,
      "TransientStructuredBuffer::ReleaseSlotView failed to unregister view: "
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
