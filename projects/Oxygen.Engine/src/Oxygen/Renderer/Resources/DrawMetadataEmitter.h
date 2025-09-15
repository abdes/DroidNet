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
#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/AtlasBuffer.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
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

//! Builds and uploads per-draw metadata using an AtlasBuffer.
/*!
 Holds a CPU vector of DrawMetadata for the current frame, applies stable
 sorting and partitioning, and uploads per-element into a persistent
 AtlasBuffer with a stable SRV. Elements are allocated once and retired by
 frame slot, mirroring TransformUploader/MaterialBinder patterns to ensure
 in-flight safety without over-allocating.
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
  std::unique_ptr<AtlasBuffer> atlas_;
  std::vector<AtlasBuffer::ElementRef> element_refs_;

  // Sorting & partitions
  std::vector<SortingKey> keys_;
  std::vector<oxygen::engine::PreparedSceneFrame::PartitionRange> partitions_;

  // Telemetry
  std::chrono::microseconds last_sort_time_ { 0 };
  std::uint64_t last_order_hash_ { 0ULL };

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
};

} // namespace oxygen::renderer::resources
