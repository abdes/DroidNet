//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

#include <fmt/format.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

namespace {

auto ClassifyMaterialPassMask(const oxygen::data::MaterialAsset* mat)
  -> oxygen::engine::PassMask
{
  if (!mat) {
    return oxygen::engine::PassMask { oxygen::engine::PassMaskBit::kOpaque };
  }
  const auto domain = mat->GetMaterialDomain();

  oxygen::engine::PassMask mask {};
  switch (domain) {
  case oxygen::data::MaterialDomain::kOpaque:
    mask.Set(oxygen::engine::PassMaskBit::kOpaque);
    break;
  case oxygen::data::MaterialDomain::kAlphaBlended:
    mask.Set(oxygen::engine::PassMaskBit::kTransparent);
    break;
  case oxygen::data::MaterialDomain::kMasked:
    mask.Set(oxygen::engine::PassMaskBit::kMasked);
    break;
  default:
    mask.Set(oxygen::engine::PassMaskBit::kTransparent);
    break;
  }

  // Double-sided is explicit and data-driven via the PAK material flag.
  // Render passes use it to pick appropriate cull mode.
  if (mat->IsDoubleSided()) {
    mask.Set(oxygen::engine::PassMaskBit::kDoubleSided);
  }

  DLOG_F(2, "Material classify: name='{}' domain={} flags=0x{:08X} -> {}",
    mat->GetAssetName(), static_cast<int>(domain), mat->GetFlags(),
    oxygen::engine::to_string(mask));
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
  , draw_metadata_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(oxygen::engine::DrawMetadata)),
      inline_transfers_, "DrawMetadataEmitter.Draws")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");
  DCHECK_NOTNULL_F(inline_transfers_,
    "DrawMetadataEmitter requires InlineTransfersCoordinator");
}

DrawMetadataEmitter::~DrawMetadataEmitter()
{
  LOG_SCOPE_F(INFO, "DrawMetadataEmitter Statistics");
  LOG_F(INFO, "frames started    : {}", frames_started_count_);
  LOG_F(INFO, "total emits       : {}", total_emits_);
  LOG_F(INFO, "sort calls        : {}", sort_calls_count_);
  LOG_F(INFO, "peak draws        : {}", peak_draws_);
  LOG_F(INFO, "peak partitions   : {}", peak_partitions_);
}

auto DrawMetadataEmitter::OnFrameStart(renderer::RendererTag,
  oxygen::frame::SequenceNumber sequence, oxygen::frame::Slot slot) -> void
{
  // Reset per-frame CPU state; keep GPU resources
  Cpu().clear();
  keys_.clear();
  partitions_.clear();
  draw_metadata_buffer_.OnFrameStart(sequence, slot);
  draw_metadata_srv_index_ = kInvalidShaderVisibleIndex;
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
    dm.flags = ClassifyMaterialPassMask(item.material.resolved_asset.get());
    DCHECK_F(handle != oxygen::engine::sceneprep::kInvalidTransformHandle,
      "Invalid transform handle while emitting");
    DCHECK_F(!dm.flags.IsEmpty(), "flags cannot be empty after assignment");

    this->Cpu().push_back(dm);

    const std::uint8_t bucket_order
      = dm.flags.IsSet(oxygen::engine::PassMaskBit::kOpaque)
      ? static_cast<std::uint8_t>(0)
      : (dm.flags.IsSet(oxygen::engine::PassMaskBit::kMasked)
            ? static_cast<std::uint8_t>(1)
            : static_cast<std::uint8_t>(2));
    keys_.push_back(SortingKey {
      .pass_mask = dm.flags,
      .bucket_order = bucket_order,
      .sort_distance2 = item.sort_distance2,
      .material_index = dm.material_handle,
      .vb_srv = dm.vertex_buffer_index,
      .ib_srv = dm.index_buffer_index,
    });

    ++total_emits_;
  }
}

auto DrawMetadataEmitter::SortAndPartition() -> void
{
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
      const std::uint8_t bucket_order
        = d.flags.IsSet(oxygen::engine::PassMaskBit::kOpaque)
        ? static_cast<std::uint8_t>(0)
        : (d.flags.IsSet(oxygen::engine::PassMaskBit::kMasked)
              ? static_cast<std::uint8_t>(1)
              : static_cast<std::uint8_t>(2));
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
  std::stable_sort(
    perm.begin(), perm.end(), [&](std::uint32_t a, std::uint32_t b) {
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
  for (auto idx : perm) {
    reordered.push_back(Cpu()[idx]);
    reordered_keys.push_back(keys_[idx]);
  }
  Cpu().swap(reordered);
  keys_.swap(reordered_keys);

  last_order_hash_
    = oxygen::ComputeFNV1a64(keys_.data(), keys_.size() * sizeof(SortingKey));

  partitions_.clear();
  if (!Cpu().empty()) {
    auto current_mask = Cpu().front().flags;
    std::uint32_t range_begin = 0u;
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
  DLOG_F(2,
    "DrawMetadataEmitter: pre=0x{:016X} post=0x{:016X} draws={} partitions={} "
    "keys_bytes={} sort_time_us={}",
    last_pre_sort_hash_, last_order_hash_, Cpu().size(), partitions_.size(),
    keys_.size() * sizeof(SortingKey), last_sort_time_.count());
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
    LOG_F(ERROR, "DrawMetadataEmitter: transient allocation failed: {}",
      result.error().message());
    return;
  }
  const auto alloc = *result;
  auto* ptr = alloc.mapped_ptr;
  if (!ptr) {
    LOG_F(ERROR, "DrawMetadataEmitter: mapped pointer is null after allocate");
    return;
  }

  DLOG_F(1, "DrawMetadataEmitter writing {} draw metadata to {}", count,
    fmt::ptr(ptr));

  std::memcpy(
    ptr, Cpu().data(), Cpu().size() * sizeof(oxygen::engine::DrawMetadata));

  // Store the SRV index for the most recent allocation. This may change per
  // view when the emitter is used in per-view mode; callers should query the
  // SRV index after Finalize/EnsureFrameResources.
  draw_metadata_srv_index_ = alloc.srv;
}

auto DrawMetadataEmitter::GetDrawMetadataSrvIndex() const -> ShaderVisibleIndex
{
  if (draw_metadata_srv_index_ == kInvalidShaderVisibleIndex) {
    const_cast<DrawMetadataEmitter*>(this)->EnsureFrameResources();
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

} // namespace oxygen::renderer::resources
