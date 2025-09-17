//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <span>

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
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

using oxygen::engine::DrawMetadata;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::upload::AtlasBuffer;
using oxygen::engine::upload::UploadBufferDesc;
using oxygen::engine::upload::UploadDataView;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadRequest;

namespace {

auto ClassifyMaterialPassMask(const oxygen::data::MaterialAsset* mat)
  -> oxygen::engine::PassMask
{
  if (!mat) {
    return oxygen::engine::PassMask {
      oxygen::engine::PassMaskBit::kOpaqueOrMasked,
    };
  }
  const auto domain = mat->GetMaterialDomain();
  const auto base = mat->GetBaseColor();
  const float alpha = base[3];
  const bool is_opaque_domain
    = (domain == oxygen::data::MaterialDomain::kOpaque);
  const bool is_masked_domain
    = (domain == oxygen::data::MaterialDomain::kMasked);
  DLOG_F(2,
    "Material classify: name='{}' domain={} alpha={:.3f} is_opaque={} "
    "is_masked={}",
    mat->GetAssetName(), static_cast<int>(domain), alpha, is_opaque_domain,
    is_masked_domain);
  if (is_opaque_domain && alpha >= 0.999f) {
    return oxygen::engine::PassMask {
      oxygen::engine::PassMaskBit::kOpaqueOrMasked,
    };
  }
  if (is_masked_domain) {
    return oxygen::engine::PassMask {
      oxygen::engine::PassMaskBit::kOpaqueOrMasked
    };
  }
  DLOG_F(2,
    " -> classified as Transparent (flags={}) due to domain {} and "
    "alpha={:.3f}",
    oxygen::engine::PassMask { oxygen::engine::PassMaskBit::kTransparent },
    static_cast<int>(domain), alpha);
  return oxygen::engine::PassMask { oxygen::engine::PassMaskBit::kTransparent };
}

} // namespace

namespace oxygen::renderer::resources {

DrawMetadataEmitter::DrawMetadataEmitter(observer_ptr<Graphics> gfx,
  observer_ptr<engine::upload::UploadCoordinator> uploader,
  observer_ptr<engine::upload::StagingProvider> provider,
  observer_ptr<renderer::resources::GeometryUploader> geometry,
  observer_ptr<renderer::resources::MaterialBinder> materials) noexcept
  : gfx_(std::move(gfx))
  , uploader_(std::move(uploader))
  , staging_provider_(std::move(provider))
  , geometry_uploader_(std::move(geometry))
  , material_binder_(std::move(materials))
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");
}

DrawMetadataEmitter::~DrawMetadataEmitter()
{
  LOG_SCOPE_F(INFO, "DrawMetadataEmitter Statistics");
  LOG_F(INFO, "frames started    : {}", frames_started_count_);
  LOG_F(INFO, "total emits       : {}", total_emits_);
  LOG_F(INFO, "sort calls        : {}", sort_calls_count_);
  LOG_F(INFO, "upload operations : {}", upload_operations_count_);
  LOG_F(INFO, "peak draws        : {}", peak_draws_);
  LOG_F(INFO, "peak partitions   : {}", peak_partitions_);

  if (atlas_) {
    const auto s = atlas_->GetStats();
    LOG_SCOPE_F(INFO, "DrawMetadata Atlas Buffer");
    LOG_F(INFO, "ensure calls      : {}", s.ensure_calls);
    LOG_F(INFO, "allocations       : {}", s.allocations);
    LOG_F(INFO, "releases          : {}", s.releases);
    LOG_F(INFO, "capacity (elems)  : {}", s.capacity_elements);
    LOG_F(INFO, "next index        : {}", s.next_index);
    LOG_F(INFO, "free list size    : {}", s.free_list_size);
  }
}

auto DrawMetadataEmitter::OnFrameStart(
  renderer::RendererTag, oxygen::frame::Slot slot) -> void
{
  // Reset per-frame CPU state; keep GPU resources
  Cpu().clear();
  keys_.clear();
  partitions_.clear();
  current_frame_slot_ = slot;
  last_frame_slot_ = slot;
  if (!atlas_) {
    // Lazily construct atlas for DrawMetadata with correct stride
    atlas_ = std::make_unique<AtlasBuffer>(gfx_,
      static_cast<std::uint32_t>(sizeof(oxygen::engine::DrawMetadata)),
      "DrawMetadata");
  }
  if (atlas_) {
    atlas_->OnFrameStart(slot);
  }
  ++frames_started_count_;
}

