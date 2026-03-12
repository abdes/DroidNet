//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Internal/VirtualShadowMapBackend.h>

namespace {

constexpr float kMinClipSpan = 1.0F;
constexpr float kLightPullbackPadding = 64.0F;
constexpr float kMinShadowDepthPadding = 16.0F;
constexpr std::uint32_t kPageTableValidBit = (1U << 28U);
constexpr std::uint32_t kPageTableRequestedThisFrameBit = (1U << 29U);
constexpr std::uint32_t kMaxPersistentPageTableEntries
  = 64U * 64U * oxygen::engine::kMaxVirtualDirectionalClipLevels;
constexpr std::uint32_t kVirtualShadowMaxFilterGuardTexels = 2U;
constexpr std::int32_t kFeedbackRequestGuardRadius = 1;
constexpr std::uint64_t kMaxRequestFeedbackAgeFrames
  = oxygen::frame::kFramesInFlight.get() + 1U;
constexpr std::uint64_t kMaxResolvedRasterScheduleAgeFrames
  = oxygen::frame::kFramesInFlight.get() + 1U;
constexpr std::uint32_t kDirectionalCoarseBackboneClipCount = 3U;
constexpr std::uint32_t kCoarseBackboneGuardPages = 1U;
constexpr std::uint32_t kAcceptedFeedbackCurrentFrameReinforcementClipCount
  = 3U;
constexpr std::uint32_t kAcceptedFeedbackCurrentFrameGuardPages = 1U;
constexpr std::uint32_t kAcceptedFeedbackCurrentFrameMaxDeltaPagesPerClip
  = 128U;

[[nodiscard]] auto MapDistributionExponentToPracticalLambda(
  const float distribution_exponent) -> float
{
  if (distribution_exponent <= 1.0F) {
    return 0.0F;
  }

  return std::clamp(
    1.0F - (1.0F / std::max(distribution_exponent, 1.0F)), 0.0F, 0.95F);
}

[[nodiscard]] auto ResolveClipEndDepth(
  const oxygen::engine::DirectionalShadowCandidate& candidate,
  const std::uint32_t clip_index, const float prev_depth,
  const float near_depth, const float far_depth) -> float
{
  const auto clip_count = std::max(1U,
    std::min(candidate.cascade_count,
      oxygen::engine::kMaxVirtualDirectionalClipLevels));
  const float authored_end = clip_index < candidate.cascade_distances.size()
    ? candidate.cascade_distances[clip_index]
    : 0.0F;
  if (clip_index < candidate.cascade_count
    && authored_end > prev_depth + kMinClipSpan) {
    return std::min(authored_end, far_depth);
  }

  const float normalized_split
    = static_cast<float>(clip_index + 1U) / static_cast<float>(clip_count);
  const float stabilized_near_depth = std::max(near_depth, 0.001F);
  const float stabilized_far_depth
    = std::max(far_depth, stabilized_near_depth + kMinClipSpan);
  const float linear_split
    = glm::mix(stabilized_near_depth, stabilized_far_depth, normalized_split);
  const float logarithmic_split = stabilized_near_depth
    * std::pow(stabilized_far_depth / stabilized_near_depth, normalized_split);
  const float practical_lambda
    = MapDistributionExponentToPracticalLambda(candidate.distribution_exponent);
  const float generated_end
    = glm::mix(linear_split, logarithmic_split, practical_lambda);
  return std::max(
    prev_depth + kMinClipSpan, std::min(generated_end, far_depth));
}

[[nodiscard]] auto ResolvePagesPerAxis(
  const oxygen::ShadowQualityTier quality_tier) -> std::uint32_t
{
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return 24U;
  case oxygen::ShadowQualityTier::kMedium:
    return 32U;
  case oxygen::ShadowQualityTier::kHigh:
    return 48U;
  case oxygen::ShadowQualityTier::kUltra:
    return 64U;
  default:
    return 32U;
  }
}

[[nodiscard]] auto ResolveVirtualClipLevelCount(
  const oxygen::ShadowQualityTier quality_tier,
  const std::uint32_t authored_cascade_count) -> std::uint32_t
{
  const auto authored = std::max(1U, authored_cascade_count);
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return std::max(authored, 6U);
  case oxygen::ShadowQualityTier::kMedium:
    return std::max(authored, 8U);
  case oxygen::ShadowQualityTier::kHigh:
    return std::max(authored, 10U);
  case oxygen::ShadowQualityTier::kUltra:
    return std::max(authored, 12U);
  default:
    return std::max(authored, 8U);
  }
}

[[nodiscard]] auto QuantizeUpToPowerOfTwo(const float value) -> float
{
  const float stabilized = std::max(value, 1.0e-4F);
  return std::exp2(std::ceil(std::log2(stabilized)));
}

[[nodiscard]] auto ResolveMaxVirtualAtlasResolution(
  const oxygen::ShadowQualityTier quality_tier) -> std::uint32_t
{
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return 4096U;
  case oxygen::ShadowQualityTier::kMedium:
    return 6144U;
  case oxygen::ShadowQualityTier::kHigh:
    return 8192U;
  case oxygen::ShadowQualityTier::kUltra:
    return 12288U;
  default:
    return 6144U;
  }
}

[[nodiscard]] auto ResolveMaxVirtualPageSizeTexels(
  const oxygen::ShadowQualityTier quality_tier) -> std::uint32_t
{
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return 128U;
  case oxygen::ShadowQualityTier::kMedium:
    return 256U;
  case oxygen::ShadowQualityTier::kHigh:
    return 384U;
  case oxygen::ShadowQualityTier::kUltra:
    return 512U;
  default:
    return 256U;
  }
}

[[nodiscard]] auto ResolvePhysicalTileCapacity(
  const oxygen::ShadowQualityTier quality_tier) -> std::uint32_t
{
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return 256U;
  case oxygen::ShadowQualityTier::kMedium:
    return 512U;
  case oxygen::ShadowQualityTier::kHigh:
    return 1024U;
  case oxygen::ShadowQualityTier::kUltra:
    return 1536U;
  default:
    return 512U;
  }
}

[[nodiscard]] auto SelectDirectionalVirtualFilterRadiusTexels(
  const float base_page_world, const float clip_page_world) -> std::uint32_t
{
  const float stabilized_base_page_world = std::max(base_page_world, 1.0e-4F);
  const float stabilized_clip_page_world
    = std::max(clip_page_world, stabilized_base_page_world);
  const float texel_ratio
    = stabilized_clip_page_world / stabilized_base_page_world;
  return texel_ratio > 2.5F ? 2U : 1U;
}

[[nodiscard]] auto PackPageTableEntry(
  const std::uint32_t tile_x, const std::uint32_t tile_y) -> std::uint32_t
{
  return (tile_x & 0x0FFFU) | ((tile_y & 0x0FFFU) << 12U) | kPageTableValidBit
    | kPageTableRequestedThisFrameBit;
}

[[nodiscard]] auto BuildAddressSpaceComparableLightView(glm::mat4 light_view)
  -> glm::mat4
{
  return oxygen::renderer::internal::shadow_detail::
    BuildDirectionalAddressSpaceComparableLightView(light_view);
}

[[nodiscard]] auto BuildContentComparableLightView(glm::mat4 light_view)
  -> glm::mat4
{
  // Directional page contents remain reusable across page-aligned XY lattice
  // motion, but a change in the light-space Z basis changes the normalized
  // depth values stored in the page and must force rerasterization.
  light_view[3][0] = 0.0F;
  light_view[3][1] = 0.0F;
  return light_view;
}

[[nodiscard]] auto BuildContentComparableClipMetadata(
  oxygen::engine::DirectionalVirtualClipMetadata clip)
  -> oxygen::engine::DirectionalVirtualClipMetadata
{
  // Clip origins are allowed to move in page-sized increments without forcing
  // a reraster of pages that still map to the same absolute lattice key.
  // Keep the depth mapping terms intact: they define the normalized depth
  // values stored in the page and must participate in content reuse.
  clip.origin_page_scale.x = 0.0F;
  clip.origin_page_scale.y = 0.0F;
  clip.bias_reserved.y = 0.0F;
  clip.bias_reserved.z = 0.0F;
  clip.bias_reserved.w = 0.0F;
  return clip;
}

[[nodiscard]] auto IsDirectionalVirtualAddressSpaceCompatible(
  const oxygen::engine::DirectionalVirtualShadowMetadata& previous,
  const oxygen::engine::DirectionalVirtualShadowMetadata& current) -> bool
{
  if (previous.clip_level_count != current.clip_level_count
    || previous.pages_per_axis != current.pages_per_axis
    || previous.page_size_texels != current.page_size_texels) {
    return false;
  }

  const auto previous_xy_view
    = BuildAddressSpaceComparableLightView(previous.light_view);
  const auto current_xy_view
    = BuildAddressSpaceComparableLightView(current.light_view);
  if (!oxygen::renderer::internal::shadow_detail::DirectionalCacheMat4Equal(
        previous_xy_view, current_xy_view)) {
    return false;
  }

  for (std::uint32_t clip_index = 0U; clip_index < current.clip_level_count;
    ++clip_index) {
    if (!oxygen::renderer::internal::shadow_detail::DirectionalCacheFloatEqual(
          previous.clip_metadata[clip_index].origin_page_scale.z,
          current.clip_metadata[clip_index].origin_page_scale.z)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] auto IsDirectionalVirtualClipContentReusable(
  const oxygen::engine::DirectionalVirtualShadowMetadata& previous,
  const oxygen::engine::DirectionalVirtualShadowMetadata& current,
  const std::uint32_t clip_index) -> bool
{
  if (clip_index >= previous.clip_level_count
    || clip_index >= current.clip_level_count) {
    return false;
  }

  const auto previous_content_view
    = BuildContentComparableLightView(previous.light_view);
  const auto current_content_view
    = BuildContentComparableLightView(current.light_view);
  if (!oxygen::renderer::internal::shadow_detail::DirectionalCacheMat4Equal(
        previous_content_view, current_content_view)) {
    return false;
  }

  const auto previous_clip
    = BuildContentComparableClipMetadata(previous.clip_metadata[clip_index]);
  const auto current_clip
    = BuildContentComparableClipMetadata(current.clip_metadata[clip_index]);
  return oxygen::renderer::internal::shadow_detail::DirectionalCacheFloatEqual(
           previous_clip.origin_page_scale.z, current_clip.origin_page_scale.z)
    && oxygen::renderer::internal::shadow_detail::DirectionalCacheFloatEqual(
      previous_clip.origin_page_scale.w, current_clip.origin_page_scale.w)
    && oxygen::renderer::internal::shadow_detail::DirectionalCacheFloatEqual(
      previous_clip.bias_reserved.x, current_clip.bias_reserved.x);
}

auto AppendDirtyResidentKeysForBound(const glm::vec4& bound,
  const glm::mat4& light_view,
  const std::array<float, oxygen::engine::kMaxVirtualDirectionalClipLevels>&
    clip_page_world,
  const std::uint32_t clip_level_count,
  std::unordered_set<std::uint64_t>& dirty_pages) -> void
{
  const float radius = std::max(0.0F, bound.w);
  if (radius <= 0.0F) {
    return;
  }

  const glm::vec3 center_ls
    = glm::vec3(light_view * glm::vec4(glm::vec3(bound), 1.0F));
  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    const float page_world_size
      = std::max(clip_page_world[clip_index], 1.0e-4F);
    const auto min_grid_x = static_cast<std::int32_t>(
      std::floor((center_ls.x - radius) / page_world_size));
    const auto max_grid_x = static_cast<std::int32_t>(
      std::ceil((center_ls.x + radius) / page_world_size) - 1.0F);
    const auto min_grid_y = static_cast<std::int32_t>(
      std::floor((center_ls.y - radius) / page_world_size));
    const auto max_grid_y = static_cast<std::int32_t>(
      std::ceil((center_ls.y + radius) / page_world_size) - 1.0F);

    for (std::int32_t grid_y = min_grid_y; grid_y <= max_grid_y; ++grid_y) {
      for (std::int32_t grid_x = min_grid_x; grid_x <= max_grid_x; ++grid_x) {
        dirty_pages.insert(
          oxygen::renderer::internal::shadow_detail::PackVirtualResidentPageKey(
            clip_index, grid_x, grid_y));
      }
    }
  }
}

[[nodiscard]] auto CanonicalizeShadowCasterBounds(
  const std::span<const glm::vec4> bounds) -> std::vector<glm::vec4>
{
  std::vector<glm::vec4> canonical_bounds(bounds.begin(), bounds.end());
  std::ranges::stable_sort(
    canonical_bounds, [](const glm::vec4& lhs, const glm::vec4& rhs) {
      const auto lhs_x = oxygen::renderer::internal::shadow_detail::
        QuantizeDirectionalCacheFloat(lhs.x);
      const auto rhs_x = oxygen::renderer::internal::shadow_detail::
        QuantizeDirectionalCacheFloat(rhs.x);
      if (lhs_x != rhs_x) {
        return lhs_x < rhs_x;
      }

      const auto lhs_y = oxygen::renderer::internal::shadow_detail::
        QuantizeDirectionalCacheFloat(lhs.y);
      const auto rhs_y = oxygen::renderer::internal::shadow_detail::
        QuantizeDirectionalCacheFloat(rhs.y);
      if (lhs_y != rhs_y) {
        return lhs_y < rhs_y;
      }

      const auto lhs_z = oxygen::renderer::internal::shadow_detail::
        QuantizeDirectionalCacheFloat(lhs.z);
      const auto rhs_z = oxygen::renderer::internal::shadow_detail::
        QuantizeDirectionalCacheFloat(rhs.z);
      if (lhs_z != rhs_z) {
        return lhs_z < rhs_z;
      }

      const auto lhs_w = oxygen::renderer::internal::shadow_detail::
        QuantizeDirectionalCacheFloat(lhs.w);
      const auto rhs_w = oxygen::renderer::internal::shadow_detail::
        QuantizeDirectionalCacheFloat(rhs.w);
      return lhs_w < rhs_w;
    });
  return canonical_bounds;
}

} // namespace

namespace oxygen::renderer::internal {

VirtualShadowMapBackend::VirtualShadowMapBackend(::oxygen::Graphics* gfx,
  ::oxygen::engine::upload::StagingProvider* provider,
  ::oxygen::engine::upload::InlineTransfersCoordinator* inline_transfers,
  const ::oxygen::ShadowQualityTier quality_tier)
  : gfx_(gfx)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
  , shadow_quality_tier_(quality_tier)
  , shadow_instance_buffer_(oxygen::observer_ptr<Graphics>(gfx_),
      *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::ShadowInstanceMetadata)),
      oxygen::observer_ptr<engine::upload::InlineTransfersCoordinator>(
        inline_transfers_),
      "VirtualShadowMapBackend.ShadowInstanceMetadata")
  , directional_virtual_metadata_buffer_(oxygen::observer_ptr<Graphics>(gfx_),
      *staging_provider_,
      static_cast<std::uint32_t>(
        sizeof(engine::DirectionalVirtualShadowMetadata)),
      oxygen::observer_ptr<engine::upload::InlineTransfersCoordinator>(
        inline_transfers_),
      "VirtualShadowMapBackend.DirectionalVirtualMetadata")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

