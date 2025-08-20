//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Extraction/RenderListBuilder.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::data::Mesh;
using oxygen::data::detail::IndexType;
using oxygen::engine::EvictionPolicy;
using oxygen::engine::MaterialConstants;
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

// Convert a data::MaterialAsset snapshot into engine::MaterialConstants
auto MakeMaterialConstants(const oxygen::data::MaterialAsset& mat)
  -> MaterialConstants
{
  MaterialConstants mc;
  const auto base = mat.GetBaseColor();
  mc.base_color = { base[0], base[1], base[2], base[3] };
  mc.metalness = mat.GetMetalness();
  mc.roughness = mat.GetRoughness();
  mc.normal_scale = mat.GetNormalScale();
  mc.ambient_occlusion = mat.GetAmbientOcclusion();
  // Texture indices (bindless). If not wired yet, keep zero which shaders can
  // treat as "no texture".
  mc.base_color_texture_index = mat.GetBaseColorTexture();
  mc.normal_texture_index = mat.GetNormalTexture();
  mc.metallic_texture_index = mat.GetMetallicTexture();
  mc.roughness_texture_index = mat.GetRoughnessTexture();
  mc.ambient_occlusion_texture_index = mat.GetAmbientOcclusionTexture();
  // Flags reserved for future use; leave zero for now.
  return mc;
}

//===----------------------------------------------------------------------===//
// LRU Eviction Policy Implementation
//===----------------------------------------------------------------------===//

class LruEvictionPolicy : public EvictionPolicy {
public:
  static constexpr std::size_t kDefaultLruAge = 60; // frames
  explicit LruEvictionPolicy(const std::size_t max_age_frames = kDefaultLruAge)
    : max_age_(max_age_frames)
  {
  }
  auto OnMeshAccess(const MeshId id) -> void override
  {
    last_used_[id] = current_frame_;
  }
  auto SelectResourcesToEvict(
    const std::unordered_map<MeshId, MeshGpuResources>& currentResources,
    const std::size_t currentFrame) -> std::vector<MeshId> override
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
  auto OnMeshRemoved(const MeshId id) -> void override { last_used_.erase(id); }

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

Renderer::~Renderer() = default;

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

auto Renderer::EvictUnusedMeshResources(const std::size_t current_frame) -> void
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
  // Contract checks (kept inline per style preference)
  DCHECK_F(!context.scene_constants,
    "RenderContext.scene_constants must be null; renderer sets it via "
    "ModifySceneConstants/PreExecute");

  // Phase 3: Provide opaque draw list from managed container and ensure
  // required GPU resources are resident before binding/upload steps.
  context.opaque_draw_list = opaque_items_.Items();
  const auto container_span = opaque_items_.Items();
  DCHECK_F(context.opaque_draw_list.data() == container_span.data()
      && context.opaque_draw_list.size() == container_span.size(),
    "RenderContext.opaque_draw_list must alias the renderer's container span");
  if (!context.opaque_draw_list.empty()) {
    EnsureResourcesForDrawList(context.opaque_draw_list);
  }

  EnsureAndUploadDrawMetaDataBuffer();
  UpdateDrawMetaDataSlotIfChanged();

  EnsureAndUploadWorldTransforms();
  UpdateBindlessWorldsSlotIfChanged();

  EnsureAndUploadMaterialConstants();
  UpdateBindlessMaterialConstantsSlotIfChanged();

  MaybeUpdateSceneConstants();

  WireContext(context);
}

// ReSharper disable once CppMemberFunctionMayBeStatic
auto Renderer::PostExecute(RenderContext& context) -> void
{
  // RenderContext::Reset now clears per-frame injected buffers (scene &
  // material).
  context.Reset();
}

//===----------------------------------------------------------------------===//
// PreExecute helper implementations
//===----------------------------------------------------------------------===//

