//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <unordered_map>

#include <fmt/format.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

namespace {

constexpr oxygen::nexus::DomainKey kDrawMetadataDomain {
  .view_type = oxygen::graphics::ResourceViewType::kStructuredBuffer_SRV,
  .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
};

constexpr std::uint8_t kOpaqueBucketOrder = 0U;
constexpr std::uint8_t kMaskedBucketOrder = 1U;
constexpr std::uint8_t kTransparentBucketOrder = 2U;

auto ResolveBucketOrder(const oxygen::engine::PassMask flags) -> std::uint8_t
{
  if (flags.IsSet(oxygen::engine::PassMaskBit::kOpaque)) {
    return kOpaqueBucketOrder;
  }
  if (flags.IsSet(oxygen::engine::PassMaskBit::kMasked)) {
    return kMaskedBucketOrder;
  }
  return kTransparentBucketOrder;
}

auto ClassifyMaterialPassMask(const oxygen::data::MaterialAsset* mat)
  -> oxygen::engine::PassMask
{
  if (mat == nullptr) {
    return oxygen::engine::PassMask { oxygen::engine::PassMaskBit::kOpaque };
  }
  const auto domain = mat->GetMaterialDomain();

  namespace d = oxygen::data;
  namespace e = oxygen::engine;
  oxygen::engine::PassMask mask {};
  switch (domain) {
  case d::MaterialDomain::kUnknown:
  case d::MaterialDomain::kOpaque:
    mask.Set(e::PassMaskBit::kOpaque);
    break;
  case d::MaterialDomain::kAlphaBlended:
    mask.Set(e::PassMaskBit::kTransparent);
    break;
  case d::MaterialDomain::kMasked:
    mask.Set(e::PassMaskBit::kMasked);
    break;
  case d::MaterialDomain::kDecal:
  case d::MaterialDomain::kUserInterface:
  case d::MaterialDomain::kPostProcess:
    // These material domains do not have dedicated rendering paths yet in the
    // example render-graph. Classify them as transparent to avoid writing depth
    // for alpha-blended style content and to keep them out of the opaque depth
    // pre-pass.
    mask.Set(e::PassMaskBit::kTransparent);
    break;
  default:
    LOG_F(WARNING,
      "Material '{}' has unsupported domain {} (flags=0x{:08X}); "
      "classifying as Opaque",
      mat->GetAssetName(), static_cast<int>(domain), mat->GetFlags());
    mask.Set(e::PassMaskBit::kOpaque);
    break;
  }

  const bool alpha_test_enabled
    = (mat->GetFlags() & oxygen::data::pak::render::kMaterialFlag_AlphaTest)
    != 0U;
  if (alpha_test_enabled && !mask.IsSet(e::PassMaskBit::kTransparent)) {
    mask.Unset(e::PassMaskBit::kOpaque);
    mask.Set(e::PassMaskBit::kMasked);
  }

  // Double-sided is explicit and data-driven via the PAK material flag.
  // Render passes use it to pick appropriate cull mode.
  if (mat->IsDoubleSided()) {
    mask.Set(e::PassMaskBit::kDoubleSided);
  }

  DLOG_F(2, "Material classify: name='{}' domain={} flags=0x{:08X} -> {}",
    mat->GetAssetName(), static_cast<int>(domain), mat->GetFlags(), mask);
  return mask;
}

auto ApplyShadowCasterPassRouting(oxygen::engine::PassMask mask,
  const bool cast_shadows) -> oxygen::engine::PassMask
{
  if (!cast_shadows) {
    return mask;
  }

  const bool supports_shadow_casting
    = mask.IsSet(oxygen::engine::PassMaskBit::kOpaque)
    || mask.IsSet(oxygen::engine::PassMaskBit::kMasked);
  if (supports_shadow_casting) {
    mask.Set(oxygen::engine::PassMaskBit::kShadowCaster);
  }
  return mask;
}

auto ApplyMainViewPassRouting(oxygen::engine::PassMask mask,
  const bool main_view_visible) -> oxygen::engine::PassMask
{
  if (main_view_visible) {
    mask.Set(oxygen::engine::PassMaskBit::kMainViewVisible);
  }
  return mask;
}

} // namespace

