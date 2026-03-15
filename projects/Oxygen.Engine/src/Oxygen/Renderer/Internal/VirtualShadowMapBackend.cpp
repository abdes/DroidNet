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
constexpr std::uint32_t kMaxPersistentPageTableEntries
  = 64U * 64U * oxygen::engine::kMaxVirtualDirectionalClipLevels;
constexpr std::uint64_t kMaxRequestFeedbackAgeFrames
  = oxygen::frame::kFramesInFlight.get() + 1U;
constexpr std::uint64_t kMaxResolvedRasterScheduleAgeFrames
  = oxygen::frame::kFramesInFlight.get() + 1U;
constexpr std::uint32_t kCoarseBackboneGuardPages = 1U;
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

[[nodiscard]] auto ResolveCoarseSafetyMaxPagesPerAxis(
  const std::uint32_t physical_tile_capacity) -> std::uint32_t
{
  const auto fitted_pages = static_cast<std::uint32_t>(std::floor(
    std::sqrt(static_cast<float>(std::max(1U, physical_tile_capacity)))));
  return std::max(1U, fitted_pages);
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

[[nodiscard]] auto PackPageTableEntry(const std::uint32_t tile_x,
  const std::uint32_t tile_y, const std::uint32_t fallback_lod_offset = 0U,
  const bool current_lod_valid = true, const bool any_lod_valid = true,
  const bool requested_this_frame = true) -> std::uint32_t
{
  return oxygen::renderer::PackVirtualShadowPageTableEntry(tile_x, tile_y,
    fallback_lod_offset, current_lod_valid, any_lod_valid,
    requested_this_frame);
}

[[nodiscard]] auto CountMappedPagesInClip(
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index, const std::uint32_t pages_per_level)
  -> std::uint32_t
{
  const auto clip_begin = static_cast<std::size_t>(clip_index)
    * static_cast<std::size_t>(pages_per_level);
  if (clip_begin >= page_table_entries.size()) {
    return 0U;
  }

  const auto clip_end = std::min(page_table_entries.size(),
    clip_begin + static_cast<std::size_t>(pages_per_level));
  return static_cast<std::uint32_t>(std::count_if(
    page_table_entries.begin() + static_cast<std::ptrdiff_t>(clip_begin),
    page_table_entries.begin() + static_cast<std::ptrdiff_t>(clip_end),
    [](const std::uint32_t entry) {
      return entry != 0U
        && oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(entry);
    }));
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

auto AppendDirtyResidentFlagsForBound(const glm::vec4& bound,
  const glm::mat4& light_view,
  const std::array<float, oxygen::engine::kMaxVirtualDirectionalClipLevels>&
    clip_page_world,
  const std::uint32_t clip_level_count, const std::uint32_t page_flags,
  std::unordered_map<std::uint64_t, std::uint32_t>& dirty_page_flags) -> void
{
  std::unordered_set<std::uint64_t> dirty_pages_for_bound {};
  AppendDirtyResidentKeysForBound(
    bound, light_view, clip_page_world, clip_level_count, dirty_pages_for_bound);
  for (const auto resident_key : dirty_pages_for_bound) {
    dirty_page_flags[resident_key] |= page_flags;
  }
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
  std::ranges::stable_sort(order, [&](const std::size_t lhs_index,
                                    const std::size_t rhs_index) {
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

  for (std::size_t sorted_index = 0U; sorted_index < order.size(); ++sorted_index) {
    const auto source_index = order[sorted_index];
    result.bounds[sorted_index] = bounds[source_index];
  }

  return result;
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
  for (auto& [_, resources] : view_resolve_resources_) {
    if (resources.physical_page_metadata_upload_buffer
      && resources.mapped_physical_page_metadata_upload != nullptr) {
      resources.physical_page_metadata_upload_buffer->UnMap();
      resources.mapped_physical_page_metadata_upload = nullptr;
    }
    if (resources.physical_page_lists_upload_buffer
      && resources.mapped_physical_page_lists_upload != nullptr) {
      resources.physical_page_lists_upload_buffer->UnMap();
      resources.mapped_physical_page_lists_upload = nullptr;
    }
  }
  ReleasePhysicalPool();
}

auto VirtualShadowMapBackend::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  frame_sequence_ = sequence;
  frame_slot_ = slot;
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
  const std::uint64_t shadow_caster_content_hash)
  -> ShadowFramePublication
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

  const auto canonical_shadow_caster_input
    = CanonicalizeShadowCasterInput(shadow_caster_bounds);
  const auto& canonical_shadow_caster_bounds
    = canonical_shadow_caster_input.bounds;
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
  const auto* page_management_table_resources
    = EnsureViewPageManagementPageTableResources(
      view_id, static_cast<std::uint32_t>(state.page_table_entries.size()));
  const auto* page_management_flag_resources
    = EnsureViewPageManagementPageFlagResources(
      view_id, static_cast<std::uint32_t>(state.page_flags_entries.size()));
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
    LOG_F(INFO,
      "VirtualShadowMapBackend: frame={} view={} publish shadow_instance "
      "domain={} impl={} flags=0x{:x} payload={} light={} clips={} pages={} "
      "mapped_pages={} pending_raster_pages={}",
      frame_sequence_.get(), view_id.get(),
      published_shadow_instances.front().domain,
      published_shadow_instances.front().implementation_kind,
      published_shadow_instances.front().flags,
      published_shadow_instances.front().payload_index,
      published_shadow_instances.front().light_index,
      !published_directional_virtual_metadata.empty()
        ? published_directional_virtual_metadata.front().clip_level_count
        : 0U,
      !published_directional_virtual_metadata.empty()
        ? published_directional_virtual_metadata.front().pages_per_axis
        : 0U,
      state.resolve_stats.mapped_page_count,
      state.resolve_stats.pending_raster_page_count);
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

auto VirtualShadowMapBackend::MarkRendered(
  const ViewId view_id, const bool rendered_page_work) -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    return;
  }

  // A zero-page frame must not bootstrap cache history. On first-scene startup
  // that would turn seed-only page-management buffers into "reusable" history
  // and expose garbage outside the real scene bounds on the next frame.
  if (!rendered_page_work && !it->second.has_rendered_cache_history) {
    RefreshViewExports(view_id, it->second);
    return;
  }

  // Live residency is GPU-owned now. Once a view has genuinely rasterized at
  // least one page, later zero-work steady-state frames may preserve that
  // cache history without forcing another bootstrap/reset.
  it->second.has_rendered_cache_history = true;
  RefreshViewExports(view_id, it->second);
}

auto VirtualShadowMapBackend::ResolveCurrentFrame(const ViewId view_id) -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    return;
  }

  auto& state = it->second;
  if (!state.pending_residency_resolve.valid || !state.pending_residency_resolve.dirty) {
    return;
  }

  if (state.pending_residency_resolve.reset_page_management_state) {
    StagePageManagementSeedUpload(view_id, state);
  }
  StageDirtyResidentPageUpload(view_id, state);
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
    && page_management_table_resources_it->second.gpu_buffer;
  const bool has_page_management_flags_resources
    = page_management_flags_resources_it
      != view_page_management_page_flags_resources_.end()
    && page_management_flags_resources_it->second.gpu_buffer;
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  const auto dirty_resident_page_resources_it
    = view_dirty_resident_page_resources_.find(view_id);
  const bool has_resolve_resources
    = resolve_resources_it != view_resolve_resources_.end();
  const bool has_dirty_resident_page_resources
    = dirty_resident_page_resources_it != view_dirty_resident_page_resources_.end()
    && dirty_resident_page_resources_it->second.gpu_buffer;
  if (!has_page_management_table_resources
    && !has_page_management_flags_resources && !has_resolve_resources
    && !has_dirty_resident_page_resources) {
    return;
  }
  if (has_page_management_table_resources) {
    auto& page_management_table_resources
      = page_management_table_resources_it->second;
    if (!recorder.IsResourceTracked(*page_management_table_resources.gpu_buffer)) {
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
    if (!recorder.IsResourceTracked(*page_management_flags_resources.gpu_buffer)) {
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
      if (resolve_resources.physical_page_state_reset_pending
        && resolve_resources.physical_page_metadata_upload_buffer) {
        if (!recorder.IsResourceTracked(
              *resolve_resources.physical_page_metadata_upload_buffer)) {
          recorder.BeginTrackingResourceState(
            *resolve_resources.physical_page_metadata_upload_buffer,
            graphics::ResourceStates::kCopySource, false);
        }
        recorder.RequireResourceState(
          *resolve_resources.physical_page_metadata_gpu_buffer,
          graphics::ResourceStates::kCopyDest);
        recorder.FlushBarriers();
        recorder.CopyBuffer(
          *resolve_resources.physical_page_metadata_gpu_buffer, 0U,
          *resolve_resources.physical_page_metadata_upload_buffer, 0U,
          static_cast<std::size_t>(
            resolve_resources.physical_page_metadata_capacity)
            * sizeof(renderer::VirtualShadowPhysicalPageMetadata));
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
      if (resolve_resources.physical_page_state_reset_pending
        && resolve_resources.physical_page_lists_upload_buffer) {
        if (!recorder.IsResourceTracked(
              *resolve_resources.physical_page_lists_upload_buffer)) {
          recorder.BeginTrackingResourceState(
            *resolve_resources.physical_page_lists_upload_buffer,
            graphics::ResourceStates::kCopySource, false);
        }
        recorder.RequireResourceState(
          *resolve_resources.physical_page_lists_gpu_buffer,
          graphics::ResourceStates::kCopyDest);
        recorder.FlushBarriers();
        recorder.CopyBuffer(*resolve_resources.physical_page_lists_gpu_buffer,
          0U, *resolve_resources.physical_page_lists_upload_buffer, 0U,
          static_cast<std::size_t>(resolve_resources.physical_page_lists_capacity)
            * sizeof(renderer::VirtualShadowPhysicalPageListEntry));
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
    resolve_resources.physical_page_state_reset_pending = false;
  }
  if (has_dirty_resident_page_resources) {
    auto& dirty_resources = dirty_resident_page_resources_it->second;
    if (!recorder.IsResourceTracked(*dirty_resources.gpu_buffer)) {
      recorder.BeginTrackingResourceState(*dirty_resources.gpu_buffer,
        graphics::ResourceStates::kCommon, true);
    }
    if (dirty_resources.upload_pending && dirty_resources.upload_buffer) {
      if (!recorder.IsResourceTracked(*dirty_resources.upload_buffer)) {
        recorder.BeginTrackingResourceState(*dirty_resources.upload_buffer,
          graphics::ResourceStates::kCopySource, false);
      }
      if (dirty_resources.upload_count > 0U) {
        recorder.RequireResourceState(*dirty_resources.gpu_buffer,
          graphics::ResourceStates::kCopyDest);
        recorder.FlushBarriers();
        recorder.CopyBuffer(*dirty_resources.gpu_buffer, 0U,
          *dirty_resources.upload_buffer, 0U,
          static_cast<std::size_t>(dirty_resources.upload_count)
            * sizeof(renderer::VirtualShadowDirtyResidentPageEntry));
      }
      dirty_resources.upload_pending = false;
    }
    recorder.RequireResourceState(*dirty_resources.gpu_buffer,
      graphics::ResourceStates::kShaderResource);
  }
  recorder.FlushBarriers();
}

auto VirtualShadowMapBackend::PreparePageManagementOutputsForGpuWrite(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  const auto page_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
  if (page_table_resources_it
      == view_page_management_page_table_resources_.end()
    || page_flags_resources_it
      == view_page_management_page_flags_resources_.end()
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

auto VirtualShadowMapBackend::EnsureCoherenceReadbackBuffers(
  CoherenceReadbackSlot& slot, const std::uint64_t size_bytes) -> bool
{
  if (slot.page_table_readback && slot.page_flags_readback) {
    return true;
  }

  const graphics::BufferDesc desc {
    .size_bytes = size_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kReadBack,
    .debug_name = "VirtualShadowMapBackend.CoherenceReadback",
  };

  slot.page_table_readback = gfx_->CreateBuffer(desc);
  if (!slot.page_table_readback) {
    return false;
  }
  slot.mapped_page_table = static_cast<const std::uint32_t*>(
    slot.page_table_readback->Map(0U, size_bytes));
  if (!slot.mapped_page_table) {
    return false;
  }

  slot.page_flags_readback = gfx_->CreateBuffer(desc);
  if (!slot.page_flags_readback) {
    return false;
  }
  slot.mapped_page_flags = static_cast<const std::uint32_t*>(
    slot.page_flags_readback->Map(0U, size_bytes));
  if (!slot.mapped_page_flags) {
    return false;
  }

  return true;
}

auto VirtualShadowMapBackend::CheckCoherenceReadback(
  CoherenceReadbackSlot& slot) -> void
{
  if (!slot.pending || !slot.mapped_page_table || !slot.mapped_page_flags) {
    return;
  }
  slot.pending = false;

  const auto entry_count = slot.entry_count;
  std::uint32_t table_mismatches = 0U;
  std::uint32_t exact_flags_mismatches = 0U;
  std::uint32_t structural_flags_mismatches = 0U;
  std::uint32_t semantic_only_flags_mismatches = 0U;
  std::uint32_t table_gpu_zero_cpu_nonzero = 0U;
  std::uint32_t table_gpu_nonzero_cpu_zero = 0U;
  std::uint32_t table_both_nonzero_differ = 0U;
  std::uint32_t current_without_allocated = 0U;
  std::uint32_t current_without_used = 0U;
  std::uint32_t fallback_with_base_flags = 0U;
  std::uint32_t fallback_with_hierarchy_flags = 0U;
  std::uint32_t current_page_count = 0U;
  std::uint32_t fallback_alias_count = 0U;
  constexpr std::uint32_t kPagesPerLevel = 64U * 64U;

  for (std::uint32_t i = 0U; i < entry_count; ++i) {
    const auto gpu_val = slot.mapped_page_table[i];
    const auto cpu_val = slot.cpu_page_table_snapshot[i];
    if (gpu_val != cpu_val) {
      ++table_mismatches;
      if (gpu_val == 0U && cpu_val != 0U) {
        ++table_gpu_zero_cpu_nonzero;
      } else if (gpu_val != 0U && cpu_val == 0U) {
        ++table_gpu_nonzero_cpu_zero;
      } else {
        ++table_both_nonzero_differ;
      }
    }
    const auto gpu_flags = slot.mapped_page_flags[i];
    const auto cpu_flags = slot.cpu_page_flags_snapshot[i];
    if (gpu_flags != cpu_flags) {
      ++exact_flags_mismatches;
      if (renderer::VirtualShadowPageFlagsStructurallyEqual(
            gpu_flags, cpu_flags)) {
        ++semantic_only_flags_mismatches;
      }
    }
    if (!renderer::VirtualShadowPageFlagsStructurallyEqual(
          gpu_flags, cpu_flags)) {
      ++structural_flags_mismatches;
    }
    const bool current_lod_valid
      = renderer::VirtualShadowPageTableEntryHasCurrentLod(gpu_val);
    const bool any_lod_valid
      = renderer::VirtualShadowPageTableEntryHasAnyLod(gpu_val);
    if (current_lod_valid) {
      ++current_page_count;
      if (!renderer::HasVirtualShadowPageFlag(
            gpu_flags, renderer::VirtualShadowPageFlag::kAllocated)) {
        ++current_without_allocated;
      }
      if (!renderer::HasVirtualShadowPageFlag(
            gpu_flags, renderer::VirtualShadowPageFlag::kUsedThisFrame)) {
        ++current_without_used;
      }
    } else if (any_lod_valid) {
      ++fallback_alias_count;
      constexpr auto kBasePageFlagsMask
        = renderer::ToMask(renderer::VirtualShadowPageFlag::kAllocated)
        | renderer::ToMask(renderer::VirtualShadowPageFlag::kDynamicUncached)
        | renderer::ToMask(renderer::VirtualShadowPageFlag::kStaticUncached)
        | renderer::ToMask(renderer::VirtualShadowPageFlag::kDetailGeometry)
        | renderer::ToMask(renderer::VirtualShadowPageFlag::kUsedThisFrame);
      constexpr auto kHierarchyPageFlagsMask
        = renderer::ToMask(
            renderer::VirtualShadowPageFlag::kHierarchyAllocatedDescendant)
        | renderer::ToMask(renderer::VirtualShadowPageFlag::
            kHierarchyDynamicUncachedDescendant)
        | renderer::ToMask(
            renderer::VirtualShadowPageFlag::kHierarchyStaticUncachedDescendant)
        | renderer::ToMask(
            renderer::VirtualShadowPageFlag::kHierarchyDetailDescendant)
        | renderer::ToMask(
            renderer::VirtualShadowPageFlag::kHierarchyUsedThisFrameDescendant);
      if ((gpu_flags & kBasePageFlagsMask) != 0U) {
        ++fallback_with_base_flags;
      }
      if ((gpu_flags & kHierarchyPageFlagsMask) != 0U) {
        ++fallback_with_hierarchy_flags;
      }
    }
  }

  const bool exact_coherent
    = (table_mismatches == 0U && exact_flags_mismatches == 0U);
  const bool structural_coherent
    = (table_mismatches == 0U && structural_flags_mismatches == 0U);
  const bool phase7_valid
    = current_without_allocated == 0U && fallback_with_base_flags == 0U
    && fallback_with_hierarchy_flags == 0U;
  const bool has_live_publication
    = current_page_count > 0U || fallback_alias_count > 0U;
  LOG_F(INFO,
    "VirtualShadowMapBackend: coherence check frame={} view={} entries={} "
    "page_table_mismatches={} (gpu0_cpu!0={} gpu!0_cpu0={} both!0={}) "
    "page_flags_mismatches={} semantic_only_page_flags_mismatches={} "
    "exact_page_flags_mismatches={} exact_coherent={} structural_coherent={} "
    "phase7_valid={} "
    "has_live_publication={} current_pages={} fallback_aliases={} "
    "current_without_allocated={} "
    "current_without_used={} fallback_with_base_flags={} "
    "fallback_with_hierarchy_flags={}",
    slot.source_frame.get(), slot.view_id.get(), entry_count, table_mismatches,
    table_gpu_zero_cpu_nonzero, table_gpu_nonzero_cpu_zero,
    table_both_nonzero_differ, structural_flags_mismatches,
    semantic_only_flags_mismatches, exact_flags_mismatches, exact_coherent,
    structural_coherent, phase7_valid,
    has_live_publication, current_page_count, fallback_alias_count,
    current_without_allocated, current_without_used,
    fallback_with_base_flags, fallback_with_hierarchy_flags);

  const bool log_exact_mismatches = !slot.live_authority || !phase7_valid;
  if (log_exact_mismatches && !exact_coherent) {
    std::uint32_t logged = 0U;
    for (std::uint32_t i = 0U; i < entry_count && logged < 16U; ++i) {
      if (slot.mapped_page_table[i] != slot.cpu_page_table_snapshot[i]) {
        const auto clip = i / kPagesPerLevel;
        const auto local = i % kPagesPerLevel;
        const auto page_x = local % 64U;
        const auto page_y = local / 64U;
        LOG_F(WARNING,
          "VirtualShadowMapBackend: page_table mismatch [{}] clip={} "
          "page=({},{}) gpu=0x{:08x} cpu=0x{:08x}",
          i, clip, page_x, page_y, slot.mapped_page_table[i],
          slot.cpu_page_table_snapshot[i]);
        ++logged;
      }
    }
    logged = 0U;
    for (std::uint32_t i = 0U; i < entry_count && logged < 8U; ++i) {
      if (slot.mapped_page_flags[i] != slot.cpu_page_flags_snapshot[i]) {
        const auto clip = i / kPagesPerLevel;
        const auto local = i % kPagesPerLevel;
        const auto page_x = local % 64U;
        const auto page_y = local / 64U;
        LOG_F(WARNING,
          "VirtualShadowMapBackend: page_flags mismatch [{}] clip={} "
          "page=({},{}) gpu=0x{:08x} cpu=0x{:08x}",
          i, clip, page_x, page_y, slot.mapped_page_flags[i],
          slot.cpu_page_flags_snapshot[i]);
        ++logged;
      }
    }
  }
  CHECK_F(!slot.live_authority || phase7_valid,
    "VirtualShadowMapBackend: live GPU page-management publication diverged "
    "from the Phase 7 publication contract for frame={} view={} "
    "(page_table_mismatches={} page_flags_mismatches={} "
    "current_without_allocated={} current_without_used={} "
    "fallback_with_base_flags={} fallback_with_hierarchy_flags={})",
    slot.source_frame.get(), slot.view_id.get(), table_mismatches,
    structural_flags_mismatches, current_without_allocated,
    current_without_used,
    fallback_with_base_flags, fallback_with_hierarchy_flags);
  CHECK_F(!slot.live_authority || has_live_publication,
    "VirtualShadowMapBackend: live GPU page-management publication produced "
    "no current or fallback pages for frame={} view={} "
    "(page_table_mismatches={} page_flags_mismatches={} "
    "current_without_allocated={} current_without_used={} "
    "fallback_with_base_flags={} fallback_with_hierarchy_flags={})",
    slot.source_frame.get(), slot.view_id.get(), table_mismatches,
    structural_flags_mismatches, current_without_allocated,
    current_without_used,
    fallback_with_base_flags, fallback_with_hierarchy_flags);
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
    || !page_management_table_resources_it->second.gpu_buffer
    || !page_management_flags_resources_it->second.gpu_buffer) {
    return;
  }

  auto& state = state_it->second;
  auto& page_management_table_resources
    = page_management_table_resources_it->second;
  auto& page_management_flags_resources
    = page_management_flags_resources_it->second;
  RefreshViewExports(view_id, state);

  recorder.RequireResourceState(*page_management_table_resources.gpu_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*page_management_flags_resources.gpu_buffer,
    graphics::ResourceStates::kShaderResource);
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

auto VirtualShadowMapBackend::SubmitRequestFeedback(
  const ViewId view_id, VirtualShadowRequestFeedback feedback) -> void
{
  auto& pending_feedback = request_feedback_[view_id];
  auto& channel = feedback.kind == VirtualShadowFeedbackKind::kCoarse
    ? pending_feedback.coarse
    : pending_feedback.detail;
  channel.feedback = std::move(feedback);
  channel.valid = true;
}

auto VirtualShadowMapBackend::ClearRequestFeedback(
  const ViewId view_id, const VirtualShadowFeedbackKind kind) -> void
{
  const auto it = request_feedback_.find(view_id);
  if (it == request_feedback_.end()) {
    return;
  }

  auto& channel = kind == VirtualShadowFeedbackKind::kCoarse
    ? it->second.coarse
    : it->second.detail;
  channel = {};
  if (it->second.Empty()) {
    request_feedback_.erase(it);
  }
}

auto VirtualShadowMapBackend::SetDirectionalCacheControls(
  const renderer::DirectionalVirtualCacheControls controls) -> void
{
  directional_cache_controls_ = controls;
}

auto VirtualShadowMapBackend::GetDirectionalCacheControls() const noexcept
  -> renderer::DirectionalVirtualCacheControls
{
  return directional_cache_controls_;
}

auto VirtualShadowMapBackend::TryGetFramePublication(
  const ViewId view_id) const noexcept -> const ShadowFramePublication*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.frame_publication : nullptr;
}

auto VirtualShadowMapBackend::TryGetViewIntrospection(
  const ViewId view_id) const noexcept -> const VirtualShadowViewIntrospection*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.introspection : nullptr;
}

auto VirtualShadowMapBackend::TryGetPageManagementBindings(
  const ViewId view_id) const noexcept
  -> const renderer::VirtualShadowPageManagementBindings*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.page_management_bindings
                                 : nullptr;
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
  const auto projection_matrix = view_constants.GetStableProjectionMatrix();
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

auto VirtualShadowMapBackend::InitializeDirectionalViewStateFromClipmapSetup(
  const DirectionalVirtualClipmapSetup& setup,
  const std::span<const glm::vec4> shadow_caster_bounds,
  ViewCacheEntry& state) const -> void
{
  const auto clip_level_count = setup.clip_level_count;
  const auto pages_per_level = setup.pages_per_level;

  state.shadow_instances.push_back(setup.shadow_instance);
  state.shadow_caster_bounds.assign(
    shadow_caster_bounds.begin(), shadow_caster_bounds.end());
  state.absolute_frustum_regions = setup.absolute_frustum_regions;
  state.clipmap_page_offset_x = setup.previous_clip_page_offset_x;
  state.clipmap_page_offset_y = setup.previous_clip_page_offset_y;
  state.clipmap_reuse_guardband_valid
    = setup.previous_clip_reuse_guardband_valid;
  state.clipmap_cache_valid = setup.previous_clip_cache_valid;
  state.clipmap_cache_status = setup.previous_clip_cache_status;

  state.page_table_entries.resize(static_cast<std::size_t>(clip_level_count)
      * static_cast<std::size_t>(pages_per_level),
    0U);
  state.page_flags_entries.resize(state.page_table_entries.size(), 0U);
}

auto VirtualShadowMapBackend::BuildDirectionalPreviousStateContext(
  const ViewCacheEntry* previous_state) const -> DirectionalPreviousStateContext
{
  DirectionalPreviousStateContext context {};
  if (previous_state == nullptr) {
    return context;
  }

  context.rendered_cache_history_available
    = previous_state->has_rendered_cache_history;
  if (!context.rendered_cache_history_available) {
    return context;
  }

  context.previous_key = &previous_state->key;
  context.previous_shadow_caster_bounds = &previous_state->shadow_caster_bounds;
  if (previous_state->directional_virtual_metadata.size() == 1U) {
    context.previous_metadata
      = &previous_state->directional_virtual_metadata.front();
  }
  return context;
}


auto VirtualShadowMapBackend::RefreshViewExports(
  const ViewId view_id, ViewCacheEntry& state) const -> void
{
  state.introspection.directional_virtual_metadata
    = state.directional_virtual_metadata;
  state.introspection.published_directional_virtual_metadata
    = state.directional_virtual_metadata;
  state.introspection.page_table_entries = state.page_table_entries;
  state.introspection.page_flags_entries = state.page_flags_entries;
  state.introspection.physical_page_metadata_entries
    = state.physical_page_metadata_entries;
  state.introspection.physical_page_list_entries
    = state.physical_page_list_entries;
  const auto clip_count = state.directional_virtual_metadata.empty()
    ? 0U
    : std::min(state.directional_virtual_metadata.front().clip_level_count,
        engine::kMaxVirtualDirectionalClipLevels);
  state.introspection.clipmap_page_offset_x
    = std::span<const std::int32_t> { state.clipmap_page_offset_x.data(),
        clip_count };
  state.introspection.clipmap_page_offset_y
    = std::span<const std::int32_t> { state.clipmap_page_offset_y.data(),
        clip_count };
  state.introspection.clipmap_reuse_guardband_valid
    = std::span<const bool> { state.clipmap_reuse_guardband_valid.data(),
        clip_count };
  state.introspection.clipmap_cache_valid
    = std::span<const bool> { state.clipmap_cache_valid.data(), clip_count };
  state.introspection.clipmap_cache_status
    = std::span<const renderer::DirectionalVirtualClipCacheStatus> {
        state.clipmap_cache_status.data(), clip_count
      };
  state.introspection.atlas_tile_debug_states = state.atlas_tile_debug_states;
  state.introspection.used_request_feedback = false;
  state.introspection.cache_layout_compatible
    = state.publish_diagnostics.cache_layout_compatible;
  state.introspection.depth_guardband_valid
    = state.publish_diagnostics.depth_guardband_valid;
  const auto page_management_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_management_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  const auto dirty_resident_page_resources_it
    = view_dirty_resident_page_resources_.find(view_id);
  state.introspection.has_persistent_gpu_residency_state
    = resolve_resources_it != view_resolve_resources_.end()
    && resolve_resources_it->second.physical_page_metadata_srv.IsValid()
    && resolve_resources_it->second.physical_page_lists_srv.IsValid()
    && resolve_resources_it->second.stats_srv.IsValid();
  state.introspection.resolve_stats_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.stats_srv
    : kInvalidShaderVisibleIndex;
  state.introspection.physical_page_metadata_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_metadata_srv
    : kInvalidShaderVisibleIndex;
  state.introspection.physical_page_lists_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_lists_srv
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
  state.introspection.selected_page_count
    = state.publish_diagnostics.selected_page_count;
  state.introspection.coarse_backbone_page_count
    = state.publish_diagnostics.coarse_backbone_pages;
  state.introspection.coarse_safety_selected_page_count
    = state.publish_diagnostics.coarse_safety_selected_pages;
  state.introspection.coarse_safety_budget_page_count
    = state.publish_diagnostics.coarse_safety_budget_pages;
  state.introspection.coarse_safety_capacity_fit
    = state.publish_diagnostics.coarse_safety_capacity_fit;
  state.introspection.same_frame_detail_page_count
    = state.publish_diagnostics.same_frame_detail_pages;
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
  state.introspection.allocated_page_count
    = state.publish_diagnostics.allocated_pages;
  state.introspection.evicted_page_count
    = state.publish_diagnostics.evicted_pages;
  state.introspection.rerasterized_page_count
    = state.publish_diagnostics.rerasterized_pages;
  state.introspection.requested_page_list_count
    = state.resolve_stats.requested_page_list_count;
  state.introspection.dirty_page_list_count
    = state.resolve_stats.dirty_page_list_count;
  state.introspection.clean_page_list_count
    = state.resolve_stats.clean_page_list_count;
  state.introspection.available_page_list_count
    = state.resolve_stats.available_page_list_count;
  state.introspection.resolve_stats = state.resolve_stats;

  state.page_management_bindings.page_table_srv
    = page_management_table_resources_it
      != view_page_management_page_table_resources_.end()
    ? page_management_table_resources_it->second.srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.page_table_uav
    = page_management_table_resources_it
      != view_page_management_page_table_resources_.end()
    ? page_management_table_resources_it->second.uav
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.page_flags_srv
    = page_management_flags_resources_it
      != view_page_management_page_flags_resources_.end()
    ? page_management_flags_resources_it->second.srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.page_flags_uav
    = page_management_flags_resources_it
      != view_page_management_page_flags_resources_.end()
    ? page_management_flags_resources_it->second.uav
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.dirty_page_flags_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.dirty_page_flags_srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.dirty_page_flags_uav
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.dirty_page_flags_uav
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.physical_page_metadata_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_metadata_srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.physical_page_metadata_uav
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_metadata_uav
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.physical_page_lists_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_lists_srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.physical_page_lists_uav
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_lists_uav
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.resolve_stats_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.stats_srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.resolve_stats_uav
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.stats_uav
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.dirty_resident_pages_srv
    = dirty_resident_page_resources_it
        != view_dirty_resident_page_resources_.end()
      ? dirty_resident_page_resources_it->second.srv
      : kInvalidShaderVisibleIndex;
  state.page_management_bindings.physical_page_capacity
    = resolve_resources_it != view_resolve_resources_.end()
      ? resolve_resources_it->second.physical_page_metadata_capacity
      : 0U;
  state.page_management_bindings.atlas_tiles_per_axis
    = physical_pool_config_.atlas_tiles_per_axis;
  state.page_management_bindings.dirty_resident_page_count
    = dirty_resident_page_resources_it
        != view_dirty_resident_page_resources_.end()
      ? dirty_resident_page_resources_it->second.upload_count
      : 0U;
  state.page_management_bindings.global_dirty_resident_contents
    = state.pending_residency_resolve.global_dirty_resident_contents;

  state.frame_publication.virtual_shadow_page_table_srv
    = state.page_management_bindings.page_table_srv;
  state.frame_publication.virtual_shadow_page_flags_srv
    = state.page_management_bindings.page_flags_srv;

  state.frame_publication.virtual_shadow_physical_page_metadata_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_metadata_srv
    : kInvalidShaderVisibleIndex;
  state.frame_publication.virtual_shadow_physical_page_lists_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_lists_srv
    : kInvalidShaderVisibleIndex;
}

auto VirtualShadowMapBackend::CountPublishedCurrentPagesNeedingRaster(
  const ViewCacheEntry& state) const noexcept -> std::uint32_t
{
  if (state.page_table_entries.empty()
    || state.page_flags_entries.size() != state.page_table_entries.size()) {
    return 0U;
  }

  return static_cast<std::uint32_t>(std::count_if(
    state.page_table_entries.begin(), state.page_table_entries.end(),
    [&state, page_index = std::size_t { 0U }](const std::uint32_t entry) mutable {
      const auto current_index = page_index++;
      if (!renderer::VirtualShadowPageTableEntryHasCurrentLod(entry)
        || current_index >= state.page_flags_entries.size()) {
        return false;
      }

      const auto page_flags = state.page_flags_entries[current_index];
      return renderer::HasVirtualShadowPageFlag(
               page_flags, renderer::VirtualShadowPageFlag::kDynamicUncached)
        || renderer::HasVirtualShadowPageFlag(
          page_flags, renderer::VirtualShadowPageFlag::kStaticUncached);
    }));
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

auto VirtualShadowMapBackend::PrepareDirectionalVirtualClipmapSetup(
  const engine::ViewConstants& view_constants,
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
  const glm::vec3 world_up
    = std::abs(glm::dot(light_dir_to_light, glm::vec3(0.0F, 0.0F, 1.0F)))
      > 0.95F
    ? glm::vec3(1.0F, 0.0F, 0.0F)
    : glm::vec3(0.0F, 0.0F, 1.0F);

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
  setup.coarse_safety_clip_index
    = setup.clip_level_count > 0U ? setup.clip_level_count - 1U : 0U;
  const std::uint32_t coarse_safety_max_pages_per_axis
    = ResolveCoarseSafetyMaxPagesPerAxis(
      physical_pool_config_.physical_tile_capacity);
  const std::uint32_t coarse_safety_guard_pages
    = coarse_safety_max_pages_per_axis > 2U * kCoarseBackboneGuardPages
    ? kCoarseBackboneGuardPages
    : 0U;
  const std::uint32_t coarse_safety_inner_pages_per_axis = std::max(1U,
    coarse_safety_max_pages_per_axis > 2U * coarse_safety_guard_pages
      ? coarse_safety_max_pages_per_axis - 2U * coarse_safety_guard_pages
      : coarse_safety_max_pages_per_axis);
  float coarse_safety_min_x = std::numeric_limits<float>::max();
  float coarse_safety_max_x = std::numeric_limits<float>::lowest();
  float coarse_safety_min_y = std::numeric_limits<float>::max();
  float coarse_safety_max_y = std::numeric_limits<float>::lowest();
  bool coarse_safety_bounds_valid = false;
  for (const auto& point_ws : full_frustum_world_points) {
    const glm::vec3 point_rot_ls
      = glm::vec3(rot_view * glm::vec4(point_ws, 1.0F));
    coarse_safety_min_x = std::min(coarse_safety_min_x, point_rot_ls.x);
    coarse_safety_max_x = std::max(coarse_safety_max_x, point_rot_ls.x);
    coarse_safety_min_y = std::min(coarse_safety_min_y, point_rot_ls.y);
    coarse_safety_max_y = std::max(coarse_safety_max_y, point_rot_ls.y);
    coarse_safety_bounds_valid = true;
  }
  if (coarse_safety_bounds_valid && setup.clip_level_count > 0U) {
    const float required_width
      = std::max(0.0F, coarse_safety_max_x - coarse_safety_min_x);
    const float required_height
      = std::max(0.0F, coarse_safety_max_y - coarse_safety_min_y);
    const float required_page_world
      = std::max(std::max(required_width, required_height)
          / std::max(
            1.0F, static_cast<float>(coarse_safety_inner_pages_per_axis)),
        setup.clip_page_world[setup.coarse_safety_clip_index]);
    setup.clip_page_world[setup.coarse_safety_clip_index]
      = QuantizeUpToPowerOfTwo(required_page_world);
    largest_half_extent = std::max(largest_half_extent,
      setup.clip_page_world[setup.coarse_safety_clip_index]
        * std::max(1.0F, static_cast<float>(setup.pages_per_axis)) * 0.5F);
  }

  const engine::DirectionalVirtualShadowMetadata* previous_metadata = nullptr;
  if (previous_state != nullptr && previous_state->has_rendered_cache_history
    && previous_state->directional_virtual_metadata.size() == 1U) {
    previous_metadata = &previous_state->directional_virtual_metadata.front();
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
    && previous_state_exists && previous_state->has_rendered_cache_history;
  const bool force_invalidate_cache
    = previous_rendered_cache_exists
    && directional_cache_controls_.force_invalidate;
  auto depth_guardband_candidate = previous_rendered_cache_exists
    && !force_invalidate_cache
    && EvaluateDirectionalDepthGuardband(*previous_metadata,
      std::span<const glm::vec3> {
        full_frustum_world_points.data(), full_frustum_world_points.size() },
      shadow_caster_bounds, largest_half_extent, largest_half_extent);
  setup.cache_layout_compatible = false;
  setup.depth_guardband_valid = false;

  if (depth_guardband_candidate) {
    const auto previous_depth_range
      = shadow_detail::RecoverDirectionalVirtualDepthRange(*previous_metadata);
    if (previous_depth_range.valid) {
      const glm::vec3 previous_eye_ws
        = ExtractDirectionalLightEyeWs(previous_metadata->light_view);
      const glm::vec3 previous_eye_ls
        = glm::vec3(rot_view * glm::vec4(previous_eye_ws, 1.0F));
      light_eye_ls.z = previous_eye_ls.z;
      setup.light_eye = glm::vec3(inv_rot_view * glm::vec4(light_eye_ls, 1.0F));
      setup.light_view = glm::lookAtRH(
        setup.light_eye, setup.light_eye + light_dir_to_surface, world_up);
      setup.near_plane = previous_depth_range.near_plane;
      setup.far_plane = previous_depth_range.far_plane;
    } else {
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
  setup.metadata.light_view = setup.light_view;

  for (std::uint32_t clip_index = 0U; clip_index < setup.clip_level_count;
    ++clip_index) {
    const float half_extent = setup.clip_page_world[clip_index]
      * std::max(1.0F, static_cast<float>(setup.pages_per_axis)) * 0.5F;
    setup.clip_grid_origin_x[clip_index] = static_cast<std::int32_t>(std::floor(
      (camera_ls.x - half_extent) / setup.clip_page_world[clip_index]));
    setup.clip_grid_origin_y[clip_index] = static_cast<std::int32_t>(std::floor(
      (camera_ls.y - half_extent) / setup.clip_page_world[clip_index]));
    setup.clip_origin_x[clip_index]
      = static_cast<float>(setup.clip_grid_origin_x[clip_index])
      * setup.clip_page_world[clip_index];
    setup.clip_origin_y[clip_index]
      = static_cast<float>(setup.clip_grid_origin_y[clip_index])
      * setup.clip_page_world[clip_index];
    setup.metadata.clip_metadata[clip_index].origin_page_scale = glm::vec4(
      setup.clip_origin_x[clip_index], setup.clip_origin_y[clip_index],
      setup.clip_page_world[clip_index], depth_scale);
    setup.metadata.clip_metadata[clip_index].bias_reserved
      = glm::vec4(depth_bias, 0.0F, 0.0F, 0.0F);
  }

  setup.cache_layout_compatible = previous_rendered_cache_exists
    && !force_invalidate_cache
    && shadow_detail::IsDirectionalVirtualCacheLayoutCompatible(
      *previous_metadata, setup.metadata);
  setup.depth_guardband_valid
    = setup.cache_layout_compatible && depth_guardband_candidate;

  std::array<glm::vec3, 8> frustum_light_space_points {};
  std::array<glm::vec3, 8> full_frustum_light_space_points {};
  for (std::size_t i = 0U; i < frustum_world_points.size(); ++i) {
    frustum_light_space_points[i]
      = glm::vec3(setup.light_view * glm::vec4(frustum_world_points[i], 1.0F));
    full_frustum_light_space_points[i] = glm::vec3(
      setup.light_view * glm::vec4(full_frustum_world_points[i], 1.0F));
  }

  setup.absolute_frustum_regions.resize(setup.clip_level_count);
  for (std::uint32_t clip_index = 0U; clip_index < setup.clip_level_count;
    ++clip_index) {
    float min_page_x = std::numeric_limits<float>::max();
    float max_page_x = std::numeric_limits<float>::lowest();
    float min_page_y = std::numeric_limits<float>::max();
    float max_page_y = std::numeric_limits<float>::lowest();

    const auto& selected_region_points
      = clip_index == setup.coarse_safety_clip_index
      ? full_frustum_light_space_points
      : frustum_light_space_points;
    for (const auto& point_ls : selected_region_points) {
      const float page_x = (point_ls.x - setup.clip_origin_x[clip_index])
        / setup.clip_page_world[clip_index];
      const float page_y = (point_ls.y - setup.clip_origin_y[clip_index])
        / setup.clip_page_world[clip_index];
      min_page_x = std::min(min_page_x, page_x);
      max_page_x = std::max(max_page_x, page_x);
      min_page_y = std::min(min_page_y, page_y);
      max_page_y = std::max(max_page_y, page_y);
    }

    if (max_page_x < 0.0F || max_page_y < 0.0F
      || min_page_x >= static_cast<float>(setup.pages_per_axis)
      || min_page_y >= static_cast<float>(setup.pages_per_axis)) {
      setup.absolute_frustum_regions[clip_index] = {};
      continue;
    }

    auto& region = setup.frustum_regions[clip_index];
    region.valid = true;
    region.min_x
      = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_page_x)));
    region.max_x = static_cast<std::uint32_t>(
      std::min(static_cast<float>(setup.pages_per_axis - 1U),
        std::max(0.0F, std::ceil(max_page_x) - 1.0F)));
    region.min_y
      = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_page_y)));
    region.max_y = static_cast<std::uint32_t>(
      std::min(static_cast<float>(setup.pages_per_axis - 1U),
        std::max(0.0F, std::ceil(max_page_y) - 1.0F)));

    setup.absolute_frustum_regions[clip_index] = AbsoluteClipPageRegion {
      .valid = true,
      .min_x = setup.clip_grid_origin_x[clip_index]
        + static_cast<std::int32_t>(region.min_x),
      .max_x = setup.clip_grid_origin_x[clip_index]
        + static_cast<std::int32_t>(region.max_x),
      .min_y = setup.clip_grid_origin_y[clip_index]
        + static_cast<std::int32_t>(region.min_y),
      .max_y = setup.clip_grid_origin_y[clip_index]
        + static_cast<std::int32_t>(region.max_y),
    };
  }

  setup.coarse_safety_priority_center_ls = glm::vec2(camera_ls.x, camera_ls.y);
  if (setup.clip_level_count > 0U && !visible_receiver_bounds.empty()) {
    float receiver_min_x = std::numeric_limits<float>::max();
    float receiver_max_x = std::numeric_limits<float>::lowest();
    float receiver_min_y = std::numeric_limits<float>::max();
    float receiver_max_y = std::numeric_limits<float>::lowest();
    for (const auto& receiver_bound : visible_receiver_bounds) {
      const glm::vec3 receiver_center_ls = glm::vec3(
        setup.light_view * glm::vec4(glm::vec3(receiver_bound), 1.0F));
      const float receiver_radius = std::max(0.0F, receiver_bound.w);
      receiver_min_x
        = std::min(receiver_min_x, receiver_center_ls.x - receiver_radius);
      receiver_max_x
        = std::max(receiver_max_x, receiver_center_ls.x + receiver_radius);
      receiver_min_y
        = std::min(receiver_min_y, receiver_center_ls.y - receiver_radius);
      receiver_max_y
        = std::max(receiver_max_y, receiver_center_ls.y + receiver_radius);
      setup.coarse_safety_priority_valid = true;
    }
    if (setup.coarse_safety_priority_valid) {
      setup.coarse_safety_priority_center_ls
        = glm::vec2(0.5F * (receiver_min_x + receiver_max_x),
          0.5F * (receiver_min_y + receiver_max_y));
    }
  }

  if (!previous_state_exists) {
    setup.previous_clip_cache_status.fill(
      renderer::DirectionalVirtualClipCacheStatus::kNoPreviousFrame);
  } else if (!previous_rendered_cache_exists) {
    setup.previous_clip_cache_status.fill(
      renderer::DirectionalVirtualClipCacheStatus::kNeverRendered);
  } else if (force_invalidate_cache) {
    setup.previous_clip_cache_status.fill(
      renderer::DirectionalVirtualClipCacheStatus::kForceInvalidated);
  } else if (!setup.cache_layout_compatible) {
    setup.previous_clip_cache_status.fill(
      renderer::DirectionalVirtualClipCacheStatus::kLayoutInvalid);
  } else if (!setup.depth_guardband_valid) {
    setup.previous_clip_cache_status.fill(
      renderer::DirectionalVirtualClipCacheStatus::kDepthGuardbandInvalid);
  }

  if (previous_metadata != nullptr) {
    const auto clip_count = std::min(
      previous_metadata->clip_level_count, setup.metadata.clip_level_count);
    for (std::uint32_t clip_index = 0U; clip_index < clip_count; ++clip_index) {
      const auto offset = ResolveDirectionalVirtualClipmapPageOffset(
        *previous_metadata, setup.metadata, clip_index);
      setup.previous_clip_page_offset_x[clip_index] = offset.delta_x;
      setup.previous_clip_page_offset_y[clip_index] = offset.delta_y;
      const bool panning_valid = IsDirectionalVirtualClipmapPanningCompatible(
        offset, directional_cache_controls_.clipmap_panning_enabled);
      setup.previous_clip_reuse_guardband_valid[clip_index] = panning_valid
        && IsDirectionalVirtualClipReuseGuardbandValid(
          offset, kDirectionalVirtualClipReuseGuardbandPages);
      if (!previous_metadata_exists || !previous_rendered_cache_exists
        || force_invalidate_cache) {
        setup.previous_clip_cache_valid[clip_index] = false;
        continue;
      }
      if (!setup.cache_layout_compatible) {
        setup.previous_clip_cache_valid[clip_index] = false;
        continue;
      }
      if (!setup.depth_guardband_valid) {
        setup.previous_clip_cache_valid[clip_index] = false;
        continue;
      }
      if (!panning_valid) {
        setup.previous_clip_cache_status[clip_index]
          = renderer::DirectionalVirtualClipCacheStatus::kPanningDisabled;
        setup.previous_clip_cache_valid[clip_index] = false;
        continue;
      }
      if (!setup.previous_clip_reuse_guardband_valid[clip_index]) {
        setup.previous_clip_cache_status[clip_index]
          = renderer::DirectionalVirtualClipCacheStatus::kReuseGuardbandInvalid;
        setup.previous_clip_cache_valid[clip_index] = false;
        continue;
      }
      setup.previous_clip_cache_status[clip_index]
        = renderer::DirectionalVirtualClipCacheStatus::kValid;
      setup.previous_clip_cache_valid[clip_index] = true;
    }
  }

  setup.coarse_safety_budget_pages = std::min(
    physical_pool_config_.physical_tile_capacity, setup.pages_per_level);
  setup.valid = true;
  return setup;
}

auto VirtualShadowMapBackend::BuildDirectionalSelectionResult(
  const ViewId view_id, const DirectionalVirtualClipmapSetup& setup,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const engine::DirectionalVirtualShadowMetadata* previous_metadata,
  const ViewCacheEntry* previous_state, ViewCacheEntry& state) const
  -> DirectionalSelectionBuildResult
{
  using namespace shadow_detail;

  const auto clip_level_count = setup.clip_level_count;
  const auto pages_per_axis = setup.pages_per_axis;
  const auto& metadata = setup.metadata;
  const auto& light_view = setup.light_view;
  const auto& clip_page_world = setup.clip_page_world;
  const auto& clip_origin_x = setup.clip_origin_x;
  const auto& clip_origin_y = setup.clip_origin_y;
  const auto& clip_grid_origin_x = setup.clip_grid_origin_x;
  const auto& clip_grid_origin_y = setup.clip_grid_origin_y;
  const auto coarse_safety_clip_index = setup.coarse_safety_clip_index;
  const auto coarse_safety_budget_pages = setup.coarse_safety_budget_pages;
  const auto coarse_safety_priority_center_ls
    = setup.coarse_safety_priority_center_ls;
  (void)shadow_caster_bounds;
  std::vector<ClipSelectedRegion> frustum_regions(setup.frustum_regions.begin(),
    setup.frustum_regions.begin()
      + static_cast<std::ptrdiff_t>(clip_level_count));
  // Issue 3 cleanup: current-frame fine demand now comes from GPU page
  // marking/request words, so CPU selection here must stay diagnostic-only.
  // Keep coarse region counting for publish telemetry, but do not synthesize a
  // CPU page set that could drift away from live page-management state.
  const auto count_region_pages =
    [](const ClipSelectedRegion& region) -> std::uint32_t {
        if (!region.valid || region.max_x < region.min_x
          || region.max_y < region.min_y) {
          return 0U;
        }

        return (region.max_x - region.min_x + 1U)
          * (region.max_y - region.min_y + 1U);
      };
  const auto union_region =
    [](const ClipSelectedRegion& lhs,
      const ClipSelectedRegion& rhs) -> ClipSelectedRegion {
      if (!lhs.valid) {
        return rhs;
      }
      if (!rhs.valid) {
        return lhs;
      }
      return ClipSelectedRegion {
        .valid = true,
        .min_x = std::min(lhs.min_x, rhs.min_x),
        .max_x = std::max(lhs.max_x, rhs.max_x),
        .min_y = std::min(lhs.min_y, rhs.min_y),
        .max_y = std::max(lhs.max_y, rhs.max_y),
      };
    };
  const auto union_absolute_region =
    [](const AbsoluteClipPageRegion& lhs,
      const AbsoluteClipPageRegion& rhs) -> AbsoluteClipPageRegion {
      if (!lhs.valid) {
        return rhs;
      }
      if (!rhs.valid) {
        return lhs;
      }
      return AbsoluteClipPageRegion {
        .valid = true,
        .min_x = std::min(lhs.min_x, rhs.min_x),
        .max_x = std::max(lhs.max_x, rhs.max_x),
        .min_y = std::min(lhs.min_y, rhs.min_y),
        .max_y = std::max(lhs.max_y, rhs.max_y),
      };
    };

  std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
    reusable_clip_contents {};
  reusable_clip_contents.fill(false);
  const std::uint32_t coarse_backbone_begin
    = ResolveDirectionalCoarseBackboneBegin(clip_level_count);
  if (!visible_receiver_bounds.empty()) {
    for (std::uint32_t clip_index = 0U; clip_index < coarse_backbone_begin;
      ++clip_index) {
      float min_page_x = std::numeric_limits<float>::max();
      float max_page_x = std::numeric_limits<float>::lowest();
      float min_page_y = std::numeric_limits<float>::max();
      float max_page_y = std::numeric_limits<float>::lowest();
      bool receiver_region_valid = false;

      for (const auto& receiver_bound : visible_receiver_bounds) {
        const glm::vec3 receiver_center_ls = glm::vec3(
          light_view * glm::vec4(glm::vec3(receiver_bound), 1.0F));
        const float receiver_radius = std::max(0.0F, receiver_bound.w);
        const float receiver_min_page_x
          = (receiver_center_ls.x - receiver_radius - clip_origin_x[clip_index])
          / clip_page_world[clip_index];
        const float receiver_max_page_x
          = (receiver_center_ls.x + receiver_radius - clip_origin_x[clip_index])
          / clip_page_world[clip_index];
        const float receiver_min_page_y
          = (receiver_center_ls.y - receiver_radius - clip_origin_y[clip_index])
          / clip_page_world[clip_index];
        const float receiver_max_page_y
          = (receiver_center_ls.y + receiver_radius - clip_origin_y[clip_index])
          / clip_page_world[clip_index];
        min_page_x = std::min(min_page_x, receiver_min_page_x);
        max_page_x = std::max(max_page_x, receiver_max_page_x);
        min_page_y = std::min(min_page_y, receiver_min_page_y);
        max_page_y = std::max(max_page_y, receiver_max_page_y);
        receiver_region_valid = true;
      }

      if (!receiver_region_valid || max_page_x < 0.0F || max_page_y < 0.0F
        || min_page_x >= static_cast<float>(pages_per_axis)
        || min_page_y >= static_cast<float>(pages_per_axis)) {
        continue;
      }

      const auto receiver_region = ClipSelectedRegion {
        .valid = true,
        .min_x = static_cast<std::uint32_t>(
          std::max(0.0F, std::floor(min_page_x))),
        .max_x = static_cast<std::uint32_t>(std::min(
          static_cast<float>(pages_per_axis - 1U),
          std::max(0.0F, std::ceil(max_page_x) - 1.0F))),
        .min_y = static_cast<std::uint32_t>(
          std::max(0.0F, std::floor(min_page_y))),
        .max_y = static_cast<std::uint32_t>(std::min(
          static_cast<float>(pages_per_axis - 1U),
          std::max(0.0F, std::ceil(max_page_y) - 1.0F))),
      };
      const auto receiver_absolute_region = AbsoluteClipPageRegion {
        .valid = true,
        .min_x = clip_grid_origin_x[clip_index]
          + static_cast<std::int32_t>(receiver_region.min_x),
        .max_x = clip_grid_origin_x[clip_index]
          + static_cast<std::int32_t>(receiver_region.max_x),
        .min_y = clip_grid_origin_y[clip_index]
          + static_cast<std::int32_t>(receiver_region.min_y),
        .max_y = clip_grid_origin_y[clip_index]
          + static_cast<std::int32_t>(receiver_region.max_y),
      };
      frustum_regions[clip_index]
        = union_region(frustum_regions[clip_index], receiver_region);
      state.absolute_frustum_regions[clip_index] = union_absolute_region(
        state.absolute_frustum_regions[clip_index], receiver_absolute_region);
    }
  }
  bool global_dirty_resident_contents = false;
  // Audit Stage 1 bridge note: dirty resident key generation is still CPU
  // invalidation authority here. Treat it as temporary until invalidation is
  // moved into its own dedicated stage.
  const bool previous_rendered_cache_exists
    = previous_state != nullptr && previous_state->has_rendered_cache_history;
  const bool previous_page_management_state_exists
    = previous_rendered_cache_exists && previous_state != nullptr
    && previous_state->page_management_bindings.page_table_srv.IsValid()
    && previous_state->page_management_bindings.page_flags_srv.IsValid()
    && previous_state->page_management_bindings.physical_page_metadata_srv
      .IsValid()
    && previous_state->page_management_bindings.physical_page_lists_srv.IsValid()
    && previous_state->page_management_bindings.resolve_stats_srv.IsValid();
  const std::uint32_t previous_page_management_resident_count
    = previous_page_management_state_exists
    ? previous_state->resolve_stats.resident_entry_count
    : 0U;
  const bool cache_layout_compatible = setup.cache_layout_compatible;
  const bool depth_guardband_valid = setup.depth_guardband_valid;
  bool address_space_compatible = false;
  if (previous_metadata != nullptr) {
    address_space_compatible = IsDirectionalVirtualAddressSpaceCompatible(
      *previous_metadata, metadata);
    if (cache_layout_compatible) {
      for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
        ++clip_index) {
        reusable_clip_contents[clip_index]
          = setup.previous_clip_cache_valid[clip_index]
          && IsDirectionalVirtualClipContentReusable(
            *previous_metadata, metadata, clip_index);
      }
    }
  }

  const auto feedback_it = request_feedback_.find(view_id);
  const auto* detail_feedback_channel
    = feedback_it != request_feedback_.end() && feedback_it->second.detail.valid
    ? &feedback_it->second.detail
    : nullptr;
  const auto* coarse_feedback_channel
    = feedback_it != request_feedback_.end() && feedback_it->second.coarse.valid
    ? &feedback_it->second.coarse
    : nullptr;
  const auto evaluate_feedback_channel
    = [&](const PendingRequestFeedbackChannel* channel) {
        struct FeedbackChannelDecision {
          ViewCacheEntry::RequestFeedbackDecision decision {
            ViewCacheEntry::RequestFeedbackDecision::kNoFeedback
          };
          std::uint32_t key_count { 0U };
          std::uint64_t age_frames { 0U };
        };

        FeedbackChannelDecision result {};
        if (channel == nullptr) {
          return result;
        }

        const auto& feedback = channel->feedback;
        result.key_count
          = static_cast<std::uint32_t>(feedback.requested_resident_keys.size());
        if (frame_sequence_ > feedback.source_frame_sequence) {
          result.age_frames
            = (frame_sequence_ - feedback.source_frame_sequence).get();
        }

        if (feedback.requested_resident_keys.empty()) {
          result.decision
            = ViewCacheEntry::RequestFeedbackDecision::kEmptyFeedback;
        } else if (feedback.pages_per_axis != pages_per_axis
          || feedback.clip_level_count != clip_level_count) {
          result.decision
            = ViewCacheEntry::RequestFeedbackDecision::kDimensionMismatch;
        } else if (feedback.directional_address_space_hash
          != shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(metadata)) {
          result.decision
            = ViewCacheEntry::RequestFeedbackDecision::kAddressSpaceMismatch;
        } else if (frame_sequence_ <= feedback.source_frame_sequence) {
          result.decision = ViewCacheEntry::RequestFeedbackDecision::kSameFrame;
        } else if (result.age_frames > kMaxRequestFeedbackAgeFrames) {
          result.decision = ViewCacheEntry::RequestFeedbackDecision::kStale;
        } else {
          result.decision
            = ViewCacheEntry::RequestFeedbackDecision::kTelemetryOnly;
        }
        return result;
      };
  const auto detail_feedback_state
    = evaluate_feedback_channel(detail_feedback_channel);
  const auto coarse_feedback_state
    = evaluate_feedback_channel(coarse_feedback_channel);
  auto feedback_decision = detail_feedback_state.decision;
  if (feedback_decision
    == ViewCacheEntry::RequestFeedbackDecision::kNoFeedback) {
    feedback_decision = coarse_feedback_state.decision;
  } else if (feedback_decision
      != ViewCacheEntry::RequestFeedbackDecision::kTelemetryOnly
    && coarse_feedback_state.decision
      == ViewCacheEntry::RequestFeedbackDecision::kTelemetryOnly) {
    feedback_decision = coarse_feedback_state.decision;
  }
  const std::uint32_t feedback_key_count
    = detail_feedback_state.key_count + coarse_feedback_state.key_count;
  const std::uint64_t feedback_age_frames = std::max(
    detail_feedback_state.age_frames, coarse_feedback_state.age_frames);
  const bool use_previous_resident_mismatch_carry = false;
  (void)use_previous_resident_mismatch_carry;
  std::uint32_t coarse_backbone_pages = 0U;
  std::uint32_t selected_page_count = 0U;
  std::uint32_t same_frame_detail_pages = 0U;
  std::uint32_t feedback_requested_pages = 0U;

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

  const bool transition_publish_risk = !previous_page_management_state_exists
    || !address_space_compatible
    || global_dirty_resident_contents;
  for (std::uint32_t clip_index = clip_level_count;
    clip_index-- > coarse_backbone_begin;) {
    const auto& region = frustum_regions[clip_index];
    if (!region.valid) {
      continue;
    }

    const auto region_page_count = count_region_pages(region);
    coarse_backbone_pages += region_page_count;
    selected_page_count += region_page_count;
  }
  // Unreal's split is coarse pages from coarse marking, fine pages from the
  // current frame's visible-sample request bits. Do not let previous-frame
  // feedback or same-frame CPU bootstrap author live fine demand here; that
  // keeps issue 3 alive by making CPU heuristics compete with page-state
  // mutation in resolve.
  const std::uint32_t coarse_safety_selected_pages
    = coarse_safety_clip_index < clip_level_count
    ? count_region_pages(frustum_regions[coarse_safety_clip_index])
    : 0U;
  const bool coarse_safety_capacity_fit
    = coarse_safety_selected_pages <= coarse_safety_budget_pages;
  const bool predicted_current_publish_coherent
    = coarse_safety_capacity_fit && !transition_publish_risk;
  DirectionalSelectionBuildResult selection_result {};
  selection_result.feedback_decision = feedback_decision;
  selection_result.reusable_clip_contents = reusable_clip_contents;
  selection_result.selected_page_count = selected_page_count;
  selection_result.feedback_key_count = feedback_key_count;
  selection_result.feedback_age_frames = feedback_age_frames;
  selection_result.coarse_backbone_pages = coarse_backbone_pages;
  selection_result.same_frame_detail_pages = same_frame_detail_pages;
  selection_result.feedback_requested_pages = feedback_requested_pages;
  selection_result.coarse_safety_selected_pages = coarse_safety_selected_pages;
  selection_result.coarse_safety_capacity_fit = coarse_safety_capacity_fit;
  selection_result.predicted_coherent_publication
    = predicted_current_publish_coherent;
  selection_result.bootstrap_prefers_finest_detail_pages = false;
  selection_result.address_space_compatible = address_space_compatible;
  selection_result.cache_layout_compatible = cache_layout_compatible;
  selection_result.depth_guardband_valid = depth_guardband_valid;
  selection_result.previous_page_management_state_exists
    = previous_page_management_state_exists;
  selection_result.previous_page_management_resident_count
    = previous_page_management_resident_count;
  return selection_result;
}

