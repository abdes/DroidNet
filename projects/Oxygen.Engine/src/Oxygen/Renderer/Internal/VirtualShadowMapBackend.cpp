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
#include <stdexcept>
#include <unordered_set>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Logging.h>
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
constexpr std::uint32_t kVirtualShadowMaxFilterGuardTexels = 2U;

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
  const float authored_end
    = clip_index < candidate.cascade_distances.size()
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
    return 6U;
  case oxygen::ShadowQualityTier::kMedium:
    return 8U;
  case oxygen::ShadowQualityTier::kHigh:
    return 12U;
  case oxygen::ShadowQualityTier::kUltra:
    return 16U;
  default:
    return 8U;
  }
}

[[nodiscard]] auto ResolveBudgetPagesPerAxisCap(
  const std::chrono::milliseconds gpu_budget) -> std::uint32_t
{
  const auto ms = gpu_budget.count();
  if (ms <= 16) {
    return 10U;
  }
  if (ms <= 25) {
    return 12U;
  }
  if (ms <= 33) {
    return 14U;
  }
  return 16U;
}

[[nodiscard]] auto ResolveVirtualClipLevelCount(
  const oxygen::ShadowQualityTier quality_tier,
  const std::uint32_t authored_cascade_count) -> std::uint32_t
{
  const auto authored = std::max(1U, authored_cascade_count);
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return std::min(authored, 4U);
  case oxygen::ShadowQualityTier::kMedium:
    return std::max(authored, 4U);
  case oxygen::ShadowQualityTier::kHigh:
    return std::max(authored, 5U);
  case oxygen::ShadowQualityTier::kUltra:
    return std::max(authored, 6U);
  default:
    return std::max(authored, 4U);
  }
}

[[nodiscard]] auto ResolveMaxVirtualAtlasResolution(
  const oxygen::ShadowQualityTier quality_tier,
  const std::chrono::milliseconds gpu_budget) -> std::uint32_t
{
  const auto ms = gpu_budget.count();
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return 2048U;
  case oxygen::ShadowQualityTier::kMedium:
    return 3072U;
  case oxygen::ShadowQualityTier::kHigh:
    return ms <= 16 ? 4096U : 5120U;
  case oxygen::ShadowQualityTier::kUltra:
    return ms <= 16 ? 6144U : 8192U;
  default:
    return 4096U;
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
  const oxygen::ShadowQualityTier quality_tier,
  const std::chrono::milliseconds gpu_budget) -> std::uint32_t
{
  const auto ms = gpu_budget.count();
  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    return ms <= 16 ? 64U : 96U;
  case oxygen::ShadowQualityTier::kMedium:
    return ms <= 16 ? 96U : 144U;
  case oxygen::ShadowQualityTier::kHigh:
    return ms <= 16 ? 144U : 256U;
  case oxygen::ShadowQualityTier::kUltra:
    return ms <= 16 ? 256U : 384U;
  default:
    return 128U;
  }
}

[[nodiscard]] auto ResolveMaxFullResidencyWorkUnits(
  const std::chrono::milliseconds gpu_budget) -> std::uint64_t
{
  const auto ms = gpu_budget.count();
  if (ms <= 10) {
    return 8'000ULL;
  }
  if (ms <= 16) {
    return 16'000ULL;
  }
  if (ms <= 25) {
    return 28'000ULL;
  }
  if (ms <= 33) {
    return 48'000ULL;
  }
  return 96'000ULL;
}

[[nodiscard]] auto EstimateFullResidencyWorkUnits(
  const std::uint32_t total_pages, const std::size_t shadow_caster_count)
  -> std::uint64_t
{
  const auto casters = static_cast<std::uint64_t>(
    std::clamp<std::size_t>(shadow_caster_count, 1U, 4096U));
  return static_cast<std::uint64_t>(total_pages) * casters;
}

[[nodiscard]] auto ResolveMaxRenderedPages(
  const std::chrono::milliseconds gpu_budget,
  const std::size_t shadow_caster_count) -> std::uint32_t
{
  const auto casters = static_cast<std::uint64_t>(
    std::clamp<std::size_t>(shadow_caster_count, 1U, 4096U));
  const auto budget_work_units = ResolveMaxFullResidencyWorkUnits(gpu_budget);
  return static_cast<std::uint32_t>(
    std::max<std::uint64_t>(1ULL, budget_work_units / casters));
}

