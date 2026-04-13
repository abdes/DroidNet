//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Nexus/FrameDrivenSlotReuse.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::upload {
class InlineTransfersCoordinator;
}

namespace oxygen::vortex::resources {
class GeometryUploader; // fwd
class MaterialBinder; // fwd
}

namespace oxygen::data {
class Mesh; // fwd
}

namespace oxygen::vortex::resources {

//! Builds and uploads per-draw metadata using a dense, index-addressed SRV.
/*!
 Holds a CPU vector of DrawMetadata for the current frame, applies stable
 sorting and partitioning, then ensures an AtlasBuffer exists and uploads
 each record to the element with the same index as its sorted position
 (entry i is written to element i). The AtlasBuffer exposes a stable SRV
 across growth via ResourceRegistry::Replace.

 ### Key Points

 - **Dense, index-addressed uploads**: No per-element handle/indirection is
   used for DrawMetadata. The shader reads the structured buffer by index,
   so the emitter writes by index to match consumption exactly.
 - **No ElementRef management here**: We do not allocate or retire
   per-element references for DrawMetadata. This avoids allocator free-list
   permutations and guarantees that the CPU write order matches the GPU read
   order every frame.
 - **Stable SRV, per-frame content**: Capacity is ensured with minimal
   slack; content is fully rewritten each frame. UploadPlanner performs
   packing/coalescing; the emitter need not batch adjacent writes.
 - **Frame-driven slot indexing**: Per-frame draw slot indices are assigned by

 Nexus Strategy A (`FrameDrivenSlotReuse`) using deterministic emit order.
 This
 emitter intentionally uses allocation-only semantics (no per-draw

 release/reclaim) because slots are rewritten densely every frame.

 ### When to use ElementRef instead

 If a future design introduces handle-addressed or sparse updates for
 DrawMetadata, reintroduce per-element handles only together with a consumer
 indirection layer (handle -> element index/chunk). Until then, dense
 index-addressing is the intended contract for this buffer.
 */
class DrawMetadataEmitter {
public:
  OXGN_VRTX_API DrawMetadataEmitter(observer_ptr<Graphics> gfx,
    observer_ptr<vortex::upload::StagingProvider> provider,
    observer_ptr<vortex::resources::GeometryUploader> geometry,
    observer_ptr<vortex::resources::MaterialBinder> materials,
    observer_ptr<vortex::upload::InlineTransfersCoordinator>
      inline_transfers) noexcept;

  OXYGEN_MAKE_NON_COPYABLE(DrawMetadataEmitter)
  OXYGEN_MAKE_NON_MOVABLE(DrawMetadataEmitter)

  OXGN_VRTX_API ~DrawMetadataEmitter();

  //! Start a new frame - must be called once per frame before any operations
  OXGN_VRTX_API auto OnFrameStart(vortex::RendererTag,
    oxygen::frame::SequenceNumber sequence, oxygen::frame::Slot slot) -> void;

  //! Reset per-view emitted data while keeping frame-scoped GPU resources.
  /*!
   Multi-view scene prep finalizes one view at a time before rendering
   * begins.
   Each view therefore needs an isolated DrawMetadata payload and
   * SRV indices.
   This clears the current view's CPU-side emission state and
   * invalidates the
   cached SRV indices so the next view allocates/publishes
   * its own payload.
  */
  OXGN_VRTX_API auto ResetViewData() -> void;

  //! Emits one DrawMetadata from a retained RenderItemData.
  OXGN_VRTX_API auto EmitDrawMetadata(
    const oxygen::vortex::sceneprep::RenderItemData& item_data) -> void;

  //! Sorts the emitted draws and builds partition ranges by pass.
  OXGN_VRTX_API auto SortAndPartition() -> void;

  //! Ensure GPU resources exist and schedule upload if data changed.
  OXGN_VRTX_API auto EnsureFrameResources() -> void;

  //! Shader-visible SRV index for the draw metadata SRV.
  OXGN_VRTX_NDAPI auto GetDrawMetadataSrvIndex() -> ShaderVisibleIndex;

  //! Returns draw metadata as byte span for PreparedSceneFrame integration.
  OXGN_VRTX_NDAPI auto GetDrawMetadataBytes() const noexcept
    -> std::span<const std::byte>;

  //! Returns partition ranges for pass-based rendering.
  OXGN_VRTX_NDAPI auto GetPartitions() const noexcept
    -> std::span<const oxygen::vortex::PreparedSceneFrame::PartitionRange>;

  //! Returns one world-space bounding sphere per draw metadata record.
  OXGN_VRTX_NDAPI auto GetDrawBoundingSpheres() const noexcept
    -> std::span<const glm::vec4>;

  //! Shader-visible SRV index for the per-draw bounding-sphere buffer.
  OXGN_VRTX_NDAPI auto GetDrawBoundingSpheresSrvIndex() -> ShaderVisibleIndex;