VirtualShadowMapBackend::~VirtualShadowMapBackend()
{
  for (auto& [_, resources] : view_page_table_resources_) {
    if (resources.upload_buffer && resources.mapped_upload != nullptr) {
      resources.upload_buffer->UnMap();
      resources.mapped_upload = nullptr;
    }
  }
  for (auto& [_, resources] : view_resolve_resources_) {
    if (resources.resident_pages_upload_buffer
      && resources.mapped_resident_pages_upload != nullptr) {
      resources.resident_pages_upload_buffer->UnMap();
      resources.mapped_resident_pages_upload = nullptr;
    }
    if (resources.stats_upload_buffer
      && resources.mapped_stats_upload != nullptr) {
      resources.stats_upload_buffer->UnMap();
      resources.mapped_stats_upload = nullptr;
    }
  }
  ReleasePhysicalPool();
}

auto VirtualShadowMapBackend::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  frame_sequence_ = sequence;
  shadow_instance_buffer_.OnFrameStart(sequence, slot);
  directional_virtual_metadata_buffer_.OnFrameStart(sequence, slot);
}

auto VirtualShadowMapBackend::PublishView(const ViewId view_id,
  const engine::ViewConstants& view_constants,
  const std::span<const engine::DirectionalShadowCandidate>
    directional_candidates,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::chrono::milliseconds gpu_budget, const bool allow_budget_fallback,
  const std::uint64_t shadow_caster_content_hash) -> ShadowFramePublication
{
  if (directional_candidates.size() != 1U) {
    if (!directional_candidates.empty()) {
      LOG_F(INFO,
        "VirtualShadowMapBackend: view {} kept on conventional directional "
        "shadowing because the first validation slice requires exactly one "
        "directional candidate (got {})",
        view_id.get(), directional_candidates.size());
    }
    return {};
  }

  (void)gpu_budget;
  (void)allow_budget_fallback;

  const auto canonical_shadow_caster_bounds
    = CanonicalizeShadowCasterBounds(shadow_caster_bounds);
  const auto canonical_shadow_caster_bounds_span
    = std::span<const glm::vec4>(canonical_shadow_caster_bounds.data(),
      canonical_shadow_caster_bounds.size());

  const auto key = BuildPublicationKey(view_constants, directional_candidates,
    canonical_shadow_caster_bounds_span, shadow_caster_content_hash);

  ViewCacheEntry state {};
  state.key = key;

  const auto pool_config
    = BuildPhysicalPoolConfig(directional_candidates.front(), gpu_budget,
      canonical_shadow_caster_bounds_span.size());
  EnsurePhysicalPool(pool_config);
  const auto previous_it = view_cache_.find(view_id);
  BuildDirectionalVirtualViewState(view_id, view_constants,
    directional_candidates.front(), canonical_shadow_caster_bounds_span,
    visible_receiver_bounds,
    previous_it != view_cache_.end() ? &previous_it->second : nullptr, state);
  state.resolved_raster_pages.clear();
  state.publish_diagnostics.resident_reuse_gate_open = false;

  const auto resolved_schedule_it = resolved_raster_schedules_.find(view_id);
  if (resolved_schedule_it != resolved_raster_schedules_.end()
    && !state.directional_virtual_metadata.empty()) {
    const auto& schedule = resolved_schedule_it->second;
    const auto& metadata = state.directional_virtual_metadata.front();
    const auto schedule_age_frames
      = frame_sequence_ > schedule.source_frame_sequence
      ? (frame_sequence_ - schedule.source_frame_sequence).get()
      : 0U;
    state.publish_diagnostics.resolved_schedule_pages
      = static_cast<std::uint32_t>(schedule.requested_resident_keys.size());
    state.publish_diagnostics.resolved_schedule_age_frames
      = schedule_age_frames;

    const bool schedule_compatible
      = schedule.pages_per_axis == metadata.pages_per_axis
      && schedule.clip_level_count == metadata.clip_level_count
      && schedule.directional_address_space_hash
        == shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(metadata)
      && frame_sequence_ > schedule.source_frame_sequence
      && schedule_age_frames <= kMaxResolvedRasterScheduleAgeFrames;

    if (schedule_compatible) {
      // The current resolve bridge only compacts requested pages that already
      // map to valid current-frame page-table entries. Until resolve becomes
      // the sole author of current-frame allocation and raster scheduling, that
      // payload is telemetry only and must not suppress CPU-authored fine jobs.
      state.publish_diagnostics.used_resolved_raster_schedule = false;
      state.publish_diagnostics.resolved_schedule_pruned_jobs = 0U;
    }
  }

  state.frame_publication.shadow_instance_metadata_srv
    = PublishShadowInstances(state.shadow_instances);
  state.frame_publication.virtual_directional_shadow_metadata_srv
    = PublishDirectionalVirtualMetadata(state.directional_virtual_metadata);
  state.frame_publication.virtual_shadow_page_table_srv
    = EnsurePageTablePublication(
      view_id, static_cast<std::uint32_t>(state.page_table_entries.size()));
  state.page_table_upload_entry_count
    = static_cast<std::uint32_t>(state.page_table_entries.size());
  state.page_table_upload_pending = false;
  state.frame_publication.virtual_shadow_physical_pool_srv = physical_pool_srv_;
  if (!state.shadow_instances.empty()) {
    const auto flags = static_cast<engine::ShadowProductFlags>(
      state.shadow_instances.front().flags);
    if ((flags & engine::ShadowProductFlags::kSunLight)
      != engine::ShadowProductFlags::kNone) {
      state.frame_publication.sun_shadow_index = 0U;
    }
  }

  auto [it, inserted] = view_cache_.insert_or_assign(view_id, std::move(state));
  DCHECK_F(inserted || it != view_cache_.end(),
    "VirtualShadowMapBackend failed to publish view state");
  RebuildResolveStateSnapshot(it->second);
  RefreshViewExports(view_id, it->second);
  return it->second.frame_publication;
}

auto VirtualShadowMapBackend::MarkRendered(const ViewId view_id) -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    return;
  }

  // Current-frame allocation / eviction / page-table mutation now belongs to
  // the explicit resolve stage. MarkRendered only transitions already
  // resolved-and-rasterized pending pages to clean residency.
  const auto pages_per_level = physical_pool_config_.virtual_pages_per_clip_axis
    * physical_pool_config_.virtual_pages_per_clip_axis;
  (void)pages_per_level;
  for (const auto& page : it->second.resolved_raster_pages) {
    if (const auto resident_it
      = it->second.resident_pages.find(page.resident_key);
      resident_it != it->second.resident_pages.end()) {
      resident_it->second.state
        = renderer::VirtualPageResidencyState::kResidentClean;
      resident_it->second.last_touched_frame = frame_sequence_;
    }
  }
  it->second.resolved_raster_pages.clear();
  RebuildResolveStateSnapshot(it->second);
  StageResolveStateUpload(view_id, it->second);
  RefreshViewExports(view_id, it->second);
}

auto VirtualShadowMapBackend::ResolveCurrentFrame(const ViewId view_id) -> void
{
  ResolvePendingPageResidency(view_id);
}

auto VirtualShadowMapBackend::ResolvePendingPageResidency(const ViewId view_id)
  -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    return;
  }

  auto& state = it->second;
  auto& pending = state.pending_residency_resolve;
  if (!pending.valid || !pending.dirty) {
    return;
  }

  state.resolved_raster_pages.clear();
  std::fill(
    state.page_table_entries.begin(), state.page_table_entries.end(), 0U);

  std::uint32_t reused_requested_pages = 0U;
  std::uint32_t allocated_pages = 0U;
  std::uint32_t evicted_pages = 0U;
  std::uint32_t allocation_failures = 0U;
  std::uint32_t rerasterized_pages = 0U;
  std::uint32_t released_resident_pages = 0U;
  std::uint32_t marked_dirty_pages = 0U;

  state.resident_pages.clear();
  if (!pending.previous_resident_pages.empty()) {
    if (!pending.address_space_compatible) {
      for (const auto& [_, resident_page] : pending.previous_resident_pages) {
        ReleasePhysicalTile(resident_page.tile);
        ++released_resident_pages;
      }
    } else {
      for (const auto& [resident_key, resident_page] :
        pending.previous_resident_pages) {
        const auto clip_index
          = shadow_detail::VirtualResidentPageKeyClipLevel(resident_key);
        if (clip_index >= pending.clip_level_count) {
          ReleasePhysicalTile(resident_page.tile);
          ++released_resident_pages;
          continue;
        }

        auto carried_page = resident_page;
        if ((pending.global_dirty_resident_contents
              || !pending.reusable_clip_contents[clip_index]
              || pending.dirty_resident_pages.contains(resident_key))
          && carried_page.state
            == renderer::VirtualPageResidencyState::kResidentClean) {
          carried_page.state
            = renderer::VirtualPageResidencyState::kResidentDirty;
          ++marked_dirty_pages;
        }
        state.resident_pages.insert_or_assign(resident_key, carried_page);
      }
    }
  }

  const auto process_clip_range_desc = [&](const std::uint32_t begin_clip,
                                         const std::uint32_t end_clip) {
    for (std::uint32_t clip_index = end_clip; clip_index-- > begin_clip;) {
      const float page_world_size = pending.clip_page_world[clip_index];
      const float origin_x = pending.clip_origin_x[clip_index];
      const float origin_y = pending.clip_origin_y[clip_index];
      const std::uint32_t filter_guard_texels
        = std::min(kVirtualShadowMaxFilterGuardTexels,
          SelectDirectionalVirtualFilterRadiusTexels(
            pending.clip_page_world[0], pending.clip_page_world[clip_index]));
      const float interior_texels = std::max(1.0F,
        static_cast<float>(physical_pool_config_.page_size_texels)
          - 2.0F * static_cast<float>(filter_guard_texels));
      const float page_guard_world = static_cast<float>(filter_guard_texels)
        * (page_world_size / interior_texels);
      const auto grid_offset_x = pending.clip_grid_origin_x[clip_index];
      const auto grid_offset_y = pending.clip_grid_origin_y[clip_index];

      for (std::uint32_t page_y = 0U; page_y < pending.pages_per_axis;
        ++page_y) {
        for (std::uint32_t page_x = 0U; page_x < pending.pages_per_axis;
          ++page_x) {
          const std::uint32_t local_page_index
            = page_y * pending.pages_per_axis + page_x;
          const std::uint32_t global_page_index
            = clip_index * pending.pages_per_level + local_page_index;
          if (pending.selected_pages[global_page_index] == 0U) {
            continue;
          }

          const auto resident_key = shadow_detail::PackVirtualResidentPageKey(
            clip_index, grid_offset_x + static_cast<std::int32_t>(page_x),
            grid_offset_y + static_cast<std::int32_t>(page_y));

          bool needs_raster = true;
          ResidentVirtualPage resident_page {};
          if (const auto resident_it = state.resident_pages.find(resident_key);
            resident_it != state.resident_pages.end()) {
            resident_page = resident_it->second;
            needs_raster = !resident_page.ContentsValid()
              || !pending.reusable_clip_contents[clip_index];
            ++reused_requested_pages;
          } else {
            const auto allocated_tile = AcquirePhysicalTile(
              state, pending.pages_per_level, evicted_pages);
            if (!allocated_tile.has_value()) {
              ++allocation_failures;
              continue;
            }
            resident_page.tile = *allocated_tile;
            resident_page.state
              = renderer::VirtualPageResidencyState::kPendingRender;
            resident_page.last_touched_frame = frame_sequence_;
            resident_page.last_requested_frame = frame_sequence_;
            ++allocated_pages;
          }
          resident_page.state = needs_raster
            ? renderer::VirtualPageResidencyState::kPendingRender
            : renderer::VirtualPageResidencyState::kResidentClean;
          resident_page.last_touched_frame = frame_sequence_;
          resident_page.last_requested_frame = frame_sequence_;
          state.resident_pages.insert_or_assign(resident_key, resident_page);
          state.page_table_entries[global_page_index] = PackPageTableEntry(
            resident_page.tile.tile_x, resident_page.tile.tile_y);

          const float logical_left
            = origin_x + static_cast<float>(page_x) * page_world_size;
          const float logical_right = logical_left + page_world_size;
          const float bottom
            = origin_y + static_cast<float>(page_y) * page_world_size;
          const float top = bottom + page_world_size;
          const float left = logical_left - page_guard_world;
          const float right = logical_right + page_guard_world;
          const float guarded_bottom = bottom - page_guard_world;
          const float guarded_top = top + page_guard_world;
          const glm::mat4 light_proj = glm::orthoRH_ZO(left, right,
            guarded_bottom, guarded_top, pending.near_plane, pending.far_plane);

          engine::ViewConstants page_view_constants = pending.view_constants;
          page_view_constants.SetViewMatrix(pending.light_view)
            .SetProjectionMatrix(light_proj)
            .SetCameraPosition(pending.light_eye);

          if (needs_raster) {
            ++rerasterized_pages;
            state.resolved_raster_pages.push_back(
              renderer::VirtualShadowResolvedRasterPage {
                .shadow_instance_index = 0U,
                .payload_index = 0U,
                .clip_level = clip_index,
                .page_index = local_page_index,
                .resident_key = resident_key,
                .atlas_tile_x = resident_page.tile.tile_x,
                .atlas_tile_y = resident_page.tile.tile_y,
                .view_constants = page_view_constants.GetSnapshot(),
              });
          }
        }
      }
    }
  };

  // Current-frame fine pages must win over coarse backbone coverage. If the
  // atlas is under pressure, keep the pages shading actually needs and let the
  // coarse safety net degrade first.
  const std::uint32_t coarse_backbone_begin
    = pending.clip_level_count > kDirectionalCoarseBackboneClipCount
    ? pending.clip_level_count - kDirectionalCoarseBackboneClipCount
    : 0U;
  process_clip_range_desc(0U, coarse_backbone_begin);
  process_clip_range_desc(coarse_backbone_begin, pending.clip_level_count);

  state.publish_diagnostics.resident_reuse_gate_open = false;
  if (pending.resident_reuse_snapshot.valid
    && pending.resident_reuse_snapshot.previous_pending_resolved_pages_empty
    && CanReuseResidentPages(pending.resident_reuse_snapshot, state)) {
    state.publish_diagnostics.resident_reuse_gate_open = true;
    state.resolved_raster_pages.clear();
  }

  state.publish_diagnostics.carried_resident_pages
    = static_cast<std::uint32_t>(state.resident_pages.size());
  state.publish_diagnostics.released_resident_pages = released_resident_pages;
  state.publish_diagnostics.marked_dirty_pages = marked_dirty_pages;
  state.publish_diagnostics.reused_requested_pages = reused_requested_pages;
  state.publish_diagnostics.allocated_pages = allocated_pages;
  state.publish_diagnostics.evicted_pages = evicted_pages;
  state.publish_diagnostics.allocation_failures = allocation_failures;
  state.publish_diagnostics.rerasterized_pages = rerasterized_pages;

  state.page_table_upload_entry_count
    = static_cast<std::uint32_t>(state.page_table_entries.size());
  if (state.page_table_upload_entry_count > 0U) {
    const auto page_table_srv
      = StagePageTableUpload(view_id, state.page_table_entries);
    if (!state.frame_publication.virtual_shadow_page_table_srv.IsValid()) {
      state.frame_publication.virtual_shadow_page_table_srv = page_table_srv;
    }
    state.page_table_upload_pending = page_table_srv.IsValid();
  } else {
    state.page_table_upload_pending = false;
  }

  pending.previous_resident_pages.clear();
  pending.previous_shadow_caster_bounds.clear();
  pending.dirty_resident_pages.clear();
  pending.dirty = false;
  RebuildResolveStateSnapshot(state);
  StageResolveStateUpload(view_id, state);
  RefreshAtlasTileDebugStates(state);
  RefreshViewExports(view_id, state);
  LogPublishTransition(view_id, state);
}

