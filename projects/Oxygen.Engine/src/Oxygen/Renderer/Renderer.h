//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class RenderController;
} // namespace oxygen::graphics

namespace oxygen::data {
class MeshAsset;
} // namespace oxygen::data

namespace oxygen::engine {

struct RenderContext;

//! Holds GPU resources for a mesh asset.
struct MeshGpuResources {
  std::shared_ptr<graphics::Buffer> vertex_buffer;
  std::shared_ptr<graphics::Buffer> index_buffer;
  // Optionally, descriptor indices, views, etc.
};

using MeshID = std::size_t;

//! Interface for mesh resource eviction policy.
class IEvictionPolicy {
public:
  virtual ~IEvictionPolicy() = default;
  virtual auto OnMeshAccess(MeshID id) -> void = 0;
  virtual auto SelectResourcesToEvict(
    const std::unordered_map<MeshID, MeshGpuResources>& currentResources,
    std::size_t currentFrame) -> std::vector<MeshID>
    = 0;
  virtual auto OnMeshRemoved(MeshID id) -> void = 0;
};

//! Renderer: backend-agnostic, manages mesh-to-GPU resource mapping and
//! eviction.
class Renderer {
public:
  OXGN_RNDR_API Renderer(
    std::weak_ptr<graphics::RenderController> render_controller,
    std::shared_ptr<IEvictionPolicy> eviction_policy = nullptr);
  OXGN_RNDR_API ~Renderer();

  //! Returns the vertex buffer for the given mesh, creating it if needed.
  OXGN_RNDR_API auto GetVertexBuffer(const data::MeshAsset& mesh)
    -> std::shared_ptr<graphics::Buffer>;

  //! Returns the index buffer for the given mesh, creating it if needed.
  OXGN_RNDR_API auto GetIndexBuffer(const data::MeshAsset& mesh)
    -> std::shared_ptr<graphics::Buffer>;

  //! Explicitly unregisters a mesh and its GPU resources.
  OXGN_RNDR_API auto UnregisterMesh(const data::MeshAsset& mesh) -> void;

  //! Evicts unused mesh resources according to the eviction policy.
  OXGN_RNDR_API auto EvictUnusedMeshResources(std::size_t currentFrame) -> void;

  //! Executes a render graph coroutine with the given context.
  template <typename RenderGraphCoroutine>
  auto ExecuteRenderGraph(
    RenderGraphCoroutine&& graphCoroutine, RenderContext& ctx) -> co::Co<>
  {
    co_await std::forward<RenderGraphCoroutine>(graphCoroutine)(ctx);
  }

private:
  auto EnsureMeshResources(const data::MeshAsset& mesh) -> MeshGpuResources&;

  std::weak_ptr<graphics::RenderController> render_controller_;
  std::unordered_map<MeshID, MeshGpuResources> mesh_resources_;
  std::shared_ptr<IEvictionPolicy> eviction_policy_;
};

} // namespace oxygen::engine
