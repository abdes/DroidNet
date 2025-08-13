//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/MaterialConstants.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneConstants.h>

using oxygen::data::Mesh;
using oxygen::data::detail::IndexType;
using oxygen::engine::EvictionPolicy;
using oxygen::engine::MeshGpuResources;
using oxygen::engine::MeshId;
using oxygen::engine::Renderer;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::RenderController;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::SingleQueueStrategy;

namespace {
//! Returns a unique MeshId for the given mesh instance.
/*!
 Uses the address of the mesh object as the identifier.
*/
auto GetMeshId(const Mesh& mesh) -> MeshId
{
  return std::bit_cast<MeshId>(&mesh);
}

//===----------------------------------------------------------------------===//
// LRU Eviction Policy Implementation
//===----------------------------------------------------------------------===//

class LruEvictionPolicy : public EvictionPolicy {
public:
  static constexpr std::size_t kDefaultLruAge = 60; // frames
  explicit LruEvictionPolicy(std::size_t max_age_frames = kDefaultLruAge)
    : max_age_(max_age_frames)
  {
  }
  auto OnMeshAccess(MeshId id) -> void override
  {
    last_used_[id] = current_frame_;
  }
  auto SelectResourcesToEvict(
    const std::unordered_map<MeshId, MeshGpuResources>& currentResources,
    std::size_t currentFrame) -> std::vector<MeshId> override
  {
    current_frame_ = currentFrame;
    std::vector<MeshId> evict;
    for (const auto& id : currentResources | std::views::keys) {
      auto it = last_used_.find(id);
      if (it == last_used_.end() || currentFrame - it->second > max_age_) {
        evict.push_back(id);
      }
    }
    return evict;
  }
  auto OnMeshRemoved(MeshId id) -> void override { last_used_.erase(id); }

private:
  std::size_t current_frame_ = 0;
  std::size_t max_age_;
  std::unordered_map<MeshId, std::size_t> last_used_;
};

//===----------------------------------------------------------------------===//
// LRU Policy Factory
//===----------------------------------------------------------------------===//

auto MakeLruEvictionPolicy(std::size_t max_age_frames
  = LruEvictionPolicy::kDefaultLruAge) -> std::shared_ptr<EvictionPolicy>
{
  return std::make_shared<LruEvictionPolicy>(max_age_frames);
}
} // namespace

//===----------------------------------------------------------------------===//
// Renderer Implementation
//===----------------------------------------------------------------------===//

Renderer::Renderer(std::weak_ptr<RenderController> render_controller,
  std::shared_ptr<EvictionPolicy> eviction_policy)
  : render_controller_(std::move(render_controller))
  , eviction_policy_(
      eviction_policy ? std::move(eviction_policy) : MakeLruEvictionPolicy())
{
}

Renderer::~Renderer() { mesh_resources_.clear(); }

auto Renderer::GetVertexBuffer(const Mesh& mesh) -> std::shared_ptr<Buffer>
{
  return EnsureMeshResources(mesh).vertex_buffer;
}

auto Renderer::GetIndexBuffer(const Mesh& mesh) -> std::shared_ptr<Buffer>
{
  return EnsureMeshResources(mesh).index_buffer;
}

auto Renderer::UnregisterMesh(const Mesh& mesh) -> void
{
  const MeshId id = GetMeshId(mesh);
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
  for (MeshId id : to_evict) {
    mesh_resources_.erase(id);
    eviction_policy_->OnMeshRemoved(id);
  }
}
auto Renderer::PreExecute(RenderContext& context) -> void
{
  // Contract: caller must not populate context.scene_constants directly.
  DCHECK_F(!context.scene_constants,
    "RenderContext.scene_constants must be null; use "
    "Renderer::SetSceneConstants");
  DCHECK_NOTNULL_F(scene_constants_cpu_.get(),
    "Renderer::SetSceneConstants must be called before ExecuteRenderGraph");

  // Upload if dirty or buffer not yet created.
  if (!scene_constants_buffer_ || scene_constants_dirty_) {
    auto& rc = *render_controller_.lock();
    auto& graphics = rc.GetGraphics();
    const auto size_bytes = sizeof(SceneConstants);
    if (!scene_constants_buffer_) {
      graphics::BufferDesc desc { .size_bytes = size_bytes,
        .usage = graphics::BufferUsage::kConstant,
        .memory = graphics::BufferMemory::kUpload,
        .debug_name = std::string("SceneConstants") };
      scene_constants_buffer_ = graphics.CreateBuffer(desc);
      scene_constants_buffer_->SetName(desc.debug_name);
      rc.GetResourceRegistry().Register(scene_constants_buffer_);
    }
    void* mapped = scene_constants_buffer_->Map();
    memcpy(mapped, scene_constants_cpu_.get(), size_bytes);
    scene_constants_buffer_->UnMap();
    scene_constants_dirty_ = false;
  }
  // Inject buffer into context (const_cast due to interface design expecting
  // caller fill before).
  context.scene_constants = scene_constants_buffer_;
  // Material constants are optional; upload if provided and dirty.
  if (material_constants_cpu_) {
    if (!material_constants_buffer_ || material_constants_dirty_) {
      auto& rc = *render_controller_.lock();
      auto& graphics = rc.GetGraphics();
      const auto size_bytes = sizeof(MaterialConstants);
      if (!material_constants_buffer_) {
        graphics::BufferDesc desc { .size_bytes = size_bytes,
          .usage = graphics::BufferUsage::kConstant,
          .memory = graphics::BufferMemory::kUpload,
          .debug_name = std::string("MaterialConstants") };
        material_constants_buffer_ = graphics.CreateBuffer(desc);
        material_constants_buffer_->SetName(desc.debug_name);
        rc.GetResourceRegistry().Register(material_constants_buffer_);
      }
      void* mapped = material_constants_buffer_->Map();
      memcpy(mapped, material_constants_cpu_.get(), size_bytes);
      material_constants_buffer_->UnMap();
      material_constants_dirty_ = false;
    }
    context.material_constants = material_constants_buffer_;
  }
  context.SetRenderer(this, render_controller_.lock().get());
}
auto Renderer::PostExecute(RenderContext& context) -> void
{
  // RenderContext::Reset now clears per-frame injected buffers (scene &
  // material).
  context.Reset();
}