auto VirtualShadowMapBackend::PreparePageTableResources(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  const auto state_it = view_cache_.find(view_id);
  if (state_it == view_cache_.end()) {
    return;
  }

  const auto resources_it = view_page_table_resources_.find(view_id);
  if (resources_it == view_page_table_resources_.end()
    || !resources_it->second.gpu_buffer) {
    return;
  }

  auto& state = state_it->second;
  auto& resources = resources_it->second;
  if (!recorder.IsResourceTracked(*resources.gpu_buffer)) {
    recorder.BeginTrackingResourceState(
      *resources.gpu_buffer, graphics::ResourceStates::kCommon, true);
  }

  if (state.page_table_upload_pending && resources.upload_buffer) {
    if (resources.mapped_upload != nullptr
      && state.page_table_upload_entry_count > 0U) {
      std::memcpy(resources.mapped_upload, state.page_table_entries.data(),
        static_cast<std::size_t>(state.page_table_upload_entry_count)
          * sizeof(std::uint32_t));
    }
    if (!recorder.IsResourceTracked(*resources.upload_buffer)) {
      recorder.BeginTrackingResourceState(
        *resources.upload_buffer, graphics::ResourceStates::kCopySource, false);
    }
    recorder.RequireResourceState(
      *resources.gpu_buffer, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBuffer(*resources.gpu_buffer, 0U, *resources.upload_buffer, 0U,
      static_cast<std::size_t>(state.page_table_upload_entry_count)
        * sizeof(std::uint32_t));
    state.page_table_upload_pending = false;
  }

  recorder.RequireResourceState(
    *resources.gpu_buffer, graphics::ResourceStates::kShaderResource);
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  if (resolve_resources_it != view_resolve_resources_.end()) {
    auto& resolve_resources = resolve_resources_it->second;
    if (resolve_resources.resident_pages_gpu_buffer) {
      if (!recorder.IsResourceTracked(
            *resolve_resources.resident_pages_gpu_buffer)) {
        recorder.BeginTrackingResourceState(
          *resolve_resources.resident_pages_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      if (resolve_resources.resident_page_upload_pending
        && resolve_resources.resident_pages_upload_buffer) {
        if (resolve_resources.mapped_resident_pages_upload != nullptr
          && resolve_resources.resident_page_upload_count > 0U) {
          std::memcpy(resolve_resources.mapped_resident_pages_upload,
            state.resolve_resident_page_entries.data(),
            static_cast<std::size_t>(
              resolve_resources.resident_page_upload_count)
              * sizeof(renderer::VirtualShadowResolveResidentPageEntry));
        }
        if (!recorder.IsResourceTracked(
              *resolve_resources.resident_pages_upload_buffer)) {
          recorder.BeginTrackingResourceState(
            *resolve_resources.resident_pages_upload_buffer,
            graphics::ResourceStates::kCopySource, false);
        }
        if (resolve_resources.resident_page_upload_count > 0U) {
          recorder.RequireResourceState(
            *resolve_resources.resident_pages_gpu_buffer,
            graphics::ResourceStates::kCopyDest);
          recorder.FlushBarriers();
          recorder.CopyBuffer(*resolve_resources.resident_pages_gpu_buffer, 0U,
            *resolve_resources.resident_pages_upload_buffer, 0U,
            static_cast<std::size_t>(
              resolve_resources.resident_page_upload_count)
              * sizeof(renderer::VirtualShadowResolveResidentPageEntry));
        }
        resolve_resources.resident_page_upload_pending = false;
      }
      recorder.RequireResourceState(
        *resolve_resources.resident_pages_gpu_buffer,
        graphics::ResourceStates::kShaderResource);
    }
    if (resolve_resources.stats_gpu_buffer) {
      if (!recorder.IsResourceTracked(*resolve_resources.stats_gpu_buffer)) {
        recorder.BeginTrackingResourceState(*resolve_resources.stats_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      if (resolve_resources.stats_upload_pending
        && resolve_resources.stats_upload_buffer) {
        if (resolve_resources.mapped_stats_upload != nullptr) {
          std::memcpy(resolve_resources.mapped_stats_upload,
            &state.resolve_stats, sizeof(renderer::VirtualShadowResolveStats));
        }
        if (!recorder.IsResourceTracked(
              *resolve_resources.stats_upload_buffer)) {
          recorder.BeginTrackingResourceState(
            *resolve_resources.stats_upload_buffer,
            graphics::ResourceStates::kCopySource, false);
        }
        recorder.RequireResourceState(*resolve_resources.stats_gpu_buffer,
          graphics::ResourceStates::kCopyDest);
        recorder.FlushBarriers();
        recorder.CopyBuffer(*resolve_resources.stats_gpu_buffer, 0U,
          *resolve_resources.stats_upload_buffer, 0U,
          sizeof(renderer::VirtualShadowResolveStats));
        resolve_resources.stats_upload_pending = false;
      }
      recorder.RequireResourceState(*resolve_resources.stats_gpu_buffer,
        graphics::ResourceStates::kShaderResource);
    }
  }
  recorder.FlushBarriers();
}

auto VirtualShadowMapBackend::SetPublishedViewFrameBindingsSlot(
  const ViewId view_id, const engine::BindlessViewFrameBindingsSlot slot)
  -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    return;
  }

  for (auto& page : it->second.resolved_raster_pages) {
    page.view_constants.view_frame_bindings_bslot = slot;
  }
  if (it->second.pending_residency_resolve.valid) {
    it->second.pending_residency_resolve.view_constants
      .SetBindlessViewFrameBindingsSlot(slot, engine::ViewConstants::kRenderer);
  }
  RefreshViewExports(view_id, it->second);
}

auto VirtualShadowMapBackend::SubmitRequestFeedback(
  const ViewId view_id, VirtualShadowRequestFeedback feedback) -> void
{
  std::vector<AbsoluteClipPageRegion> source_absolute_frustum_regions {};
  if (const auto view_it = view_cache_.find(view_id);
    view_it != view_cache_.end()) {
    source_absolute_frustum_regions = view_it->second.absolute_frustum_regions;
  }
  request_feedback_.insert_or_assign(view_id,
    PendingRequestFeedback {
      .feedback = std::move(feedback),
      .source_absolute_frustum_regions
      = std::move(source_absolute_frustum_regions),
    });
}

auto VirtualShadowMapBackend::ClearRequestFeedback(const ViewId view_id) -> void
{
  request_feedback_.erase(view_id);
}

auto VirtualShadowMapBackend::SubmitResolvedRasterSchedule(
  const ViewId view_id, VirtualShadowResolvedRasterSchedule schedule) -> void
{
  resolved_raster_schedules_.insert_or_assign(view_id, std::move(schedule));
}

auto VirtualShadowMapBackend::ClearResolvedRasterSchedule(const ViewId view_id)
  -> void
{
  resolved_raster_schedules_.erase(view_id);
}

auto VirtualShadowMapBackend::TryGetFramePublication(
  const ViewId view_id) const noexcept -> const ShadowFramePublication*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.frame_publication : nullptr;
}

auto VirtualShadowMapBackend::TryGetRenderPlan(
  const ViewId view_id) const noexcept -> const VirtualShadowRenderPlan*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.render_plan : nullptr;
}

auto VirtualShadowMapBackend::TryGetViewIntrospection(
  const ViewId view_id) const noexcept -> const VirtualShadowViewIntrospection*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.introspection : nullptr;
}

auto VirtualShadowMapBackend::TryGetDirectionalVirtualMetadata(
  const ViewId view_id) const noexcept
  -> const engine::DirectionalVirtualShadowMetadata*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end()
      && !it->second.directional_virtual_metadata.empty()
    ? &it->second.directional_virtual_metadata.front()
    : nullptr;
}

auto VirtualShadowMapBackend::GetPhysicalPoolTexture() const noexcept
  -> const std::shared_ptr<graphics::Texture>&
{
  return physical_pool_texture_;
}

auto VirtualShadowMapBackend::BuildPublicationKey(
  const engine::ViewConstants& view_constants,
  const std::span<const engine::DirectionalShadowCandidate>
    directional_candidates,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::uint64_t shadow_caster_content_hash) const -> PublicationKey
{
  using namespace shadow_detail;
  PublicationKey key {};
  const auto view_hash_start
    = HashBytes(&shadow_quality_tier_, sizeof(shadow_quality_tier_));
  const auto view_matrix = view_constants.GetViewMatrix();
  const auto projection_matrix = view_constants.GetProjectionMatrix();
  const auto camera_position = view_constants.GetCameraPosition();
  key.view_hash = HashBytes(&view_matrix, sizeof(view_matrix), view_hash_start);
  key.view_hash
    = HashBytes(&projection_matrix, sizeof(projection_matrix), key.view_hash);
  key.view_hash
    = HashBytes(&camera_position, sizeof(camera_position), key.view_hash);
  key.candidate_hash = HashSpan(directional_candidates);
  key.caster_hash = HashSpan(shadow_caster_bounds);
  key.shadow_content_hash = shadow_caster_content_hash;
  return key;
}

auto VirtualShadowMapBackend::RebuildResolveStateSnapshot(
  ViewCacheEntry& state) const -> void
{
  state.resolve_resident_page_entries.clear();
  state.resolve_resident_page_entries.reserve(state.resident_pages.size());

  std::uint32_t clean_page_count = 0U;
  std::uint32_t dirty_page_count = 0U;
  std::uint32_t pending_page_count = 0U;
  for (const auto& [resident_key, resident_page] : state.resident_pages) {
    state.resolve_resident_page_entries.push_back(
      renderer::VirtualShadowResolveResidentPageEntry {
        .resident_key = resident_key,
        .atlas_tile_x = resident_page.tile.tile_x,
        .atlas_tile_y = resident_page.tile.tile_y,
        .residency_state = static_cast<std::uint32_t>(resident_page.state),
        .last_touched_frame = resident_page.last_touched_frame.get(),
        .last_requested_frame = resident_page.last_requested_frame.get(),
      });

    switch (resident_page.state) {
    case renderer::VirtualPageResidencyState::kResidentClean:
      ++clean_page_count;
      break;
    case renderer::VirtualPageResidencyState::kResidentDirty:
      ++dirty_page_count;
      break;
    case renderer::VirtualPageResidencyState::kPendingRender:
      ++pending_page_count;
      break;
    case renderer::VirtualPageResidencyState::kUnmapped:
      break;
    }
  }

  std::ranges::sort(state.resolve_resident_page_entries,
    [](const renderer::VirtualShadowResolveResidentPageEntry& lhs,
      const renderer::VirtualShadowResolveResidentPageEntry& rhs) {
      return lhs.resident_key < rhs.resident_key;
    });

  state.resolve_stats.resident_entry_count
    = static_cast<std::uint32_t>(state.resolve_resident_page_entries.size());
  state.resolve_stats.resident_entry_capacity
    = std::max(physical_pool_config_.physical_tile_capacity,
      state.resolve_stats.resident_entry_count);
  state.resolve_stats.clean_page_count = clean_page_count;
  state.resolve_stats.dirty_page_count = dirty_page_count;
  state.resolve_stats.pending_page_count = pending_page_count;
  state.resolve_stats.mapped_page_count
    = static_cast<std::uint32_t>(std::count_if(state.page_table_entries.begin(),
      state.page_table_entries.end(),
      [](const std::uint32_t entry) { return entry != 0U; }));
  state.resolve_stats.pending_raster_page_count
    = static_cast<std::uint32_t>(state.resolved_raster_pages.size());
  state.resolve_stats.selected_page_count
    = state.publish_diagnostics.selected_page_count;
  state.resolve_stats.allocated_page_count
    = state.publish_diagnostics.allocated_pages;
  state.resolve_stats.evicted_page_count
    = state.publish_diagnostics.evicted_pages;
  state.resolve_stats.rerasterized_page_count
    = state.publish_diagnostics.rerasterized_pages;
  state.resolve_stats.reused_requested_page_count
    = state.publish_diagnostics.reused_requested_pages;
}

auto VirtualShadowMapBackend::RefreshViewExports(
  const ViewId view_id, ViewCacheEntry& state) const -> void
{
  state.render_plan.depth_texture = physical_pool_texture_.get();
  state.render_plan.resolved_pages
    = std::span<const renderer::VirtualShadowResolvedRasterPage> {
        state.resolved_raster_pages
      };
  state.render_plan.page_size_texels = physical_pool_config_.page_size_texels;
  state.render_plan.atlas_tiles_per_axis
    = physical_pool_config_.atlas_tiles_per_axis;

  state.introspection.directional_virtual_metadata
    = state.directional_virtual_metadata;
  state.introspection.resolved_raster_pages = state.resolved_raster_pages;
  state.introspection.resolve_resident_page_entries
    = state.resolve_resident_page_entries;
  state.introspection.page_table_entries = state.page_table_entries;
  state.introspection.atlas_tile_debug_states = state.atlas_tile_debug_states;
  state.introspection.used_request_feedback
    = state.publish_diagnostics.feedback_decision
    == ViewCacheEntry::RequestFeedbackDecision::kAccepted;
  state.introspection.used_resolved_raster_schedule
    = state.publish_diagnostics.used_resolved_raster_schedule;
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  state.introspection.has_persistent_gpu_residency_state
    = resolve_resources_it != view_resolve_resources_.end()
    && resolve_resources_it->second.resident_pages_srv.IsValid()
    && resolve_resources_it->second.stats_srv.IsValid();
  state.introspection.resolve_resident_pages_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.resident_pages_srv
    : kInvalidShaderVisibleIndex;
  state.introspection.resolve_stats_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.stats_srv
    : kInvalidShaderVisibleIndex;
  state.introspection.mapped_page_count = state.resolve_stats.mapped_page_count;
  state.introspection.resident_page_count
    = state.resolve_stats.resident_entry_count;
  state.introspection.clean_page_count = state.resolve_stats.clean_page_count;
  state.introspection.dirty_page_count = state.resolve_stats.dirty_page_count;
  state.introspection.pending_page_count
    = state.resolve_stats.pending_page_count;
  state.introspection.pending_raster_page_count
    = state.resolve_stats.pending_raster_page_count;
  state.introspection.resolved_schedule_page_count
    = state.publish_diagnostics.resolved_schedule_pages;
  state.introspection.resolved_schedule_pruned_job_count
    = state.publish_diagnostics.resolved_schedule_pruned_jobs;
  state.introspection.selected_page_count
    = state.publish_diagnostics.selected_page_count;
  state.introspection.coarse_backbone_page_count
    = state.publish_diagnostics.coarse_backbone_pages;
  state.introspection.feedback_requested_page_count
    = state.publish_diagnostics.feedback_requested_pages;
  state.introspection.feedback_refinement_page_count
    = state.publish_diagnostics.feedback_refinement_pages;
  state.introspection.receiver_bootstrap_page_count
    = state.publish_diagnostics.receiver_bootstrap_pages;
  state.introspection.current_frame_reinforcement_page_count
    = state.publish_diagnostics.current_frame_reinforcement_pages;
  state.introspection.current_frame_reinforcement_reference_frame
    = state.publish_diagnostics.current_frame_reinforcement_reference_frame;
  state.introspection.resolved_schedule_age_frames
    = state.publish_diagnostics.resolved_schedule_age_frames;
  state.introspection.allocated_page_count
    = state.publish_diagnostics.allocated_pages;
  state.introspection.evicted_page_count
    = state.publish_diagnostics.evicted_pages;
  state.introspection.rerasterized_page_count
    = state.publish_diagnostics.rerasterized_pages;
  state.introspection.resolve_stats = state.resolve_stats;
}