namespace oxygen::renderer::resources {

DrawMetadataEmitter::DrawMetadataEmitter(observer_ptr<Graphics> gfx,
  observer_ptr<engine::upload::StagingProvider> provider,
  observer_ptr<renderer::resources::GeometryUploader> geometry,
  observer_ptr<renderer::resources::MaterialBinder> materials,
  observer_ptr<engine::upload::InlineTransfersCoordinator>
    inline_transfers) noexcept
  : gfx_(gfx)
  , geometry_uploader_(geometry)
  , material_binder_(materials)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
  , slot_reuse_(
      [this](oxygen::nexus::DomainKey /*domain*/) -> bindless::HeapIndex {
        return bindless::HeapIndex { frame_write_count_ };
      },
      [](oxygen::nexus::DomainKey /*domain*/, bindless::HeapIndex /*index*/) {},
      slot_reclaimer_)
  , draw_metadata_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(oxygen::engine::DrawMetadata)),
      inline_transfers_, "DrawMetadataEmitter.Draws")
  , draw_bounds_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(glm::vec4)), inline_transfers_,
      "DrawMetadataEmitter.DrawBounds")
  , instance_data_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(std::uint32_t)), inline_transfers_,
      "DrawMetadataEmitter.InstanceData")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");
  DCHECK_NOTNULL_F(inline_transfers_,
    "DrawMetadataEmitter requires InlineTransfersCoordinator");
}

DrawMetadataEmitter::~DrawMetadataEmitter()
{
  const auto telemetry = slot_reuse_.GetTelemetrySnapshot();
  const auto expected_zero_marker = [](const uint64_t value) -> const char* {
    return value == 0U ? " \u2713" : " (expected 0) !";
  };

  LOG_SCOPE_F(INFO, "DrawMetadataEmitter Statistics");
  LOG_F(INFO, "frames started    : {}", frames_started_count_);
  LOG_F(INFO, "nexus.allocate_calls      : {}", telemetry.allocate_calls);
  LOG_F(INFO, "nexus.release_calls       : {}{}", telemetry.release_calls,
    expected_zero_marker(telemetry.release_calls));
  LOG_F(INFO, "nexus.stale_reject_count  : {}{}", telemetry.stale_reject_count,
    expected_zero_marker(telemetry.stale_reject_count));
  LOG_F(INFO, "nexus.duplicate_rejects   : {}{}",
    telemetry.duplicate_reject_count,
    expected_zero_marker(telemetry.duplicate_reject_count));
  LOG_F(INFO, "nexus.reclaimed_count     : {}{}", telemetry.reclaimed_count,
    expected_zero_marker(telemetry.reclaimed_count));
  LOG_F(INFO, "nexus.pending_count       : {}{}", telemetry.pending_count,
    expected_zero_marker(telemetry.pending_count));
  LOG_F(INFO, "sort calls        : {}", sort_calls_count_);
  LOG_F(INFO, "peak draws        : {}", peak_draws_);
  LOG_F(INFO, "peak partitions   : {}", peak_partitions_);
}

auto DrawMetadataEmitter::OnFrameStart(renderer::RendererTag /*tag*/,
  oxygen::frame::SequenceNumber sequence, oxygen::frame::Slot slot) -> void
{
  slot_reuse_.OnBeginFrame(slot);
  frame_write_count_ = 0U;

  // Reset per-frame CPU state; keep GPU resources
  Cpu().clear();
  keys_.clear();
  partitions_.clear();
  draw_bounding_spheres_.clear();
  instance_transform_indices_.clear();
  draw_metadata_buffer_.OnFrameStart(sequence, slot);
  draw_bounds_buffer_.OnFrameStart(sequence, slot);
  instance_data_buffer_.OnFrameStart(sequence, slot);
  draw_metadata_srv_index_ = kInvalidShaderVisibleIndex;
  draw_bounds_srv_index_ = kInvalidShaderVisibleIndex;
  instance_data_srv_index_ = kInvalidShaderVisibleIndex;
  ++frames_started_count_;
}

