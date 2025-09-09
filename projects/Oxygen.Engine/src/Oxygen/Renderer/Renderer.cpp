//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::Graphics;
using oxygen::data::Mesh;
using oxygen::data::detail::IndexType;
using oxygen::engine::MaterialConstants;
using oxygen::engine::Renderer;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::SingleQueueStrategy;

namespace {

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

} // namespace

//===----------------------------------------------------------------------===//
// Renderer Implementation
//===----------------------------------------------------------------------===//

Renderer::Renderer(std::weak_ptr<Graphics> graphics)
  : gfx_weak_(std::move(graphics))
  , scene_prep_pipeline_(std::make_unique<sceneprep::ScenePrepPipelineImpl<
        decltype(sceneprep::CreateBasicCollectionConfig())>>(
      sceneprep::CreateBasicCollectionConfig()))
{
  LOG_F(
    2, "Renderer::Renderer [this={}] - constructor", static_cast<void*>(this));
  CHECK_F(!gfx_weak_.expired(), "Renderer constructed with expired Graphics");
  auto& gfx = *gfx_weak_.lock();
  uploader_ = std::make_unique<upload::UploadCoordinator>(gfx);
  // Ensure transform_mgr is always valid for scene prep and extraction
  scene_prep_state_.transform_mgr
    = std::make_unique<renderer::resources::TransformUploader>(
      gfx, observer_ptr { uploader_.get() });

  // Initialize GeometryUploader to replace legacy GeometryRegistry
  scene_prep_state_.geometry_uploader
    = std::make_unique<renderer::resources::GeometryUploader>(
      gfx, observer_ptr { uploader_.get() });

  // Initialize MaterialBinder to replace legacy MaterialRegistry
  scene_prep_state_.material_binder
    = std::make_unique<renderer::resources::MaterialBinder>(
      gfx, observer_ptr { uploader_.get() });
}

Renderer::~Renderer()
{
  // Proactively unregister bindless structured buffers from the registry to
  // avoid late destruction during Graphics shutdown.
  if (auto g = gfx_weak_.lock()) {
    // Best-effort: ignore if already unregistered or null.
    draw_metadata_.ReleaseGpuResources(*g);
    material_constants_.ReleaseGpuResources(*g);
  }
}

auto Renderer::GetGraphics() -> std::shared_ptr<Graphics>
{
  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::GetGraphics");
  }
  return graphics_ptr;
}

auto Renderer::PreExecute(
  RenderContext& context, const FrameContext& frame_context) -> void
{
  // Contract checks (kept inline per style preference)
  DCHECK_F(!context.scene_constants,
    "RenderContext.scene_constants must be null; renderer sets it via "
    "ModifySceneConstants/PreExecute");
  // Legacy AoS draw list path (opaque_items_) removed from PreExecute. All
  // per-draw GPU residency assurance now happens during SoA finalization
  // (FinalizeScenePrepSoA) via EnsureMeshResources(). RenderContext
  // opaque_draw_list remains intentionally untouched (empty span) for
  // transitional compatibility with any remaining callers; passes should now
  // consume prepared_frame / draw_metadata_bytes instead.

  EnsureAndUploadDrawMetadataBuffer();
  UpdateDrawMetadataSlotIfChanged();

  // Consolidated transform resource preparation
  if (scene_prep_state_.transform_mgr) {
    scene_prep_state_.transform_mgr->EnsureFrameResources();

    const auto worlds_srv
      = scene_prep_state_.transform_mgr->GetWorldsSrvIndex();
    const auto normals_srv
      = scene_prep_state_.transform_mgr->GetNormalsSrvIndex();

    scene_const_cpu_.SetBindlessWorldsSlot(
      BindlessWorldsSlot(worlds_srv.get()), SceneConstants::kRenderer);
    scene_const_cpu_.SetBindlessNormalMatricesSlot(
      BindlessNormalsSlot(normals_srv.get()), SceneConstants::kRenderer);
  }

  // Consolidated geometry resource preparation
  if (scene_prep_state_.geometry_uploader) {
    scene_prep_state_.geometry_uploader->EnsureFrameResources();
  }

  // Consolidated material resource preparation
  if (scene_prep_state_.material_binder) {
    scene_prep_state_.material_binder->EnsureFrameResources();

    const auto materials_srv
      = scene_prep_state_.material_binder->GetMaterialsSrvIndex();
    scene_const_cpu_.SetBindlessMaterialConstantsSlot(
      BindlessMaterialConstantsSlot(materials_srv.get()),
      SceneConstants::kRenderer);
  }

  MaybeUpdateSceneConstants(frame_context);

  WireContext(context);

  // Wire PreparedSceneFrame pointer (SoA finalized snapshot). This enables
  // passes to start consuming SoA data incrementally. Null remains valid if
  // finalization produced an empty frame.
  context.prepared_frame.reset(&prepared_frame_);

  // Ensure any upload command lists are submitted promptly for this frame.
  if (uploader_) {
    uploader_->Flush();
  }
}