auto VirtualShadowMapBackend::RefreshAtlasTileDebugStates(
  ViewCacheEntry& state) const -> void
{
  const auto tiles_per_axis = physical_pool_config_.atlas_tiles_per_axis;
  const auto tile_count = tiles_per_axis * tiles_per_axis;
  state.atlas_tile_debug_states.assign(tile_count,
    static_cast<std::uint32_t>(
      renderer::VirtualShadowAtlasTileDebugState::kCleared));

  if (tiles_per_axis == 0U) {
    return;
  }

  const auto tile_index_for
    = [tiles_per_axis](
        const PhysicalTileAddress tile) -> std::optional<std::uint32_t> {
    if (tile.tile_x >= tiles_per_axis || tile.tile_y >= tiles_per_axis) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(tile.tile_y) * tiles_per_axis
      + static_cast<std::uint32_t>(tile.tile_x);
  };

  for (const auto& [_, resident_page] : state.resident_pages) {
    const auto tile_index = tile_index_for(resident_page.tile);
    if (!tile_index.has_value()) {
      continue;
    }

    const bool requested_this_frame
      = resident_page.last_requested_frame == frame_sequence_;
    const auto tile_state = requested_this_frame
      ? (resident_page.state
              == renderer::VirtualPageResidencyState::kPendingRender
            ? renderer::VirtualShadowAtlasTileDebugState::kRewritten
            : renderer::VirtualShadowAtlasTileDebugState::kReused)
      : renderer::VirtualShadowAtlasTileDebugState::kCached;
    state.atlas_tile_debug_states[*tile_index]
      = static_cast<std::uint32_t>(tile_state);
  }

  for (const auto& page : state.resolved_raster_pages) {
    const auto tile_index = tile_index_for(PhysicalTileAddress {
      .tile_x = page.atlas_tile_x, .tile_y = page.atlas_tile_y });
    if (!tile_index.has_value()) {
      continue;
    }
    state.atlas_tile_debug_states[*tile_index] = static_cast<std::uint32_t>(
      renderer::VirtualShadowAtlasTileDebugState::kRewritten);
  }
}

auto VirtualShadowMapBackend::CanReuseResidentPages(
  const ViewCacheEntry& previous, const ViewCacheEntry& current) const noexcept
  -> bool
{
  if (previous.key.candidate_hash != current.key.candidate_hash
    || previous.key.caster_hash != current.key.caster_hash
    || previous.key.shadow_content_hash != current.key.shadow_content_hash) {
    return false;
  }

  if (previous.directional_virtual_metadata.size()
      != current.directional_virtual_metadata.size()
    || previous.page_table_entries.size()
      != current.page_table_entries.size()) {
    return false;
  }

  // Compare metadata element-wise: exact match for integer fields,
  // tolerance-based for floats that may experience FP non-determinism
  // across recomputation with identical inputs (e.g. light_view built
  // from the same snapped camera position via two lookAtRH calls).
  for (std::size_t i = 0U; i < previous.directional_virtual_metadata.size();
    ++i) {
    const auto& prev = previous.directional_virtual_metadata[i];
    const auto& curr = current.directional_virtual_metadata[i];
    if (prev.shadow_instance_index != curr.shadow_instance_index
      || prev.flags != curr.flags
      || prev.clip_level_count != curr.clip_level_count
      || prev.pages_per_axis != curr.pages_per_axis
      || prev.page_size_texels != curr.page_size_texels
      || prev.page_table_offset != curr.page_table_offset) {
      return false;
    }
    if (!shadow_detail::DirectionalCacheFloatEqual(
          prev.constant_bias, curr.constant_bias)
      || !shadow_detail::DirectionalCacheFloatEqual(
        prev.normal_bias, curr.normal_bias)) {
      return false;
    }
    if (!shadow_detail::DirectionalCacheMat4Equal(
          prev.light_view, curr.light_view)) {
      return false;
    }
    const auto clip_count = std::min(
      prev.clip_level_count, engine::kMaxVirtualDirectionalClipLevels);
    for (std::uint32_t c = 0U; c < clip_count; ++c) {
      const auto& pc = prev.clip_metadata[c];
      const auto& cc = curr.clip_metadata[c];
      if (!shadow_detail::DirectionalCacheFloatEqual(
            pc.origin_page_scale.x, cc.origin_page_scale.x)
        || !shadow_detail::DirectionalCacheFloatEqual(
          pc.origin_page_scale.y, cc.origin_page_scale.y)
        || !shadow_detail::DirectionalCacheFloatEqual(
          pc.origin_page_scale.z, cc.origin_page_scale.z)
        || !shadow_detail::DirectionalCacheFloatEqual(
          pc.origin_page_scale.w, cc.origin_page_scale.w)
        || !shadow_detail::DirectionalCacheFloatEqual(
          pc.bias_reserved.x, cc.bias_reserved.x)) {
        return false;
      }
    }
  }

  const bool page_table_equal = previous.page_table_entries.empty()
    || std::memcmp(previous.page_table_entries.data(),
         current.page_table_entries.data(),
         previous.page_table_entries.size() * sizeof(std::uint32_t))
      == 0;
  return page_table_equal;
}

auto VirtualShadowMapBackend::CanReuseResidentPages(
  const ViewCacheEntry::PendingResidentReuseGateSnapshot& previous,
  const ViewCacheEntry& current) const noexcept -> bool
{
  if (!previous.valid) {
    return false;
  }

  if (previous.key.candidate_hash != current.key.candidate_hash
    || previous.key.caster_hash != current.key.caster_hash
    || previous.key.shadow_content_hash != current.key.shadow_content_hash) {
    return false;
  }

  if (previous.directional_virtual_metadata.size()
      != current.directional_virtual_metadata.size()
    || previous.page_table_entries.size()
      != current.page_table_entries.size()) {
    return false;
  }

  for (std::size_t i = 0U; i < current.directional_virtual_metadata.size();
    ++i) {
    const auto& previous_metadata = previous.directional_virtual_metadata[i];
    const auto& current_metadata = current.directional_virtual_metadata[i];
    if (!IsDirectionalVirtualAddressSpaceCompatible(
          previous_metadata, current_metadata)) {
      return false;
    }

    if (previous_metadata.shadow_instance_index
        != current_metadata.shadow_instance_index
      || previous_metadata.flags != current_metadata.flags
      || previous_metadata.clip_level_count != current_metadata.clip_level_count
      || previous_metadata.pages_per_axis != current_metadata.pages_per_axis
      || previous_metadata.page_size_texels != current_metadata.page_size_texels
      || previous_metadata.page_table_offset
        != current_metadata.page_table_offset
      || !shadow_detail::DirectionalCacheFloatEqual(
        previous_metadata.constant_bias, current_metadata.constant_bias)
      || !shadow_detail::DirectionalCacheFloatEqual(
        previous_metadata.normal_bias, current_metadata.normal_bias)) {
      return false;
    }

    for (std::uint32_t clip_index = 0U;
      clip_index < current_metadata.clip_level_count; ++clip_index) {
      const auto& pm = previous_metadata.clip_metadata[clip_index];
      const auto& cm = current_metadata.clip_metadata[clip_index];
      if (!shadow_detail::DirectionalCacheFloatEqual(
            pm.origin_page_scale.x, cm.origin_page_scale.x)
        || !shadow_detail::DirectionalCacheFloatEqual(
          pm.origin_page_scale.y, cm.origin_page_scale.y)
        || !shadow_detail::DirectionalCacheFloatEqual(
          pm.origin_page_scale.z, cm.origin_page_scale.z)
        || !shadow_detail::DirectionalCacheFloatEqual(
          pm.origin_page_scale.w, cm.origin_page_scale.w)
        || !shadow_detail::DirectionalCacheFloatEqual(
          pm.bias_reserved.x, cm.bias_reserved.x)) {
        return false;
      }
    }
  }

  const bool page_table_equal = previous.page_table_entries.empty()
    || std::memcmp(previous.page_table_entries.data(),
         current.page_table_entries.data(),
         previous.page_table_entries.size() * sizeof(std::uint32_t))
      == 0;
  return page_table_equal;
}

auto VirtualShadowMapBackend::BuildPhysicalPoolConfig(
  const engine::DirectionalShadowCandidate& candidate,
  const std::chrono::milliseconds gpu_budget,
  const std::size_t shadow_caster_count) const -> PhysicalPoolConfig
{
  using namespace shadow_detail;
  (void)gpu_budget;
  (void)shadow_caster_count;

  PhysicalPoolConfig config {};
  config.clip_level_count = std::clamp(
    ResolveVirtualClipLevelCount(shadow_quality_tier_, candidate.cascade_count),
    1U, engine::kMaxVirtualDirectionalClipLevels);
  const auto requested_pages_per_axis
    = ResolvePagesPerAxis(shadow_quality_tier_);
  config.virtual_pages_per_clip_axis = requested_pages_per_axis;
  config.virtual_page_count = config.clip_level_count
    * config.virtual_pages_per_clip_axis * config.virtual_pages_per_clip_axis;
  config.physical_tile_capacity = std::min(config.virtual_page_count,
    ResolvePhysicalTileCapacity(shadow_quality_tier_));
  config.atlas_tiles_per_axis = static_cast<std::uint32_t>(std::ceil(std::sqrt(
    static_cast<float>(std::max(1U, config.physical_tile_capacity)))));

  const auto authored_resolution = ApplyDirectionalShadowQualityTier(
    ShadowResolutionFromHint(candidate.resolution_hint), shadow_quality_tier_,
    1U);
  const auto authored_page_size
    = authored_resolution / std::max(1U, config.virtual_pages_per_clip_axis);
  const auto atlas_limited_page_size = std::max(128U,
    ResolveMaxVirtualAtlasResolution(shadow_quality_tier_)
      / std::max(1U, config.atlas_tiles_per_axis));
  const auto max_page_size
    = std::min(ResolveMaxVirtualPageSizeTexels(shadow_quality_tier_),
      atlas_limited_page_size);
  config.page_size_texels
    = std::clamp(authored_page_size, 128U, std::max(128U, max_page_size));
  config.atlas_resolution
    = config.atlas_tiles_per_axis * config.page_size_texels;
  return config;
}

auto VirtualShadowMapBackend::EnsurePhysicalPool(
  const PhysicalPoolConfig& config) -> void
{
  if (config.virtual_page_count == 0U || config.physical_tile_capacity == 0U
    || config.atlas_resolution == 0U) {
    return;
  }

  const bool needs_recreate = !physical_pool_texture_
    || config.page_size_texels != physical_pool_config_.page_size_texels
    || config.virtual_pages_per_clip_axis
      != physical_pool_config_.virtual_pages_per_clip_axis
    || config.clip_level_count != physical_pool_config_.clip_level_count
    || config.atlas_resolution > physical_pool_config_.atlas_resolution;
  if (!needs_recreate) {
    physical_pool_config_ = config;
    return;
  }

  ReleasePhysicalPool();

  physical_pool_config_ = config;

  graphics::TextureDesc desc {};
  desc.width = physical_pool_config_.atlas_resolution;
  desc.height = physical_pool_config_.atlas_resolution;
  desc.array_size = 1U;
  desc.mip_levels = 1U;
  desc.format = oxygen::Format::kDepth32;
  desc.texture_type = oxygen::TextureType::kTexture2D;
  desc.is_render_target = true;
  desc.is_shader_resource = true;
  desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kCommon;
  desc.debug_name = "VirtualDirectionalShadowPhysicalPool";

  physical_pool_texture_ = gfx_->CreateTexture(desc);
  if (!physical_pool_texture_) {
    physical_pool_config_ = {};
    throw std::runtime_error(
      "VirtualShadowMapBackend: failed to create physical page-pool texture");
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();
  registry.Register(physical_pool_texture_);

  auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    physical_pool_texture_.reset();
    physical_pool_config_ = {};
    throw std::runtime_error(
      "VirtualShadowMapBackend: failed to allocate physical pool SRV");
  }

  const graphics::TextureViewDescription srv_desc {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = oxygen::Format::kDepth32,
    .dimension = oxygen::TextureType::kTexture2D,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };

  physical_pool_srv_ = allocator.GetShaderVisibleIndex(handle);
  physical_pool_srv_view_ = registry.RegisterView(
    *physical_pool_texture_, std::move(handle), srv_desc);
  free_physical_tiles_.clear();
  free_physical_tiles_.reserve(
    static_cast<std::size_t>(physical_pool_config_.physical_tile_capacity));
  std::uint32_t allocated_tiles = 0U;
  for (std::uint32_t tile_y = 0U;
    tile_y < physical_pool_config_.atlas_tiles_per_axis
    && allocated_tiles < physical_pool_config_.physical_tile_capacity;
    ++tile_y) {
    for (std::uint32_t tile_x = 0U;
      tile_x < physical_pool_config_.atlas_tiles_per_axis
      && allocated_tiles < physical_pool_config_.physical_tile_capacity;
      ++tile_x) {
      free_physical_tiles_.push_back(PhysicalTileAddress {
        .tile_x = static_cast<std::uint16_t>(tile_x),
        .tile_y = static_cast<std::uint16_t>(tile_y),
      });
      ++allocated_tiles;
    }
  }
  view_cache_.clear();

  LOG_F(INFO,
    "VirtualShadowMapBackend: created physical page pool {}x{} page_size={} "
    "pages_per_clip_axis={} clip_levels={} physical_tiles={} virtual_pages={} "
    "srv={}",
    physical_pool_config_.atlas_resolution,
    physical_pool_config_.atlas_resolution,
    physical_pool_config_.page_size_texels,
    physical_pool_config_.virtual_pages_per_clip_axis,
    physical_pool_config_.clip_level_count,
    physical_pool_config_.physical_tile_capacity,
    physical_pool_config_.virtual_page_count, physical_pool_srv_.get());
}

auto VirtualShadowMapBackend::ReleasePhysicalPool() -> void
{
  if (!gfx_) {
    physical_pool_texture_.reset();
    physical_pool_srv_view_ = {};
    physical_pool_srv_ = kInvalidShaderVisibleIndex;
    physical_pool_config_ = {};
    return;
  }

  if (physical_pool_texture_ && physical_pool_srv_view_->IsValid()) {
    gfx_->GetResourceRegistry().UnRegisterView(
      *physical_pool_texture_, physical_pool_srv_view_);
  }

  physical_pool_texture_.reset();
  physical_pool_srv_view_ = {};
  physical_pool_srv_ = kInvalidShaderVisibleIndex;
  physical_pool_config_ = {};
  free_physical_tiles_.clear();
  view_cache_.clear();
}

auto VirtualShadowMapBackend::AllocatePhysicalTile()
  -> std::optional<PhysicalTileAddress>
{
  if (free_physical_tiles_.empty()) {
    return std::nullopt;
  }

  auto tile = free_physical_tiles_.back();
  free_physical_tiles_.pop_back();
  return tile;
}

