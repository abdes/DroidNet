//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::upload {

TransientStructuredBuffer::TransientStructuredBuffer(observer_ptr<Graphics> gfx,
  StagingProvider& staging, std::uint32_t stride,
  observer_ptr<InlineTransfersCoordinator> inline_transfers,
  std::string debug_label, const bindless::DomainToken domain)
  : gfx_(gfx)
  , staging_(&staging)
  , stride_(stride)
  , inline_transfers_(inline_transfers)
  , debug_label_(std::move(debug_label))
  , domain_(domain)
{
  DCHECK_NOTNULL_F(gfx_);
  if (debug_label_.empty()) {
    debug_label_ = "TransientStructuredBuffer";
  }
  // Release recycled-slot views at frame start, before any other buffer on
  // the same provider can allocate (and look up) the same ranges.
  frame_anchor_ = std::make_shared<FrameAnchor>(this);
  staging_->AddFrameListener(frame_anchor_);
}

TransientStructuredBuffer::TransientStructuredBuffer(
  TransientStructuredBuffer&& other) noexcept
  : gfx_(other.gfx_)
  , staging_(other.staging_)
  , stride_(other.stride_)
  , inline_transfers_(other.inline_transfers_)
  , debug_label_(std::move(other.debug_label_))
  , domain_(other.domain_)
  , current_slot_(std::exchange(other.current_slot_, frame::kInvalidSlot))
  , current_frame_(other.current_frame_)
  , slots_(std::move(other.slots_))
  , frame_anchor_(std::move(other.frame_anchor_))
{
  for (auto& slot : other.slots_) {
    slot.allocs.clear();
  }
  if (frame_anchor_) {
    frame_anchor_->Retarget(this);
  }
}

auto TransientStructuredBuffer::operator=(
  TransientStructuredBuffer&& other) noexcept -> TransientStructuredBuffer&
{
  if (this == &other) {
    return *this;
  }
  Reset();
  if (frame_anchor_) {
    frame_anchor_->Retarget(nullptr);
  }
  gfx_ = other.gfx_;
  staging_ = other.staging_;
  stride_ = other.stride_;
  inline_transfers_ = other.inline_transfers_;
  debug_label_ = std::move(other.debug_label_);
  domain_ = other.domain_;
  current_slot_ = std::exchange(other.current_slot_, frame::kInvalidSlot);
  current_frame_ = other.current_frame_;
  slots_ = std::move(other.slots_);
  for (auto& slot : other.slots_) {
    slot.allocs.clear();
  }
  frame_anchor_ = std::move(other.frame_anchor_);
  if (frame_anchor_) {
    frame_anchor_->Retarget(this);
  }
  return *this;
}

TransientStructuredBuffer::~TransientStructuredBuffer()
{
  if (frame_anchor_) {
    frame_anchor_->Retarget(nullptr);
  }
  Reset();
}