// ReSharper disable once CppMemberFunctionMayBeStatic
auto Renderer::PostExecute(RenderContext& context) -> void
{
  // RenderContext::Reset now clears per-frame injected buffers (scene &
  // material).
  context.Reset();
}

auto Renderer::OnCommandRecord(FrameContext& /*context*/) -> co::Co<>
{
  // TODO: this will receive the render graph execution once the rendeer is
  // fully refactored
  co_return;
}

//===----------------------------------------------------------------------===//
// PreExecute helper implementations
//===----------------------------------------------------------------------===//

auto Renderer::EnsureAndUploadDrawMetadataBuffer() -> void
{
  // Delegate lifecycle to bindless structured buffer helper
  if (!draw_metadata_.HasData()) {
    return; // optional feature not used this frame
  }
  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    LOG_F(ERROR, "Graphics expired while ensuring draw metadata buffer");
    return;
  }
  auto& graphics = *graphics_ptr;
  // Ensure SRV exists (device-local buffer) and register if needed
  static_cast<void>(
    draw_metadata_.EnsureBufferAndSrv(graphics, "DrawResourceIndices"));
  // Upload via coordinator when marked dirty
  if (uploader_ && draw_metadata_.IsDirty()) {
    using upload::BatchPolicy;
    using upload::UploadBufferDesc;
    using upload::UploadDataView;
    using upload::UploadKind;
    using upload::UploadRequest;

    const auto& cpu = draw_metadata_.GetCpuData();
    const uint64_t size = cpu.size() * sizeof(DrawMetadata);
    if (size > 0 && draw_metadata_.GetBuffer()) {
      UploadRequest req;
      req.kind = UploadKind::kBuffer;
      req.batch_policy = BatchPolicy::kCoalesce;
      req.debug_name = "DrawResourceIndices";
      req.desc = UploadBufferDesc {
        .dst = draw_metadata_.GetBuffer(),
        .size_bytes = size,
        .dst_offset = 0,
      };
      req.data = UploadDataView { std::as_bytes(std::span(cpu)) };
      static_cast<void>(uploader_->SubmitMany(std::span { &req, 1 }));
      draw_metadata_.ClearDirty();
    }
  }
}

// Removed legacy draw-metadata helpers; lifecycle now handled by
// BindlessStructuredBuffer<DrawMetadata>

auto Renderer::UpdateDrawMetadataSlotIfChanged() -> void
{
  // DrawMetadata structured buffer now publishes its descriptor heap slot via
  // SceneConstants.bindless_draw_metadata_slot (HLSL reads this dynamic slot).
  // Legacy bindless_indices_slot fully removed (C++ & HLSL). Shaders use
  // bindless_draw_metadata_slot exclusively. Docs pending sync (see work log).

  auto new_draw_slot = BindlessDrawMetadataSlot(kInvalidDescriptorSlot);
  if (draw_metadata_.HasData() && draw_metadata_.IsSlotAssigned()) {
    new_draw_slot = BindlessDrawMetadataSlot(draw_metadata_.GetHeapSlot());
  }

  // Primary: set new draw-metadata slot
  scene_const_cpu_.SetBindlessDrawMetadataSlot(
    new_draw_slot, SceneConstants::kRenderer);

  DLOG_F(2,
    "UpdateDrawMetadataSlotIfChanged: draw_metadata_slot={} (legacy "
    "indices_slot mirror={})",
    new_draw_slot.value, new_draw_slot.value);
}

