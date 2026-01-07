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
#include <Oxygen/Renderer/Types/EnvironmentDynamicData.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::internal {

//! Manages per-view, per-frame-slot EnvironmentDynamicData buffers for root CBV
//! binding at register b3.
/*!
 This class provides upload-heap management for EnvironmentDynamicData:

 - **Upload Heap**: Buffers are CPU-visible (Upload heap), persistently mapped.
 - **No GPU Copy**: Data is written directly by CPU, read by GPU over PCIe.
 - **Slot-Aware**: Pre-allocates buffers for frame_slots × views.
 - **Root CBV**: Exposes Buffer and GPU virtual address for root signature
   binding at b3.

 ### Cluster Buffer Wiring

 When LightCullingPass executes, it produces cluster grid and light index list
 buffers. The caller should populate EnvironmentDynamicData with:
 - `bindless_cluster_grid_slot` from
 `LightCullingPass::GetClusterGridSrvIndex()`
 - `bindless_cluster_index_list_slot` from
   `LightCullingPass::GetLightIndexListSrvIndex()`
 - Cluster dimensions and Z-binning params from `ClusterConfig`

 Usage:
 1. Call OnFrameStart(slot) at frame start.
 2. Populate EnvironmentDynamicData struct with current frame values.
 3. Call WriteEnvironmentData(view_id, data) to upload.
 4. Bind buffer's GPU virtual address as root CBV at b3.

 @see SceneConstantsManager, LightCullingPass, EnvironmentDynamicData
*/
class EnvironmentDynamicDataManager {
public:
  struct BufferInfo {
    std::shared_ptr<graphics::Buffer> buffer;
    void* mapped_ptr { nullptr };
  };

  OXGN_RNDR_API EnvironmentDynamicDataManager(observer_ptr<Graphics> gfx);

  OXGN_RNDR_API ~EnvironmentDynamicDataManager();

  //! Set active frame slot for upcoming allocations.
  OXGN_RNDR_API auto OnFrameStart(frame::Slot slot) -> void;

  //! Get or create buffer for current slot + view_id.
  OXGN_RNDR_API auto GetOrCreateBuffer(ViewId view_id) -> BufferInfo;

  //! Write EnvironmentDynamicData snapshot into the per-slot per-view buffer.
  /*!
   Returns BufferInfo for convenience; logs and returns an empty BufferInfo
   on failure.

   @param view_id The view identifier.
   @param data The environment data to upload.
   @return BufferInfo with buffer and mapped pointer.
  */
  OXGN_RNDR_API auto WriteEnvironmentData(
    ViewId view_id, const EnvironmentDynamicData& data) -> BufferInfo;

  //! Get current frame slot.
  [[nodiscard]] auto GetCurrentSlot() const noexcept -> frame::Slot
  {
    return current_slot_;
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

  observer_ptr<Graphics> gfx_;
  frame::Slot current_slot_ { frame::kInvalidSlot };
  std::unordered_map<BufferKey, BufferInfo, BufferKeyHash> buffers_;
};

} // namespace oxygen::engine::internal
