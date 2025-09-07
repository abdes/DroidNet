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
#include <Oxygen/Renderer/ScenePrep/State/GeometryRegistry.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Types/PassMask.h>
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

  EnsureAndUploadDrawMetadataBuffer();
  UpdateDrawMetadataSlotIfChanged();

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

auto Renderer::EnsureAndUploadDrawMetadataBuffer() -> void
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

auto Renderer::BuildFrame(
  const View& view, const engine::FrameContext& frame_context) -> std::size_t
{
  // Phase Mapping: This function orchestrates PhaseId::kFrameGraph duties:
  //  * sequence number init
  //  * scene prep collection
  //  * SoA finalization (DrawMetadata, materials, partitions, sorting)
  //  * view/scenec constants update
  // Command recording happens later (PhaseId::kCommandRecord).

  auto& scene = ResolveScene(frame_context);
  InitializeFrameSequence(frame_context);

  // Reset per-frame state but retain persistent caches
  // (transform/material/geometry)
  scene_prep_state_.ResetFrameData();
  scene_prep_state_.transform_mgr.BeginFrame();
  const auto cfg = PrepareScenePrepCollectionConfig();
  CollectScenePrep(scene, view, scene_prep_state_, cfg);
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
  const auto& filtered = ResolveFilteredIndices(prep_state);
  const auto unique_transform_count
    = prep_state.transform_mgr.GetUniqueTransformCount();
  // Allocate storage sized to number of unique transforms (handle id space)
  ReserveMatrixStorage(unique_transform_count);
  // Populate (or update) per-handle world/normal matrices using cached data.
  PopulateMatrices(filtered, prep_state);
  GenerateDrawMetadataAndMaterials(filtered,
    prep_state); // may mutate material registry (stable handle allocation)
  UploadWorldTransforms(unique_transform_count); // upload unique transforms
  BuildSortingAndPartitions();
  PublishPreparedFrameSpans(unique_transform_count);
  UploadDrawMetadataBindless();
  UpdateFinalizeStatistics(prep_state, filtered.size(), t_begin);
}

//=== SoA Finalization helper implementations =============================//

//=== Improvement Opportunities (SoA Finalization)
//-----------------------------//
// 1. Matrix Computation Caching:
//    (Partially Addressed) World & normal matrices now sourced from
//    TransformManager cache (allocated during collection). We still duplicate
//    them into per-frame SoA arrays. Next: alias cached storage directly and
//    map DrawMetadata.transform_index to global handle indices.
// 2. Parallelization:
//    PopulateMatrices + GenerateDrawMetadataAndMaterials are independent of
//    sorting; could be partitioned across worker threads (job system) using
//    chunked ranges. Care: material dedupe map would need thread-safe
//    combining (e.g. per-thread maps + merge phase) to keep deterministic
//    ordering.
// 3. Memory Pooling:
//    Replace std::vector high-water reserves with a ring-buffer or slab
//    allocator to retain pages and reduce potential fragmentation for large
//    scenes. Track peak separately per frame graph stage.
// 4. Sorting Key Optimization:
//    Current stable_sort builds permutation then copies vectors. Could switch
//    to radix / timsort adaptation over array-of-struct keys, or store keys
//    interleaved in DrawMetadata to reduce cache misses.
// 5. Pass Mask Expansion:
//    Future additional bits (additive, decals, ui, transmission) require
//    adjusting partition build to group by composite classes (e.g. depth pre-
//    pass eligibility) rather than raw flag value order.
// 6. Indirect Draw Args:
//    Once unified 32-byte indirect arg struct is implemented, generate and
//    upload alongside DrawMetadata to avoid CPU re-walk for multi-pass issue
//    (see work log indirect args note).
// 7. GPU-driven Culling Path:
//    Insert compute stage after collection to perform frustum/occlusion
//    culling on compressed instance data; filter masks before sorting to
//    shrink sort workload.
// 8. Material Dedupe Hashing:
//    Replace pointer-map with content hash (pipeline-affecting fields) to
//    enable merging identical materials originating from distinct assets.
// 9. Telemetry:
//    Add counters for per-stage time (populate, materials, sort, partition)
//    instead of aggregated finalize_time for finer regressions.
// 10. Validation Mode:
//    Optional expensive checks (mesh SRV indices valid, no NaN in matrices)
//    gated by a debug flag to keep hot path clean in release.
//----------------------------------------------------------------------------//