auto Renderer::MaybeUpdateSceneConstants(const FrameContext& frame_context)
  -> void
{
  // Ensure renderer-managed fields are refreshed for this frame prior to
  // snapshot/upload. This also bumps the version when they change.
  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    LOG_F(ERROR, "Graphics expired while updating scene constants");
    return;
  }

  // Set frame information from FrameContext
  scene_const_cpu_.SetFrameSlot(
    frame_context.GetFrameSlot(), SceneConstants::kRenderer);
  scene_const_cpu_.SetFrameSequenceNumber(
    frame_context.GetFrameSequenceNumber(), SceneConstants::kRenderer);
  const auto current_version = scene_const_cpu_.GetVersion();
  if (scene_const_buffer_
    && current_version == last_uploaded_scene_const_version_) {
    DLOG_F(2, "MaybeUpdateSceneConstants: skipping upload (up-to-date)");
    return; // up-to-date
  }

  auto& graphics = *graphics_ptr;
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
    graphics.GetResourceRegistry().Register(scene_const_buffer_);
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
  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::WireContext");
  }
  context.SetRenderer(this, graphics_ptr.get());
}

auto Renderer::SetDrawMetadata(const DrawMetadata& indices) -> void
{
  // Set single-entry array in the bindless structured buffer and mark dirty
  auto& cpu = draw_metadata_.GetCpuData();
  cpu.assign(1, indices);
  draw_metadata_.MarkDirty();
}

auto Renderer::GetDrawMetadata() const -> const DrawMetadata&
{
  const auto& cpu = draw_metadata_.GetCpuData();
  DCHECK_F(!cpu.empty());
  return cpu.front();
}

auto Renderer::BuildFrame(const View& view, const FrameContext& frame_context)
  -> std::size_t
{
  // Phase Mapping: This function orchestrates PhaseId::kFrameGraph duties:
  //  * sequence number init
  //  * scene prep collection
  //  * SoA finalization (DrawMetadata, materials, partitions, sorting)
  //  * view/scenec constants update
  // Command recording happens later (PhaseId::kCommandRecord).

  auto scene_ptr = frame_context.GetScene();
  CHECK_NOTNULL_F(scene_ptr, "FrameContext.scene is null in BuildFrame");
  auto& scene = *scene_ptr;

  const auto t_collect0 = std::chrono::high_resolution_clock::now();
  scene_prep_pipeline_->Collect(
    scene, view, frame_seq_num.get(), scene_prep_state_, true);
  const auto t_collect1 = std::chrono::high_resolution_clock::now();
  last_finalize_stats_.collection_time
    = std::chrono::duration_cast<std::chrono::microseconds>(
      t_collect1 - t_collect0);
  DLOG_F(1, "ScenePrep collected {} items (nodes={}) time_collect_us={}",
    scene_prep_state_.collected_items.size(), scene.GetNodes().Items().size(),
    last_finalize_stats_.collection_time.count());

  FinalizeScenePrepPhase(scene_prep_state_); // non-const: material registration
  UpdateSceneConstantsFromView(view);

  const auto draw_count = CurrentDrawCount();
  DLOG_F(2, "BuildFrame: finalized {} draws (SoA only)", draw_count);
  return draw_count;
}

//===----------------------------------------------------------------------===//
// SoA Finalization (Task 6 initial subset)
//===----------------------------------------------------------------------===//