auto VirtualShadowMapBackend::BuildDirectionalInvalidationResult(
  const DirectionalVirtualClipmapSetup& setup,
  const PublicationKey* previous_key, const PublicationKey& current_key,
  const engine::DirectionalVirtualShadowMetadata* previous_metadata,
  const std::vector<glm::vec4>* previous_shadow_caster_bounds,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const bool address_space_compatible) const -> DirectionalInvalidationBuildResult
{
  using namespace shadow_detail;

  DirectionalInvalidationBuildResult result {};
  if (previous_metadata == nullptr || !address_space_compatible) {
    return result;
  }

  const bool shadow_content_hash_changed = previous_key != nullptr
    && previous_key->shadow_content_hash != current_key.shadow_content_hash;
  const bool caster_bounds_changed = previous_key != nullptr
    && previous_key->caster_hash != current_key.caster_hash;
  bool found_spatial_delta = false;

  if (previous_shadow_caster_bounds != nullptr
    && previous_shadow_caster_bounds->size() == shadow_caster_bounds.size()) {
    const auto dirty_page_flags
      = renderer::ToMask(renderer::VirtualShadowPageFlag::kDynamicUncached)
      | renderer::ToMask(renderer::VirtualShadowPageFlag::kStaticUncached);
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
      AppendDirtyResidentFlagsForBound(previous_bound,
        previous_metadata->light_view, setup.clip_page_world,
        setup.clip_level_count, dirty_page_flags,
        result.dirty_resident_page_flags);
      AppendDirtyResidentFlagsForBound(current_bound, setup.light_view,
        setup.clip_page_world, setup.clip_level_count, dirty_page_flags,
        result.dirty_resident_page_flags);
    }
  } else if (shadow_content_hash_changed || caster_bounds_changed) {
    result.global_dirty_resident_contents = true;
  }

  if (!result.global_dirty_resident_contents
    && (shadow_content_hash_changed || caster_bounds_changed)
    && !found_spatial_delta) {
    result.global_dirty_resident_contents = true;
  }

  return result;
}