auto VirtualShadowMapBackend::AcquirePhysicalTile(ViewCacheEntry& state,
  const std::uint32_t pages_per_level, std::uint32_t& evicted_page_count)
  -> std::optional<PhysicalTileAddress>
{
  (void)pages_per_level;
  if (const auto free_tile = AllocatePhysicalTile(); free_tile.has_value()) {
    return free_tile;
  }

  auto eviction_it = state.resident_pages.end();
  std::uint64_t eviction_key = 0U;
  for (auto it = state.resident_pages.begin(); it != state.resident_pages.end();
    ++it) {
    if (it->second.last_requested_frame == frame_sequence_) {
      continue;
    }

    if (eviction_it == state.resident_pages.end()) {
      eviction_it = it;
      eviction_key = it->first;
      continue;
    }

    const bool current_contents_invalid = !it->second.ContentsValid();
    const bool best_contents_invalid = !eviction_it->second.ContentsValid();
    if (current_contents_invalid != best_contents_invalid) {
      if (current_contents_invalid) {
        eviction_it = it;
        eviction_key = it->first;
      }
      continue;
    }

    const auto current_clip_level
      = shadow_detail::VirtualResidentPageKeyClipLevel(it->first);
    const auto best_clip_level
      = shadow_detail::VirtualResidentPageKeyClipLevel(eviction_key);
    if (current_clip_level != best_clip_level) {
      if (current_clip_level > best_clip_level) {
        eviction_it = it;
        eviction_key = it->first;
      }
      continue;
    }

    if (it->second.last_touched_frame
      != eviction_it->second.last_touched_frame) {
      if (it->second.last_touched_frame
        < eviction_it->second.last_touched_frame) {
        eviction_it = it;
        eviction_key = it->first;
      }
      continue;
    }

    if (it->first < eviction_key) {
      eviction_it = it;
      eviction_key = it->first;
    }
  }

  if (eviction_it == state.resident_pages.end()) {
    return std::nullopt;
  }

  const auto tile = eviction_it->second.tile;
  state.resident_pages.erase(eviction_it);
  ++evicted_page_count;
  return tile;
}

auto VirtualShadowMapBackend::ReleasePhysicalTile(
  const PhysicalTileAddress tile) -> void
{
  free_physical_tiles_.push_back(tile);
}