auto Renderer::ResolveFilteredIndices(
  const sceneprep::ScenePrepState& prep_state)
  -> const std::vector<std::size_t>&
{
  // Fallback: if filtering not yet producing output, synthesize full sequence.
  if (prep_state.filtered_indices.empty()) {
    static std::vector<std::size_t> synth;
    synth.resize(prep_state.collected_items.size());
    std::iota(synth.begin(), synth.end(), 0ULL);
    return synth;
  }
  return prep_state.filtered_indices;
}

auto Renderer::ReserveMatrixStorage(std::size_t count) -> void
{
  if (count > max_finalized_count_) {
    max_finalized_count_ = count; // grow monotonic
  }
  const auto needed_floats = count * 16u; // 4x4 per transform
  if (world_matrices_cpu_.capacity() < needed_floats) {
    world_matrices_cpu_.reserve(needed_floats);
  }
  if (normal_matrices_cpu_.capacity() < needed_floats) {
    normal_matrices_cpu_.reserve(needed_floats);
  }
  world_matrices_cpu_.resize(needed_floats);
  normal_matrices_cpu_.resize(needed_floats);
}

auto Renderer::PopulateMatrices(const std::vector<std::size_t>& filtered,
  const sceneprep::ScenePrepState& prep_state) -> void
{
  // Transform caching path: fetch world & cached normal matrix via handle.
  const auto& tm = prep_state.transform_mgr;
  const auto unique_count = tm.GetUniqueTransformCount();
  if (unique_count == 0) {
    return;
  }
  const bool alias_world = alias_world_matrices_;
  const bool alias_normal = alias_normal_matrices_;
  std::vector<uint8_t> written;
  written.resize(unique_count, 0u);
  for (size_t fi = 0; fi < filtered.size(); ++fi) {
    const auto src_index = filtered[fi];
    if (src_index >= prep_state.collected_items.size()) {
      continue; // defensive
    }
    const auto& item = prep_state.collected_items[src_index];
    const auto handle = item.transform_handle;
    const auto h = handle.get();
    if (h >= unique_count) {
      continue; // invalid handle (should not happen)
    }
    if (written[h]) {
      continue; // already populated
    }
    written[h] = 1u;
    glm::mat4 world { 1.0f };
    glm::mat4 normal { 1.0f };
    if (tm.IsValidHandle(handle)) {
      world = tm.GetTransform(handle);
      normal = tm.GetNormalMatrix(handle); // now stored natively as mat4
    }
    if (!alias_world) {
      std::memcpy(&world_matrices_cpu_[h * 16u], &world, sizeof(float) * 16u);
    }
    if (!alias_normal) {
      std::memcpy(&normal_matrices_cpu_[h * 16u], &normal, sizeof(float) * 16u);
    }
  }
}