auto VirtualShadowMapBackend::PopulateDirectionalPendingResolve(
  ViewCacheEntry& state, const DirectionalVirtualClipmapSetup& setup,
  DirectionalSelectionBuildResult selection,
  DirectionalInvalidationBuildResult invalidation,
  const engine::ViewConstants& view_constants,
  const std::uint32_t visible_receiver_bound_count) const
  -> void
{
  constexpr std::uint32_t feedback_refinement_pages = 0U;
  constexpr std::uint32_t receiver_bootstrap_pages = 0U;
  constexpr std::uint32_t current_frame_reinforcement_pages = 0U;
  constexpr std::uint64_t current_frame_reinforcement_reference_frame = 0U;

  state.pending_residency_resolve = {};
  auto& pending_resolve = state.pending_residency_resolve;
  pending_resolve.valid = true;
  pending_resolve.dirty = true;
  pending_resolve.reset_page_management_state
    = !selection.previous_page_management_state_exists
    || !selection.address_space_compatible;
  pending_resolve.clip_level_count = setup.clip_level_count;
  pending_resolve.pages_per_axis = setup.pages_per_axis;
  pending_resolve.pages_per_level = setup.pages_per_level;
  pending_resolve.view_constants = view_constants;
  pending_resolve.light_view = setup.light_view;
  pending_resolve.light_eye = setup.light_eye;
  pending_resolve.near_plane = setup.near_plane;
  pending_resolve.far_plane = setup.far_plane;
  pending_resolve.clip_page_world = setup.clip_page_world;
  pending_resolve.clip_origin_x = setup.clip_origin_x;
  pending_resolve.clip_origin_y = setup.clip_origin_y;
  pending_resolve.clip_grid_origin_x = setup.clip_grid_origin_x;
  pending_resolve.clip_grid_origin_y = setup.clip_grid_origin_y;
  pending_resolve.previous_clip_page_offset_x = state.clipmap_page_offset_x;
  pending_resolve.previous_clip_page_offset_y = state.clipmap_page_offset_y;
  pending_resolve.previous_clip_reuse_guardband_valid
    = state.clipmap_reuse_guardband_valid;
  pending_resolve.previous_clip_cache_valid = state.clipmap_cache_valid;
  pending_resolve.previous_clip_cache_status = state.clipmap_cache_status;
  pending_resolve.coarse_backbone_begin
    = shadow_detail::ResolveDirectionalCoarseBackboneBegin(
      setup.clip_level_count);
  pending_resolve.coarse_safety_clip_index = setup.coarse_safety_clip_index;
  pending_resolve.coarse_safety_max_page_count = setup.coarse_safety_budget_pages;
  pending_resolve.coarse_safety_priority_center_ls
    = setup.coarse_safety_priority_center_ls;
  pending_resolve.coarse_safety_priority_valid
    = setup.coarse_safety_priority_valid;
  pending_resolve.reusable_clip_contents = selection.reusable_clip_contents;
  pending_resolve.bootstrap_prefers_finest_detail_pages
    = selection.bootstrap_prefers_finest_detail_pages;
  pending_resolve.address_space_compatible = selection.address_space_compatible;
  pending_resolve.cache_layout_compatible = selection.cache_layout_compatible;
  pending_resolve.depth_guardband_valid = selection.depth_guardband_valid;
  pending_resolve.global_dirty_resident_contents
    = invalidation.global_dirty_resident_contents;
  pending_resolve.dirty_resident_page_flags
    = std::move(invalidation.dirty_resident_page_flags);

  state.publish_diagnostics.feedback_decision = selection.feedback_decision;
  state.publish_diagnostics.feedback_key_count = selection.feedback_key_count;
  state.publish_diagnostics.feedback_age_frames = selection.feedback_age_frames;
  state.publish_diagnostics.address_space_compatible
    = selection.address_space_compatible;
  state.publish_diagnostics.cache_layout_compatible
    = selection.cache_layout_compatible;
  state.publish_diagnostics.depth_guardband_valid
    = selection.depth_guardband_valid;
  state.publish_diagnostics.global_dirty_resident_contents
    = pending_resolve.global_dirty_resident_contents;
  state.publish_diagnostics.shadow_caster_bound_count
    = static_cast<std::uint32_t>(state.shadow_caster_bounds.size());
  state.publish_diagnostics.visible_receiver_bound_count
    = visible_receiver_bound_count;
  state.publish_diagnostics.clip_level_count = setup.clip_level_count;
  state.publish_diagnostics.coarse_backbone_begin
    = pending_resolve.coarse_backbone_begin;
  state.publish_diagnostics.selected_page_count = selection.selected_page_count;
  state.publish_diagnostics.coarse_backbone_pages
    = selection.coarse_backbone_pages;
  state.publish_diagnostics.coarse_safety_selected_pages
    = selection.coarse_safety_selected_pages;
  state.publish_diagnostics.coarse_safety_budget_pages
    = setup.coarse_safety_budget_pages;
  state.publish_diagnostics.coarse_safety_capacity_fit
    = selection.coarse_safety_capacity_fit;
  state.publish_diagnostics.predicted_coherent_publication
    = selection.predicted_coherent_publication;
  state.publish_diagnostics.same_frame_detail_pages
    = selection.same_frame_detail_pages;
  state.publish_diagnostics.feedback_requested_pages
    = selection.feedback_requested_pages;
  state.publish_diagnostics.feedback_refinement_pages
    = feedback_refinement_pages;
  state.publish_diagnostics.receiver_bootstrap_pages
    = receiver_bootstrap_pages;
  state.publish_diagnostics.current_frame_reinforcement_pages
    = current_frame_reinforcement_pages;
  state.publish_diagnostics.current_frame_reinforcement_reference_frame
    = current_frame_reinforcement_reference_frame;
  state.publish_diagnostics.previous_resident_pages
    = selection.previous_page_management_resident_count;
  state.publish_diagnostics.carried_resident_pages
    = state.publish_diagnostics.previous_resident_pages;
  state.publish_diagnostics.released_resident_pages = 0U;
  state.publish_diagnostics.dirty_resident_page_count
    = static_cast<std::uint32_t>(pending_resolve.dirty_resident_page_flags.size());
  state.publish_diagnostics.marked_dirty_pages = 0U;
  state.publish_diagnostics.reused_requested_pages = 0U;
  state.publish_diagnostics.allocated_pages = 0U;
  state.publish_diagnostics.evicted_pages = 0U;
  state.publish_diagnostics.allocation_failures = 0U;
  state.publish_diagnostics.rerasterized_pages = 0U;
}