auto Renderer::FinalizeScenePrepSoA(sceneprep::ScenePrepState& prep_state)
  -> void
{
  const auto t_begin = std::chrono::high_resolution_clock::now();

  // Ensure geometry uploader resources are ready for this frame
  prep_state.geometry_uploader->EnsureFrameResources();

  const auto& filtered = prep_state.filtered_indices;
  GenerateDrawMetadata(filtered, prep_state);
  BuildSortingAndPartitions();
  PublishPreparedFrameSpans();
  UploadDrawMetadataBindless();
  UpdateFinalizeStatistics(prep_state, filtered.size(), t_begin);
}

auto Renderer::GenerateDrawMetadata(const std::vector<std::size_t>& filtered,
  sceneprep::ScenePrepState& prep_state) -> void
{
  // Direct DrawMetadata generation (Task 6: native SoA path)
  draw_metadata_cpu_soa_.clear();
  draw_metadata_cpu_soa_.reserve(filtered.size());

  auto ClassifyMaterialPassMask
    = [&](const data::MaterialAsset* mat) -> PassMask {
    if (!mat) {
      return PassMask { PassMaskBit::kOpaqueOrMasked };
    }
    const auto domain = mat->GetMaterialDomain();
    const auto base = mat->GetBaseColor();
    const float alpha = base[3];
    const bool is_opaque_domain = (domain == data::MaterialDomain::kOpaque);
    const bool is_masked_domain = (domain == data::MaterialDomain::kMasked);
    DLOG_F(2,
      "Material classify: name='{}' domain={} alpha={:.3f} is_opaque={} "
      "is_masked={}",
      mat->GetAssetName(), static_cast<int>(domain), alpha, is_opaque_domain,
      is_masked_domain);
    if (is_opaque_domain && alpha >= 0.999f) {
      return PassMask { PassMaskBit::kOpaqueOrMasked };
    }
    if (is_masked_domain) {
      return PassMask { PassMaskBit::kOpaqueOrMasked };
    }
    DLOG_F(2,
      " -> classified as Transparent (flags={}) due to domain {} and "
      "alpha={:.3f}",
      PassMask { PassMaskBit::kTransparent }, static_cast<int>(domain), alpha);
    return PassMask { PassMaskBit::kTransparent };
  };

  for (size_t i = 0; i < filtered.size(); ++i) {
    const auto src_index = filtered[i];
    if (src_index >= prep_state.collected_items.size()) {
      continue;
    }
    const auto& item = prep_state.collected_items[src_index];
    if (!item.geometry) {
      continue;
    }
    const auto lod_index = item.lod_index;
    const auto submesh_index = item.submesh_index;
    const auto& geom = *item.geometry;
    auto meshes_span = geom.Meshes();
    if (lod_index >= meshes_span.size()) {
      continue;
    }
    const auto& lod_mesh_ptr = meshes_span[lod_index];
    if (!lod_mesh_ptr) {
      continue;
    }
    const auto& lod = *lod_mesh_ptr;
    auto submeshes_span = lod.SubMeshes();
    if (submesh_index >= submeshes_span.size()) {
      continue;
    }
    const auto& submesh = submeshes_span[submesh_index];
    auto views_span = submesh.MeshViews();
    if (views_span.empty()) {
      continue;
    }
    for (const auto& view : views_span) {
      DrawMetadata dm {};

      // Get actual SRV indices for GPU access
      ShaderVisibleIndex vertex_srv_index { kInvalidShaderVisibleIndex };
      ShaderVisibleIndex index_srv_index { kInvalidShaderVisibleIndex };
      if (lod_mesh_ptr && lod_mesh_ptr->IsValid()) {
        const auto mesh_handle
          = scene_prep_state_.geometry_uploader->GetOrAllocate(*lod_mesh_ptr);
        if (scene_prep_state_.geometry_uploader->IsValidHandle(mesh_handle)) {
          // Get the actual SRV indices for GPU access
          vertex_srv_index
            = scene_prep_state_.geometry_uploader->GetVertexSrvIndex(
              mesh_handle);
          index_srv_index
            = scene_prep_state_.geometry_uploader->GetIndexSrvIndex(
              mesh_handle);
        }
      }

      dm.vertex_buffer_index = vertex_srv_index;
      dm.index_buffer_index = index_srv_index;
      const auto index_view = view.IndexBuffer();
      const bool has_indices = index_view.Count() > 0;
      if (has_indices) {
        dm.first_index = view.FirstIndex();
        dm.base_vertex = static_cast<int32_t>(view.FirstVertex());
        dm.is_indexed = 1;
        dm.index_count = static_cast<uint32_t>(index_view.Count());
        dm.vertex_count = 0;
      } else {
        dm.is_indexed = 0;
        dm.index_count = 0;
        dm.vertex_count = view.VertexCount();
      }
      dm.instance_count = 1;
      // Obtain stable handle from MaterialBinder
      LOG_F(2,
        "GenerateDrawMetadata - prep_state addr={}, "
        "material_binder addr={}",
        static_cast<void*>(&prep_state),
        static_cast<void*>(prep_state.material_binder.get()));
      const auto stable_handle
        = prep_state.material_binder->GetOrAllocate(item.material);
      const auto stable_handle_value = stable_handle.get();

      dm.material_handle = stable_handle_value;
      // Use stable TransformHandle id instead of filtered order index.
      const auto handle = item.transform_handle;
      dm.transform_index = handle.get();
      dm.instance_metadata_buffer_index = 0;
      dm.instance_metadata_offset = 0;
      dm.flags = ClassifyMaterialPassMask(item.material.get());
      DCHECK_F(prep_state.transform_mgr != nullptr);
      DCHECK_F(prep_state.transform_mgr->IsValidHandle(handle),
        "Invalid transform handle (id={}) while generating DrawMetadata",
        dm.transform_index);
      DCHECK_F(!dm.flags.IsEmpty(), "flags cannot be empty after assignment");
      draw_metadata_cpu_soa_.push_back(dm);
    }
  }
  if (!draw_metadata_cpu_soa_.empty()) {
    const std::size_t sample
      = std::min<std::size_t>(draw_metadata_cpu_soa_.size(), 3);
    for (std::size_t s = 0; s < sample; ++s) {
      const auto& d = draw_metadata_cpu_soa_[s];
      DLOG_F(3,
        "DrawMetadata[{}]: vb={} ib={} first_index={} base_vertex={} "
        "index_count={} vertex_count={} indexed={} transform_index={} "
        "material_handle={}",
        s, d.vertex_buffer_index, d.index_buffer_index, d.first_index,
        d.base_vertex, d.index_count, d.vertex_count, d.is_indexed,
        d.transform_index, d.material_handle);
    }
  }
}