auto DrawMetadataEmitter::EmitDrawMetadata(
  const oxygen::engine::sceneprep::RenderItemData& item) -> void
{
  if (!item.geometry.IsValid()) {
    return;
  }
  const auto submesh_index = item.submesh_index;
  const auto& lod = *item.geometry.mesh;
  const auto submeshes_span = lod.SubMeshes();
  if (submesh_index >= submeshes_span.size()) {
    return;
  }
  const auto& submesh = submeshes_span[submesh_index];
  const auto views_span = submesh.MeshViews();
  if (views_span.empty()) {
    return;
  }
  // Acquire geometry handle once per LOD mesh; GeometryUploader interns by
  // stable identity `(AssetKey, lod_index)`.
  auto geo_handle = geometry_uploader_
    ? geometry_uploader_->GetOrAllocate(item.geometry)
    : oxygen::engine::sceneprep::kInvalidGeometryHandle;

  // Resolve SRV indices once per LOD mesh. If geometry is not resident (or
  // upload failed), indices remain invalid and the draw is skipped.
  const auto indices = geometry_uploader_
    ? geometry_uploader_->GetShaderVisibleIndices(geo_handle)
    : oxygen::renderer::resources::GeometryUploader::
        MeshShaderVisibleIndices {};

  for (const auto& view : views_span) {
    const auto index_view = view.IndexBuffer();
    const bool has_indices = index_view.Count() > 0;

    // Final runtime behavior (agreed): render nothing when geometry is invalid
    // or not yet resident. Never issue a draw that references invalid bindless
    // indices.
    if (indices.vertex_srv_index == kInvalidShaderVisibleIndex) {
      continue;
    }
    if (has_indices && indices.index_srv_index == kInvalidShaderVisibleIndex) {
      continue;
    }

    oxygen::engine::DrawMetadata dm {};
    dm.vertex_buffer_index = indices.vertex_srv_index;
    dm.index_buffer_index = indices.index_srv_index;

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

    // Stable material handle
    // Resolve material handle via MaterialBinder if available
    if (material_binder_ && item.material.IsValid()) {
      const auto stable_handle = material_binder_->GetOrAllocate(item.material);
      const auto u_stable_handle = stable_handle.get();
      dm.material_handle = u_stable_handle;
    }

    // Transform indirection
    const auto handle = item.transform_handle;
    const auto u_handle = handle.get();
    dm.transform_index = u_handle;
    dm.instance_metadata_buffer_index = 0;
    dm.instance_metadata_offset = 0;
    dm.flags = ApplyMainViewPassRouting(
      ApplyShadowCasterPassRouting(
        ClassifyMaterialPassMask(item.material.resolved_asset.get()),
        item.cast_shadows),
      item.main_view_visible);
    DCHECK_F(handle != oxygen::engine::sceneprep::kInvalidTransformHandle,
      "Invalid transform handle while emitting");
    DCHECK_F(!dm.flags.IsEmpty(), "flags cannot be empty after assignment");

    const std::uint8_t bucket_order = ResolveBucketOrder(dm.flags);
    const auto index
      = slot_reuse_.Allocate(kDrawMetadataDomain).ToBindlessHandle().get();
    if (index >= Cpu().size()) {
      Cpu().resize(static_cast<size_t>(index) + 1U);
      keys_.resize(static_cast<size_t>(index) + 1U);
      draw_bounding_spheres_.resize(static_cast<size_t>(index) + 1U);
    }
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    Cpu()[index] = dm;
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    keys_[index] = SortingKey {
      .pass_mask = dm.flags,
      .bucket_order = bucket_order,
      .sort_distance2 = item.sort_distance2,
      .material_index = dm.material_handle,
      .vb_srv = dm.vertex_buffer_index,
      .ib_srv = dm.index_buffer_index,
    };
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    draw_bounding_spheres_[index] = item.world_bounding_sphere;
    ++frame_write_count_;
  }
}

