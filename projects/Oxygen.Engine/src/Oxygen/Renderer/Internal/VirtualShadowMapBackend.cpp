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
      *staging_provider_,
      static_cast<std::uint32_t>(sizeof(glm::vec4)),
      oxygen::observer_ptr<engine::upload::InlineTransfersCoordinator>(
        inline_transfers_),
      "VirtualShadowMapBackend.ShadowCasterBounds")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

VirtualShadowMapBackend::~VirtualShadowMapBackend()
{
  ReleasePhysicalPool();
}

auto VirtualShadowMapBackend::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  frame_sequence_ = sequence;
  frame_slot_ = slot;
  shadow_instance_buffer_.OnFrameStart(sequence, slot);
  directional_virtual_metadata_buffer_.OnFrameStart(sequence, slot);
  shadow_caster_bounds_buffer_.OnFrameStart(sequence, slot);
}

auto VirtualShadowMapBackend::PublishView(const ViewId view_id,
  const engine::ViewConstants& view_constants,
  const std::span<const engine::DirectionalShadowCandidate>
    directional_candidates,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::chrono::milliseconds gpu_budget,
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
  //
  // The caller must derive this only from authoritative resolve/raster page
  // work. CPU draw metadata, partition counts, or indirect-record counts are
  // not sufficient because they can be nonzero before any current VSM page
  // mapping exists.
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
  if (!state.pending_residency_resolve.valid
    || !state.pending_residency_resolve.dirty) {
    return;
  }

  if (state.pending_residency_resolve.reset_page_management_state) {
    StagePageManagementSeedUpload(view_id, state);
  }
  RefreshViewExports(view_id, state);
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
  const bool has_resolve_resources
    = resolve_resources_it != view_resolve_resources_.end();
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
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);
  RefreshViewExports(view_id, state);

  recorder.RequireResourceState(*page_management_table_resources.gpu_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*page_management_flags_resources.gpu_buffer,
    graphics::ResourceStates::kShaderResource);
  if (resolve_resources_it != view_resolve_resources_.end()) {
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
  state.shadow_instances.push_back(setup.shadow_instance);
  state.shadow_caster_bounds.assign(
    shadow_caster_bounds.begin(), shadow_caster_bounds.end());
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
  const auto page_management_table_resources_it
    = view_page_management_page_table_resources_.find(view_id);
  const auto page_management_flags_resources_it
    = view_page_management_page_flags_resources_.find(view_id);
  const auto resolve_resources_it = view_resolve_resources_.find(view_id);

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
  state.page_management_bindings.dirty_page_flags_uav
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.dirty_page_flags_uav
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.previous_shadow_caster_bounds_srv
    = state.pending_residency_resolve.valid
    ? state.pending_residency_resolve.previous_shadow_caster_bounds_srv
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.current_shadow_caster_bounds_srv
    = state.pending_residency_resolve.valid
    ? state.pending_residency_resolve.current_shadow_caster_bounds_srv
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
  state.page_management_bindings.resolve_stats_uav
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.stats_uav
    : kInvalidShaderVisibleIndex;
  state.page_management_bindings.previous_light_view
    = state.pending_residency_resolve.valid
    ? state.pending_residency_resolve.previous_light_view
    : glm::mat4 { 1.0F };
  state.page_management_bindings.shadow_caster_bound_count
    = state.pending_residency_resolve.valid
    ? state.pending_residency_resolve.shadow_caster_bound_count
    : 0U;
  state.page_management_bindings.physical_page_capacity
    = resolve_resources_it != view_resolve_resources_.end()
    ? resolve_resources_it->second.physical_page_metadata_capacity
    : 0U;
  state.page_management_bindings.atlas_tiles_per_axis
    = physical_pool_config_.atlas_tiles_per_axis;
  state.page_management_bindings.pending_raster_page_count = 0U;
  state.page_management_bindings.reset_page_management_state
    = resolve_resources_it != view_resolve_resources_.end()
    && resolve_resources_it->second.physical_page_state_reset_pending;
  state.page_management_bindings.global_dirty_resident_contents
    = state.pending_residency_resolve.valid
    && state.pending_residency_resolve.global_dirty_resident_contents;

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
  auto depth_guardband_candidate = previous_rendered_cache_exists
    && EvaluateDirectionalDepthGuardband(*previous_metadata,
      std::span<const glm::vec3> {
        full_frustum_world_points.data(), full_frustum_world_points.size() },
      shadow_caster_bounds, largest_half_extent, largest_half_extent);

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

  (void)frustum_world_points;
  (void)full_frustum_world_points;
  (void)visible_receiver_bounds;

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
  (void)view_id;
  (void)shadow_caster_bounds;
  (void)visible_receiver_bounds;
  (void)state;

  const auto& metadata = setup.metadata;
  const bool previous_rendered_cache_exists
    = previous_state != nullptr && previous_state->has_rendered_cache_history;
  const bool previous_page_management_state_exists
    = previous_rendered_cache_exists && previous_state != nullptr
    && previous_state->page_management_bindings.page_table_srv.IsValid()
    && previous_state->page_management_bindings.page_flags_srv.IsValid()
    && previous_state->page_management_bindings.physical_page_metadata_srv
         .IsValid()
    && previous_state->page_management_bindings.physical_page_lists_srv
         .IsValid();
  bool address_space_compatible = false;
  if (previous_metadata != nullptr) {
    address_space_compatible = IsDirectionalVirtualAddressSpaceCompatible(
      *previous_metadata, metadata);
  }
  DirectionalSelectionBuildResult selection_result {};
  selection_result.address_space_compatible = address_space_compatible;
  selection_result.previous_page_management_state_exists
    = previous_page_management_state_exists;
  return selection_result;
}