auto VirtualShadowMapBackend::BuildDirectionalVirtualViewState(
  const ViewId view_id, const engine::ViewConstants& view_constants,
  const engine::DirectionalShadowCandidate& candidate,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const ViewCacheEntry* previous_state, ViewCacheEntry& state) -> void
{
  using namespace shadow_detail;

  if (!physical_pool_texture_) {
    return;
  }

  const auto camera_view_constants = view_constants.GetSnapshot();
  const glm::mat4 view_matrix = camera_view_constants.view_matrix;
  const glm::mat4 projection_matrix = camera_view_constants.projection_matrix;
  const glm::mat4 inv_view = glm::inverse(view_matrix);
  const glm::mat4 inv_proj = glm::inverse(projection_matrix);

  std::array<glm::vec3, 4> view_near_corners {};
  std::array<glm::vec3, 4> view_far_corners {};
  constexpr std::array<glm::vec2, 4> clip_corners {
    glm::vec2(-1.0F, -1.0F),
    glm::vec2(1.0F, -1.0F),
    glm::vec2(1.0F, 1.0F),
    glm::vec2(-1.0F, 1.0F),
  };
  for (std::size_t i = 0; i < clip_corners.size(); ++i) {
    view_near_corners[i]
      = TransformPoint(inv_proj, glm::vec3(clip_corners[i], 0.0F));
    view_far_corners[i]
      = TransformPoint(inv_proj, glm::vec3(clip_corners[i], 1.0F));
  }

  float near_depth = 0.0F;
  float far_depth = 0.0F;
  for (std::size_t i = 0; i < view_near_corners.size(); ++i) {
    near_depth += std::max(0.0F, -view_near_corners[i].z);
    far_depth += std::max(0.0F, -view_far_corners[i].z);
  }
  near_depth /= static_cast<float>(view_near_corners.size());
  far_depth /= static_cast<float>(view_far_corners.size());
  far_depth = std::max(far_depth, near_depth + kMinClipSpan);

  float effective_near_depth = near_depth;
  float effective_far_depth = far_depth;
  if (!visible_receiver_bounds.empty()) {
    float receiver_near_depth = std::numeric_limits<float>::max();
    float receiver_far_depth = 0.0F;
    for (const auto& receiver_bound : visible_receiver_bounds) {
      const glm::vec3 receiver_vs
        = glm::vec3(view_matrix * glm::vec4(glm::vec3(receiver_bound), 1.0F));
      const float receiver_radius = std::max(0.0F, receiver_bound.w);
      const float receiver_center_depth = std::max(0.0F, -receiver_vs.z);
      receiver_near_depth = std::min(receiver_near_depth,
        std::max(near_depth, receiver_center_depth - receiver_radius));
      receiver_far_depth = std::max(receiver_far_depth,
        std::min(far_depth, receiver_center_depth + receiver_radius));
    }

    if (receiver_far_depth > 0.0F) {
      effective_near_depth = std::min(receiver_near_depth, receiver_far_depth);
      effective_far_depth
        = std::max(receiver_far_depth, effective_near_depth + kMinClipSpan);
      effective_far_depth = std::min(effective_far_depth, far_depth);
    }
  }

  const std::uint32_t clip_level_count = physical_pool_config_.clip_level_count;
  const std::uint32_t pages_per_axis
    = physical_pool_config_.virtual_pages_per_clip_axis;
  const std::uint32_t pages_per_level = pages_per_axis * pages_per_axis;
  const glm::vec3 light_dir_to_surface
    = NormalizeOrFallback(candidate.direction_ws, glm::vec3(0.0F, -1.0F, 0.0F));
  const glm::vec3 light_dir_to_light = -light_dir_to_surface;
  const glm::vec3 world_up
    = std::abs(glm::dot(light_dir_to_light, glm::vec3(0.0F, 0.0F, 1.0F)))
      > 0.95F
    ? glm::vec3(1.0F, 0.0F, 0.0F)
    : glm::vec3(0.0F, 0.0F, 1.0F);

  const glm::vec3 camera_position = camera_view_constants.camera_position;
  std::array<float, engine::kMaxVirtualDirectionalClipLevels>
    clip_half_extents {};
  std::array<float, engine::kMaxVirtualDirectionalClipLevels>
    clip_page_world {};
  std::array<float, engine::kMaxVirtualDirectionalClipLevels> clip_origin_x {};
  std::array<float, engine::kMaxVirtualDirectionalClipLevels> clip_origin_y {};
  std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
    clip_grid_origin_x {};
  std::array<std::int32_t, engine::kMaxVirtualDirectionalClipLevels>
    clip_grid_origin_y {};

  const float authored_first_clip_end = ResolveClipEndDepth(
    candidate, 0U, std::max(near_depth, 0.0F), near_depth, far_depth);
  const float first_clip_half_extent_floor = far_depth
    / std::exp2(static_cast<float>(std::max(clip_level_count, 1U) - 1U));
  const float desired_first_half_extent
    = std::max(std::max(authored_first_clip_end, first_clip_half_extent_floor),
      kMinClipSpan);
  const float desired_base_page_world = (2.0F * desired_first_half_extent)
    / std::max(1.0F, static_cast<float>(pages_per_axis));
  const float base_page_world = QuantizeUpToPowerOfTwo(desired_base_page_world);
  const float base_half_extent = base_page_world
    * std::max(1.0F, static_cast<float>(pages_per_axis)) * 0.5F;

  float largest_half_extent = 0.0F;
  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    const float scale = std::exp2(static_cast<float>(clip_index));
    const float half_extent = base_half_extent * scale;
    clip_half_extents[clip_index] = half_extent;
    clip_page_world[clip_index] = base_page_world * scale;
    largest_half_extent = std::max(largest_half_extent, half_extent);
  }

  const glm::mat4 rot_view = glm::lookAtRH(
    glm::vec3(0.0F), glm::vec3(0.0F) + light_dir_to_surface, world_up);
  const glm::vec3 cam_rot_ls
    = glm::vec3(rot_view * glm::vec4(camera_position, 1.0F));

  const float snap_size = clip_page_world[clip_level_count - 1U];
  const float snapped_x = std::floor(cam_rot_ls.x / snap_size) * snap_size;
  const float snapped_y = std::floor(cam_rot_ls.y / snap_size) * snap_size;

  const glm::vec3 light_eye_ls = glm::vec3(snapped_x, snapped_y,
    cam_rot_ls.z + largest_half_extent + kLightPullbackPadding);

  const glm::mat4 inv_rot_view = glm::inverse(rot_view);
  const glm::vec3 light_eye
    = glm::vec3(inv_rot_view * glm::vec4(light_eye_ls, 1.0F));

  const glm::mat4 light_view
    = glm::lookAtRH(light_eye, light_eye + light_dir_to_surface, world_up);
  const glm::vec3 camera_ls
    = glm::vec3(light_view * glm::vec4(camera_position, 1.0F));

  float max_depth
    = -(largest_half_extent + kLightPullbackPadding) + largest_half_extent;
  float min_depth
    = -(largest_half_extent + kLightPullbackPadding) - largest_half_extent;
  [[maybe_unused]] const bool tightened
    = TightenDepthRangeWithShadowCasters(shadow_caster_bounds, light_view,
      largest_half_extent, largest_half_extent, min_depth, max_depth);

  const float depth_padding
    = std::max(kMinShadowDepthPadding, largest_half_extent * 0.1F);
  const float near_plane = std::max(0.1F, -max_depth - depth_padding);
  const float far_plane
    = std::max(near_plane + 1.0F, -min_depth + depth_padding);
  const glm::mat4 depth_only_proj
    = glm::orthoRH_ZO(-1.0F, 1.0F, -1.0F, 1.0F, near_plane, far_plane);
  // Oxygen uses right-handed shadow views with forward along -Z, so derive the
  // linear depth map from z=0 and z=-1 in light-view space rather than z=+1.
  // glm::orthoRH_ZO stores the exact linear depth mapping in the projection
  // matrix. Extract it directly instead of numerically reconstructing it.
  const float depth_scale = depth_only_proj[2][2];
  const float depth_bias = depth_only_proj[3][2];

  const auto flags = BuildShadowProductFlags(candidate.light_flags);
  state.shadow_instances.push_back(engine::ShadowInstanceMetadata {
    .light_index = candidate.light_index,
    .payload_index = 0U,
    .domain = static_cast<std::uint32_t>(engine::ShadowDomain::kDirectional),
    .implementation_kind
    = static_cast<std::uint32_t>(engine::ShadowImplementationKind::kVirtual),
    .flags = flags,
  });

  engine::DirectionalVirtualShadowMetadata metadata {};
  metadata.shadow_instance_index = 0U;
  metadata.flags = flags;
  metadata.constant_bias = candidate.bias;
  metadata.normal_bias = candidate.normal_bias;
  metadata.clip_level_count = clip_level_count;
  metadata.pages_per_axis = pages_per_axis;
  metadata.page_size_texels = physical_pool_config_.page_size_texels;
  metadata.page_table_offset = 0U;
  metadata.light_view = light_view;
  state.shadow_caster_bounds.assign(
    shadow_caster_bounds.begin(), shadow_caster_bounds.end());

  state.page_table_entries.resize(static_cast<std::size_t>(clip_level_count)
      * static_cast<std::size_t>(pages_per_level),
    0U);
  state.resolved_raster_pages.reserve(state.page_table_entries.size());

  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    const float half_extent = clip_half_extents[clip_index];
    const float page_world_size = clip_page_world[clip_index];
    clip_grid_origin_x[clip_index] = static_cast<std::int32_t>(
      std::floor((camera_ls.x - half_extent) / page_world_size));
    clip_grid_origin_y[clip_index] = static_cast<std::int32_t>(
      std::floor((camera_ls.y - half_extent) / page_world_size));
    clip_origin_x[clip_index]
      = static_cast<float>(clip_grid_origin_x[clip_index]) * page_world_size;
    clip_origin_y[clip_index]
      = static_cast<float>(clip_grid_origin_y[clip_index]) * page_world_size;
    metadata.clip_metadata[clip_index].origin_page_scale
      = glm::vec4(clip_origin_x[clip_index], clip_origin_y[clip_index],
        page_world_size, depth_scale);
    metadata.clip_metadata[clip_index].bias_reserved
      = glm::vec4(depth_bias, 0.0F, 0.0F, 0.0F);
  }
  struct CasterBoundsLightSpace2D {
    float min_x { 0.0F };
    float max_x { 0.0F };
    float min_y { 0.0F };
    float max_y { 0.0F };
  };
  std::vector<CasterBoundsLightSpace2D> caster_bounds_ls;
  caster_bounds_ls.reserve(shadow_caster_bounds.size());
  for (const auto& bound : shadow_caster_bounds) {
    const glm::vec3 center_ls
      = glm::vec3(light_view * glm::vec4(glm::vec3(bound), 1.0F));
    // Pad by 5% to absorb precision loss and PCF reach
    const float radius = ((std::max)(0.0F, bound.w) * 1.05F) + 0.1F;
    caster_bounds_ls.push_back(CasterBoundsLightSpace2D {
      .min_x = center_ls.x - radius,
      .max_x = center_ls.x + radius,
      .min_y = center_ls.y - radius,
      .max_y = center_ls.y + radius,
    });
  }

  struct ClipSelectedRegion {
    bool valid { false };
    std::uint32_t min_x { 0U };
    std::uint32_t max_x { 0U };
    std::uint32_t min_y { 0U };
    std::uint32_t max_y { 0U };
  };
  std::vector<std::uint8_t> selected_pages(state.page_table_entries.size(), 0U);
  std::uint32_t selected_page_count = 0U;
  const auto mark_selected_page
    = [&](const std::uint32_t clip_index, const std::uint32_t page_x,
        const std::uint32_t page_y) -> bool {
    if (clip_index >= clip_level_count || page_x >= pages_per_axis
      || page_y >= pages_per_axis) {
      return false;
    }

    const auto global_page_index = static_cast<std::size_t>(clip_index)
        * static_cast<std::size_t>(pages_per_level)
      + static_cast<std::size_t>(page_y * pages_per_axis + page_x);
    if (global_page_index >= selected_pages.size()
      || selected_pages[global_page_index] != 0U) {
      return false;
    }

    // Caster culling: If this page does not intersect any caster's 2D
    // footprint, it will never contain shadow-casting geometry, and thus cannot
    // cast shadows. By skipping allocation, the sampler naturally falls back to
    // returning 1.0 (unoccluded), which is perfectly accurate and avoids
    // immense page churn.
    bool overlaps_caster = false;
    if (!caster_bounds_ls.empty()) {
      const float page_world_size = clip_page_world[clip_index];
      const float page_min_x = clip_origin_x[clip_index]
        + static_cast<float>(page_x) * page_world_size;
      const float page_max_x = page_min_x + page_world_size;
      const float page_min_y = clip_origin_y[clip_index]
        + static_cast<float>(page_y) * page_world_size;
      const float page_max_y = page_min_y + page_world_size;

      for (const auto& caster : caster_bounds_ls) {
        if (page_min_x <= caster.max_x && page_max_x >= caster.min_x
          && page_min_y <= caster.max_y && page_max_y >= caster.min_y) {
          overlaps_caster = true;
          break;
        }
      }
    }

    if (!overlaps_caster) {
      return false;
    }

    selected_pages[global_page_index] = 1U;
    ++selected_page_count;
    return true;
  };

  const auto mark_region
    = [&](const std::uint32_t clip_index, const ClipSelectedRegion& region) {
        if (!region.valid) {
          return 0U;
        }

        std::uint32_t added = 0U;
        for (std::uint32_t page_y = region.min_y; page_y <= region.max_y;
          ++page_y) {
          for (std::uint32_t page_x = region.min_x; page_x <= region.max_x;
            ++page_x) {
            if (mark_selected_page(clip_index, page_x, page_y)) {
              ++added;
            }
          }
        }
        return added;
      };
  const auto mark_dilated_page =
    [&](const std::uint32_t clip_index, const std::uint32_t page_x,
      const std::uint32_t page_y, const std::uint32_t guard_radius) {
      if (clip_index >= clip_level_count || page_x >= pages_per_axis
        || page_y >= pages_per_axis) {
        return 0U;
      }

      const auto min_x = page_x > guard_radius ? page_x - guard_radius : 0U;
      const auto min_y = page_y > guard_radius ? page_y - guard_radius : 0U;
      const auto max_x = std::min(pages_per_axis - 1U, page_x + guard_radius);
      const auto max_y = std::min(pages_per_axis - 1U, page_y + guard_radius);

      std::uint32_t added = 0U;
      for (std::uint32_t dilated_page_y = min_y; dilated_page_y <= max_y;
        ++dilated_page_y) {
        for (std::uint32_t dilated_page_x = min_x; dilated_page_x <= max_x;
          ++dilated_page_x) {
          if (mark_selected_page(clip_index, dilated_page_x, dilated_page_y)) {
            ++added;
          }
        }
      }
      return added;
    };
  const auto mark_absolute_region_limited =
    [&](const std::uint32_t clip_index,
      const AbsoluteClipPageRegion& absolute_region,
      std::uint32_t remaining_budget) {
      if (!absolute_region.valid || remaining_budget == 0U
        || clip_index >= clip_level_count) {
        return 0U;
      }

      const auto local_min_x
        = std::max(0, absolute_region.min_x - clip_grid_origin_x[clip_index]);
      const auto local_max_x
        = std::min(static_cast<std::int32_t>(pages_per_axis - 1U),
          absolute_region.max_x - clip_grid_origin_x[clip_index]);
      const auto local_min_y
        = std::max(0, absolute_region.min_y - clip_grid_origin_y[clip_index]);
      const auto local_max_y
        = std::min(static_cast<std::int32_t>(pages_per_axis - 1U),
          absolute_region.max_y - clip_grid_origin_y[clip_index]);
      if (local_min_x > local_max_x || local_min_y > local_max_y) {
        return 0U;
      }

      std::uint32_t added = 0U;
      for (std::int32_t page_y = local_min_y; page_y <= local_max_y; ++page_y) {
        for (std::int32_t page_x = local_min_x; page_x <= local_max_x;
          ++page_x) {
          if (mark_selected_page(clip_index, static_cast<std::uint32_t>(page_x),
                static_cast<std::uint32_t>(page_y))) {
            ++added;
            if (added >= remaining_budget) {
              return added;
            }
          }
        }
      }
      return added;
    };
  const auto expand_absolute_region
    = [&](const AbsoluteClipPageRegion& region,
        const std::uint32_t guard_pages) -> AbsoluteClipPageRegion {
    if (!region.valid) {
      return region;
    }

    const auto guard = static_cast<std::int32_t>(guard_pages);
    return AbsoluteClipPageRegion {
      .valid = true,
      .min_x = region.min_x - guard,
      .max_x = region.max_x + guard,
      .min_y = region.min_y - guard,
      .max_y = region.max_y + guard,
    };
  };
  const auto intersect_absolute_region
    = [](const AbsoluteClipPageRegion& lhs,
        const AbsoluteClipPageRegion& rhs) -> AbsoluteClipPageRegion {
    if (!lhs.valid || !rhs.valid) {
      return {};
    }

    const auto min_x = std::max(lhs.min_x, rhs.min_x);
    const auto max_x = std::min(lhs.max_x, rhs.max_x);
    const auto min_y = std::max(lhs.min_y, rhs.min_y);
    const auto max_y = std::min(lhs.max_y, rhs.max_y);
    if (min_x > max_x || min_y > max_y) {
      return {};
    }

    return AbsoluteClipPageRegion {
      .valid = true,
      .min_x = min_x,
      .max_x = max_x,
      .min_y = min_y,
      .max_y = max_y,
    };
  };
  const auto mark_absolute_border_band
    = [&](const std::uint32_t clip_index,
        const AbsoluteClipPageRegion& absolute_region,
        std::uint32_t remaining_budget) {
        if (!absolute_region.valid || remaining_budget == 0U) {
          return 0U;
        }

        const auto band = std::max(1,
          static_cast<std::int32_t>(kAcceptedFeedbackCurrentFrameGuardPages));
        std::uint32_t added = 0U;
        auto add_strip = [&](const AbsoluteClipPageRegion& strip) {
          if (added >= remaining_budget) {
            return;
          }
          added += mark_absolute_region_limited(
            clip_index, strip, remaining_budget - added);
        };

        add_strip(AbsoluteClipPageRegion {
          .valid = true,
          .min_x = absolute_region.min_x,
          .max_x = absolute_region.max_x,
          .min_y = absolute_region.min_y,
          .max_y
          = std::min(absolute_region.max_y, absolute_region.min_y + band - 1),
        });
        add_strip(AbsoluteClipPageRegion {
          .valid = true,
          .min_x = absolute_region.min_x,
          .max_x = absolute_region.max_x,
          .min_y
          = std::max(absolute_region.min_y, absolute_region.max_y - band + 1),
          .max_y = absolute_region.max_y,
        });
        add_strip(AbsoluteClipPageRegion {
          .valid = true,
          .min_x = absolute_region.min_x,
          .max_x
          = std::min(absolute_region.max_x, absolute_region.min_x + band - 1),
          .min_y = absolute_region.min_y,
          .max_y = absolute_region.max_y,
        });
        add_strip(AbsoluteClipPageRegion {
          .valid = true,
          .min_x
          = std::max(absolute_region.min_x, absolute_region.max_x - band + 1),
          .max_x = absolute_region.max_x,
          .min_y = absolute_region.min_y,
          .max_y = absolute_region.max_y,
        });
        return added;
      };
  const auto mark_absolute_delta_band =
    [&](const std::uint32_t clip_index,
      const AbsoluteClipPageRegion& current_region,
      const AbsoluteClipPageRegion& previous_region) {
      if (!current_region.valid) {
        return 0U;
      }

      const auto expanded_current = expand_absolute_region(
        current_region, kAcceptedFeedbackCurrentFrameGuardPages);
      constexpr auto kBudget
        = kAcceptedFeedbackCurrentFrameMaxDeltaPagesPerClip;
      auto overlap
        = intersect_absolute_region(expanded_current, previous_region);
      if (!overlap.valid) {
        return mark_absolute_border_band(clip_index, expanded_current, kBudget);
      }

      std::uint32_t added = 0U;
      auto add_strip = [&](const AbsoluteClipPageRegion& strip) {
        if (!strip.valid || added >= kBudget) {
          return;
        }
        added
          += mark_absolute_region_limited(clip_index, strip, kBudget - added);
      };

      if (expanded_current.min_x < overlap.min_x) {
        add_strip(AbsoluteClipPageRegion {
          .valid = true,
          .min_x = expanded_current.min_x,
          .max_x = overlap.min_x - 1,
          .min_y = expanded_current.min_y,
          .max_y = expanded_current.max_y,
        });
      }
      if (expanded_current.max_x > overlap.max_x) {
        add_strip(AbsoluteClipPageRegion {
          .valid = true,
          .min_x = overlap.max_x + 1,
          .max_x = expanded_current.max_x,
          .min_y = expanded_current.min_y,
          .max_y = expanded_current.max_y,
        });
      }

      const auto interior_min_x
        = std::max(expanded_current.min_x, overlap.min_x);
      const auto interior_max_x
        = std::min(expanded_current.max_x, overlap.max_x);
      if (interior_min_x <= interior_max_x
        && expanded_current.min_y < overlap.min_y) {
        add_strip(AbsoluteClipPageRegion {
          .valid = true,
          .min_x = interior_min_x,
          .max_x = interior_max_x,
          .min_y = expanded_current.min_y,
          .max_y = overlap.min_y - 1,
        });
      }
      if (interior_min_x <= interior_max_x
        && expanded_current.max_y > overlap.max_y) {
        add_strip(AbsoluteClipPageRegion {
          .valid = true,
          .min_x = interior_min_x,
          .max_x = interior_max_x,
          .min_y = overlap.max_y + 1,
          .max_y = expanded_current.max_y,
        });
      }
      return added;
    };

  std::array<glm::vec3, 8> frustum_light_space_points {};
  for (std::size_t i = 0U; i < clip_corners.size(); ++i) {
    const float far_corner_depth = std::max(1.0e-4F, -view_far_corners[i].z);
    const glm::vec3 view_ray = view_far_corners[i] / far_corner_depth;
    const glm::vec3 clamped_near_vs = view_ray * effective_near_depth;
    const glm::vec3 clamped_far_vs = view_ray * effective_far_depth;
    const auto near_world = TransformPoint(inv_view, clamped_near_vs);
    const auto far_world = TransformPoint(inv_view, clamped_far_vs);
    frustum_light_space_points[i]
      = glm::vec3(light_view * glm::vec4(near_world, 1.0F));
    frustum_light_space_points[i + clip_corners.size()]
      = glm::vec3(light_view * glm::vec4(far_world, 1.0F));
  }

  std::vector<ClipSelectedRegion> frustum_regions(clip_level_count);
  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    float min_page_x = std::numeric_limits<float>::max();
    float max_page_x = std::numeric_limits<float>::lowest();
    float min_page_y = std::numeric_limits<float>::max();
    float max_page_y = std::numeric_limits<float>::lowest();

    for (const auto& point_ls : frustum_light_space_points) {
      const float page_x = (point_ls.x - clip_origin_x[clip_index])
        / clip_page_world[clip_index];
      const float page_y = (point_ls.y - clip_origin_y[clip_index])
        / clip_page_world[clip_index];
      min_page_x = std::min(min_page_x, page_x);
      max_page_x = std::max(max_page_x, page_x);
      min_page_y = std::min(min_page_y, page_y);
      max_page_y = std::max(max_page_y, page_y);
    }

    if (max_page_x < 0.0F || max_page_y < 0.0F
      || min_page_x >= static_cast<float>(pages_per_axis)
      || min_page_y >= static_cast<float>(pages_per_axis)) {
      continue;
    }

    auto& region = frustum_regions[clip_index];
    region.valid = true;
    region.min_x
      = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_page_x)));
    region.max_x = static_cast<std::uint32_t>(
      std::min(static_cast<float>(pages_per_axis - 1U),
        std::max(0.0F, std::ceil(max_page_x) - 1.0F)));
    region.min_y
      = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_page_y)));
    region.max_y = static_cast<std::uint32_t>(
      std::min(static_cast<float>(pages_per_axis - 1U),
        std::max(0.0F, std::ceil(max_page_y) - 1.0F)));
  }
  state.absolute_frustum_regions.resize(clip_level_count);
  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    const auto& region = frustum_regions[clip_index];
    if (!region.valid) {
      state.absolute_frustum_regions[clip_index] = {};
      continue;
    }

    state.absolute_frustum_regions[clip_index] = AbsoluteClipPageRegion {
      .valid = true,
      .min_x = clip_grid_origin_x[clip_index]
        + static_cast<std::int32_t>(region.min_x),
      .max_x = clip_grid_origin_x[clip_index]
        + static_cast<std::int32_t>(region.max_x),
      .min_y = clip_grid_origin_y[clip_index]
        + static_cast<std::int32_t>(region.min_y),
      .max_y = clip_grid_origin_y[clip_index]
        + static_cast<std::int32_t>(region.max_y),
    };
  }

  const std::uint32_t coarse_backbone_begin
    = clip_level_count > kDirectionalCoarseBackboneClipCount
    ? clip_level_count - kDirectionalCoarseBackboneClipCount
    : 0U;
  const auto current_feedback_address_space_hash
    = shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(metadata);
  const auto feedback_it = request_feedback_.find(view_id);
  auto feedback_decision = ViewCacheEntry::RequestFeedbackDecision::kNoFeedback;
  std::uint32_t feedback_key_count = 0U;
  std::uint64_t feedback_age_frames = 0U;
  if (feedback_it != request_feedback_.end()) {
    const auto& feedback = feedback_it->second.feedback;
    feedback_key_count
      = static_cast<std::uint32_t>(feedback.requested_resident_keys.size());
    if (frame_sequence_ > feedback.source_frame_sequence) {
      feedback_age_frames
        = (frame_sequence_ - feedback.source_frame_sequence).get();
    }

    if (feedback.requested_resident_keys.empty()) {
      feedback_decision
        = ViewCacheEntry::RequestFeedbackDecision::kEmptyFeedback;
    } else if (feedback.pages_per_axis != pages_per_axis
      || feedback.clip_level_count != clip_level_count) {
      feedback_decision
        = ViewCacheEntry::RequestFeedbackDecision::kDimensionMismatch;
    } else if (feedback.directional_address_space_hash
      != current_feedback_address_space_hash) {
      feedback_decision
        = ViewCacheEntry::RequestFeedbackDecision::kAddressSpaceMismatch;
    } else if (frame_sequence_ <= feedback.source_frame_sequence) {
      feedback_decision = ViewCacheEntry::RequestFeedbackDecision::kSameFrame;
    } else if (feedback_age_frames > kMaxRequestFeedbackAgeFrames) {
      feedback_decision = ViewCacheEntry::RequestFeedbackDecision::kStale;
    } else {
      feedback_decision = ViewCacheEntry::RequestFeedbackDecision::kAccepted;
    }
  }
  const auto use_request_feedback
    = feedback_decision == ViewCacheEntry::RequestFeedbackDecision::kAccepted;
  std::uint32_t coarse_backbone_pages = 0U;
  std::uint32_t feedback_requested_pages = 0U;
  std::uint32_t feedback_refinement_pages = 0U;
  std::uint32_t receiver_bootstrap_pages = 0U;
  std::uint32_t current_frame_reinforcement_pages = 0U;
  std::uint64_t current_frame_reinforcement_reference_frame = 0U;

  for (std::uint32_t clip_index = coarse_backbone_begin;
    clip_index < clip_level_count; ++clip_index) {
    auto& region = frustum_regions[clip_index];
    if (!region.valid) {
      continue;
    }
    region.min_x = region.min_x > kCoarseBackboneGuardPages
      ? region.min_x - kCoarseBackboneGuardPages
      : 0U;
    region.min_y = region.min_y > kCoarseBackboneGuardPages
      ? region.min_y - kCoarseBackboneGuardPages
      : 0U;
    region.max_x
      = std::min(pages_per_axis - 1U, region.max_x + kCoarseBackboneGuardPages);
    region.max_y
      = std::min(pages_per_axis - 1U, region.max_y + kCoarseBackboneGuardPages);
  }

  for (std::uint32_t clip_index = clip_level_count;
    clip_index-- > coarse_backbone_begin;) {
    const auto& region = frustum_regions[clip_index];
    if (!region.valid) {
      continue;
    }

    coarse_backbone_pages += mark_region(clip_index, region);
  }

  feedback_requested_pages = 0U;
  feedback_refinement_pages = 0U;
  const bool use_receiver_bootstrap
    = !use_request_feedback && !visible_receiver_bounds.empty();
  const bool use_current_frame_reinforcement = use_request_feedback
    && !visible_receiver_bounds.empty() && coarse_backbone_begin > 0U;
  if (use_request_feedback || use_receiver_bootstrap
    || use_current_frame_reinforcement) {
    const auto mark_receiver_refinement = [&](
                                            const glm::vec3 receiver_center_ls,
                                            const float receiver_radius,
                                            const std::uint32_t guard_radius,
                                            const std::uint32_t end_clip) {
      std::uint32_t added = 0U;
      const auto clipped_end = std::min(coarse_backbone_begin, end_clip);
      for (std::uint32_t clip_index = 0U; clip_index < clipped_end;
        ++clip_index) {
        const float min_page_x
          = (receiver_center_ls.x - receiver_radius - clip_origin_x[clip_index])
          / clip_page_world[clip_index];
        const float max_page_x
          = (receiver_center_ls.x + receiver_radius - clip_origin_x[clip_index])
          / clip_page_world[clip_index];
        const float min_page_y
          = (receiver_center_ls.y - receiver_radius - clip_origin_y[clip_index])
          / clip_page_world[clip_index];
        const float max_page_y
          = (receiver_center_ls.y + receiver_radius - clip_origin_y[clip_index])
          / clip_page_world[clip_index];
        if (max_page_x < 0.0F || max_page_y < 0.0F
          || min_page_x >= static_cast<float>(pages_per_axis)
          || min_page_y >= static_cast<float>(pages_per_axis)) {
          continue;
        }

        ClipSelectedRegion region {
          .valid = true,
          .min_x
          = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_page_x))),
          .max_x = static_cast<std::uint32_t>(
            std::min(static_cast<float>(pages_per_axis - 1U),
              std::max(0.0F, std::ceil(max_page_x) - 1.0F))),
          .min_y
          = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_page_y))),
          .max_y = static_cast<std::uint32_t>(
            std::min(static_cast<float>(pages_per_axis - 1U),
              std::max(0.0F, std::ceil(max_page_y) - 1.0F))),
        };
        region.min_x
          = region.min_x > guard_radius ? region.min_x - guard_radius : 0U;
        region.min_y
          = region.min_y > guard_radius ? region.min_y - guard_radius : 0U;
        region.max_x
          = std::min(pages_per_axis - 1U, region.max_x + guard_radius);
        region.max_y
          = std::min(pages_per_axis - 1U, region.max_y + guard_radius);
        added += mark_region(clip_index, region);
      }
      return added;
    };

    if (use_request_feedback) {
      std::vector<std::uint8_t> feedback_seed_pages(
        static_cast<std::size_t>(coarse_backbone_begin) * pages_per_level, 0U);
      for (const auto resident_key :
        feedback_it->second.feedback.requested_resident_keys) {
        const auto clip_index
          = shadow_detail::VirtualResidentPageKeyClipLevel(resident_key);
        if (clip_index >= coarse_backbone_begin) {
          continue;
        }

        const auto local_page_x
          = shadow_detail::VirtualResidentPageKeyGridX(resident_key)
          - clip_grid_origin_x[clip_index];
        const auto local_page_y
          = shadow_detail::VirtualResidentPageKeyGridY(resident_key)
          - clip_grid_origin_y[clip_index];
        if (local_page_x < 0 || local_page_y < 0
          || local_page_x >= static_cast<std::int32_t>(pages_per_axis)
          || local_page_y >= static_cast<std::int32_t>(pages_per_axis)) {
          continue;
        }

        const auto local_page_index
          = static_cast<std::uint32_t>(local_page_y) * pages_per_axis
          + static_cast<std::uint32_t>(local_page_x);
        const auto fine_feedback_page_index
          = static_cast<std::size_t>(clip_index) * pages_per_level
          + local_page_index;
        if (fine_feedback_page_index >= feedback_seed_pages.size()
          || feedback_seed_pages[fine_feedback_page_index] != 0U) {
          continue;
        }
        feedback_seed_pages[fine_feedback_page_index] = 1U;
        ++feedback_requested_pages;
      }

      const auto refinement_guard_radius
        = static_cast<std::uint32_t>(kFeedbackRequestGuardRadius);
      for (std::uint32_t clip_index = 0U; clip_index < coarse_backbone_begin;
        ++clip_index) {
        const auto clip_seed_offset
          = static_cast<std::size_t>(clip_index) * pages_per_level;
        for (std::uint32_t local_page_index = 0U;
          local_page_index < pages_per_level; ++local_page_index) {
          if (feedback_seed_pages[clip_seed_offset + local_page_index] == 0U) {
            continue;
          }

          const auto page_y = local_page_index / pages_per_axis;
          const auto page_x = local_page_index % pages_per_axis;
          feedback_refinement_pages += mark_dilated_page(
            clip_index, page_x, page_y, refinement_guard_radius);
        }
      }
    }

    // Once fine-page feedback is available, trust that sparse signal instead of
    // reseeding the fine clips every frame from coarse object bounds. The
    // coarse backbone still covers the current view, while repeated receiver
    // bootstrap was driving allocator churn and page thrash under camera
    // motion.
    if (use_receiver_bootstrap) {
      const auto refinement_guard_radius = static_cast<std::uint32_t>(1U);
      for (const auto& receiver_bound : visible_receiver_bounds) {
        const glm::vec3 receiver_center_ls
          = glm::vec3(light_view * glm::vec4(glm::vec3(receiver_bound), 1.0F));
        const float receiver_radius = std::max(0.0F, receiver_bound.w);
        receiver_bootstrap_pages += mark_receiver_refinement(receiver_center_ls,
          receiver_radius, refinement_guard_radius, coarse_backbone_begin);
      }
    }

    if (use_current_frame_reinforcement) {
      const auto reinforcement_end_clip = std::min(coarse_backbone_begin,
        kAcceptedFeedbackCurrentFrameReinforcementClipCount);
      current_frame_reinforcement_reference_frame
        = feedback_it->second.feedback.source_frame_sequence.get();
      for (std::uint32_t clip_index = 0U; clip_index < reinforcement_end_clip;
        ++clip_index) {
        const auto source_region = clip_index
            < feedback_it->second.source_absolute_frustum_regions.size()
          ? feedback_it->second.source_absolute_frustum_regions[clip_index]
          : AbsoluteClipPageRegion {};
        current_frame_reinforcement_pages
          += mark_absolute_delta_band(clip_index,
            state.absolute_frustum_regions[clip_index], source_region);
      }
    }
  }

  std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
    reusable_clip_contents {};
  reusable_clip_contents.fill(false);
  bool address_space_compatible = false;
  std::unordered_set<std::uint64_t> dirty_resident_pages {};
  bool global_dirty_resident_contents = false;
  const auto* previous_resident_pages
    = previous_state != nullptr ? &previous_state->resident_pages : nullptr;
  const auto* previous_shadow_caster_bounds = previous_state != nullptr
    ? &previous_state->shadow_caster_bounds
    : nullptr;
  const auto* previous_key
    = previous_state != nullptr ? &previous_state->key : nullptr;
  const auto* previous_page_table_entries
    = previous_state != nullptr ? &previous_state->page_table_entries : nullptr;
  const engine::DirectionalVirtualShadowMetadata* previous_metadata = nullptr;
  bool previous_pending_resolved_pages_empty = previous_state == nullptr
    || previous_state->resolved_raster_pages.empty();
  if (previous_state != nullptr
    && previous_state->pending_residency_resolve.valid
    && !previous_state->pending_residency_resolve.previous_resident_pages
      .empty()
    && previous_state->pending_residency_resolve.resident_reuse_snapshot
      .valid) {
    const auto& previous_pending = previous_state->pending_residency_resolve;
    previous_resident_pages = &previous_pending.previous_resident_pages;
    previous_shadow_caster_bounds
      = &previous_pending.previous_shadow_caster_bounds;
    previous_key = &previous_pending.resident_reuse_snapshot.key;
    previous_page_table_entries
      = &previous_pending.resident_reuse_snapshot.page_table_entries;
    previous_pending_resolved_pages_empty
      = previous_pending.resident_reuse_snapshot
          .previous_pending_resolved_pages_empty;
    if (previous_pending.resident_reuse_snapshot.directional_virtual_metadata
          .size()
      == 1U) {
      previous_metadata = &previous_pending.resident_reuse_snapshot
                             .directional_virtual_metadata.front();
    }
  } else if (previous_state != nullptr
    && previous_state->directional_virtual_metadata.size() == 1U) {
    previous_metadata = &previous_state->directional_virtual_metadata.front();
  }

  if (previous_metadata != nullptr) {
    address_space_compatible = IsDirectionalVirtualAddressSpaceCompatible(
      *previous_metadata, metadata);
    if (address_space_compatible) {
      for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
        ++clip_index) {
        reusable_clip_contents[clip_index]
          = IsDirectionalVirtualClipContentReusable(
            *previous_metadata, metadata, clip_index);
      }

      const bool shadow_content_hash_changed = previous_key != nullptr
        && previous_key->shadow_content_hash != state.key.shadow_content_hash;
      const bool caster_bounds_changed = previous_key != nullptr
        && previous_key->caster_hash != state.key.caster_hash;
      bool found_spatial_delta = false;
      if (previous_shadow_caster_bounds != nullptr
        && previous_shadow_caster_bounds->size()
          == shadow_caster_bounds.size()) {
        for (std::size_t i = 0U; i < shadow_caster_bounds.size(); ++i) {
          const auto& previous_bound = (*previous_shadow_caster_bounds)[i];
          const auto& current_bound = shadow_caster_bounds[i];
          const bool bounds_equal = previous_bound.x == current_bound.x
            && previous_bound.y == current_bound.y
            && previous_bound.z == current_bound.z
            && previous_bound.w == current_bound.w;
          if (bounds_equal) {
            continue;
          }

          found_spatial_delta = true;
          // Previous bound must be projected with the previous frame's
          // light_view so the generated lattice keys match the keys stored in
          // the resident page map. Using the current light_view would produce
          // wrong grid coordinates when the light eye has shifted.
          AppendDirtyResidentKeysForBound(previous_bound,
            previous_metadata->light_view, clip_page_world, clip_level_count,
            dirty_resident_pages);
          AppendDirtyResidentKeysForBound(current_bound, light_view,
            clip_page_world, clip_level_count, dirty_resident_pages);
        }
      } else if (shadow_content_hash_changed || caster_bounds_changed) {
        global_dirty_resident_contents = true;
      }

      if (!global_dirty_resident_contents
        && (shadow_content_hash_changed || caster_bounds_changed)
        && !found_spatial_delta) {
        global_dirty_resident_contents = true;
      }
    }
  }

  state.pending_residency_resolve = {};
  auto& pending_resolve = state.pending_residency_resolve;
  pending_resolve.valid = true;
  pending_resolve.dirty = true;
  pending_resolve.clip_level_count = clip_level_count;
  pending_resolve.pages_per_axis = pages_per_axis;
  pending_resolve.pages_per_level = pages_per_level;
  pending_resolve.view_constants = view_constants;
  pending_resolve.light_view = light_view;
  pending_resolve.light_eye = light_eye;
  pending_resolve.near_plane = near_plane;
  pending_resolve.far_plane = far_plane;
  pending_resolve.clip_page_world = clip_page_world;
  pending_resolve.clip_origin_x = clip_origin_x;
  pending_resolve.clip_origin_y = clip_origin_y;
  pending_resolve.clip_grid_origin_x = clip_grid_origin_x;
  pending_resolve.clip_grid_origin_y = clip_grid_origin_y;
  pending_resolve.reusable_clip_contents = reusable_clip_contents;
  pending_resolve.address_space_compatible = address_space_compatible;
  pending_resolve.global_dirty_resident_contents
    = global_dirty_resident_contents;
  pending_resolve.selected_pages = std::move(selected_pages);
  if (previous_resident_pages != nullptr) {
    pending_resolve.previous_resident_pages = *previous_resident_pages;
  }
  if (previous_shadow_caster_bounds != nullptr) {
    pending_resolve.previous_shadow_caster_bounds
      = *previous_shadow_caster_bounds;
  }
  pending_resolve.dirty_resident_pages = std::move(dirty_resident_pages);
  pending_resolve.resident_reuse_snapshot.valid = previous_key != nullptr;
  if (previous_key != nullptr) {
    pending_resolve.resident_reuse_snapshot
      .previous_pending_resolved_pages_empty
      = previous_pending_resolved_pages_empty;
    pending_resolve.resident_reuse_snapshot.key = *previous_key;
    if (previous_metadata != nullptr) {
      pending_resolve.resident_reuse_snapshot.directional_virtual_metadata
        = { *previous_metadata };
    }
    if (previous_page_table_entries != nullptr) {
      pending_resolve.resident_reuse_snapshot.page_table_entries
        = *previous_page_table_entries;
    }
  }
  state.resolved_raster_pages.clear();

  state.publish_diagnostics.feedback_decision = feedback_decision;
  state.publish_diagnostics.feedback_key_count = feedback_key_count;
  state.publish_diagnostics.feedback_age_frames = feedback_age_frames;
  state.publish_diagnostics.address_space_compatible = address_space_compatible;
  state.publish_diagnostics.global_dirty_resident_contents
    = global_dirty_resident_contents;
  state.publish_diagnostics.shadow_caster_bound_count
    = static_cast<std::uint32_t>(shadow_caster_bounds.size());
  state.publish_diagnostics.visible_receiver_bound_count
    = static_cast<std::uint32_t>(visible_receiver_bounds.size());
  state.publish_diagnostics.clip_level_count = clip_level_count;
  state.publish_diagnostics.coarse_backbone_begin = coarse_backbone_begin;
  state.publish_diagnostics.selected_page_count = selected_page_count;
  state.publish_diagnostics.coarse_backbone_pages = coarse_backbone_pages;
  state.publish_diagnostics.feedback_requested_pages = feedback_requested_pages;
  state.publish_diagnostics.feedback_refinement_pages
    = feedback_refinement_pages;
  state.publish_diagnostics.receiver_bootstrap_pages = receiver_bootstrap_pages;
  state.publish_diagnostics.current_frame_reinforcement_pages
    = current_frame_reinforcement_pages;
  state.publish_diagnostics.current_frame_reinforcement_reference_frame
    = current_frame_reinforcement_reference_frame;
  state.publish_diagnostics.previous_resident_pages
    = previous_resident_pages != nullptr
    ? static_cast<std::uint32_t>(previous_resident_pages->size())
    : 0U;
  state.publish_diagnostics.carried_resident_pages = 0U;
  state.publish_diagnostics.released_resident_pages = 0U;
  state.publish_diagnostics.dirty_resident_page_count
    = static_cast<std::uint32_t>(pending_resolve.dirty_resident_pages.size());
  state.publish_diagnostics.marked_dirty_pages = 0U;
  state.publish_diagnostics.reused_requested_pages = 0U;
  state.publish_diagnostics.allocated_pages = 0U;
  state.publish_diagnostics.evicted_pages = 0U;
  state.publish_diagnostics.allocation_failures = 0U;
  state.publish_diagnostics.rerasterized_pages = 0U;

  state.directional_virtual_metadata.push_back(metadata);
}