auto Renderer::BuildSortingAndPartitions() -> void
{
  sorting_keys_cpu_soa_.clear();
  sorting_keys_cpu_soa_.reserve(draw_metadata_cpu_soa_.size());
  for (const auto& d : draw_metadata_cpu_soa_) {
    sorting_keys_cpu_soa_.push_back(DrawSortingKey {
      .pass_mask = d.flags,
      .material_index = d.material_handle, // now stable MaterialHandle value
      .geometry_vertex_srv = d.vertex_buffer_index,
      .geometry_index_srv = d.index_buffer_index,
    });
  }
  const auto t_sort_begin = std::chrono::high_resolution_clock::now();
  auto ComputeFNV1a64 = [](const void* data, size_t size_bytes) -> uint64_t {
    uint64_t h = 1469598103934665603ULL; // offset
    constexpr uint64_t prime = 1099511628211ULL;
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size_bytes; ++i) {
      h ^= static_cast<uint64_t>(p[i]);
      h *= prime;
    }
    return h;
  };
  const auto pre_sort_hash = ComputeFNV1a64(sorting_keys_cpu_soa_.data(),
    sorting_keys_cpu_soa_.size() * sizeof(DrawSortingKey));
  const size_t draw_count = draw_metadata_cpu_soa_.size();
  std::vector<uint32_t> permutation(draw_count);
  for (uint32_t i = 0; i < draw_count; ++i) {
    permutation[i] = i;
  }
  std::stable_sort(
    permutation.begin(), permutation.end(), [&](uint32_t a, uint32_t b) {
      const auto& ka = sorting_keys_cpu_soa_[a];
      const auto& kb = sorting_keys_cpu_soa_[b];
      if (ka.pass_mask != kb.pass_mask) {
        return ka.pass_mask < kb.pass_mask;
      }
      if (ka.material_index != kb.material_index) {
        return ka.material_index < kb.material_index;
      }
      if (ka.geometry_vertex_srv != kb.geometry_vertex_srv) {
        return ka.geometry_vertex_srv < kb.geometry_vertex_srv;
      }
      if (ka.geometry_index_srv != kb.geometry_index_srv) {
        return ka.geometry_index_srv < kb.geometry_index_srv;
      }
      return a < b;
    });
  std::vector<DrawMetadata> reordered_dm;
  reordered_dm.reserve(draw_count);
  std::vector<DrawSortingKey> reordered_keys;
  reordered_keys.reserve(draw_count);
  for (auto idx : permutation) {
    reordered_dm.push_back(draw_metadata_cpu_soa_[idx]);
    reordered_keys.push_back(sorting_keys_cpu_soa_[idx]);
  }
  draw_metadata_cpu_soa_.swap(reordered_dm);
  sorting_keys_cpu_soa_.swap(reordered_keys);
  const auto post_sort_hash = ComputeFNV1a64(sorting_keys_cpu_soa_.data(),
    sorting_keys_cpu_soa_.size() * sizeof(DrawSortingKey));
  last_draw_order_hash_ = post_sort_hash;
  partitions_cpu_soa_.clear();
  if (!draw_metadata_cpu_soa_.empty()) {
    auto current_mask = draw_metadata_cpu_soa_.front().flags;
    uint32_t range_begin = 0u;
    for (uint32_t i = 1; i < draw_metadata_cpu_soa_.size(); ++i) {
      const auto mask = draw_metadata_cpu_soa_[i].flags;
      if (mask != current_mask) {
        partitions_cpu_soa_.push_back(PreparedSceneFrame::PartitionRange {
          .pass_mask = current_mask,
          .begin = range_begin,
          .end = i,
        });
        current_mask = mask;
        range_begin = i;
      }
    }
    partitions_cpu_soa_.push_back(PreparedSceneFrame::PartitionRange {
      .pass_mask = current_mask,
      .begin = range_begin,
      .end = static_cast<uint32_t>(draw_metadata_cpu_soa_.size()),
    });
  }
  prepared_frame_.partitions_storage = &partitions_cpu_soa_;
  prepared_frame_.partitions
    = std::span<const PreparedSceneFrame::PartitionRange>(
      partitions_cpu_soa_.data(), partitions_cpu_soa_.size());
  const auto t_sort_end = std::chrono::high_resolution_clock::now();
  last_sort_time_ = std::chrono::duration_cast<std::chrono::microseconds>(
    t_sort_end - t_sort_begin);
  DLOG_F(2,
    "DrawOrderSort: pre=0x{:016X} post=0x{:016X} draws={} partitions={} "
    "keys_bytes={} sort_time_us={}",
    pre_sort_hash, post_sort_hash, draw_metadata_cpu_soa_.size(),
    partitions_cpu_soa_.size(),
    sorting_keys_cpu_soa_.size() * sizeof(DrawSortingKey),
    last_sort_time_.count());
}

