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
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
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
#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::Graphics;
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

Renderer::Renderer(std::weak_ptr<oxygen::Graphics> graphics,
  std::shared_ptr<EvictionPolicy> eviction_policy)
  : graphics_(std::move(graphics))
  , eviction_policy_(
      eviction_policy ? std::move(eviction_policy) : MakeLruEvictionPolicy())
{
}

Renderer::~Renderer() = default;

auto Renderer::GetGraphics() -> std::shared_ptr<oxygen::Graphics>
{
  auto graphics_ptr = graphics_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::GetGraphics");
  }
  return graphics_ptr;
}

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

auto Renderer::PreExecute(
  RenderContext& context, const engine::FrameContext& frame_context) -> void
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

  EnsureAndUploadDrawMetaDataBuffer();
  UpdateDrawMetaDataSlotIfChanged();

  EnsureAndUploadWorldTransforms();
  UpdateBindlessWorldsSlotIfChanged();

  EnsureAndUploadMaterialConstants();
  UpdateBindlessMaterialConstantsSlotIfChanged();

  MaybeUpdateSceneConstants(frame_context);

  WireContext(context);

  // Wire PreparedSceneFrame pointer (SoA finalized snapshot). This enables
  // passes to start consuming SoA data incrementally. Null remains valid if
  // finalization produced an empty frame.
  context.prepared_frame.reset(&prepared_frame_);
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
  const auto graphics_ptr = graphics_.lock();
  if (!graphics_ptr) {
    LOG_F(ERROR, "Graphics expired while ensuring draw metadata buffer");
    return;
  }
  auto& graphics = *graphics_ptr;
  // Ensure SRV exists and upload CPU snapshot if dirty; returns true if slot
  // changed
  static_cast<void>(
    draw_metadata_.EnsureAndUpload(graphics, "DrawResourceIndices"));
}

// Removed legacy draw-metadata helpers; lifecycle now handled by
// BindlessStructuredBuffer<DrawMetadata>

auto Renderer::UpdateDrawMetaDataSlotIfChanged() -> void
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
    "UpdateDrawMetaDataSlotIfChanged: draw_metadata_slot={} (legacy "
    "indices_slot mirror={})",
    new_draw_slot.value, new_draw_slot.value);
}

auto Renderer::MaybeUpdateSceneConstants(
  const engine::FrameContext& frame_context) -> void
{
  // Ensure renderer-managed fields are refreshed for this frame prior to
  // snapshot/upload. This also bumps the version when they change.
  const auto graphics_ptr = graphics_.lock();
  if (!graphics_ptr) {
    LOG_F(ERROR, "Graphics expired while updating scene constants");
    return;
  }

  // Set frame information from FrameContext
  scene_const_cpu_.SetFrameSlot(
    frame_context.GetFrameSlot(), SceneConstants::kRenderer);
  scene_const_cpu_.SetFrameSequenceNumber(
    frame_seq_num, SceneConstants::kRenderer);
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
  const auto graphics_ptr = graphics_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::WireContext");
  }
  context.SetRenderer(this, graphics_ptr.get());
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

