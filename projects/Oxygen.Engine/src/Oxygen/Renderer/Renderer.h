//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Renderer/Detail/BindlessStructuredBuffer.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::scene {
class Scene;
}

namespace oxygen::graphics {
class Buffer;
} // namespace oxygen::graphics

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::engine {

namespace sceneprep {
  struct ScenePrepState;
  class ScenePrepPipeline;
} // namespace sceneprep

struct RenderContext;
struct MaterialConstants;

//! Renderer: backend-agnostic, manages mesh-to-GPU resource mapping and
//! eviction.
class Renderer : public EngineModule {
  OXYGEN_TYPED(Renderer)

public:
  OXGN_RNDR_API explicit Renderer(std::weak_ptr<Graphics> graphics);

  OXYGEN_MAKE_NON_COPYABLE(Renderer)
  OXYGEN_DEFAULT_MOVABLE(Renderer)

  OXGN_RNDR_API ~Renderer() override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "RendererModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    // This module must run last, after all modules that may contribute to the
    // frame context.
    return ModulePriority { kModulePriorityLowest };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask override
  {
    // Participate in frame start, transform propagation and command record.
    return MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kTransformPropagation, core::PhaseId::kCommandRecord>();
  }

  OXGN_RNDR_NDAPI auto OnFrameStart(FrameContext& context) -> void override;

  OXGN_RNDR_NDAPI auto OnTransformPropagation(FrameContext& context)
    -> co::Co<> override;

  // Submit deferred uploads and retire completed ones during command record.
  OXGN_RNDR_NDAPI auto OnCommandRecord(FrameContext& context)
    -> co::Co<> override;

  //! Executes a render graph coroutine with the given context.
  template <typename RenderGraphCoroutine>
  auto ExecuteRenderGraph(RenderGraphCoroutine&& graph_coroutine,
    RenderContext& context, const FrameContext& frame_context) -> co::Co<>
  {
    PreExecute(context, frame_context);
    co_await std::forward<RenderGraphCoroutine>(graph_coroutine)(context);
    PostExecute(context);
  }

  // Legacy AoS accessors removed (opaque_items_ no longer drives frame build).
  // Temporary: any external caller relying on OpaqueItems()/GetOpaqueItems()
  // must migrate to PreparedSceneFrame consumption.

  //! Returns the Graphics system used by this renderer.
  OXGN_RNDR_API auto GetGraphics() -> std::shared_ptr<Graphics>;

  //! Modify scene constants in-place via a user-provided mutator.
  /*!
    The mutator is invoked with a reference to the internal SceneConstants
    instance. Use the chainable SceneConstants setters inside the mutator so
    versioning and lazy snapshotting are preserved.
  */
  OXGN_RNDR_API auto ModifySceneConstants(
    const std::function<void(SceneConstants&)>& mutator) -> void;
  //! Returns the last set scene constants (undefined before first set).
  OXGN_RNDR_API auto GetSceneConstants() const -> const SceneConstants&;

  OXGN_RNDR_API auto SetDrawMetadata(const DrawMetadata& indices) -> void;
  OXGN_RNDR_API auto GetDrawMetadata() const -> const DrawMetadata&;

  //! Accessor for in-progress SoA frame snapshot (Task 6+). Returns an empty
  //! frame until finalization is wired.
  [[nodiscard]] auto GetPreparedFrame() const noexcept
    -> const PreparedSceneFrame&
  {
    return prepared_frame_;
  }

  // Material constants via bindless structured buffer (single element)
  OXGN_RNDR_API auto SetMaterialConstants(const MaterialConstants& constants)
    -> void;
  OXGN_RNDR_API auto GetMaterialConstants() const -> const MaterialConstants&;

  OXGN_RNDR_API auto BuildFrame(
    const View& view, const FrameContext& frame_context) -> std::size_t;

  OXGN_RNDR_API auto BuildFrame(const CameraView& camera_view,
    const FrameContext& frame_context) -> std::size_t;

private:
  OXGN_RNDR_API auto PreExecute(
    RenderContext& context, const FrameContext& frame_context) -> void;
  OXGN_RNDR_API auto PostExecute(RenderContext& context) -> void;

  //! Finalize SoA (wrapper around existing FinalizeScenePrepSoA) and log.
  auto FinalizeScenePrepPhase(sceneprep::ScenePrepState& prep_state) -> void;
  //! Update scene constants from resolved view matrices & camera state.
  auto UpdateSceneConstantsFromView(const View& view) -> void;
  //! Compute current finalized draw count (post-sort) from prepared frame.
  auto CurrentDrawCount() const noexcept -> std::size_t;

  //=== SoA Finalization Decomposition (Task 6+ incremental) ===============//
  //! Generate DrawMetadata & material constant arrays with dedupe + validate.
  auto GenerateDrawMetadata(const std::vector<std::size_t>& filtered,
    sceneprep::ScenePrepState& prep_state)
    -> void; // non-const: may register new materials (stable handle allocation)
  //! Build sorting keys, stable-sort, and construct partition ranges.
  auto BuildSortingAndPartitions()
    -> void; // builds sorting keys, sorts, partitions
  //! Publish spans into PreparedSceneFrame using TransformUploader data
  //! directly.
  auto PublishPreparedFrameSpans() -> void;
  //! Upload sorted DrawMetadata to bindless StructuredBuffer.
  auto UploadDrawMetadataBindless() -> void;
  //! Aggregate timing + counters and emit diagnostic partition logs.
  auto UpdateFinalizeStatistics(const sceneprep::ScenePrepState& prep_state,
    std::size_t filtered_count,
    std::chrono::high_resolution_clock::time_point t_begin) -> void;

  auto MaybeUpdateSceneConstants(const FrameContext& frame_context) -> void;

  auto UpdateDrawMetadataSlotIfChanged() -> void;

  auto EnsureAndUploadDrawMetadataBuffer() -> void;

  //! Wires updated buffers into the provided render context for the frame.
  auto WireContext(RenderContext& context) -> void;

  std::weak_ptr<Graphics> gfx_weak_; // New AsyncEngine path

  // Managed draw item container removed (AoS path deprecated).

  // Scene constants management
  std::shared_ptr<graphics::Buffer> scene_const_buffer_;
  SceneConstants scene_const_cpu_;
  MonotonicVersion last_uploaded_scene_const_version_ { (
    std::numeric_limits<uint64_t>::max)() };

  // Material constants (StructuredBuffer<MaterialConstants>)
  detail::BindlessStructuredBuffer<MaterialConstants> material_constants_;

  // Per-draw metadata buffer (StructuredBuffer<DrawMetadata>)
  detail::BindlessStructuredBuffer<DrawMetadata> draw_metadata_;

  //=== SoA Finalization (in-progress) ===----------------------------------//
  // CPU-owning storage populated during finalization each frame. Spans inside
  // PreparedSceneFrame alias these vectors (no ownership transfer).
  PreparedSceneFrame prepared_frame_ {}; // view object
  std::vector<DrawMetadata>
    draw_metadata_cpu_soa_; // SoA-built per-draw records
  // Partition map backing storage (pass mask -> [begin,end)) published via
  // PreparedSceneFrame spans each frame (Task 11 scaffolding).
  std::vector<PreparedSceneFrame::PartitionRange> partitions_cpu_soa_;
  // Sorting key scaffolding (Task 16 prep). One key per draw; not yet used to
  // reorder. Captures pass_mask + material + geometry indices (currently
  // placeholders) to ensure stable deterministic ordering hash.
  struct DrawSortingKey {
    PassMask pass_mask {}; // from DrawMetadata.flags
    // DrawMetadata.material_handle (stable MaterialHandle)
    uint32_t material_index { 0 };
    ShaderVisibleIndex
      geometry_vertex_srv {}; // DrawMetadata.vertex_buffer_index
    ShaderVisibleIndex geometry_index_srv {}; // DrawMetadata.index_buffer_index
  };
  std::vector<DrawSortingKey> sorting_keys_cpu_soa_;
  uint64_t last_draw_order_hash_ { 0ULL }; // FNV-1a over sequence of keys

  // High-water reservation to minimize realloc churn for SoA arrays.
  std::size_t max_finalized_count_ { 0 };

  struct FinalizeStats {
    std::size_t collected { 0 };
    std::size_t filtered { 0 };
    std::size_t finalized { 0 };
    std::chrono::microseconds collection_time { 0 };
    std::chrono::microseconds finalize_time { 0 };
  } last_finalize_stats_ {};

  // Microseconds spent performing draw sorting (stable sort + permutation +
  // partition range construction). Captured each frame FinalizeScenePrepSoA
  // runs. 0 when no draws.
  std::chrono::microseconds last_sort_time_ { 0 };

  // Populates SoA arrays from ScenePrep outputs (initial minimal subset).
  auto FinalizeScenePrepSoA(sceneprep::ScenePrepState& prep_state) -> void;

  // Persistent ScenePrep state (caches transforms/materials/geometry across
  // frames). ResetFrameData() is invoked each BuildFrame while retaining
  // deduplicated caches inside contained managers.
  sceneprep::ScenePrepState scene_prep_state_;
  std::unique_ptr<sceneprep::ScenePrepPipeline> scene_prep_pipeline_;

  // Frame sequence number from FrameContext
  frame::SequenceNumber frame_seq_num { 0ULL };

  // Upload coordinator: manages buffer/texture uploads and completion.
  std::unique_ptr<upload::UploadCoordinator> uploader_;
};

} // namespace oxygen::engine