auto Renderer::PublishPreparedFrameSpans() -> void
{
  const auto& tm = scene_prep_state_.transform_mgr;
  DCHECK_NOTNULL_F(tm); // tm should never be null in a valid Renderer
  const auto world_span = tm->GetWorldMatrices();
  prepared_frame_.world_matrices = std::span<const float>(
    reinterpret_cast<const float*>(world_span.data()), world_span.size() * 16u);

  const auto normal_span = tm->GetNormalMatrices();
  prepared_frame_.normal_matrices
    = std::span<const float>(reinterpret_cast<const float*>(normal_span.data()),
      normal_span.size() * 16u);

  prepared_frame_.draw_metadata_bytes = std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(draw_metadata_cpu_soa_.data()),
    draw_metadata_cpu_soa_.size() * sizeof(DrawMetadata));
}

auto Renderer::UploadDrawMetadataBindless() -> void
{
  if (draw_metadata_cpu_soa_.empty()) {
    return;
  }
  auto& gpu_dm = draw_metadata_.GetCpuData();
  gpu_dm.assign(draw_metadata_cpu_soa_.begin(), draw_metadata_cpu_soa_.end());
  draw_metadata_.MarkDirty();
}

auto Renderer::UpdateFinalizeStatistics(
  const sceneprep::ScenePrepState& prep_state, std::size_t filtered_count,
  std::chrono::high_resolution_clock::time_point t_begin) -> void
{
  const auto t_end = std::chrono::high_resolution_clock::now();
  const auto us
    = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_begin)
        .count();
  last_finalize_stats_.collected = prep_state.collected_items.size();
  last_finalize_stats_.filtered = filtered_count;
  last_finalize_stats_.finalized = filtered_count; // same for now
  last_finalize_stats_.finalize_time = std::chrono::microseconds(us);
  DLOG_F(2,
    "FinalizeScenePrepSoA: collected={} filtered={} finalized={} time_us={} "
    "hw_mark={}",
    last_finalize_stats_.collected, last_finalize_stats_.filtered,
    last_finalize_stats_.finalized, last_finalize_stats_.finalize_time.count(),
    max_finalized_count_);
  if (!partitions_cpu_soa_.empty()) {
    for (std::size_t i = 0; i < partitions_cpu_soa_.size(); ++i) {
      const auto& pr = partitions_cpu_soa_[i];
      DLOG_F(3, "Partition[{}]: mask={} range=[{},{}] (count={})", i,
        pr.pass_mask, pr.begin, pr.end, (pr.end - pr.begin));
    }
  } else {
    DLOG_F(3, "Partition map empty (no draws)");
  }
  if (draw_metadata_.HasData()) {
    const auto legacy_count = draw_metadata_.GetCpuData().size();
    const auto soa_count = draw_metadata_cpu_soa_.size();
    if (legacy_count != soa_count) {
      DLOG_F(1,
        "DrawMetadata direct-gen count differs legacy={} soa={} (expected "
        "while transition)",
        legacy_count, soa_count);
    }
  }
}

