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

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Upload/AtlasBuffer.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::sceneprep {
class ScenePrepState; // fwd
}

namespace oxygen::renderer::resources {
class GeometryUploader; // fwd
class MaterialBinder; // fwd
}

namespace oxygen::data {
class Mesh; // fwd
}

namespace oxygen::renderer::resources {

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

 ### When to use ElementRef instead

 If a future design introduces handle-addressed or sparse updates for
 DrawMetadata, reintroduce per-element handles only together with a consumer
 indirection layer (handle -> element index/chunk). Until then, dense
 index-addressing is the intended contract for this buffer.
 */
class DrawMetadataEmitter {
public:
  OXGN_RNDR_API DrawMetadataEmitter(observer_ptr<Graphics> gfx,
    observer_ptr<engine::upload::UploadCoordinator> uploader,
    observer_ptr<engine::upload::StagingProvider> provider,
    observer_ptr<renderer::resources::GeometryUploader> geometry,
    observer_ptr<renderer::resources::MaterialBinder> materials) noexcept;

  OXYGEN_MAKE_NON_COPYABLE(DrawMetadataEmitter)
  OXYGEN_MAKE_NON_MOVABLE(DrawMetadataEmitter)

  OXGN_RNDR_API ~DrawMetadataEmitter();

  //! Start a new frame - must be called once per frame before any operations
  OXGN_RNDR_API auto OnFrameStart(
    renderer::RendererTag, oxygen::frame::Slot slot) -> void;

  //! Emits one DrawMetadata from a retained RenderItemData.
  OXGN_RNDR_API auto EmitDrawMetadata(
    const oxygen::engine::sceneprep::RenderItemData& item_data) -> void;

  //! Sorts the emitted draws and builds partition ranges by pass.
  OXGN_RNDR_API auto SortAndPartition() -> void;

  //! Ensure GPU resources exist and schedule upload if data changed.
  OXGN_RNDR_API auto EnsureFrameResources() -> void;

  //! Shader-visible SRV index for the draw metadata SRV.
  OXGN_RNDR_NDAPI auto GetDrawMetadataSrvIndex() const -> ShaderVisibleIndex;

  //! Returns draw metadata as byte span for PreparedSceneFrame integration.
  OXGN_RNDR_NDAPI auto GetDrawMetadataBytes() const noexcept
    -> std::span<const std::byte>;

  //! Returns partition ranges for pass-based rendering.
  OXGN_RNDR_NDAPI auto GetPartitions() const noexcept
    -> std::span<const oxygen::engine::PreparedSceneFrame::PartitionRange>;

private:
  struct SortingKey {
    oxygen::engine::PassMask pass_mask {};
    std::uint32_t material_index { 0 };
    ShaderVisibleIndex vb_srv {};
    ShaderVisibleIndex ib_srv {};
  };

  auto Cpu() noexcept -> std::vector<engine::DrawMetadata>& { return cpu_; }
  auto Cpu() const noexcept -> const std::vector<engine::DrawMetadata>&
  {
    return cpu_;
  }

  auto BuildSortingAndPartitions() -> void;

private:
  // Core state
  observer_ptr<Graphics> gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
  observer_ptr<engine::upload::StagingProvider> staging_provider_;
  observer_ptr<renderer::resources::GeometryUploader> geometry_uploader_;
  observer_ptr<renderer::resources::MaterialBinder> material_binder_;

  // CPU shadow storage and GPU atlas buffer for DrawMetadata
  std::vector<engine::DrawMetadata> cpu_;
  std::unique_ptr<engine::upload::AtlasBuffer> atlas_;

  // Sorting & partitions
  std::vector<SortingKey> keys_;
  std::vector<oxygen::engine::PreparedSceneFrame::PartitionRange> partitions_;

  // Telemetry
  std::chrono::microseconds last_sort_time_ { 0 };
  std::uint64_t last_order_hash_ { 0ULL };
  std::uint64_t last_pre_sort_hash_ { 0ULL };

  // Frame lifecycle
  bool frame_started_ { false };
  oxygen::frame::Slot current_frame_slot_ { oxygen::frame::kInvalidSlot };
  oxygen::frame::Slot last_frame_slot_ { oxygen::frame::kInvalidSlot };

  // Runtime statistics (telemetry)
  std::uint64_t frames_started_count_ { 0 };
  std::uint64_t total_emits_ { 0 };
  std::uint64_t sort_calls_count_ { 0 };
  std::uint64_t upload_operations_count_ { 0 };
  std::uint32_t peak_draws_ { 0 };
  std::uint32_t peak_partitions_ { 0 };

  // Runtime note: upload ticket tracking was intentionally kept out of the
  // emitter itself. Shutdown coordination for the upload subsystem must be
  // handled centrally by the UploadCoordinator/Tracker so we do not continue
  // to accept uploads during shutdown or rely on destructors to block.
};

} // namespace oxygen::renderer::resources
