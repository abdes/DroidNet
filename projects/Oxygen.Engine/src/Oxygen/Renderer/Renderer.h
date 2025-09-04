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
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Renderer/api_export.h>
// Bindless structured buffer helper for per-draw arrays
#include <Oxygen/Renderer/Detail/BindlessStructuredBuffer.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h> // template config return type

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
  struct ScenePrepState; // forward for SoA finalization method
}

struct RenderContext;
struct MaterialConstants;

//! Holds GPU resources for a mesh asset.
struct MeshGpuResources {
  std::shared_ptr<graphics::Buffer> vertex_buffer;
  std::shared_ptr<graphics::Buffer> index_buffer;
  // Optionally, descriptor indices, views, etc.
  //! Shader-visible descriptor heap index for the vertex buffer SRV.
  uint32_t vertex_srv_index { 0 };
  //! Shader-visible descriptor heap index for the index buffer SRV.
  uint32_t index_srv_index { 0 };
};

using MeshId = std::size_t;

// TODO: Optimize the eviction so that only one traversal is needed.
//! Interface for mesh resource eviction policy.
class EvictionPolicy {
public:
  EvictionPolicy() = default;
  virtual ~EvictionPolicy() = default;

  OXYGEN_DEFAULT_COPYABLE(EvictionPolicy)
  OXYGEN_DEFAULT_MOVABLE(EvictionPolicy)

  virtual auto OnMeshAccess(MeshId id) -> void = 0;
  virtual auto SelectResourcesToEvict(
    const std::unordered_map<MeshId, MeshGpuResources>& current_resources,
    std::size_t current_frame) -> std::vector<MeshId>
    = 0;
  virtual auto OnMeshRemoved(MeshId id) -> void = 0;
};

//! Renderer: backend-agnostic, manages mesh-to-GPU resource mapping and
//! eviction.
class Renderer {
public:
  OXGN_RNDR_API explicit Renderer(std::weak_ptr<oxygen::Graphics> graphics,
    std::shared_ptr<EvictionPolicy> eviction_policy = nullptr);

  OXGN_RNDR_API ~Renderer();

  OXYGEN_MAKE_NON_COPYABLE(Renderer)
  OXYGEN_DEFAULT_MOVABLE(Renderer)

  //! Returns the vertex buffer for the given mesh, creating it if needed.
  OXGN_RNDR_API auto GetVertexBuffer(const data::Mesh& mesh)
    -> std::shared_ptr<graphics::Buffer>;

  //! Returns the index buffer for the given mesh, creating it if needed.
  OXGN_RNDR_API auto GetIndexBuffer(const data::Mesh& mesh)
    -> std::shared_ptr<graphics::Buffer>;

  //! Explicitly unregisters a mesh and its GPU resources.
  OXGN_RNDR_API auto UnregisterMesh(const data::Mesh& mesh) -> void;

  //! Evicts unused mesh resources according to the eviction policy.
  OXGN_RNDR_API auto EvictUnusedMeshResources(std::size_t current_frame)
    -> void;

  //! Executes a render graph coroutine with the given context.
  template <typename RenderGraphCoroutine>
  auto ExecuteRenderGraph(RenderGraphCoroutine&& graph_coroutine,
    RenderContext& context, const oxygen::engine::FrameContext& frame_context)
    -> co::Co<>
  {
    PreExecute(context, frame_context);
    co_await std::forward<RenderGraphCoroutine>(graph_coroutine)(context);
    PostExecute(context);
  }

  // Legacy AoS accessors removed (opaque_items_ no longer drives frame build).
  // Temporary: any external caller relying on OpaqueItems()/GetOpaqueItems()
  // must migrate to PreparedSceneFrame consumption.

  //! Returns the Graphics system used by this renderer.
  OXGN_RNDR_API auto GetGraphics() -> std::shared_ptr<oxygen::Graphics>;

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

  OXGN_RNDR_API auto SetDrawMetaData(const DrawMetadata& indices) -> void;
  OXGN_RNDR_API auto GetDrawMetaData() const -> const DrawMetadata&;

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

  OXGN_RNDR_API auto BuildFrame(const View& view,
    const oxygen::engine::FrameContext& frame_context) -> std::size_t;

  OXGN_RNDR_API auto BuildFrame(const CameraView& camera_view,
    const oxygen::engine::FrameContext& frame_context) -> std::size_t;

private:
  OXGN_RNDR_API auto PreExecute(RenderContext& context,
    const oxygen::engine::FrameContext& frame_context) -> void;
  OXGN_RNDR_API auto PostExecute(RenderContext& context) -> void;

  //=== FrameGraph Phase Helpers (PhaseId::kFrameGraph) -------------------//
  //! Extract scene pointer from FrameContext (defensive null check).
  auto ResolveScene(const oxygen::engine::FrameContext& frame_context)
    -> scene::Scene&;
  //! Initialize frame sequence number from FrameContext (PhaseId::kFrameGraph).
  auto InitializeFrameSequence(
    const oxygen::engine::FrameContext& frame_context) -> void;
  //! Prepare collection configuration (centralized future policy hook).
  using BasicCollectionConfig
    = decltype(sceneprep::CreateBasicCollectionConfig());
  auto PrepareScenePrepCollectionConfig() const -> BasicCollectionConfig;
  //! Run ScenePrep collection with timing + logging; fills prep_state.
  template <typename CollectionCfg>
  auto CollectScenePrep(scene::Scene& scene, const View& view,
    sceneprep::ScenePrepState& prep_state, const CollectionCfg& cfg) -> void;
  //! Finalize SoA (wrapper around existing FinalizeScenePrepSoA) and log.
  auto FinalizeScenePrepPhase(const sceneprep::ScenePrepState& prep_state)
    -> void;
  //! Update scene constants from resolved view matrices & camera state.
  auto UpdateSceneConstantsFromView(const View& view) -> void;
  //! Compute current finalized draw count (post-sort) from prepared frame.
  auto CurrentDrawCount() const noexcept -> std::size_t;