auto Renderer::EnsureAndUploadDrawMetaDataBuffer() -> void
{
  // Delegate lifecycle to bindless structured buffer helper
  if (!draw_metadata_.HasData()) {
    return; // optional feature not used this frame
  }
  const auto rc_ptr = render_controller_.lock();
  if (!rc_ptr) {
    LOG_F(
      ERROR, "RenderController expired while ensuring draw metadata buffer");
    return;
  }
  auto& rc = *rc_ptr;
  // Ensure SRV exists and upload CPU snapshot if dirty; returns true if slot
  // changed
  static_cast<void>(draw_metadata_.EnsureAndUpload(rc, "DrawResourceIndices"));
}

// Removed legacy draw-metadata helpers; lifecycle now handled by
// BindlessStructuredBuffer<DrawMetadata>

auto Renderer::UpdateDrawMetaDataSlotIfChanged() -> void
{
  auto new_slot = BindlessIndicesSlot(kInvalidDescriptorSlot); // sentinel
  if (draw_metadata_.HasData() && draw_metadata_.IsSlotAssigned()) {
    new_slot = BindlessIndicesSlot(draw_metadata_.GetHeapSlot());
  }
  scene_const_cpu_.SetBindlessIndicesSlot(new_slot, SceneConstants::kRenderer);
}

auto Renderer::MaybeUpdateSceneConstants() -> void
{
  // Ensure renderer-managed fields are refreshed for this frame prior to
  // snapshot/upload. This also bumps the version when they change.
  if (const auto rc_ptr_local = render_controller_.lock()) {
    const auto fr = rc_ptr_local->CurrentFrameIndex();
    scene_const_cpu_.SetFrameSlot(
      rc_ptr_local->CurrentFrameIndex(), SceneConstants::kRenderer);
  }
  scene_const_cpu_.SetFrameSequenceNumber(
    frame_seq_num, SceneConstants::kRenderer);
  const auto current_version = scene_const_cpu_.GetVersion();
  if (scene_const_buffer_
    && current_version == last_uploaded_scene_const_version_) {
    DLOG_F(2, "MaybeUpdateSceneConstants: skipping upload (up-to-date)");
    return; // up-to-date
  }
  const auto rc_ptr = render_controller_.lock();
  if (!rc_ptr) {
    LOG_F(ERROR, "RenderController expired while updating scene constants");
    return;
  }
  auto& rc = *rc_ptr;
  const auto& graphics = rc.GetGraphics();
  constexpr auto size_bytes = sizeof(SceneConstants::GpuData);
  if (!scene_const_buffer_) {
    const BufferDesc desc {
      .size_bytes = size_bytes,
      .usage = BufferUsage::kConstant,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string("SceneConstants"),
    };
    scene_const_buffer_ = graphics.CreateBuffer(desc);
    scene_const_buffer_->SetName(desc.debug_name);
    rc.GetResourceRegistry().Register(scene_const_buffer_);
  }
  const auto& snapshot = scene_const_cpu_.GetSnapshot();
  void* mapped = scene_const_buffer_->Map();
  std::memcpy(mapped, &snapshot, size_bytes);
  scene_const_buffer_->UnMap();
  last_uploaded_scene_const_version_ = current_version;
}

auto Renderer::WireContext(RenderContext& context) -> void
{
  context.scene_constants = scene_const_buffer_;
  // Material constants are now accessed through bindless table, not direct CBV
  context.SetRenderer(this, render_controller_.lock().get());
}

auto Renderer::UpdateBindlessWorldsSlotIfChanged() -> void
{
  auto new_slot = BindlessWorldsSlot(kInvalidDescriptorSlot);
  if (world_transforms_.HasData() && world_transforms_.IsSlotAssigned()) {
    new_slot = BindlessWorldsSlot(world_transforms_.GetHeapSlot());
  }
  scene_const_cpu_.SetBindlessWorldsSlot(new_slot, SceneConstants::kRenderer);
}

auto Renderer::UpdateBindlessMaterialConstantsSlotIfChanged() -> void
{
  auto new_slot = BindlessMaterialConstantsSlot(kInvalidDescriptorSlot);
  if (material_constants_.HasData() && material_constants_.IsSlotAssigned()) {
    new_slot = BindlessMaterialConstantsSlot(material_constants_.GetHeapSlot());
  }
  scene_const_cpu_.SetBindlessMaterialConstantsSlot(
    new_slot, SceneConstants::kRenderer);
}

