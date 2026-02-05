//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <glm/vec3.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Types/EnvironmentDynamicData.h>
#include <Oxygen/Renderer/Types/SunState.h>
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
  OXGN_RNDR_API explicit EnvironmentDynamicDataManager(
    observer_ptr<Graphics> gfx);

  OXGN_RNDR_API ~EnvironmentDynamicDataManager();

  OXYGEN_MAKE_NON_COPYABLE(EnvironmentDynamicDataManager)
  OXYGEN_DEFAULT_MOVABLE(EnvironmentDynamicDataManager)

  //! Set active frame slot for upcoming allocations.
  OXGN_RNDR_API auto OnFrameStart(frame::Slot slot) -> void;

  //! Resolve data and upload to GPU if dirty for the current frame slot.
  /*!
   This call is idempotent for a given view within the same frame slot.
   Subsequent calls will only perform an upload if data has been updated
   via setters.
  */
  OXGN_RNDR_API auto UpdateIfNeeded(ViewId view_id) -> void;

  //! Get the GPU virtual address for the current slot's buffer for a view.
  OXGN_RNDR_NDAPI auto GetGpuVirtualAddress(ViewId view_id) -> uint64_t;

  //! Get the buffer for the current slot for a view.
  OXGN_RNDR_NDAPI auto GetBuffer(ViewId view_id)
    -> std::shared_ptr<graphics::Buffer>;

  //! Set exposure for a specific view.
  OXGN_RNDR_API auto SetExposure(ViewId view_id, float exposure) -> void;

  //! Get exposure for a specific view (defaults to 1.0).
  OXGN_RNDR_NDAPI auto GetExposure(ViewId view_id) const -> float;

  //! Set clustered culling configuration for a specific view.

  //! Structured setter for light culling data.
  OXGN_RNDR_API auto SetLightCullingData(
    ViewId view_id, const LightCullingData& data) -> void;

  //! Set Z-binning parameters for a specific view.
  OXGN_RNDR_API auto SetZBinning(ViewId view_id, float z_near, float z_far,
    float z_scale, float z_bias) -> void;

  //! Set the designated sun state for a view.
  OXGN_RNDR_API auto SetSunState(ViewId view_id, const SunState& sun) -> void;

  //! Set aerial perspective strength/scales for SkyAtmosphere.
  OXGN_RNDR_API auto SetAtmosphereScattering(ViewId view_id,
    float aerial_distance_scale, float aerial_scattering_strength) -> void;

  //! Set per-frame planet and camera context for atmosphere sampling.
  //! Note: planet_radius is NOT passed here - it's in EnvironmentStaticData.
  OXGN_RNDR_API auto SetAtmosphereFrameContext(ViewId view_id,
    const glm::vec3& planet_center_ws, const glm::vec3& planet_up_ws,
    float camera_altitude_m, float sky_view_lut_slice,
    float planet_to_sun_cos_zenith) -> void;

  //! Set debug/feature flags for the atmospheric pipeline.
  OXGN_RNDR_API auto SetAtmosphereFlags(
    ViewId view_id, uint32_t atmosphere_flags) -> void;

  //! Set optional override sun values for atmosphere debugging.
  OXGN_RNDR_API auto SetAtmosphereSunOverride(
    ViewId view_id, const SunState& sun) -> void;

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
      std::size_t h = 0;
      oxygen::HashCombine(h, key.slot);
      oxygen::HashCombine(h, key.view_id);
      return h;
    }
  };

  struct BufferInfo {
    std::shared_ptr<graphics::Buffer> buffer;
    void* mapped_ptr { nullptr };
  };

  struct ViewState {
    EnvironmentDynamicData data {};
    std::array<bool, frame::kFramesInFlight.get()> slot_dirty_ {};
  };

  observer_ptr<Graphics> gfx_;
  frame::Slot current_slot_ { frame::kInvalidSlot };

  std::unordered_map<ViewId, ViewState> view_states_;
  std::unordered_map<BufferKey, BufferInfo, BufferKeyHash> buffers_;

  auto GetOrCreateBuffer(ViewId view_id) -> BufferInfo;

  //! Mark all frame slots dirty for a view, forced an update on next access.
  /*!
   Used when canonical CPU state changes, ensuring all GPU buffers in the
   ring-buffer rotation eventually receive the update.
  */
  auto MarkAllSlotsDirty(ViewId view_id) -> void;
};

} // namespace oxygen::engine::internal