[[nodiscard]] auto ResolveMinUsefulRenderedPages(
  const std::uint32_t clip_level_count) -> std::uint32_t
{
  return std::max(clip_level_count, clip_level_count * 4U);
}

[[nodiscard]] auto ResolveCenteredResidentWindowPagesPerAxis(
  const std::uint32_t pages_per_axis, const std::uint32_t clip_level_count,
  const std::uint32_t max_rendered_pages) -> std::uint32_t
{
  if (pages_per_axis == 0U || clip_level_count == 0U) {
    return 0U;
  }

  if (max_rendered_pages
    >= clip_level_count * pages_per_axis * pages_per_axis) {
    return pages_per_axis;
  }

  const float pages_per_clip
    = static_cast<float>(std::max(1U, max_rendered_pages))
    / static_cast<float>(clip_level_count);
  const auto resident_pages = static_cast<std::uint32_t>(
    std::floor(std::sqrt(std::max(pages_per_clip, 1.0F))));
  return std::clamp(resident_pages, 1U, pages_per_axis);
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
  return (tile_x & 0x0FFFU) | ((tile_y & 0x0FFFU) << 12U) | kPageTableValidBit;
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
  , virtual_page_table_buffer_(oxygen::observer_ptr<Graphics>(gfx_),
      *staging_provider_, static_cast<std::uint32_t>(sizeof(std::uint32_t)),
      oxygen::observer_ptr<engine::upload::InlineTransfersCoordinator>(
        inline_transfers_),
      "VirtualShadowMapBackend.PageTable")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

VirtualShadowMapBackend::~VirtualShadowMapBackend() { ReleasePhysicalPool(); }

auto VirtualShadowMapBackend::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  frame_sequence_ = sequence;
  shadow_instance_buffer_.OnFrameStart(sequence, slot);
  directional_virtual_metadata_buffer_.OnFrameStart(sequence, slot);
  virtual_page_table_buffer_.OnFrameStart(sequence, slot);
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

  const auto key = BuildPublicationKey(view_constants, directional_candidates,
    shadow_caster_bounds, visible_receiver_bounds, shadow_caster_content_hash);

  ViewCacheEntry state {};
  state.key = key;

  const auto pool_config
    = BuildPhysicalPoolConfig(directional_candidates.front(), gpu_budget);
  const auto max_rendered_pages
    = ResolveMaxRenderedPages(gpu_budget, shadow_caster_bounds.size());
  const auto estimated_work_units = EstimateFullResidencyWorkUnits(
    std::min(pool_config.virtual_page_count, max_rendered_pages),
    shadow_caster_bounds.size());
  if (allow_budget_fallback
    && estimated_work_units > ResolveMaxFullResidencyWorkUnits(gpu_budget)) {
    if (max_rendered_pages
      < ResolveMinUsefulRenderedPages(pool_config.clip_level_count)) {
      return {};
    }
  }
  EnsurePhysicalPool(pool_config);
  const auto previous_it = view_cache_.find(view_id);
  BuildDirectionalVirtualViewState(view_id, view_constants,
    directional_candidates.front(), shadow_caster_bounds,
    visible_receiver_bounds, max_rendered_pages,
    previous_it != view_cache_.end() ? &previous_it->second : nullptr, state);
  state.pending_raster_jobs = state.raster_jobs;

  if (previous_it != view_cache_.end()
    && previous_it->second.pending_raster_jobs.empty()
    && CanReuseResidentPages(previous_it->second, state)) {
    state.pending_raster_jobs.clear();
  }

  state.frame_publication.shadow_instance_metadata_srv
    = PublishShadowInstances(state.shadow_instances);
  state.frame_publication.virtual_directional_shadow_metadata_srv
    = PublishDirectionalVirtualMetadata(state.directional_virtual_metadata);
  state.frame_publication.virtual_shadow_page_table_srv
    = PublishPageTable(state.page_table_entries);
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
  RefreshViewExports(it->second);
  return it->second.frame_publication;
}

auto VirtualShadowMapBackend::MarkRendered(const ViewId view_id) -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    return;
  }

  const auto pages_per_level = physical_pool_config_.pages_per_clip_axis
    * physical_pool_config_.pages_per_clip_axis;
  for (const auto& job : it->second.pending_raster_jobs) {
    const auto global_page_index
      = job.clip_level * pages_per_level + job.page_index;
    if (const auto resident_it
      = it->second.resident_pages.find(global_page_index);
      resident_it != it->second.resident_pages.end()) {
      resident_it->second.state
        = renderer::VirtualPageResidencyState::kResidentClean;
      resident_it->second.last_touched_frame = frame_sequence_;
    }
  }
  it->second.pending_raster_jobs.clear();
  RefreshViewExports(it->second);
}