auto DrawMetadataEmitter::EmitDrawMetadata(
  const oxygen::engine::sceneprep::RenderItemData& item) -> void
{
  if (!item.geometry) {
    return;
  }
  const auto lod_index = item.lod_index;
  const auto submesh_index = item.submesh_index;
  const auto& geom = *item.geometry;
  auto meshes_span = geom.Meshes();
  if (lod_index >= meshes_span.size()) {
    return;
  }
  const auto& lod_mesh_ptr = meshes_span[lod_index];
  if (!lod_mesh_ptr) {
    return;
  }
  const auto& lod = *lod_mesh_ptr;
  auto submeshes_span = lod.SubMeshes();
  if (submesh_index >= submeshes_span.size()) {
    return;
  }
  const auto& submesh = submeshes_span[submesh_index];
  auto views_span = submesh.MeshViews();
  if (views_span.empty()) {
    return;
  }
  // Acquire geometry handle once per lod mesh; GeometryUploader dedups.
  auto& lod_mesh = *lod_mesh_ptr;
  auto geo_handle = geometry_uploader_
    ? geometry_uploader_->GetOrAllocate(lod_mesh)
    : oxygen::engine::sceneprep::kInvalidGeometryHandle;
  for (const auto& view : views_span) {
    DrawMetadata dm {};
    // Resolve SRV indices immediately (geometry uploads now happen earlier).
    if (geometry_uploader_) {
      const auto indices
        = geometry_uploader_->GetShaderVisibleIndices(geo_handle);
      dm.vertex_buffer_index = indices.vertex_srv_index;
      dm.index_buffer_index = indices.index_srv_index;
    } else {
      dm.vertex_buffer_index = ShaderVisibleIndex {};
      dm.index_buffer_index = ShaderVisibleIndex {};
    }

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

    // Stable material handle
    // Resolve material handle via MaterialBinder if available
    if (material_binder_ && item.material) {
      const auto stable_handle = material_binder_->GetOrAllocate(item.material);
      dm.material_handle = stable_handle.get();
    }

    // Transform indirection
    const auto handle = item.transform_handle;
    dm.transform_index = handle.get();
    dm.instance_metadata_buffer_index = 0;
    dm.instance_metadata_offset = 0;
    dm.flags = ClassifyMaterialPassMask(item.material.get());
    DCHECK_F(handle != oxygen::engine::sceneprep::kInvalidTransformHandle,
      "Invalid transform handle while emitting");
    DCHECK_F(!dm.flags.IsEmpty(), "flags cannot be empty after assignment");

    this->Cpu().push_back(dm);
    ++total_emits_;
  }
}

auto DrawMetadataEmitter::SortAndPartition() -> void
{
  BuildSortingAndPartitions();
}

auto DrawMetadataEmitter::BuildSortingAndPartitions() -> void
{
  keys_.clear();
  keys_.reserve(Cpu().size());
  for (const auto& d : Cpu()) {
    keys_.push_back(SortingKey {
      .pass_mask = d.flags,
      .material_index = d.material_handle,
      .vb_srv = d.vertex_buffer_index,
      .ib_srv = d.index_buffer_index,
    });
  }

  const auto t_sort_begin = std::chrono::high_resolution_clock::now();
  last_pre_sort_hash_
    = oxygen::ComputeFNV1a64(keys_.data(), keys_.size() * sizeof(SortingKey));

  const size_t n = Cpu().size();
  std::vector<uint32_t> perm(n);
  for (uint32_t i = 0; i < n; ++i) {
    perm[i] = i;
  }
  std::stable_sort(perm.begin(), perm.end(), [&](uint32_t a, uint32_t b) {
    const auto& ka = keys_[a];
    const auto& kb = keys_[b];
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

  std::vector<DrawMetadata> reordered;
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
    uint32_t range_begin = 0u;
    for (uint32_t i = 1; i < Cpu().size(); ++i) {
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
      .end = static_cast<uint32_t>(Cpu().size()),
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

  // Ensure atlas capacity for current draw count (with minimal slack).
  const auto required_elements
    = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(Cpu().size()));
  if (const auto result = atlas_->EnsureCapacity(required_elements, 0.5f);
    !result) {
    LOG_F(ERROR, "Failed to ensure DrawMetadata atlas capacity: {}",
      result.error().message());
    return;
  }

  std::vector<UploadRequest> requests;
  requests.reserve(Cpu().size());
  const auto stride
    = static_cast<std::uint64_t>(sizeof(oxygen::engine::DrawMetadata));
  const auto count = static_cast<std::uint32_t>(Cpu().size());

  // Minimal emitter: create one UploadRequest per element, but submit the
  // entire batch once. UploadPlanner will sort/pack/optimize the requests
  // (no emitter-side coalescing required).
  for (std::uint32_t idx = 0; idx < count; ++idx) {
    if (auto desc = atlas_->MakeUploadDescForIndex(idx, stride)) {
      UploadRequest req;
      req.kind = UploadKind::kBuffer;
      req.debug_name = "DrawMetadata";
      req.desc = *desc;
      req.data = UploadDataView { std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(&Cpu()[idx]), stride) };
      requests.push_back(std::move(req));
    } else {
      LOG_F(ERROR, "Failed to make upload desc for DrawMetadata {}", idx);
    }
  }

  if (!requests.empty()) {
    const auto tickets = uploader_->SubmitMany(
      std::span { requests.data(), requests.size() }, *staging_provider_);
    upload_operations_count_ += requests.size();
    if (!tickets) {
      const std::error_code ec = tickets.error();
      LOG_F(ERROR, "DrawMetadata upload submission failed: [{}] {}",
        ec.category().name(), ec.message());
    }
  }
}

auto DrawMetadataEmitter::GetDrawMetadataSrvIndex() const -> ShaderVisibleIndex
{
  if (!atlas_) {
    return ShaderVisibleIndex {};
  }
  // Ensure SRV is created even if no draws were emitted yet this frame.
  if (atlas_->GetBinding().srv == kInvalidShaderVisibleIndex) {
    // Best effort: allocate minimal capacity to create SRV
    (void)atlas_->EnsureCapacity(1u, 0.5f);
  }
  return atlas_->GetBinding().srv;
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
