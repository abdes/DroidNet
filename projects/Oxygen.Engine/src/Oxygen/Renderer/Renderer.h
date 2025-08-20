//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/RenderItemsList.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Renderer/api_export.h>
// Bindless structured buffer helper for per-draw arrays
#include <Oxygen/Renderer/Detail/BindlessStructuredBuffer.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen::graphics {
class Buffer;
class RenderController;
} // namespace oxygen::graphics

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::engine {

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
  OXGN_RNDR_API explicit Renderer(
    std::weak_ptr<graphics::RenderController> render_controller,
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

  //! Ensures GPU resources (buffers + SRVs) are resident for all meshes in the
  //! provided draw list. Must be called before ExecuteRenderGraph to guarantee
  //! residency before bindless indices upload.
  OXGN_RNDR_API auto EnsureResourcesForDrawList(
    std::span<const RenderItem> draw_list) -> void;

  //! Evicts unused mesh resources according to the eviction policy.
  OXGN_RNDR_API auto EvictUnusedMeshResources(std::size_t current_frame)
    -> void;

  //! Executes a render graph coroutine with the given context.
  template <typename RenderGraphCoroutine>
  auto ExecuteRenderGraph(
    RenderGraphCoroutine&& graph_coroutine, RenderContext& context) -> co::Co<>
  {
    PreExecute(context);
    co_await std::forward<RenderGraphCoroutine>(graph_coroutine)(context);
    PostExecute(context);
  }

  //! Access the opaque items container for construction/mutation.
  OXGN_RNDR_API auto OpaqueItems() -> RenderItemsList& { return opaque_items_; }
  //! Read-only span of opaque items for draw submission.
  OXGN_RNDR_API auto GetOpaqueItems() const -> std::span<const RenderItem>
  {
    return opaque_items_.Items();
  }

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

  // Material constants via bindless structured buffer (single element)
  OXGN_RNDR_API auto SetMaterialConstants(const MaterialConstants& constants)
    -> void;
  OXGN_RNDR_API auto GetMaterialConstants() const -> const MaterialConstants&;

  //! Build the frame draw list from a scene and a view.
  /*! Populates opaque items using CPU culling and updates scene constants. */
  OXGN_RNDR_API auto BuildFrame(oxygen::scene::Scene& scene, const View& view)
    -> std::size_t;

  //! Build the frame draw list from a scene and a camera view descriptor.
  /*! Ensures transforms are updated, resolves a per-frame View snapshot from
      the camera, then populates opaque items and updates scene constants. */
  OXGN_RNDR_API auto BuildFrame(
    oxygen::scene::Scene& scene, const CameraView& camera_view) -> std::size_t;

private:
  OXGN_RNDR_API auto PreExecute(RenderContext& context) -> void;
  OXGN_RNDR_API auto PostExecute(RenderContext& context) -> void;

  auto EnsureMeshResources(const data::Mesh& mesh) -> MeshGpuResources&;
  auto MaybeUpdateSceneConstants() -> void;

  auto UpdateBindlessMaterialConstantsSlotIfChanged() -> void;
  auto UpdateBindlessWorldsSlotIfChanged() -> void;
  auto UpdateDrawMetaDataSlotIfChanged() -> void;

  auto EnsureAndUploadDrawMetaDataBuffer() -> void;
  auto EnsureAndUploadMaterialConstants() -> void;
  auto EnsureAndUploadWorldTransforms() -> void;

  //! Wires updated buffers into the provided render context for the frame.
  auto WireContext(RenderContext& context) -> void;

  std::weak_ptr<graphics::RenderController> render_controller_;
  std::unordered_map<MeshId, MeshGpuResources> mesh_resources_;
  std::shared_ptr<EvictionPolicy> eviction_policy_;

  // Managed draw item container (Phase 3)
  RenderItemsList opaque_items_;

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

  // TODO: temporary - this should move out to the engine core
  frame::SequenceNumber frame_seq_num { 0ULL };
};

} // namespace oxygen::engine
