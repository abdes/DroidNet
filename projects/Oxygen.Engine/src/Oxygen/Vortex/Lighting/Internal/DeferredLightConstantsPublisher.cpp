//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightConstantsPublisher.h>
#include <Oxygen/Vortex/Lighting/Types/DeferredLightConstants.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>

namespace oxygen::vortex::lighting::internal {

DeferredLightConstantsPublisher::DeferredLightConstantsPublisher(
  std::weak_ptr<Graphics> graphics, upload::StagingProvider& staging,
  observer_ptr<upload::InlineTransfersCoordinator> transfers)
  : graphics_(std::move(graphics))
  , staging_(staging)
  , transfers_(transfers)
{
}

DeferredLightConstantsPublisher::~DeferredLightConstantsPublisher()
{
  for (auto& slot : slots_) {
    ResetSlot(slot);
  }
}

auto DeferredLightConstantsPublisher::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_slot_ = frame::kInvalidSlot;
  if (slot.get() >= slots_.size()) {
    return;
  }
  current_slot_ = slot;
  auto& storage = slots_.at(slot.get());
  // A repeated in-frame offscreen session must not retire queued readers.
  if (storage.sequence == sequence) {
    return;
  }
  // The renderer has waited for this slot's GPU consumers. Retain descriptors
  // for matching arena ranges, and retire batches unused on its previous use.
  for (std::size_t index = storage.used_batches; index < storage.batches.size();
    ++index) {
    ReleaseBatch(storage.batches[index]);
  }
  storage.batches.erase(
    storage.batches.begin() + storage.used_batches, storage.batches.end());
  storage.used_batches = 0U;
  storage.sequence = sequence;
}

