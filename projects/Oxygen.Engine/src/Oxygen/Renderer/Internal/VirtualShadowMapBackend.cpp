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
constexpr std::int32_t kFeedbackRequestGuardRadius = 1;
constexpr std::uint64_t kMaxRequestFeedbackAgeFrames
  = oxygen::frame::kFramesInFlight.get() + 1U;
constexpr std::size_t kCompatibleFeedbackHashHistoryLimit
  = static_cast<std::size_t>(kMaxRequestFeedbackAgeFrames + 1U);
constexpr std::uint64_t kMaxResolvedRasterScheduleAgeFrames
  = oxygen::frame::kFramesInFlight.get() + 1U;
constexpr std::uint32_t kCoarseBackboneGuardPages = 1U;
constexpr std::uint32_t kReceiverBootstrapFineClipCount = 3U;
constexpr std::uint32_t kAcceptedFeedbackCurrentFrameReinforcementClipCount
  = 3U;
constexpr std::uint32_t kAcceptedFeedbackCurrentFrameGuardPages = 1U;
constexpr std::uint32_t kAcceptedFeedbackCurrentFrameMaxDeltaPagesPerClip
  = 128U;
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

struct CanonicalShadowCasterInput {
  std::vector<glm::vec4> bounds;
  std::vector<std::uint8_t> static_flags;
};