auto DrawMetadataEmitter::SortAndPartition() -> void
{
  // Apply GPU instancing batches before sorting
  ApplyInstancingBatches();
  BuildSortingAndPartitions();
}

auto DrawMetadataEmitter::BuildSortingAndPartitions() -> void
{
  if (keys_.size() != Cpu().size()) {
    // Fallback path: rebuild keys from DrawMetadata only. This loses
    // view-relative sorting information but preserves deterministic ordering.
    keys_.clear();
    keys_.reserve(Cpu().size());
    for (const auto& d : Cpu()) {
      const std::uint8_t bucket_order = ResolveBucketOrder(d.flags);
      keys_.push_back(SortingKey {
        .pass_mask = d.flags,
        .bucket_order = bucket_order,
        .sort_distance2 = 0.0F,
        .material_index = d.material_handle,
        .vb_srv = d.vertex_buffer_index,
        .ib_srv = d.index_buffer_index,
      });
    }
  }

  const auto t_sort_begin = std::chrono::high_resolution_clock::now();
  last_pre_sort_hash_
    = oxygen::ComputeFNV1a64(keys_.data(), keys_.size() * sizeof(SortingKey));

  const auto n = Cpu().size();
  DCHECK_LE_F(n, (std::numeric_limits<std::uint32_t>::max)());
  const auto u_draw_count = static_cast<std::uint32_t>(n);
  std::vector<std::uint32_t> perm(n);
  for (std::size_t i = 0; i < n; ++i) {
    perm[i] = static_cast<std::uint32_t>(i);
  }
  std::ranges::stable_sort(perm, [&](std::uint32_t a, std::uint32_t b) {
    const auto& ka = keys_[a];
    const auto& kb = keys_[b];
    if (ka.bucket_order != kb.bucket_order) {
      return ka.bucket_order < kb.bucket_order;
    }

    // Transparent: strict back-to-front ordering by distance first.
    if (ka.bucket_order == 2) {
      if (ka.sort_distance2 != kb.sort_distance2) {
        return ka.sort_distance2 > kb.sort_distance2;
      }
    }

    if (ka.pass_mask != kb.pass_mask) {
      return ka.pass_mask < kb.pass_mask;
    }
    if (ka.material_index != kb.material_index) {
      return ka.material_index < kb.material_index;
    }
    if (ka.vb_srv != kb.vb_srv) {
      return ka.vb_srv < kb.vb_srv;
    }
    if (ka.ib_srv != kb.ib_srv) {
      return ka.ib_srv < kb.ib_srv;
    }
    return a < b;
  });

  std::vector<oxygen::engine::DrawMetadata> reordered;
  reordered.reserve(n);
  std::vector<SortingKey> reordered_keys;
  reordered_keys.reserve(n);
  std::vector<glm::vec4> reordered_bounds;
  reordered_bounds.reserve(n);
  for (auto idx : perm) {
    reordered.push_back(Cpu()[idx]);
    reordered_keys.push_back(keys_[idx]);
    reordered_bounds.push_back(draw_bounding_spheres_[idx]);
  }
  Cpu().swap(reordered);
  keys_.swap(reordered_keys);
  draw_bounding_spheres_.swap(reordered_bounds);

  last_order_hash_
    = oxygen::ComputeFNV1a64(keys_.data(), keys_.size() * sizeof(SortingKey));

  partitions_.clear();
  if (!Cpu().empty()) {
    auto current_mask = Cpu().front().flags;
    std::uint32_t range_begin = 0U;
    for (std::uint32_t i = 1; i < u_draw_count; ++i) {
      const auto mask = Cpu()[i].flags;
      if (mask != current_mask) {
        partitions_.push_back(
          oxygen::engine::PreparedSceneFrame::PartitionRange {
            .pass_mask = current_mask,
            .begin = range_begin,
            .end = i,
          });
        current_mask = mask;
        range_begin = i;
      }
    }
    partitions_.push_back(oxygen::engine::PreparedSceneFrame::PartitionRange {
      .pass_mask = current_mask,
      .begin = range_begin,
      .end = u_draw_count,
    });
  }

  const auto t_sort_end = std::chrono::high_resolution_clock::now();
  last_sort_time_ = std::chrono::duration_cast<std::chrono::microseconds>(
    t_sort_end - t_sort_begin);
  ++sort_calls_count_;
  peak_draws_
    = (std::max)(peak_draws_, static_cast<std::uint32_t>(Cpu().size()));
  peak_partitions_ = (std::max)(peak_partitions_,
    static_cast<std::uint32_t>(partitions_.size()));
}

