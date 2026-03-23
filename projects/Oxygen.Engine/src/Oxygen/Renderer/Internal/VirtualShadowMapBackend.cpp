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
#include <numeric>
#include <stdexcept>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Hash.h>
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
constexpr std::uint32_t kMaxPersistentPageTableEntries
  = 64U * 64U * oxygen::engine::kMaxVirtualDirectionalClipLevels;
constexpr std::uint64_t kMaxResolvedRasterScheduleAgeFrames
  = oxygen::frame::kFramesInFlight.get() + 1U;
[[nodiscard]] auto HasPageManagementHierarchyVisibility(
  const std::uint32_t page_flags) -> bool
{
  return oxygen::renderer::HasVirtualShadowHierarchyVisibility(page_flags);
}
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
  // Per-tier resident-tile budget caps. BuildPhysicalPoolConfig clamps the
  // selected budget against virtual_page_count and derives atlas sizing from
  // the resulting physical_tile_capacity.
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return 256U;
  case oxygen::ShadowQualityTier::kMedium:
    return 512U;
  case oxygen::ShadowQualityTier::kHigh:
    return 1024U;
  case oxygen::ShadowQualityTier::kUltra:
    return 2048U;
  default:
    return 512U;
  }
}

[[nodiscard]] auto PackPageTableEntry(const std::uint32_t tile_x,
  const std::uint32_t tile_y, const std::uint32_t fallback_lod_offset = 0U,
  const bool current_lod_valid = true, const bool any_lod_valid = true,
  const bool requested_this_frame = true) -> std::uint32_t
{
  return oxygen::renderer::PackVirtualShadowPageTableEntry(tile_x, tile_y,
    fallback_lod_offset, current_lod_valid, any_lod_valid,
    requested_this_frame);
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

[[nodiscard]] auto ResolveVirtualPageEntryCount(
  const std::span<const oxygen::engine::DirectionalVirtualShadowMetadata>
    metadata) -> std::uint32_t
{
  if (metadata.empty()) {
    return 0U;
  }

  const auto& directional = metadata.front();
  return directional.clip_level_count * directional.pages_per_axis
    * directional.pages_per_axis;
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

[[nodiscard]] auto ExtractDirectionalViewForwardWs(const glm::mat4& light_view)
  -> glm::vec3
{
  const glm::mat4 inverse_view = glm::inverse(light_view);
  const glm::vec3 world_origin
    = glm::vec3(inverse_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 forward_point
    = glm::vec3(inverse_view * glm::vec4(0.0F, 0.0F, -1.0F, 1.0F));
  const glm::vec3 forward = forward_point - world_origin;
  const float length_squared = glm::dot(forward, forward);
  if (length_squared <= 1.0e-8F) {
    return glm::vec3(0.0F, 0.0F, -1.0F);
  }
  return forward * glm::inversesqrt(length_squared);
}

[[nodiscard]] auto ExtractDirectionalLightEyeWs(const glm::mat4& light_view)
  -> glm::vec3
{
  const glm::mat4 inverse_view = glm::inverse(light_view);
  return glm::vec3(inverse_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
}

[[nodiscard]] auto EvaluateDirectionalDepthGuardband(
  const oxygen::engine::DirectionalVirtualShadowMetadata& previous_metadata,
  const std::span<const glm::vec3> required_world_points,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const float ortho_half_extent_x, const float ortho_half_extent_y) -> bool
{
  // Global depth reuse must track the sampleable region, not every caster
  // motion. Per-page dirty invalidation already handles changed caster
  // contents; including caster bounds here would spuriously invalidate
  // unrelated clean pages and collapse Phase 7 static/dynamic separation.
  (void)shadow_caster_bounds;
  (void)ortho_half_extent_x;
  (void)ortho_half_extent_y;
  float required_min_depth = std::numeric_limits<float>::max();
  float required_max_depth = std::numeric_limits<float>::lowest();

  for (const auto& point_ws : required_world_points) {
    const glm::vec3 point_ls
      = glm::vec3(previous_metadata.light_view * glm::vec4(point_ws, 1.0F));
    required_min_depth = std::min(required_min_depth, point_ls.z);
    required_max_depth = std::max(required_max_depth, point_ls.z);
  }

  if (required_min_depth > required_max_depth) {
    return false;
  }

  return oxygen::renderer::internal::shadow_detail::
    IsDirectionalVirtualDepthGuardbandValid(
      previous_metadata, required_min_depth, required_max_depth);
}

struct CanonicalShadowCasterInput {
  std::vector<glm::vec4> bounds;
};

[[nodiscard]] auto CanonicalizeShadowCasterInput(
  const std::span<const glm::vec4> bounds) -> CanonicalShadowCasterInput
{
  CanonicalShadowCasterInput result {};
  result.bounds.resize(bounds.size());

  std::vector<std::size_t> order(bounds.size());
  std::iota(order.begin(), order.end(), 0U);
  std::ranges::stable_sort(
    order, [&](const std::size_t lhs_index, const std::size_t rhs_index) {
      const auto& lhs = bounds[lhs_index];
      const auto& rhs = bounds[rhs_index];
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

  for (std::size_t sorted_index = 0U; sorted_index < order.size();
    ++sorted_index) {
    const auto source_index = order[sorted_index];
    result.bounds[sorted_index] = bounds[source_index];
  }

  return result;
}

[[nodiscard]] auto BuildClipmapCacheKey(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata)
  -> oxygen::renderer::VirtualShadowClipmapCacheKey
{
  return {
    .directional_address_space_hash = oxygen::renderer::internal::
      shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(metadata),
    .clip_level_count = metadata.clip_level_count,
    .pages_per_axis = metadata.pages_per_axis,
    .page_size_texels = metadata.page_size_texels,
  };
}

[[nodiscard]] auto AreClipmapCacheKeysEqual(
  const oxygen::renderer::VirtualShadowClipmapCacheKey& lhs,
  const oxygen::renderer::VirtualShadowClipmapCacheKey& rhs) -> bool
{
  return lhs.directional_address_space_hash
    == rhs.directional_address_space_hash
    && lhs.clip_level_count == rhs.clip_level_count
    && lhs.pages_per_axis == rhs.pages_per_axis
    && lhs.page_size_texels == rhs.page_size_texels;
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
  , shadow_caster_bounds_buffer_(oxygen::observer_ptr<Graphics>(gfx_),
      *staging_provider_, static_cast<std::uint32_t>(sizeof(glm::vec4)),
      oxygen::observer_ptr<engine::upload::InlineTransfersCoordinator>(
        inline_transfers_),
      "VirtualShadowMapBackend.ShadowCasterBounds")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

VirtualShadowMapBackend::~VirtualShadowMapBackend() { ReleasePhysicalPool(); }

auto VirtualShadowMapBackend::SetDirectionalBiasSettings(
  const renderer::DirectionalVirtualBiasSettings& settings) noexcept -> void
{
  directional_bias_settings_ = settings;
}

auto VirtualShadowMapBackend::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  frame_sequence_ = sequence;
  frame_slot_ = slot;
  for (auto& [view_id, state] : view_cache_) {
    (void)view_id;
    RollExtractedCacheFrames(state);
  }
  shadow_instance_buffer_.OnFrameStart(sequence, slot);
  directional_virtual_metadata_buffer_.OnFrameStart(sequence, slot);
  shadow_caster_bounds_buffer_.OnFrameStart(sequence, slot);
}

auto VirtualShadowMapBackend::ResetCachedState() -> void
{
  std::vector<ViewId> retired_view_ids;
  retired_view_ids.reserve(view_cache_.size()
    + view_page_management_page_table_resources_.size()
    + view_page_management_page_flags_resources_.size()
    + view_resolve_resources_.size());
  const auto record_retired_view = [&retired_view_ids](const ViewId view_id) {
    if (std::find(retired_view_ids.begin(), retired_view_ids.end(), view_id)
      == retired_view_ids.end()) {
      retired_view_ids.push_back(view_id);
    }
  };
  for (const auto& [view_id, _] : view_cache_) {
    record_retired_view(view_id);
  }
  for (const auto& [view_id, _] : view_page_management_page_table_resources_) {
    record_retired_view(view_id);
  }
  for (const auto& [view_id, _] : view_page_management_page_flags_resources_) {
    record_retired_view(view_id);
  }
  for (const auto& [view_id, _] : view_resolve_resources_) {
    record_retired_view(view_id);
  }

  const auto previous_epoch = cache_epoch_;
  for (auto& [view_id, state] : view_cache_) {
    SetLifecycleState(view_id, state, CacheLifecycleState::kRetired,
      "scene_reset_retire");
    LOG_F(INFO,
      "VirtualShadowMapBackend: frame={} view={} retiring cache instance "
      "cache_epoch={} view_generation={} publication_invalidated=true "
      "page_table={} page_flags={} physical_pool={} physical_meta={} "
      "physical_lists={}",
      frame_sequence_.get(), view_id.get(), state.cache_epoch,
      state.view_generation,
      state.frame_publication.virtual_shadow_page_table_srv.get(),
      state.frame_publication.virtual_shadow_page_flags_srv.get(),
      state.frame_publication.virtual_shadow_physical_pool_srv.get(),
      state.frame_publication.virtual_shadow_physical_page_metadata_srv.get(),
      state.frame_publication.virtual_shadow_physical_page_lists_srv.get());
  }
  ++cache_epoch_;
  LOG_F(INFO,
    "VirtualShadowMapBackend: reset cached state cache_epoch {} -> {} "
    "(retired_views={} table_resources={} flag_resources={} resolve_resources={})",
    previous_epoch, cache_epoch_, retired_view_ids.size(),
    view_page_management_page_table_resources_.size(),
    view_page_management_page_flags_resources_.size(),
    view_resolve_resources_.size());
  ReleasePhysicalPool();
  view_cache_.clear();
  view_page_management_page_table_resources_.clear();
  view_page_management_page_flags_resources_.clear();
  view_resolve_resources_.clear();
  for (const auto view_id : retired_view_ids) {
    const auto previous_generation = GetOrCreateViewGeneration(view_id);
    ++view_generations_[view_id];
    LOG_F(INFO,
      "VirtualShadowMapBackend: retired view={} generation {} -> {} "
      "during cache reset",
      view_id.get(), previous_generation, view_generations_[view_id]);
  }
}

auto VirtualShadowMapBackend::RetireView(const ViewId view_id) -> void
{
  if (const auto it = view_cache_.find(view_id); it != view_cache_.end()) {
    SetLifecycleState(view_id, it->second, CacheLifecycleState::kRetired,
      "view_retired");
    LOG_F(INFO,
      "VirtualShadowMapBackend: frame={} view={} retiring cache instance "
      "cache_epoch={} view_generation={} publication_invalidated=true "
      "page_table={} page_flags={} physical_pool={} physical_meta={} "
      "physical_lists={}",
      frame_sequence_.get(), view_id.get(), it->second.cache_epoch,
      it->second.view_generation,
      it->second.frame_publication.virtual_shadow_page_table_srv.get(),
      it->second.frame_publication.virtual_shadow_page_flags_srv.get(),
      it->second.frame_publication.virtual_shadow_physical_pool_srv.get(),
      it->second.frame_publication.virtual_shadow_physical_page_metadata_srv.get(),
      it->second.frame_publication.virtual_shadow_physical_page_lists_srv.get());
  }
  view_cache_.erase(view_id);
  view_page_management_page_table_resources_.erase(view_id);
  view_page_management_page_flags_resources_.erase(view_id);
  view_resolve_resources_.erase(view_id);
  const auto previous_generation = GetOrCreateViewGeneration(view_id);
  ++view_generations_[view_id];
  LOG_F(INFO,
    "VirtualShadowMapBackend: retired view={} generation {} -> {}",
    view_id.get(), previous_generation, view_generations_[view_id]);
}

auto VirtualShadowMapBackend::PublishView(const ViewId view_id,
  const engine::ViewConstants& view_constants,
  const float camera_viewport_width,
  const std::span<const engine::DirectionalShadowCandidate>
    directional_candidates,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::chrono::milliseconds gpu_budget, const std::uint64_t view_generation,
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

  const auto canonical_shadow_caster_input
    = CanonicalizeShadowCasterInput(shadow_caster_bounds);
  const auto& canonical_shadow_caster_bounds
    = canonical_shadow_caster_input.bounds;
  const auto canonical_shadow_caster_bounds_span
    = std::span<const glm::vec4>(canonical_shadow_caster_bounds.data(),
      canonical_shadow_caster_bounds.size());

  ViewCacheEntry state {};
  state.shadow_caster_content_hash = shadow_caster_content_hash;
  state.cache_epoch = cache_epoch_;
  SyncViewGeneration(view_id, view_generation);
  state.view_generation = view_generation;

  const auto pool_config
    = BuildPhysicalPoolConfig(directional_candidates.front(), gpu_budget,
      canonical_shadow_caster_bounds_span.size());
  EnsurePhysicalPool(pool_config);
  const auto previous_it = view_cache_.find(view_id);
  const auto* previous_state
    = previous_it != view_cache_.end() ? &previous_it->second : nullptr;
  BuildDirectionalVirtualViewState(view_id, view_constants,
    camera_viewport_width, directional_candidates.front(),
    canonical_shadow_caster_bounds_span, visible_receiver_bounds,
    previous_state, state);

  if (previous_state != nullptr
    && previous_state->cache_epoch != state.cache_epoch) {
    CHECK_F(false,
      "VirtualShadowMapBackend: stale previous state crossed cache epoch "
      "boundary in PublishView view={} previous_epoch={} current_epoch={}",
      view_id.get(), previous_state->cache_epoch, state.cache_epoch);
  }

  const auto published_shadow_instances
    = std::span<const engine::ShadowInstanceMetadata> {
        state.shadow_instances
      };
  const auto published_directional_virtual_metadata
    = std::span<const engine::DirectionalVirtualShadowMetadata> {
        state.directional_virtual_metadata
      };

  state.frame_publication.shadow_instance_metadata_srv
    = PublishShadowInstances(published_shadow_instances);
  state.frame_publication.virtual_directional_shadow_metadata_srv
    = PublishDirectionalVirtualMetadata(published_directional_virtual_metadata);
  const auto virtual_page_entry_count
    = ResolveVirtualPageEntryCount(published_directional_virtual_metadata);
  const auto* page_management_table_resources
    = EnsureViewPageManagementPageTableResources(
      view_id, virtual_page_entry_count);
  const auto* page_management_flag_resources
    = EnsureViewPageManagementPageFlagResources(
      view_id, virtual_page_entry_count);
  state.frame_publication.virtual_shadow_page_table_srv
    = page_management_table_resources != nullptr
    ? page_management_table_resources->srv
    : kInvalidShaderVisibleIndex;
  state.frame_publication.virtual_shadow_page_flags_srv
    = page_management_flag_resources != nullptr
    ? page_management_flag_resources->srv
    : kInvalidShaderVisibleIndex;
  state.frame_publication.virtual_shadow_physical_pool_srv = physical_pool_srv_;
  if (!published_shadow_instances.empty()) {
    const auto flags = static_cast<engine::ShadowProductFlags>(
      published_shadow_instances.front().flags);
    if ((flags & engine::ShadowProductFlags::kSunLight)
      != engine::ShadowProductFlags::kNone) {
      state.frame_publication.sun_shadow_index = 0U;
    }
  }

  auto [it, inserted] = view_cache_.insert_or_assign(view_id, std::move(state));
  DCHECK_F(inserted || it != view_cache_.end(),
    "VirtualShadowMapBackend failed to publish view state");
  RefreshViewExports(view_id, it->second);
  return it->second.frame_publication;
}

auto VirtualShadowMapBackend::ResolveCurrentFrame(const ViewId view_id) -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    return;
  }

  auto& state = it->second;
  if (!CanApplyPendingResolveToLiveBindings(state)) {
    return;
  }

  DCHECK_F(CanApplyPendingResolveToLiveBindings(state),
    "VirtualShadowMapBackend: ResolveCurrentFrame may only apply fresh "
    "pending resolve inputs to live bindings");

  if (state.pending_residency_resolve.reset_page_management_state) {
    if (auto* resources = EnsureViewResolveResources(view_id);
      resources != nullptr) {
      // A reset only zeroes persistent GPU page-management state. Do not
      // rebuild or upload a CPU-authored residency snapshot here.
      resources->physical_page_state_reset_pending = true;
      state.current_frame.has_authoritative_page_management_state = false;
      SetLifecycleState(
        view_id, state, CacheLifecycleState::kClearing, "resolve_reset");
    }
  } else if (state.lifecycle_state == CacheLifecycleState::kResetPending
    || state.lifecycle_state == CacheLifecycleState::kInvalidated) {
    SetLifecycleState(view_id, state, CacheLifecycleState::kWarmupRasterPending,
      "resolve_without_reset");
  }
  RefreshViewExports(view_id, state);
}

auto VirtualShadowMapBackend::ApplyExtractedScheduleResult(const ViewId view_id,
  const frame::SequenceNumber source_sequence, const std::uint64_t cache_epoch,
  const std::uint64_t view_generation,
  const std::uint32_t scheduled_page_count) -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    LOG_F(INFO,
      "VirtualShadowMapBackend: dropped schedule feedback for missing view={} "
      "source_frame={}",
      view_id.get(), source_sequence.get());
    return;
  }
  if (it->second.cache_epoch != cache_epoch
    || it->second.view_generation != view_generation) {
    LOG_F(INFO,
      "VirtualShadowMapBackend: dropped stale schedule feedback view={} "
      "source_frame={} feedback_epoch={} cache_epoch={} "
      "feedback_view_generation={} cache_view_generation={}",
      view_id.get(), source_sequence.get(), cache_epoch, it->second.cache_epoch,
      view_generation, it->second.view_generation);
    return;
  }

  auto& prev_frame = it->second.prev_frame;
  prev_frame.scheduled_frame_number = scheduled_page_count > 0U
    ? static_cast<std::int64_t>(source_sequence.get())
    : -1;
  auto& extracted_feedback = it->second.extracted_feedback;
  extracted_feedback.scheduled_source_sequence = source_sequence;
  extracted_feedback.scheduled_page_count = scheduled_page_count;
  extracted_feedback.has_schedule_feedback = true;
  LOG_F(INFO,
    "VirtualShadowMapBackend: accepted schedule feedback view={} "
    "source_frame={} cache_epoch={} view_generation={} scheduled_pages={} "
    "prev_rendered_frame={} lifecycle={}",
    view_id.get(), source_sequence.get(), it->second.cache_epoch,
    it->second.view_generation, scheduled_page_count,
    it->second.prev_frame.rendered_frame_number,
    CacheLifecycleStateName(it->second.lifecycle_state));
}

auto VirtualShadowMapBackend::ApplyExtractedResolveStatsResult(
  const ViewId view_id, const frame::SequenceNumber source_sequence,
  const std::uint64_t cache_epoch, const std::uint64_t view_generation,
  const renderer::VirtualShadowResolveStats& resolve_stats) -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    LOG_F(INFO,
      "VirtualShadowMapBackend: dropped resolve feedback for missing view={} "
      "source_frame={}",
      view_id.get(), source_sequence.get());
    return;
  }
  if (it->second.cache_epoch != cache_epoch
    || it->second.view_generation != view_generation) {
    LOG_F(INFO,
      "VirtualShadowMapBackend: dropped stale resolve feedback view={} "
      "source_frame={} feedback_epoch={} cache_epoch={} "
      "feedback_view_generation={} cache_view_generation={}",
      view_id.get(), source_sequence.get(), cache_epoch, it->second.cache_epoch,
      view_generation, it->second.view_generation);
    return;
  }

  auto& prev_frame = it->second.prev_frame;
  auto& extracted_feedback = it->second.extracted_feedback;
  extracted_feedback.resolve_stats_source_sequence = source_sequence;
  extracted_feedback.resolve_stats = resolve_stats;
  extracted_feedback.has_resolve_stats_feedback = true;

  const auto scheduled_raster_page_count
    = resolve_stats.scheduled_raster_page_count;
  const auto rasterized_page_count = resolve_stats.rasterized_page_count;
  const auto requested_page_count = resolve_stats.requested_page_count;
  const auto pages_requiring_schedule_count
    = resolve_stats.pages_requiring_schedule_count;
  const bool has_unsatisfied_schedule_work
    = scheduled_raster_page_count == 0U && pages_requiring_schedule_count > 0U;
  const bool has_rendered_page_management_provenance
    = prev_frame.rendered_frame_number >= 0;
  const bool raster_work_converged
    = rasterized_page_count == scheduled_raster_page_count;
  const bool warmup_validation_required
    = it->second.stable_validation_pending
    || it->second.bootstrap_feedback_complete
    || it->second.lifecycle_state == CacheLifecycleState::kResetPending
    || it->second.lifecycle_state == CacheLifecycleState::kClearing
    || it->second.lifecycle_state == CacheLifecycleState::kCleared
    || it->second.lifecycle_state == CacheLifecycleState::kWarmupRasterPending
    || it->second.lifecycle_state == CacheLifecycleState::kWarmupFeedbackPending;
  const bool requires_stable_validation_after_raster
    = warmup_validation_required
    && (it->second.cache_epoch > 1U || it->second.view_generation > 1U);
  const bool zero_work_validation_frame = scheduled_raster_page_count == 0U
    && prev_frame.page_management_finalized
    && has_rendered_page_management_provenance;
  const bool stable_validation_frame
    = raster_work_converged && zero_work_validation_frame
    && !has_unsatisfied_schedule_work;
  const bool was_live_before_feedback
    = IsPublicationLiveState(it->second.lifecycle_state);
  const bool requires_post_reset_validation_barrier
    = it->second.current_frame_requested_reset
    && (it->second.cache_epoch > 1U || it->second.view_generation > 1U);
  const auto current_frame_matches_prev_rendered_basis = [&]() {
    if (prev_frame.directional_metadata.size() != 1U
      || it->second.current_frame.directional_metadata.size() != 1U) {
      return false;
    }
    const auto& previous_metadata = prev_frame.directional_metadata.front();
    const auto& current_metadata
      = it->second.current_frame.directional_metadata.front();
    if (!IsDirectionalVirtualAddressSpaceCompatible(
          previous_metadata, current_metadata)) {
      return false;
    }
    for (std::uint32_t clip_index = 0U;
      clip_index < current_metadata.clip_level_count; ++clip_index) {
      if (!IsDirectionalVirtualClipContentReusable(
            previous_metadata, current_metadata, clip_index)) {
        return false;
      }
    }
    return true;
  };
  const auto preserve_validated_directional_basis = [&]() {
    it->second.directional_virtual_metadata = prev_frame.directional_metadata;
    it->second.current_frame.directional_metadata
      = prev_frame.directional_metadata;
    it->second.current_frame.clipmap_cache_key = prev_frame.clipmap_cache_key;
    it->second.current_frame.cached_clip_grid_origin_x
      = prev_frame.cached_clip_grid_origin_x;
    it->second.current_frame.cached_clip_grid_origin_y
      = prev_frame.cached_clip_grid_origin_y;
    it->second.current_frame.has_cached_clip_grid_origins
      = prev_frame.has_cached_clip_grid_origins;
  };
  const auto snapshot_current_rendered_basis = [&]() {
    if (it->second.current_frame.directional_metadata.empty()) {
      return;
    }
    prev_frame.directional_metadata = it->second.current_frame.directional_metadata;
    prev_frame.clipmap_cache_key = it->second.current_frame.clipmap_cache_key;
    prev_frame.shadow_caster_content_hash
      = it->second.current_frame.shadow_caster_content_hash;
    prev_frame.cached_clip_grid_origin_x
      = it->second.current_frame.cached_clip_grid_origin_x;
    prev_frame.cached_clip_grid_origin_y
      = it->second.current_frame.cached_clip_grid_origin_y;
    prev_frame.has_cached_clip_grid_origins
      = it->second.current_frame.has_cached_clip_grid_origins;
    prev_frame.is_uncached = it->second.current_frame.is_uncached;
  };
  if (scheduled_raster_page_count > 0U && raster_work_converged
    && requires_stable_validation_after_raster) {
    snapshot_current_rendered_basis();
    it->second.preserve_rendered_basis_until_next_raster = false;
    prev_frame.has_authoritative_page_management_state = false;
    prev_frame.rendered_frame_number
      = static_cast<std::int64_t>(source_sequence.get());
    prev_frame.scheduled_frame_number
      = static_cast<std::int64_t>(source_sequence.get());
    it->second.current_frame.has_authoritative_page_management_state = false;
    it->second.current_frame.rendered_frame_number
      = static_cast<std::int64_t>(source_sequence.get());
    it->second.current_frame.scheduled_frame_number
      = static_cast<std::int64_t>(source_sequence.get());
    it->second.bootstrap_feedback_complete = true;
    it->second.stable_validation_pending = true;
    SetLifecycleState(view_id, it->second,
      CacheLifecycleState::kWarmupFeedbackPending,
      "accepted_raster_feedback_requires_stable_validation");
  } else if (scheduled_raster_page_count > 0U && raster_work_converged) {
    snapshot_current_rendered_basis();
    it->second.preserve_rendered_basis_until_next_raster = false;
    prev_frame.has_authoritative_page_management_state = true;
    prev_frame.rendered_frame_number
      = static_cast<std::int64_t>(source_sequence.get());
    prev_frame.scheduled_frame_number
      = static_cast<std::int64_t>(source_sequence.get());
    it->second.current_frame.has_authoritative_page_management_state = true;
    it->second.current_frame.rendered_frame_number
      = static_cast<std::int64_t>(source_sequence.get());
    it->second.current_frame.scheduled_frame_number
      = static_cast<std::int64_t>(source_sequence.get());
    it->second.bootstrap_feedback_complete = false;
    it->second.stable_validation_pending = false;
    SetLifecycleState(
      view_id, it->second, CacheLifecycleState::kValid, "accepted_feedback");
  } else if (has_unsatisfied_schedule_work) {
    if (was_live_before_feedback) {
      preserve_validated_directional_basis();
      it->second.preserve_rendered_basis_until_next_raster = false;
      prev_frame.has_authoritative_page_management_state = true;
      it->second.current_frame.has_authoritative_page_management_state = true;
      it->second.current_frame.rendered_frame_number
        = prev_frame.rendered_frame_number;
      it->second.current_frame.scheduled_frame_number
        = prev_frame.scheduled_frame_number;
      it->second.bootstrap_feedback_complete = false;
      it->second.stable_validation_pending = true;
      SetLifecycleState(view_id, it->second, CacheLifecycleState::kDirty,
        "zero_schedule_with_unsatisfied_work_dirty");
    } else {
      preserve_validated_directional_basis();
      it->second.preserve_rendered_basis_until_next_raster = false;
      prev_frame.has_authoritative_page_management_state = false;
      it->second.current_frame.has_authoritative_page_management_state = false;
      it->second.current_frame.rendered_frame_number
        = prev_frame.rendered_frame_number;
      it->second.current_frame.scheduled_frame_number
        = prev_frame.scheduled_frame_number;
      it->second.bootstrap_feedback_complete = true;
      it->second.stable_validation_pending = true;
      SetLifecycleState(view_id, it->second,
        CacheLifecycleState::kWarmupFeedbackPending,
        "zero_schedule_with_unsatisfied_work");
    }
  } else if (stable_validation_frame) {
    preserve_validated_directional_basis();
    if (!current_frame_matches_prev_rendered_basis()) {
      it->second.preserve_rendered_basis_until_next_raster = true;
      prev_frame.has_authoritative_page_management_state = false;
      it->second.current_frame.has_authoritative_page_management_state = false;
      it->second.current_frame.rendered_frame_number
        = prev_frame.rendered_frame_number;
      it->second.current_frame.scheduled_frame_number
        = prev_frame.scheduled_frame_number;
      it->second.bootstrap_feedback_complete = true;
      it->second.stable_validation_pending = true;
      SetLifecycleState(view_id, it->second,
        CacheLifecycleState::kWarmupFeedbackPending,
        "accepted_feedback_requires_same_basis_provenance");
    } else if (requires_post_reset_validation_barrier) {
      it->second.preserve_rendered_basis_until_next_raster = true;
      prev_frame.has_authoritative_page_management_state = false;
      it->second.current_frame.has_authoritative_page_management_state = false;
      it->second.current_frame.rendered_frame_number
        = prev_frame.rendered_frame_number;
      it->second.current_frame.scheduled_frame_number
        = prev_frame.scheduled_frame_number;
      it->second.bootstrap_feedback_complete = true;
      it->second.stable_validation_pending = true;
      SetLifecycleState(view_id, it->second,
        CacheLifecycleState::kWarmupFeedbackPending,
        "accepted_feedback_requires_post_reset_barrier");
    } else {
      it->second.preserve_rendered_basis_until_next_raster = true;
      prev_frame.has_authoritative_page_management_state = true;
      it->second.current_frame.has_authoritative_page_management_state = true;
      it->second.current_frame.rendered_frame_number
        = prev_frame.rendered_frame_number;
      it->second.current_frame.scheduled_frame_number
        = prev_frame.scheduled_frame_number;
      it->second.bootstrap_feedback_complete = false;
      it->second.stable_validation_pending = false;
      SetLifecycleState(
        view_id, it->second, CacheLifecycleState::kValid, "accepted_feedback");
    }
  } else if (scheduled_raster_page_count > 0U
    && rasterized_page_count != scheduled_raster_page_count) {
    it->second.preserve_rendered_basis_until_next_raster = false;
    prev_frame.has_authoritative_page_management_state = false;
    prev_frame.rendered_frame_number = -1;
    it->second.current_frame.has_authoritative_page_management_state = false;
    it->second.current_frame.rendered_frame_number = -1;
    it->second.bootstrap_feedback_complete = !was_live_before_feedback;
    it->second.stable_validation_pending = true;
    SetLifecycleState(view_id, it->second,
      was_live_before_feedback ? CacheLifecycleState::kDirty
                               : CacheLifecycleState::kWarmupFeedbackPending,
      was_live_before_feedback ? "incomplete_raster_feedback"
                               : "incomplete_raster_feedback_warmup");
    LOG_F(ERROR,
      "VirtualShadowMapBackend: view={} source_frame={} incomplete raster "
      "completion stats scheduled_raster_pages={} rasterized_pages={} "
      "cached_transitions={}",
      view_id.get(), source_sequence.get(), scheduled_raster_page_count,
      rasterized_page_count, resolve_stats.cached_page_transition_count);
  }

  if (extracted_feedback.has_schedule_feedback
    && extracted_feedback.scheduled_source_sequence == source_sequence
    && extracted_feedback.scheduled_page_count != scheduled_raster_page_count) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: view={} source_frame={} schedule mismatch "
      "schedule_readback_pages={} resolve_scheduled_raster_pages={}",
      view_id.get(), source_sequence.get(),
      extracted_feedback.scheduled_page_count, scheduled_raster_page_count);
  }
  if (extracted_feedback.has_schedule_feedback
    && extracted_feedback.scheduled_source_sequence == source_sequence
    && extracted_feedback.scheduled_page_count == scheduled_raster_page_count) {
    LOG_F(INFO,
      "VirtualShadowMapBackend: view={} source_frame={} schedule/resolve "
      "matched lifecycle={} scheduled_pages={} rasterized_pages={} "
      "requested_pages={} requires_schedule_pages={}",
      view_id.get(), source_sequence.get(),
      CacheLifecycleStateName(it->second.lifecycle_state),
      scheduled_raster_page_count, rasterized_page_count,
      requested_page_count, pages_requiring_schedule_count);
  }

  LOG_F(INFO,
    "VirtualShadowMapBackend: view={} source_frame={} extracted feedback "
    "schedule_readback_pages={} scheduled_raster_pages={} "
    "rasterized_pages={} cached_transitions={} allocated={} requested={} "
    "requires_schedule={} resident_dirty={} resident_clean={} available={}",
    view_id.get(), source_sequence.get(),
    extracted_feedback.has_schedule_feedback
      ? extracted_feedback.scheduled_page_count
      : 0U,
    resolve_stats.scheduled_raster_page_count,
    resolve_stats.rasterized_page_count,
    resolve_stats.cached_page_transition_count,
    resolve_stats.allocated_page_count, resolve_stats.requested_page_count,
    resolve_stats.pages_requiring_schedule_count,
    resolve_stats.resident_dirty_page_count,
    resolve_stats.resident_clean_page_count,
    resolve_stats.available_page_list_count);
}