auto TransientStructuredBuffer::OnFrameStart(
  frame::SequenceNumber sequence, frame::Slot slot) -> void
{
  // Offscreen sessions can restart scene preparation within the same physical
  // frame. Earlier queued GPU readers still reference the published
  // descriptors.
  if (slot != frame::kInvalidSlot && current_slot_ == slot
    && current_frame_ == sequence) {
    return;
  }
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

  auto& slot = slots_.at(slot_index);

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

  // A structured SRV's byte offset must be a multiple of its stride, which the
  // shared staging ring does not guarantee.
  auto result = staging_->AllocateAligned(
    SizeBytes { size_bytes }, stride_, "TransientBuffer");
  if (!result) {
    auto ec = make_error_code(result.error());
    if (result.error() != UploadError::kBudgetExceeded) {
      LOG_F(
        ERROR, "{}: staging allocation failed: {}", debug_label_, ec.message());
    } else if (budget_warning_frame_ != current_frame_) {
      budget_warning_frame_ = current_frame_;
      LOG_F(WARNING,
        "{}: staging budget exhausted in frame {}; {} bytes and later "
        "requests this frame are rejected",
        debug_label_, current_frame_.get(), size_bytes);
    }
    return std::unexpected(ec);
  }

  auto allocation = std::move(*result);
  const auto u_aligned_offset = allocation.Offset().get();
  auto* const aligned_ptr = allocation.Ptr();

  if (inline_transfers_) {
    inline_transfers_->NotifyInlineWrite(
      SizeBytes { size_bytes }, debug_label_);
  }

  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  view_desc.range = { u_aligned_offset, size_bytes };
  view_desc.stride = stride_;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;

  DCHECK_F(gfx_->GetResourceRegistry().Contains(allocation.Buffer()),
    "Backing buffer (RingBufferStaging) not registered in ResourceRegistry!");
  try {
    auto& registry = gfx_->GetResourceRegistry();

    // A registered view for this exact range belongs to another buffer that
    // published it from the same recycled memory and has not released it.
    // Borrowing its index is unsafe: the owner frees it at its next reset and
    // the descriptor is immediately reissued for unrelated data, which shaders
    // then read as this buffer's contents.
    if (const auto existing_srv
      = registry.FindShaderVisibleIndex(allocation.Buffer(), view_desc);
      existing_srv.has_value()) {
      LOG_F(ERROR,
        "TransientStructuredBuffer '{}': range [{}, +{}) stride {} still has "
        "view {} from a previous use of this slot; its owner was not started "
        "for the frame (StagingProvider::NotifyFrameStart)",
        debug_label_, u_aligned_offset, size_bytes, stride_,
        existing_srv->get());
      return std::unexpected(make_error_code(UploadError::kInvalidRequest));
    }

    auto& allocator = gfx_->GetDescriptorAllocator();
    auto handle = allocator.AllocateBindless(
      domain_, graphics::ResourceViewType::kStructuredBuffer_SRV);

    if (!handle.IsValid()) {
      LOG_F(ERROR, "Descriptor allocation for transient upload buffer failed!");
      return std::unexpected(
        make_error_code(UploadError::kResourceAllocFailed));
    }

    // Register view and append a per-slot allocation record so multiple
    // allocations within the same frame slot are preserved until Next
    // OnFrameStart calls.
    try {
      const auto srv_index = allocator.GetShaderVisibleIndex(handle);
      auto native_view = registry.RegisterView(
        allocation.Buffer(), std::move(handle), view_desc);

      // Build allocation entry and append
      SlotAlloc alloc_entry {};
      alloc_entry.allocation = std::move(allocation);
      alloc_entry.srv_index = srv_index;
      alloc_entry.native_view = native_view;
      alloc_entry.sequence = current_frame_;
      slot.allocs.emplace_back(std::move(alloc_entry));

      TransientAllocation out {};
      out.srv = srv_index;
      out.mapped_ptr = aligned_ptr;
      out.size_bytes = size_bytes;
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
      return std::unexpected(make_error_code(UploadError::kStagingAllocFailed));
    }
  } catch (const std::exception& e) {
    LOG_F(
      ERROR, "TransientStructuredBuffer: Failed to create view: {}", e.what());
    return std::unexpected(make_error_code(UploadError::kStagingAllocFailed));
  }

  // Should have returned earlier after successful allocation.
}

auto TransientStructuredBuffer::GetActiveBuffer() const noexcept
  -> const graphics::Buffer*
{
  const auto* slot = ActiveSlot();
  if (slot == nullptr || slot->allocs.empty()) {
    return nullptr;
  }

  const auto& latest = slot->allocs.back();
  if (!latest.allocation.has_value()) {
    return nullptr;
  }

  return &latest.allocation->Buffer();
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
  auto& slot = slots_.at(slot_index);
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

  auto& registry = gfx_->GetResourceRegistry();
  if (!registry.Contains(slot.allocation->Buffer())) {
    // Shutdown/teardown case: the backing buffer and its views may already have
    // been removed from the registry. Avoid logging errors and just clear our
    // local bookkeeping.
    slot.srv_index = kInvalidShaderVisibleIndex;
    slot.native_view = {};
    return;
  }

  try {
    registry.UnRegisterView(slot.allocation->Buffer(), slot.native_view);
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
  return &slots_.at(slot_index);
}

auto TransientStructuredBuffer::ActiveSlot() noexcept -> SlotData*
{
  const auto slot_index = current_slot_.get();
  if (slot_index >= slots_.size()) {
    return nullptr;
  }
  return &slots_.at(slot_index);
}

} // namespace oxygen::vortex::upload