auto Renderer::SetDrawMetaData(const DrawMetadata& indices) -> void
{
  // Set single-entry array in the bindless structured buffer and mark dirty
  auto& cpu = draw_metadata_.GetCpuData();
  cpu.assign(1, indices);
  draw_metadata_.MarkDirty();
}

auto Renderer::GetDrawMetaData() const -> const DrawMetadata&
{
  const auto& cpu = draw_metadata_.GetCpuData();
  DCHECK_F(!cpu.empty());
  return cpu.front();
}

auto Renderer::BuildFrame(scene::Scene& scene, const View& view) -> std::size_t
{
  // TODO: temporary - this should move out to the engine core
  (void)frame_seq_num++;

  // Reset draw list for this frame
  opaque_items_.Clear();

  // Extract items via the new two-phase builder (Collect -> Finalize)
  using extraction::RenderListBuilder;
  RenderListBuilder builder;
  const auto collected = builder.Collect(scene, view, frame_seq_num);
  // Finalize directly into the renderer-managed output list.
  // RenderContext is not required by Finalize currently; pass a local.
  RenderContext dummy_context;
  builder.Finalize(collected, dummy_context, opaque_items_);

  // Update scene constants from the provided view snapshot
  ModifySceneConstants([&](SceneConstants& sc) {
    sc.SetViewMatrix(view.ViewMatrix())
      .SetProjectionMatrix(view.ProjectionMatrix())
      .SetCameraPosition(view.CameraPosition());
  });

  const auto inserted_count = opaque_items_.Items().size();
  DLOG_F(2, "BuildFrame: inserted {} render items", inserted_count);

  return inserted_count;
}

auto Renderer::BuildFrame(scene::Scene& scene, const CameraView& camera_view)
  -> std::size_t
{
  // Ensure transforms are up-to-date for this frame. SceneExtraction also
  // calls scene.Update(), but doing it here makes the sequencing explicit
  // before resolving the camera pose.
  scene.Update();

  const auto view = camera_view.Resolve();
  return BuildFrame(scene, view);
}

namespace {
auto CreateVertexBuffer(const Mesh& mesh, RenderController& render_controller)
  -> std::shared_ptr<Buffer>
{
  DLOG_F(2, "Create vertex buffer");

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
  std::memcpy(mapped, vertices.data(), upload_desc.size_bytes);
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
    const auto src = indices_view.AsU16();
    std::memcpy(mapped, src.data(), upload_desc.size_bytes);
  } else if (indices_view.type == IndexType::kUInt32) {
    const auto src = indices_view.AsU32();
    std::memcpy(mapped, src.data(), upload_desc.size_bytes);
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

// Create a StructuredBuffer SRV for a vertex buffer and register it.
auto CreateAndRegisterVertexSrv(RenderController& rc, Buffer& vertex_buffer)
  -> uint32_t
{
  using oxygen::Format;
  using oxygen::data::Vertex;
  using oxygen::graphics::BufferViewDescription;
  using oxygen::graphics::DescriptorVisibility;
  using oxygen::graphics::ResourceViewType;

  auto& descriptor_allocator = rc.GetDescriptorAllocator();
  auto& registry = rc.GetResourceRegistry();

  constexpr BufferViewDescription srv_desc {
    .view_type = ResourceViewType::kStructuredBuffer_SRV,
    .visibility = DescriptorVisibility::kShaderVisible,
    .format = Format::kUnknown,
    .stride = sizeof(Vertex),
  };
  auto handle
    = descriptor_allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate descriptor for vertex buffer SRV");
    return 0U;
  }
  const auto view = vertex_buffer.GetNativeView(handle, srv_desc);
  const auto index = descriptor_allocator.GetShaderVisibleIndex(handle);
  registry.RegisterView(vertex_buffer, view, std::move(handle), srv_desc);
  LOG_F(INFO, "Vertex buffer SRV registered at heap index {}", index);
  return index.get();
}

// Create a typed SRV for an index buffer (R16/R32) and register it.
auto CreateAndRegisterIndexSrv(RenderController& rc, Buffer& index_buffer,
  const IndexType index_type) -> uint32_t
{
  using oxygen::Format;
  using oxygen::graphics::BufferViewDescription;
  using oxygen::graphics::DescriptorVisibility;
  using oxygen::graphics::ResourceViewType;

  auto& descriptor_allocator = rc.GetDescriptorAllocator();
  auto& registry = rc.GetResourceRegistry();

  const auto typed_format
    = index_type == IndexType::kUInt16 ? Format::kR16UInt : Format::kR32UInt;
  const BufferViewDescription srv_desc {
    .view_type = ResourceViewType::kTypedBuffer_SRV,
    .visibility = DescriptorVisibility::kShaderVisible,
    .format = typed_format,
    .stride = 0,
  };
  auto handle = descriptor_allocator.Allocate(
    ResourceViewType::kTypedBuffer_SRV, DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate descriptor for index buffer SRV");
    return 0U;
  }
  const auto view = index_buffer.GetNativeView(handle, srv_desc);
  const auto index = descriptor_allocator.GetShaderVisibleIndex(handle);
  registry.RegisterView(index_buffer, view, std::move(handle), srv_desc);
  LOG_F(INFO, "Index buffer SRV registered at heap index {}", index);
  return index.get();
}

} // namespace