auto VirtualShadowMapBackend::PreparePageTableResources(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  const auto state_it = view_cache_.find(view_id);
  if (state_it == view_cache_.end()) {
    return;
  }

  const auto page_management_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_management_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
  const bool has_page_management_table_resources
    = page_management_table_resources_it
      != view_page_management_page_table_resources_.end()
    && ResourceOwnershipMatches(
      page_management_table_resources_it->second, state_it->second)
    && page_management_table_resources_it->second.gpu_buffer;
  const bool has_page_management_flags_resources
    = page_management_flags_resources_it
      != view_page_management_page_flags_resources_.end()
    && ResourceOwnershipMatches(
      page_management_flags_resources_it->second, state_it->second)
    && page_management_flags_resources_it->second.gpu_buffer;
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  const bool has_resolve_resources
    = resolve_resources_it != view_resolve_resources_.end()
    && ResourceOwnershipMatches(resolve_resources_it->second, state_it->second);
  if (!has_page_management_table_resources
    && !has_page_management_flags_resources && !has_resolve_resources) {
    return;
  }
  if (has_page_management_table_resources) {
    auto& page_management_table_resources
      = page_management_table_resources_it->second;
    if (!recorder.IsResourceTracked(
          *page_management_table_resources.gpu_buffer)) {
      recorder.BeginTrackingResourceState(
        *page_management_table_resources.gpu_buffer,
        graphics::ResourceStates::kCommon, true);
    }
    recorder.RequireResourceState(*page_management_table_resources.gpu_buffer,
      graphics::ResourceStates::kShaderResource);
  }
  if (has_page_management_flags_resources) {
    auto& page_management_flags_resources
      = page_management_flags_resources_it->second;
    if (!recorder.IsResourceTracked(
          *page_management_flags_resources.gpu_buffer)) {
      recorder.BeginTrackingResourceState(
        *page_management_flags_resources.gpu_buffer,
        graphics::ResourceStates::kCommon, true);
    }
    recorder.RequireResourceState(*page_management_flags_resources.gpu_buffer,
      graphics::ResourceStates::kShaderResource);
  }
  if (has_resolve_resources) {
    auto& resolve_resources = resolve_resources_it->second;
    if (resolve_resources.physical_page_metadata_gpu_buffer) {
      if (!recorder.IsResourceTracked(
            *resolve_resources.physical_page_metadata_gpu_buffer)) {
        recorder.BeginTrackingResourceState(
          *resolve_resources.physical_page_metadata_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      recorder.RequireResourceState(
        *resolve_resources.physical_page_metadata_gpu_buffer,
        graphics::ResourceStates::kShaderResource);
    }
    if (resolve_resources.physical_page_lists_gpu_buffer) {
      if (!recorder.IsResourceTracked(
            *resolve_resources.physical_page_lists_gpu_buffer)) {
        recorder.BeginTrackingResourceState(
          *resolve_resources.physical_page_lists_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      recorder.RequireResourceState(
        *resolve_resources.physical_page_lists_gpu_buffer,
        graphics::ResourceStates::kShaderResource);
    }
    if (resolve_resources.stats_gpu_buffer) {
      if (!recorder.IsResourceTracked(*resolve_resources.stats_gpu_buffer)) {
        recorder.BeginTrackingResourceState(*resolve_resources.stats_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      recorder.RequireResourceState(*resolve_resources.stats_gpu_buffer,
        graphics::ResourceStates::kShaderResource);
    }
    if (resolve_resources.dirty_page_flags_gpu_buffer) {
      if (!recorder.IsResourceTracked(
            *resolve_resources.dirty_page_flags_gpu_buffer)) {
        recorder.BeginTrackingResourceState(
          *resolve_resources.dirty_page_flags_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      recorder.RequireResourceState(
        *resolve_resources.dirty_page_flags_gpu_buffer,
        graphics::ResourceStates::kShaderResource);
    }
  }
  recorder.FlushBarriers();
}

auto VirtualShadowMapBackend::PreparePageManagementOutputsForGpuWrite(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  const auto state_it = view_cache_.find(view_id);
  if (state_it == view_cache_.end()) {
    return;
  }
  const auto page_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
  if (page_table_resources_it
      == view_page_management_page_table_resources_.end()
    || page_flags_resources_it
      == view_page_management_page_flags_resources_.end()
    || !ResourceOwnershipMatches(page_table_resources_it->second, state_it->second)
    || !ResourceOwnershipMatches(page_flags_resources_it->second, state_it->second)
    || !page_table_resources_it->second.gpu_buffer
    || !page_flags_resources_it->second.gpu_buffer) {
    return;
  }

  auto& page_table_resources = page_table_resources_it->second;
  auto& page_flags_resources = page_flags_resources_it->second;
  auto resolve_resources_it = view_resolve_resources_.find(view_id);

  if (!recorder.IsResourceTracked(*page_table_resources.gpu_buffer)) {
    recorder.BeginTrackingResourceState(*page_table_resources.gpu_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*page_flags_resources.gpu_buffer)) {
    recorder.BeginTrackingResourceState(*page_flags_resources.gpu_buffer,
      graphics::ResourceStates::kCommon, true);
  }

  recorder.EnableAutoMemoryBarriers(*page_table_resources.gpu_buffer);
  recorder.EnableAutoMemoryBarriers(*page_flags_resources.gpu_buffer);
  recorder.RequireResourceState(*page_table_resources.gpu_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*page_flags_resources.gpu_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  if (resolve_resources_it != view_resolve_resources_.end()) {
    auto& resolve_resources = resolve_resources_it->second;
    if (resolve_resources.dirty_page_flags_gpu_buffer) {
      if (!recorder.IsResourceTracked(
            *resolve_resources.dirty_page_flags_gpu_buffer)) {
        recorder.BeginTrackingResourceState(
          *resolve_resources.dirty_page_flags_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      recorder.EnableAutoMemoryBarriers(
        *resolve_resources.dirty_page_flags_gpu_buffer);
      recorder.RequireResourceState(
        *resolve_resources.dirty_page_flags_gpu_buffer,
        graphics::ResourceStates::kUnorderedAccess);
    }
    if (resolve_resources.physical_page_metadata_gpu_buffer) {
      if (!recorder.IsResourceTracked(
            *resolve_resources.physical_page_metadata_gpu_buffer)) {
        recorder.BeginTrackingResourceState(
          *resolve_resources.physical_page_metadata_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      recorder.EnableAutoMemoryBarriers(
        *resolve_resources.physical_page_metadata_gpu_buffer);
      recorder.RequireResourceState(
        *resolve_resources.physical_page_metadata_gpu_buffer,
        graphics::ResourceStates::kUnorderedAccess);
    }
    if (resolve_resources.physical_page_lists_gpu_buffer) {
      if (!recorder.IsResourceTracked(
            *resolve_resources.physical_page_lists_gpu_buffer)) {
        recorder.BeginTrackingResourceState(
          *resolve_resources.physical_page_lists_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      recorder.EnableAutoMemoryBarriers(
        *resolve_resources.physical_page_lists_gpu_buffer);
      recorder.RequireResourceState(
        *resolve_resources.physical_page_lists_gpu_buffer,
        graphics::ResourceStates::kUnorderedAccess);
    }
    if (resolve_resources.stats_gpu_buffer) {
      if (!recorder.IsResourceTracked(*resolve_resources.stats_gpu_buffer)) {
        recorder.BeginTrackingResourceState(*resolve_resources.stats_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      recorder.EnableAutoMemoryBarriers(*resolve_resources.stats_gpu_buffer);
      recorder.RequireResourceState(*resolve_resources.stats_gpu_buffer,
        graphics::ResourceStates::kUnorderedAccess);
    }
  }
  recorder.FlushBarriers();
}

auto VirtualShadowMapBackend::FinalizePageManagementOutputs(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  const auto state_it = view_cache_.find(view_id);
  const auto page_management_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_management_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
  if (state_it == view_cache_.end()
    || page_management_table_resources_it
      == view_page_management_page_table_resources_.end()
    || page_management_flags_resources_it
      == view_page_management_page_flags_resources_.end()
    || !ResourceOwnershipMatches(
      page_management_table_resources_it->second, state_it->second)
    || !ResourceOwnershipMatches(
      page_management_flags_resources_it->second, state_it->second)
    || !page_management_table_resources_it->second.gpu_buffer
    || !page_management_flags_resources_it->second.gpu_buffer) {
    return;
  }

  auto& state = state_it->second;
  auto& page_management_table_resources
    = page_management_table_resources_it->second;
  auto& page_management_flags_resources
    = page_management_flags_resources_it->second;
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  RefreshViewExports(view_id, state);

  recorder.RequireResourceState(*page_management_table_resources.gpu_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*page_management_flags_resources.gpu_buffer,
    graphics::ResourceStates::kShaderResource);
  state.current_frame.page_management_finalized = true;
  if (resolve_resources_it != view_resolve_resources_.end()
    && ResourceOwnershipMatches(resolve_resources_it->second, state_it->second)) {
    if (resolve_resources_it->second.physical_page_state_reset_pending
      && state.lifecycle_state == CacheLifecycleState::kClearing) {
      SetLifecycleState(
        view_id, state, CacheLifecycleState::kCleared, "clear_finalize");
    }
    resolve_resources_it->second.physical_page_state_reset_pending = false;
  }
  RefreshViewExports(view_id, state);
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

  if (it->second.pending_residency_resolve.valid) {
    it->second.pending_residency_resolve.view_constants
      .SetBindlessViewFrameBindingsSlot(slot, engine::ViewConstants::kRenderer);
  }
  RefreshViewExports(view_id, it->second);
}

auto VirtualShadowMapBackend::TryGetFramePublication(
  const ViewId view_id) const noexcept -> const ShadowFramePublication*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.frame_publication : nullptr;
}

auto VirtualShadowMapBackend::TryGetPageManagementBindings(
  const ViewId view_id) const noexcept
  -> const renderer::VirtualShadowPageManagementBindings*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.page_management_bindings
                                 : nullptr;
}

auto VirtualShadowMapBackend::TryGetPageManagementStateSnapshot(
  const ViewId view_id) const noexcept
  -> std::optional<renderer::VirtualShadowPageManagementStateSnapshot>
{
  const auto state_it = view_cache_.find(view_id);
  const auto* bindings = TryGetPageManagementBindings(view_id);
  if (bindings == nullptr || state_it == view_cache_.end()) {
    return std::nullopt;
  }

  return renderer::VirtualShadowPageManagementStateSnapshot {
    .reset_page_management_state = bindings->reset_page_management_state,
    .reset_request_pending
    = state_it->second.current_frame_requested_reset ? 1U : 0U,
    .global_dirty_resident_contents
    = bindings->global_dirty_resident_contents,
  };
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

auto VirtualShadowMapBackend::TryGetShadowInstanceMetadata(
  const ViewId view_id) const noexcept -> const engine::ShadowInstanceMetadata*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() && !it->second.shadow_instances.empty()
    ? &it->second.shadow_instances.front()
    : nullptr;
}

auto VirtualShadowMapBackend::TryGetPageFlagsBuffer(
  const ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>
{
  const auto it = view_page_management_page_flags_resources_.find(view_id);
  return it != view_page_management_page_flags_resources_.end()
    ? it->second.gpu_buffer
    : nullptr;
}

auto VirtualShadowMapBackend::TryGetPhysicalPageMetadataBuffer(
  const ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>
{
  const auto it = view_resolve_resources_.find(view_id);
  return it != view_resolve_resources_.end()
    ? it->second.physical_page_metadata_gpu_buffer
    : nullptr;
}

auto VirtualShadowMapBackend::TryGetResolveStatsBuffer(
  const ViewId view_id) const noexcept -> std::shared_ptr<graphics::Buffer>
{
  const auto it = view_resolve_resources_.find(view_id);
  return it != view_resolve_resources_.end()
    ? it->second.stats_gpu_buffer
    : nullptr;
}

auto VirtualShadowMapBackend::GetPhysicalPoolTexture() const noexcept
  -> const std::shared_ptr<graphics::Texture>&
{
  return physical_pool_texture_;
}

auto VirtualShadowMapBackend::InitializeDirectionalViewStateFromClipmapSetup(
  const DirectionalVirtualClipmapSetup& setup,
  const std::span<const glm::vec4> shadow_caster_bounds,
  ViewCacheEntry& state) const -> void
{
  state.shadow_instances.push_back(setup.shadow_instance);
  state.shadow_caster_bounds.assign(
    shadow_caster_bounds.begin(), shadow_caster_bounds.end());
}

auto VirtualShadowMapBackend::RollExtractedCacheFrames(
  ViewCacheEntry& state) noexcept -> void
{
  const auto old_previous = state.prev_frame;
  const auto old_current = state.current_frame;
  const bool preserve_last_rendered_basis = !old_current.directional_metadata.empty()
    && old_current.page_management_finalized
    && old_previous.rendered_frame_number >= 0
    && old_current.rendered_frame_number == old_previous.rendered_frame_number
    && old_current.scheduled_frame_number == old_previous.scheduled_frame_number;

  state.prev_frame
    = old_current.directional_metadata.empty() ? old_previous : old_current;
  if (preserve_last_rendered_basis) {
    // A zero-raster validation frame can validate the previously rendered page
    // management state, but it must not replace the rendered shadow basis.
    state.prev_frame.directional_metadata = old_previous.directional_metadata;
    state.prev_frame.clipmap_cache_key = old_previous.clipmap_cache_key;
    state.prev_frame.shadow_caster_content_hash
      = old_previous.shadow_caster_content_hash;
    state.prev_frame.cached_clip_grid_origin_x
      = old_previous.cached_clip_grid_origin_x;
    state.prev_frame.cached_clip_grid_origin_y
      = old_previous.cached_clip_grid_origin_y;
    state.prev_frame.has_cached_clip_grid_origins
      = old_previous.has_cached_clip_grid_origins;
    state.prev_frame.is_uncached = old_previous.is_uncached;
  }
  if (!old_current.directional_metadata.empty()) {
    if (old_current.page_management_finalized) {
      // A finalized frame only carries reusable authority forward once matching
      // resolve feedback validated that exact frame. Finalization alone is not
      // enough to preserve authority from an older frame through a new one.
      state.prev_frame.has_authoritative_page_management_state
        = old_current.has_authoritative_page_management_state;
      state.prev_frame.rendered_frame_number
        = old_current.rendered_frame_number;
      state.prev_frame.scheduled_frame_number
        = old_current.scheduled_frame_number;
    } else {
      state.prev_frame.has_authoritative_page_management_state = false;
      state.prev_frame.rendered_frame_number = -1;
      state.prev_frame.scheduled_frame_number = -1;
    }
  }

  state.current_frame = {};
}

auto VirtualShadowMapBackend::AssessDirectionalCacheReuse(
  const DirectionalVirtualClipmapSetup& setup,
  const ViewCacheEntry* previous_state, const ViewCacheEntry& state) const
  -> DirectionalCacheReuseAssessment
{
  DirectionalCacheReuseAssessment assessment {};
  const bool has_reusable_rendered_cache
    = HasReusableRenderedCache(previous_state);
  assessment.has_reusable_rendered_cache = has_reusable_rendered_cache;
  assessment.current_view_is_uncached = !has_reusable_rendered_cache;

  if (!has_reusable_rendered_cache || previous_state == nullptr) {
    assessment.invalidate_rendered_cache_history = false;
    const bool previous_reset_already_applied = previous_state != nullptr
      && (previous_state->lifecycle_state == CacheLifecycleState::kClearing
        || previous_state->lifecycle_state == CacheLifecycleState::kCleared
        || previous_state->lifecycle_state
          == CacheLifecycleState::kWarmupRasterPending
        || previous_state->lifecycle_state
          == CacheLifecycleState::kWarmupFeedbackPending
        || previous_state->lifecycle_state == CacheLifecycleState::kValid
        || previous_state->lifecycle_state == CacheLifecycleState::kDirty);
    if (previous_reset_already_applied) {
      // After a reset-generation cache has cleared its persistent GPU state,
      // subsequent frames must continue warmup instead of re-requesting a full
      // reset on every publish.
      assessment.reset_page_management_state = false;
      assessment.invalidate_rendered_cache_history = false;
    }
    return assessment;
  }

  assessment.previous_shadow_caster_bounds
    = &previous_state->shadow_caster_bounds;
  if (previous_state->prev_frame.directional_metadata.size() == 1U) {
    assessment.previous_metadata
      = &previous_state->prev_frame.directional_metadata.front();
  }

  const auto current_clipmap_cache_key = BuildClipmapCacheKey(setup.metadata);
  const bool clipmap_key_matches = AreClipmapCacheKeysEqual(
    previous_state->prev_frame.clipmap_cache_key, current_clipmap_cache_key);
  assessment.clipmap_key_matches = clipmap_key_matches;
  const bool address_space_compatible = assessment.previous_metadata != nullptr
    && clipmap_key_matches
    && IsDirectionalViewCacheCompatible(setup, previous_state);
  assessment.address_space_compatible = address_space_compatible;
  bool content_compatible = address_space_compatible;
  if (content_compatible) {
    const auto& previous_metadata = *assessment.previous_metadata;
    const auto clip_count = std::min(
      previous_metadata.clip_level_count, setup.metadata.clip_level_count);
    for (std::uint32_t clip_index = 0U; clip_index < clip_count; ++clip_index) {
      if (!IsDirectionalVirtualClipContentReusable(
            previous_metadata, setup.metadata, clip_index)) {
        assessment.first_content_mismatch_clip_index
          = static_cast<std::int32_t>(clip_index);
        content_compatible = false;
        break;
      }
    }
  }
  assessment.content_compatible = content_compatible;

  const bool requires_cache_reset
    = !address_space_compatible || !content_compatible;
  assessment.reset_page_management_state = requires_cache_reset;
  assessment.invalidate_rendered_cache_history = requires_cache_reset;

  const bool shadow_content_changed
    = previous_state->prev_frame.shadow_caster_content_hash
    != state.shadow_caster_content_hash;
  assessment.shadow_content_changed = shadow_content_changed;
  assessment.shadow_bounds_count_match
    = assessment.previous_shadow_caster_bounds != nullptr
    && assessment.previous_shadow_caster_bounds->size()
      == state.shadow_caster_bounds.size();
  assessment.compare_shadow_caster_bounds_on_gpu = content_compatible
    && !shadow_content_changed
    && assessment.previous_shadow_caster_bounds != nullptr
    && assessment.shadow_bounds_count_match
    && !state.shadow_caster_bounds.empty();
  assessment.global_dirty_resident_contents = address_space_compatible
    && content_compatible && !assessment.compare_shadow_caster_bounds_on_gpu;

  return assessment;
}

auto VirtualShadowMapBackend::HasReusableRenderedCache(
  const ViewCacheEntry* previous_state) const noexcept -> bool
{
  return previous_state != nullptr
    && IsPublicationLiveState(previous_state->lifecycle_state)
    && !previous_state->bootstrap_feedback_complete
    && !previous_state->stable_validation_pending
    && previous_state->prev_frame.has_authoritative_page_management_state
    && previous_state->prev_frame.rendered_frame_number >= 0;
}

auto VirtualShadowMapBackend::RollForwardCacheHistory(
  const ViewCacheEntry* previous_state, ViewCacheEntry& state) const noexcept
  -> void
{
  state.prev_frame = {};
  state.current_frame = {};
  state.current_frame.shadow_caster_content_hash
    = state.shadow_caster_content_hash;
  state.lifecycle_state = CacheLifecycleState::kUninitialized;
  state.bootstrap_feedback_complete = false;
  state.stable_validation_pending = false;
  state.preserve_rendered_basis_until_next_raster = false;
  state.current_frame_requested_reset = false;

  if (previous_state == nullptr) {
    return;
  }

  CHECK_F(previous_state->cache_epoch == state.cache_epoch,
    "VirtualShadowMapBackend: RollForwardCacheHistory crossed cache epoch "
    "boundary previous_epoch={} current_epoch={}",
    previous_state->cache_epoch, state.cache_epoch);

  state.prev_frame = previous_state->prev_frame;
  state.lifecycle_state = previous_state->lifecycle_state;
  state.bootstrap_feedback_complete
    = previous_state->bootstrap_feedback_complete;
  state.stable_validation_pending = previous_state->stable_validation_pending;
  state.preserve_rendered_basis_until_next_raster
    = previous_state->preserve_rendered_basis_until_next_raster;
}

auto VirtualShadowMapBackend::InvalidateRenderedCacheHistory(
  ViewCacheEntry& state) noexcept -> void
{
  state.prev_frame.has_authoritative_page_management_state = false;
  state.current_frame.has_authoritative_page_management_state = false;
  state.prev_frame.rendered_frame_number = -1;
  state.current_frame.rendered_frame_number = -1;
  state.prev_frame.scheduled_frame_number = -1;
  state.current_frame.scheduled_frame_number = -1;
  state.preserve_rendered_basis_until_next_raster = false;
}

auto VirtualShadowMapBackend::IsPublicationLiveState(
  const CacheLifecycleState state) noexcept -> bool
{
  return state == CacheLifecycleState::kValid
    || state == CacheLifecycleState::kDirty;
}

namespace {

[[nodiscard]] auto IsAllowedLifecycleTransition(
  const oxygen::renderer::internal::VirtualShadowMapBackend::CacheLifecycleState
    current,
  const oxygen::renderer::internal::VirtualShadowMapBackend::CacheLifecycleState
    next) noexcept -> bool
{
  using State
    = oxygen::renderer::internal::VirtualShadowMapBackend::CacheLifecycleState;
  switch (current) {
  case State::kUninitialized:
    return next == State::kInvalidated || next == State::kResetPending
      || next == State::kRetired;
  case State::kResetPending:
    return next == State::kClearing || next == State::kRetired;
  case State::kClearing:
    return next == State::kCleared || next == State::kRetired;
  case State::kCleared:
    return next == State::kWarmupRasterPending
      || next == State::kWarmupFeedbackPending || next == State::kValid
      || next == State::kRetired;
  case State::kWarmupRasterPending:
    return next == State::kWarmupFeedbackPending || next == State::kValid
      || next == State::kRetired;
  case State::kWarmupFeedbackPending:
    return next == State::kWarmupRasterPending || next == State::kValid
      || next == State::kRetired;
  case State::kValid:
    return next == State::kDirty || next == State::kInvalidated
      || next == State::kRetired;
  case State::kDirty:
    return next == State::kValid || next == State::kInvalidated
      || next == State::kRetired;
  case State::kInvalidated:
    return next == State::kResetPending || next == State::kRetired;
  case State::kRetired:
    return false;
  }
  return false;
}

} // namespace

auto VirtualShadowMapBackend::CacheLifecycleStateName(
  const CacheLifecycleState state) noexcept -> const char*
{
  switch (state) {
  case CacheLifecycleState::kUninitialized:
    return "Uninitialized";
  case CacheLifecycleState::kResetPending:
    return "ResetPending";
  case CacheLifecycleState::kClearing:
    return "Clearing";
  case CacheLifecycleState::kCleared:
    return "Cleared";
  case CacheLifecycleState::kWarmupRasterPending:
    return "WarmupRasterPending";
  case CacheLifecycleState::kWarmupFeedbackPending:
    return "WarmupFeedbackPending";
  case CacheLifecycleState::kValid:
    return "Valid";
  case CacheLifecycleState::kDirty:
    return "Dirty";
  case CacheLifecycleState::kInvalidated:
    return "Invalidated";
  case CacheLifecycleState::kRetired:
    return "Retired";
  }
  return "Unknown";
}

auto VirtualShadowMapBackend::SetLifecycleState(const ViewId view_id,
  ViewCacheEntry& state, const CacheLifecycleState next_state,
  const char* reason) const -> void
{
  if (state.lifecycle_state == next_state) {
    return;
  }

  CHECK_F(IsAllowedLifecycleTransition(state.lifecycle_state, next_state),
    "VirtualShadowMapBackend: illegal lifecycle transition frame={} view={} "
    "{} -> {} reason={}",
    frame_sequence_.get(), view_id.get(),
    CacheLifecycleStateName(state.lifecycle_state),
    CacheLifecycleStateName(next_state), reason);

  LOG_F(INFO,
    "VirtualShadowMapBackend: frame={} view={} lifecycle {} -> {} reason={}",
    frame_sequence_.get(), view_id.get(),
    CacheLifecycleStateName(state.lifecycle_state),
    CacheLifecycleStateName(next_state), reason);
  state.lifecycle_state = next_state;
}

auto VirtualShadowMapBackend::GetOrCreateViewGeneration(const ViewId view_id)
  -> std::uint64_t
{
  auto [it, inserted] = view_generations_.try_emplace(view_id, 1U);
  if (inserted && it->second == 0U) {
    it->second = 1U;
  }
  return it->second;
}

auto VirtualShadowMapBackend::SyncViewGeneration(const ViewId view_id,
  const std::uint64_t view_generation) -> void
{
  auto& current_generation = view_generations_[view_id];
  if (current_generation != 0U && current_generation != view_generation) {
    LOG_F(INFO,
      "VirtualShadowMapBackend: synced view={} generation {} -> {}",
      view_id.get(), current_generation, view_generation);
  }
  current_generation = std::max<std::uint64_t>(1U, view_generation);
}

auto VirtualShadowMapBackend::ResourceOwnershipMatches(
  const ViewStructuredWordBufferResources& resources,
  const ViewCacheEntry& state) const noexcept -> bool
{
  return resources.cache_epoch == state.cache_epoch
    && resources.view_generation == state.view_generation;
}

auto VirtualShadowMapBackend::ResourceOwnershipMatches(
  const ViewResolveResources& resources, const ViewCacheEntry& state) const
  noexcept -> bool
{
  return resources.cache_epoch == state.cache_epoch
    && resources.view_generation == state.view_generation;
}

auto VirtualShadowMapBackend::CanApplyPendingResolveToLiveBindings(
  const ViewCacheEntry& state) noexcept -> bool
{
  return state.pending_residency_resolve.valid
    && state.pending_residency_resolve.has_fresh_pending_resolve_inputs;
}

auto VirtualShadowMapBackend::RefreshViewExports(
  const ViewId view_id, ViewCacheEntry& state) const -> void
{
  const auto page_management_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_management_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  const bool has_owned_page_table_resources
    = page_management_table_resources_it
      != view_page_management_page_table_resources_.end()
    && ResourceOwnershipMatches(page_management_table_resources_it->second, state);
  const bool has_owned_page_flags_resources
    = page_management_flags_resources_it
      != view_page_management_page_flags_resources_.end()
    && ResourceOwnershipMatches(page_management_flags_resources_it->second, state);
  const bool has_owned_resolve_resources
    = resolve_resources_it != view_resolve_resources_.end()
    && ResourceOwnershipMatches(resolve_resources_it->second, state);

  DCHECK_F(!state.pending_residency_resolve.has_fresh_pending_resolve_inputs
      || state.pending_residency_resolve.valid,
    "VirtualShadowMapBackend: fresh pending resolve inputs require a valid "
    "pending resolve packet");

  renderer::VirtualShadowPageManagementBindings bindings {};
  bindings.page_table_srv = has_owned_page_table_resources
    ? page_management_table_resources_it->second.srv
    : kInvalidShaderVisibleIndex;
  bindings.page_table_uav = has_owned_page_table_resources
    ? page_management_table_resources_it->second.uav
    : kInvalidShaderVisibleIndex;
  bindings.page_flags_srv = has_owned_page_flags_resources
    ? page_management_flags_resources_it->second.srv
    : kInvalidShaderVisibleIndex;
  bindings.page_flags_uav = has_owned_page_flags_resources
    ? page_management_flags_resources_it->second.uav
    : kInvalidShaderVisibleIndex;
  bindings.dirty_page_flags_uav = has_owned_resolve_resources
    ? resolve_resources_it->second.dirty_page_flags_uav
    : kInvalidShaderVisibleIndex;
  bindings.previous_shadow_caster_bounds_srv
    = state.pending_residency_resolve.valid
    ? state.pending_residency_resolve.previous_shadow_caster_bounds_srv
    : kInvalidShaderVisibleIndex;
  bindings.current_shadow_caster_bounds_srv
    = state.pending_residency_resolve.valid
    ? state.pending_residency_resolve.current_shadow_caster_bounds_srv
    : kInvalidShaderVisibleIndex;
  bindings.physical_page_metadata_srv = has_owned_resolve_resources
    ? resolve_resources_it->second.physical_page_metadata_srv
    : kInvalidShaderVisibleIndex;
  bindings.physical_page_metadata_uav = has_owned_resolve_resources
    ? resolve_resources_it->second.physical_page_metadata_uav
    : kInvalidShaderVisibleIndex;
  bindings.physical_page_lists_srv = has_owned_resolve_resources
    ? resolve_resources_it->second.physical_page_lists_srv
    : kInvalidShaderVisibleIndex;
  bindings.physical_page_lists_uav = has_owned_resolve_resources
    ? resolve_resources_it->second.physical_page_lists_uav
    : kInvalidShaderVisibleIndex;
  bindings.resolve_stats_uav = has_owned_resolve_resources
    ? resolve_resources_it->second.stats_uav
    : kInvalidShaderVisibleIndex;
  bindings.previous_light_view
    = state.pending_residency_resolve.valid
    ? state.pending_residency_resolve.previous_light_view
    : glm::mat4 { 1.0F };
  bindings.shadow_caster_bound_count
    = state.pending_residency_resolve.valid
    ? state.pending_residency_resolve.shadow_caster_bound_count
    : 0U;
  bindings.physical_page_capacity = has_owned_resolve_resources
    ? resolve_resources_it->second.physical_page_metadata_capacity
    : 0U;
  bindings.atlas_tiles_per_axis = physical_pool_config_.atlas_tiles_per_axis;
  bindings.reset_page_management_state
    = ((state.pending_residency_resolve.valid
          && state.pending_residency_resolve.reset_page_management_state)
        || (resolve_resources_it != view_resolve_resources_.end()
          && has_owned_resolve_resources
          && resolve_resources_it->second.physical_page_state_reset_pending))
    ? 1U
    : 0U;
  bindings.global_dirty_resident_contents
    = (state.pending_residency_resolve.valid
        && state.pending_residency_resolve.global_dirty_resident_contents)
    ? 1U
    : 0U;
  state.page_management_bindings = bindings;

  const bool has_authoritative_page_management_cache
    = (state.prev_frame.has_authoritative_page_management_state
        || state.current_frame.has_authoritative_page_management_state)
    && IsPublicationLiveState(state.lifecycle_state);
  const bool virtual_shadow_bindings_live
    = bindings.reset_page_management_state == 0U
    && has_authoritative_page_management_cache;

  CHECK_F(!virtual_shadow_bindings_live
      || IsPublicationLiveState(state.lifecycle_state),
    "VirtualShadowMapBackend: illegal live publication frame={} view={} "
    "lifecycle={} reset_page_management={} authoritative_prev={} "
    "authoritative_current={}",
    frame_sequence_.get(), view_id.get(),
    CacheLifecycleStateName(state.lifecycle_state),
    bindings.reset_page_management_state,
    state.prev_frame.has_authoritative_page_management_state,
    state.current_frame.has_authoritative_page_management_state);

  auto publication = state.frame_publication;
  publication.virtual_shadow_page_table_srv
    = virtual_shadow_bindings_live
    ? bindings.page_table_srv
    : kInvalidShaderVisibleIndex;
  publication.virtual_shadow_page_flags_srv
    = virtual_shadow_bindings_live
    ? bindings.page_flags_srv
    : kInvalidShaderVisibleIndex;

  publication.virtual_shadow_physical_page_metadata_srv
    = virtual_shadow_bindings_live
      && has_owned_resolve_resources
    ? resolve_resources_it->second.physical_page_metadata_srv
    : kInvalidShaderVisibleIndex;
  publication.virtual_shadow_physical_page_lists_srv
    = virtual_shadow_bindings_live
      && has_owned_resolve_resources
    ? resolve_resources_it->second.physical_page_lists_srv
    : kInvalidShaderVisibleIndex;
  publication.virtual_shadow_physical_pool_srv
    = virtual_shadow_bindings_live ? physical_pool_srv_
                                   : kInvalidShaderVisibleIndex;
  state.frame_publication = publication;
  if (!virtual_shadow_bindings_live) {
    LOG_F(INFO,
      "VirtualShadowMapBackend: frame={} view={} publication invalid "
      "cache_epoch={} view_generation={} lifecycle={} reset_page_management={} "
      "has_authoritative_cache={} bootstrap_feedback_complete={} "
      "stable_validation_pending={} preserve_until_next_raster={} "
      "owned_table={} owned_flags={} "
      "owned_resolve={}",
      frame_sequence_.get(), view_id.get(), state.cache_epoch,
      state.view_generation, CacheLifecycleStateName(state.lifecycle_state),
      bindings.reset_page_management_state,
      has_authoritative_page_management_cache, state.bootstrap_feedback_complete,
      state.stable_validation_pending,
      state.preserve_rendered_basis_until_next_raster,
      has_owned_page_table_resources,
      has_owned_page_flags_resources, has_owned_resolve_resources);
  }
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
  view_cache_.clear();
}

auto VirtualShadowMapBackend::PrepareDirectionalVirtualClipmapSetup(
  const engine::ViewConstants& view_constants,
  const float camera_viewport_width,
  const engine::DirectionalShadowCandidate& candidate,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const ViewCacheEntry* previous_state) const
  -> std::optional<DirectionalVirtualClipmapSetup>
{
  using namespace shadow_detail;

  if (!physical_pool_texture_) {
    return std::nullopt;
  }

  DirectionalVirtualClipmapSetup setup {};
  const auto camera_view_constants = view_constants.GetSnapshot();
  const glm::mat4 view_matrix = camera_view_constants.view_matrix;
  const glm::mat4 projection_matrix
    = view_constants.GetStableProjectionMatrix();
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

  std::array<glm::vec3, 8> full_frustum_world_points {};
  for (std::size_t i = 0U; i < clip_corners.size(); ++i) {
    const float far_corner_depth = std::max(1.0e-4F, -view_far_corners[i].z);
    const glm::vec3 view_ray = view_far_corners[i] / far_corner_depth;
    const glm::vec3 full_near_vs = view_ray * near_depth;
    const glm::vec3 full_far_vs = view_ray * far_depth;
    full_frustum_world_points[i] = TransformPoint(inv_view, full_near_vs);
    full_frustum_world_points[i + clip_corners.size()]
      = TransformPoint(inv_view, full_far_vs);
  }

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

  std::array<glm::vec3, 8> frustum_world_points {};
  for (std::size_t i = 0U; i < clip_corners.size(); ++i) {
    const float far_corner_depth = std::max(1.0e-4F, -view_far_corners[i].z);
    const glm::vec3 view_ray = view_far_corners[i] / far_corner_depth;
    const glm::vec3 clamped_near_vs = view_ray * effective_near_depth;
    const glm::vec3 clamped_far_vs = view_ray * effective_far_depth;
    frustum_world_points[i] = TransformPoint(inv_view, clamped_near_vs);
    frustum_world_points[i + clip_corners.size()]
      = TransformPoint(inv_view, clamped_far_vs);
  }

  setup.clip_level_count = physical_pool_config_.clip_level_count;
  setup.pages_per_axis = physical_pool_config_.virtual_pages_per_clip_axis;
  setup.pages_per_level = setup.pages_per_axis * setup.pages_per_axis;
  const glm::vec3 light_dir_to_surface
    = NormalizeOrFallback(candidate.direction_ws, glm::vec3(0.0F, -1.0F, 0.0F));
  const glm::vec3 light_dir_to_light = -light_dir_to_surface;
  glm::vec3 world_up = NormalizeOrFallback(candidate.basis_up_ws,
    glm::vec3(0.0F, 0.0F, 1.0F));
  if (std::abs(glm::dot(light_dir_to_light, world_up)) > 0.95F) {
    world_up = std::abs(glm::dot(light_dir_to_light, glm::vec3(0.0F, 0.0F, 1.0F)))
        > 0.95F
      ? glm::vec3(1.0F, 0.0F, 0.0F)
      : glm::vec3(0.0F, 0.0F, 1.0F);
  }

  const glm::vec3 camera_position = camera_view_constants.camera_position;
  const float authored_first_clip_end = ResolveClipEndDepth(
    candidate, 0U, std::max(near_depth, 0.0F), near_depth, far_depth);
  const float first_clip_half_extent_floor = far_depth
    / std::exp2(static_cast<float>(std::max(setup.clip_level_count, 1U) - 1U));
  const float desired_first_half_extent
    = std::max(std::max(authored_first_clip_end, first_clip_half_extent_floor),
      kMinClipSpan);
  const float desired_base_page_world = (2.0F * desired_first_half_extent)
    / std::max(1.0F, static_cast<float>(setup.pages_per_axis));
  const float base_page_world = QuantizeUpToPowerOfTwo(desired_base_page_world);
  const float base_half_extent = base_page_world
    * std::max(1.0F, static_cast<float>(setup.pages_per_axis)) * 0.5F;

  float largest_half_extent = 0.0F;
  for (std::uint32_t clip_index = 0U; clip_index < setup.clip_level_count;
    ++clip_index) {
    const float scale = std::exp2(static_cast<float>(clip_index));
    const float half_extent = base_half_extent * scale;
    setup.clip_page_world[clip_index] = base_page_world * scale;
    largest_half_extent = std::max(largest_half_extent, half_extent);
  }

  const glm::mat4 rot_view = glm::lookAtRH(
    glm::vec3(0.0F), glm::vec3(0.0F) + light_dir_to_surface, world_up);
  const glm::vec3 cam_rot_ls
    = glm::vec3(rot_view * glm::vec4(camera_position, 1.0F));

  // Basis stabilization is allowed to reuse the last validated rendered basis
  // even while warmup keeps page-management bindings non-live. Reusable
  // shading authority stays separately gated by
  // has_authoritative_page_management_state.
  const bool has_previous_rendered_provenance = previous_state != nullptr
    && previous_state->prev_frame.page_management_finalized
    && previous_state->prev_frame.rendered_frame_number >= 0;
  const engine::DirectionalVirtualShadowMetadata* previous_metadata = nullptr;
  if (has_previous_rendered_provenance
    && previous_state->prev_frame.directional_metadata.size() == 1U) {
    previous_metadata
      = &previous_state->prev_frame.directional_metadata.front();
  }

  const float snap_size = setup.clip_page_world[setup.clip_level_count - 1U];
  const float snapped_x = std::floor(cam_rot_ls.x / snap_size) * snap_size;
  const float snapped_y = std::floor(cam_rot_ls.y / snap_size) * snap_size;

  glm::vec3 light_eye_ls = glm::vec3(snapped_x, snapped_y,
    cam_rot_ls.z + largest_half_extent + kLightPullbackPadding);

  const glm::mat4 inv_rot_view = glm::inverse(rot_view);
  setup.light_eye = glm::vec3(inv_rot_view * glm::vec4(light_eye_ls, 1.0F));

  setup.light_view = glm::lookAtRH(
    setup.light_eye, setup.light_eye + light_dir_to_surface, world_up);

  float max_depth
    = -(largest_half_extent + kLightPullbackPadding) + largest_half_extent;
  float min_depth
    = -(largest_half_extent + kLightPullbackPadding) - largest_half_extent;
  [[maybe_unused]] const bool tightened
    = TightenDepthRangeWithShadowCasters(shadow_caster_bounds, setup.light_view,
      largest_half_extent, largest_half_extent, min_depth, max_depth);

  const float depth_padding
    = std::max(kMinShadowDepthPadding, largest_half_extent * 0.1F);
  setup.near_plane = std::max(0.1F, -max_depth - depth_padding);
  setup.far_plane
    = std::max(setup.near_plane + 1.0F, -min_depth + depth_padding);

  const bool previous_state_exists = previous_state != nullptr;
  const bool previous_metadata_exists = previous_metadata != nullptr;
  const bool previous_rendered_cache_exists = previous_metadata_exists
    && previous_state_exists && has_previous_rendered_provenance;
  bool force_previous_depth_basis_for_warmup = previous_rendered_cache_exists
    && previous_state != nullptr
    && (previous_state->preserve_rendered_basis_until_next_raster
      || previous_state->bootstrap_feedback_complete
      || previous_state->stable_validation_pending
      || previous_state->lifecycle_state
        == CacheLifecycleState::kWarmupRasterPending
      || previous_state->lifecycle_state
        == CacheLifecycleState::kWarmupFeedbackPending);
  auto depth_guardband_candidate = !force_previous_depth_basis_for_warmup
    && previous_rendered_cache_exists
    && EvaluateDirectionalDepthGuardband(*previous_metadata,
      std::span<const glm::vec3> {
        full_frustum_world_points.data(), full_frustum_world_points.size() },
      shadow_caster_bounds, largest_half_extent, largest_half_extent);

  if (force_previous_depth_basis_for_warmup || depth_guardband_candidate) {
    const auto previous_depth_range
      = shadow_detail::RecoverDirectionalVirtualDepthRange(*previous_metadata);
    if (previous_depth_range.valid) {
      const glm::vec3 previous_eye_ws
        = ExtractDirectionalLightEyeWs(previous_metadata->light_view);
      if (force_previous_depth_basis_for_warmup) {
        // A warmup/validation preservation path is required to keep the exact
        // last rasterized depth basis alive until a new raster replaces it.
        // Reconstructing a fresh look-at matrix from recovered parameters
        // reintroduces float jitter and defeats content reuse immediately.
        setup.light_eye = previous_eye_ws;
        setup.light_view = previous_metadata->light_view;
      } else {
        const glm::vec3 previous_eye_ls
          = glm::vec3(rot_view * glm::vec4(previous_eye_ws, 1.0F));
        light_eye_ls.z = previous_eye_ls.z;
        setup.light_eye
          = glm::vec3(inv_rot_view * glm::vec4(light_eye_ls, 1.0F));
        setup.light_view = glm::lookAtRH(
          setup.light_eye, setup.light_eye + light_dir_to_surface, world_up);
      }
      setup.near_plane = previous_depth_range.near_plane;
      setup.far_plane = previous_depth_range.far_plane;
    } else {
      force_previous_depth_basis_for_warmup = false;
      depth_guardband_candidate = false;
    }
  }

  const glm::vec3 camera_ls
    = glm::vec3(setup.light_view * glm::vec4(camera_position, 1.0F));
  const glm::mat4 depth_only_proj = glm::orthoRH_ZO(
    -1.0F, 1.0F, -1.0F, 1.0F, setup.near_plane, setup.far_plane);
  const float depth_scale = depth_only_proj[2][2];
  const float depth_bias = depth_only_proj[3][2];

  const auto flags = BuildShadowProductFlags(candidate.light_flags);
  setup.shadow_instance = engine::ShadowInstanceMetadata {
    .light_index = candidate.light_index,
    .payload_index = 0U,
    .domain = static_cast<std::uint32_t>(engine::ShadowDomain::kDirectional),
    .implementation_kind
    = static_cast<std::uint32_t>(engine::ShadowImplementationKind::kVirtual),
    .flags = flags,
  };

  setup.metadata.shadow_instance_index = 0U;
  setup.metadata.flags = flags;
  setup.metadata.constant_bias = candidate.bias;
  setup.metadata.normal_bias = candidate.normal_bias;
  setup.metadata.clip_level_count = setup.clip_level_count;
  setup.metadata.pages_per_axis = setup.pages_per_axis;
  setup.metadata.page_size_texels = physical_pool_config_.page_size_texels;
  setup.metadata.page_table_offset = 0U;
  setup.metadata.coarse_clip_mask
    = BuildDirectionalCoarseClipMask(setup.clip_level_count);
  setup.metadata.receiver_normal_bias_scale
    = directional_bias_settings_.receiver_normal_bias_scale;
  setup.metadata.receiver_constant_bias_scale
    = directional_bias_settings_.receiver_constant_bias_scale;
  setup.metadata.receiver_slope_bias_scale
    = directional_bias_settings_.receiver_slope_bias_scale;
  setup.metadata.raster_constant_bias_scale
    = directional_bias_settings_.raster_constant_bias_scale;
  setup.metadata.raster_slope_bias_scale
    = directional_bias_settings_.raster_slope_bias_scale;
  setup.metadata.reserved0 = 0U;
  setup.metadata.reserved1 = 0U;
  static bool logged_directional_bias_settings = false;
  if (!logged_directional_bias_settings) {
    LOG_F(INFO,
      "Directional VSM bias settings: constant_bias={} normal_bias={} "
      "receiver_normal_scale={} "
      "receiver_constant_scale={} receiver_slope_scale={} "
      "raster_constant_scale={} "
      "raster_slope_scale={}",
      setup.metadata.constant_bias, setup.metadata.normal_bias,
      setup.metadata.receiver_normal_bias_scale,
      setup.metadata.receiver_constant_bias_scale,
      setup.metadata.receiver_slope_bias_scale,
      setup.metadata.raster_constant_bias_scale,
      setup.metadata.raster_slope_bias_scale);
    logged_directional_bias_settings = true;
  }
  const float clip0_radius = std::max(base_page_world
      * std::max(1.0F, static_cast<float>(setup.pages_per_axis)) * 0.25F,
    1.0e-4F);
  const auto first_clipmap_level
    = static_cast<std::int32_t>(std::llround(std::log2(clip0_radius)));
  const float projection_scale_x
    = std::max(std::abs(projection_matrix[0][0]), 1.0e-6F);
  const float virtual_max_resolution_xy = std::max(1.0F,
    static_cast<float>(
      setup.pages_per_axis * physical_pool_config_.page_size_texels));
  const float viewport_width = std::max(camera_viewport_width, 1.0F);
  const float lod_scale = (0.5F / projection_scale_x)
    * (virtual_max_resolution_xy / viewport_width);
  const float selection_lod_bias
    = lod_scale > 1.0e-6F ? std::max(0.0F, std::log2(lod_scale)) : 0.0F;
  setup.metadata.clipmap_selection_world_origin_lod_bias
    = glm::vec4(camera_view_constants.camera_position, selection_lod_bias);
  setup.metadata.clipmap_receiver_origin_lod_bias = glm::vec4(
    0.0F, 0.0F, 0.0F, -std::log2(std::max(base_half_extent, 1.0e-4F)) - 0.5F);
  setup.metadata.clip_grid_origin_x_packed.fill(glm::ivec4(0));
  setup.metadata.clip_grid_origin_y_packed.fill(glm::ivec4(0));
  setup.metadata.light_view = setup.light_view;

  // Origin-snap hysteresis: When a previous stable grid origin exists, only
  // re-snap when the camera has moved at least one full page cell from the
  // cached origin center. This prevents floor() from toggling between
  // adjacent integers when the camera hovers at a page boundary, which
  // causes per-frame page-mapping instability and visible clipmap-edge
  // flickering.  UE5 applies equivalent hysteresis in its clipmap update.
  //
  // GUARD: The cached grid origins are only valid when the previous frame's
  // clip basis is structurally compatible with the current one.  If the light
  // view orientation, clip_level_count, pages_per_axis, page_size_texels, or
  // per-clip page_world changed, the cached integer coordinates are in a
  // different coordinate space and MUST be discarded.  This reuses the same
  // address-space compatibility predicate that
  // PopulateDirectionalPendingResolve uses to decide whether to reset page
  // management state.
  const bool have_previous_grid_origins = [&]() {
    if (previous_state == nullptr || !has_previous_rendered_provenance
      || !previous_state->prev_frame.has_cached_clip_grid_origins
      || previous_state->prev_frame.directional_metadata.size() != 1U) {
      return false;
    }
    const auto& prev_meta
      = previous_state->prev_frame.directional_metadata.front();
    // Check structural compatibility: level count, pages_per_axis,
    // page_size_texels, light view XY, and per-clip page_world.
    // setup.metadata already has these fields populated (lines above).
    // The per-clip origin_page_scale.z (page_world) is set from
    // setup.clip_page_world which is computed before this point.
    // We temporarily fill setup.metadata.clip_metadata[].origin_page_scale.z
    // just for the compatibility check — the full origin_page_scale will be
    // overwritten in the loop below.  This is safe because the z component
    // is independent of the origin xy that the loop computes.
    for (std::uint32_t ci = 0U; ci < setup.clip_level_count; ++ci) {
      setup.metadata.clip_metadata[ci].origin_page_scale.z
        = setup.clip_page_world[ci];
    }
    return IsDirectionalVirtualAddressSpaceCompatible(
      prev_meta, setup.metadata);
  }();

  for (std::uint32_t clip_index = 0U; clip_index < setup.clip_level_count;
    ++clip_index) {
    const float page_world = setup.clip_page_world[clip_index];
    const float half_extent = page_world
      * std::max(1.0F, static_cast<float>(setup.pages_per_axis)) * 0.5F;

    // Compute the candidate grid origin from the current camera position.
    const auto candidate_grid_x = static_cast<std::int32_t>(
      std::floor((camera_ls.x - half_extent) / page_world));
    const auto candidate_grid_y = static_cast<std::int32_t>(
      std::floor((camera_ls.y - half_extent) / page_world));

    if (have_previous_grid_origins) {
      // Hysteresis: keep the previous grid origin unless the camera has
      // moved far enough that the candidate differs by at least 1 cell.
      // When exactly at a page boundary, floor() can toggle between N and
      // N-1 due to floating-point rounding; sticking to the cached value
      // in that regime absorbs the jitter.
      const auto prev_x
        = previous_state->prev_frame.cached_clip_grid_origin_x[clip_index];
      const auto prev_y
        = previous_state->prev_frame.cached_clip_grid_origin_y[clip_index];

      // Continuous grid coordinate (before floor).  The fractional part
      // tells us how far the camera is past the snap boundary.
      const float cont_x = (camera_ls.x - half_extent) / page_world;
      const float cont_y = (camera_ls.y - half_extent) / page_world;

      // Accept the candidate only when the continuous coordinate is at
      // least half a page past the boundary implied by the cached origin.
      // This creates a dead-zone of ~1 page width around the current snap
      // point, preventing single-ULP jitter from re-triggering a snap.
      const float dist_x = cont_x - static_cast<float>(prev_x);
      const float dist_y = cont_y - static_cast<float>(prev_y);
      constexpr float kHysteresisMargin = 0.5F;
      const bool update_x
        = dist_x < -kHysteresisMargin || dist_x > (1.0F + kHysteresisMargin);
      const bool update_y
        = dist_y < -kHysteresisMargin || dist_y > (1.0F + kHysteresisMargin);

      setup.clip_grid_origin_x[clip_index]
        = update_x ? candidate_grid_x : prev_x;
      setup.clip_grid_origin_y[clip_index]
        = update_y ? candidate_grid_y : prev_y;
    } else {
      setup.clip_grid_origin_x[clip_index] = candidate_grid_x;
      setup.clip_grid_origin_y[clip_index] = candidate_grid_y;
    }

    setup.clip_origin_x[clip_index]
      = static_cast<float>(setup.clip_grid_origin_x[clip_index]) * page_world;
    setup.clip_origin_y[clip_index]
      = static_cast<float>(setup.clip_grid_origin_y[clip_index]) * page_world;
    setup.metadata.clip_metadata[clip_index].origin_page_scale
      = glm::vec4(setup.clip_origin_x[clip_index],
        setup.clip_origin_y[clip_index], page_world, depth_scale);
    setup.metadata.clip_metadata[clip_index].bias_reserved
      = glm::vec4(depth_bias, 0.0F, 0.0F, 0.0F);
    setup.metadata.clip_metadata[clip_index].clipmap_level_data
      = glm::ivec4(first_clipmap_level + static_cast<std::int32_t>(clip_index),
        static_cast<std::int32_t>(setup.clip_level_count - clip_index), 0, 0);
    const auto packed_index = clip_index / 4U;
    const auto packed_lane = clip_index % 4U;
    switch (packed_lane) {
    case 0U:
      setup.metadata.clip_grid_origin_x_packed[packed_index].x
        = setup.clip_grid_origin_x[clip_index];
      setup.metadata.clip_grid_origin_y_packed[packed_index].x
        = setup.clip_grid_origin_y[clip_index];
      break;
    case 1U:
      setup.metadata.clip_grid_origin_x_packed[packed_index].y
        = setup.clip_grid_origin_x[clip_index];
      setup.metadata.clip_grid_origin_y_packed[packed_index].y
        = setup.clip_grid_origin_y[clip_index];
      break;
    case 2U:
      setup.metadata.clip_grid_origin_x_packed[packed_index].z
        = setup.clip_grid_origin_x[clip_index];
      setup.metadata.clip_grid_origin_y_packed[packed_index].z
        = setup.clip_grid_origin_y[clip_index];
      break;
    default:
      setup.metadata.clip_grid_origin_x_packed[packed_index].w
        = setup.clip_grid_origin_x[clip_index];
      setup.metadata.clip_grid_origin_y_packed[packed_index].w
        = setup.clip_grid_origin_y[clip_index];
      break;
    }
  }

  {
    const float clip0_half_extent = setup.clip_page_world[0]
      * std::max(1.0F, static_cast<float>(setup.pages_per_axis)) * 0.5F;
    const glm::vec3 clip0_center_ls(setup.clip_origin_x[0] + clip0_half_extent,
      setup.clip_origin_y[0] + clip0_half_extent, camera_ls.z);
    const glm::vec3 clip0_center_ws = glm::vec3(
      glm::inverse(setup.light_view) * glm::vec4(clip0_center_ls, 1.0F));
    setup.metadata.clipmap_receiver_origin_lod_bias.x = clip0_center_ws.x;
    setup.metadata.clipmap_receiver_origin_lod_bias.y = clip0_center_ws.y;
    setup.metadata.clipmap_receiver_origin_lod_bias.z = clip0_center_ws.z;
  }

  (void)frustum_world_points;
  (void)full_frustum_world_points;
  (void)visible_receiver_bounds;

  setup.valid = true;
  return setup;
}

auto VirtualShadowMapBackend::IsDirectionalViewCacheCompatible(
  const DirectionalVirtualClipmapSetup& setup,
  const ViewCacheEntry* previous_state) const -> bool
{
  using namespace shadow_detail;

  if (!HasReusableRenderedCache(previous_state)
    || previous_state->prev_frame.directional_metadata.size() != 1U) {
    return false;
  }

  const auto& previous_metadata
    = previous_state->prev_frame.directional_metadata.front();
  const auto current_clipmap_cache_key = BuildClipmapCacheKey(setup.metadata);
  return AreClipmapCacheKeysEqual(previous_state->prev_frame.clipmap_cache_key,
           current_clipmap_cache_key)
    && IsDirectionalVirtualAddressSpaceCompatible(
      previous_metadata, setup.metadata);
}

auto VirtualShadowMapBackend::PopulateDirectionalPendingResolve(
  const DirectionalCacheReuseAssessment& assessment,
  const engine::ViewConstants& view_constants, ViewCacheEntry& state) -> void
{
  state.pending_residency_resolve = {};
  auto& pending_resolve = state.pending_residency_resolve;
  pending_resolve.valid = true;
  pending_resolve.has_fresh_pending_resolve_inputs = true;
  pending_resolve.reset_page_management_state
    = assessment.reset_page_management_state;
  pending_resolve.view_constants = view_constants;
  pending_resolve.previous_light_view = glm::mat4 { 1.0F };
  pending_resolve.global_dirty_resident_contents
    = assessment.global_dirty_resident_contents;

  if (assessment.compare_shadow_caster_bounds_on_gpu) {
    const auto bound_count
      = static_cast<std::uint32_t>(state.shadow_caster_bounds.size());
    auto previous_bounds_allocation
      = shadow_caster_bounds_buffer_.Allocate(bound_count);
    auto current_bounds_allocation
      = shadow_caster_bounds_buffer_.Allocate(bound_count);
    if (previous_bounds_allocation && current_bounds_allocation
      && previous_bounds_allocation->mapped_ptr != nullptr
      && current_bounds_allocation->mapped_ptr != nullptr) {
      std::memcpy(previous_bounds_allocation->mapped_ptr,
        assessment.previous_shadow_caster_bounds->data(),
        assessment.previous_shadow_caster_bounds->size() * sizeof(glm::vec4));
      std::memcpy(current_bounds_allocation->mapped_ptr,
        state.shadow_caster_bounds.data(),
        state.shadow_caster_bounds.size() * sizeof(glm::vec4));
      pending_resolve.previous_shadow_caster_bounds_srv
        = previous_bounds_allocation->srv;
      pending_resolve.current_shadow_caster_bounds_srv
        = current_bounds_allocation->srv;
      pending_resolve.shadow_caster_bound_count = bound_count;
      pending_resolve.previous_light_view
        = assessment.previous_metadata != nullptr
        ? assessment.previous_metadata->light_view
        : glm::mat4 { 1.0F };
    } else {
      pending_resolve.global_dirty_resident_contents = true;
    }
  }

  DCHECK_F(CanApplyPendingResolveToLiveBindings(state),
    "VirtualShadowMapBackend: freshly populated pending resolve packet must "
    "be eligible for live binding export");
}

auto VirtualShadowMapBackend::BuildDirectionalVirtualViewState(
  const ViewId view_id, const engine::ViewConstants& view_constants,
  const float camera_viewport_width,
  const engine::DirectionalShadowCandidate& candidate,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const ViewCacheEntry* previous_state, ViewCacheEntry& state) -> void
{
  RollForwardCacheHistory(previous_state, state);

  const auto clipmap_setup = PrepareDirectionalVirtualClipmapSetup(
    view_constants, camera_viewport_width, candidate, shadow_caster_bounds,
    visible_receiver_bounds, previous_state);
  if (!clipmap_setup.has_value() || !clipmap_setup->valid) {
    return;
  }

  const auto& setup = *clipmap_setup;
  InitializeDirectionalViewStateFromClipmapSetup(
    setup, shadow_caster_bounds, state);
  const auto reuse_assessment
    = AssessDirectionalCacheReuse(setup, previous_state, state);
  const auto prev_frame_before_invalidation = state.prev_frame;
  if (reuse_assessment.invalidate_rendered_cache_history) {
    InvalidateRenderedCacheHistory(state);
    SetLifecycleState(
      view_id, state, CacheLifecycleState::kInvalidated, "reuse_invalidated");
    state.bootstrap_feedback_complete = false;
    state.stable_validation_pending = false;
    state.preserve_rendered_basis_until_next_raster = false;
  }
  state.current_frame.is_uncached = reuse_assessment.current_view_is_uncached;
  PopulateDirectionalPendingResolve(reuse_assessment, view_constants, state);
  state.current_frame_requested_reset
    = state.pending_residency_resolve.reset_page_management_state;
  state.directional_virtual_metadata.push_back(setup.metadata);
  state.current_frame.has_authoritative_page_management_state = false;
  state.current_frame.clipmap_cache_key = BuildClipmapCacheKey(setup.metadata);
  state.current_frame.shadow_caster_content_hash
    = state.shadow_caster_content_hash;
  if (reuse_assessment.has_reusable_rendered_cache) {
    state.current_frame.rendered_frame_number
      = state.prev_frame.rendered_frame_number;
    state.current_frame.scheduled_frame_number
      = state.prev_frame.scheduled_frame_number;
  }
  state.current_frame.directional_metadata = state.directional_virtual_metadata;
  state.current_frame.cached_clip_grid_origin_x = setup.clip_grid_origin_x;
  state.current_frame.cached_clip_grid_origin_y = setup.clip_grid_origin_y;
  state.current_frame.has_cached_clip_grid_origins = true;
  const bool requires_rendered_basis_provenance_for_warmup
    = !state.current_frame_requested_reset
    && state.prev_frame.rendered_frame_number >= 0
    && (state.bootstrap_feedback_complete || state.stable_validation_pending
      || state.lifecycle_state == CacheLifecycleState::kWarmupRasterPending
      || state.lifecycle_state == CacheLifecycleState::kWarmupFeedbackPending);
  const bool preserve_previous_rendered_basis_for_immediate_reuse
    = !state.current_frame_requested_reset && previous_state != nullptr
    && previous_state->preserve_rendered_basis_until_next_raster
    && state.prev_frame.rendered_frame_number >= 0;
  CHECK_F(!requires_rendered_basis_provenance_for_warmup
      || (state.prev_frame.directional_metadata.size() == 1U
        && state.prev_frame.has_cached_clip_grid_origins),
    "VirtualShadowMapBackend: warmup validation is missing rendered basis "
    "provenance frame={} view={} lifecycle={} prev_rendered_frame={} "
    "cache_epoch={} view_generation={} metadata_count={} has_clip_origins={}",
    frame_sequence_.get(), view_id.get(),
    CacheLifecycleStateName(state.lifecycle_state),
    state.prev_frame.rendered_frame_number, state.cache_epoch,
    state.view_generation, state.prev_frame.directional_metadata.size(),
    state.prev_frame.has_cached_clip_grid_origins);
  const bool preserve_previous_rendered_basis_for_warmup
    = requires_rendered_basis_provenance_for_warmup
    && state.prev_frame.directional_metadata.size() == 1U;
  const bool preserve_previous_rendered_basis_for_publish
    = (preserve_previous_rendered_basis_for_warmup
        || preserve_previous_rendered_basis_for_immediate_reuse)
    && state.prev_frame.directional_metadata.size() == 1U;
  if (preserve_previous_rendered_basis_for_publish) {
    state.directional_virtual_metadata = state.prev_frame.directional_metadata;
    state.current_frame.directional_metadata = state.prev_frame.directional_metadata;
    state.current_frame.clipmap_cache_key = state.prev_frame.clipmap_cache_key;
    state.current_frame.cached_clip_grid_origin_x
      = state.prev_frame.cached_clip_grid_origin_x;
    state.current_frame.cached_clip_grid_origin_y
      = state.prev_frame.cached_clip_grid_origin_y;
    state.current_frame.has_cached_clip_grid_origins
      = state.prev_frame.has_cached_clip_grid_origins;
    LOG_F(INFO,
      "VirtualShadowMapBackend: frame={} view={} preserved rendered basis for "
      "{} prev_rendered_frame={} lifecycle={} cache_epoch={} "
      "view_generation={} preserve_until_next_raster={}",
      frame_sequence_.get(), view_id.get(), state.prev_frame.rendered_frame_number,
      preserve_previous_rendered_basis_for_immediate_reuse ? "immediate_reuse"
                                                           : "warmup",
      CacheLifecycleStateName(state.lifecycle_state), state.cache_epoch,
      state.view_generation, state.preserve_rendered_basis_until_next_raster);
  }
  CHECK_F(!preserve_previous_rendered_basis_for_publish
      || (state.current_frame.directional_metadata.size() == 1U
        && AreClipmapCacheKeysEqual(state.current_frame.clipmap_cache_key,
          state.prev_frame.clipmap_cache_key)),
    "VirtualShadowMapBackend: warmup publication failed to preserve rendered "
    "basis frame={} view={} prev_rendered_frame={} lifecycle={} "
    "cache_epoch={} view_generation={}",
    frame_sequence_.get(), view_id.get(), state.prev_frame.rendered_frame_number,
    CacheLifecycleStateName(state.lifecycle_state), state.cache_epoch,
    state.view_generation);
  if (state.current_frame_requested_reset) {
    SetLifecycleState(
      view_id, state, CacheLifecycleState::kResetPending, "publish_needs_reset");
    state.bootstrap_feedback_complete = false;
    state.stable_validation_pending = false;
    state.preserve_rendered_basis_until_next_raster = false;
  } else if (state.lifecycle_state == CacheLifecycleState::kCleared) {
    state.current_frame.rendered_frame_number
      = state.prev_frame.rendered_frame_number;
    state.current_frame.scheduled_frame_number
      = state.prev_frame.scheduled_frame_number;
    SetLifecycleState(view_id, state, CacheLifecycleState::kWarmupRasterPending,
      "publish_after_clear");
  } else if (state.bootstrap_feedback_complete) {
    state.current_frame.rendered_frame_number
      = state.prev_frame.rendered_frame_number;
    state.current_frame.scheduled_frame_number
      = state.prev_frame.scheduled_frame_number;
    SetLifecycleState(view_id, state, CacheLifecycleState::kWarmupRasterPending,
      "publish_after_reset_barrier");
  } else if (reuse_assessment.has_reusable_rendered_cache) {
    SetLifecycleState(view_id, state,
      reuse_assessment.global_dirty_resident_contents
          || reuse_assessment.compare_shadow_caster_bounds_on_gpu
        ? CacheLifecycleState::kDirty
        : CacheLifecycleState::kValid,
      reuse_assessment.global_dirty_resident_contents
          || reuse_assessment.compare_shadow_caster_bounds_on_gpu
        ? "publish_reuse_dirty"
        : "publish_reuse_valid");
  } else if (state.lifecycle_state == CacheLifecycleState::kRetired) {
    SetLifecycleState(
      view_id, state, CacheLifecycleState::kResetPending, "publish_from_retired");
  }

  const auto& extracted_feedback = state.extracted_feedback;
  LOG_F(INFO,
    "VirtualShadowMapBackend: frame={} view={} reuse prev_cache_valid={} "
    "prev_uncached={} prev_rendered_frame={} prev_scheduled_frame={} "
    "current_uncached={} reset_page_management={} invalidate_history={} "
    "bootstrap_feedback_complete={} stable_validation_pending={} "
    "preserve_until_next_raster={} "
    "compare_bounds={} global_dirty={} reusable_rendered_cache={} "
    "prev_metadata={} clipmap_key_match={} address_space_compatible={} "
    "content_compatible={} content_mismatch_clip={} "
    "shadow_content_changed={} bounds_count_match={} "
    "extracted_schedule_frame={} "
    "extracted_schedule_pages={} extracted_stats_frame={} "
    "scheduled_raster_pages={} rasterized_pages={} cached_transitions={} "
    "allocated={} requested={} requires_schedule={} resident_dirty={} "
    "resident_clean={} available={}",
    frame_sequence_.get(), view_id.get(),
    prev_frame_before_invalidation.has_authoritative_page_management_state,
    prev_frame_before_invalidation.is_uncached,
    prev_frame_before_invalidation.rendered_frame_number,
    prev_frame_before_invalidation.scheduled_frame_number,
    reuse_assessment.current_view_is_uncached,
    reuse_assessment.reset_page_management_state,
    reuse_assessment.invalidate_rendered_cache_history,
    state.bootstrap_feedback_complete, state.stable_validation_pending,
    state.preserve_rendered_basis_until_next_raster,
    reuse_assessment.compare_shadow_caster_bounds_on_gpu,
    reuse_assessment.global_dirty_resident_contents,
    reuse_assessment.has_reusable_rendered_cache,
    reuse_assessment.previous_metadata != nullptr,
    reuse_assessment.clipmap_key_matches,
    reuse_assessment.address_space_compatible,
    reuse_assessment.content_compatible,
    reuse_assessment.first_content_mismatch_clip_index,
    reuse_assessment.shadow_content_changed,
    reuse_assessment.shadow_bounds_count_match,
    extracted_feedback.scheduled_source_sequence.get(),
    extracted_feedback.has_schedule_feedback
      ? extracted_feedback.scheduled_page_count
      : 0U,
    extracted_feedback.resolve_stats_source_sequence.get(),
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.scheduled_raster_page_count
      : 0U,
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.rasterized_page_count
      : 0U,
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.cached_page_transition_count
      : 0U,
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.allocated_page_count
      : 0U,
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.requested_page_count
      : 0U,
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.pages_requiring_schedule_count
      : 0U,
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.resident_dirty_page_count
      : 0U,
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.resident_clean_page_count
      : 0U,
    extracted_feedback.has_resolve_stats_feedback
      ? extracted_feedback.resolve_stats.available_page_list_count
      : 0U);
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

auto VirtualShadowMapBackend::EnsureViewPageManagementPageTableResources(
  const ViewId view_id, const std::uint32_t required_entry_count)
  -> ViewStructuredWordBufferResources*
{
  if (required_entry_count == 0U) {
    return nullptr;
  }

  auto [it, _]
    = view_page_management_page_table_resources_.try_emplace(view_id);
  auto& resources = it->second;
  const auto view_generation = GetOrCreateViewGeneration(view_id);
  const bool ownership_matches = resources.cache_epoch == cache_epoch_
    && resources.view_generation == view_generation;
  if (resources.gpu_buffer
    && required_entry_count <= resources.entry_capacity && ownership_matches) {
    resources.cache_epoch = cache_epoch_;
    resources.view_generation = view_generation;
    return &resources;
  }
  if (!ownership_matches) {
    resources = {};
  }

  if (required_entry_count > kMaxPersistentPageTableEntries) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: view {} requested {} page-management "
      "page-table "
      "entries but the persistent capacity is only {}",
      view_id.get(), required_entry_count, kMaxPersistentPageTableEntries);
    return nullptr;
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
    .debug_name = "VirtualShadowMapBackend.PageManagementPageTable",
  };
  resources.gpu_buffer = gfx_->CreateBuffer(gpu_desc);
  if (!resources.gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create page-management page table "
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
      "VirtualShadowMapBackend: failed to allocate page-management page-table "
      "SRV for view {}",
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
      "VirtualShadowMapBackend: failed to allocate page-management page-table "
      "UAV for view {}",
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

  resources.entry_capacity = kMaxPersistentPageTableEntries;
  resources.cache_epoch = cache_epoch_;
  resources.view_generation = view_generation;
  return &resources;
}

auto VirtualShadowMapBackend::EnsureViewPageManagementPageFlagResources(
  const ViewId view_id, const std::uint32_t required_entry_count)
  -> ViewStructuredWordBufferResources*
{
  if (required_entry_count == 0U) {
    return nullptr;
  }

  auto [it, _]
    = view_page_management_page_flags_resources_.try_emplace(view_id);
  auto& resources = it->second;
  const auto view_generation = GetOrCreateViewGeneration(view_id);
  const bool ownership_matches = resources.cache_epoch == cache_epoch_
    && resources.view_generation == view_generation;
  if (resources.gpu_buffer
    && required_entry_count <= resources.entry_capacity && ownership_matches) {
    resources.cache_epoch = cache_epoch_;
    resources.view_generation = view_generation;
    return &resources;
  }
  if (!ownership_matches) {
    resources = {};
  }

  if (required_entry_count > kMaxPersistentPageTableEntries) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: view {} requested {} page-management page-flag "
      "entries but the persistent capacity is only {}",
      view_id.get(), required_entry_count, kMaxPersistentPageTableEntries);
    return nullptr;
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
    .debug_name = "VirtualShadowMapBackend.PageManagementPageFlags",
  };
  resources.gpu_buffer = gfx_->CreateBuffer(gpu_desc);
  if (!resources.gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create page-management page flags "
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
      "VirtualShadowMapBackend: failed to allocate page-management page-flags "
      "SRV for view {}",
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
      "VirtualShadowMapBackend: failed to allocate page-management page-flags "
      "UAV for view {}",
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

  resources.entry_capacity = kMaxPersistentPageTableEntries;
  resources.cache_epoch = cache_epoch_;
  resources.view_generation = view_generation;
  return &resources;
}

auto VirtualShadowMapBackend::EnsureViewResolveResources(const ViewId view_id)
  -> ViewResolveResources*
{
  const auto required_physical_list_capacity
    = physical_pool_config_.physical_tile_capacity * 4U;

  auto [it, _] = view_resolve_resources_.try_emplace(view_id);
  auto& resources = it->second;
  const auto view_generation = GetOrCreateViewGeneration(view_id);
  const bool ownership_matches = resources.cache_epoch == cache_epoch_
    && resources.view_generation == view_generation;
  if (resources.stats_gpu_buffer && resources.physical_page_metadata_gpu_buffer
    && resources.dirty_page_flags_gpu_buffer
    && resources.physical_page_lists_gpu_buffer
    && physical_pool_config_.physical_tile_capacity
      <= resources.physical_page_metadata_capacity
    && required_physical_list_capacity
      <= resources.physical_page_lists_capacity
    && ownership_matches) {
    resources.cache_epoch = cache_epoch_;
    resources.view_generation = view_generation;
    return &resources;
  }
  if (!ownership_matches) {
    resources = {};
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

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

  const auto dirty_page_flags_capacity
    = static_cast<std::uint64_t>(physical_pool_config_.physical_tile_capacity)
    * 3ULL;
  const auto dirty_page_flags_bytes
    = dirty_page_flags_capacity * sizeof(std::uint32_t);
  const graphics::BufferDesc dirty_page_flags_gpu_desc {
    .size_bytes = dirty_page_flags_bytes,
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowMapBackend.DirtyPageFlags",
  };
  resources.dirty_page_flags_gpu_buffer
    = gfx_->CreateBuffer(dirty_page_flags_gpu_desc);
  if (!resources.dirty_page_flags_gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create dirty-page flags "
      "buffer for view {}",
      view_id.get());
    return nullptr;
  }
  registry.Register(resources.dirty_page_flags_gpu_buffer);

  auto dirty_page_flags_uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!dirty_page_flags_uav_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate dirty-page flags "
      "UAV for view {}",
      view_id.get());
    return nullptr;
  }
  resources.dirty_page_flags_uav
    = allocator.GetShaderVisibleIndex(dirty_page_flags_uav_handle);

  graphics::BufferViewDescription dirty_page_flags_uav_desc;
  dirty_page_flags_uav_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_UAV;
  dirty_page_flags_uav_desc.visibility
    = graphics::DescriptorVisibility::kShaderVisible;
  dirty_page_flags_uav_desc.range = { 0U, dirty_page_flags_bytes };
  dirty_page_flags_uav_desc.stride = sizeof(std::uint32_t);
  registry.RegisterView(*resources.dirty_page_flags_gpu_buffer,
    std::move(dirty_page_flags_uav_handle), dirty_page_flags_uav_desc);

  const auto physical_page_metadata_bytes
    = static_cast<std::uint64_t>(physical_pool_config_.physical_tile_capacity)
    * sizeof(renderer::VirtualShadowPhysicalPageMetadata);
  const graphics::BufferDesc physical_page_metadata_gpu_desc {
    .size_bytes = physical_page_metadata_bytes,
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowMapBackend.PhysicalPageMetadata",
  };
  resources.physical_page_metadata_gpu_buffer
    = gfx_->CreateBuffer(physical_page_metadata_gpu_desc);
  if (!resources.physical_page_metadata_gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create physical-page metadata "
      "buffer for view {}",
      view_id.get());
    return nullptr;
  }
  registry.Register(resources.physical_page_metadata_gpu_buffer);

  auto physical_page_metadata_srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!physical_page_metadata_srv_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate physical-page metadata "
      "SRV for view {}",
      view_id.get());
    return nullptr;
  }
  resources.physical_page_metadata_srv
    = allocator.GetShaderVisibleIndex(physical_page_metadata_srv_handle);

  graphics::BufferViewDescription physical_page_metadata_srv_desc;
  physical_page_metadata_srv_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_SRV;
  physical_page_metadata_srv_desc.visibility
    = graphics::DescriptorVisibility::kShaderVisible;
  physical_page_metadata_srv_desc.range = { 0U, physical_page_metadata_bytes };
  physical_page_metadata_srv_desc.stride
    = sizeof(renderer::VirtualShadowPhysicalPageMetadata);
  registry.RegisterView(*resources.physical_page_metadata_gpu_buffer,
    std::move(physical_page_metadata_srv_handle),
    physical_page_metadata_srv_desc);

  auto physical_page_metadata_uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!physical_page_metadata_uav_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate physical-page metadata "
      "UAV for view {}",
      view_id.get());
    return nullptr;
  }
  resources.physical_page_metadata_uav
    = allocator.GetShaderVisibleIndex(physical_page_metadata_uav_handle);

  graphics::BufferViewDescription physical_page_metadata_uav_desc;
  physical_page_metadata_uav_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_UAV;
  physical_page_metadata_uav_desc.visibility
    = graphics::DescriptorVisibility::kShaderVisible;
  physical_page_metadata_uav_desc.range = { 0U, physical_page_metadata_bytes };
  physical_page_metadata_uav_desc.stride
    = sizeof(renderer::VirtualShadowPhysicalPageMetadata);
  registry.RegisterView(*resources.physical_page_metadata_gpu_buffer,
    std::move(physical_page_metadata_uav_handle),
    physical_page_metadata_uav_desc);

  const auto physical_page_lists_capacity = required_physical_list_capacity;
  const auto physical_page_lists_bytes
    = static_cast<std::uint64_t>(physical_page_lists_capacity)
    * sizeof(renderer::VirtualShadowPhysicalPageListEntry);
  const graphics::BufferDesc physical_page_lists_gpu_desc {
    .size_bytes = physical_page_lists_bytes,
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowMapBackend.PhysicalPageLists",
  };
  resources.physical_page_lists_gpu_buffer
    = gfx_->CreateBuffer(physical_page_lists_gpu_desc);
  if (!resources.physical_page_lists_gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create physical-page lists "
      "buffer for view {}",
      view_id.get());
    return nullptr;
  }
  registry.Register(resources.physical_page_lists_gpu_buffer);

  auto physical_page_lists_srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!physical_page_lists_srv_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate physical-page lists "
      "SRV for view {}",
      view_id.get());
    return nullptr;
  }
  resources.physical_page_lists_srv
    = allocator.GetShaderVisibleIndex(physical_page_lists_srv_handle);

  graphics::BufferViewDescription physical_page_lists_srv_desc;
  physical_page_lists_srv_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_SRV;
  physical_page_lists_srv_desc.visibility
    = graphics::DescriptorVisibility::kShaderVisible;
  physical_page_lists_srv_desc.range = { 0U, physical_page_lists_bytes };
  physical_page_lists_srv_desc.stride
    = sizeof(renderer::VirtualShadowPhysicalPageListEntry);
  registry.RegisterView(*resources.physical_page_lists_gpu_buffer,
    std::move(physical_page_lists_srv_handle), physical_page_lists_srv_desc);

  auto physical_page_lists_uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!physical_page_lists_uav_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate physical-page lists "
      "UAV for view {}",
      view_id.get());
    return nullptr;
  }
  resources.physical_page_lists_uav
    = allocator.GetShaderVisibleIndex(physical_page_lists_uav_handle);

  graphics::BufferViewDescription physical_page_lists_uav_desc;
  physical_page_lists_uav_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_UAV;
  physical_page_lists_uav_desc.visibility
    = graphics::DescriptorVisibility::kShaderVisible;
  physical_page_lists_uav_desc.range = { 0U, physical_page_lists_bytes };
  physical_page_lists_uav_desc.stride
    = sizeof(renderer::VirtualShadowPhysicalPageListEntry);
  registry.RegisterView(*resources.physical_page_lists_gpu_buffer,
    std::move(physical_page_lists_uav_handle), physical_page_lists_uav_desc);

  resources.physical_page_metadata_capacity
    = physical_pool_config_.physical_tile_capacity;
  resources.dirty_page_flags_capacity
    = static_cast<std::uint32_t>(dirty_page_flags_capacity);
  resources.physical_page_lists_capacity = physical_page_lists_capacity;
  resources.physical_page_state_reset_pending = true;
  resources.cache_epoch = cache_epoch_;
  resources.view_generation = view_generation;
  return &resources;
}

} // namespace oxygen::renderer::internal