auto VirtualShadowMapBackend::BuildDirectionalPendingResolveStage(
  const ViewId view_id, const DirectionalVirtualClipmapSetup& setup,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const DirectionalPreviousStateContext& previous_context,
  const ViewCacheEntry* previous_state,
  const engine::ViewConstants& view_constants, ViewCacheEntry& state) const
  -> void
{
  auto selection_result = BuildDirectionalSelectionResult(view_id, setup,
    shadow_caster_bounds, visible_receiver_bounds,
    previous_context.previous_metadata, previous_state, state);
  auto invalidation_result = BuildDirectionalInvalidationResult(setup,
    previous_context.previous_key, state.key,
    previous_context.previous_metadata, previous_context.previous_shadow_caster_bounds,
    shadow_caster_bounds,
    selection_result.address_space_compatible);
  const bool transition_publish_risk
    = !selection_result.previous_page_management_state_exists
    || !selection_result.address_space_compatible
    || invalidation_result.global_dirty_resident_contents;
  selection_result.predicted_coherent_publication
    = selection_result.coarse_safety_capacity_fit && !transition_publish_risk;

  PopulateDirectionalPendingResolve(state, setup, std::move(selection_result),
    std::move(invalidation_result), view_constants,
    static_cast<std::uint32_t>(visible_receiver_bounds.size()));
}