[[nodiscard]] auto CanonicalizeShadowCasterInput(
  const std::span<const glm::vec4> bounds,
  const std::span<const std::uint8_t> static_flags) -> CanonicalShadowCasterInput
{
  CanonicalShadowCasterInput result {};
  result.bounds.resize(bounds.size());
  if (static_flags.size() == bounds.size()) {
    result.static_flags.resize(static_flags.size());
  }

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
    if (result.static_flags.size() == order.size()) {
      result.static_flags[sorted_index] = static_flags[source_index];
    }
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
  for (auto& [_, resources] : view_page_table_resources_) {
    if (resources.upload_buffer && resources.mapped_upload != nullptr) {
      resources.upload_buffer->UnMap();
      resources.mapped_upload = nullptr;
    }
  }
  for (auto& [_, resources] : view_page_flags_resources_) {
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
  const std::uint64_t shadow_caster_content_hash,
  const std::span<const std::uint8_t> shadow_caster_static_flags)
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

  const auto canonical_shadow_caster_input = CanonicalizeShadowCasterInput(
    shadow_caster_bounds, shadow_caster_static_flags);
  const auto& canonical_shadow_caster_bounds
    = canonical_shadow_caster_input.bounds;
  const auto canonical_shadow_caster_bounds_span
    = std::span<const glm::vec4>(canonical_shadow_caster_bounds.data(),
      canonical_shadow_caster_bounds.size());
  const auto canonical_shadow_caster_static_flags_span
    = std::span<const std::uint8_t>(
      canonical_shadow_caster_input.static_flags.data(),
      canonical_shadow_caster_input.static_flags.size());

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
    canonical_shadow_caster_static_flags_span,
    visible_receiver_bounds,
    previous_it != view_cache_.end() ? &previous_it->second : nullptr, state);
  state.resolved_raster_pages.clear();
  state.publish_diagnostics.resident_reuse_gate_open = false;

  const auto published_shadow_instances
    = std::span<const engine::ShadowInstanceMetadata> {
        state.shadow_instances
      };
  const auto published_directional_virtual_metadata
    = std::span<const engine::DirectionalVirtualShadowMetadata> {
        state.directional_virtual_metadata
      };
  const auto published_page_table_entries
    = std::span<const std::uint32_t> { state.page_table_entries };
  const auto published_page_flags_entries
    = std::span<const std::uint32_t> { state.page_flags_entries };

  state.frame_publication.shadow_instance_metadata_srv
    = PublishShadowInstances(published_shadow_instances);
  state.frame_publication.virtual_directional_shadow_metadata_srv
    = PublishDirectionalVirtualMetadata(published_directional_virtual_metadata);
  const auto* page_management_table_resources
    = EnsureViewPageManagementPageTableResources(
      view_id, static_cast<std::uint32_t>(published_page_table_entries.size()));
  const auto* page_management_flag_resources
    = EnsureViewPageManagementPageFlagResources(
      view_id, static_cast<std::uint32_t>(published_page_flags_entries.size()));
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
  RebuildResolveStateSnapshot(it->second);
  RebuildPhysicalPageManagementSnapshot(
    it->second, it->second.pending_residency_resolve);
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
  // the explicit resolve stage. Once the live GPU raster pass has executed,
  // every resident page that remained dirty/pending has been consumed by the
  // GPU-authored schedule and can transition to clean without consulting the
  // CPU telemetry mirror.
  for (auto& [_, resident_page] : it->second.resident_pages) {
    if (resident_page.state != renderer::VirtualPageResidencyState::kResidentDirty
      && resident_page.state
        != renderer::VirtualPageResidencyState::kPendingRender) {
      continue;
    }

    resident_page.state = renderer::VirtualPageResidencyState::kResidentClean;
    resident_page.last_touched_frame = frame_sequence_;
  }
  it->second.has_rendered_cache_history
    = std::ranges::any_of(it->second.resident_pages,
      [](const auto& entry) { return entry.second.ContentsValid(); });
  it->second.resolved_raster_pages.clear();
  RebuildResolveStateSnapshot(it->second);
  RebuildPhysicalPageManagementSnapshot(
    it->second, it->second.pending_residency_resolve);
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
  std::fill(
    state.page_flags_entries.begin(), state.page_flags_entries.end(), 0U);
  state.physical_page_metadata_entries.clear();
  state.physical_page_list_entries.clear();

  std::uint32_t reused_requested_pages = 0U;
  std::uint32_t allocated_pages = 0U;
  std::uint32_t evicted_pages = 0U;
  std::uint32_t allocation_failures = 0U;
  std::uint32_t rerasterized_pages = 0U;
  std::uint32_t released_resident_pages = 0U;
  std::uint32_t marked_dirty_pages = 0U;

  state.resident_pages.clear();
  state.has_rendered_cache_history = false;
  CarryForwardCompatibleDirectionalResidentPages(
    state, pending, released_resident_pages, marked_dirty_pages);
  auto requested_resident_keys = BuildRequestedResidentKeySet(pending);
  auto eviction_candidates
    = BuildEvictionCandidateList(state, requested_resident_keys);
  std::size_t next_eviction_candidate_index = 0U;

  const auto process_clip_range = [&](const std::uint32_t begin_clip,
                                    const std::uint32_t end_clip,
                                    const bool descending) {
    const auto process_clip = [&](const std::uint32_t clip_index) {
      const float page_world_size = pending.clip_page_world[clip_index];
      const float origin_x = pending.clip_origin_x[clip_index];
      const float origin_y = pending.clip_origin_y[clip_index];
        const std::uint32_t filter_guard_texels
          = shadow_detail::ResolveDirectionalVirtualGuardTexels(
            physical_pool_config_.page_size_texels,
            SelectDirectionalVirtualFilterRadiusTexels(
              pending.clip_page_world[0], pending.clip_page_world[clip_index]));
      const float interior_texels = std::max(1.0F,
        static_cast<float>(physical_pool_config_.page_size_texels)
          - 2.0F * static_cast<float>(filter_guard_texels));
      const float page_guard_world = static_cast<float>(filter_guard_texels)
        * (page_world_size / interior_texels);
      const auto grid_offset_x = pending.clip_grid_origin_x[clip_index];
      const auto grid_offset_y = pending.clip_grid_origin_y[clip_index];
      const auto process_selected_page = [&](const std::uint32_t page_x,
                                           const std::uint32_t page_y) {
        const std::uint32_t local_page_index
          = page_y * pending.pages_per_axis + page_x;
        const std::uint32_t global_page_index
          = clip_index * pending.pages_per_level + local_page_index;
        if (pending.selected_pages[global_page_index] == 0U) {
          return;
        }

        const auto resident_key = shadow_detail::PackVirtualResidentPageKey(
          clip_index, grid_offset_x + static_cast<std::int32_t>(page_x),
          grid_offset_y + static_cast<std::int32_t>(page_y));
        const bool static_dirty
          = pending.dirty_static_resident_pages.contains(resident_key);
        const bool dynamic_dirty
          = pending.dirty_dynamic_resident_pages.contains(resident_key);

        bool needs_raster = true;
        ResidentVirtualPage resident_page {};
        if (const auto resident_it = state.resident_pages.find(resident_key);
          resident_it != state.resident_pages.end()) {
          resident_page = resident_it->second;
          needs_raster = !resident_page.ContentsValid()
            || !pending.reusable_clip_contents[clip_index];
          ++reused_requested_pages;
        } else {
          const auto allocated_tile = AcquirePhysicalTile(state,
            eviction_candidates, next_eviction_candidate_index, evicted_pages);
          if (!allocated_tile.has_value()) {
            ++allocation_failures;
            return;
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
        state.page_flags_entries[global_page_index]
          = renderer::MakeVirtualShadowPageFlags(true,
            needs_raster && (dynamic_dirty || !static_dirty),
            needs_raster && static_dirty,
            clip_index < pending.coarse_backbone_begin, true);

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
      };

      for (std::uint32_t page_y = 0U; page_y < pending.pages_per_axis;
        ++page_y) {
        for (std::uint32_t page_x = 0U; page_x < pending.pages_per_axis;
          ++page_x) {
          process_selected_page(page_x, page_y);
        }
      }
    };

    if (descending) {
      for (std::uint32_t clip_index = end_clip; clip_index-- > begin_clip;) {
        process_clip(clip_index);
      }
      return;
    }

    for (std::uint32_t clip_index = begin_clip; clip_index < end_clip;
      ++clip_index) {
      process_clip(clip_index);
    }
  };

  const std::uint32_t coarse_backbone_begin = pending.coarse_backbone_begin;
  // Guardrail: do not reorder coarse/detail allocation ahead of the accepted
  // fine-feedback authority chain. The live path still relies on that chain to
  // repopulate fine current pages after compatible motion; changing ordering in
  // isolation can turn the view from coarse fallback into total no-page
  // publication even while the page-table diagnostics still look coherent.
  //
  // Bootstrap is the one exception: when accepted fine feedback is absent or
  // unusable, spending the whole pool on coarser clips makes the view resolve
  // almost entirely from visibly wrong fallback pages. In that window, allocate
  // the finest detail clips first and let the coarse backbone consume only the
  // remaining capacity.
  if (pending.bootstrap_prefers_finest_detail_pages) {
    process_clip_range(0U, coarse_backbone_begin, false);
    process_clip_range(coarse_backbone_begin, pending.clip_level_count, true);
  } else {
    process_clip_range(coarse_backbone_begin, pending.clip_level_count, true);
    process_clip_range(0U, coarse_backbone_begin, true);
  }

  PopulateDirectionalFallbackPageTableEntries(state, pending);
  PropagateDirectionalHierarchicalPageFlags(state, pending);
  RebuildResolvedRasterPagesFromPublishedCurrentPages(state, pending);
  rerasterized_pages
    = static_cast<std::uint32_t>(state.resolved_raster_pages.size());

  // Reuse gating is diagnostic-only unless the current frame has already
  // proven it needs no raster work. Dirty carried pages must never suppress
  // rerasterization just because the metadata/page-table contract still
  // compares equal.
  state.publish_diagnostics.resident_reuse_gate_open
    = pending.resident_reuse_snapshot.valid
    && pending.resident_reuse_snapshot.previous_pending_resolved_pages_empty
    && marked_dirty_pages == 0U && state.resolved_raster_pages.empty()
    && CanReuseResidentPages(pending.resident_reuse_snapshot, state);

  state.publish_diagnostics.carried_resident_pages
    = static_cast<std::uint32_t>(state.resident_pages.size());
  state.publish_diagnostics.released_resident_pages = released_resident_pages;
  state.publish_diagnostics.marked_dirty_pages = marked_dirty_pages;
  state.publish_diagnostics.reused_requested_pages = reused_requested_pages;
  state.publish_diagnostics.allocated_pages = allocated_pages;
  state.publish_diagnostics.evicted_pages = evicted_pages;
  state.publish_diagnostics.allocation_failures = allocation_failures;
  state.publish_diagnostics.rerasterized_pages = rerasterized_pages;
  state.has_rendered_cache_history = std::ranges::any_of(state.resident_pages,
    [](const auto& entry) { return entry.second.ContentsValid(); });

  pending.previous_resident_pages.clear();
  pending.previous_shadow_caster_bounds.clear();
  pending.previous_shadow_caster_static_flags.clear();
  pending.dirty_resident_pages.clear();
  pending.dirty_static_resident_pages.clear();
  pending.dirty_dynamic_resident_pages.clear();
  pending.dirty = false;
  RebuildResolveStateSnapshot(state);
  RebuildPhysicalPageManagementSnapshot(state, pending);
  RebuildPublishedCurrentPagesFromPageManagementSnapshot(state, pending);
  PopulateDirectionalFallbackPageTableEntries(state, pending);
  PropagateDirectionalHierarchicalPageFlags(state, pending);
  RebuildResolvedRasterPagesFromPublishedCurrentPages(state, pending);
  state.resolve_stats.mapped_page_count
    = static_cast<std::uint32_t>(std::count_if(state.page_table_entries.begin(),
      state.page_table_entries.end(), [](const std::uint32_t entry) {
        return entry != 0U
          && renderer::VirtualShadowPageTableEntryHasCurrentLod(entry);
      }));
  state.resolve_stats.pending_raster_page_count
    = static_cast<std::uint32_t>(state.resolved_raster_pages.size());
  StageResolveStateUpload(view_id, state);
  RefreshAtlasTileDebugStates(state);
  RefreshViewExports(view_id, state);
  LogPublishTransition(view_id, state);
}

auto VirtualShadowMapBackend::RebuildResolvedRasterPagesFromPublishedCurrentPages(
  ViewCacheEntry& state,
  const ViewCacheEntry::PendingResidencyResolve& pending) const -> void
{
  state.resolved_raster_pages.clear();
  if (pending.clip_level_count == 0U || pending.pages_per_axis == 0U
    || pending.pages_per_level == 0U || state.page_table_entries.empty()
    || state.page_flags_entries.size() != state.page_table_entries.size()) {
    return;
  }

  state.resolved_raster_pages.reserve(state.page_table_entries.size());

  for (std::uint32_t clip_index = 0U; clip_index < pending.clip_level_count;
    ++clip_index) {
    const float page_world_size = pending.clip_page_world[clip_index];
    const float origin_x = pending.clip_origin_x[clip_index];
    const float origin_y = pending.clip_origin_y[clip_index];
    const std::uint32_t filter_guard_texels
      = shadow_detail::ResolveDirectionalVirtualGuardTexels(
        physical_pool_config_.page_size_texels,
        SelectDirectionalVirtualFilterRadiusTexels(
          pending.clip_page_world[0], pending.clip_page_world[clip_index]));
    const float interior_texels = std::max(1.0F,
      static_cast<float>(physical_pool_config_.page_size_texels)
        - static_cast<float>(filter_guard_texels * 2U));
    const float page_guard_world
      = page_world_size
      * (static_cast<float>(filter_guard_texels) / interior_texels);

    for (std::uint32_t page_y = 0U; page_y < pending.pages_per_axis; ++page_y) {
      for (std::uint32_t page_x = 0U; page_x < pending.pages_per_axis;
        ++page_x) {
        const auto local_page_index = page_y * pending.pages_per_axis + page_x;
        const auto global_page_index
          = clip_index * pending.pages_per_level + local_page_index;
        if (global_page_index >= state.page_table_entries.size()) {
          continue;
        }

        const auto decoded_entry = renderer::DecodeVirtualShadowPageTableEntry(
          state.page_table_entries[global_page_index]);
        if (!decoded_entry.current_lod_valid) {
          continue;
        }

        const auto page_flags = state.page_flags_entries[global_page_index];
        const bool uncached = renderer::HasVirtualShadowPageFlag(
                                page_flags,
                                renderer::VirtualShadowPageFlag::kDynamicUncached)
          || renderer::HasVirtualShadowPageFlag(
            page_flags, renderer::VirtualShadowPageFlag::kStaticUncached);
        if (!uncached) {
          continue;
        }

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

        const auto resident_key = shadow_detail::PackVirtualResidentPageKey(
          clip_index,
          pending.clip_grid_origin_x[clip_index]
            + static_cast<std::int32_t>(page_x),
          pending.clip_grid_origin_y[clip_index]
            + static_cast<std::int32_t>(page_y));

        state.resolved_raster_pages.push_back(
          renderer::VirtualShadowResolvedRasterPage {
            .shadow_instance_index = 0U,
            .payload_index = 0U,
            .clip_level = clip_index,
            .page_index = local_page_index,
            .resident_key = resident_key,
            .atlas_tile_x = static_cast<std::uint16_t>(decoded_entry.tile_x),
            .atlas_tile_y = static_cast<std::uint16_t>(decoded_entry.tile_y),
            .view_constants = page_view_constants.GetSnapshot(),
          });
      }
    }
  }
}

auto VirtualShadowMapBackend::RebuildPublishedCurrentPagesFromPageManagementSnapshot(
  ViewCacheEntry& state,
  const ViewCacheEntry::PendingResidencyResolve& pending) const -> void
{
  if (!pending.valid || pending.clip_level_count == 0U
    || pending.pages_per_axis == 0U || pending.pages_per_level == 0U
    || state.page_table_entries.empty()
    || state.page_flags_entries.size() != state.page_table_entries.size()) {
    return;
  }

  std::fill(state.page_table_entries.begin(), state.page_table_entries.end(), 0U);
  std::fill(state.page_flags_entries.begin(), state.page_flags_entries.end(), 0U);

  const auto current_entry_count = std::min<std::size_t>(
    state.physical_page_list_entries.size(),
    static_cast<std::size_t>(state.resolve_stats.requested_page_list_count)
      + static_cast<std::size_t>(state.resolve_stats.dirty_page_list_count)
      + static_cast<std::size_t>(state.resolve_stats.clean_page_list_count));

  for (std::size_t entry_index = 0U; entry_index < current_entry_count;
    ++entry_index) {
    const auto& list_entry = state.physical_page_list_entries[entry_index];
    if (list_entry.physical_page_index == 0xFFFFFFFFU
      || list_entry.physical_page_index
        >= state.physical_page_metadata_entries.size()) {
      continue;
    }

    const auto clip_index
      = shadow_detail::VirtualResidentPageKeyClipLevel(list_entry.resident_key);
    if (clip_index >= pending.clip_level_count) {
      continue;
    }

    const auto local_page_x
      = shadow_detail::VirtualResidentPageKeyGridX(list_entry.resident_key)
      - pending.clip_grid_origin_x[clip_index];
    const auto local_page_y
      = shadow_detail::VirtualResidentPageKeyGridY(list_entry.resident_key)
      - pending.clip_grid_origin_y[clip_index];
    if (local_page_x < 0 || local_page_y < 0
      || local_page_x >= static_cast<std::int32_t>(pending.pages_per_axis)
      || local_page_y >= static_cast<std::int32_t>(pending.pages_per_axis)) {
      continue;
    }

    const auto local_page_index
      = static_cast<std::uint32_t>(local_page_y) * pending.pages_per_axis
      + static_cast<std::uint32_t>(local_page_x);
    const auto global_page_index
      = clip_index * pending.pages_per_level + local_page_index;
    if (global_page_index >= state.page_table_entries.size()) {
      continue;
    }

    const auto& metadata
      = state.physical_page_metadata_entries[list_entry.physical_page_index];
    const bool dynamic_uncached = renderer::HasVirtualShadowPageFlag(
      list_entry.page_flags, renderer::VirtualShadowPageFlag::kDynamicUncached);
    const bool static_uncached = renderer::HasVirtualShadowPageFlag(
      list_entry.page_flags, renderer::VirtualShadowPageFlag::kStaticUncached);

    state.page_table_entries[global_page_index] = PackPageTableEntry(
      metadata.AtlasTileX(), metadata.AtlasTileY(), 0U, true, true, true);
    state.page_flags_entries[global_page_index]
      = renderer::MakeVirtualShadowPageFlags(
        true, dynamic_uncached, static_uncached, false, false);
  }
}

auto VirtualShadowMapBackend::CarryForwardCompatibleDirectionalResidentPages(
  ViewCacheEntry& state, const ViewCacheEntry::PendingResidencyResolve& pending,
  std::uint32_t& released_page_count, std::uint32_t& marked_dirty_page_count)
  -> void
{
  if (pending.previous_resident_pages.empty()) {
    return;
  }

  if (!pending.address_space_compatible) {
    for (const auto& [_, resident_page] : pending.previous_resident_pages) {
      ReleasePhysicalTile(resident_page.tile);
      ++released_page_count;
    }
    return;
  }

  for (const auto& [previous_resident_key, previous_resident_page] :
    pending.previous_resident_pages) {
    const auto clip_index
      = shadow_detail::VirtualResidentPageKeyClipLevel(previous_resident_key);
    if (clip_index >= pending.clip_level_count
      || !pending.previous_clip_cache_valid[clip_index]) {
      ReleasePhysicalTile(previous_resident_page.tile);
      ++released_page_count;
      continue;
    }

    // Oxygen resident keys already live in absolute clip page space. Under
    // compatible motion the new local clipmap origin should rediscover the
    // same absolute pages; rebasing the key would double-shift identity and
    // bind the wrong physical contents.
    const auto absolute_grid_x
      = shadow_detail::VirtualResidentPageKeyGridX(previous_resident_key);
    const auto absolute_grid_y
      = shadow_detail::VirtualResidentPageKeyGridY(previous_resident_key);
    const auto local_x
      = absolute_grid_x - pending.clip_grid_origin_x[clip_index];
    const auto local_y
      = absolute_grid_y - pending.clip_grid_origin_y[clip_index];
    if (local_x < 0 || local_y < 0
      || local_x >= static_cast<std::int32_t>(pending.pages_per_axis)
      || local_y >= static_cast<std::int32_t>(pending.pages_per_axis)) {
      ReleasePhysicalTile(previous_resident_page.tile);
      ++released_page_count;
      continue;
    }

    auto carried_page = previous_resident_page;
    if ((pending.global_dirty_resident_contents
          || !pending.reusable_clip_contents[clip_index]
          || pending.dirty_resident_pages.contains(previous_resident_key))
      && carried_page.state
        == renderer::VirtualPageResidencyState::kResidentClean) {
      carried_page.state = renderer::VirtualPageResidencyState::kResidentDirty;
      ++marked_dirty_page_count;
    }

    state.resident_pages.insert_or_assign(previous_resident_key, carried_page);
  }
}

auto VirtualShadowMapBackend::PopulateDirectionalFallbackPageTableEntries(
  ViewCacheEntry& state, const ViewCacheEntry::PendingResidencyResolve& pending)
  -> void
{
  if (pending.clip_level_count == 0U || pending.pages_per_axis == 0U
    || pending.pages_per_level == 0U) {
    return;
  }

  for (std::uint32_t clip_index = pending.clip_level_count;
    clip_index-- > 0U;) {
    if (clip_index + 1U >= pending.clip_level_count) {
      continue;
    }

    const auto clip_page_world = pending.clip_page_world[clip_index];
    const auto clip_origin_x = pending.clip_origin_x[clip_index];
    const auto clip_origin_y = pending.clip_origin_y[clip_index];
    for (std::uint32_t page_y = 0U; page_y < pending.pages_per_axis; ++page_y) {
      for (std::uint32_t page_x = 0U; page_x < pending.pages_per_axis;
        ++page_x) {
        const auto local_page_index = page_y * pending.pages_per_axis + page_x;
        const auto global_page_index
          = clip_index * pending.pages_per_level + local_page_index;
        if (global_page_index >= state.page_table_entries.size()) {
          continue;
        }

        const auto current_entry = state.page_table_entries[global_page_index];
        if (current_entry != 0U
          && renderer::VirtualShadowPageTableEntryHasCurrentLod(
            current_entry)) {
          continue;
        }

        const auto page_center_x = clip_origin_x
          + (static_cast<float>(page_x) + 0.5F) * clip_page_world;
        const auto page_center_y = clip_origin_y
          + (static_cast<float>(page_y) + 0.5F) * clip_page_world;

        for (std::uint32_t candidate_clip = clip_index + 1U;
          candidate_clip < pending.clip_level_count; ++candidate_clip) {
          const auto candidate_page_world
            = pending.clip_page_world[candidate_clip];
          const auto candidate_page_x_f
            = (page_center_x - pending.clip_origin_x[candidate_clip])
            / candidate_page_world;
          const auto candidate_page_y_f
            = (page_center_y - pending.clip_origin_y[candidate_clip])
            / candidate_page_world;
          if (candidate_page_x_f < 0.0F || candidate_page_y_f < 0.0F
            || candidate_page_x_f >= static_cast<float>(pending.pages_per_axis)
            || candidate_page_y_f
              >= static_cast<float>(pending.pages_per_axis)) {
            continue;
          }

          const auto candidate_page_x = static_cast<std::uint32_t>(
            std::min(static_cast<float>(pending.pages_per_axis - 1U),
              std::max(0.0F, std::floor(candidate_page_x_f))));
          const auto candidate_page_y = static_cast<std::uint32_t>(
            std::min(static_cast<float>(pending.pages_per_axis - 1U),
              std::max(0.0F, std::floor(candidate_page_y_f))));
          const auto candidate_global_page_index
            = candidate_clip * pending.pages_per_level
            + candidate_page_y * pending.pages_per_axis + candidate_page_x;
          if (candidate_global_page_index >= state.page_table_entries.size()) {
            continue;
          }

          const auto candidate_entry
            = state.page_table_entries[candidate_global_page_index];
          if (!renderer::VirtualShadowPageTableEntryHasAnyLod(
                candidate_entry)) {
            continue;
          }
          if (candidate_global_page_index >= state.page_flags_entries.size()
            || !HasPageManagementHierarchyVisibility(
              state.page_flags_entries[candidate_global_page_index])) {
            continue;
          }

          const auto candidate_decoded
            = renderer::DecodeVirtualShadowPageTableEntry(candidate_entry);
          const auto resolved_fallback_clip
            = renderer::ResolveVirtualShadowFallbackClipIndex(
              candidate_clip, pending.clip_level_count, candidate_entry);
          if (resolved_fallback_clip < candidate_clip
            || resolved_fallback_clip >= pending.clip_level_count
            || resolved_fallback_clip <= clip_index) {
            continue;
          }

          state.page_table_entries[global_page_index] = PackPageTableEntry(
            candidate_decoded.tile_x, candidate_decoded.tile_y,
            resolved_fallback_clip - clip_index, false, true, false);
          break;
        }
      }
    }
  }
}

auto VirtualShadowMapBackend::PreparePageTableResources(
  const ViewId view_id, graphics::CommandRecorder& recorder) -> void
{
  const auto state_it = view_cache_.find(view_id);
  if (state_it == view_cache_.end()) {
    return;
  }

  const auto page_table_resources_it = view_page_table_resources_.find(view_id);
  const auto page_flags_resources_it = view_page_flags_resources_.find(view_id);
  const auto page_management_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_management_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
  const bool has_page_table_resources
    = page_table_resources_it != view_page_table_resources_.end()
    && page_table_resources_it->second.gpu_buffer;
  const bool has_page_flags_resources
    = page_flags_resources_it != view_page_flags_resources_.end()
    && page_flags_resources_it->second.gpu_buffer;
  const bool has_page_management_table_resources
    = page_management_table_resources_it
      != view_page_management_page_table_resources_.end()
    && page_management_table_resources_it->second.gpu_buffer;
  const bool has_page_management_flags_resources
    = page_management_flags_resources_it
      != view_page_management_page_flags_resources_.end()
    && page_management_flags_resources_it->second.gpu_buffer;
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  const bool has_resolve_resources
    = resolve_resources_it != view_resolve_resources_.end();
  if (!has_page_table_resources && !has_page_flags_resources
    && !has_page_management_table_resources
    && !has_page_management_flags_resources && !has_resolve_resources) {
    return;
  }

  auto& state = state_it->second;
  if (has_page_table_resources) {
    auto& page_table_resources = page_table_resources_it->second;
    if (!recorder.IsResourceTracked(*page_table_resources.gpu_buffer)) {
      recorder.BeginTrackingResourceState(*page_table_resources.gpu_buffer,
        graphics::ResourceStates::kCommon, true);
    }
    recorder.RequireResourceState(*page_table_resources.gpu_buffer,
      graphics::ResourceStates::kShaderResource);
  }
  if (has_page_flags_resources) {
    auto& page_flags_resources = page_flags_resources_it->second;
    if (!recorder.IsResourceTracked(*page_flags_resources.gpu_buffer)) {
      recorder.BeginTrackingResourceState(*page_flags_resources.gpu_buffer,
        graphics::ResourceStates::kCommon, true);
    }
    recorder.RequireResourceState(*page_flags_resources.gpu_buffer,
      graphics::ResourceStates::kShaderResource);
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
    if (resolve_resources.physical_page_metadata_gpu_buffer) {
      if (!recorder.IsResourceTracked(
            *resolve_resources.physical_page_metadata_gpu_buffer)) {
        recorder.BeginTrackingResourceState(
          *resolve_resources.physical_page_metadata_gpu_buffer,
          graphics::ResourceStates::kCommon, true);
      }
      if (resolve_resources.physical_page_metadata_upload_pending
        && resolve_resources.physical_page_metadata_upload_buffer) {
        if (resolve_resources.mapped_physical_page_metadata_upload != nullptr
          && resolve_resources.physical_page_metadata_upload_count > 0U) {
          std::memcpy(resolve_resources.mapped_physical_page_metadata_upload,
            state.physical_page_metadata_entries.data(),
            static_cast<std::size_t>(
              resolve_resources.physical_page_metadata_upload_count)
              * sizeof(renderer::VirtualShadowPhysicalPageMetadata));
        }
        if (!recorder.IsResourceTracked(
              *resolve_resources.physical_page_metadata_upload_buffer)) {
          recorder.BeginTrackingResourceState(
            *resolve_resources.physical_page_metadata_upload_buffer,
            graphics::ResourceStates::kCopySource, false);
        }
        if (resolve_resources.physical_page_metadata_upload_count > 0U) {
          recorder.RequireResourceState(
            *resolve_resources.physical_page_metadata_gpu_buffer,
            graphics::ResourceStates::kCopyDest);
          recorder.FlushBarriers();
          recorder.CopyBuffer(
            *resolve_resources.physical_page_metadata_gpu_buffer, 0U,
            *resolve_resources.physical_page_metadata_upload_buffer, 0U,
            static_cast<std::size_t>(
              resolve_resources.physical_page_metadata_upload_count)
              * sizeof(renderer::VirtualShadowPhysicalPageMetadata));
        }
        resolve_resources.physical_page_metadata_upload_pending = false;
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
      if (resolve_resources.physical_page_lists_upload_pending
        && resolve_resources.physical_page_lists_upload_buffer) {
        if (resolve_resources.mapped_physical_page_lists_upload != nullptr
          && resolve_resources.physical_page_lists_upload_count > 0U) {
          std::memcpy(resolve_resources.mapped_physical_page_lists_upload,
            state.physical_page_list_entries.data(),
            static_cast<std::size_t>(
              resolve_resources.physical_page_lists_upload_count)
              * sizeof(renderer::VirtualShadowPhysicalPageListEntry));
        }
        if (!recorder.IsResourceTracked(
              *resolve_resources.physical_page_lists_upload_buffer)) {
          recorder.BeginTrackingResourceState(
            *resolve_resources.physical_page_lists_upload_buffer,
            graphics::ResourceStates::kCopySource, false);
        }
        if (resolve_resources.physical_page_lists_upload_count > 0U) {
          recorder.RequireResourceState(
            *resolve_resources.physical_page_lists_gpu_buffer,
            graphics::ResourceStates::kCopyDest);
          recorder.FlushBarriers();
          recorder.CopyBuffer(*resolve_resources.physical_page_lists_gpu_buffer,
            0U, *resolve_resources.physical_page_lists_upload_buffer, 0U,
            static_cast<std::size_t>(
              resolve_resources.physical_page_lists_upload_count)
              * sizeof(renderer::VirtualShadowPhysicalPageListEntry));
        }
        resolve_resources.physical_page_lists_upload_pending = false;
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

  // Check the coherence readback from the previous cycle on this slot.
  // By the time the slot is reused (kFramesInFlight frames later), the GPU
  // work that wrote the readback is guaranteed to be complete.
  if (frame_slot_ != frame::kInvalidSlot) {
    CheckCoherenceReadback(coherence_readbacks_[frame_slot_.get()]);
  }

  if (state.pending_residency_resolve.valid) {
    RebuildPublishedCurrentPagesFromPageManagementSnapshot(
      state, state.pending_residency_resolve);
    PopulateDirectionalFallbackPageTableEntries(
      state, state.pending_residency_resolve);
    PropagateDirectionalHierarchicalPageFlags(
      state, state.pending_residency_resolve);
    RebuildResolvedRasterPagesFromPublishedCurrentPages(
      state, state.pending_residency_resolve);
    state.resolve_stats.pending_raster_page_count
      = static_cast<std::uint32_t>(state.resolved_raster_pages.size());
    state.introspection.pending_raster_page_count
      = state.resolve_stats.pending_raster_page_count;
    RefreshViewExports(view_id, state);
  }

  // Initiate coherence readback: copy page-management GPU buffers to readback
  // so the next slot cycle can compare them against the CPU reference.
  const auto entry_count
    = static_cast<std::uint32_t>(state.page_table_entries.size());
  if (frame_slot_ != frame::kInvalidSlot && entry_count > 0U) {
    const auto readback_bytes
      = static_cast<std::uint64_t>(entry_count) * sizeof(std::uint32_t);
    auto& readback = coherence_readbacks_[frame_slot_.get()];
    if (EnsureCoherenceReadbackBuffers(readback, readback_bytes)) {
      // Transition page-management to CopySource for the readback copy.
      recorder.RequireResourceState(*page_management_table_resources.gpu_buffer,
        graphics::ResourceStates::kCopySource);
      recorder.RequireResourceState(*page_management_flags_resources.gpu_buffer,
        graphics::ResourceStates::kCopySource);
      if (!recorder.IsResourceTracked(*readback.page_table_readback)) {
        recorder.BeginTrackingResourceState(*readback.page_table_readback,
          graphics::ResourceStates::kCopyDest, false);
      }
      if (!recorder.IsResourceTracked(*readback.page_flags_readback)) {
        recorder.BeginTrackingResourceState(*readback.page_flags_readback,
          graphics::ResourceStates::kCopyDest, false);
      }
      recorder.RequireResourceState(
        *readback.page_table_readback, graphics::ResourceStates::kCopyDest);
      recorder.RequireResourceState(
        *readback.page_flags_readback, graphics::ResourceStates::kCopyDest);
      recorder.FlushBarriers();

      recorder.CopyBuffer(*readback.page_table_readback, 0U,
        *page_management_table_resources.gpu_buffer, 0U, readback_bytes);
      recorder.CopyBuffer(*readback.page_flags_readback, 0U,
        *page_management_flags_resources.gpu_buffer, 0U, readback_bytes);

      // Snapshot the CPU reference for this slot so the comparison can happen
      // when this slot is next reused.
      readback.cpu_page_table_snapshot.assign(
        state.page_table_entries.begin(), state.page_table_entries.end());
      readback.cpu_page_flags_snapshot.assign(
        state.page_flags_entries.begin(), state.page_flags_entries.end());
      readback.source_frame = frame_sequence_;
      readback.view_id = view_id;
      readback.entry_count = entry_count;
      readback.live_authority = true;
      readback.pending = true;
    }
  }

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
  auto& pending_feedback = request_feedback_[view_id];
  auto& channel = feedback.kind == VirtualShadowFeedbackKind::kCoarse
    ? pending_feedback.coarse
    : pending_feedback.detail;
  channel.feedback = std::move(feedback);
  channel.source_absolute_frustum_regions
    = std::move(source_absolute_frustum_regions);
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
      state.page_table_entries.end(), [](const std::uint32_t entry) {
        return entry != 0U
          && renderer::VirtualShadowPageTableEntryHasCurrentLod(entry);
      }));
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
  state.resolve_stats.requested_page_list_count = 0U;
  state.resolve_stats.dirty_page_list_count = 0U;
  state.resolve_stats.clean_page_list_count = 0U;
  state.resolve_stats.available_page_list_count = 0U;
}

auto VirtualShadowMapBackend::BuildRequestedResidentKeySet(
  const ViewCacheEntry::PendingResidencyResolve& pending) const
  -> std::unordered_set<std::uint64_t>
{
  std::unordered_set<std::uint64_t> requested_resident_keys {};
  requested_resident_keys.reserve(pending.selected_pages.size() / 8U);

  for (std::uint32_t clip_index = 0U; clip_index < pending.clip_level_count;
    ++clip_index) {
    const auto grid_offset_x = pending.clip_grid_origin_x[clip_index];
    const auto grid_offset_y = pending.clip_grid_origin_y[clip_index];
    for (std::uint32_t page_y = 0U; page_y < pending.pages_per_axis; ++page_y) {
      for (std::uint32_t page_x = 0U; page_x < pending.pages_per_axis;
        ++page_x) {
        const auto local_page_index = page_y * pending.pages_per_axis + page_x;
        const auto global_page_index
          = clip_index * pending.pages_per_level + local_page_index;
        if (global_page_index >= pending.selected_pages.size()
          || pending.selected_pages[global_page_index] == 0U) {
          continue;
        }

        requested_resident_keys.insert(
          shadow_detail::PackVirtualResidentPageKey(clip_index,
            grid_offset_x + static_cast<std::int32_t>(page_x),
            grid_offset_y + static_cast<std::int32_t>(page_y)));
      }
    }
  }

  return requested_resident_keys;
}

auto VirtualShadowMapBackend::BuildEvictionCandidateList(
  const ViewCacheEntry& state,
  const std::unordered_set<std::uint64_t>& protected_resident_keys) const
  -> std::vector<std::uint64_t>
{
  std::vector<std::uint64_t> eviction_candidates {};
  eviction_candidates.reserve(state.resident_pages.size());

  for (const auto& [resident_key, resident_page] : state.resident_pages) {
    if (resident_page.last_requested_frame == frame_sequence_
      || protected_resident_keys.contains(resident_key)) {
      continue;
    }
    eviction_candidates.push_back(resident_key);
  }

  std::ranges::sort(
    eviction_candidates, [&](const std::uint64_t lhs, const std::uint64_t rhs) {
      const auto lhs_it = state.resident_pages.find(lhs);
      const auto rhs_it = state.resident_pages.find(rhs);
      DCHECK_F(lhs_it != state.resident_pages.end(),
        "Missing lhs resident page while sorting eviction candidates");
      DCHECK_F(rhs_it != state.resident_pages.end(),
        "Missing rhs resident page while sorting eviction candidates");

      return shadow_detail::CompareVirtualResidentEvictionPriority(lhs,
        lhs_it->second.ContentsValid(), lhs_it->second.last_touched_frame.get(),
        rhs, rhs_it->second.ContentsValid(),
        rhs_it->second.last_touched_frame.get());
    });

  return eviction_candidates;
}

auto VirtualShadowMapBackend::PropagateDirectionalHierarchicalPageFlags(
  ViewCacheEntry& state,
  const ViewCacheEntry::PendingResidencyResolve& pending) const -> void
{
  if (pending.clip_level_count < 2U || pending.pages_per_axis == 0U
    || pending.pages_per_level == 0U) {
    return;
  }

  for (std::uint32_t fine_clip = 0U; fine_clip + 1U < pending.clip_level_count;
    ++fine_clip) {
    const auto parent_clip = fine_clip + 1U;
    const auto fine_page_world = pending.clip_page_world[fine_clip];
    const auto fine_origin_x = pending.clip_origin_x[fine_clip];
    const auto fine_origin_y = pending.clip_origin_y[fine_clip];
    const auto parent_page_world = pending.clip_page_world[parent_clip];
    const auto parent_origin_x = pending.clip_origin_x[parent_clip];
    const auto parent_origin_y = pending.clip_origin_y[parent_clip];

    for (std::uint32_t page_y = 0U; page_y < pending.pages_per_axis; ++page_y) {
      for (std::uint32_t page_x = 0U; page_x < pending.pages_per_axis;
        ++page_x) {
        const auto local_page_index = page_y * pending.pages_per_axis + page_x;
        const auto global_page_index
          = fine_clip * pending.pages_per_level + local_page_index;
        if (global_page_index >= state.page_flags_entries.size()) {
          continue;
        }

        const auto fine_flags = state.page_flags_entries[global_page_index];
        if (fine_flags == 0U) {
          continue;
        }

        const auto page_center_x = fine_origin_x
          + (static_cast<float>(page_x) + 0.5F) * fine_page_world;
        const auto page_center_y = fine_origin_y
          + (static_cast<float>(page_y) + 0.5F) * fine_page_world;
        const auto parent_page_x_f
          = (page_center_x - parent_origin_x) / parent_page_world;
        const auto parent_page_y_f
          = (page_center_y - parent_origin_y) / parent_page_world;
        if (parent_page_x_f < 0.0F || parent_page_y_f < 0.0F
          || parent_page_x_f >= static_cast<float>(pending.pages_per_axis)
          || parent_page_y_f >= static_cast<float>(pending.pages_per_axis)) {
          continue;
        }

        const auto parent_page_x
          = static_cast<std::uint32_t>(std::floor(parent_page_x_f));
        const auto parent_page_y
          = static_cast<std::uint32_t>(std::floor(parent_page_y_f));
        const auto parent_local_page_index
          = parent_page_y * pending.pages_per_axis + parent_page_x;
        const auto parent_global_page_index
          = parent_clip * pending.pages_per_level + parent_local_page_index;
        if (parent_global_page_index >= state.page_flags_entries.size()) {
          continue;
        }

        const auto parent_entry = renderer::DecodeVirtualShadowPageTableEntry(
          state.page_table_entries[parent_global_page_index]);
        if (!parent_entry.current_lod_valid) {
          continue;
        }

        state.page_flags_entries[parent_global_page_index]
          = renderer::MergeVirtualShadowHierarchyFlags(
            state.page_flags_entries[parent_global_page_index], fine_flags);
      }
    }
  }
}

auto VirtualShadowMapBackend::RebuildPhysicalPageManagementSnapshot(
  ViewCacheEntry& state,
  const ViewCacheEntry::PendingResidencyResolve& pending) const -> void
{
  const auto tile_capacity = physical_pool_config_.physical_tile_capacity;
  state.physical_page_metadata_entries.assign(tile_capacity, {});
  state.physical_page_list_entries.clear();
  state.physical_page_list_entries.reserve(
    state.resident_pages.size() + free_physical_tiles_.size());

  std::vector<renderer::VirtualShadowPhysicalPageListEntry>
    requested_entries {};
  std::vector<renderer::VirtualShadowPhysicalPageListEntry> dirty_entries {};
  std::vector<renderer::VirtualShadowPhysicalPageListEntry> clean_entries {};
  std::vector<renderer::VirtualShadowPhysicalPageListEntry>
    available_entries {};
  requested_entries.reserve(state.resident_pages.size());
  dirty_entries.reserve(state.resident_pages.size());
  clean_entries.reserve(state.resident_pages.size());
  available_entries.reserve(free_physical_tiles_.size());

  const auto classify_page_flags =
    [&](const std::uint64_t resident_key,
        const ResidentVirtualPage& resident_page) -> std::uint32_t {
    const auto clip_index
      = shadow_detail::VirtualResidentPageKeyClipLevel(resident_key);
    if (clip_index >= pending.clip_level_count) {
      return 0U;
    }
    const auto absolute_grid_x
      = shadow_detail::VirtualResidentPageKeyGridX(resident_key);
    const auto absolute_grid_y
      = shadow_detail::VirtualResidentPageKeyGridY(resident_key);
    const auto local_x
      = absolute_grid_x - pending.clip_grid_origin_x[clip_index];
    const auto local_y
      = absolute_grid_y - pending.clip_grid_origin_y[clip_index];
    if (local_x < 0 || local_y < 0
      || local_x >= static_cast<std::int32_t>(pending.pages_per_axis)
      || local_y >= static_cast<std::int32_t>(pending.pages_per_axis)) {
      return 0U;
    }
    const auto local_page_index
      = static_cast<std::uint32_t>(local_y) * pending.pages_per_axis
      + static_cast<std::uint32_t>(local_x);
    const auto global_page_index
      = clip_index * pending.pages_per_level + local_page_index;
    if (global_page_index >= state.page_flags_entries.size()) {
      return 0U;
    }
    // Phase 7 makes the page-management snapshot reconstruct residency /
    // invalidation base bits from resident state itself. USED/DETAIL remain
    // same-frame GPU marking semantics layered on top during resolve.
    const bool static_dirty
      = pending.dirty_static_resident_pages.contains(resident_key);
    const bool dynamic_dirty
      = pending.dirty_dynamic_resident_pages.contains(resident_key);
    const bool needs_raster = !resident_page.ContentsValid()
      || !pending.reusable_clip_contents[clip_index];
    const bool dynamic_uncached
      = needs_raster && (dynamic_dirty || !static_dirty);
    const bool static_uncached = needs_raster && static_dirty;
    return renderer::MakeVirtualShadowPageFlags(
      true, dynamic_uncached, static_uncached, false, false);
  };

  for (const auto& [resident_key, resident_page] : state.resident_pages) {
    const auto physical_page_index = static_cast<std::uint32_t>(
      resident_page.tile.tile_y * physical_pool_config_.atlas_tiles_per_axis
      + resident_page.tile.tile_x);
    if (physical_page_index < state.physical_page_metadata_entries.size()) {
      state.physical_page_metadata_entries[physical_page_index]
        = renderer::VirtualShadowPhysicalPageMetadata {
            .resident_key = resident_key,
            .page_flags = classify_page_flags(resident_key, resident_page),
            .packed_atlas_tile_coords
            = renderer::VirtualShadowPhysicalPageMetadata::PackAtlasTileCoords(
              resident_page.tile.tile_x, resident_page.tile.tile_y),
          };
    }

    const renderer::VirtualShadowPhysicalPageListEntry entry {
      .resident_key = resident_key,
      .physical_page_index = physical_page_index,
      .page_flags = classify_page_flags(resident_key, resident_page),
    };
    if (resident_page.last_requested_frame == frame_sequence_) {
      requested_entries.push_back(entry);
    } else if (resident_page.state
        == renderer::VirtualPageResidencyState::kResidentDirty
      || resident_page.state
        == renderer::VirtualPageResidencyState::kPendingRender) {
      dirty_entries.push_back(entry);
    } else {
      clean_entries.push_back(entry);
    }
  }

  for (const auto& tile : free_physical_tiles_) {
    available_entries.push_back(renderer::VirtualShadowPhysicalPageListEntry {
      .resident_key = 0U,
      .physical_page_index = static_cast<std::uint32_t>(
        tile.tile_y * physical_pool_config_.atlas_tiles_per_axis + tile.tile_x),
      .page_flags = 0U,
    });
  }

  state.resolve_stats.requested_page_list_count
    = static_cast<std::uint32_t>(requested_entries.size());
  state.resolve_stats.dirty_page_list_count
    = static_cast<std::uint32_t>(dirty_entries.size());
  state.resolve_stats.clean_page_list_count
    = static_cast<std::uint32_t>(clean_entries.size());
  state.resolve_stats.available_page_list_count
    = static_cast<std::uint32_t>(available_entries.size());

  state.physical_page_list_entries.insert(
    state.physical_page_list_entries.end(), requested_entries.begin(),
    requested_entries.end());
  state.physical_page_list_entries.insert(
    state.physical_page_list_entries.end(), dirty_entries.begin(),
    dirty_entries.end());
  state.physical_page_list_entries.insert(
    state.physical_page_list_entries.end(), clean_entries.begin(),
    clean_entries.end());
  state.physical_page_list_entries.insert(
    state.physical_page_list_entries.end(), available_entries.begin(),
    available_entries.end());
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
  state.introspection.published_directional_virtual_metadata
    = state.directional_virtual_metadata;
  state.introspection.resolved_raster_pages = state.resolved_raster_pages;
  state.introspection.resolve_resident_page_entries
    = state.resolve_resident_page_entries;
  state.introspection.page_table_entries = state.page_table_entries;
  state.introspection.published_page_table_entries = state.page_table_entries;
  state.introspection.page_flags_entries = state.page_flags_entries;
  state.introspection.published_page_flags_entries = state.page_flags_entries;
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
  state.introspection.used_request_feedback
    = state.publish_diagnostics.feedback_decision
    == ViewCacheEntry::RequestFeedbackDecision::kAccepted;
  state.introspection.cache_layout_compatible
    = state.publish_diagnostics.cache_layout_compatible;
  state.introspection.depth_guardband_valid
    = state.publish_diagnostics.depth_guardband_valid;
  const auto page_table_resources_it = view_page_table_resources_.find(view_id);
  const auto page_flags_resources_it = view_page_flags_resources_.find(view_id);
  const auto page_management_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_management_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
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
  state.page_management_bindings.physical_page_metadata_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_metadata_srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.physical_page_lists_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_lists_srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.resolve_stats_srv
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.stats_srv
    : kInvalidShaderVisibleIndex;

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
  std::vector<std::uint64_t>& eviction_candidates,
  std::size_t& next_eviction_candidate_index, std::uint32_t& evicted_page_count)
  -> std::optional<PhysicalTileAddress>
{
  if (const auto free_tile = AllocatePhysicalTile(); free_tile.has_value()) {
    return free_tile;
  }

  while (next_eviction_candidate_index < eviction_candidates.size()) {
    const auto eviction_key
      = eviction_candidates[next_eviction_candidate_index++];
    const auto eviction_it = state.resident_pages.find(eviction_key);
    if (eviction_it == state.resident_pages.end()
      || eviction_it->second.last_requested_frame == frame_sequence_) {
      continue;
    }

    const auto tile = eviction_it->second.tile;
    state.resident_pages.erase(eviction_it);
    ++evicted_page_count;
    return tile;
  }

  return std::nullopt;
}

auto VirtualShadowMapBackend::ReleasePhysicalTile(
  const PhysicalTileAddress tile) -> void
{
  free_physical_tiles_.push_back(tile);
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
  bool previous_authoritative_snapshot_exists = false;
  if (previous_state != nullptr
    && previous_state->pending_residency_resolve.valid
    && !previous_state->pending_residency_resolve.previous_resident_pages
      .empty()
    && previous_state->pending_residency_resolve.resident_reuse_snapshot.valid
    && previous_state->pending_residency_resolve.resident_reuse_snapshot
         .directional_virtual_metadata.size()
      == 1U) {
    previous_authoritative_snapshot_exists = true;
    previous_metadata
      = &previous_state->pending_residency_resolve.resident_reuse_snapshot
           .directional_virtual_metadata.front();
  } else if (previous_state != nullptr
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

  const bool previous_metadata_exists = previous_metadata != nullptr;
  const bool previous_rendered_cache_exists = previous_metadata_exists
    && previous_state != nullptr
    && (previous_state->has_rendered_cache_history
      || previous_state->resolve_stats.clean_page_count > 0U);
  const bool previous_authoritative_rendered_cache_exists
    = previous_rendered_cache_exists || previous_authoritative_snapshot_exists;
  const bool force_invalidate_cache
    = previous_authoritative_rendered_cache_exists
    && directional_cache_controls_.force_invalidate;
  auto depth_guardband_candidate = previous_authoritative_rendered_cache_exists
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

  setup.cache_layout_compatible = previous_authoritative_rendered_cache_exists
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

  if (!previous_metadata_exists) {
    setup.previous_clip_cache_status.fill(
      renderer::DirectionalVirtualClipCacheStatus::kNoPreviousFrame);
  } else if (!previous_authoritative_rendered_cache_exists) {
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
      if (!previous_metadata_exists
        || !previous_authoritative_rendered_cache_exists
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

auto VirtualShadowMapBackend::BuildDirectionalVirtualViewState(
  const ViewId view_id, const engine::ViewConstants& view_constants,
  const engine::DirectionalShadowCandidate& candidate,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const std::uint8_t> shadow_caster_static_flags,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const ViewCacheEntry* previous_state, ViewCacheEntry& state) -> void
{
  using namespace shadow_detail;

  const auto clipmap_setup
    = PrepareDirectionalVirtualClipmapSetup(view_constants, candidate,
      shadow_caster_bounds, visible_receiver_bounds, previous_state);
  if (!clipmap_setup.has_value() || !clipmap_setup->valid) {
    return;
  }
  const auto& setup = *clipmap_setup;
  const auto clip_level_count = setup.clip_level_count;
  const auto pages_per_axis = setup.pages_per_axis;
  const auto pages_per_level = setup.pages_per_level;
  const auto& metadata = setup.metadata;
  const auto& light_view = setup.light_view;
  const auto& light_eye = setup.light_eye;
  const auto near_plane = setup.near_plane;
  const auto far_plane = setup.far_plane;
  const auto& clip_page_world = setup.clip_page_world;
  const auto& clip_origin_x = setup.clip_origin_x;
  const auto& clip_origin_y = setup.clip_origin_y;
  const auto& clip_grid_origin_x = setup.clip_grid_origin_x;
  const auto& clip_grid_origin_y = setup.clip_grid_origin_y;
  const auto coarse_safety_clip_index = setup.coarse_safety_clip_index;
  const auto coarse_safety_budget_pages = setup.coarse_safety_budget_pages;
  const auto coarse_safety_priority_center_ls
    = setup.coarse_safety_priority_center_ls;
  const auto coarse_safety_priority_valid = setup.coarse_safety_priority_valid;
  std::vector<ClipSelectedRegion> frustum_regions(setup.frustum_regions.begin(),
    setup.frustum_regions.begin()
      + static_cast<std::ptrdiff_t>(clip_level_count));
  std::vector<ClipSelectedRegion> bootstrap_detail_regions(
    static_cast<std::size_t>(clip_level_count));

  state.shadow_instances.push_back(setup.shadow_instance);
  state.shadow_caster_bounds.assign(
    shadow_caster_bounds.begin(), shadow_caster_bounds.end());
  state.shadow_caster_static_flags.assign(
    shadow_caster_static_flags.begin(), shadow_caster_static_flags.end());
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
  state.resolved_raster_pages.reserve(state.page_table_entries.size());
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
  std::vector<std::uint8_t> selected_pages(state.page_table_entries.size(), 0U);
  std::uint32_t selected_page_count = 0U;
  const auto page_overlaps_caster =
    [&](const std::uint32_t clip_index, const std::uint32_t page_x,
        const std::uint32_t page_y) {
    if (clip_index >= clip_level_count || page_x >= pages_per_axis
      || page_y >= pages_per_axis) {
      return false;
    }

    // Caster culling is only safe for sparse feedback/coarse selection. During
    // receiver-driven same-frame fine publication we sometimes need a blank
    // current page even when no caster projects into it, otherwise sampling
    // falls through to a much coarser fallback page that may contain an
    // unrelated occluder. That is the exact "wrong fallback shadow" failure
    // seen in live scenes.
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

    return true;
  };
  const auto mark_selected_page
    = [&](const std::uint32_t clip_index, const std::uint32_t page_x,
        const std::uint32_t page_y,
        const bool require_caster_overlap = true) -> bool {
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

    if (require_caster_overlap
      && !page_overlaps_caster(clip_index, page_x, page_y)) {
      return false;
    }

    selected_pages[global_page_index] = 1U;
    ++selected_page_count;
    return true;
  };

  const auto mark_region
    = [&](const std::uint32_t clip_index, const ClipSelectedRegion& region,
        const bool require_caster_overlap = true) {
        if (!region.valid) {
          return 0U;
        }

        std::uint32_t added = 0U;
        for (std::uint32_t page_y = region.min_y; page_y <= region.max_y;
          ++page_y) {
          for (std::uint32_t page_x = region.min_x; page_x <= region.max_x;
            ++page_x) {
            if (mark_selected_page(
                  clip_index, page_x, page_y, require_caster_overlap)) {
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
          if (mark_selected_page(
                clip_index, dilated_page_x, dilated_page_y, true)) {
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
                static_cast<std::uint32_t>(page_y), true)) {
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

  std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
    reusable_clip_contents {};
  reusable_clip_contents.fill(false);
  bool address_space_compatible = false;
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
      bootstrap_detail_regions[clip_index]
        = union_region(bootstrap_detail_regions[clip_index], receiver_region);
      frustum_regions[clip_index]
        = union_region(frustum_regions[clip_index], receiver_region);
      state.absolute_frustum_regions[clip_index] = union_absolute_region(
        state.absolute_frustum_regions[clip_index], receiver_absolute_region);
    }
  }
  std::unordered_set<std::uint64_t> dirty_resident_pages {};
  std::unordered_set<std::uint64_t> dirty_static_resident_pages {};
  std::unordered_set<std::uint64_t> dirty_dynamic_resident_pages {};
  bool global_dirty_resident_contents = false;
  const auto* previous_resident_pages
    = previous_state != nullptr ? &previous_state->resident_pages : nullptr;
  const auto* previous_shadow_caster_bounds = previous_state != nullptr
    ? &previous_state->shadow_caster_bounds
    : nullptr;
  const auto* previous_shadow_caster_static_flags = previous_state != nullptr
    ? &previous_state->shadow_caster_static_flags
    : nullptr;
  const auto* previous_key
    = previous_state != nullptr ? &previous_state->key : nullptr;
  const auto* previous_page_table_entries
    = previous_state != nullptr ? &previous_state->page_table_entries : nullptr;
  const engine::DirectionalVirtualShadowMetadata* previous_metadata = nullptr;
  bool previous_pending_resolved_pages_empty = previous_state == nullptr
    || previous_state->resolve_stats.pending_raster_page_count == 0U;
  const bool previous_rendered_cache_exists = previous_state != nullptr
    && (previous_state->has_rendered_cache_history
      || previous_state->resolve_stats.clean_page_count > 0U);
  const bool previous_authoritative_snapshot_exists = previous_state != nullptr
    && previous_state->pending_residency_resolve.valid
    && !previous_state->pending_residency_resolve.previous_resident_pages
          .empty()
    && previous_state->pending_residency_resolve.resident_reuse_snapshot.valid;
  if (!previous_rendered_cache_exists
    && previous_authoritative_snapshot_exists) {
    const auto& previous_pending = previous_state->pending_residency_resolve;
    previous_resident_pages = &previous_pending.previous_resident_pages;
    previous_shadow_caster_bounds
      = &previous_pending.previous_shadow_caster_bounds;
    previous_shadow_caster_static_flags
      = &previous_pending.previous_shadow_caster_static_flags;
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

  const auto count_selected_pages_in_clip
    = [&](const std::uint32_t clip_index) {
        if (clip_index >= clip_level_count) {
          return 0U;
        }
        const auto clip_begin = static_cast<std::size_t>(clip_index)
          * static_cast<std::size_t>(pages_per_level);
        const auto clip_end
          = clip_begin + static_cast<std::size_t>(pages_per_level);
        return static_cast<std::uint32_t>(std::count(
          selected_pages.begin() + static_cast<std::ptrdiff_t>(clip_begin),
          selected_pages.begin() + static_cast<std::ptrdiff_t>(clip_end), 1U));
      };
  const bool cache_layout_compatible = setup.cache_layout_compatible;
  const bool depth_guardband_valid = setup.depth_guardband_valid;
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

  const auto current_feedback_address_space_hash
    = shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(metadata);
  const auto feedback_lineage_cache_compatible = [&]() {
    if (previous_metadata == nullptr || !cache_layout_compatible
      || !depth_guardband_valid) {
      return false;
    }

    for (std::uint32_t clip_index = 0U; clip_index < coarse_backbone_begin;
      ++clip_index) {
      if (!state.absolute_frustum_regions[clip_index].valid) {
        continue;
      }
      if (!setup.previous_clip_cache_valid[clip_index]) {
        return false;
      }
    }

    return true;
  };
  const auto feedback_it = request_feedback_.find(view_id);
  const auto* detail_feedback_channel
    = feedback_it != request_feedback_.end() && feedback_it->second.detail.valid
    ? &feedback_it->second.detail
    : nullptr;
  const auto* coarse_feedback_channel
    = feedback_it != request_feedback_.end() && feedback_it->second.coarse.valid
    ? &feedback_it->second.coarse
    : nullptr;
  const auto feedback_source_regions_compatible =
    [&](const PendingRequestFeedbackChannel* channel) {
      if (channel == nullptr || !feedback_lineage_cache_compatible()) {
        return false;
      }

      const auto& source_regions = channel->source_absolute_frustum_regions;
      if (source_regions.size()
          < static_cast<std::size_t>(coarse_backbone_begin)
        || state.absolute_frustum_regions.size()
          < static_cast<std::size_t>(coarse_backbone_begin)) {
        return false;
      }

      for (std::uint32_t clip_index = 0U; clip_index < coarse_backbone_begin;
        ++clip_index) {
        const auto& current_region = state.absolute_frustum_regions[clip_index];
        if (!current_region.valid) {
          continue;
        }

        const auto source_region = expand_absolute_region(
          source_regions[clip_index], kAcceptedFeedbackCurrentFrameGuardPages);
        if (!source_region.valid
          || !intersect_absolute_region(source_region, current_region).valid) {
          return false;
        }
      }

      return true;
    };
  const auto feedback_hash_is_recently_compatible
    = [&](const PendingRequestFeedbackChannel* channel) {
        if (channel == nullptr) {
          return false;
        }
        const auto feedback_hash
          = channel->feedback.directional_address_space_hash;
        if (feedback_hash == current_feedback_address_space_hash) {
          return true;
        }
        if (previous_state == nullptr
          || !feedback_source_regions_compatible(channel)) {
          return false;
        }

        return std::ranges::find(
                 previous_state->compatible_feedback_address_space_hashes,
                 feedback_hash)
          != previous_state->compatible_feedback_address_space_hashes.end();
      };
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
        } else if (!feedback_hash_is_recently_compatible(channel)) {
          result.decision
            = ViewCacheEntry::RequestFeedbackDecision::kAddressSpaceMismatch;
        } else if (frame_sequence_ <= feedback.source_frame_sequence) {
          result.decision = ViewCacheEntry::RequestFeedbackDecision::kSameFrame;
        } else if (result.age_frames > kMaxRequestFeedbackAgeFrames) {
          result.decision = ViewCacheEntry::RequestFeedbackDecision::kStale;
        } else {
          result.decision = ViewCacheEntry::RequestFeedbackDecision::kAccepted;
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
      != ViewCacheEntry::RequestFeedbackDecision::kAccepted
    && coarse_feedback_state.decision
      == ViewCacheEntry::RequestFeedbackDecision::kAccepted) {
    feedback_decision = coarse_feedback_state.decision;
  }
  const std::uint32_t feedback_key_count
    = detail_feedback_state.key_count + coarse_feedback_state.key_count;
  const std::uint64_t feedback_age_frames = std::max(
    detail_feedback_state.age_frames, coarse_feedback_state.age_frames);
  const bool detail_feedback_would_have_been_accepted
    = detail_feedback_state.decision
    == ViewCacheEntry::RequestFeedbackDecision::kAccepted;
  const bool coarse_feedback_would_have_been_accepted
    = coarse_feedback_state.decision
    == ViewCacheEntry::RequestFeedbackDecision::kAccepted;
  const bool use_previous_resident_mismatch_carry = false;
  (void)use_previous_resident_mismatch_carry;
  std::uint32_t coarse_backbone_pages = 0U;
  std::uint32_t same_frame_detail_pages = 0U;
  std::uint32_t feedback_requested_pages = 0U;
  // Legacy diagnostics remain for visibility, but the live Phase 5 path no
  // longer authors synthetic CPU page demand through these mechanisms.
  constexpr std::uint32_t feedback_refinement_pages = 0U;
  constexpr std::uint32_t receiver_bootstrap_pages = 0U;
  constexpr std::uint32_t current_frame_reinforcement_pages = 0U;
  constexpr std::uint64_t current_frame_reinforcement_reference_frame = 0U;
  std::uint32_t detail_feedback_requested_pages = 0U;
  std::uint32_t coarse_feedback_requested_pages = 0U;
  bool force_full_same_frame_detail_region = false;

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

  const bool transition_publish_risk = previous_resident_pages == nullptr
    || previous_resident_pages->empty() || !address_space_compatible
    || global_dirty_resident_contents;
  // Guardrail: accepted fine feedback remains authoritative for live fine-page
  // creation until same-frame GPU publication can prove equivalent coverage.
  // Demoting it to telemetry-only before that replacement exists collapses the
  // live view to coarse/no-page results even though the cache still looks
  // structurally coherent in diagnostics.
  const auto mark_same_frame_detail_region =
    [&](const std::uint32_t clip_index, const ClipSelectedRegion& region,
        const bool allow_delta_seed) {
      if (!region.valid) {
        return 0U;
      }

      // Guardrail: when fine publication falls back to current-frame bootstrap,
      // keep that bootstrap tied to the explicit receiver region instead of the
      // whole fine frustum. Re-expanding back to frustum coverage eagerly burns
      // the full physical pool on pages the current frame never asked for,
      // which is what makes fallback dominate the startup/motion image.
      if (!allow_delta_seed) {
        // Guardrail: current-frame receiver publication must be allowed to map
        // blank fine pages. If we cull these pages just because no caster
        // overlaps them, sampling drops to coarse fallback and produces the
        // visibly wrong shadows reported in live movement scenes.
        return mark_region(clip_index, region, false);
      }

      // Guardrail: if accepted detail feedback translated to zero current-space
      // pages, do not let the delta-band path starve fine publication. That
      // state is only telemetry-compatible; the live view still needs full
      // same-frame fine coverage to avoid collapsing to visibly wrong coarse
      // fallback.
      if (force_full_same_frame_detail_region) {
        return mark_region(clip_index, region);
      }

      const bool can_delta_seed = previous_state != nullptr
        && clip_index < state.absolute_frustum_regions.size()
        && clip_index < previous_state->absolute_frustum_regions.size()
        && cache_layout_compatible && depth_guardband_valid
        && setup.previous_clip_cache_valid[clip_index];
      if (!can_delta_seed) {
        return mark_region(clip_index, region);
      }

      const auto& current_region = state.absolute_frustum_regions[clip_index];
      const auto& previous_region
        = previous_state->absolute_frustum_regions[clip_index];
      if (!current_region.valid || !previous_region.valid) {
        return mark_region(clip_index, region);
      }

      // Guardrail: absolute receiver-region delta remains the right Phase 7
      // live authority even when the snapped clip origin does not move. Falling
      // back to full-region reselection in the zero-shift case starves the pool
      // with speculative fine pages, which is what drives the visible collapse
      // back to bad coarse fallback during motion.
      const bool identical_absolute_region
        = current_region.min_x == previous_region.min_x
        && current_region.max_x == previous_region.max_x
        && current_region.min_y == previous_region.min_y
        && current_region.max_y == previous_region.max_y;
      if (identical_absolute_region) {
        // Guardrail: zero-shift frames may only skip same-frame fine coverage
        // once accepted fine feedback is already seeding the live current page
        // set. When feedback is absent, publishing only the coarse backbone
        // makes fallback dominate the image, which is the visibly wrong
        // bootstrap/reset regression the live demos keep hitting.
        if (!detail_feedback_would_have_been_accepted) {
          return mark_region(clip_index, region);
        }
        return 0U;
      }

      return mark_absolute_delta_band(clip_index, current_region, previous_region);
    };
  const auto mark_feedback_requested_pages =
    [&](const PendingRequestFeedbackChannel* channel,
        const bool use_detail_clips_only) {
      if (channel == nullptr) {
        return 0U;
      }

      std::uint32_t added = 0U;
      for (const auto resident_key : channel->feedback.requested_resident_keys) {
        const auto clip_index
          = shadow_detail::VirtualResidentPageKeyClipLevel(resident_key);
        if (clip_index >= clip_level_count) {
          continue;
        }
        if (use_detail_clips_only) {
          if (clip_index >= coarse_backbone_begin) {
            continue;
          }
        } else if (clip_index < coarse_backbone_begin) {
          continue;
        }

        const auto local_x
          = shadow_detail::VirtualResidentPageKeyGridX(resident_key)
          - clip_grid_origin_x[clip_index];
        const auto local_y
          = shadow_detail::VirtualResidentPageKeyGridY(resident_key)
          - clip_grid_origin_y[clip_index];
        if (local_x < 0 || local_y < 0
          || local_x >= static_cast<std::int32_t>(pages_per_axis)
          || local_y >= static_cast<std::int32_t>(pages_per_axis)) {
          continue;
        }

        added += mark_dilated_page(clip_index,
          static_cast<std::uint32_t>(local_x),
          static_cast<std::uint32_t>(local_y), kFeedbackRequestGuardRadius);
      }
      return added;
    };
  for (std::uint32_t clip_index = clip_level_count;
    clip_index-- > coarse_backbone_begin;) {
    const auto& region = frustum_regions[clip_index];
    if (!region.valid) {
      continue;
    }

    coarse_backbone_pages += mark_region(clip_index, region);
  }
  if (detail_feedback_would_have_been_accepted) {
    detail_feedback_requested_pages += mark_feedback_requested_pages(
      detail_feedback_channel, true);
    if (detail_feedback_requested_pages == 0U) {
      force_full_same_frame_detail_region = true;
    }
    for (std::uint32_t clip_index = 0U; clip_index < coarse_backbone_begin;
      ++clip_index) {
      const bool use_bootstrap_region = force_full_same_frame_detail_region
        && bootstrap_detail_regions[clip_index].valid;
      const auto& region = use_bootstrap_region
        ? bootstrap_detail_regions[clip_index]
        : frustum_regions[clip_index];
      if (!region.valid) {
        continue;
      }
      same_frame_detail_pages
        += mark_same_frame_detail_region(
          clip_index, region, !use_bootstrap_region);
    }
  } else {
    for (std::uint32_t clip_index = 0U; clip_index < coarse_backbone_begin;
      ++clip_index) {
      const auto& region = bootstrap_detail_regions[clip_index].valid
        ? bootstrap_detail_regions[clip_index]
        : frustum_regions[clip_index];
      if (!region.valid) {
        continue;
      }
      same_frame_detail_pages
        += mark_same_frame_detail_region(clip_index, region, false);
    }
  }
  if (coarse_feedback_would_have_been_accepted) {
    coarse_feedback_requested_pages += mark_feedback_requested_pages(
      coarse_feedback_channel, false);
  }
  feedback_requested_pages
    = detail_feedback_requested_pages + coarse_feedback_requested_pages;
  if (feedback_decision == ViewCacheEntry::RequestFeedbackDecision::kAccepted
    && detail_feedback_would_have_been_accepted
    && detail_feedback_requested_pages == 0U) {
    // Accepted lineage that cannot seed a single current-space fine page is
    // not live authority. Keep it for telemetry, but make the log/introspection
    // reflect that same-frame publication carried correctness.
    feedback_decision = ViewCacheEntry::RequestFeedbackDecision::kTelemetryOnly;
  }

  const std::uint32_t coarse_safety_selected_pages
    = count_selected_pages_in_clip(coarse_safety_clip_index);
  const bool coarse_safety_capacity_fit
    = coarse_safety_selected_pages <= coarse_safety_budget_pages;
  const bool predicted_current_publish_coherent
    = coarse_safety_capacity_fit && !transition_publish_risk;

  if (previous_metadata != nullptr) {
    if (address_space_compatible) {
      const bool shadow_content_hash_changed = previous_key != nullptr
        && previous_key->shadow_content_hash != state.key.shadow_content_hash;
      const bool caster_bounds_changed = previous_key != nullptr
        && previous_key->caster_hash != state.key.caster_hash;
      bool found_spatial_delta = false;
      if (previous_shadow_caster_bounds != nullptr
        && previous_shadow_caster_bounds->size()
          == shadow_caster_bounds.size()) {
        const bool static_flag_count_matches
          = previous_shadow_caster_static_flags != nullptr
          && previous_shadow_caster_static_flags->size()
            == shadow_caster_bounds.size()
          && shadow_caster_static_flags.size() == shadow_caster_bounds.size();
        for (std::size_t i = 0U; i < shadow_caster_bounds.size(); ++i) {
          const auto& previous_bound = (*previous_shadow_caster_bounds)[i];
          const auto& current_bound = shadow_caster_bounds[i];
          const bool static_caster = static_flag_count_matches
            ? ((*previous_shadow_caster_static_flags)[i] != 0U
              && shadow_caster_static_flags[i] != 0U)
            : false;
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
          auto& dirty_target = static_caster ? dirty_static_resident_pages
                                             : dirty_dynamic_resident_pages;
          AppendDirtyResidentKeysForBound(previous_bound,
            previous_metadata->light_view, clip_page_world, clip_level_count,
            dirty_target);
          AppendDirtyResidentKeysForBound(current_bound, light_view,
            clip_page_world, clip_level_count, dirty_target);
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
  pending_resolve.previous_clip_page_offset_x = state.clipmap_page_offset_x;
  pending_resolve.previous_clip_page_offset_y = state.clipmap_page_offset_y;
  pending_resolve.previous_clip_reuse_guardband_valid
    = state.clipmap_reuse_guardband_valid;
  pending_resolve.previous_clip_cache_valid = state.clipmap_cache_valid;
  pending_resolve.previous_clip_cache_status = state.clipmap_cache_status;
  pending_resolve.coarse_backbone_begin = coarse_backbone_begin;
  pending_resolve.coarse_safety_clip_index = coarse_safety_clip_index;
  pending_resolve.coarse_safety_max_page_count = coarse_safety_budget_pages;
  pending_resolve.coarse_safety_priority_center_ls
    = coarse_safety_priority_center_ls;
  pending_resolve.coarse_safety_priority_valid = coarse_safety_priority_valid;
  pending_resolve.reusable_clip_contents = reusable_clip_contents;
  pending_resolve.bootstrap_prefers_finest_detail_pages
    = !detail_feedback_would_have_been_accepted
    || force_full_same_frame_detail_region;
  pending_resolve.address_space_compatible = address_space_compatible;
  pending_resolve.cache_layout_compatible = cache_layout_compatible;
  pending_resolve.depth_guardband_valid = depth_guardband_valid;
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
  if (previous_shadow_caster_static_flags != nullptr) {
    pending_resolve.previous_shadow_caster_static_flags
      = *previous_shadow_caster_static_flags;
  }
  pending_resolve.dirty_resident_pages = std::move(dirty_resident_pages);
  pending_resolve.dirty_static_resident_pages
    = std::move(dirty_static_resident_pages);
  pending_resolve.dirty_dynamic_resident_pages
    = std::move(dirty_dynamic_resident_pages);
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
  state.publish_diagnostics.cache_layout_compatible = cache_layout_compatible;
  state.publish_diagnostics.depth_guardband_valid = depth_guardband_valid;
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
  state.publish_diagnostics.coarse_safety_selected_pages
    = coarse_safety_selected_pages;
  state.publish_diagnostics.coarse_safety_budget_pages
    = coarse_safety_budget_pages;
  state.publish_diagnostics.coarse_safety_capacity_fit
    = coarse_safety_capacity_fit;
  state.publish_diagnostics.predicted_coherent_publication
    = predicted_current_publish_coherent;
  state.publish_diagnostics.same_frame_detail_pages = same_frame_detail_pages;
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

  state.compatible_feedback_address_space_hashes.clear();
  if (previous_state != nullptr && feedback_lineage_cache_compatible()) {
    state.compatible_feedback_address_space_hashes
      = previous_state->compatible_feedback_address_space_hashes;
  }
  if (std::ranges::find(state.compatible_feedback_address_space_hashes,
        current_feedback_address_space_hash)
    == state.compatible_feedback_address_space_hashes.end()) {
    state.compatible_feedback_address_space_hashes.push_back(
      current_feedback_address_space_hash);
  }
  if (state.compatible_feedback_address_space_hashes.size()
    > kCompatibleFeedbackHashHistoryLimit) {
    const auto remove_count
      = state.compatible_feedback_address_space_hashes.size()
      - kCompatibleFeedbackHashHistoryLimit;
    state.compatible_feedback_address_space_hashes.erase(
      state.compatible_feedback_address_space_hashes.begin(),
      state.compatible_feedback_address_space_hashes.begin()
        + static_cast<std::ptrdiff_t>(remove_count));
  }

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

auto VirtualShadowMapBackend::EnsureViewPageTableResources(
  const ViewId view_id, const std::uint32_t required_entry_count)
  -> ViewStructuredWordBufferResources*
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

auto VirtualShadowMapBackend::EnsurePageTablePublication(const ViewId view_id,
  const std::uint32_t required_entry_count) -> ShaderVisibleIndex
{
  if (required_entry_count == 0U) {
    return kInvalidShaderVisibleIndex;
  }

  auto* resources = EnsureViewPageTableResources(view_id, required_entry_count);
  return resources != nullptr ? resources->srv : kInvalidShaderVisibleIndex;
}

auto VirtualShadowMapBackend::EnsureViewPageFlagResources(
  const ViewId view_id, const std::uint32_t required_entry_count)
  -> ViewStructuredWordBufferResources*
{
  if (required_entry_count == 0U) {
    return nullptr;
  }

  auto [it, _] = view_page_flags_resources_.try_emplace(view_id);
  auto& resources = it->second;
  if (resources.gpu_buffer && resources.upload_buffer
    && resources.mapped_upload != nullptr
    && required_entry_count <= resources.entry_capacity) {
    return &resources;
  }

  if (required_entry_count > kMaxPersistentPageTableEntries) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: view {} requested {} page-flag entries but "
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
    .debug_name = "VirtualShadowMapBackend.PersistentPageFlags",
  };
  resources.gpu_buffer = gfx_->CreateBuffer(gpu_desc);
  if (!resources.gpu_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create persistent page flags "
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
      "VirtualShadowMapBackend: failed to allocate page-flags SRV for view {}",
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
      "VirtualShadowMapBackend: failed to allocate page-flags UAV for view {}",
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
    .debug_name = "VirtualShadowMapBackend.PersistentPageFlagsUpload",
  };
  resources.upload_buffer = gfx_->CreateBuffer(upload_desc);
  if (!resources.upload_buffer) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to create page-flags upload buffer "
      "for view {}",
      view_id.get());
    return nullptr;
  }

  resources.mapped_upload
    = static_cast<std::uint32_t*>(resources.upload_buffer->Map(0U, size_bytes));
  if (resources.mapped_upload == nullptr) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to map page-flags upload buffer for "
      "view {}",
      view_id.get());
    resources.upload_buffer.reset();
    return nullptr;
  }
  std::memset(resources.mapped_upload, 0, static_cast<std::size_t>(size_bytes));
  resources.entry_capacity = kMaxPersistentPageTableEntries;
  return &resources;
}

auto VirtualShadowMapBackend::EnsurePageFlagsPublication(const ViewId view_id,
  const std::uint32_t required_entry_count) -> ShaderVisibleIndex
{
  if (required_entry_count == 0U) {
    return kInvalidShaderVisibleIndex;
  }

  auto* resources = EnsureViewPageFlagResources(view_id, required_entry_count);
  return resources != nullptr ? resources->srv : kInvalidShaderVisibleIndex;
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

auto VirtualShadowMapBackend::EnsureViewResolveResources(const ViewId view_id,
  const std::uint32_t required_entry_count) -> ViewResolveResources*
{
  const auto required_capacity = std::max(std::max(required_entry_count, 1U),
    physical_pool_config_.physical_tile_capacity);
  const auto required_physical_list_capacity
    = required_capacity + physical_pool_config_.physical_tile_capacity;

  auto [it, _] = view_resolve_resources_.try_emplace(view_id);
  auto& resources = it->second;
  if (resources.resident_pages_gpu_buffer
    && resources.resident_pages_upload_buffer
    && resources.mapped_resident_pages_upload != nullptr
    && resources.stats_gpu_buffer && resources.stats_upload_buffer
    && resources.mapped_stats_upload != nullptr
    && resources.physical_page_metadata_gpu_buffer
    && resources.physical_page_metadata_upload_buffer
    && resources.mapped_physical_page_metadata_upload != nullptr
    && resources.physical_page_lists_gpu_buffer
    && resources.physical_page_lists_upload_buffer
    && resources.mapped_physical_page_lists_upload != nullptr
    && required_capacity <= resources.resident_page_capacity
    && physical_pool_config_.physical_tile_capacity
      <= resources.physical_page_metadata_capacity
    && required_physical_list_capacity
      <= resources.physical_page_lists_capacity) {
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

  const graphics::BufferDesc physical_page_metadata_upload_desc {
    .size_bytes = physical_page_metadata_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowMapBackend.PhysicalPageMetadataUpload",
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

  const graphics::BufferDesc physical_page_lists_upload_desc {
    .size_bytes = physical_page_lists_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowMapBackend.PhysicalPageListsUpload",
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

  resources.resident_page_capacity = required_capacity;
  resources.physical_page_metadata_capacity
    = physical_pool_config_.physical_tile_capacity;
  resources.physical_page_lists_capacity = physical_page_lists_capacity;
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
  resources->physical_page_metadata_upload_count
    = static_cast<std::uint32_t>(state.physical_page_metadata_entries.size());
  resources->physical_page_metadata_upload_pending = true;
  resources->physical_page_lists_upload_count
    = static_cast<std::uint32_t>(state.physical_page_list_entries.size());
  resources->physical_page_lists_upload_pending = true;

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
    case Decision::kTelemetryOnly:
      return "telemetry-only";
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
    "gpu_resolved_pages={} pending_raster_pages={} reused={} allocated={} evicted={} "
    "alloc_failures={} rerasterized={} resident_reuse_gate={}",
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
    diagnostics.current_frame_reinforcement_pages,
    pending_raster_page_count, pending_raster_page_count,
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