auto Renderer::GenerateDrawMetadataAndMaterials(
  const std::vector<std::size_t>& filtered,
  sceneprep::ScenePrepState& prep_state) -> void
{
  // Direct DrawMetadata generation (Task 6: native SoA path)
  draw_metadata_cpu_soa_.clear();
  draw_metadata_cpu_soa_.reserve(filtered.size());
  material_constants_cpu_soa_.clear();
  // IMPORTANT: After adopting stable registry MaterialHandle in DrawMetadata
  // we must make the MaterialConstants StructuredBuffer index space MATCH the
  // handle values. Previous logic built a densely packed per-frame dedupe
  // array and stored the stable handle in DrawMetadata; this caused mismatched
  // lookups on the GPU (wrong colors) because meta.material_handle no longer
  // referenced the packed slot. We now build a sparse-aligned array whose
  // indices correspond exactly to MaterialHandle values used this frame.
  // Strategy:
  //  - Allocate vector up to (max_handle_value + 1) lazily as we encounter
  //    handles during draw emission.
  //  - Slot 0 always contains default material constants (sentinel).
  //  - For each draw with non-null material, fill the slot at its handle
  //    value if not already initialized.
  //  - Unused intermediate slots remain default-initialized (zero / default
  //    constructed). This is acceptable because no draw references them.
  //  - This keeps per-frame rebuild cheap while guaranteeing index alignment.
  // NOTE: Potential memory growth if handle space becomes sparse & large.
  // Acceptable for current iteration; future optimization could maintain a
  // compact indirection table (handle->packed index) and store packed index in
  // DrawMetadata instead.
  // Initialize default slot (index 0) always.
  material_constants_cpu_soa_.resize(1u);
  {
    // Ensure index 0 populated with canonical default each frame for safety.
    const auto fallback = oxygen::data::MaterialAsset::CreateDefault();
    material_constants_cpu_soa_[0] = MakeMaterialConstants(*fallback);
  }
  std::vector<uint8_t> material_slot_initialized; // parallels vector size
  material_slot_initialized.resize(1u, 1u); // slot 0 initialized

  auto ClassifyMaterialPassMask
    = [&](const oxygen::data::MaterialAsset* mat) -> PassMask {
    if (!mat) {
      return PassMask { PassMaskBit::kOpaqueOrMasked };
    }
    const auto domain = mat->GetMaterialDomain();
    const auto base = mat->GetBaseColor();
    const float alpha = base[3];
    const bool has_transparency_feature = false; // placeholder hook
    const bool is_opaque_domain
      = (domain == oxygen::data::MaterialDomain::kOpaque);
    const bool is_masked_domain
      = (domain == oxygen::data::MaterialDomain::kMasked);
    DLOG_F(2,
      "Material classify: name='{}' domain={} alpha={:.3f} is_opaque={} "
      "is_masked={}",
      mat->GetAssetName(), static_cast<int>(domain), alpha, is_opaque_domain,
      is_masked_domain);
    if (is_opaque_domain && alpha >= 0.999f && !has_transparency_feature) {
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
    oxygen::engine::sceneprep::GeometryHandle mesh_handle { 0, 0 };
    if (lod_mesh_ptr && lod_mesh_ptr->IsValid()) {
      // Register or lookup stable geometry buffer indices via geometry
      // registry.
      // TODO(geometry-resource): Move EnsureMeshResources upload + eviction
      // logic into a dedicated MeshResourceManager so this call becomes purely
      // logical.
      mesh_handle = scene_prep_state_.geometry_registry.GetOrRegisterMesh(
        lod_mesh_ptr.get(), [this, &lod_mesh_ptr]() {
          auto& res = EnsureMeshResources(*lod_mesh_ptr);
          return oxygen::engine::sceneprep::GeometryRegistry::
            GeometryProvisionResult { res.vertex_srv_index,
              res.index_srv_index };
        });
    }
    for (const auto& view : views_span) {
      DrawMetadata dm {};
      dm.vertex_buffer_index = mesh_handle.vertex_buffer;
      dm.index_buffer_index = mesh_handle.index_buffer;
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
        dm.vertex_count = static_cast<uint32_t>(view.VertexCount());
      }
      dm.instance_count = 1;
      // Obtain stable registry handle (sentinel 0 if null material)
      const auto stable_handle
        = prep_state.material_registry.GetOrRegisterMaterial(item.material);
      const auto stable_handle_value = stable_handle.get();
      // Migration assert: stable handle should be 0 iff material pointer is
      // null.
      DCHECK_F((stable_handle_value == 0u) == (item.material == nullptr),
        "MaterialHandle sentinel mismatch (handle={} null_ptr={})",
        stable_handle_value, item.material == nullptr);
      // Resize aligned constants array if needed.
      if (stable_handle_value >= material_constants_cpu_soa_.size()) {
        material_constants_cpu_soa_.resize(stable_handle_value + 1u);
        material_slot_initialized.resize(stable_handle_value + 1u, 0u);
        // Newly added slots remain uninitialized until first assignment.
        // (They contain default constructed MaterialConstants.)
      }
      // Populate slot if first time this frame.
      if (!material_slot_initialized[stable_handle_value]) {
        if (stable_handle_value == 0u) {
          // Already default; nothing to do (but mark initialized).
          material_slot_initialized[0] = 1u;
        } else if (item.material) {
          material_constants_cpu_soa_[stable_handle_value]
            = MakeMaterialConstants(*item.material);
          material_slot_initialized[stable_handle_value] = 1u;
        } else {
          // Null material with non-zero handle should never occur.
          DCHECK_F(false,
            "Null material produced non-zero handle value={} (logic error)",
            stable_handle_value);
        }
      }
      dm.material_handle = stable_handle_value; // stable registry handle value
      // Use stable TransformHandle id instead of filtered order index.
      const auto handle = item.transform_handle;
      dm.transform_index = handle.get();
      dm.instance_metadata_buffer_index = 0;
      dm.instance_metadata_offset = 0;
      dm.flags = ClassifyMaterialPassMask(item.material.get());
      DCHECK_F(prep_state.transform_mgr.IsValidHandle(handle),
        "Invalid transform handle (id={}) while generating DrawMetadata",
        dm.transform_index);
      // Basic validity (0 sentinel allowed). Underlying type is unsigned so >=0
      // always.
      DCHECK_F(dm.material_handle >= 0u,
        "Material handle unexpected negative (impossible) value={}",
        dm.material_handle);
      DCHECK_F(!dm.flags.IsEmpty(), "flags cannot be empty after assignment");
      draw_metadata_cpu_soa_.push_back(dm);
    }
  }
  if (!material_constants_cpu_soa_.empty()) {
    material_constants_.GetCpuData() = material_constants_cpu_soa_;
    material_constants_.MarkDirty();
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
  // Post population validation
  const auto unique_transform_count
    = prep_state.transform_mgr.GetUniqueTransformCount();
  for (std::size_t s = 0; s < draw_metadata_cpu_soa_.size(); ++s) {
    const auto& d = draw_metadata_cpu_soa_[s];
    DCHECK_F(d.transform_index < unique_transform_count,
      "Post population: transform_index {} >= unique_transform_count {}",
      d.transform_index, unique_transform_count);
    // material_handle not range-checked against per-frame constants: stable
    // registry may exceed current per-frame material constants vector size
    // (which covers only referenced materials).
    DCHECK_F(
      !d.flags.IsEmpty(), "Pass mask must not be empty for record {}", s);
  }
}

auto Renderer::UploadWorldTransforms(std::size_t count) -> void
{
  if (count == 0)
    return;
  auto& gpu_worlds = world_transforms_.GetCpuData();
  const auto* src_mats
    = reinterpret_cast<const glm::mat4*>(world_matrices_cpu_.data());

  // First frame or size change -> full upload path.
  const bool size_mismatch = gpu_worlds.size() != count;
  if (size_mismatch) {
    if (alias_world_matrices_) {
      const auto& tm_span
        = scene_prep_state_.transform_mgr.GetWorldMatricesSpan();
      gpu_worlds.assign(tm_span.begin(), tm_span.end());
    } else {
      gpu_worlds.assign(src_mats, src_mats + count);
    }
    world_transforms_.MarkDirty();
    return;
  }
  // Sparse update path using TransformManager dirty indices.
  const auto& tm = scene_prep_state_.transform_mgr;
  const auto& dirty = tm.GetDirtyIndices();
  if (dirty.empty()) {
    return; // nothing changed
  }
  // Ensure gpu_worlds already sized (otherwise size_mismatch branch would run)
  if (alias_world_matrices_) {
    const auto& tm_span
      = scene_prep_state_.transform_mgr.GetWorldMatricesSpan();
    for (auto idx : dirty) {
      if (idx >= count)
        continue;
      gpu_worlds[idx] = tm_span[idx];
    }
  } else {
    for (auto idx : dirty) {
      if (idx >= count)
        continue;
      gpu_worlds[idx] = src_mats[idx];
    }
  }
  world_transforms_.MarkDirty();
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
  for (uint32_t i = 0; i < draw_count; ++i)
    permutation[i] = i;
  std::stable_sort(
    permutation.begin(), permutation.end(), [&](uint32_t a, uint32_t b) {
      const auto& ka = sorting_keys_cpu_soa_[a];
      const auto& kb = sorting_keys_cpu_soa_[b];
      if (ka.pass_mask != kb.pass_mask)
        return ka.pass_mask < kb.pass_mask;
      if (ka.material_index != kb.material_index)
        return ka.material_index < kb.material_index;
      if (ka.geometry_vertex_srv != kb.geometry_vertex_srv)
        return ka.geometry_vertex_srv < kb.geometry_vertex_srv;
      if (ka.geometry_index_srv != kb.geometry_index_srv)
        return ka.geometry_index_srv < kb.geometry_index_srv;
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

auto Renderer::PublishPreparedFrameSpans(std::size_t transform_count) -> void
{
  const auto& tm = scene_prep_state_.transform_mgr;
  if (alias_world_matrices_) {
    const auto world_span = tm.GetWorldMatricesSpan();
    prepared_frame_.world_matrices = std::span<const float>(
      reinterpret_cast<const float*>(world_span.data()),
      world_span.size() * 16u);
  } else {
    prepared_frame_.world_matrices = std::span<const float>(
      world_matrices_cpu_.data(), world_matrices_cpu_.size());
  }
  if (alias_normal_matrices_) {
    const auto normal_span = tm.GetNormalMatricesSpan();
    prepared_frame_.normal_matrices = std::span<const float>(
      reinterpret_cast<const float*>(normal_span.data()),
      normal_span.size() * 16u);
  } else {
    prepared_frame_.normal_matrices = std::span<const float>(
      normal_matrices_cpu_.data(), normal_matrices_cpu_.size());
  }
  prepared_frame_.draw_metadata_bytes = std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(draw_metadata_cpu_soa_.data()),
    draw_metadata_cpu_soa_.size() * sizeof(DrawMetadata));
  if (!alias_world_matrices_) {
    DCHECK_F(world_matrices_cpu_.size() == transform_count * 16u,
      "World matrices size mismatch (unique_count * 16)");
  }
  if (!alias_normal_matrices_) {
    DCHECK_F(normal_matrices_cpu_.size() == transform_count * 16u,
      "Normal matrices size mismatch (unique_count * 16)");
  }
}

auto Renderer::UploadDrawMetadataBindless() -> void
{
  if (draw_metadata_cpu_soa_.empty())
    return;
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
  const engine::FrameContext& frame_context) -> std::size_t
{
  const auto view = camera_view.Resolve();
  return BuildFrame(view, frame_context);
}

//=== FrameGraph Phase Helpers (PhaseId::kFrameGraph) ---------------------//

auto Renderer::ResolveScene(const engine::FrameContext& frame_context)
  -> scene::Scene&
{
  auto scene_ptr = frame_context.GetScene();
  CHECK_NOTNULL_F(scene_ptr, "FrameContext.scene is null in BuildFrame");
  // FIXME: temporary until everything uses the frame context (existing note
  // preserved)
  return *scene_ptr;
}

auto Renderer::InitializeFrameSequence(
  const engine::FrameContext& frame_context) -> void
{
  // Store frame sequence number from FrameContext (PhaseId::kFrameGraph)
  frame_seq_num = frame_context.GetFrameSequenceNumber();
}

auto Renderer::PrepareScenePrepCollectionConfig() const -> BasicCollectionConfig
{
  // Central hook for future renderer policy injection.
  // TODO: pass policy/config from renderer settings (preserved from original
  // comment)
  return sceneprep::CreateBasicCollectionConfig();
}

template <typename CollectionCfg>
auto Renderer::CollectScenePrep(scene::Scene& scene, const View& view,
  sceneprep::ScenePrepState& prep_state, const CollectionCfg& cfg) -> void
{
  // === ScenePrep collection ===
  // Legacy AoS translation step removed; we now directly finalize into SoA
  // arrays (matrices, draw metadata, materials, partitions, sorting keys).
  // FIXME(perf): Avoid per-frame allocations by pooling ScenePrepState or
  // reusing internal vectors (requires clear ownership strategy & lifetime).
  sceneprep::ScenePrepPipelineCollection<CollectionCfg> pipeline { cfg };
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
}

auto Renderer::FinalizeScenePrepPhase(sceneprep::ScenePrepState& prep_state)
  -> void
{
  // New path (SoA finalization) - currently only populates matrix arrays.
  // This is non-destructive and coexists with legacy AoS until passes migrate.
  FinalizeScenePrepSoA(prep_state); // may allocate new material handles
  DLOG_F(1,
    "Renderer BuildFrame finalized SoA frame: collected={} filtered={} "
    "draws={} partitions={}",
    last_finalize_stats_.collected, last_finalize_stats_.filtered,
    prepared_frame_.draw_metadata_bytes.size() / sizeof(DrawMetadata),
    prepared_frame_.partitions.size());
}

// Explicit template instantiation for current collection config
template auto Renderer::CollectScenePrep<Renderer::BasicCollectionConfig>(
  scene::Scene& scene, const View& view, sceneprep::ScenePrepState& prep_state,
  const Renderer::BasicCollectionConfig& cfg) -> void;

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