auto Renderer::BuildFrame(
  const View& view, const engine::FrameContext& frame_context) -> std::size_t
{
  auto scene_ptr = frame_context.GetScene();
  CHECK_NOTNULL_F(scene_ptr, "FrameContext.scene is null in BuildFrame");
  // FIXME: temporary until everything uses the frame context
  auto& scene = *scene_ptr;

  // Store frame sequence number from FrameContext
  frame_seq_num = frame_context.GetFrameSequenceNumber();
  // === ScenePrep collection ===
  // Legacy AoS translation step removed; we now directly finalize into SoA
  // arrays (matrices, draw metadata, materials, partitions, sorting keys).
  // FIXME(perf): Avoid per-frame allocations by pooling ScenePrepState or
  // reusing internal vectors (requires clear ownership strategy & lifetime).
  namespace sp = oxygen::engine::sceneprep;
  sp::ScenePrepState prep_state;
  auto cfg = sp::CreateBasicCollectionConfig(); // TODO: pass policy/config from
                                                // renderer settings
  sp::ScenePrepPipelineCollection pipeline { cfg };
  // NOTE: StrongType frame_seq_num unwrapped for ScenePrep API.
  const auto t_collect0 = std::chrono::high_resolution_clock::now();
  pipeline.Collect(scene, view, frame_seq_num.get(), prep_state);
  const auto t_collect1 = std::chrono::high_resolution_clock::now();
  last_finalize_stats_.collection_time
    = std::chrono::duration_cast<std::chrono::microseconds>(
      t_collect1 - t_collect0);
  DLOG_F(1, "ScenePrep collected {} items (nodes={}) time_collect_us={}",
    prep_state.collected_items.size(), scene.GetNodes().Items().size(),
    last_finalize_stats_.collection_time.count());
  // New path (SoA finalization) - currently only populates matrix arrays.
  // This is non-destructive and coexists with legacy AoS until passes migrate.
  FinalizeScenePrepSoA(prep_state);
  DLOG_F(1,
    "Renderer BuildFrame finalized SoA frame: collected={} filtered={} "
    "draws={} partitions={}",
    last_finalize_stats_.collected, last_finalize_stats_.filtered,
    prepared_frame_.draw_metadata_bytes.size() / sizeof(DrawMetadata),
    prepared_frame_.partitions.size());

  // Update scene constants from the provided view snapshot
  ModifySceneConstants([&](SceneConstants& sc) {
    sc.SetViewMatrix(view.ViewMatrix())
      .SetProjectionMatrix(view.ProjectionMatrix())
      .SetCameraPosition(view.CameraPosition());
  });

  // Return number of finalized draw records (post-sort order)
  const auto draw_count
    = prepared_frame_.draw_metadata_bytes.size() / sizeof(DrawMetadata);
  DLOG_F(2, "BuildFrame: finalized {} draws (SoA only)", draw_count);
  return draw_count;
}

//===----------------------------------------------------------------------===//
// SoA Finalization (Task 6 initial subset)
//===----------------------------------------------------------------------===//

