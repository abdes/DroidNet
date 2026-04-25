//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::internal {

//! Manages per-view, per-frame-slot `ViewConstants` buffers for root CBV
//! binding.
/*!
 This class provides a simple, dedicated solution for `ViewConstants`
 upload:

 - **Upload Heap**: Buffers are CPU-visible (Upload heap), persistently mapped.
 - **No GPU Copy**: Data is written directly by CPU, read by GPU over PCIe.
 - **Slot-Aware**: Pre-allocates buffers for frame_slots × views.
 - **Root CBV**: Exposes Buffer and GPU virtual address for root signature
 binding.
 - **No Fencing**: Relies on N-buffering (different buffer per frame slot).
 - **No SRV**: Unlike TransientStructuredBuffer, this is for direct CBV binding.

 Usage:
 1. Call OnFrameStart(slot) at frame start.
 2. Call GetOrCreateBuffer(view_id) to get buffer for current slot + view.
 3. Write ViewConstants to returned mapped pointer.
 4. Bind buffer's GPU virtual address as root CBV in render pass.
*/
class ViewConstantsManager {
public:
  struct BufferInfo {
    std::shared_ptr<graphics::Buffer> buffer;
    void* mapped_ptr { nullptr };
  };

  OXGN_VRTX_API ViewConstantsManager(
    observer_ptr<Graphics> gfx, std::uint32_t buffer_size);

  OXGN_VRTX_API ~ViewConstantsManager();

  //! Set active frame slot for upcoming allocations.
  OXGN_VRTX_API auto OnFrameStart(frame::Slot slot) -> void;

  //! Get or create buffer for current slot + view_id.
  OXGN_VRTX_API auto GetOrCreateBuffer(ViewId view_id) -> BufferInfo;

  //! Write provided ViewConstants snapshot into the per-slot per-view buffer.
  /*! Returns BufferInfo for convenience; logs and returns an empty BufferInfo
      on failure. This centralizes mapping/copying logic so callers don't need
      to perform raw memcpy operations. */
  OXGN_VRTX_API auto WriteViewConstants(
    ViewId view_id, const void* snapshot, std::size_t size_bytes) -> BufferInfo;

  //! Release all tracked buffers for the specified view across every frame slot.
  OXGN_VRTX_API auto RemoveView(ViewId view_id) -> void;

  //! Get current frame slot.
  [[nodiscard]] auto GetCurrentSlot() const noexcept -> frame::Slot
  {
    return current_slot_;
  }

  [[nodiscard]] auto GetTrackedBufferCount() const noexcept -> std::size_t
  {
    return buffers_.size();
  }

private:
  struct BufferKey {
    frame::Slot slot;
    ViewId view_id;

    auto operator==(const BufferKey& other) const noexcept -> bool
    {
      return slot == other.slot && view_id == other.view_id;
    }
  };

  struct BufferKeyHash {
    auto operator()(const BufferKey& key) const noexcept -> std::size_t
    {
      return std::hash<std::uint32_t> {}(key.slot.get())
        ^ (std::hash<std::uint64_t> {}(key.view_id.get()) << 1);
    }
  };

  auto ReleaseBuffer(BufferInfo& info) -> void;

  observer_ptr<Graphics> gfx_;
  std::uint32_t buffer_size_;
  frame::Slot current_slot_ { frame::kInvalidSlot };
  std::unordered_map<BufferKey, BufferInfo, BufferKeyHash> buffers_;
};

} // namespace oxygen::vortex::internal
