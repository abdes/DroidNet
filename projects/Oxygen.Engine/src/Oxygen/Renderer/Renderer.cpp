//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/MeshAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Renderer.h>

using oxygen::data::MeshAsset;
using oxygen::engine::IEvictionPolicy;
using oxygen::engine::MeshGpuResources;
using oxygen::engine::MeshID;
using oxygen::engine::Renderer;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::RenderController;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::SingleQueueStrategy;

namespace {
auto GetMeshID(const MeshAsset& mesh) -> MeshID
{
  // Use pointer value or a unique mesh identifier if available
  return reinterpret_cast<MeshID>(&mesh);
}

//===----------------------------------------------------------------------===//
// LRU Eviction Policy Implementation
//===----------------------------------------------------------------------===//

class LruEvictionPolicy : public IEvictionPolicy {
public:
  static constexpr std::size_t kDefaultLruAge = 60; // frames
  explicit LruEvictionPolicy(std::size_t max_age_frames = kDefaultLruAge)
    : max_age_(max_age_frames)
  {
  }
  auto OnMeshAccess(MeshID id) -> void override
  {
    last_used_[id] = current_frame_;
  }
  auto SelectResourcesToEvict(
    const std::unordered_map<MeshID, MeshGpuResources>& currentResources,
    std::size_t currentFrame) -> std::vector<MeshID> override
  {
    current_frame_ = currentFrame;
    std::vector<MeshID> evict;
    for (const auto& id : currentResources | std::views::keys) {
      auto it = last_used_.find(id);
      if (it == last_used_.end() || currentFrame - it->second > max_age_) {
        evict.push_back(id);
      }
    }
    return evict;
  }
  auto OnMeshRemoved(MeshID id) -> void override { last_used_.erase(id); }

private:
  std::size_t current_frame_ = 0;
  std::size_t max_age_;
  std::unordered_map<MeshID, std::size_t> last_used_;
};

//===----------------------------------------------------------------------===//
// LRU Policy Factory
//===----------------------------------------------------------------------===//

auto MakeLruEvictionPolicy(std::size_t max_age_frames
  = LruEvictionPolicy::kDefaultLruAge) -> std::shared_ptr<IEvictionPolicy>
{
  return std::make_shared<LruEvictionPolicy>(max_age_frames);
}
} // namespace

//===----------------------------------------------------------------------===//
// Renderer Implementation
//===----------------------------------------------------------------------===//

Renderer::Renderer(std::weak_ptr<RenderController> render_controller,
  std::shared_ptr<IEvictionPolicy> eviction_policy)
  : render_controller_(std::move(render_controller))
  , eviction_policy_(
      eviction_policy ? std::move(eviction_policy) : MakeLruEvictionPolicy())
{
}

Renderer::~Renderer() { mesh_resources_.clear(); }

auto Renderer::GetVertexBuffer(const MeshAsset& mesh) -> std::shared_ptr<Buffer>
{
  return EnsureMeshResources(mesh).vertex_buffer;
}

auto Renderer::GetIndexBuffer(const MeshAsset& mesh) -> std::shared_ptr<Buffer>
{
  return EnsureMeshResources(mesh).index_buffer;
}

auto Renderer::UnregisterMesh(const MeshAsset& mesh) -> void
{
  const MeshID id = GetMeshID(mesh);
  const auto it = mesh_resources_.find(id);
  if (it != mesh_resources_.end()) {
    mesh_resources_.erase(it);
    if (eviction_policy_) {
      eviction_policy_->OnMeshRemoved(id);
    }
  }
}

auto Renderer::EvictUnusedMeshResources(std::size_t current_frame) -> void
{
  if (!eviction_policy_) {
    return;
  }
  const auto to_evict
    = eviction_policy_->SelectResourcesToEvict(mesh_resources_, current_frame);
  for (MeshID id : to_evict) {
    mesh_resources_.erase(id);
    eviction_policy_->OnMeshRemoved(id);
  }
}