auto Renderer::EnsureResourcesForDrawList(
  const std::span<const RenderItem> draw_list) -> void
{
  // Tolerate empty spans: nothing to ensure, no uploads, no container mutation.
  if (draw_list.empty()) {
    return;
  }
  // Build per-draw array of indices in submission order
  std::vector<DrawMetadata> per_draw;
  std::vector<glm::mat4> per_world;
  std::vector<MaterialConstants> per_materials;
  per_draw.reserve(draw_list.size());
  per_world.reserve(draw_list.size());
  per_materials.reserve(draw_list.size());
  for (const auto& item : draw_list) {
    // Only consider items with a valid mesh and non-zero vertices
    if (!item.mesh || item.mesh->VertexCount() == 0) {
      continue;
    }
    const auto& ensured = EnsureMeshResources(*item.mesh);

    // Resolve selected submesh and iterate its MeshViews
    const auto submeshes = item.mesh->SubMeshes();
    const auto sm_idx = static_cast<std::size_t>(item.submesh_index);
    if (sm_idx >= submeshes.size()) {
      continue;
    }
    const auto views = submeshes[sm_idx].MeshViews();
    if (views.empty()) {
      continue;
    }

    for (size_t v = 0; v < views.size(); ++v) {
      const auto& view = views[v];
      const uint32_t transform_offset
        = static_cast<uint32_t>(per_world.size()); // next index
      // Material snapshot per view (simple duplication for now)
      uint32_t material_index = 0U;
      if (item.material) {
        per_materials.emplace_back(MakeMaterialConstants(*item.material));
        material_index = static_cast<uint32_t>(per_materials.size() - 1U);
      } else {
        const auto fallback = data::MaterialAsset::CreateDefault();
        per_materials.emplace_back(MakeMaterialConstants(*fallback));
        material_index = static_cast<uint32_t>(per_materials.size() - 1U);
      }

      per_draw.emplace_back(DrawMetadata {
        .vertex_buffer_index = ensured.vertex_srv_index,
        .index_buffer_index = ensured.index_srv_index,
        .is_indexed = item.mesh->IsIndexed() ? 1U : 0U,
        .instance_count = 1U,
        .transform_offset = transform_offset,
        .material_index = material_index,
        .instance_metadata_buffer_index = 0u,
        .instance_metadata_offset = 0u,
        .flags = 0u,
        .first_index = view.FirstIndex(),
        .base_vertex = static_cast<int32_t>(view.FirstVertex()),
        .padding = 0u,
      });
      per_world.emplace_back(item.world_transform);
    }
  }
  // Upload the full per-draw array once
  if (!per_draw.empty()) {
    draw_metadata_.GetCpuData() = std::move(per_draw);
    draw_metadata_.MarkDirty();
  }
  if (!per_world.empty()) {
    world_transforms_.GetCpuData() = std::move(per_world);
    world_transforms_.MarkDirty();
  }
  if (!per_materials.empty()) {
    material_constants_.GetCpuData() = std::move(per_materials);
    material_constants_.MarkDirty();
  }
}
auto Renderer::EnsureAndUploadWorldTransforms() -> void
{
  if (!world_transforms_.HasData()) {
    return;
  }
  const auto rc_ptr = render_controller_.lock();
  if (!rc_ptr) {
    LOG_F(ERROR, "RenderController expired while ensuring world transforms");
    return;
  }
  auto& rc = *rc_ptr;
  static_cast<void>(world_transforms_.EnsureAndUpload(rc, "WorldTransforms"));
}

