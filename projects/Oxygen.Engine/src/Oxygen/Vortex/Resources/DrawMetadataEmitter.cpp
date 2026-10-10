//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/vector_relational.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/Resources/MaterialBinder.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/Types/DrawCullRecord.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/Types/PassMask.h>
#include <Oxygen/Vortex/Types/ShadowCasterSource.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace {

constexpr std::uint8_t kOpaqueBucketOrder = 0U;
constexpr std::uint8_t kMaskedBucketOrder = 1U;
constexpr std::uint8_t kTransparentBucketOrder = 2U;

auto ResolveBucketOrder(const oxygen::vortex::PassMask flags) -> std::uint8_t
{
  if (flags.IsSet(oxygen::vortex::PassMaskBit::kOpaque)) {
    return kOpaqueBucketOrder;
  }
  if (flags.IsSet(oxygen::vortex::PassMaskBit::kMasked)) {
    return kMaskedBucketOrder;
  }
  return kTransparentBucketOrder;
}

auto ClassifyMaterialPassMask(const oxygen::data::MaterialAsset* mat)
  -> oxygen::vortex::PassMask
{
  if (mat == nullptr) {
    return oxygen::vortex::PassMask { oxygen::vortex::PassMaskBit::kOpaque };
  }
  const auto domain = mat->GetMaterialDomain();

  namespace d = oxygen::data;
  namespace v = oxygen::vortex;
  oxygen::vortex::PassMask mask {};
  switch (domain) {
  case d::MaterialDomain::kUnknown:
  case d::MaterialDomain::kOpaque:
    mask.Set(v::PassMaskBit::kOpaque);
    break;
  case d::MaterialDomain::kAlphaBlended:
    mask.Set(v::PassMaskBit::kTransparent);
    break;
  case d::MaterialDomain::kMasked:
    mask.Set(v::PassMaskBit::kMasked);
    break;
  case d::MaterialDomain::kDecal:
  case d::MaterialDomain::kUserInterface:
  case d::MaterialDomain::kPostProcess:
    // These material domains do not have dedicated rendering paths yet in the
    // example render-graph. Classify them as transparent to avoid writing depth
    // for alpha-blended style content and to keep them out of the opaque depth
    // pre-pass.
    mask.Set(v::PassMaskBit::kTransparent);
    break;
  default:
    LOG_F(WARNING,
      "Material '{}' has unsupported domain {} (flags=0x{:08X}); "
      "classifying as Opaque",
      mat->GetAssetName(), static_cast<int>(domain), mat->GetFlags());
    mask.Set(v::PassMaskBit::kOpaque);
    break;
  }

  const bool alpha_test_enabled
    = (mat->GetFlags() & oxygen::data::pak::render::kMaterialFlag_AlphaTest)
    != 0U;
  if (alpha_test_enabled && !mask.IsSet(v::PassMaskBit::kTransparent)) {
    mask.Unset(v::PassMaskBit::kOpaque);
    mask.Set(v::PassMaskBit::kMasked);
  }

  // Double-sided is explicit and data-driven via the PAK material flag.
  // Render passes use it to pick appropriate cull mode.
  if (mat->IsDoubleSided()) {
    mask.Set(v::PassMaskBit::kDoubleSided);
  }

  DLOG_F(2, "Material classify: name='{}' domain={} flags=0x{:08X} -> {}",
    mat->GetAssetName(), static_cast<int>(domain), mat->GetFlags(), mask);
  return mask;
}

auto ApplyShadowCasterPassRouting(oxygen::vortex::PassMask mask,
  const bool cast_shadows) -> oxygen::vortex::PassMask
{
  if (!cast_shadows) {
    return mask;
  }

  // TODO(post-v0.1, EV01-SHADOW-BLEND): Add blended shadow transmission
  // semantics; routing Blend through the opaque/masked depth path would give
  // wrong shadows. Scope:
  // design/vortex/milestones/ED-M08/deferred-capabilities.md#ev01-shadow-blend
  const bool supports_shadow_casting
    = mask.IsSet(oxygen::vortex::PassMaskBit::kOpaque)
    || mask.IsSet(oxygen::vortex::PassMaskBit::kMasked);
  if (supports_shadow_casting) {
    mask.Set(oxygen::vortex::PassMaskBit::kShadowCaster);
  }
  return mask;
}