auto Renderer::FinalizeScenePrepSoA(const sceneprep::ScenePrepState& prep_state)
  -> void
{
  // For Task 6 we only materialize world & normal matrix arrays using the
  // collected_items vector. Later tasks will consume filtered_indices,
  // per-submesh expansion, sorting, partition maps and material/geometry
  // indirection. This keeps the first step low risk while exercising the
  // per-frame storage and PreparedSceneFrame lifetime contract.

  const auto& filtered = prep_state.filtered_indices.empty()
    ? [&]() -> const std::vector<std::size_t>& {
    // Fallback: if filtering not yet producing output, synthesize full sequence
    static std::vector<std::size_t> synth;
    synth.resize(prep_state.collected_items.size());
    std::iota(synth.begin(), synth.end(), 0ULL);
    return synth;
  }()
    : prep_state.filtered_indices;
  const auto count = filtered.size();
  if (count > max_finalized_count_) {
    max_finalized_count_ = count; // grow monotonic
  }

  // High-water reservation (monotonic grow) to reduce realloc thrash.
  const auto needed_floats = count * 16u;
  if (world_matrices_cpu_.capacity() < needed_floats) {
    world_matrices_cpu_.reserve(needed_floats);
  }
  if (normal_matrices_cpu_.capacity() < needed_floats) {
    normal_matrices_cpu_.reserve(needed_floats);
  }
  world_matrices_cpu_.resize(needed_floats);
  normal_matrices_cpu_.resize(needed_floats);

  const auto t0 = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < count; ++i) {
    const auto src_index = filtered[i];
    if (src_index >= prep_state.collected_items.size()) {
      continue; // defensive; should not happen
    }
    const auto& it = prep_state.collected_items[src_index];
    // World matrix
    std::memcpy(
      &world_matrices_cpu_[i * 16u], &it.world_transform, sizeof(float) * 16u);
    // Normal matrix (inverse transpose of upper-left 3x3). Use existing logic
    // from legacy RenderItem path by recomputing here (cheap for small counts;
    // will be optimized later by caching inside ScenePrep).
    glm::mat3 upper3x3 { it.world_transform };
    const glm::mat3 normal3 = glm::transpose(glm::inverse(upper3x3));
    // Store as full 4x4 with last row/col identity to simplify shader usage.
    glm::mat4 normal4 { 1.0f };
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        normal4[c][r] = normal3[r][c];
      }
    }
    std::memcpy(&normal_matrices_cpu_[i * 16u], &normal4, sizeof(float) * 16u);
  }

  // Direct DrawMetadata generation (Task 6: native SoA path)
  // Expand each filtered collected item into its submesh mesh views and
  // construct an engine::DrawMetadata record per mesh view. This replaces the
  // previous parity copy of the legacy draw_metadata_ structured buffer.
  draw_metadata_cpu_soa_.clear();
  draw_metadata_cpu_soa_.reserve(count); // minimum; may grow if multiple views
  material_constants_cpu_soa_.clear();
  material_constants_cpu_soa_.reserve(count); // worst case (no dedupe)
  // Material deduplication support: pointer map (raw MaterialAsset*) -> index
  std::unordered_map<const oxygen::data::MaterialAsset*, uint32_t>
    material_index_map;
  material_index_map.reserve(count);
  uint32_t material_default_index = std::numeric_limits<uint32_t>::max();
  std::size_t material_duplicate_avoids = 0;

  // NOTE: We currently do not perform material indirection or bindless buffer
  // index resolution here; placeholders (0) are used where data is not yet
  // surfaced by ScenePrep. Deterministic ordering: iterate filtered indices
  // in order, then submeshes, then mesh views in their declared order.
  for (size_t i = 0; i < count; ++i) {
    const auto src_index = filtered[i];
    if (src_index >= prep_state.collected_items.size()) {
      continue; // defensive
    }
    const auto& item = prep_state.collected_items[src_index];
    if (!item.geometry) {
      continue; // skip incomplete entries
    }

    // Resolve LOD & submesh; tolerate out-of-range by skipping.
    const auto lod_index = item.lod_index;
    const auto submesh_index = item.submesh_index;
    const auto& geom = *item.geometry; // shared_ptr already validated

    // GeometryAsset API: Meshes() -> span of shared_ptr<Mesh>; each Mesh has
    // SubMeshes(); each SubMesh exposes MeshViews(). We read only slice
    // parameters to populate DrawMetadata. (Corrected from previous LODs()).
    auto meshes_span = geom.Meshes();
    if (lod_index >= meshes_span.size()) {
      continue; // invalid LOD index
    }
    const auto& lod_mesh_ptr = meshes_span[lod_index];
    if (!lod_mesh_ptr) {
      continue; // null mesh pointer
    }
    const auto& lod = *lod_mesh_ptr;
    auto submeshes_span = lod.SubMeshes();
    if (submesh_index >= submeshes_span.size()) {
      continue; // invalid submesh
    }
    const auto& submesh = submeshes_span[submesh_index];
    auto views_span = submesh.MeshViews();
    if (views_span.empty()) {
      continue; // nothing to draw
    }

    // For now we generate one DrawMetadata per MeshView. Resolve (or create)
    // mesh GPU resources once per collected item.
    MeshGpuResources* ensured_mesh_resources = nullptr;
    if (lod_mesh_ptr && lod_mesh_ptr->IsValid()) {
      // Note: EnsureMeshResources operates on Mesh (not GeometryAsset). Mesh
      // identity for caching is derived via GetMeshId inside.
      ensured_mesh_resources = &EnsureMeshResources(*lod_mesh_ptr);
    }
    for (const auto& view : views_span) {
      DrawMetadata dm {};
      // Geometry buffers: use resolved SRV indices if available.
      if (ensured_mesh_resources) {
        dm.vertex_buffer_index = ensured_mesh_resources->vertex_srv_index;
        dm.index_buffer_index = ensured_mesh_resources->index_srv_index;
      } else {
        dm.vertex_buffer_index = 0; // fallback
        dm.index_buffer_index = 0;
      }

      // Determine indexed vs non-indexed from MeshView IndexBuffer().
      const auto index_view = view.IndexBuffer();
      // MeshView IndexBuffer() returns detail::IndexBufferView; treat any
      // non-zero count as indexed (IndexType exposed only internally).
      const bool has_indices = index_view.Count() > 0;
      if (has_indices) {
        dm.first_index = view.FirstIndex();
        dm.base_vertex = static_cast<int32_t>(view.FirstVertex());
        dm.is_indexed = 1;
        dm.index_count = static_cast<uint32_t>(index_view.Count());
        dm.vertex_count = 0; // unused in indexed path
      } else {
        dm.is_indexed = 0;
        dm.index_count = 0; // unused in non-indexed path
        dm.vertex_count = static_cast<uint32_t>(view.VertexCount());
      }

      dm.instance_count = 1; // single instance path (instancing later)
      // Material indirection + deduplication
      uint32_t material_index = 0;
      if (item.material) {
        const auto* raw_mat = item.material.get();
        if (auto it_mat = material_index_map.find(raw_mat);
          it_mat != material_index_map.end()) {
          material_index = it_mat->second;
          ++material_duplicate_avoids;
        } else {
          material_constants_cpu_soa_.push_back(
            MakeMaterialConstants(*item.material));
          material_index
            = static_cast<uint32_t>(material_constants_cpu_soa_.size() - 1U);
          material_index_map.emplace(raw_mat, material_index);
        }
      } else { // fallback default material (single shared entry)
        if (material_default_index == std::numeric_limits<uint32_t>::max()) {
          const auto fallback = oxygen::data::MaterialAsset::CreateDefault();
          material_constants_cpu_soa_.push_back(
            MakeMaterialConstants(*fallback));
          material_default_index
            = static_cast<uint32_t>(material_constants_cpu_soa_.size() - 1U);
        }
        material_index = material_default_index;
      }
      dm.material_index
        = material_index; // index into material_constants_cpu_soa_
      dm.transform_index = static_cast<uint32_t>(i); // matches matrix slots
      dm.instance_metadata_buffer_index = 0; // unused placeholder
      dm.instance_metadata_offset = 0; // unused placeholder
      // Pass mask flags (initial): bit 0 = opaque (placeholder until real
      // material transparency or pass classification logic). All current
      // collected items are treated as opaque. Additional bits will be
      // populated by Task 11 when partition map & full mask logic added.
      constexpr uint32_t kPassFlagOpaque = 1u << 0;
      dm.flags = kPassFlagOpaque;

      // Debug validation (will be extended later): ensure indices within
      // provisional ranges *before* pushing to vector so any failure leaves
      // container unchanged on that record.
      DCHECK_F(dm.transform_index < count,
        "transform_index out of range while generating DrawMetadata");
      DCHECK_F(dm.material_index < material_constants_cpu_soa_.size(),
        "material_index out of range ({} >= {}): logic error in "
        "dedupe/population",
        dm.material_index, material_constants_cpu_soa_.size());
      DCHECK_F(dm.flags != 0, "flags must be non-zero after assignment");

      draw_metadata_cpu_soa_.push_back(dm);
    }
  }

  // (Moved) draw_metadata_bytes & partitions publication occurs after sorting
  // and partition range construction below.

  // Upload / wire material constants if we produced any (SoA path).
  if (!material_constants_cpu_soa_.empty()) {
    // Replace legacy path contents only if SoA produced something (keeps
    // previous frame otherwise). We now DEFER the GPU upload + slot update to
    // PreExecute() for consistency with world_transforms_ & draw_metadata_
    // (single phase for all structured buffers). This avoids per-frame mixed
    // timing and makes reasoning about visibility simpler.
    material_constants_.GetCpuData() = material_constants_cpu_soa_;
    material_constants_.MarkDirty(); // uploaded in PreExecute
  }

  // Diagnostic: log first few generated entries (limited verbosity level).
  if (!draw_metadata_cpu_soa_.empty()) {
    const std::size_t sample
      = std::min<std::size_t>(draw_metadata_cpu_soa_.size(), 3);
    for (std::size_t s = 0; s < sample; ++s) {
      const auto& d = draw_metadata_cpu_soa_[s];
      DLOG_F(3,
        "DrawMetadata[{}]: vb={} ib={} first_index={} base_vertex={} "
        "index_count={} vertex_count={} indexed={} transform_index={} "
        "material_index={}",
        s, d.vertex_buffer_index, d.index_buffer_index, d.first_index,
        d.base_vertex, d.index_count, d.vertex_count, d.is_indexed,
        d.transform_index, d.material_index);
    }
  }
  if (material_duplicate_avoids > 0) {
    DLOG_F(2,
      "Material dedupe avoided {} duplicate constant uploads "
      "(unique_materials={} total_draws={})",
      material_duplicate_avoids, material_constants_cpu_soa_.size(),
      draw_metadata_cpu_soa_.size());
  }
  // Summary validation after full population (secondary range checks).
  for (std::size_t s = 0; s < draw_metadata_cpu_soa_.size(); ++s) {
    const auto& d = draw_metadata_cpu_soa_[s];
    DCHECK_F(d.transform_index < count,
      "Post population: transform_index {} out of range count {}",
      d.transform_index, count);
    DCHECK_F(d.material_index < material_constants_cpu_soa_.size(),
      "Post population: material_index {} out of range materials {}",
      d.material_index, material_constants_cpu_soa_.size());
    DCHECK_F(d.flags & 0x1u, "Opaque bit not set in flags for record {}", s);
  }

  // Update PreparedSceneFrame spans (draw metadata not yet produced here).
  prepared_frame_.world_matrices = std::span<const float>(
    world_matrices_cpu_.data(), world_matrices_cpu_.size());
  prepared_frame_.normal_matrices = std::span<const float>(
    normal_matrices_cpu_.data(), normal_matrices_cpu_.size());

  // --- GPU StructuredBuffer<WorldMatrix> population (restored) ---
  // Regression Fix: After removing the legacy AoS path we stopped mirroring
  // per-draw world matrices into the bindless structured buffer
  // (world_transforms_). Shaders then read either an empty buffer or stale
  // identity data, collapsing all meshes to the origin (user report: "Meshes
  // are now all at the center"). We now copy the freshly finalized SoA world
  // matrices (world_matrices_cpu_) into the structured buffer each frame and
  // mark it dirty so PreExecute() uploads before passes execute.
  if (count > 0) {
    auto& gpu_worlds = world_transforms_.GetCpuData();
    const auto* src_mats
      = reinterpret_cast<const glm::mat4*>(world_matrices_cpu_.data());
    gpu_worlds.assign(src_mats, src_mats + count);
    world_transforms_
      .MarkDirty(); // uploaded in PreExecute -> EnsureAndUploadWorldTransforms
  }

  // Build sorting key array (now used for actual ordering) and compute
  // pre-sort hash for diagnostics.
  sorting_keys_cpu_soa_.clear();
  sorting_keys_cpu_soa_.reserve(draw_metadata_cpu_soa_.size());
  for (const auto& d : draw_metadata_cpu_soa_) {
    sorting_keys_cpu_soa_.push_back(DrawSortingKey {
      .pass_mask = d.flags,
      .material_index = d.material_index,
      .geometry_vertex_srv = d.vertex_buffer_index,
      .geometry_index_srv = d.index_buffer_index,
    });
  }
  // Measure sorting + partition construction time separately from overall
  // finalize_time to surface ordering overhead costs.
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

  // Stable sort: produce permutation indices, then apply to draw metadata.
  const size_t draw_count = draw_metadata_cpu_soa_.size();
  std::vector<uint32_t> permutation(draw_count);
  for (uint32_t i = 0; i < draw_count; ++i)
    permutation[i] = i;
  std::stable_sort(
    permutation.begin(), permutation.end(), [&](uint32_t a, uint32_t b) {
      const auto& ka = sorting_keys_cpu_soa_[a];
      const auto& kb = sorting_keys_cpu_soa_[b];
      if (ka.pass_mask != kb.pass_mask)
        return ka.pass_mask < kb.pass_mask; // group by pass
      if (ka.material_index != kb.material_index)
        return ka.material_index < kb.material_index; // material locality
      if (ka.geometry_vertex_srv != kb.geometry_vertex_srv)
        return ka.geometry_vertex_srv
          < kb.geometry_vertex_srv; // vertex buffer locality
      if (ka.geometry_index_srv != kb.geometry_index_srv)
        return ka.geometry_index_srv
          < kb.geometry_index_srv; // index buffer locality
      return a < b; // deterministic tiebreaker
    });
  // Apply permutation to draw metadata and sorting keys (in-place via temp
  // copies)
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

  // Recompute hash post-sort (final deterministic order signature).
  const auto post_sort_hash = ComputeFNV1a64(sorting_keys_cpu_soa_.data(),
    sorting_keys_cpu_soa_.size() * sizeof(DrawSortingKey));
  last_draw_order_hash_ = post_sort_hash;

  // Build partition ranges from sorted draws: contiguous segments with same
  // pass_mask.
  partitions_cpu_soa_.clear();
  if (!draw_metadata_cpu_soa_.empty()) {
    uint32_t current_mask = draw_metadata_cpu_soa_.front().flags;
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
    // Final range
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

  // Publish draw metadata bytes (post-sort order).
  prepared_frame_.draw_metadata_bytes = std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(draw_metadata_cpu_soa_.data()),
    draw_metadata_cpu_soa_.size() * sizeof(DrawMetadata));

  // --- GPU StructuredBuffer<DrawMetadata> population (restored) ---
  // Legacy EnsureResourcesForDrawList previously populated the bindless
  // draw_metadata_ buffer each frame. After removing that path we must copy
  // the freshly generated & sorted SoA DrawMetadata records into the
  // bindless structured buffer so shaders (which fetch DrawMetadata via the
  // bindless slot) see correct per-draw data. Without this, issued draws use
  // an empty / stale buffer and nothing renders.
  if (!draw_metadata_cpu_soa_.empty()) {
    auto& gpu_dm = draw_metadata_.GetCpuData();
    gpu_dm.assign(draw_metadata_cpu_soa_.begin(), draw_metadata_cpu_soa_.end());
    draw_metadata_.MarkDirty(); // upload will occur in PreExecute
  }

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

  // Parity logging (transitional): we only log count divergence now since
  // direct generation intentionally differs in layout/content from legacy.
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

  DCHECK_F(world_matrices_cpu_.size() == count * 16u,
    "World matrices size mismatch (expected count*16)");
  DCHECK_F(normal_matrices_cpu_.size() == count * 16u,
    "Normal matrices size mismatch (expected count*16)");

  const auto t1 = std::chrono::high_resolution_clock::now();
  const auto us
    = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  last_finalize_stats_.collected = prep_state.collected_items.size();
  last_finalize_stats_.filtered = filtered.size();
  last_finalize_stats_.finalized = count;
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
      DLOG_F(3, "Partition[{}]: mask=0x{:X} range=[{},{}] (count={})", i,
        pr.pass_mask, pr.begin, pr.end, (pr.end - pr.begin));
    }
  } else {
    DLOG_F(3, "Partition map empty (no draws)");
  }
}