auto VirtualShadowMapBackend::BuildDirectionalVirtualViewState(
  const ViewId view_id, const engine::ViewConstants& view_constants,
  const engine::DirectionalShadowCandidate& candidate,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const ViewCacheEntry* previous_state, ViewCacheEntry& state) -> void
{
  const auto clipmap_setup
    = PrepareDirectionalVirtualClipmapSetup(view_constants, candidate,
      shadow_caster_bounds, visible_receiver_bounds, previous_state);
  if (!clipmap_setup.has_value() || !clipmap_setup->valid) {
    return;
  }

  const auto& setup = *clipmap_setup;
  InitializeDirectionalViewStateFromClipmapSetup(setup, shadow_caster_bounds, state);
  const auto previous_context
    = BuildDirectionalPreviousStateContext(previous_state);
  BuildDirectionalPendingResolveStage(
    view_id, setup, shadow_caster_bounds, visible_receiver_bounds,
    previous_context, previous_state, view_constants,
    state);
  state.directional_virtual_metadata.push_back(setup.metadata);
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
  if (resources.gpu_buffer
    && required_entry_count <= resources.entry_capacity) {
    return &resources;
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
  if (resources.gpu_buffer
    && required_entry_count <= resources.entry_capacity) {
    return &resources;
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
  return &resources;
}

auto VirtualShadowMapBackend::EnsureViewResolveResources(const ViewId view_id)
  -> ViewResolveResources*
{
  const auto required_physical_list_capacity
    = physical_pool_config_.physical_tile_capacity * 4U;

  auto [it, _] = view_resolve_resources_.try_emplace(view_id);
  auto& resources = it->second;
  if (resources.stats_gpu_buffer && resources.physical_page_metadata_gpu_buffer
    && resources.physical_page_metadata_upload_buffer
    && resources.mapped_physical_page_metadata_upload != nullptr
    && resources.physical_page_lists_gpu_buffer
    && resources.physical_page_lists_upload_buffer
    && resources.mapped_physical_page_lists_upload != nullptr
    && physical_pool_config_.physical_tile_capacity
      <= resources.physical_page_metadata_capacity
    && required_physical_list_capacity
      <= resources.physical_page_lists_capacity) {
    return &resources;
  }

  if (resources.physical_page_metadata_upload_buffer
    && resources.mapped_physical_page_metadata_upload != nullptr) {
    resources.physical_page_metadata_upload_buffer->UnMap();
    resources.mapped_physical_page_metadata_upload = nullptr;
  }
  if (resources.physical_page_lists_upload_buffer
    && resources.mapped_physical_page_lists_upload != nullptr) {
    resources.physical_page_lists_upload_buffer->UnMap();
    resources.mapped_physical_page_lists_upload = nullptr;
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

  auto dirty_page_flags_srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!dirty_page_flags_srv_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate dirty-page flags "
      "SRV for view {}",
      view_id.get());
    return nullptr;
  }
  resources.dirty_page_flags_srv
    = allocator.GetShaderVisibleIndex(dirty_page_flags_srv_handle);

  graphics::BufferViewDescription dirty_page_flags_srv_desc;
  dirty_page_flags_srv_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_SRV;
  dirty_page_flags_srv_desc.visibility
    = graphics::DescriptorVisibility::kShaderVisible;
  dirty_page_flags_srv_desc.range = { 0U, dirty_page_flags_bytes };
  dirty_page_flags_srv_desc.stride = sizeof(std::uint32_t);
  registry.RegisterView(*resources.dirty_page_flags_gpu_buffer,
    std::move(dirty_page_flags_srv_handle), dirty_page_flags_srv_desc);

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

  const graphics::BufferDesc physical_page_metadata_upload_desc {
    .size_bytes = physical_page_metadata_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowMapBackend.PhysicalPageMetadataReset",
  };
  resources.physical_page_metadata_upload_buffer
    = gfx_->CreateBuffer(physical_page_metadata_upload_desc);
  if (!resources.physical_page_metadata_upload_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create physical-page metadata "
      "upload buffer for view {}",
      view_id.get());
    return nullptr;
  }
  resources.mapped_physical_page_metadata_upload
    = static_cast<renderer::VirtualShadowPhysicalPageMetadata*>(
      resources.physical_page_metadata_upload_buffer->Map(
        0U, physical_page_metadata_bytes));
  if (resources.mapped_physical_page_metadata_upload == nullptr) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to map physical-page metadata "
      "upload buffer for view {}",
      view_id.get());
    resources.physical_page_metadata_upload_buffer.reset();
    return nullptr;
  }
  std::memset(resources.mapped_physical_page_metadata_upload, 0,
    static_cast<std::size_t>(physical_page_metadata_bytes));

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

  const graphics::BufferDesc physical_page_lists_upload_desc {
    .size_bytes = physical_page_lists_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowMapBackend.PhysicalPageListsReset",
  };
  resources.physical_page_lists_upload_buffer
    = gfx_->CreateBuffer(physical_page_lists_upload_desc);
  if (!resources.physical_page_lists_upload_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create physical-page lists "
      "upload buffer for view {}",
      view_id.get());
    return nullptr;
  }
  resources.mapped_physical_page_lists_upload
    = static_cast<renderer::VirtualShadowPhysicalPageListEntry*>(
      resources.physical_page_lists_upload_buffer->Map(
        0U, physical_page_lists_bytes));
  if (resources.mapped_physical_page_lists_upload == nullptr) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to map physical-page lists "
      "upload buffer for view {}",
      view_id.get());
    resources.physical_page_lists_upload_buffer.reset();
    return nullptr;
  }
  std::memset(resources.mapped_physical_page_lists_upload, 0,
    static_cast<std::size_t>(physical_page_lists_bytes));

  resources.physical_page_metadata_capacity
    = physical_pool_config_.physical_tile_capacity;
  resources.dirty_page_flags_capacity
    = static_cast<std::uint32_t>(dirty_page_flags_capacity);
  resources.physical_page_lists_capacity = physical_page_lists_capacity;
  resources.physical_page_state_reset_pending = true;
  return &resources;
}