//! The pass bits occlusion culling needs, as DrawCullFlagBits.
auto CullFlagsFromPassMask(const oxygen::vortex::PassMask mask) -> std::uint32_t
{
  using oxygen::vortex::DrawCullFlagBits;
  using oxygen::vortex::PassMaskBit;
  using oxygen::vortex::ToUnderlying;
  auto flags = 0U;
  const auto copy
    = [&](const PassMaskBit pass, const DrawCullFlagBits cull) -> void {
    if (mask.IsSet(pass)) {
      flags |= ToUnderlying(cull);
    }
  };
  copy(PassMaskBit::kOpaque, DrawCullFlagBits::kOpaque);
  copy(PassMaskBit::kMasked, DrawCullFlagBits::kMasked);
  copy(PassMaskBit::kTransparent, DrawCullFlagBits::kTransparent);
  copy(PassMaskBit::kShadowCaster, DrawCullFlagBits::kShadowCaster);
  copy(PassMaskBit::kMainViewVisible, DrawCullFlagBits::kMainViewVisible);
  return flags;
}

auto IsFinite(const glm::vec3& value) -> bool
{
  return std::isfinite(value.x) && std::isfinite(value.y)
    && std::isfinite(value.z);
}

//! Center and half extent of the box `[min, max]`.
auto MakeBoxRecord(const glm::vec3& min, const glm::vec3& max,
  const std::uint32_t flags) -> oxygen::vortex::DrawCullRecord
{
  return oxygen::vortex::DrawCullRecord {
    .box_center = 0.5F * (min + max),
    .box_extent = 0.5F * (max - min),
    .flags = flags,
  };
}

auto ApplyMainViewPassRouting(oxygen::vortex::PassMask mask,
  const bool main_view_visible) -> oxygen::vortex::PassMask
{
  if (main_view_visible) {
    mask.Set(oxygen::vortex::PassMaskBit::kMainViewVisible);
  }
  return mask;
}

} // namespace

namespace oxygen::vortex::resources {

DrawMetadataEmitter::DrawMetadataEmitter(observer_ptr<Graphics> gfx,
  observer_ptr<vortex::upload::StagingProvider> provider,
  observer_ptr<vortex::resources::GeometryUploader> geometry,
  observer_ptr<vortex::resources::MaterialBinder> materials,
  observer_ptr<vortex::upload::InlineTransfersCoordinator> inline_transfers)
  : gfx_(gfx)
  , geometry_uploader_(geometry)
  , material_binder_(materials)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
  , draw_metadata_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(oxygen::vortex::DrawMetadata)),
      inline_transfers_, "DrawMetadataEmitter.Draws",
      oxygen::bindless::generated::kMaterialsDomain)
  , draw_bounds_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(glm::vec4)), inline_transfers_,
      "DrawMetadataEmitter.DrawBounds",
      oxygen::bindless::generated::kMaterialsDomain)
  , draw_cull_records_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(DrawCullRecord)), inline_transfers_,
      "DrawMetadataEmitter.CullRecords",
      oxygen::bindless::generated::kMaterialsDomain)
  , instance_data_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(std::uint32_t)), inline_transfers_,
      "DrawMetadataEmitter.InstanceData",
      oxygen::bindless::generated::kGlobalSrvDomain)
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
  LOG_F(INFO, "sort calls        : {}", sort_calls_count_);
  LOG_F(INFO, "peak draws        : {}", peak_draws_);
  LOG_F(INFO, "peak partitions   : {}", peak_partitions_);
}