auto VirtualShadowMapBackend::SetPublishedViewFrameBindingsSlot(
  const ViewId view_id, const engine::BindlessViewFrameBindingsSlot slot)
  -> void
{
  const auto it = view_cache_.find(view_id);
  if (it == view_cache_.end()) {
    return;
  }

  for (auto& job : it->second.raster_jobs) {
    job.view_constants.view_frame_bindings_bslot = slot;
  }
  for (auto& job : it->second.pending_raster_jobs) {
    job.view_constants.view_frame_bindings_bslot = slot;
  }
  RefreshViewExports(it->second);
}

auto VirtualShadowMapBackend::SubmitRequestFeedback(
  const ViewId view_id, VirtualShadowRequestFeedback feedback) -> void
{
  request_feedback_.insert_or_assign(view_id,
    PendingRequestFeedback {
      .feedback = std::move(feedback),
    });
}

auto VirtualShadowMapBackend::ClearRequestFeedback(const ViewId view_id) -> void
{
  request_feedback_.erase(view_id);
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
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::uint64_t shadow_caster_content_hash) const
  -> PublicationKey
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
  key.receiver_hash = HashSpan(visible_receiver_bounds);
  key.shadow_content_hash = shadow_caster_content_hash;
  return key;
}

auto VirtualShadowMapBackend::RefreshViewExports(ViewCacheEntry& state) const
  -> void
{
  state.render_plan.depth_texture = physical_pool_texture_.get();
  state.render_plan.jobs = state.pending_raster_jobs;
  state.render_plan.page_size_texels = physical_pool_config_.page_size_texels;
  state.render_plan.atlas_tiles_per_axis
    = physical_pool_config_.atlas_tiles_per_axis;

  state.introspection.directional_virtual_metadata
    = state.directional_virtual_metadata;
  state.introspection.virtual_raster_jobs = state.raster_jobs;
  state.introspection.mapped_page_count = static_cast<std::uint32_t>(
    std::count_if(state.page_table_entries.begin(), state.page_table_entries.end(),
      [](const std::uint32_t entry) { return entry != 0U; }));
  state.introspection.resident_page_count = static_cast<std::uint32_t>(
    state.resident_pages.size());
  state.introspection.clean_page_count = 0U;
  state.introspection.dirty_page_count = 0U;
  state.introspection.pending_page_count = 0U;
  for (const auto& [_, resident_page] : state.resident_pages) {
    switch (resident_page.state) {
    case renderer::VirtualPageResidencyState::kResidentClean:
      ++state.introspection.clean_page_count;
      break;
    case renderer::VirtualPageResidencyState::kResidentDirty:
      ++state.introspection.dirty_page_count;
      break;
    case renderer::VirtualPageResidencyState::kPendingRender:
      ++state.introspection.pending_page_count;
      break;
    case renderer::VirtualPageResidencyState::kUnmapped:
      break;
    }
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

  const bool metadata_equal = previous.directional_virtual_metadata.empty()
    || std::memcmp(previous.directional_virtual_metadata.data(),
         current.directional_virtual_metadata.data(),
         previous.directional_virtual_metadata.size()
           * sizeof(engine::DirectionalVirtualShadowMetadata))
      == 0;
  if (!metadata_equal) {
    return false;
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
  const std::chrono::milliseconds gpu_budget) const -> PhysicalPoolConfig
{
  using namespace shadow_detail;

  PhysicalPoolConfig config {};
  config.clip_level_count = std::clamp(
    ResolveVirtualClipLevelCount(shadow_quality_tier_, candidate.cascade_count),
    1U, engine::kMaxVirtualDirectionalClipLevels);
  config.pages_per_clip_axis
    = std::min(ResolvePagesPerAxis(shadow_quality_tier_),
      ResolveBudgetPagesPerAxisCap(gpu_budget));
  config.virtual_page_count = config.clip_level_count * config.pages_per_clip_axis
    * config.pages_per_clip_axis;
  config.physical_tile_capacity = std::min(config.virtual_page_count,
    ResolvePhysicalTileCapacity(shadow_quality_tier_, gpu_budget));
  config.atlas_tiles_per_axis = static_cast<std::uint32_t>(std::ceil(
    std::sqrt(static_cast<float>(std::max(1U, config.physical_tile_capacity)))));

  const auto authored_resolution = ApplyDirectionalShadowQualityTier(
    ShadowResolutionFromHint(candidate.resolution_hint), shadow_quality_tier_,
    1U);
  const auto authored_page_size
    = authored_resolution / std::max(1U, config.pages_per_clip_axis);
  const auto atlas_limited_page_size = std::max(128U,
    ResolveMaxVirtualAtlasResolution(shadow_quality_tier_, gpu_budget)
      / std::max(1U, config.atlas_tiles_per_axis));
  const auto max_page_size = std::min(
    ResolveMaxVirtualPageSizeTexels(shadow_quality_tier_),
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
    || config.pages_per_clip_axis != physical_pool_config_.pages_per_clip_axis
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
    physical_pool_config_.pages_per_clip_axis,
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

auto VirtualShadowMapBackend::AcquirePhysicalTile(
  ViewCacheEntry& state, const std::uint32_t pages_per_level)
  -> std::optional<PhysicalTileAddress>
{
  if (const auto free_tile = AllocatePhysicalTile(); free_tile.has_value()) {
    return free_tile;
  }

  auto eviction_it = state.resident_pages.end();
  std::uint32_t eviction_page_index = 0U;
  for (auto it = state.resident_pages.begin(); it != state.resident_pages.end();
    ++it) {
    if (it->second.last_requested_frame == frame_sequence_) {
      continue;
    }

    if (eviction_it == state.resident_pages.end()) {
      eviction_it = it;
      eviction_page_index = it->first;
      continue;
    }

    const auto current_clip_level = it->first / pages_per_level;
    const auto best_clip_level = eviction_page_index / pages_per_level;
    if (current_clip_level != best_clip_level) {
      if (current_clip_level > best_clip_level) {
        eviction_it = it;
        eviction_page_index = it->first;
      }
      continue;
    }

    if (it->second.last_touched_frame != eviction_it->second.last_touched_frame) {
      if (it->second.last_touched_frame < eviction_it->second.last_touched_frame) {
        eviction_it = it;
        eviction_page_index = it->first;
      }
      continue;
    }

    if (it->first < eviction_page_index) {
      eviction_it = it;
      eviction_page_index = it->first;
    }
  }

  if (eviction_it == state.resident_pages.end()) {
    return std::nullopt;
  }

  const auto tile = eviction_it->second.tile;
  if (eviction_page_index < state.page_table_entries.size()) {
    state.page_table_entries[eviction_page_index] = 0U;
  }
  state.resident_pages.erase(eviction_it);
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
  const std::uint32_t max_rendered_pages, const ViewCacheEntry* previous_state,
  ViewCacheEntry& state) -> void
{
  using namespace shadow_detail;

  if (!physical_pool_texture_) {
    return;
  }

  const auto camera_view_constants = view_constants.GetSnapshot();
  const glm::mat4 view_matrix = camera_view_constants.view_matrix;
  const glm::mat4 projection_matrix = camera_view_constants.projection_matrix;
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

  const std::uint32_t clip_level_count = physical_pool_config_.clip_level_count;
  const std::uint32_t pages_per_axis
    = physical_pool_config_.pages_per_clip_axis;
  const std::uint32_t pages_per_level = pages_per_axis * pages_per_axis;
  const std::uint32_t fallback_resident_pages_per_axis
    = ResolveCenteredResidentWindowPagesPerAxis(
      pages_per_axis, clip_level_count, max_rendered_pages);

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
  std::array<float, engine::kMaxVirtualDirectionalClipLevels> camera_page_x {};
  std::array<float, engine::kMaxVirtualDirectionalClipLevels> camera_page_y {};

  float prev_depth = std::max(near_depth, 0.0F);
  float largest_half_extent = 0.0F;
  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    const float clip_end_depth = ResolveClipEndDepth(
      candidate, clip_index, prev_depth, near_depth, far_depth);
    const float half_extent = std::max(clip_end_depth, kMinClipSpan);
    clip_half_extents[clip_index] = half_extent;
    clip_page_world[clip_index] = (2.0F * half_extent)
      / std::max(1.0F, static_cast<float>(pages_per_axis));
    largest_half_extent = std::max(largest_half_extent, half_extent);
    prev_depth = clip_end_depth;
  }

  const glm::vec3 light_eye = camera_position
    + light_dir_to_light * (largest_half_extent + kLightPullbackPadding);
  const glm::mat4 light_view
    = glm::lookAtRH(light_eye, camera_position, world_up);
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

  state.page_table_entries.resize(static_cast<std::size_t>(clip_level_count)
      * static_cast<std::size_t>(pages_per_level),
    0U);
  state.raster_jobs.reserve(state.page_table_entries.size());

  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    const float half_extent = clip_half_extents[clip_index];
    const float page_world_size = clip_page_world[clip_index];
    clip_origin_x[clip_index]
      = std::floor((camera_ls.x - half_extent) / page_world_size)
      * page_world_size;
    clip_origin_y[clip_index]
      = std::floor((camera_ls.y - half_extent) / page_world_size)
      * page_world_size;
    camera_page_x[clip_index]
      = (camera_ls.x - clip_origin_x[clip_index]) / page_world_size;
    camera_page_y[clip_index]
      = (camera_ls.y - clip_origin_y[clip_index]) / page_world_size;
    metadata.clip_metadata[clip_index].origin_page_scale
      = glm::vec4(clip_origin_x[clip_index], clip_origin_y[clip_index],
        page_world_size, depth_scale);
    metadata.clip_metadata[clip_index].bias_reserved
      = glm::vec4(depth_bias, 0.0F, 0.0F, 0.0F);
  }

  std::vector<std::uint16_t> receiver_hits(state.page_table_entries.size(), 0U);
  std::vector<std::uint8_t> selected_pages(state.page_table_entries.size(), 0U);
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
    selected_pages[global_page_index] = 1U;
    return true;
  };

  const auto feedback_it = request_feedback_.find(view_id);
  const auto use_request_feedback = feedback_it != request_feedback_.end()
    && !feedback_it->second.feedback.requested_page_indices.empty()
    && feedback_it->second.feedback.pages_per_axis == pages_per_axis
    && feedback_it->second.feedback.clip_level_count == clip_level_count;

  if (use_request_feedback) {
    for (const auto global_page_index :
      feedback_it->second.feedback.requested_page_indices) {
      if (global_page_index >= receiver_hits.size()) {
        continue;
      }

      receiver_hits[global_page_index] = 0xFFFFU;
      const auto clip_index = global_page_index / pages_per_level;
      const auto local_page_index = global_page_index % pages_per_level;
      const auto page_y = local_page_index / pages_per_axis;
      const auto page_x = local_page_index % pages_per_axis;

      for (std::int32_t dy = -1; dy <= 1; ++dy) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
          const auto nx = static_cast<std::int32_t>(page_x) + dx;
          const auto ny = static_cast<std::int32_t>(page_y) + dy;
          if (nx < 0 || ny < 0
            || nx >= static_cast<std::int32_t>(pages_per_axis)
            || ny >= static_cast<std::int32_t>(pages_per_axis)) {
            continue;
          }

          const auto neighbor_index
            = static_cast<std::size_t>(clip_index)
                * static_cast<std::size_t>(pages_per_level)
              + static_cast<std::size_t>(
                static_cast<std::uint32_t>(ny) * pages_per_axis
                + static_cast<std::uint32_t>(nx));
          if (receiver_hits[neighbor_index] == 0U) {
            receiver_hits[neighbor_index] = 1U;
          }
        }
      }
    }
  } else {
    for (const auto& receiver : visible_receiver_bounds) {
      if (receiver.w <= 0.0F) {
        continue;
      }

      const glm::vec3 receiver_center_ls = glm::vec3(
        light_view * glm::vec4(receiver.x, receiver.y, receiver.z, 1.0F));
      const float radius = receiver.w;

      for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
        ++clip_index) {
        const float page_world_size = clip_page_world[clip_index];
        const float min_page_x
          = (receiver_center_ls.x - radius - clip_origin_x[clip_index])
          / page_world_size;
        const float max_page_x
          = (receiver_center_ls.x + radius - clip_origin_x[clip_index])
          / page_world_size;
        const float min_page_y
          = (receiver_center_ls.y - radius - clip_origin_y[clip_index])
          / page_world_size;
        const float max_page_y
          = (receiver_center_ls.y + radius - clip_origin_y[clip_index])
          / page_world_size;

        if (max_page_x < 0.0F || max_page_y < 0.0F
          || min_page_x >= static_cast<float>(pages_per_axis)
          || min_page_y >= static_cast<float>(pages_per_axis)) {
          continue;
        }

        const auto begin_x
          = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_page_x)));
        const auto end_x = static_cast<std::uint32_t>(std::min(
          static_cast<float>(pages_per_axis - 1U), std::floor(max_page_x)));
        const auto begin_y
          = static_cast<std::uint32_t>(std::max(0.0F, std::floor(min_page_y)));
        const auto end_y = static_cast<std::uint32_t>(std::min(
          static_cast<float>(pages_per_axis - 1U), std::floor(max_page_y)));

        for (std::uint32_t page_y = begin_y; page_y <= end_y; ++page_y) {
          for (std::uint32_t page_x = begin_x; page_x <= end_x; ++page_x) {
            const auto global_page_index = static_cast<std::size_t>(clip_index)
                * static_cast<std::size_t>(pages_per_level)
              + static_cast<std::size_t>(page_y * pages_per_axis + page_x);
            receiver_hits[global_page_index]
              = static_cast<std::uint16_t>(std::min(
                static_cast<std::uint32_t>(receiver_hits[global_page_index])
                  + 1U,
                0xFFFFU));
          }
        }
      }
    }
  }

  struct RequestedPageCandidate {
    std::uint32_t clip_index { 0U };
    std::uint32_t page_x { 0U };
    std::uint32_t page_y { 0U };
    std::uint32_t local_page_index { 0U };
    std::uint16_t hit_count { 0U };
    float distance2_to_camera { 0.0F };
  };

  std::vector<std::vector<RequestedPageCandidate>> per_clip_candidates(
    clip_level_count);
  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    auto& clip_candidates = per_clip_candidates[clip_index];
    clip_candidates.reserve(pages_per_level);
    for (std::uint32_t page_y = 0U; page_y < pages_per_axis; ++page_y) {
      for (std::uint32_t page_x = 0U; page_x < pages_per_axis; ++page_x) {
        const std::uint32_t local_page_index = page_y * pages_per_axis + page_x;
        const auto global_page_index = static_cast<std::size_t>(clip_index)
            * static_cast<std::size_t>(pages_per_level)
          + local_page_index;
        if (receiver_hits[global_page_index] == 0U) {
          continue;
        }

        const float page_center_x = static_cast<float>(page_x) + 0.5F;
        const float page_center_y = static_cast<float>(page_y) + 0.5F;
        const float dx = page_center_x - camera_page_x[clip_index];
        const float dy = page_center_y - camera_page_y[clip_index];
        clip_candidates.push_back(RequestedPageCandidate {
          .clip_index = clip_index,
          .page_x = page_x,
          .page_y = page_y,
          .local_page_index = local_page_index,
          .hit_count = receiver_hits[global_page_index],
          .distance2_to_camera = dx * dx + dy * dy,
        });
      }
    }

    std::sort(clip_candidates.begin(), clip_candidates.end(),
      [](const RequestedPageCandidate& lhs, const RequestedPageCandidate& rhs) {
        if (lhs.hit_count != rhs.hit_count) {
          return lhs.hit_count > rhs.hit_count;
        }
        if (lhs.distance2_to_camera != rhs.distance2_to_camera) {
          return lhs.distance2_to_camera < rhs.distance2_to_camera;
        }
        return lhs.local_page_index < rhs.local_page_index;
      });
  }

  std::uint32_t pages_remaining = std::max(1U,
    std::min(std::min(max_rendered_pages, physical_pool_config_.physical_tile_capacity),
      static_cast<std::uint32_t>(selected_pages.size())));
  for (std::uint32_t clip_index = 0U;
    clip_index < clip_level_count && pages_remaining > 0U; ++clip_index) {
    const auto& clip_candidates = per_clip_candidates[clip_index];
    if (clip_candidates.empty()) {
      continue;
    }
    if (mark_selected_page(clip_index, clip_candidates.front().page_x,
          clip_candidates.front().page_y)) {
      --pages_remaining;
    }
  }

  while (pages_remaining > 0U) {
    const RequestedPageCandidate* best_candidate = nullptr;
    for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
      ++clip_index) {
      const auto& clip_candidates = per_clip_candidates[clip_index];
      for (const auto& candidate_page : clip_candidates) {
        const auto global_page_index
          = static_cast<std::size_t>(candidate_page.clip_index)
            * static_cast<std::size_t>(pages_per_level)
          + static_cast<std::size_t>(candidate_page.local_page_index);
        if (selected_pages[global_page_index] != 0U) {
          continue;
        }
        if (best_candidate == nullptr
          || candidate_page.clip_index < best_candidate->clip_index
          || (candidate_page.clip_index == best_candidate->clip_index
            && (candidate_page.hit_count > best_candidate->hit_count
              || (candidate_page.hit_count == best_candidate->hit_count
                && candidate_page.distance2_to_camera
                  < best_candidate->distance2_to_camera)))) {
          best_candidate = &candidate_page;
        }
        break;
      }
    }

    if (best_candidate == nullptr) {
      break;
    }

    if (!mark_selected_page(best_candidate->clip_index, best_candidate->page_x,
          best_candidate->page_y)) {
      break;
    }
    --pages_remaining;
  }

  if (std::none_of(selected_pages.begin(), selected_pages.end(),
        [](const std::uint8_t value) { return value != 0U; })) {
    pages_remaining = std::max(1U,
      std::min(
        std::min(max_rendered_pages, physical_pool_config_.physical_tile_capacity),
        static_cast<std::uint32_t>(selected_pages.size())));
    for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
      ++clip_index) {
      if (pages_remaining == 0U) {
        break;
      }
      const std::uint32_t resident_origin_x
        = (pages_per_axis - fallback_resident_pages_per_axis) / 2U;
      const std::uint32_t resident_origin_y
        = (pages_per_axis - fallback_resident_pages_per_axis) / 2U;
      const std::uint32_t resident_end_x
        = resident_origin_x + fallback_resident_pages_per_axis;
      const std::uint32_t resident_end_y
        = resident_origin_y + fallback_resident_pages_per_axis;
      for (std::uint32_t page_y = resident_origin_y; page_y < resident_end_y;
        ++page_y) {
        if (pages_remaining == 0U) {
          break;
        }
        for (std::uint32_t page_x = resident_origin_x; page_x < resident_end_x;
          ++page_x) {
          if (pages_remaining == 0U) {
            break;
          }
          if (mark_selected_page(clip_index, page_x, page_y)) {
            --pages_remaining;
          }
        }
      }
    }
  }

  std::array<bool, engine::kMaxVirtualDirectionalClipLevels>
    reusable_clip_contents {};
  reusable_clip_contents.fill(false);
  const bool shadow_contents_unchanged = previous_state != nullptr
    && previous_state->key.candidate_hash == state.key.candidate_hash
    && previous_state->key.caster_hash == state.key.caster_hash
    && previous_state->key.shadow_content_hash == state.key.shadow_content_hash;
  if (shadow_contents_unchanged
    && previous_state->directional_virtual_metadata.size() == 1U) {
    const auto& previous_metadata
      = previous_state->directional_virtual_metadata.front();
    const bool light_view_equal = std::memcmp(&previous_metadata.light_view,
                                    &metadata.light_view, sizeof(glm::mat4))
      == 0;
    if (light_view_equal
      && previous_metadata.pages_per_axis == metadata.pages_per_axis
      && previous_metadata.page_size_texels == metadata.page_size_texels
      && previous_metadata.clip_level_count == metadata.clip_level_count) {
      for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
        ++clip_index) {
        reusable_clip_contents[clip_index]
          = std::memcmp(&previous_metadata.clip_metadata[clip_index],
              &metadata.clip_metadata[clip_index],
              sizeof(engine::DirectionalVirtualClipMetadata))
          == 0;
      }
    }
  }

  if (previous_state != nullptr) {
    const std::unordered_set<std::uint32_t> requested_pages = [&]() {
      std::unordered_set<std::uint32_t> requested;
      requested.reserve(selected_pages.size());
      for (std::uint32_t global_page_index = 0U;
        global_page_index < selected_pages.size(); ++global_page_index) {
        if (selected_pages[global_page_index] != 0U) {
          requested.insert(global_page_index);
        }
      }
      return requested;
    }();

    for (const auto& [virtual_page_index, resident_page] :
      previous_state->resident_pages) {
      const auto clip_index = virtual_page_index / pages_per_level;
      const bool retain_clean_page = !requested_pages.contains(virtual_page_index)
        && clip_index < clip_level_count && resident_page.ContentsValid()
        && shadow_contents_unchanged && reusable_clip_contents[clip_index];
      if (retain_clean_page) {
        state.resident_pages.insert_or_assign(virtual_page_index, resident_page);
        continue;
      }

      if (!requested_pages.contains(virtual_page_index)) {
        ReleasePhysicalTile(resident_page.tile);
      }
    }
  }

  for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
    ++clip_index) {
    const float page_world_size = clip_page_world[clip_index];
    const float origin_x = clip_origin_x[clip_index];
    const float origin_y = clip_origin_y[clip_index];
    const std::uint32_t filter_guard_texels
      = std::min(kVirtualShadowMaxFilterGuardTexels,
        SelectDirectionalVirtualFilterRadiusTexels(
          clip_page_world[0], clip_page_world[clip_index]));
    const float interior_texels = std::max(1.0F,
      static_cast<float>(physical_pool_config_.page_size_texels)
        - 2.0F * static_cast<float>(filter_guard_texels));
    const float page_guard_world = static_cast<float>(filter_guard_texels)
      * (page_world_size / interior_texels);

    for (std::uint32_t page_y = 0U; page_y < pages_per_axis; ++page_y) {
      for (std::uint32_t page_x = 0U; page_x < pages_per_axis; ++page_x) {
        const std::uint32_t local_page_index = page_y * pages_per_axis + page_x;
        const std::uint32_t global_page_index
          = clip_index * pages_per_level + local_page_index;
        if (selected_pages[global_page_index] == 0U) {
          continue;
        }
        bool needs_raster = true;
        ResidentVirtualPage resident_page {};
        if (previous_state != nullptr) {
          if (const auto previous_resident_it
            = previous_state->resident_pages.find(global_page_index);
            previous_resident_it != previous_state->resident_pages.end()) {
            resident_page = previous_resident_it->second;
            needs_raster = !resident_page.ContentsValid()
              || !reusable_clip_contents[clip_index];
          } else {
            const auto allocated_tile
              = AcquirePhysicalTile(state, pages_per_level);
            if (!allocated_tile.has_value()) {
              continue;
            }
            resident_page.tile = *allocated_tile;
            resident_page.state
              = renderer::VirtualPageResidencyState::kPendingRender;
            resident_page.last_touched_frame = frame_sequence_;
            resident_page.last_requested_frame = frame_sequence_;
          }
        } else {
          const auto allocated_tile
            = AcquirePhysicalTile(state, pages_per_level);
          if (!allocated_tile.has_value()) {
            continue;
          }
          resident_page.tile = *allocated_tile;
          resident_page.state
            = renderer::VirtualPageResidencyState::kPendingRender;
          resident_page.last_touched_frame = frame_sequence_;
          resident_page.last_requested_frame = frame_sequence_;
        }
        resident_page.state = needs_raster
          ? renderer::VirtualPageResidencyState::kPendingRender
          : renderer::VirtualPageResidencyState::kResidentClean;
        resident_page.last_touched_frame = frame_sequence_;
        resident_page.last_requested_frame = frame_sequence_;
        state.resident_pages.insert_or_assign(global_page_index, resident_page);
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
        const glm::mat4 light_proj = glm::orthoRH_ZO(
          left, right, guarded_bottom, guarded_top, near_plane, far_plane);

        engine::ViewConstants page_view_constants = view_constants;
        page_view_constants.SetViewMatrix(light_view)
          .SetProjectionMatrix(light_proj)
          .SetCameraPosition(light_eye);

        if (needs_raster) {
          state.raster_jobs.push_back(VirtualShadowRasterJob {
            .shadow_instance_index = 0U,
            .payload_index = 0U,
            .clip_level = clip_index,
            .page_index = local_page_index,
            .atlas_tile_x = resident_page.tile.tile_x,
            .atlas_tile_y = resident_page.tile.tile_y,
            .view_constants = page_view_constants.GetSnapshot(),
          });
        }
      }
    }
  }

  state.directional_virtual_metadata.push_back(metadata);

  if (use_request_feedback) {
    request_feedback_.erase(view_id);
  }
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

auto VirtualShadowMapBackend::PublishPageTable(
  const std::span<const std::uint32_t> entries) -> ShaderVisibleIndex
{
  if (entries.empty()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto result = virtual_page_table_buffer_.Allocate(
    static_cast<std::uint32_t>(entries.size()));
  if (!result) {
    LOG_F(ERROR,
      "VirtualShadowMapBackend: failed to allocate virtual shadow page table "
      "buffer: {}",
      result.error().message());
    return kInvalidShaderVisibleIndex;
  }

  const auto& allocation = *result;
  if (allocation.mapped_ptr != nullptr) {
    std::memcpy(allocation.mapped_ptr, entries.data(),
      entries.size() * sizeof(std::uint32_t));
  }
  return allocation.srv;
}

} // namespace oxygen::renderer::internal