auto DeferredLightConstantsPublisher::Publish(
  const std::span<const DeferredLightConstants> records)
  -> std::expected<std::vector<ShaderVisibleIndex>, upload::UploadError>
{
  using upload::UploadError;
  constexpr auto kStride = packing::kConstantBufferAlignment;
  static_assert(sizeof(DeferredLightConstants) <= kStride);
  if (current_slot_.get() >= slots_.size()
    || records.size()
      > (std::numeric_limits<std::size_t>::max() - (kStride - 1U)) / kStride) {
    return std::unexpected(UploadError::kInvalidRequest);
  }
  auto indices = std::vector<ShaderVisibleIndex> {};
  if (records.empty()) {
    return indices;
  }
  auto gfx = graphics_.lock();
  if (!gfx) {
    return std::unexpected(UploadError::kResourceAllocFailed);
  }
  const auto batch_bytes = records.size() * kStride;
  auto allocation = staging_.Allocate(SizeBytes { batch_bytes + kStride - 1U },
    "LightingService.DeferredLight.Constants");
  if (!allocation) {
    return std::unexpected(allocation.error());
  }
  auto batch = Batch {
    .allocation = std::move(*allocation), .views = {}, .indices = {}
  };
  try {
    auto& registry = gfx->GetResourceRegistry();
    if (!registry.Contains(batch.allocation.Buffer())) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
    const auto base = batch.allocation.Offset().get();
    const auto padding = (kStride - (base % kStride)) % kStride;
    if (base > std::numeric_limits<std::uint64_t>::max() - padding) {
      return std::unexpected(UploadError::kInvalidRequest);
    }
    auto memory = std::span(batch.allocation.Ptr(),
      static_cast<std::size_t>(batch.allocation.Size().get()));
    batch.aligned_offset = base + padding;
    auto& storage = slots_.at(current_slot_.get());
    auto& batches = storage.batches;
    // Copy current values even when the immutable CBV descriptions are reused.
    for (const auto& [index, record] : std::views::enumerate(records)) {
      const auto record_offset = static_cast<std::size_t>(index) * kStride;
      auto destination = memory.subspan(padding + record_offset, kStride);
      std::ranges::fill(destination, std::byte {});
      std::memcpy(destination.data(), &record, sizeof(DeferredLightConstants));
    }
    if (transfers_) {
      transfers_->NotifyInlineWrite(
        SizeBytes { batch_bytes }, "LightingService.DeferredLight.Constants");
    }
    const auto unused = batches.begin() + storage.used_batches;
    const auto reusable
      = std::find_if(unused, batches.end(), [&](const Batch& cached) {
          return &cached.allocation.Buffer() == &batch.allocation.Buffer()
            && cached.aligned_offset == batch.aligned_offset
            && cached.views.size() == records.size();
        });
    if (reusable != batches.end()) {
      indices = reusable->indices;
      std::iter_swap(unused, reusable);
      unused->allocation = std::move(batch.allocation);
      ++storage.used_batches;
      return indices;
    }
    // A larger/reordered view can overlap several old batch ranges. Those
    // unused descriptions must retire before registering the replacement.
    for (auto it = unused; it != batches.end(); ++it) {
      if (&it->allocation.Buffer() == &batch.allocation.Buffer()
        && it->aligned_offset < batch.aligned_offset + batch_bytes
        && batch.aligned_offset
          < it->aligned_offset + it->views.size() * kStride) {
        ReleaseBatch(*it);
      }
    }
    if (storage.used_batches < batches.size()) {
      ReleaseBatch(batches[storage.used_batches]);
    } else {
      batches.reserve(batches.size() + 1U);
    }
    batch.views.reserve(records.size());
    batch.indices.reserve(records.size());
    for (std::size_t index = 0U; index < records.size(); ++index) {
      const auto record_offset = index * kStride;
      auto description = graphics::BufferViewDescription {};
      description.view_type = graphics::ResourceViewType::kConstantBuffer;
      description.visibility = graphics::DescriptorVisibility::kShaderVisible;
      description.range = { base + padding + record_offset, kStride };
      auto& allocator = gfx->GetDescriptorAllocator();
      auto handle
        = allocator.AllocateRaw(description.view_type, description.visibility);
      if (!handle.IsValid()) {
        ReleaseBatch(batch);
        return std::unexpected(UploadError::kResourceAllocFailed);
      }
      const auto descriptor = allocator.GetShaderVisibleIndex(handle);
      auto view = registry.RegisterView(
        batch.allocation.Buffer(), std::move(handle), description);
      batch.views.push_back(view);
      if (!view->IsValid()) {
        ReleaseBatch(batch);
        return std::unexpected(UploadError::kResourceAllocFailed);
      }
      batch.indices.push_back(descriptor);
    }
    indices = batch.indices;
    if (storage.used_batches < batches.size()) {
      batches[storage.used_batches] = std::move(batch);
    } else {
      batches.push_back(std::move(batch));
    }
    ++storage.used_batches;
    return indices;
  } catch (const std::exception& error) {
    ReleaseBatch(batch);
    LOG_F(ERROR, "Deferred constant publication failed: {}", error.what());
    return std::unexpected(UploadError::kResourceAllocFailed);
  }
}

auto DeferredLightConstantsPublisher::ReleaseBatch(Batch& batch) noexcept
  -> void
{
  auto gfx = graphics_.lock();
  if (!gfx || batch.views.empty()) {
    batch.views.clear();
    batch.indices.clear();
    return;
  }
  try {
    auto& registry = gfx->GetResourceRegistry();
    if (registry.Contains(batch.allocation.Buffer())) {
      registry.UnRegisterViews(batch.allocation.Buffer(), batch.views);
    }
  } catch (const std::exception& error) {
    LOG_F(ERROR, "Deferred constant retirement failed: {}", error.what());
  }
  batch.views.clear();
  batch.indices.clear();
}

auto DeferredLightConstantsPublisher::ResetSlot(Slot& slot) noexcept -> void
{
  for (auto& batch : slot.batches) {
    ReleaseBatch(batch);
  }
  slot.batches.clear();
  slot.used_batches = 0U;
  slot.sequence.reset();
}

} // namespace oxygen::vortex::lighting::internal