auto DrawMetadataEmitter::EnsureFrameResources() -> void
{
  if (Cpu().empty()) {
    return;
  }

  const auto count = static_cast<std::uint32_t>(Cpu().size());
  auto result = draw_metadata_buffer_.Allocate(count);
  if (!result) {
    LOG_F(ERROR, "Transient allocation failed: {}", result.error().message());
    return;
  }
  const auto alloc = *result;
  auto* ptr = alloc.mapped_ptr;
  if (ptr == nullptr) {
    LOG_F(ERROR, "Mapped pointer is null after allocate");
    return;
  }

  DLOG_F(1, "Writing {} draw metadata to {}", count, fmt::ptr(ptr));

  std::memcpy(
    ptr, Cpu().data(), Cpu().size() * sizeof(oxygen::engine::DrawMetadata));

  // Store the SRV index for the most recent allocation. This may change per
  // view when the emitter is used in per-view mode; callers should query the
  // SRV index after Finalize/EnsureFrameResources.
  draw_metadata_srv_index_ = alloc.srv;

  if (!draw_bounding_spheres_.empty()) {
    const auto bounds_count
      = static_cast<std::uint32_t>(draw_bounding_spheres_.size());
    auto bounds_result = draw_bounds_buffer_.Allocate(bounds_count);
    if (!bounds_result) {
      LOG_F(ERROR, "Draw bounds allocation failed: {}",
        bounds_result.error().message());
      return;
    }
    const auto bounds_alloc = *bounds_result;
    auto* bounds_ptr = bounds_alloc.mapped_ptr;
    if (bounds_ptr != nullptr) {
      std::memcpy(bounds_ptr, draw_bounding_spheres_.data(),
        draw_bounding_spheres_.size() * sizeof(glm::vec4));
      draw_bounds_srv_index_ = bounds_alloc.srv;
    }
  }

  // Upload instance data buffer if we have instanced draws
  if (!instance_transform_indices_.empty()) {
    const auto instance_count
      = static_cast<std::uint32_t>(instance_transform_indices_.size());
    auto instance_result = instance_data_buffer_.Allocate(instance_count);
    if (!instance_result) {
      LOG_F(ERROR, "Instance data allocation failed: {}",
        instance_result.error().message());
      return;
    }
    const auto instance_alloc = *instance_result;
    auto* instance_ptr = instance_alloc.mapped_ptr;
    if (instance_ptr != nullptr) {
      std::memcpy(instance_ptr, instance_transform_indices_.data(),
        instance_transform_indices_.size() * sizeof(std::uint32_t));
      instance_data_srv_index_ = instance_alloc.srv;
      DLOG_F(1, "Uploaded {} instance transform indices", instance_count);
    }
  }
}

auto DrawMetadataEmitter::GetDrawMetadataSrvIndex() -> ShaderVisibleIndex
{
  if (draw_metadata_srv_index_ == kInvalidShaderVisibleIndex) {
    EnsureFrameResources();
  }
  return draw_metadata_srv_index_;
}