auto VirtualShadowMapBackend::BuildDirectionalInvalidationResult(
  const DirectionalVirtualClipmapSetup& setup,
  const PublicationKey* previous_key, const PublicationKey& current_key,
  const engine::DirectionalVirtualShadowMetadata* previous_metadata,
  const std::vector<glm::vec4>* previous_shadow_caster_bounds,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const bool address_space_compatible) const
  -> DirectionalInvalidationBuildResult
{
  (void)setup;

  DirectionalInvalidationBuildResult result {};
  if (previous_metadata == nullptr || !address_space_compatible) {
    return result;
  }

  const bool shadow_content_hash_changed = previous_key != nullptr
    && previous_key->shadow_content_hash != current_key.shadow_content_hash;
  const bool caster_bounds_changed = previous_key != nullptr
    && previous_key->caster_hash != current_key.caster_hash;
  if (shadow_content_hash_changed && !caster_bounds_changed) {
    result.global_dirty_resident_contents = true;
    return result;
  }

  if (!caster_bounds_changed) {
    return result;
  }

  if (previous_shadow_caster_bounds == nullptr
    || previous_shadow_caster_bounds->size() != shadow_caster_bounds.size()) {
    result.global_dirty_resident_contents = true;
    return result;
  }

  result.compare_shadow_caster_bounds_on_gpu = !shadow_caster_bounds.empty();
  return result;
}

auto VirtualShadowMapBackend::PopulateDirectionalPendingResolve(
  ViewCacheEntry& state, const DirectionalVirtualClipmapSetup& setup,
  DirectionalSelectionBuildResult selection,
  DirectionalInvalidationBuildResult invalidation,
  const engine::DirectionalVirtualShadowMetadata* previous_metadata,
  const std::vector<glm::vec4>* previous_shadow_caster_bounds,
  const engine::ViewConstants& view_constants,
  const std::uint32_t visible_receiver_bound_count) -> void
{
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
  pending_resolve.previous_light_view = glm::mat4 { 1.0F };
  pending_resolve.global_dirty_resident_contents
    = invalidation.global_dirty_resident_contents;
  if (invalidation.compare_shadow_caster_bounds_on_gpu
    && previous_shadow_caster_bounds != nullptr
    && previous_shadow_caster_bounds->size() == state.shadow_caster_bounds.size()
    && !state.shadow_caster_bounds.empty()) {
    const auto bound_count = static_cast<std::uint32_t>(
      state.shadow_caster_bounds.size());
    auto previous_bounds_allocation
      = shadow_caster_bounds_buffer_.Allocate(bound_count);
    auto current_bounds_allocation
      = shadow_caster_bounds_buffer_.Allocate(bound_count);
    if (previous_bounds_allocation && current_bounds_allocation
      && previous_bounds_allocation->mapped_ptr != nullptr
      && current_bounds_allocation->mapped_ptr != nullptr) {
      std::memcpy(previous_bounds_allocation->mapped_ptr,
        previous_shadow_caster_bounds->data(),
        previous_shadow_caster_bounds->size() * sizeof(glm::vec4));
      std::memcpy(current_bounds_allocation->mapped_ptr,
        state.shadow_caster_bounds.data(),
        state.shadow_caster_bounds.size() * sizeof(glm::vec4));
      pending_resolve.previous_shadow_caster_bounds_srv
        = previous_bounds_allocation->srv;
      pending_resolve.current_shadow_caster_bounds_srv
        = current_bounds_allocation->srv;
      pending_resolve.shadow_caster_bound_count = bound_count;
      pending_resolve.previous_light_view = previous_metadata != nullptr
        ? previous_metadata->light_view
        : glm::mat4 { 1.0F };
    } else {
      pending_resolve.global_dirty_resident_contents = true;
    }
  }
  (void)visible_receiver_bound_count;
}

auto VirtualShadowMapBackend::BuildDirectionalPendingResolveStage(
  const ViewId view_id, const DirectionalVirtualClipmapSetup& setup,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const DirectionalPreviousStateContext& previous_context,
  const ViewCacheEntry* previous_state,
  const engine::ViewConstants& view_constants, ViewCacheEntry& state)
  -> void
{
  auto selection_result = BuildDirectionalSelectionResult(view_id, setup,
    shadow_caster_bounds, visible_receiver_bounds,
    previous_context.previous_metadata, previous_state, state);
  auto invalidation_result
    = BuildDirectionalInvalidationResult(setup, previous_context.previous_key,
      state.key, previous_context.previous_metadata,
      previous_context.previous_shadow_caster_bounds, shadow_caster_bounds,
      selection_result.address_space_compatible);

  PopulateDirectionalPendingResolve(state, setup, std::move(selection_result),
    std::move(invalidation_result), previous_context.previous_metadata,
    previous_context.previous_shadow_caster_bounds, view_constants,
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
  InitializeDirectionalViewStateFromClipmapSetup(
    setup, shadow_caster_bounds, state);
  const auto previous_context
    = BuildDirectionalPreviousStateContext(previous_state);
  BuildDirectionalPendingResolveStage(view_id, setup, shadow_caster_bounds,
    visible_receiver_bounds, previous_context, previous_state, view_constants,
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
    && resources.dirty_page_flags_gpu_buffer
    && resources.physical_page_lists_gpu_buffer
    && physical_pool_config_.physical_tile_capacity
      <= resources.physical_page_metadata_capacity
    && required_physical_list_capacity
      <= resources.physical_page_lists_capacity) {
    return &resources;
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
  // state. Do not rebuild or upload a CPU-authored live residency snapshot
  // here.
  resources->physical_page_state_reset_pending = true;
  RefreshViewExports(view_id, state);
}

} // namespace oxygen::renderer::internal