auto Renderer::BuildFrame(const CameraView& camera_view,
  const engine::FrameContext& frame_context) -> std::size_t
{
  const auto view = camera_view.Resolve();
  return BuildFrame(view, frame_context);
}

namespace {
auto CreateVertexBuffer(const Mesh& mesh, Graphics& graphics)
  -> std::shared_ptr<Buffer>
{
  DLOG_F(2, "Create vertex buffer");

  const auto vertices = mesh.Vertices();
  const BufferDesc vb_desc {
    .size_bytes = vertices.size() * sizeof(oxygen::data::Vertex),
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = std::string(mesh.GetName()) + ".VertexBuffer",
  };
  auto vertex_buffer = graphics.CreateBuffer(vb_desc);
  vertex_buffer->SetName(vb_desc.debug_name);
  graphics.GetResourceRegistry().Register(vertex_buffer);
  return vertex_buffer;
}

auto CreateIndexBuffer(const Mesh& mesh, Graphics& graphics)
  -> std::shared_ptr<Buffer>
{
  DLOG_F(2, "Create index buffer");

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
  graphics.GetResourceRegistry().Register(index_buffer);
  return index_buffer;
}

auto UploadVertexBuffer(const Mesh& mesh, Graphics& graphics,
  Buffer& vertex_buffer, oxygen::graphics::CommandRecorder& recorder) -> void
{
  DLOG_F(2, "Upload vertex buffer");

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
  DeferredObjectRelease(upload_buffer, graphics.GetDeferredReclaimer());
}

auto UploadIndexBuffer(const Mesh& mesh, Graphics& graphics,
  Buffer& index_buffer, oxygen::graphics::CommandRecorder& recorder) -> void
{
  DLOG_F(2, "Upload index buffer");

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
  DeferredObjectRelease(upload_buffer, graphics.GetDeferredReclaimer());
}

// Create a StructuredBuffer SRV for a vertex buffer and register it.
auto CreateAndRegisterVertexSrv(Graphics& graphics, Buffer& vertex_buffer)
  -> uint32_t
{
  using oxygen::Format;
  using oxygen::data::Vertex;
  using oxygen::graphics::BufferViewDescription;
  using oxygen::graphics::DescriptorVisibility;
  using oxygen::graphics::ResourceViewType;

  auto& descriptor_allocator = graphics.GetDescriptorAllocator();
  auto& registry = graphics.GetResourceRegistry();

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
auto CreateAndRegisterIndexSrv(Graphics& graphics, Buffer& index_buffer,
  const IndexType index_type) -> uint32_t
{
  using oxygen::Format;
  using oxygen::graphics::BufferViewDescription;
  using oxygen::graphics::DescriptorVisibility;
  using oxygen::graphics::ResourceViewType;

  auto& descriptor_allocator = graphics.GetDescriptorAllocator();
  auto& registry = graphics.GetResourceRegistry();

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

auto Renderer::EnsureAndUploadWorldTransforms() -> void
{
  if (!world_transforms_.HasData()) {
    return;
  }
  const auto graphics_ptr = graphics_.lock();
  if (!graphics_ptr) {
    LOG_F(ERROR, "Graphics expired while ensuring world transforms");
    return;
  }
  auto& graphics = *graphics_ptr;
  static_cast<void>(
    world_transforms_.EnsureAndUpload(graphics, "WorldTransforms"));
}

auto Renderer::EnsureAndUploadMaterialConstants() -> void
{
  if (!material_constants_.HasData()) {
    return; // optional feature not used this frame
  }
  const auto graphics_ptr = graphics_.lock();
  if (!graphics_ptr) {
    LOG_F(ERROR, "Graphics expired while ensuring material constants");
    return;
  }
  auto& graphics = *graphics_ptr;
  static_cast<void>(
    material_constants_.EnsureAndUpload(graphics, "MaterialConstants"));
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

  const auto graphics_ptr = graphics_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error(
      "Graphics expired in Renderer::EnsureMeshResources");
  }

  const auto vertex_buffer = CreateVertexBuffer(mesh, *graphics_ptr);
  const auto index_buffer = CreateIndexBuffer(mesh, *graphics_ptr);

  // Upload both buffers in a single command recorder scope.
  {
    const auto recorder = graphics_ptr->AcquireCommandRecorder(
      SingleQueueStrategy().KeyFor(graphics::QueueRole::kGraphics),
      "MeshBufferUpload");
    UploadVertexBuffer(mesh, *graphics_ptr, *vertex_buffer, *recorder);
    UploadIndexBuffer(mesh, *graphics_ptr, *index_buffer, *recorder);
  }

  MeshGpuResources gpu;
  gpu.vertex_buffer = vertex_buffer;
  gpu.index_buffer = index_buffer;

  // Create SRVs and register to obtain shader-visible indices
  gpu.vertex_srv_index
    = CreateAndRegisterVertexSrv(*graphics_ptr, *vertex_buffer);
  gpu.index_srv_index = CreateAndRegisterIndexSrv(
    *graphics_ptr, *index_buffer, mesh.IndexBuffer().type);

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
