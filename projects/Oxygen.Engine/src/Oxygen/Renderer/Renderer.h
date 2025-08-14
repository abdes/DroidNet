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

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/RenderItemsList.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/api_export.h>

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

//! Provides shader-visible indices for current vertex/index buffers (Phase 1
//! transitional).
struct DrawResourceIndices {
  uint32_t vertex_buffer_index;
  uint32_t index_buffer_index;
  uint32_t is_indexed; // 1 if indexed draw, 0 otherwise
};
// Expected packed size of DrawResourceIndices in bytes (3 x uint32_t) as per
// the shaders.
static_assert(sizeof(DrawResourceIndices) == 3U * sizeof(std::uint32_t),
  "Unexpected DrawResourceIndices size (packing change?)");

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

  //! Sets per-material constants snapshot (optional each frame before execute).
  //! If never called in a frame, material constants will not be bound.
  OXGN_RNDR_API auto SetMaterialConstants(const MaterialConstants& constants)
    -> void;
  //! Returns last set material constants (undefined before first set).
  OXGN_RNDR_API auto GetMaterialConstants() const -> const MaterialConstants&;

  //! Sets the per-frame draw resource indices snapshot (transitional API).
  /*!\n   Provides the shader-visible descriptor heap indices for the
   * currently\n   selected vertex & index buffers and whether the draw is
   * indexed. This is a\n   Phase 1–2 migration aid; later phases (mesh resource
   * & packet build) will\n   derive per-item indices automatically. Must be
   * called before the first\n   ExecuteRenderGraph in a frame if geometry is
   * rendered. Last call wins.\n  */
  OXGN_RNDR_API auto SetDrawResourceIndices(const DrawResourceIndices& indices)
    -> void;
  //! Returns last set draw resource indices snapshot (undefined until set).
  OXGN_RNDR_API auto GetDrawResourceIndices() const
    -> const DrawResourceIndices&;

private:
  OXGN_RNDR_API auto PreExecute(RenderContext& context) -> void;
  OXGN_RNDR_API auto PostExecute(RenderContext& context) -> void;

  //! Ensures GPU buffers and SRVs for a mesh are created/uploaded/registered.
  //! Returns the cached GPU resources and shader-visible SRV indices.
  auto EnsureMeshResources(const data::Mesh& mesh) -> MeshGpuResources&;

  //! PreExecute helpers broken out to reduce cyclomatic complexity. Kept
  //! private to avoid API surface changes.
  //! Ensures the draw resource indices buffer, descriptor & upload if dirty.
  auto EnsureAndUploadDrawResourceIndices() -> void;
  //! Updates the scene constants GPU buffer if dirty / uninitialized.
  auto MaybeUpdateSceneConstants() -> void;
  //! Updates the material constants GPU buffer if present & dirty.
  auto MaybeUpdateMaterialConstants() -> void;
  //! Applies any slot changes for draw resource indices into scene constants
  //! (sets dirty flag when changed).
  auto UpdateDrawResourceIndicesSlotIfChanged() -> void;
  //! Wires updated buffers into the provided render context for the frame.
  auto WireContext(RenderContext& context) -> void;

  // Internal helpers for DrawResourceIndices buffer lifecycle
  auto CreateOrResizeDrawIndicesBuffer(
    std::size_t size_bytes, graphics::RenderController& rc) -> void;
  auto RegisterDrawIndicesSrv(graphics::RenderController& rc) -> void;
  auto UploadDrawIndices(const void* src, std::size_t size_bytes) const -> void;

  std::weak_ptr<graphics::RenderController> render_controller_;
  std::unordered_map<MeshId, MeshGpuResources> mesh_resources_;
  std::shared_ptr<EvictionPolicy> eviction_policy_;

  // Managed draw item container (Phase 3)
  RenderItemsList opaque_items_;

  // Scene constants management
  std::shared_ptr<graphics::Buffer> scene_const_buffer_;
  SceneConstants scene_const_cpu_;
  // Use SceneConstants' internal versioning to detect changes instead of a
  // separate dirty flag. `last_uploaded_scene_const_version_` records
  // the version that was last uploaded to the GPU. A sentinel max value
  // forces the first upload.
  MonotonicVersion last_uploaded_scene_const_version_ { (
    std::numeric_limits<uint64_t>::max)() };

  // Material constants management
  std::shared_ptr<graphics::Buffer> material_const_buffer_;
  std::unique_ptr<MaterialConstants> material_const_cpu_;
  bool material_const_dirty_ { false };

  // Draw resource indices management (bindless vertex/index SRV indices).
  // Structured buffer now holds a per-draw array of DrawResourceIndices.
  // The descriptor heap slot is dynamic; SceneConstants carries the slot each
  // frame (bindless_indices_slot) instead of assuming 0.
  std::vector<DrawResourceIndices> bindless_indices_cpu_;
  std::shared_ptr<graphics::Buffer> bindless_indices_buffer_;
  bool bindless_indices_dirty_ { false };
  uint32_t bindless_indices_heap_slot_ { 0 };
  bool bindless_indices_slot_assigned_ { false };

  // Internal ensure that optionally updates the per-frame indices snapshot.
};

} // namespace oxygen::engine