auto VirtualShadowMapBackend::EnsureViewDirtyResidentPageResources(
  const ViewId view_id, const std::uint32_t required_entry_count)
  -> ViewDirtyResidentPageResources*
{
  if (required_entry_count == 0U) {
    return nullptr;
  }

  auto [it, _] = view_dirty_resident_page_resources_.try_emplace(view_id);
  auto& resources = it->second;
  if (resources.gpu_buffer && resources.upload_buffer
    && resources.mapped_upload != nullptr
    && required_entry_count <= resources.entry_capacity) {
    return &resources;
  }

  if (resources.upload_buffer && resources.mapped_upload != nullptr) {
    resources.upload_buffer->UnMap();
    resources.mapped_upload = nullptr;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();
  const auto size_bytes
    = static_cast<std::uint64_t>(required_entry_count)
    * sizeof(renderer::VirtualShadowDirtyResidentPageEntry);

  const graphics::BufferDesc gpu_desc {
    .size_bytes = size_bytes,
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowMapBackend.DirtyResidentPages",
  };
  resources.gpu_buffer = gfx_->CreateBuffer(gpu_desc);
  if (!resources.gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create dirty resident-page buffer "
      "for view {}",
      view_id.get());
    return nullptr;
  }
  registry.Register(resources.gpu_buffer);

  auto srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate dirty resident-page SRV "
      "for view {}",
      view_id.get());
    return nullptr;
  }
  resources.srv = allocator.GetShaderVisibleIndex(srv_handle);

  graphics::BufferViewDescription srv_desc;
  srv_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  srv_desc.range = { 0U, size_bytes };
  srv_desc.stride = sizeof(renderer::VirtualShadowDirtyResidentPageEntry);
  registry.RegisterView(*resources.gpu_buffer, std::move(srv_handle), srv_desc);

  const graphics::BufferDesc upload_desc {
    .size_bytes = size_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowMapBackend.DirtyResidentPagesUpload",
  };
  resources.upload_buffer = gfx_->CreateBuffer(upload_desc);
  if (!resources.upload_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create dirty resident-page upload "
      "buffer for view {}",
      view_id.get());
    return nullptr;
  }
  resources.mapped_upload
    = static_cast<renderer::VirtualShadowDirtyResidentPageEntry*>(
      resources.upload_buffer->Map(0U, size_bytes));
  if (resources.mapped_upload == nullptr) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to map dirty resident-page upload "
      "buffer for view {}",
      view_id.get());
    resources.upload_buffer.reset();
    return nullptr;
  }
  std::memset(resources.mapped_upload, 0, static_cast<std::size_t>(size_bytes));
  resources.entry_capacity = required_entry_count;
  return &resources;
}