auto DrawMetadataEmitter::GetDrawMetadataBytes() const noexcept
  -> std::span<const std::byte>
{
  return std::as_bytes(std::span { Cpu().data(), Cpu().size() });
}

auto DrawMetadataEmitter::GetPartitions() const noexcept
  -> std::span<const oxygen::engine::PreparedSceneFrame::PartitionRange>
{
  return { partitions_.data(), partitions_.size() };
}

auto DrawMetadataEmitter::GetDrawBoundingSpheres() const noexcept
  -> std::span<const glm::vec4>
{
  return { draw_bounding_spheres_.data(), draw_bounding_spheres_.size() };
}

auto DrawMetadataEmitter::GetDrawBoundingSpheresSrvIndex() -> ShaderVisibleIndex
{
  if (draw_bounds_srv_index_ == kInvalidShaderVisibleIndex) {
    EnsureFrameResources();
  }
  return draw_bounds_srv_index_;
}

auto DrawMetadataEmitter::GetInstanceDataSrvIndex() const noexcept
  -> ShaderVisibleIndex
{
  return instance_data_srv_index_;
}

auto DrawMetadataEmitter::BatchingKeyHash::operator()(
  const BatchingKey& key) const noexcept -> std::size_t
{
  std::size_t hash = 0;
  oxygen::HashCombine(hash, key.vertex_buffer_index.get());
  oxygen::HashCombine(hash, key.index_buffer_index.get());
  oxygen::HashCombine(hash, key.first_index);
  oxygen::HashCombine(hash, static_cast<std::uint32_t>(key.base_vertex));
  oxygen::HashCombine(hash, key.material_handle);
  oxygen::HashCombine(hash, key.index_count);
  oxygen::HashCombine(hash, key.vertex_count);
  oxygen::HashCombine(hash, key.is_indexed);
  oxygen::HashCombine(hash, key.flags.get());
  return hash;
}

