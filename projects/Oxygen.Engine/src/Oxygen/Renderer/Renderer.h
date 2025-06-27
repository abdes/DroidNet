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

class RenderContext;

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
  virtual void OnMeshAccess(MeshID id) = 0;
  virtual std::vector<MeshID> SelectResourcesToEvict(
    const std::unordered_map<MeshID, MeshGpuResources>& currentResources,
    std::size_t currentFrame)
    = 0;
  virtual void OnMeshRemoved(MeshID id) = 0;
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
  OXGN_RNDR_API std::shared_ptr<graphics::Buffer> GetVertexBuffer(
    const data::MeshAsset& mesh);

  //! Returns the index buffer for the given mesh, creating it if needed.
  OXGN_RNDR_API std::shared_ptr<graphics::Buffer> GetIndexBuffer(
    const data::MeshAsset& mesh);

  //! Explicitly unregisters a mesh and its GPU resources.
  OXGN_RNDR_API void UnregisterMesh(const data::MeshAsset& mesh);

  //! Evicts unused mesh resources according to the eviction policy.
  OXGN_RNDR_API void EvictUnusedMeshResources(std::size_t currentFrame);

  //! Executes a render graph coroutine with the given context.
  template <typename RenderGraphCoroutine>
  oxygen::co::Co<> ExecuteRenderGraph(
    RenderGraphCoroutine&& graphCoroutine, RenderContext& ctx)
  {
    co_await std::forward<RenderGraphCoroutine>(graphCoroutine)(ctx);
  }

private:
  MeshGpuResources& EnsureMeshResources(const data::MeshAsset& mesh);

  std::weak_ptr<graphics::RenderController> render_controller_;
  std::unordered_map<MeshID, MeshGpuResources> mesh_resources_;
  std::shared_ptr<IEvictionPolicy> eviction_policy_;
};

} // namespace oxygen::engine