  //=== SoA Finalization Decomposition (Task 6+ incremental) ===============//
  // These helpers break down the large FinalizeScenePrepSoA routine into
  // focused steps while preserving original comments & ordering semantics.
  auto ResolveFilteredIndices(const sceneprep::ScenePrepState& prep_state)
    -> const std::vector<std::size_t>&;
  //! Reserve matrix storage (monotonic high-water) for world & normal arrays.
  auto ReserveMatrixStorage(std::size_t count) -> void;
  //! Populate world & normal matrix SoA arrays from filtered indices.
  auto PopulateMatrices(const std::vector<std::size_t>& filtered,
    const sceneprep::ScenePrepState& prep_state) -> void;
  //! Generate DrawMetadata & material constant arrays with dedupe + validate.
  auto GenerateDrawMetadataAndMaterials(
    const std::vector<std::size_t>& filtered,
    const sceneprep::ScenePrepState& prep_state) -> void;
  //! Mirror finalized world matrices into bindless StructuredBuffer.
  auto UploadWorldTransforms(std::size_t count) -> void;
  //! Build sorting keys, stable-sort, and construct partition ranges.
  auto BuildSortingAndPartitions()
    -> void; // builds sorting keys, sorts, partitions
  //! Publish spans into PreparedSceneFrame for downstream passes.
  auto PublishPreparedFrameSpans(std::size_t transform_count) -> void;
  //! Upload sorted DrawMetadata to bindless StructuredBuffer.
  auto UploadDrawMetadataBindless() -> void;
  //! Aggregate timing + counters and emit diagnostic partition logs.
  auto UpdateFinalizeStatistics(const sceneprep::ScenePrepState& prep_state,
    std::size_t filtered_count,
    std::chrono::high_resolution_clock::time_point t_begin) -> void;

  auto EnsureMeshResources(const data::Mesh& mesh) -> MeshGpuResources&;
  auto MaybeUpdateSceneConstants(
    const oxygen::engine::FrameContext& frame_context) -> void;

  auto UpdateBindlessMaterialConstantsSlotIfChanged() -> void;
  auto UpdateBindlessWorldsSlotIfChanged() -> void;
  auto UpdateDrawMetaDataSlotIfChanged() -> void;

  auto EnsureAndUploadDrawMetaDataBuffer() -> void;
  auto EnsureAndUploadMaterialConstants() -> void;
  auto EnsureAndUploadWorldTransforms() -> void;

  //! Wires updated buffers into the provided render context for the frame.
  auto WireContext(RenderContext& context) -> void;

  std::weak_ptr<oxygen::Graphics> graphics_; // New AsyncEngine path
  std::unordered_map<MeshId, MeshGpuResources> mesh_resources_;
  std::shared_ptr<EvictionPolicy> eviction_policy_;

  // Managed draw item container removed (AoS path deprecated).

  // Scene constants management
  std::shared_ptr<graphics::Buffer> scene_const_buffer_;
  SceneConstants scene_const_cpu_;
  MonotonicVersion last_uploaded_scene_const_version_ { (
    std::numeric_limits<uint64_t>::max)() };

  // Material constants (StructuredBuffer<MaterialConstants>)
  oxygen::engine::detail::BindlessStructuredBuffer<MaterialConstants>
    material_constants_;

  // Per-draw metadata buffer (StructuredBuffer<DrawMetadata>)
  oxygen::engine::detail::BindlessStructuredBuffer<DrawMetadata> draw_metadata_;

  // Per-draw world matrices buffer (StructuredBuffer<float4x4>)
  oxygen::engine::detail::BindlessStructuredBuffer<glm::mat4> world_transforms_;

  //=== SoA Finalization (in-progress) ===----------------------------------//
  // CPU-owning storage populated during finalization each frame. Spans inside
  // PreparedSceneFrame alias these vectors (no ownership transfer). Only world
  // & normal matrices for Task 6; draw metadata still built through legacy
  // path until later tasks migrate it fully.
  std::vector<float> world_matrices_cpu_; // 16 floats per draw
  std::vector<float> normal_matrices_cpu_; // 16 floats per draw
  PreparedSceneFrame prepared_frame_ {}; // view object
  std::vector<DrawMetadata>
    draw_metadata_cpu_soa_; // SoA-built per-draw records
  std::vector<MaterialConstants>
    material_constants_cpu_soa_; // SoA-built material constants parallel to
                                 // draw metadata
  // Partition map backing storage (pass mask -> [begin,end)) published via
  // PreparedSceneFrame spans each frame (Task 11 scaffolding).
  std::vector<PreparedSceneFrame::PartitionRange> partitions_cpu_soa_;
  // Sorting key scaffolding (Task 16 prep). One key per draw; not yet used to
  // reorder. Captures pass_mask + material + geometry indices (currently
  // placeholders) to ensure stable deterministic ordering hash.
  struct DrawSortingKey {
    uint32_t pass_mask { 0 }; // from DrawMetadata.flags
    uint32_t material_index { 0 }; // DrawMetadata.material_index
    uint32_t geometry_vertex_srv { 0 }; // DrawMetadata.vertex_buffer_index
    uint32_t geometry_index_srv { 0 }; // DrawMetadata.index_buffer_index
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
  auto FinalizeScenePrepSoA(const sceneprep::ScenePrepState& prep_state)
    -> void;

  // Frame sequence number from FrameContext
  frame::SequenceNumber frame_seq_num { 0ULL };
};

} // namespace oxygen::engine