auto VirtualShadowMapBackend::PublishShadowInstances(
  const std::span<const engine::ShadowInstanceMetadata> instances)
  -> ShaderVisibleIndex
{
  if (instances.empty()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto result = shadow_instance_buffer_.Allocate(
    static_cast<std::uint32_t>(instances.size()));
  if (!result) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate shadow instance metadata "
      "buffer: {}",
      result.error().message());
    return kInvalidShaderVisibleIndex;
  }

  const auto& allocation = *result;
  if (allocation.mapped_ptr != nullptr) {
    std::memcpy(allocation.mapped_ptr, instances.data(),
      instances.size() * sizeof(engine::ShadowInstanceMetadata));
  }
  return allocation.srv;
}

auto VirtualShadowMapBackend::PublishDirectionalVirtualMetadata(
  const std::span<const engine::DirectionalVirtualShadowMetadata> metadata)
  -> ShaderVisibleIndex
{
  if (metadata.empty()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto result = directional_virtual_metadata_buffer_.Allocate(
    static_cast<std::uint32_t>(metadata.size()));
  if (!result) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate directional virtual shadow "
      "metadata buffer: {}",
      result.error().message());
    return kInvalidShaderVisibleIndex;
  }

  const auto& allocation = *result;
  if (allocation.mapped_ptr != nullptr) {
    std::memcpy(allocation.mapped_ptr, metadata.data(),
      metadata.size() * sizeof(engine::DirectionalVirtualShadowMetadata));
  }
  return allocation.srv;
}