  //! Shader-visible SRV index for the instance data buffer.
  /*!
   Returns the bindless SRV index for the per-instance transform index buffer.
   Used by shaders to look up per-instance transforms when instance_count > 1.
   Returns kInvalidShaderVisibleIndex if no instancing is active this frame.
  */
  OXGN_VRTX_NDAPI auto GetInstanceDataSrvIndex() const noexcept
    -> ShaderVisibleIndex;

private:
  //! Batching key for grouping identical draws for GPU instancing.
  /*!
   All fields must match for two draws to be batched together. The key includes
   geometry identity (vertex/index buffers, offsets) and material handle.
  */
  struct BatchingKey {
    ShaderVisibleIndex vertex_buffer_index {};
    ShaderVisibleIndex index_buffer_index {};
    std::uint32_t first_index { 0 };
    std::int32_t base_vertex { 0 };
    std::uint32_t material_handle { 0 };
    std::uint32_t index_count { 0 };
    std::uint32_t vertex_count { 0 };
    std::uint32_t is_indexed { 0 };
    oxygen::vortex::PassMask flags;

    [[nodiscard]] constexpr auto operator==(const BatchingKey&) const noexcept
      -> bool
      = default;
  };

  //! Hash functor for BatchingKey.
  struct BatchingKeyHash {
    [[nodiscard]] auto operator()(const BatchingKey& key) const noexcept
      -> std::size_t;
  };

  struct SortingKey {
    oxygen::vortex::PassMask pass_mask;
    std::uint8_t bucket_order { 0 };
    std::uint8_t _pad0 { 0 };
    std::uint16_t _pad1 { 0 };
    float sort_distance2 { 0.0F };
    std::uint32_t material_index { 0 };
    ShaderVisibleIndex vb_srv {};
    ShaderVisibleIndex ib_srv {};
  };

  auto Cpu() noexcept -> std::vector<vortex::DrawMetadata>& { return cpu_; }
  auto Cpu() const noexcept -> const std::vector<vortex::DrawMetadata>&
  {
    return cpu_;
  }

  auto BuildSortingAndPartitions() -> void;

  //! Applies GPU instancing by batching identical draws.
  /*!
   Groups draws with matching BatchingKey, collapses them into single draws
   with instance_count > 1, and populates the instance data buffer with
   per-instance transform indices.
  */
  auto ApplyInstancingBatches() -> void;

  // Core state
  observer_ptr<Graphics> gfx_;
  observer_ptr<vortex::resources::GeometryUploader> geometry_uploader_;
  observer_ptr<vortex::resources::MaterialBinder> material_binder_;
  observer_ptr<vortex::upload::StagingProvider> staging_provider_;
  observer_ptr<vortex::upload::InlineTransfersCoordinator> inline_transfers_;
  graphics::detail::DeferredReclaimer slot_reclaimer_;
  nexus::FrameDrivenSlotReuse slot_reuse_;
  std::uint32_t frame_write_count_ { 0U };

  // CPU shadow storage and transient GPU buffer for DrawMetadata
  std::vector<vortex::DrawMetadata> cpu_;
  vortex::upload::TransientStructuredBuffer draw_metadata_buffer_;
  ShaderVisibleIndex draw_metadata_srv_index_ { kInvalidShaderVisibleIndex };

  // Sorting & partitions
  std::vector<SortingKey> keys_;
  std::vector<oxygen::vortex::PreparedSceneFrame::PartitionRange> partitions_;
  std::vector<glm::vec4> draw_bounding_spheres_;
  vortex::upload::TransientStructuredBuffer draw_bounds_buffer_;
  ShaderVisibleIndex draw_bounds_srv_index_ { kInvalidShaderVisibleIndex };

  // GPU instancing: per-instance transform indices
  std::vector<std::uint32_t> instance_transform_indices_;
  vortex::upload::TransientStructuredBuffer instance_data_buffer_;
  ShaderVisibleIndex instance_data_srv_index_ { kInvalidShaderVisibleIndex };

  // Telemetry
  std::chrono::microseconds last_sort_time_ { 0 };
  std::uint64_t last_order_hash_ { 0ULL };
  std::uint64_t last_pre_sort_hash_ { 0ULL };

  // Frame lifecycle
  // Runtime statistics (telemetry)
  std::uint64_t frames_started_count_ { 0 };
  std::uint64_t sort_calls_count_ { 0 };
  std::uint32_t peak_draws_ { 0 };
  std::uint32_t peak_partitions_ { 0 };

  // Runtime note: upload ticket tracking was intentionally kept out of the
  // emitter itself. Shutdown coordination for the upload subsystem must be
  // handled centrally by the UploadCoordinator/Tracker so we do not continue
  // to accept uploads during shutdown or rely on destructors to block.
};

} // namespace oxygen::vortex::resources