auto Renderer::EnsureAndUploadMaterialConstants() -> void
{
  if (!material_constants_.HasData()) {
    return; // optional feature not used this frame
  }
  const auto rc_ptr = render_controller_.lock();
  if (!rc_ptr) {
    LOG_F(ERROR, "RenderController expired while ensuring material constants");
    return;
  }
  auto& rc = *rc_ptr;
  static_cast<void>(
    material_constants_.EnsureAndUpload(rc, "MaterialConstants"));
}

// Removed legacy material constants SRV registration; handled by helper

auto Renderer::SetMaterialConstants(const MaterialConstants& constants) -> void
{
  auto& cpu = material_constants_.GetCpuData();
  if (cpu.size() != 1) {
    cpu.resize(1);
  }
  cpu[0] = constants;
  material_constants_.MarkDirty();
}

auto Renderer::GetMaterialConstants() const -> const MaterialConstants&
{
  const auto& cpu = material_constants_.GetCpuData();
  DCHECK_F(!cpu.empty());
  return cpu.front();
}

auto Renderer::EnsureMeshResources(const Mesh& mesh) -> MeshGpuResources&
{
  DLOG_SCOPE_FUNCTION(3);
  DLOG_F(3, "mesh: {}", mesh.GetName());

  MeshId id = GetMeshId(mesh);
  if (const auto it = mesh_resources_.find(id); it != mesh_resources_.end()) {
    if (eviction_policy_) {
      eviction_policy_->OnMeshAccess(id);
    }
    return it->second; // cache hit
  }

  const auto render_controller = render_controller_.lock();
  if (!render_controller) {
    throw std::runtime_error(
      "RenderController expired in Renderer::EnsureMeshResources");
  }

  const auto vertex_buffer = CreateVertexBuffer(mesh, *render_controller);
  const auto index_buffer = CreateIndexBuffer(mesh, *render_controller);

  // Upload both buffers in a single command recorder scope.
  {
    const auto recorder = render_controller->AcquireCommandRecorder(
      SingleQueueStrategy().GraphicsQueueName(), "MeshBufferUpload");
    UploadVertexBuffer(mesh, *render_controller, *vertex_buffer, *recorder);
    UploadIndexBuffer(mesh, *render_controller, *index_buffer, *recorder);
  }

  MeshGpuResources gpu;
  gpu.vertex_buffer = vertex_buffer;
  gpu.index_buffer = index_buffer;

  // Create SRVs and register to obtain shader-visible indices
  gpu.vertex_srv_index
    = CreateAndRegisterVertexSrv(*render_controller, *vertex_buffer);
  gpu.index_srv_index = CreateAndRegisterIndexSrv(
    *render_controller, *index_buffer, mesh.IndexBuffer().type);

  auto [iter, _] = mesh_resources_.emplace(id, std::move(gpu));
  if (eviction_policy_) {
    eviction_policy_->OnMeshAccess(id);
  }

  return iter->second;
}

auto Renderer::ModifySceneConstants(
  const std::function<void(SceneConstants&)>& mutator) -> void
{
  mutator(scene_const_cpu_);
}

auto Renderer::GetSceneConstants() const -> const SceneConstants&
{
  return scene_const_cpu_;
}