auto DrawMetadataEmitter::ApplyInstancingBatches() -> void
{
  if (Cpu().empty()) {
    return;
  }
  const auto initial_draw_count = Cpu().size();

  // Build deterministic groups in first-seen draw order.
  // Using unordered_map iteration directly would make batch emission order
  // dependent on hash-bucket layout.
  std::unordered_map<BatchingKey, std::uint32_t, BatchingKeyHash>
    key_to_group_index;
  key_to_group_index.reserve(Cpu().size());
  std::vector<std::vector<std::uint32_t>> groups;
  groups.reserve(Cpu().size());

  for (std::uint32_t i = 0U; i < static_cast<std::uint32_t>(Cpu().size());
    ++i) {
    const auto& dm = Cpu()[i];
    const BatchingKey key {
      .vertex_buffer_index = dm.vertex_buffer_index,
      .index_buffer_index = dm.index_buffer_index,
      .first_index = dm.first_index,
      .base_vertex = dm.base_vertex,
      .material_handle = dm.material_handle,
      .index_count = dm.index_count,
      .vertex_count = dm.vertex_count,
      .is_indexed = dm.is_indexed,
      .flags = dm.flags,
    };
    if (const auto it = key_to_group_index.find(key);
      it != key_to_group_index.end()) {
      groups[it->second].push_back(i);
    } else {
      const auto new_group_index = static_cast<std::uint32_t>(groups.size());
      key_to_group_index.emplace(key, new_group_index);
      groups.emplace_back();
      groups.back().push_back(i);
    }
  }

  // Check if any batching is possible
  bool has_batches = false;
  for (const auto& indices : groups) {
    if (indices.size() > 1U) {
      has_batches = true;
      break;
    }
  }

  if (!has_batches) {
    // No batching opportunities - keep single-instance draws
    return;
  }

  // Rebuild cpu_ with batched draws and populate instance data
  std::vector<oxygen::engine::DrawMetadata> batched_cpu;
  std::vector<SortingKey> batched_keys;
  std::vector<glm::vec4> batched_bounds;
  batched_cpu.reserve(initial_draw_count);
  batched_keys.reserve(initial_draw_count);
  batched_bounds.reserve(initial_draw_count);
  instance_transform_indices_.clear();
  instance_transform_indices_.reserve(Cpu().size());

  for (const auto& indices : groups) {
    const auto instance_count = static_cast<std::uint32_t>(indices.size());

    // Use first draw as representative
    auto dm = Cpu()[indices[0]];
    dm.instance_count = instance_count;

    if (instance_count > 1) {
      // Multi-instance: record offset into instance data buffer
      dm.instance_metadata_offset
        = static_cast<std::uint32_t>(instance_transform_indices_.size());
      dm.instance_metadata_buffer_index = 1; // Signal that instance data is
                                             // used

      // Append all transform indices for this batch
      for (const auto draw_idx : indices) {
        instance_transform_indices_.push_back(Cpu()[draw_idx].transform_index);
      }
    } else {
      // Single instance: no instance data needed
      dm.instance_metadata_offset = 0;
      dm.instance_metadata_buffer_index = 0;
    }

    batched_cpu.push_back(dm);

    // Build sorting key for this batched draw
    const std::uint8_t bucket_order = ResolveBucketOrder(dm.flags);

    // For batched draws, use the average sort distance (or first item's)
    float batch_sort_distance2 = 0.0F;
    if (!indices.empty() && indices[0] < keys_.size()) {
      batch_sort_distance2 = keys_[indices[0]].sort_distance2;
    }

    batched_keys.push_back(SortingKey {
      .pass_mask = dm.flags,
      .bucket_order = bucket_order,
      .sort_distance2 = batch_sort_distance2,
      .material_index = dm.material_handle,
      .vb_srv = dm.vertex_buffer_index,
      .ib_srv = dm.index_buffer_index,
    });

    glm::vec4 merged_bound { 0.0F, 0.0F, 0.0F, 0.0F };
    if (!indices.empty() && indices[0] < draw_bounding_spheres_.size()) {
      glm::vec3 bounds_min { (std::numeric_limits<float>::max)() };
      glm::vec3 bounds_max { (std::numeric_limits<float>::lowest)() };
      bool have_valid_bound = false;
      for (const auto draw_idx : indices) {
        if (draw_idx >= draw_bounding_spheres_.size()) {
          continue;
        }
        const auto& sphere = draw_bounding_spheres_[draw_idx];
        if (sphere.w <= 0.0F) {
          continue;
        }
        const glm::vec3 center { sphere.x, sphere.y, sphere.z };
        const glm::vec3 radius_vec { sphere.w, sphere.w, sphere.w };
        bounds_min = glm::min(bounds_min, center - radius_vec);
        bounds_max = glm::max(bounds_max, center + radius_vec);
        have_valid_bound = true;
      }
      if (have_valid_bound) {
        const glm::vec3 merged_center = 0.5F * (bounds_min + bounds_max);
        float merged_radius = 0.0F;
        for (const auto draw_idx : indices) {
          if (draw_idx >= draw_bounding_spheres_.size()) {
            continue;
          }
          const auto& sphere = draw_bounding_spheres_[draw_idx];
          if (sphere.w <= 0.0F) {
            continue;
          }
          merged_radius = (std::max)(merged_radius,
            glm::distance(merged_center, glm::vec3(sphere)) + sphere.w);
        }
        merged_bound = glm::vec4(
          merged_center.x, merged_center.y, merged_center.z, merged_radius);
      } else {
        merged_bound = draw_bounding_spheres_[indices[0]];
      }
    }
    batched_bounds.push_back(merged_bound);
  }

  Cpu().swap(batched_cpu);
  keys_.swap(batched_keys);
  draw_bounding_spheres_.swap(batched_bounds);

  DLOG_F(1, "Batched {} draws into {} batches, {} instance indices",
    initial_draw_count, Cpu().size(), instance_transform_indices_.size());
}

} // namespace oxygen::renderer::resources