auto DrawMetadataEmitter::OnFrameStart(vortex::RendererTag /*tag*/,
  oxygen::frame::SequenceNumber sequence, oxygen::frame::Slot slot) -> void
{
  frame_write_count_ = 0U;

  // Reset per-frame CPU state; keep GPU resources
  Cpu().clear();
  keys_.clear();
  partitions_.clear();
  draw_bounding_spheres_.clear();
  draw_cull_records_.clear();
  draw_world_boxes_.clear();
  shadow_caster_sources_.clear();
  velocity_publication_sources_.clear();
  instance_transform_indices_.clear();
  draw_metadata_buffer_.OnFrameStart(sequence, slot);
  draw_bounds_buffer_.OnFrameStart(sequence, slot);
  draw_cull_records_buffer_.OnFrameStart(sequence, slot);
  instance_data_buffer_.OnFrameStart(sequence, slot);
  draw_metadata_srv_index_ = kInvalidShaderVisibleIndex;
  draw_bounds_srv_index_ = kInvalidShaderVisibleIndex;
  draw_cull_records_srv_index_ = kInvalidShaderVisibleIndex;
  instance_data_srv_index_ = kInvalidShaderVisibleIndex;
  ++frames_started_count_;
}

auto DrawMetadataEmitter::ResetViewData() -> void
{
  frame_write_count_ = 0U;
  Cpu().clear();
  keys_.clear();
  partitions_.clear();
  draw_bounding_spheres_.clear();
  draw_cull_records_.clear();
  draw_world_boxes_.clear();
  shadow_caster_sources_.clear();
  velocity_publication_sources_.clear();
  instance_transform_indices_.clear();
  draw_metadata_srv_index_ = kInvalidShaderVisibleIndex;
  draw_bounds_srv_index_ = kInvalidShaderVisibleIndex;
  draw_cull_records_srv_index_ = kInvalidShaderVisibleIndex;
  instance_data_srv_index_ = kInvalidShaderVisibleIndex;
}