auto Renderer::BuildFrame(const CameraView& camera_view,
  const FrameContext& frame_context) -> std::size_t
{
  const auto view = camera_view.Resolve();
  return BuildFrame(view, frame_context);
}

auto Renderer::FinalizeScenePrepPhase(sceneprep::ScenePrepState& prep_state)
  -> void
{
  FinalizeScenePrepSoA(prep_state); // may allocate new material handles
  DLOG_F(1,
    "Renderer BuildFrame finalized SoA frame: collected={} filtered={} "
    "draws={} partitions={}",
    last_finalize_stats_.collected, last_finalize_stats_.filtered,
    prepared_frame_.draw_metadata_bytes.size() / sizeof(DrawMetadata),
    prepared_frame_.partitions.size());
}

auto Renderer::UpdateSceneConstantsFromView(const View& view) -> void
{
  // Update scene constants from the provided view snapshot
  ModifySceneConstants([&](SceneConstants& sc) {
    sc.SetViewMatrix(view.ViewMatrix())
      .SetProjectionMatrix(view.ProjectionMatrix())
      .SetCameraPosition(view.CameraPosition());
  });
}

auto Renderer::CurrentDrawCount() const noexcept -> std::size_t
{
  return prepared_frame_.draw_metadata_bytes.size() / sizeof(DrawMetadata);
}

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

auto Renderer::ModifySceneConstants(
  const std::function<void(SceneConstants&)>& mutator) -> void
{
  mutator(scene_const_cpu_);
}