auto Renderer::SetMaterialConstants(const MaterialConstants& constants) -> void
{
  if (!material_constants_cpu_) {
    material_constants_cpu_ = std::make_unique<MaterialConstants>(constants);
    material_constants_dirty_ = true;
    return;
  }
  if (memcmp(
        material_constants_cpu_.get(), &constants, sizeof(MaterialConstants))
    != 0) {
    *material_constants_cpu_ = constants;
    material_constants_dirty_ = true;
  }
}

auto Renderer::GetMaterialConstants() const -> const MaterialConstants&
{
  return *material_constants_cpu_;
}

namespace {
auto CreateVertexBuffer(const Mesh& mesh, RenderController& render_controller)
  -> std::shared_ptr<Buffer>
{
  DLOG_F(2, "Create vertex buffe");

  const auto& graphics = render_controller.GetGraphics();
  const auto vertices = mesh.Vertices();
  const BufferDesc vb_desc {
    .size_bytes = vertices.size() * sizeof(oxygen::data::Vertex),
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = std::string(mesh.GetName()) + ".VertexBuffer",
  };
  auto vertex_buffer = graphics.CreateBuffer(vb_desc);
  vertex_buffer->SetName(vb_desc.debug_name);
  render_controller.GetResourceRegistry().Register(vertex_buffer);
  return vertex_buffer;
}

auto CreateIndexBuffer(const Mesh& mesh, RenderController& render_controller)
  -> std::shared_ptr<Buffer>
{
  DLOG_F(2, "Create index buffer");

  const auto& graphics = render_controller.GetGraphics();
  const auto indices_view = mesh.IndexBuffer();
  std::size_t element_size = 0U;
  if (indices_view.type == IndexType::kUInt16) {
    element_size = sizeof(std::uint16_t);
  } else if (indices_view.type == IndexType::kUInt32) {
    element_size = sizeof(std::uint32_t);
  }
  const auto index_count = indices_view.Count();
  const BufferDesc ib_desc {
    .size_bytes = index_count * element_size,
    .usage = BufferUsage::kIndex,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = std::string(mesh.GetName()) + ".IndexBuffer",
  };
  auto index_buffer = graphics.CreateBuffer(ib_desc);
  index_buffer->SetName(ib_desc.debug_name);
  render_controller.GetResourceRegistry().Register(index_buffer);
  return index_buffer;
}

auto UploadVertexBuffer(const Mesh& mesh, RenderController& render_controller,
  Buffer& vertex_buffer, oxygen::graphics::CommandRecorder& recorder) -> void
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
    .debug_name = std::string(mesh.GetName()) + ".VertexUploadBuffer",
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

auto UploadIndexBuffer(const Mesh& mesh, RenderController& render_controller,
  Buffer& index_buffer, oxygen::graphics::CommandRecorder& recorder) -> void
{
  DLOG_F(2, "Upload index buffer");

  const auto& graphics = render_controller.GetGraphics();
  const auto indices_view = mesh.IndexBuffer();
  if (indices_view.Count() == 0) {
    return;
  }
  const std::size_t element_size = indices_view.type == IndexType::kUInt16
    ? sizeof(std::uint16_t)
    : sizeof(std::uint32_t);
  const BufferDesc upload_desc {
    .size_bytes = indices_view.Count() * element_size,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = std::string(mesh.GetName()) + ".IndexUploadBuffer",
  };
  auto upload_buffer = graphics.CreateBuffer(upload_desc);
  upload_buffer->SetName(upload_desc.debug_name);
  void* mapped = upload_buffer->Map();
  if (indices_view.type == IndexType::kUInt16) {
    auto src = indices_view.AsU16();
    memcpy(mapped, src.data(), upload_desc.size_bytes);
  } else if (indices_view.type == IndexType::kUInt32) {
    auto src = indices_view.AsU32();
    memcpy(mapped, src.data(), upload_desc.size_bytes);
  }
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

auto Renderer::EnsureMeshResources(const Mesh& mesh) -> MeshGpuResources&
{
  MeshId id = GetMeshId(mesh);
  const auto it = mesh_resources_.find(id);
  if (it != mesh_resources_.end()) {
    if (eviction_policy_) {
      eviction_policy_->OnMeshAccess(id);
    }
    return it->second;
  }

  DLOG_SCOPE_FUNCTION(INFO);
  DLOG_F(INFO, "mesh: {}", mesh.GetName());

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

auto Renderer::SetSceneConstants(const SceneConstants& constants) -> void
{
  if (!scene_constants_cpu_) {
    scene_constants_cpu_ = std::make_unique<SceneConstants>(constants);
    scene_constants_dirty_ = true;
    return;
  }
  *scene_constants_cpu_ = constants;
  scene_constants_dirty_ = true;
}

auto Renderer::GetSceneConstants() const -> const SceneConstants&
{
  DCHECK_NOTNULL_F(scene_constants_cpu_.get());
  return *scene_constants_cpu_;
}