namespace {
auto CreateVertexBuffer(const MeshAsset& mesh,
  RenderController& render_controller) -> std::shared_ptr<Buffer>
{
  DLOG_F(2, "Create vertex buffe");

  const auto& graphics = render_controller.GetGraphics();
  const auto vertices = mesh.Vertices();
  const BufferDesc vb_desc {
    .size_bytes = vertices.size() * sizeof(oxygen::data::Vertex),
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = mesh.Name() + ".VertexBuffer",
  };
  auto vertex_buffer = graphics.CreateBuffer(vb_desc);
  vertex_buffer->SetName(vb_desc.debug_name);
  render_controller.GetResourceRegistry().Register(vertex_buffer);
  return vertex_buffer;
}

auto CreateIndexBuffer(const MeshAsset& mesh,
  RenderController& render_controller) -> std::shared_ptr<Buffer>
{
  DLOG_F(2, "Create index buffer");

  const auto& graphics = render_controller.GetGraphics();
  const auto indices = mesh.Indices();
  const BufferDesc ib_desc {
    .size_bytes = indices.size() * sizeof(std::uint32_t),
    .usage = BufferUsage::kIndex,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = mesh.Name() + ".IndexBuffer",
  };
  auto index_buffer = graphics.CreateBuffer(ib_desc);
  index_buffer->SetName(ib_desc.debug_name);
  render_controller.GetResourceRegistry().Register(index_buffer);
  return index_buffer;
}

auto UploadVertexBuffer(const MeshAsset& mesh,
  RenderController& render_controller, Buffer& vertex_buffer,
  oxygen::graphics::CommandRecorder& recorder) -> void
{
  DLOG_F(2, "Upload vertex buffer");

  const auto& graphics = render_controller.GetGraphics();
  const auto vertices = mesh.Vertices();
  if (vertices.empty()) {
    return;
  }
  const BufferDesc upload_desc {
    .size_bytes = vertices.size() * sizeof(oxygen::data::Vertex),
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = mesh.Name() + ".VertexUploadBuffer",
  };
  auto upload_buffer = graphics.CreateBuffer(upload_desc);
  upload_buffer->SetName(upload_desc.debug_name);
  void* mapped = upload_buffer->Map();
  memcpy(mapped, vertices.data(), upload_desc.size_bytes);
  upload_buffer->UnMap();
  recorder.BeginTrackingResourceState(
    vertex_buffer, ResourceStates::kCommon, false);
  recorder.RequireResourceState(vertex_buffer, ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(
    vertex_buffer, 0, *upload_buffer, 0, upload_desc.size_bytes);
  recorder.RequireResourceState(vertex_buffer, ResourceStates::kVertexBuffer);
  recorder.FlushBarriers();

  // Keep the upload buffer alive until the command list is executed
  DeferredObjectRelease(
    upload_buffer, render_controller.GetPerFrameResourceManager());
}

auto UploadIndexBuffer(const MeshAsset& mesh,
  RenderController& render_controller, Buffer& index_buffer,
  oxygen::graphics::CommandRecorder& recorder) -> void
{
  DLOG_F(2, "Upload index buffer");

  const auto& graphics = render_controller.GetGraphics();
  const auto indices = mesh.Indices();
  if (indices.empty()) {
    return;
  }
  const BufferDesc upload_desc {
    .size_bytes = indices.size() * sizeof(std::uint32_t),
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = mesh.Name() + ".IndexUploadBuffer",
  };
  auto upload_buffer = graphics.CreateBuffer(upload_desc);
  upload_buffer->SetName(upload_desc.debug_name);
  void* mapped = upload_buffer->Map();
  memcpy(mapped, indices.data(), upload_desc.size_bytes);
  upload_buffer->UnMap();
  recorder.BeginTrackingResourceState(
    index_buffer, ResourceStates::kCommon, false);
  recorder.RequireResourceState(index_buffer, ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(
    index_buffer, 0, *upload_buffer, 0, upload_desc.size_bytes);
  recorder.RequireResourceState(index_buffer, ResourceStates::kIndexBuffer);
  recorder.FlushBarriers();

  // Keep the upload buffer alive until the command list is executed
  DeferredObjectRelease(
    upload_buffer, render_controller.GetPerFrameResourceManager());
}

} // namespace

auto Renderer::EnsureMeshResources(const MeshAsset& mesh) -> MeshGpuResources&
{
  MeshID id = GetMeshID(mesh);
  const auto it = mesh_resources_.find(id);
  if (it != mesh_resources_.end()) {
    if (eviction_policy_) {
      eviction_policy_->OnMeshAccess(id);
    }
    return it->second;
  }

  DLOG_SCOPE_FUNCTION(INFO);
  DLOG_F(INFO, "mesh: {}", mesh.Name());

  const auto render_controller = render_controller_.lock();
  if (!render_controller) {
    throw std::runtime_error(
      "RenderController expired in Renderer::EnsureMeshResources");
  }

  const auto vertex_buffer = CreateVertexBuffer(mesh, *render_controller);
  const auto index_buffer = CreateIndexBuffer(mesh, *render_controller);

  // Acquire a command recorder for uploading buffers. Will be immediately
  // submitted on destruction.
  {
    const auto recorder = render_controller->AcquireCommandRecorder(
      SingleQueueStrategy().GraphicsQueueName(), "MeshBufferUpload");
    UploadVertexBuffer(mesh, *render_controller, *vertex_buffer, *recorder);
    UploadIndexBuffer(mesh, *render_controller, *index_buffer, *recorder);
  }

  MeshGpuResources gpu;
  gpu.vertex_buffer = vertex_buffer;
  gpu.index_buffer = index_buffer;

  auto [ins, inserted] = mesh_resources_.emplace(id, std::move(gpu));
  if (eviction_policy_) {
    eviction_policy_->OnMeshAccess(id);
  }
  return ins->second;
}