auto Renderer::GetSceneConstants() const -> const SceneConstants&
{
  return scene_const_cpu_;
}

auto Renderer::OnFrameStart(FrameContext& /*context*/) -> void
{
  LOG_F(2,
    "Renderer::OnFrameStart - scene_prep_state_ addr={}, material_binder "
    "addr={}",
    static_cast<void*>(&scene_prep_state_),
    static_cast<void*>(scene_prep_state_.material_binder.get()));

  // Retire staging resources from previous frame uploads
  if (uploader_) {
    uploader_->RetireCompleted();
  }

  // Reset transform manager for the new frame
  DCHECK_NOTNULL_F(scene_prep_state_.transform_mgr);
  scene_prep_state_.transform_mgr->OnFrameStart();

  // Reset geometry uploader for the new frame
  DCHECK_NOTNULL_F(scene_prep_state_.geometry_uploader);
  scene_prep_state_.geometry_uploader->OnFrameStart();

  // Reset material binder for the new frame
  DCHECK_NOTNULL_F(scene_prep_state_.material_binder);
  scene_prep_state_.material_binder->OnFrameStart();
}

/*!
 Executes the scene transform propagation phase.

 Flow
 1. Acquire non-owning scene pointer from the frame context.
 2. If absent: early return (benign no-op, keeps frame deterministic).
 3. Call Scene::Update() which performs:
    - Pass 1: Dense linear scan processing dirty node flags (non-transform).
    - Pass 2: Pre-order filtered traversal (DirtyTransformFilter) resolving
      world transforms only along dirty chains (parent first).
 4. Return; no extra state retained by this module.

 Invariants / Guarantees
 - Invoked exactly once per frame in kTransformPropagation phase.
 - Parent world matrix valid before any child transform recompute.
 - Clean descendants of a dirty ancestor incur only an early-out check.
 - kIgnoreParentTransform subtrees intentionally skipped per design.
 - No scene graph structural mutation occurs here.
 - No GPU resource mutation or uploads here (CPU authoritative only).

 Never Do
 - Do not reparent / create / destroy nodes here.
 - Do not call Scene::Update() more than once per frame.
 - Do not cache raw pointers across frames.
 - Do not allocate large transient buffers (Scene owns traversal memory).
 - Do not introduce side-effects dependent on sibling visitation order.

 Performance Characteristics
 - Time: O(F + T) where F = processed dirty flags, T = visited transform
   chain nodes (<= total nodes, typically sparse).
 - Memory: No steady-state allocations.
 - Optimization: Early-exit for clean transforms; dense flag pass for cache
 locality.

 Future Improvement (Parallel Chains)
 - The scene's root hierarchies are independent for transform propagation.
 - A future optimization can collect the subset of root hierarchies that have
   at least one dirty descendant and dispatch each qualifying root subtree to
   a worker task (parent-first order preserved inside each task, no sharing).
 - Synchronize (join) all tasks before proceeding to later phases to maintain
   frame determinism. Skip parallel dispatch below a configurable dirty-node
   threshold to avoid overhead on small scenes.
 - This preserves all existing invariants (no graph mutation, parent-first,
   single update per node) while offering scalable speedups on large scenes.

 @note Dirty flag semantics, traversal filtering, and no-mutation policy are
       deliberate and should be preserved.
 @see oxygen::scene::Scene::Update
 @see oxygen::scene::SceneTraversal::UpdateTransforms
 @see oxygen::scene::DirtyTransformFilter
*/
auto Renderer::OnTransformPropagation(FrameContext& context) -> co::Co<>
{
  // Acquire scene pointer (non-owning). If absent, log once per frame in debug.
  auto scene_ptr = context.GetScene();
  if (!scene_ptr) {
    DLOG_F(
      1, "TransformsModule: no active scene set in FrameContext (skipping)");
    co_return; // Nothing to update
  }

  // Perform hierarchy propagation & world matrix updates.
  scene_ptr->Update();

  co_return;
}