auto DrawMetadataEmitter::EmitDrawMetadata(
  const oxygen::vortex::sceneprep::RenderItemData& item) -> void
{
  if (!item.geometry.IsValid()) {
    return;
  }
  const auto& lod = *item.geometry.mesh;
  const auto submeshes_span = lod.SubMeshes();
  if (item.submesh_index.get() >= submeshes_span.size()) {
    return;
  }
  const auto& submesh
    = *std::next(submeshes_span.begin(), item.submesh_index.get());
  const auto views_span = submesh.MeshViews();
  if (views_span.empty()) {
    return;
  }
  // Acquire geometry handle once per LOD mesh; GeometryUploader interns by
  // stable identity `(AssetKey, lod_index)`.
  auto geo_handle = geometry_uploader_
    ? geometry_uploader_->GetOrAllocate(item.geometry)
    : oxygen::vortex::sceneprep::kInvalidGeometryHandle;

  // Resolve SRV indices once per LOD mesh. If geometry is not resident (or
  // upload failed), indices remain invalid and the draw is skipped.
  const auto indices = geometry_uploader_
    ? geometry_uploader_->GetShaderVisibleIndices(geo_handle)
    : oxygen::vortex::resources::GeometryUploader::MeshShaderVisibleIndices {};

  for (const auto& [view_position, view] : std::views::enumerate(views_span)) {
    const auto view_index = static_cast<std::uint32_t>(view_position);
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

    oxygen::vortex::DrawMetadata dm {};
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
    dm.transform_generation = item.transform_handle.GenerationValue().get();
    dm.submesh_index = item.submesh_index.get();
    dm.primitive_flags = item.receive_shadows
      ? 0U
      : static_cast<uint32_t>(DrawPrimitiveFlagBits::kDisableShadowReception);
    if (item.static_shadow_caster) {
      dm.primitive_flags |= static_cast<uint32_t>(
        oxygen::vortex::DrawPrimitiveFlagBits::kStaticShadowCaster);
    }
    if (item.main_view_visible) {
      dm.primitive_flags |= static_cast<uint32_t>(
        oxygen::vortex::DrawPrimitiveFlagBits::kMainViewVisible);
    }
    dm.flags = ApplyMainViewPassRouting(
      ApplyShadowCasterPassRouting(
        ClassifyMaterialPassMask(item.material.resolved_asset.get()),
        item.cast_shadows),
      item.main_view_visible);
    if (item.reverse_winding) {
      // Pass flags participate in both instancing and partition keys. Opposite
      // handedness cannot share a draw with a fixed rasterizer front face.
      dm.flags.Set(PassMaskBit::kReverseWinding);
    }
    DCHECK_F(handle != oxygen::vortex::sceneprep::kInvalidTransformHandle,
      "Invalid transform handle while emitting");
    DCHECK_F(!dm.flags.IsEmpty(), "flags cannot be empty after assignment");

    const std::uint8_t bucket_order = ResolveBucketOrder(dm.flags);
    const auto index = frame_write_count_;
    if (index >= Cpu().size()) {
      Cpu().resize(static_cast<size_t>(index) + 1U);
      keys_.resize(static_cast<size_t>(index) + 1U);
      draw_bounding_spheres_.resize(static_cast<size_t>(index) + 1U);
      draw_cull_records_.resize(static_cast<size_t>(index) + 1U);
      draw_world_boxes_.resize(static_cast<size_t>(index) + 1U);
      velocity_publication_sources_.resize(static_cast<size_t>(index) + 1U);
    }
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    Cpu().at(index) = dm;
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    keys_.at(index) = SortingKey {
      .pass_mask = dm.flags,
      .bucket_order = bucket_order,
      .sort_distance2 = item.sort_distance2,
      .material_index = dm.material_handle,
      .vb_srv = dm.vertex_buffer_index,
      .ib_srv = dm.index_buffer_index,
      .node_handle = item.node_handle,
    };
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    draw_bounding_spheres_.at(index) = item.world_bounding_sphere;
    EmitCullRecord(index, view, dm.flags, item.world_transform);
    if (dm.flags.IsSet(PassMaskBit::kShadowCaster)) {
      shadow_caster_sources_.push_back(ShadowCasterSource {
        .draw = dm,
        .bounds = item.world_bounding_sphere,
        .node = item.node_handle,
        .geometry_asset_key = item.geometry.asset_key,
        .lod_index = item.geometry.lod_index,
        .geometry_generation = geo_handle.GenerationValue().get(),
        .geometry_content_revision = indices.content_revision,
        .material_generation = item.material_handle.GenerationValue().get(),
      });
    }
    // NOLINTNEXTLINE(*-pro-bounds-avoid-unchecked-container-access)
    velocity_publication_sources_.at(index)
      = DrawMetadataEmitter::VelocityPublicationSource {
          .node_handle = item.node_handle,
          .geometry_asset_key = item.geometry.asset_key,
          .lod_index = item.geometry.lod_index,
          .submesh_index = item.submesh_index,
          .mesh_view_index = data::MeshViewIndex { view_index },
        };
    ++frame_write_count_;
  }
}