auto VirtualShadowMapBackend::StagePageManagementSeedUpload(
  const ViewId view_id, ViewCacheEntry& state) -> void
{
  auto* resources = EnsureViewResolveResources(view_id);
  if (resources == nullptr) {
    return;
  }

  // Stage 5 ownership rule: a reset only zeroes persistent GPU page-management
  // state. Do not rebuild or upload a CPU-authored live residency snapshot here.
  resources->physical_page_state_reset_pending = true;
  state.physical_page_metadata_entries.clear();
  state.physical_page_list_entries.clear();
  state.resolve_stats = {};
  RefreshViewExports(view_id, state);
}

auto VirtualShadowMapBackend::StageDirtyResidentPageUpload(
  const ViewId view_id, ViewCacheEntry& state) -> void
{
  auto& pending = state.pending_residency_resolve;
  if (pending.dirty_resident_page_flags.empty()) {
    const auto resources_it = view_dirty_resident_page_resources_.find(view_id);
    if (resources_it != view_dirty_resident_page_resources_.end()) {
      resources_it->second.upload_count = 0U;
      resources_it->second.upload_pending = false;
    }
    RefreshViewExports(view_id, state);
    return;
  }

  auto* resources = EnsureViewDirtyResidentPageResources(
    view_id, static_cast<std::uint32_t>(pending.dirty_resident_page_flags.size()));
  if (resources == nullptr || resources->mapped_upload == nullptr) {
    return;
  }

  std::uint32_t upload_index = 0U;
  for (const auto& [resident_key, page_flags] : pending.dirty_resident_page_flags) {
    resources->mapped_upload[upload_index++] =
      renderer::VirtualShadowDirtyResidentPageEntry {
        .resident_key = resident_key,
        .page_flags = page_flags,
      };
  }
  resources->upload_count = upload_index;
  resources->upload_pending = upload_index > 0U;
  RefreshViewExports(view_id, state);
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
    case Decision::kTelemetryOnly:
      return "telemetry-only";
    default:
      return "unknown";
    }
  }();
  constexpr bool used_request_feedback = false;
  const auto selected_page_count = diagnostics.selected_page_count;
  const auto pending_raster_page_count = state.resolve_stats.pending_raster_page_count;
  const auto clip_valid_count
    = static_cast<std::uint32_t>(std::count(state.clipmap_cache_valid.begin(),
      state.clipmap_cache_valid.begin()
        + static_cast<std::ptrdiff_t>(std::min(diagnostics.clip_level_count,
          engine::kMaxVirtualDirectionalClipLevels)),
      true));

  LOG_F(INFO,
    "VirtualShadowMapBackend: frame={} view={} request={} feedback_keys={} "
    "feedback_age={} casters={} receivers={} clips={} coarse_begin={} "
    "cache_layout_valid={} depth_guardband_valid={} clip_cache_valid={} "
    "selected={} coarse={} coarse_safety_selected={} coarse_safety_budget={} "
    "coarse_safety_fit={} same_frame_detail={} feedback_seed={} "
    "feedback_refine={} receiver_bootstrap={} current_reinforce={} "
    "pending_raster_pages={} reused={} allocated={} evicted={} "
    "alloc_failures={} rerasterized={}",
    frame_sequence_.get(), view_id.get(), feedback_reason,
    diagnostics.feedback_key_count, diagnostics.feedback_age_frames,
    diagnostics.shadow_caster_bound_count,
    diagnostics.visible_receiver_bound_count, diagnostics.clip_level_count,
    diagnostics.coarse_backbone_begin, diagnostics.cache_layout_compatible,
    diagnostics.depth_guardband_valid, clip_valid_count, selected_page_count,
    diagnostics.coarse_backbone_pages, diagnostics.coarse_safety_selected_pages,
    diagnostics.coarse_safety_budget_pages,
    diagnostics.coarse_safety_capacity_fit,
    diagnostics.same_frame_detail_pages,
    diagnostics.feedback_requested_pages, diagnostics.feedback_refinement_pages,
    diagnostics.receiver_bootstrap_pages,
    diagnostics.current_frame_reinforcement_pages, pending_raster_page_count,
    diagnostics.reused_requested_pages, diagnostics.allocated_pages,
    diagnostics.evicted_pages, diagnostics.allocation_failures,
    diagnostics.rerasterized_pages);
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