auto VirtualShadowMapBackend::EnsureViewPageTableResources(const ViewId view_id,
  const std::uint32_t required_entry_count) -> ViewPageTableResources*
{
  if (required_entry_count == 0U) {
    return nullptr;
  }

  auto [it, _] = view_page_table_resources_.try_emplace(view_id);
  auto& resources = it->second;
  if (resources.gpu_buffer && resources.upload_buffer
    && resources.mapped_upload != nullptr
    && required_entry_count <= resources.entry_capacity) {
    return &resources;
  }

  if (required_entry_count > kMaxPersistentPageTableEntries) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: view {} requested {} page-table entries but "
      "the persistent capacity is only {}",
      view_id.get(), required_entry_count, kMaxPersistentPageTableEntries);
    return nullptr;
  }

  if (resources.upload_buffer && resources.mapped_upload != nullptr) {
    resources.upload_buffer->UnMap();
    resources.mapped_upload = nullptr;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();
  const auto size_bytes
    = static_cast<std::uint64_t>(kMaxPersistentPageTableEntries)
    * sizeof(std::uint32_t);

  const graphics::BufferDesc gpu_desc {
    .size_bytes = size_bytes,
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowMapBackend.PersistentPageTable",
  };
  resources.gpu_buffer = gfx_->CreateBuffer(gpu_desc);
  if (!resources.gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create persistent page table "
      "buffer for view {}",
      view_id.get());
    return nullptr;
  }
  registry.Register(resources.gpu_buffer);

  auto srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate page-table SRV for view {}",
      view_id.get());
    return nullptr;
  }
  resources.srv = allocator.GetShaderVisibleIndex(srv_handle);

  graphics::BufferViewDescription srv_desc;
  srv_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  srv_desc.range = { 0U, size_bytes };
  srv_desc.stride = sizeof(std::uint32_t);
  registry.RegisterView(*resources.gpu_buffer, std::move(srv_handle), srv_desc);

  auto uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!uav_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate page-table UAV for view {}",
      view_id.get());
    return nullptr;
  }
  resources.uav = allocator.GetShaderVisibleIndex(uav_handle);

  graphics::BufferViewDescription uav_desc;
  uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
  uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  uav_desc.range = { 0U, size_bytes };
  uav_desc.stride = sizeof(std::uint32_t);
  registry.RegisterView(*resources.gpu_buffer, std::move(uav_handle), uav_desc);

  const graphics::BufferDesc upload_desc {
    .size_bytes = size_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowMapBackend.PersistentPageTableUpload",
  };
  resources.upload_buffer = gfx_->CreateBuffer(upload_desc);
  if (!resources.upload_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create page-table upload buffer "
      "for view {}",
      view_id.get());
    return nullptr;
  }

  resources.mapped_upload
    = static_cast<std::uint32_t*>(resources.upload_buffer->Map(0U, size_bytes));
  if (resources.mapped_upload == nullptr) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to map page-table upload buffer for "
      "view {}",
      view_id.get());
    resources.upload_buffer.reset();
    return nullptr;
  }
  std::memset(resources.mapped_upload, 0, static_cast<std::size_t>(size_bytes));
  resources.entry_capacity = kMaxPersistentPageTableEntries;
  return &resources;
}

auto VirtualShadowMapBackend::StagePageTableUpload(const ViewId view_id,
  const std::span<const std::uint32_t> entries) -> ShaderVisibleIndex
{
  if (entries.empty()) {
    return kInvalidShaderVisibleIndex;
  }

  auto* resources = EnsureViewPageTableResources(
    view_id, static_cast<std::uint32_t>(entries.size()));
  if (resources == nullptr || resources->mapped_upload == nullptr) {
    return kInvalidShaderVisibleIndex;
  }

  return resources->srv;
}

auto VirtualShadowMapBackend::EnsurePageTablePublication(const ViewId view_id,
  const std::uint32_t required_entry_count) -> ShaderVisibleIndex
{
  if (required_entry_count == 0U) {
    return kInvalidShaderVisibleIndex;
  }

  auto* resources = EnsureViewPageTableResources(view_id, required_entry_count);
  return resources != nullptr ? resources->srv : kInvalidShaderVisibleIndex;
}

auto VirtualShadowMapBackend::EnsureViewResolveResources(const ViewId view_id,
  const std::uint32_t required_entry_count) -> ViewResolveResources*
{
  const auto required_capacity = std::max(std::max(required_entry_count, 1U),
    physical_pool_config_.physical_tile_capacity);

  auto [it, _] = view_resolve_resources_.try_emplace(view_id);
  auto& resources = it->second;
  if (resources.resident_pages_gpu_buffer
    && resources.resident_pages_upload_buffer
    && resources.mapped_resident_pages_upload != nullptr
    && resources.stats_gpu_buffer && resources.stats_upload_buffer
    && resources.mapped_stats_upload != nullptr
    && required_capacity <= resources.resident_page_capacity) {
    return &resources;
  }

  if (resources.resident_pages_upload_buffer
    && resources.mapped_resident_pages_upload != nullptr) {
    resources.resident_pages_upload_buffer->UnMap();
    resources.mapped_resident_pages_upload = nullptr;
  }
  if (resources.stats_upload_buffer
    && resources.mapped_stats_upload != nullptr) {
    resources.stats_upload_buffer->UnMap();
    resources.mapped_stats_upload = nullptr;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  const auto resident_bytes = static_cast<std::uint64_t>(required_capacity)
    * sizeof(renderer::VirtualShadowResolveResidentPageEntry);
  const graphics::BufferDesc resident_gpu_desc {
    .size_bytes = resident_bytes,
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowMapBackend.ResolveResidentPages",
  };
  resources.resident_pages_gpu_buffer = gfx_->CreateBuffer(resident_gpu_desc);
  if (!resources.resident_pages_gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create resolve resident-page "
      "buffer for view {}",
      view_id.get());
    return nullptr;
  }
  registry.Register(resources.resident_pages_gpu_buffer);

  auto resident_srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!resident_srv_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate resolve resident-page SRV "
      "for view {}",
      view_id.get());
    return nullptr;
  }
  resources.resident_pages_srv
    = allocator.GetShaderVisibleIndex(resident_srv_handle);

  graphics::BufferViewDescription resident_srv_desc;
  resident_srv_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_SRV;
  resident_srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  resident_srv_desc.range = { 0U, resident_bytes };
  resident_srv_desc.stride
    = sizeof(renderer::VirtualShadowResolveResidentPageEntry);
  registry.RegisterView(*resources.resident_pages_gpu_buffer,
    std::move(resident_srv_handle), resident_srv_desc);

  auto resident_uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!resident_uav_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate resolve resident-page UAV "
      "for view {}",
      view_id.get());
    return nullptr;
  }
  resources.resident_pages_uav
    = allocator.GetShaderVisibleIndex(resident_uav_handle);

  graphics::BufferViewDescription resident_uav_desc;
  resident_uav_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_UAV;
  resident_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  resident_uav_desc.range = { 0U, resident_bytes };
  resident_uav_desc.stride
    = sizeof(renderer::VirtualShadowResolveResidentPageEntry);
  registry.RegisterView(*resources.resident_pages_gpu_buffer,
    std::move(resident_uav_handle), resident_uav_desc);

  const graphics::BufferDesc resident_upload_desc {
    .size_bytes = resident_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowMapBackend.ResolveResidentPagesUpload",
  };
  resources.resident_pages_upload_buffer
    = gfx_->CreateBuffer(resident_upload_desc);
  if (!resources.resident_pages_upload_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create resolve resident-page "
      "upload buffer for view {}",
      view_id.get());
    return nullptr;
  }

  resources.mapped_resident_pages_upload
    = static_cast<renderer::VirtualShadowResolveResidentPageEntry*>(
      resources.resident_pages_upload_buffer->Map(0U, resident_bytes));
  if (resources.mapped_resident_pages_upload == nullptr) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to map resolve resident-page upload "
      "buffer for view {}",
      view_id.get());
    resources.resident_pages_upload_buffer.reset();
    return nullptr;
  }
  std::memset(resources.mapped_resident_pages_upload, 0,
    static_cast<std::size_t>(resident_bytes));

  const graphics::BufferDesc stats_gpu_desc {
    .size_bytes = sizeof(renderer::VirtualShadowResolveStats),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowMapBackend.ResolveStats",
  };
  resources.stats_gpu_buffer = gfx_->CreateBuffer(stats_gpu_desc);
  if (!resources.stats_gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create resolve stats buffer for "
      "view {}",
      view_id.get());
    return nullptr;
  }
  registry.Register(resources.stats_gpu_buffer);

  auto stats_srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!stats_srv_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate resolve stats SRV for "
      "view {}",
      view_id.get());
    return nullptr;
  }
  resources.stats_srv = allocator.GetShaderVisibleIndex(stats_srv_handle);

  graphics::BufferViewDescription stats_srv_desc;
  stats_srv_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  stats_srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  stats_srv_desc.range = { 0U, sizeof(renderer::VirtualShadowResolveStats) };
  stats_srv_desc.stride = sizeof(renderer::VirtualShadowResolveStats);
  registry.RegisterView(
    *resources.stats_gpu_buffer, std::move(stats_srv_handle), stats_srv_desc);

  auto stats_uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!stats_uav_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate resolve stats UAV for "
      "view {}",
      view_id.get());
    return nullptr;
  }
  resources.stats_uav = allocator.GetShaderVisibleIndex(stats_uav_handle);

  graphics::BufferViewDescription stats_uav_desc;
  stats_uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
  stats_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  stats_uav_desc.range = { 0U, sizeof(renderer::VirtualShadowResolveStats) };
  stats_uav_desc.stride = sizeof(renderer::VirtualShadowResolveStats);
  registry.RegisterView(
    *resources.stats_gpu_buffer, std::move(stats_uav_handle), stats_uav_desc);

  const graphics::BufferDesc stats_upload_desc {
    .size_bytes = sizeof(renderer::VirtualShadowResolveStats),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowMapBackend.ResolveStatsUpload",
  };
  resources.stats_upload_buffer = gfx_->CreateBuffer(stats_upload_desc);
  if (!resources.stats_upload_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create resolve stats upload "
      "buffer for view {}",
      view_id.get());
    return nullptr;
  }

  resources.mapped_stats_upload
    = static_cast<renderer::VirtualShadowResolveStats*>(
      resources.stats_upload_buffer->Map(
        0U, sizeof(renderer::VirtualShadowResolveStats)));
  if (resources.mapped_stats_upload == nullptr) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to map resolve stats upload buffer "
      "for view {}",
      view_id.get());
    resources.stats_upload_buffer.reset();
    return nullptr;
  }
  std::memset(resources.mapped_stats_upload, 0,
    sizeof(renderer::VirtualShadowResolveStats));

  resources.resident_page_capacity = required_capacity;
  return &resources;
}

auto VirtualShadowMapBackend::StageResolveStateUpload(
  const ViewId view_id, const ViewCacheEntry& state) -> void
{
  auto* resources = EnsureViewResolveResources(
    view_id, state.resolve_stats.resident_entry_count);
  if (resources == nullptr) {
    return;
  }

  resources->resident_page_upload_count
    = state.resolve_stats.resident_entry_count;
  resources->resident_page_upload_pending = true;

  resources->stats_upload_pending = resources->mapped_stats_upload != nullptr;
}

auto VirtualShadowMapBackend::LogPublishTransition(
  const ViewId view_id, const ViewCacheEntry& state) -> void
{
  auto& log_state = publish_log_states_[view_id];
  const auto& diagnostics = state.publish_diagnostics;
  const auto feedback_reason = [&diagnostics]() -> const char* {
    using Decision = ViewCacheEntry::RequestFeedbackDecision;
    switch (diagnostics.feedback_decision) {
    case Decision::kNoFeedback:
      return "none";
    case Decision::kEmptyFeedback:
      return "empty";
    case Decision::kDimensionMismatch:
      return "dimension-mismatch";
    case Decision::kAddressSpaceMismatch:
      return "address-space-mismatch";
    case Decision::kSameFrame:
      return "same-frame";
    case Decision::kStale:
      return "stale";
    case Decision::kAccepted:
      return "accepted";
    default:
      return "unknown";
    }
  }();
  const bool used_request_feedback = diagnostics.feedback_decision
    == ViewCacheEntry::RequestFeedbackDecision::kAccepted;
  const auto selected_page_count = diagnostics.selected_page_count;
  const auto pending_raster_page_count
    = static_cast<std::uint32_t>(state.resolved_raster_pages.size());

  LOG_F(INFO,
    "VirtualShadowMapBackend: frame={} view={} request={} feedback_keys={} "
    "feedback_age={} casters={} receivers={} clips={} coarse_begin={} "
    "selected={} coarse={} feedback_seed={} "
    "feedback_refine={} receiver_bootstrap={} current_reinforce={} "
    "resolve_pages={} resolve_age={} resolve_pruned={} resolve_used={} "
    "pending_raster_pages={} reused={} allocated={} evicted={} "
    "alloc_failures={} rerasterized={} resident_reuse_gate={}",
    frame_sequence_.get(), view_id.get(), feedback_reason,
    diagnostics.feedback_key_count, diagnostics.feedback_age_frames,
    diagnostics.shadow_caster_bound_count,
    diagnostics.visible_receiver_bound_count, diagnostics.clip_level_count,
    diagnostics.coarse_backbone_begin, selected_page_count,
    diagnostics.coarse_backbone_pages, diagnostics.feedback_requested_pages,
    diagnostics.feedback_refinement_pages, diagnostics.receiver_bootstrap_pages,
    diagnostics.current_frame_reinforcement_pages,
    diagnostics.resolved_schedule_pages,
    diagnostics.resolved_schedule_age_frames,
    diagnostics.resolved_schedule_pruned_jobs,
    diagnostics.used_resolved_raster_schedule, pending_raster_page_count,
    diagnostics.reused_requested_pages, diagnostics.allocated_pages,
    diagnostics.evicted_pages, diagnostics.allocation_failures,
    diagnostics.rerasterized_pages, diagnostics.resident_reuse_gate_open);
  LOG_F(INFO,
    "VirtualShadowMapBackend: frame={} view={} address_space_compatible={} "
    "global_dirty={} previous_resident={} carried_resident={} released={} "
    "dirty_resident_keys={} marked_dirty={} mapped_pages={} resident_pages={}",
    frame_sequence_.get(), view_id.get(), diagnostics.address_space_compatible,
    diagnostics.global_dirty_resident_contents,
    diagnostics.previous_resident_pages, diagnostics.carried_resident_pages,
    diagnostics.released_resident_pages, diagnostics.dirty_resident_page_count,
    diagnostics.marked_dirty_pages, state.introspection.mapped_page_count,
    state.introspection.resident_page_count);

  log_state.last_selected_page_count = selected_page_count;
  log_state.last_pending_raster_page_count = pending_raster_page_count;
  log_state.last_used_feedback = used_request_feedback;
  log_state.initialized = true;
}

} // namespace oxygen::renderer::internal