auto DrawMetadataEmitter::EmitCullRecord(const std::uint32_t index,
  const data::MeshView& view, const PassMask pass_mask, const glm::mat4& world)
  -> void
{
  const auto flags = CullFlagsFromPassMask(pass_mask);
  const auto local_min = view.BoundingBoxMin();
  const auto local_max = view.BoundingBoxMax();
  auto record = MakeBoxRecord(local_min, local_max, flags);
  auto world_box = WorldBox {};
  if (IsFinite(local_min) && IsFinite(local_max)
    && glm::all(glm::lessThanEqual(local_min, local_max))) {
    // World AABB of the local box: the center moves with the transform, the
    // extent spans the absolute value of the linear part.
    const auto center = glm::vec3(world * glm::vec4(record.box_center, 1.0F));
    const auto linear = glm::mat3(world);
    const auto extent = (glm::abs(glm::column(linear, 0)) * record.box_extent.x)
      + (glm::abs(glm::column(linear, 1)) * record.box_extent.y)
      + (glm::abs(glm::column(linear, 2)) * record.box_extent.z);
    world_box = WorldBox {
      .min = center - extent,
      .max = center + extent,
      .finite = IsFinite(center) && IsFinite(extent),
    };
  }
  if (!world_box.finite) {
    record = MakeBoxRecord(glm::vec3(0.0F), glm::vec3(0.0F),
      flags | ToUnderlying(DrawCullFlagBits::kAlwaysVisible));
  }
  draw_cull_records_.at(index) = record;
  draw_world_boxes_.at(index) = world_box;
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
        .node_handle = {},
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
    perm.at(i) = static_cast<std::uint32_t>(i);
  }
  std::ranges::stable_sort(perm, [&](std::uint32_t a, std::uint32_t b) -> bool {
    const auto& ka = keys_.at(a);
    const auto& kb = keys_.at(b);
    if (ka.bucket_order != kb.bucket_order) {
      return ka.bucket_order < kb.bucket_order;
    }

    // Transparent: strict back-to-front ordering by distance first.
    if ((ka.bucket_order == 2) && (ka.sort_distance2 != kb.sort_distance2)) {
      return ka.sort_distance2 > kb.sort_distance2;
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

  std::vector<oxygen::vortex::DrawMetadata> reordered;
  reordered.reserve(n);
  std::vector<SortingKey> reordered_keys;
  reordered_keys.reserve(n);
  std::vector<glm::vec4> reordered_bounds;
  reordered_bounds.reserve(n);
  std::vector<DrawMetadataEmitter::VelocityPublicationSource> reordered_sources;
  reordered_sources.reserve(n);
  std::vector<DrawCullRecord> reordered_records;
  reordered_records.reserve(n);
  std::vector<WorldBox> reordered_world_boxes;
  reordered_world_boxes.reserve(n);
  for (auto idx : perm) {
    reordered.push_back(Cpu().at(idx));
    reordered_keys.push_back(keys_.at(idx));
    reordered_bounds.push_back(draw_bounding_spheres_.at(idx));
    reordered_sources.push_back(velocity_publication_sources_.at(idx));
    reordered_records.push_back(draw_cull_records_.at(idx));
    reordered_world_boxes.push_back(draw_world_boxes_.at(idx));
  }
  Cpu().swap(reordered);
  keys_.swap(reordered_keys);
  draw_bounding_spheres_.swap(reordered_bounds);
  velocity_publication_sources_.swap(reordered_sources);
  draw_cull_records_.swap(reordered_records);
  draw_world_boxes_.swap(reordered_world_boxes);

  last_order_hash_
    = oxygen::ComputeFNV1a64(keys_.data(), keys_.size() * sizeof(SortingKey));

  partitions_.clear();
  if (!Cpu().empty()) {
    auto current_mask = Cpu().front().flags;
    std::uint32_t range_begin = 0U;
    for (std::uint32_t i = 1; i < u_draw_count; ++i) {
      const auto mask = Cpu().at(i).flags;
      if (mask != current_mask) {
        partitions_.push_back(
          oxygen::vortex::PreparedSceneFrame::PartitionRange {
            .pass_mask = current_mask,
            .begin = range_begin,
            .end = i,
          });
        current_mask = mask;
        range_begin = i;
      }
    }
    partitions_.push_back(oxygen::vortex::PreparedSceneFrame::PartitionRange {
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

  if (!alloc.TryWriteRange(std::span { Cpu() })) {
    LOG_F(ERROR, "Failed to write draw metadata payload");
    return;
  }

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
      if (!bounds_alloc.TryWriteRange(std::span { draw_bounding_spheres_ })) {
        LOG_F(ERROR, "Failed to write draw bounds payload");
        return;
      }
      draw_bounds_srv_index_ = bounds_alloc.srv;
    }
  }

  if (!draw_cull_records_.empty()) {
    auto records_result = draw_cull_records_buffer_.Allocate(
      static_cast<std::uint32_t>(draw_cull_records_.size()));
    if (!records_result) {
      LOG_F(ERROR, "Draw cull record allocation failed: {}",
        records_result.error().message());
      return;
    }
    const auto records_alloc = *records_result;
    if (records_alloc.mapped_ptr != nullptr) {
      if (!records_alloc.TryWriteRange(std::span { draw_cull_records_ })) {
        LOG_F(ERROR, "Failed to write draw cull records payload");
        return;
      }
      draw_cull_records_srv_index_ = records_alloc.srv;
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
      if (!instance_alloc.TryWriteRange(
            std::span { instance_transform_indices_ })) {
        LOG_F(ERROR, "Failed to write instance metadata payload");
        return;
      }
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
  -> std::span<const oxygen::vortex::PreparedSceneFrame::PartitionRange>
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

auto DrawMetadataEmitter::GetDrawCullRecords() const noexcept
  -> std::span<const DrawCullRecord>
{
  return { draw_cull_records_.data(), draw_cull_records_.size() };
}

auto DrawMetadataEmitter::GetDrawCullRecordsSrvIndex() -> ShaderVisibleIndex
{
  if (draw_cull_records_srv_index_ == kInvalidShaderVisibleIndex) {
    EnsureFrameResources();
  }
  return draw_cull_records_srv_index_;
}

auto DrawMetadataEmitter::GetInstanceDataSrvIndex() const noexcept
  -> ShaderVisibleIndex
{
  return instance_data_srv_index_;
}

auto DrawMetadataEmitter::GetVelocityPublicationSources() const noexcept
  -> std::span<const DrawMetadataEmitter::VelocityPublicationSource>
{
  return { velocity_publication_sources_.data(),
    velocity_publication_sources_.size() };
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
  oxygen::HashCombine(hash, key.primitive_flags);
  oxygen::HashCombine(hash, key.node_handle);
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
    const auto& dm = Cpu().at(i);
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
      .primitive_flags = dm.primitive_flags,
      .node_handle = keys_.at(i).node_handle,
    };
    if (const auto it = key_to_group_index.find(key);
      it != key_to_group_index.end()) {
      groups.at(it->second).push_back(i);
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
  std::vector<oxygen::vortex::DrawMetadata> batched_cpu;
  std::vector<SortingKey> batched_keys;
  std::vector<glm::vec4> batched_bounds;
  std::vector<DrawMetadataEmitter::VelocityPublicationSource> batched_sources;
  std::vector<DrawCullRecord> batched_records;
  std::vector<WorldBox> batched_world_boxes;
  batched_records.reserve(initial_draw_count);
  batched_world_boxes.reserve(initial_draw_count);
  batched_cpu.reserve(initial_draw_count);
  batched_keys.reserve(initial_draw_count);
  batched_bounds.reserve(initial_draw_count);
  batched_sources.reserve(initial_draw_count);
  instance_transform_indices_.clear();
  instance_transform_indices_.reserve(Cpu().size());

  for (const auto& indices : groups) {
    const auto instance_count = static_cast<std::uint32_t>(indices.size());

    // Use first draw as representative
    auto dm = Cpu().at(indices.at(0));
    dm.instance_count = instance_count;

    if (instance_count > 1) {
      // Multi-instance: record offset into instance data buffer
      dm.instance_metadata_offset
        = static_cast<std::uint32_t>(instance_transform_indices_.size());
      dm.instance_metadata_buffer_index = 1; // Signal that instance data is
                                             // used

      // Append all transform indices for this batch
      for (const auto draw_idx : indices) {
        instance_transform_indices_.push_back(
          Cpu().at(draw_idx).transform_index);
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
    if (!indices.empty() && indices.at(0) < keys_.size()) {
      batch_sort_distance2 = keys_.at(indices.at(0)).sort_distance2;
    }

    batched_keys.push_back(SortingKey {
      .pass_mask = dm.flags,
      .bucket_order = bucket_order,
      .sort_distance2 = batch_sort_distance2,
      .material_index = dm.material_handle,
      .vb_srv = dm.vertex_buffer_index,
      .ib_srv = dm.index_buffer_index,
      .node_handle = keys_.at(indices.at(0)).node_handle,
    });

    glm::vec4 merged_bound { 0.0F, 0.0F, 0.0F, 0.0F };
    if (!indices.empty() && indices.at(0) < draw_bounding_spheres_.size()) {
      glm::vec3 bounds_min { std::numeric_limits<float>::max() };
      glm::vec3 bounds_max { std::numeric_limits<float>::lowest() };
      bool have_valid_bound = false;
      bool all_bounds_valid = true;
      for (const auto draw_idx : indices) {
        if (draw_idx >= draw_bounding_spheres_.size()) {
          all_bounds_valid = false;
          break;
        }
        const auto& sphere = draw_bounding_spheres_.at(draw_idx);
        if (sphere.w <= 0.0F || !std::isfinite(sphere.x)
          || !std::isfinite(sphere.y) || !std::isfinite(sphere.z)
          || !std::isfinite(sphere.w)) {
          all_bounds_valid = false;
          break;
        }
        const glm::vec3 center { sphere.x, sphere.y, sphere.z };
        const glm::vec3 radius_vec { sphere.w, sphere.w, sphere.w };
        bounds_min = glm::min(bounds_min, center - radius_vec);
        bounds_max = glm::max(bounds_max, center + radius_vec);
        have_valid_bound = true;
      }
      if (have_valid_bound && all_bounds_valid) {
        const glm::vec3 merged_center = 0.5F * (bounds_min + bounds_max);
        float merged_radius = 0.0F;
        for (const auto draw_idx : indices) {
          if (draw_idx >= draw_bounding_spheres_.size()) {
            continue;
          }
          const auto& sphere = draw_bounding_spheres_.at(draw_idx);
          if (sphere.w <= 0.0F) {
            continue;
          }
          merged_radius = (std::max)(merged_radius,
            glm::distance(merged_center, glm::vec3(sphere)) + sphere.w);
        }
        merged_bound = glm::vec4(
          merged_center.x, merged_center.y, merged_center.z, merged_radius);
      }
    }
    batched_bounds.push_back(merged_bound);
    if (instance_count > 1) {
      // A batch is culled as a whole, by the world AABB of its instances.
      auto world_box = WorldBox {
        .min = glm::vec3(std::numeric_limits<float>::max()),
        .max = glm::vec3(std::numeric_limits<float>::lowest()),
        .finite = true,
      };
      for (const auto draw_idx : indices) {
        const auto& instance_box = draw_world_boxes_.at(draw_idx);
        world_box.finite = world_box.finite && instance_box.finite;
        world_box.min = glm::min(world_box.min, instance_box.min);
        world_box.max = glm::max(world_box.max, instance_box.max);
      }
      auto flags = draw_cull_records_.at(indices.at(0)).flags
        & ~ToUnderlying(DrawCullFlagBits::kAlwaysVisible);
      flags |= ToUnderlying(DrawCullFlagBits::kWorldSpaceBox);
      if (!world_box.finite) {
        flags |= ToUnderlying(DrawCullFlagBits::kAlwaysVisible);
        world_box.min = world_box.max = glm::vec3(0.0F);
      }
      batched_records.push_back(
        MakeBoxRecord(world_box.min, world_box.max, flags));
      batched_world_boxes.push_back(world_box);
    } else {
      batched_records.push_back(draw_cull_records_.at(indices.at(0)));
      batched_world_boxes.push_back(draw_world_boxes_.at(indices.at(0)));
    }
    if (!indices.empty()
      && indices.at(0) < velocity_publication_sources_.size()) {
      batched_sources.push_back(
        velocity_publication_sources_.at(indices.at(0)));
    } else {
      batched_sources.push_back(
        DrawMetadataEmitter::VelocityPublicationSource {});
    }
  }

  Cpu().swap(batched_cpu);
  keys_.swap(batched_keys);
  draw_bounding_spheres_.swap(batched_bounds);
  velocity_publication_sources_.swap(batched_sources);
  draw_cull_records_.swap(batched_records);
  draw_world_boxes_.swap(batched_world_boxes);

  DLOG_F(1, "Batched {} draws into {} batches, {} instance indices",
    initial_draw_count, Cpu().size(), instance_transform_indices_.size());
}

} // namespace oxygen::vortex::resources
