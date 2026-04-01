//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/ConventionalShadowBackend.h>
#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Scene/Light/LightCommon.h>

namespace {

constexpr float kMinCascadeSpan = 0.1F;
constexpr float kLightPullbackPadding = 32.0F;
constexpr float kMinShadowDepthPadding = 8.0F;
constexpr float kDirectionalShadowKernelPaddingTexels = 3.0F;
constexpr float kDirectionalShadowSnapPaddingTexels = 1.0F;
constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

[[nodiscard]] auto HashBytes(const void* data, const std::size_t size,
  std::uint64_t hash = kFnvOffsetBasis) -> std::uint64_t
{
  const auto* bytes = static_cast<const std::byte*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(bytes[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

template <typename T>
[[nodiscard]] auto HashSpan(const std::span<const T> values,
  std::uint64_t hash = kFnvOffsetBasis) -> std::uint64_t
{
  const auto size = values.size();
  hash = HashBytes(&size, sizeof(size), hash);
  if (!values.empty()) {
    hash = HashBytes(values.data(), values.size_bytes(), hash);
  }
  return hash;
}

[[nodiscard]] auto BuildShadowProductFlags(const std::uint32_t light_flags)
  -> std::uint32_t
{
  using oxygen::engine::DirectionalLightFlags;
  using oxygen::engine::ShadowProductFlags;

  auto flags = ShadowProductFlags::kValid;
  const auto directional_flags
    = static_cast<DirectionalLightFlags>(light_flags);

  if ((directional_flags & DirectionalLightFlags::kContactShadows)
    != DirectionalLightFlags::kNone) {
    flags |= ShadowProductFlags::kContactShadows;
  }
  if ((directional_flags & DirectionalLightFlags::kSunLight)
    != DirectionalLightFlags::kNone) {
    flags |= ShadowProductFlags::kSunLight;
  }

  return static_cast<std::uint32_t>(flags);
}

[[nodiscard]] auto NormalizeOrFallback(
  const glm::vec3& value, const glm::vec3& fallback) -> glm::vec3
{
  const float len_sq = glm::dot(value, value);
  if (len_sq <= oxygen::math::EpsilonDirection) {
    return fallback;
  }
  return glm::normalize(value);
}

[[nodiscard]] auto ShadowResolutionFromHint(const std::uint32_t hint)
  -> std::uint32_t
{
  using oxygen::scene::ShadowResolutionHint;
  switch (static_cast<ShadowResolutionHint>(hint)) {
  case ShadowResolutionHint::kLow:
    return 1024U;
  case ShadowResolutionHint::kMedium:
    return 2048U;
  case ShadowResolutionHint::kHigh:
    return 3072U;
  case ShadowResolutionHint::kUltra:
    return 4096U;
  default:
    return 2048U;
  }
}

[[nodiscard]] auto ApplyDirectionalShadowQualityTier(
  const std::uint32_t authored_resolution,
  const oxygen::ShadowQualityTier quality_tier,
  const std::size_t directional_candidate_count) -> std::uint32_t
{
  const bool single_dominant_directional = directional_candidate_count <= 1U;
  std::uint32_t preferred_min_resolution = authored_resolution;
  std::uint32_t max_resolution = authored_resolution;

  switch (quality_tier) {
  case oxygen::ShadowQualityTier::kLow:
    preferred_min_resolution = 1024U;
    max_resolution = 1024U;
    break;
  case oxygen::ShadowQualityTier::kMedium:
    preferred_min_resolution = 2048U;
    max_resolution = 2048U;
    break;
  case oxygen::ShadowQualityTier::kHigh:
    preferred_min_resolution = single_dominant_directional ? 3072U : 2048U;
    max_resolution = 3072U;
    break;
  case oxygen::ShadowQualityTier::kUltra:
    preferred_min_resolution = single_dominant_directional ? 4096U : 3072U;
    max_resolution = 4096U;
    break;
  default:
    preferred_min_resolution = 2048U;
    max_resolution = 2048U;
    break;
  }

  return std::clamp(std::max(authored_resolution, preferred_min_resolution),
    1024U, max_resolution);
}

[[nodiscard]] auto TransformPoint(
  const glm::mat4& matrix, const glm::vec3& point) -> glm::vec3
{
  const glm::vec4 transformed = matrix * glm::vec4(point, 1.0F);
  const float inv_w
    = std::abs(transformed.w) > 1.0e-6F ? (1.0F / transformed.w) : 1.0F;
  return glm::vec3(transformed) * inv_w;
}

[[nodiscard]] auto BuildDirectionalLightRotation(
  const glm::vec3& light_dir_to_surface, const glm::vec3& up) -> glm::mat4
{
  return glm::lookAtRH(glm::vec3(0.0F), light_dir_to_surface, up);
}

[[nodiscard]] auto SnapLightSpaceCenter(const glm::vec3& center_ls,
  const float half_extent_x, const float half_extent_y,
  const std::uint32_t resolution) -> glm::vec3
{
  if (half_extent_x <= 0.0F || half_extent_y <= 0.0F || resolution == 0U) {
    return center_ls;
  }

  const float world_units_per_texel_x
    = (2.0F * half_extent_x) / static_cast<float>(resolution);
  const float world_units_per_texel_y
    = (2.0F * half_extent_y) / static_cast<float>(resolution);
  if (world_units_per_texel_x <= 0.0F || world_units_per_texel_y <= 0.0F) {
    return center_ls;
  }

  glm::vec3 snapped = center_ls;
  snapped.x = std::floor(center_ls.x / world_units_per_texel_x)
    * world_units_per_texel_x;
  snapped.y = std::floor(center_ls.y / world_units_per_texel_y)
    * world_units_per_texel_y;
  return snapped;
}

[[nodiscard]] auto ComputePaddedHalfExtent(
  const float base_extent, const std::uint32_t resolution) -> float
{
  if (base_extent <= 0.0F || resolution == 0U) {
    return base_extent;
  }

  const float total_guard_texels = 2.0F
    * (kDirectionalShadowKernelPaddingTexels
      + kDirectionalShadowSnapPaddingTexels);
  const float usable_texels
    = std::max(1.0F, static_cast<float>(resolution) - total_guard_texels);
  return base_extent * (static_cast<float>(resolution) / usable_texels);
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

auto TightenDepthRangeWithShadowCasters(
  const std::span<const glm::vec4> shadow_caster_bounds,
  const glm::mat4& light_view, const float ortho_half_extent_x,
  const float ortho_half_extent_y, float& min_depth, float& max_depth) -> bool
{
  bool tightened = false;
  for (const auto& sphere : shadow_caster_bounds) {
    const float radius = sphere.w;
    if (radius <= 0.0F) {
      continue;
    }

    const glm::vec3 center_ls
      = glm::vec3(light_view * glm::vec4(glm::vec3(sphere), 1.0F));
    if (std::abs(center_ls.x) > ortho_half_extent_x + radius
      || std::abs(center_ls.y) > ortho_half_extent_y + radius) {
      continue;
    }

    min_depth = std::min(min_depth, center_ls.z - radius);
    max_depth = std::max(max_depth, center_ls.z + radius);
    tightened = true;
  }

  return tightened;
}

auto SphereOverlapsViewDepthRange(const glm::vec4& sphere,
  const glm::mat4& view_matrix, const float min_depth, const float max_depth)
  -> bool
{
  const float radius = sphere.w;
  if (radius <= 0.0F) {
    return false;
  }

  const glm::vec3 center_vs
    = glm::vec3(view_matrix * glm::vec4(glm::vec3(sphere), 1.0F));
  const float nearest_depth = std::max(0.0F, -center_vs.z - radius);
  const float farthest_depth = std::max(0.0F, -center_vs.z + radius);
  return farthest_depth >= min_depth && nearest_depth <= max_depth;
}

auto AccumulateReceiverLightSpaceExtents(
  const std::span<const glm::vec4> visible_receiver_bounds,
  const glm::mat4& view_matrix, const glm::mat4& light_rotation,
  const float min_depth, const float max_depth, glm::vec2& min_xy,
  glm::vec2& max_xy) -> bool
{
  bool found_receivers = false;
  for (const auto& sphere : visible_receiver_bounds) {
    const float radius = sphere.w;
    if (!SphereOverlapsViewDepthRange(
          sphere, view_matrix, min_depth, max_depth)) {
      continue;
    }

    const glm::vec3 center_ls
      = glm::vec3(light_rotation * glm::vec4(glm::vec3(sphere), 1.0F));
    const glm::vec2 sphere_min = glm::vec2(center_ls) - glm::vec2(radius);
    const glm::vec2 sphere_max = glm::vec2(center_ls) + glm::vec2(radius);
    if (!found_receivers) {
      min_xy = sphere_min;
      max_xy = sphere_max;
      found_receivers = true;
    } else {
      min_xy = glm::min(min_xy, sphere_min);
      max_xy = glm::max(max_xy, sphere_max);
    }
  }

  return found_receivers;
}

[[nodiscard]] auto ResolveCascadeEndDepth(
  const oxygen::engine::DirectionalShadowCandidate& candidate,
  const std::uint32_t cascade_index, const float prev_depth,
  const float near_depth, const float far_depth) -> float
{
  const auto cascade_count = std::max(
    1U, std::min(candidate.cascade_count, oxygen::scene::kMaxShadowCascades));
  const float authored_end = candidate.cascade_distances[cascade_index];
  if (authored_end > prev_depth + kMinCascadeSpan) {
    return std::min(authored_end, far_depth);
  }

  const float normalized_split = static_cast<float>(cascade_index + 1U)
    / static_cast<float>(cascade_count);
  const float stabilized_near_depth = std::max(near_depth, 0.001F);
  const float stabilized_far_depth
    = std::max(far_depth, stabilized_near_depth + kMinCascadeSpan);
  const float linear_split
    = glm::mix(stabilized_near_depth, stabilized_far_depth, normalized_split);
  const float logarithmic_split = stabilized_near_depth
    * std::pow(stabilized_far_depth / stabilized_near_depth, normalized_split);
  const float practical_lambda
    = MapDistributionExponentToPracticalLambda(candidate.distribution_exponent);
  const float generated_end
    = glm::mix(linear_split, logarithmic_split, practical_lambda);
  return std::max(
    prev_depth + kMinCascadeSpan, std::min(generated_end, far_depth));
}

} // namespace

namespace oxygen::renderer::internal {

ConventionalShadowBackend::ConventionalShadowBackend(
  const observer_ptr<Graphics> gfx, const observer_ptr<ProviderT> provider,
  const observer_ptr<CoordinatorT> inline_transfers,
  const oxygen::ShadowQualityTier quality_tier)
  : gfx_(gfx)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
  , shadow_quality_tier_(quality_tier)
  , shadow_instance_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::ShadowInstanceMetadata)),
      inline_transfers_, "ConventionalShadowBackend.ShadowInstanceMetadata")
  , directional_shadow_metadata_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::DirectionalShadowMetadata)),
      inline_transfers_, "ConventionalShadowBackend.DirectionalShadowMetadata")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

ConventionalShadowBackend::~ConventionalShadowBackend()
{
  ReleaseDirectionalResources();
}

auto ConventionalShadowBackend::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  shadow_instance_buffer_.OnFrameStart(sequence, slot);
  directional_shadow_metadata_buffer_.OnFrameStart(sequence, slot);
  view_cache_.clear();
}

auto ConventionalShadowBackend::ResetCachedState() -> void
{
  view_cache_.clear();
}

auto ConventionalShadowBackend::ReserveFrameResources(
  const std::span<const engine::DirectionalShadowCandidate>
    directional_candidates,
  const std::uint32_t scene_view_count) -> void
{
  if (scene_view_count == 0U || directional_candidates.empty()) {
    return;
  }

  const auto layers_per_view = CountDirectionalLayers(directional_candidates);
  if (layers_per_view == 0U) {
    return;
  }

  CHECK_F(scene_view_count
      <= (std::numeric_limits<std::uint32_t>::max() / layers_per_view),
    "ConventionalShadowBackend: scene_view_count={} with layers_per_view={} "
    "overflows the directional shadow layer reservation",
    scene_view_count, layers_per_view);

  const auto resource_config = BuildDirectionalResourceConfig(
    directional_candidates, layers_per_view * scene_view_count, 0U);
  EnsureDirectionalResources(resource_config);
}

auto ConventionalShadowBackend::PublishView(const ViewId view_id,
  const engine::ViewConstants& view_constants,
  const std::span<const engine::DirectionalShadowCandidate>
    directional_candidates,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::uint64_t shadow_caster_content_hash) -> ShadowFramePublication
{
  const auto key = BuildPublicationKey(view_constants, directional_candidates,
    shadow_caster_bounds, visible_receiver_bounds, shadow_caster_content_hash);
  if (const auto it = view_cache_.find(view_id);
    it != view_cache_.end() && it->second.key == key) {
    return it->second.frame_publication;
  }

  ViewCacheEntry state {};
  state.key = key;
  state.directional_layers_used
    = CountDirectionalLayers(directional_candidates);
  state.required_directional_resolution
    = BuildDirectionalResourceConfig(directional_candidates, 0U, 0U).resolution;

  if (directional_candidates.empty()) {
    LOG_F(INFO,
      "ConventionalShadowBackend: view {} has no eligible directional shadow "
      "candidates",
      view_id.get());
  }

  const auto base_resource_layer
    = CountPublishedDirectionalLayers(std::optional<ViewId> { view_id });
  const auto required_resolution
    = std::max(state.required_directional_resolution,
      MaxPublishedDirectionalResolution(std::optional<ViewId> { view_id }));
  const auto resource_config
    = BuildDirectionalResourceConfig(directional_candidates,
      base_resource_layer + state.directional_layers_used, required_resolution);
  EnsureDirectionalResources(resource_config);
  BuildDirectionalViewState(view_id, view_constants, directional_candidates,
    shadow_caster_bounds, visible_receiver_bounds, base_resource_layer, state);

  if (!directional_candidates.empty() && state.shadow_instances.empty()) {
    LOG_F(WARNING,
      "ConventionalShadowBackend: view {} had {} directional shadow "
      "candidate(s), but no shadow products were published",
      view_id.get(), directional_candidates.size());
  }

  state.frame_publication.shadow_instance_metadata_srv
    = PublishShadowInstances(state.shadow_instances);
  state.frame_publication.directional_shadow_metadata_srv
    = PublishDirectionalMetadata(state.directional_metadata);
  state.frame_publication.directional_shadow_texture_srv
    = directional_shadow_texture_srv_;
  for (std::size_t i = 0; i < state.shadow_instances.size(); ++i) {
    const auto& instance = state.shadow_instances[i];
    if ((instance.flags
          & static_cast<std::uint32_t>(engine::ShadowProductFlags::kSunLight))
      != 0U) {
      state.frame_publication.sun_shadow_index = static_cast<std::uint32_t>(i);
      break;
    }
  }

  auto [it, inserted] = view_cache_.insert_or_assign(view_id, std::move(state));
  DCHECK_F(inserted || it != view_cache_.end(),
    "ConventionalShadowBackend failed to publish view state");
  RefreshViewExports(it->second);
  return it->second.frame_publication;
}

auto ConventionalShadowBackend::SetPublishedViewFrameBindingsSlot(
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
  RefreshViewExports(it->second);
}

auto ConventionalShadowBackend::TryGetFramePublication(
  const ViewId view_id) const noexcept -> const ShadowFramePublication*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.frame_publication : nullptr;
}

auto ConventionalShadowBackend::TryGetShadowInstanceMetadata(
  const ViewId view_id) const noexcept -> const engine::ShadowInstanceMetadata*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() && !it->second.shadow_instances.empty()
    ? &it->second.shadow_instances.front()
    : nullptr;
}

auto ConventionalShadowBackend::TryGetRasterRenderPlan(
  const ViewId view_id) const noexcept -> const RasterShadowRenderPlan*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.raster_plan : nullptr;
}

auto ConventionalShadowBackend::TryGetReceiverAnalysisPlan(
  const ViewId view_id) const noexcept
  -> const ConventionalShadowReceiverAnalysisPlan*
{
  const auto it = view_cache_.find(view_id);
  return it != view_cache_.end() ? &it->second.receiver_analysis_plan : nullptr;
}

auto ConventionalShadowBackend::GetDirectionalShadowTexture() const noexcept
  -> const std::shared_ptr<graphics::Texture>&
{
  return directional_shadow_texture_;
}

auto ConventionalShadowBackend::BuildPublicationKey(
  const engine::ViewConstants& view_constants,
  const std::span<const engine::DirectionalShadowCandidate>
    directional_candidates,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::uint64_t shadow_caster_content_hash) const -> PublicationKey
{
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

auto ConventionalShadowBackend::CountDirectionalLayers(
  const std::span<const engine::DirectionalShadowCandidate> candidates) const
  -> std::uint32_t
{
  std::uint32_t required_layers = 0U;
  for (const auto& candidate : candidates) {
    required_layers += std::max(
      1U, std::min(candidate.cascade_count, oxygen::scene::kMaxShadowCascades));
  }
  return required_layers;
}

auto ConventionalShadowBackend::CountPublishedDirectionalLayers(
  const std::optional<ViewId> excluded_view_id) const -> std::uint32_t
{
  std::uint32_t required_layers = 0U;
  for (const auto& [cached_view_id, state] : view_cache_) {
    if (excluded_view_id.has_value() && cached_view_id == *excluded_view_id) {
      continue;
    }
    required_layers += state.directional_layers_used;
  }
  return required_layers;
}

auto ConventionalShadowBackend::MaxPublishedDirectionalResolution(
  const std::optional<ViewId> excluded_view_id) const -> std::uint32_t
{
  std::uint32_t resolution = 0U;
  for (const auto& [cached_view_id, state] : view_cache_) {
    if (excluded_view_id.has_value() && cached_view_id == *excluded_view_id) {
      continue;
    }
    resolution = std::max(resolution, state.required_directional_resolution);
  }
  return resolution;
}

auto ConventionalShadowBackend::RefreshViewExports(ViewCacheEntry& state) const
  -> void
{
  state.frame_publication.directional_shadow_texture_srv
    = directional_shadow_texture_srv_;
  state.raster_plan.depth_texture
    = observer_ptr { directional_shadow_texture_.get() };
  state.raster_plan.jobs = state.raster_jobs;
  state.receiver_analysis_plan.jobs = state.receiver_analysis_jobs;
}

auto ConventionalShadowBackend::RefreshAllViewExports() -> void
{
  for (auto& [_, state] : view_cache_) {
    RefreshViewExports(state);
  }
}

auto ConventionalShadowBackend::BuildDirectionalResourceConfig(
  const std::span<const engine::DirectionalShadowCandidate> candidates,
  const std::uint32_t required_layers,
  const std::uint32_t required_resolution) const
  -> DirectionalShadowResourceConfig
{
  DirectionalShadowResourceConfig config {};
  config.required_layers = required_layers;
  config.resolution = required_resolution;
  for (const auto& candidate : candidates) {
    config.resolution = std::max(config.resolution,
      ApplyDirectionalShadowQualityTier(
        ShadowResolutionFromHint(candidate.resolution_hint),
        shadow_quality_tier_, candidates.size()));
  }
  return config;
}

auto ConventionalShadowBackend::EnsureDirectionalResources(
  const DirectionalShadowResourceConfig& config) -> void
{
  if (config.required_layers == 0U || config.resolution == 0U) {
    return;
  }

  const bool needs_recreate = !directional_shadow_texture_
    || config.required_layers > directional_shadow_capacity_layers_
    || config.resolution > directional_shadow_resolution_;
  if (!needs_recreate) {
    return;
  }

  const auto old_texture = directional_shadow_texture_;
  const auto new_capacity_layers = config.required_layers;
  const auto new_resolution = config.resolution;

  graphics::TextureDesc desc {};
  desc.width = new_resolution;
  desc.height = new_resolution;
  desc.array_size = new_capacity_layers;
  desc.mip_levels = 1U;
  desc.format = oxygen::Format::kDepth32;
  desc.texture_type = oxygen::TextureType::kTexture2DArray;
  desc.is_render_target = true;
  desc.is_shader_resource = true;
  desc.is_typeless = true;
  desc.use_clear_value = true;
  // Conventional shadow depth also follows the engine reversed-Z convention.
  desc.clear_value = { 0.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kCommon;
  desc.debug_name = "DirectionalShadowDepthArray";

  auto new_texture = gfx_->CreateTexture(desc);
  if (!new_texture) {
    directional_shadow_capacity_layers_ = 0U;
    directional_shadow_resolution_ = 0U;
    throw std::runtime_error(
      "ConventionalShadowBackend: failed to create directional shadow texture");
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();
  registry.Register(new_texture);

  const graphics::TextureViewDescription srv_desc {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = oxygen::Format::kDepth32,
    .dimension = oxygen::TextureType::kTexture2DArray,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = 0U,
      .num_array_slices = new_capacity_layers,
    },
    .is_read_only_dsv = false,
  };

  graphics::NativeView new_srv_view {};
  auto handle
    = allocator.AllocateBindless(oxygen::bindless::generated::kGlobalSrvDomain,
      graphics::ResourceViewType::kTexture_SRV);
  if (!handle.IsValid()) {
    registry.UnRegisterResource(*new_texture);
    throw std::runtime_error(
      "ConventionalShadowBackend: failed to allocate directional shadow SRV "
      "descriptor");
  }

  const auto new_srv_index = allocator.GetShaderVisibleIndex(handle);
  new_srv_view
    = registry.RegisterView(*new_texture, std::move(handle), srv_desc);
  if (!new_srv_view->IsValid()) {
    registry.UnRegisterResource(*new_texture);
    throw std::runtime_error(
      "ConventionalShadowBackend: failed to register directional shadow SRV "
      "view");
  }

  directional_shadow_texture_ = std::move(new_texture);
  directional_shadow_texture_srv_ = new_srv_index;
  directional_shadow_texture_srv_view_ = new_srv_view;
  directional_shadow_capacity_layers_ = new_capacity_layers;
  directional_shadow_resolution_ = new_resolution;
  if (old_texture) {
    registry.UnRegisterResource(*old_texture);
  }
  RefreshAllViewExports();

  LOG_F(INFO,
    "ConventionalShadowBackend: created directional shadow texture {}x{} "
    "layers={} srv={}",
    directional_shadow_resolution_, directional_shadow_resolution_,
    directional_shadow_capacity_layers_, directional_shadow_texture_srv_.get());
}

auto ConventionalShadowBackend::ReleaseDirectionalResources() -> void
{
  if (!gfx_) {
    directional_shadow_texture_.reset();
    directional_shadow_texture_srv_view_ = {};
    directional_shadow_texture_srv_ = kInvalidShaderVisibleIndex;
    directional_shadow_resolution_ = 0U;
    directional_shadow_capacity_layers_ = 0U;
    return;
  }

  if (directional_shadow_texture_) {
    gfx_->GetResourceRegistry().UnRegisterResource(
      *directional_shadow_texture_);
  }

  directional_shadow_texture_.reset();
  directional_shadow_texture_srv_view_ = {};
  directional_shadow_texture_srv_ = kInvalidShaderVisibleIndex;
  directional_shadow_resolution_ = 0U;
  directional_shadow_capacity_layers_ = 0U;
}

auto ConventionalShadowBackend::BuildDirectionalViewState(const ViewId view_id,
  const engine::ViewConstants& view_constants,
  const std::span<const engine::DirectionalShadowCandidate> candidates,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::uint32_t base_resource_layer, ViewCacheEntry& state) -> void
{
  if (candidates.empty() || !directional_shadow_texture_) {
    return;
  }

  const auto camera_view_constants = view_constants.GetSnapshot();
  const glm::mat4 view_matrix = camera_view_constants.view_matrix;
  const glm::mat4 projection_matrix = camera_view_constants.projection_matrix;
  const glm::mat4 inv_view_proj = glm::inverse(projection_matrix * view_matrix);
  const glm::mat4 inv_proj = glm::inverse(projection_matrix);

  std::array<glm::vec3, 4> near_corners {};
  std::array<glm::vec3, 4> far_corners {};
  std::array<glm::vec3, 4> view_near_corners {};
  std::array<glm::vec3, 4> view_far_corners {};
  constexpr std::array<glm::vec2, 4> clip_corners {
    glm::vec2(-1.0F, -1.0F),
    glm::vec2(1.0F, -1.0F),
    glm::vec2(1.0F, 1.0F),
    glm::vec2(-1.0F, 1.0F),
  };
  for (std::size_t i = 0; i < clip_corners.size(); ++i) {
    near_corners[i]
      = TransformPoint(inv_view_proj, glm::vec3(clip_corners[i], 1.0F));
    far_corners[i]
      = TransformPoint(inv_view_proj, glm::vec3(clip_corners[i], 0.0F));
    view_near_corners[i]
      = TransformPoint(inv_proj, glm::vec3(clip_corners[i], 1.0F));
    view_far_corners[i]
      = TransformPoint(inv_proj, glm::vec3(clip_corners[i], 0.0F));
  }

  float near_depth = 0.0F;
  float far_depth = 0.0F;
  for (std::size_t i = 0; i < view_near_corners.size(); ++i) {
    near_depth += std::max(0.0F, -view_near_corners[i].z);
    far_depth += std::max(0.0F, -view_far_corners[i].z);
  }
  near_depth /= static_cast<float>(view_near_corners.size());
  far_depth /= static_cast<float>(view_far_corners.size());
  if (far_depth <= near_depth + kMinCascadeSpan) {
    LOG_F(WARNING,
      "ConventionalShadowBackend: invalid camera depth span for view {} "
      "(near_depth={} far_depth={}); skipping directional shadows",
      view_id.get(), near_depth, far_depth);
    return;
  }

  state.shadow_instances.reserve(candidates.size());
  state.directional_metadata.reserve(candidates.size());

  std::uint32_t next_resource_layer = base_resource_layer;
  for (const auto& candidate : candidates) {
    const auto cascade_count = std::max(
      1U, std::min(candidate.cascade_count, oxygen::scene::kMaxShadowCascades));
    const auto shadow_instance_index
      = static_cast<std::uint32_t>(state.shadow_instances.size());
    const auto payload_index
      = static_cast<std::uint32_t>(state.directional_metadata.size());
    const auto implementation_kind = static_cast<std::uint32_t>(
      engine::ShadowImplementationKind::kConventional);
    const auto flags = BuildShadowProductFlags(candidate.light_flags);
    const auto resource_index = next_resource_layer;
    next_resource_layer += cascade_count;

    state.shadow_instances.push_back(engine::ShadowInstanceMetadata {
      .light_index = candidate.light_index,
      .payload_index = payload_index,
      .domain = static_cast<std::uint32_t>(engine::ShadowDomain::kDirectional),
      .implementation_kind = implementation_kind,
      .flags = flags,
    });

    engine::DirectionalShadowMetadata metadata {};
    metadata.shadow_instance_index = shadow_instance_index;
    metadata.implementation_kind = implementation_kind;
    metadata.constant_bias = candidate.bias;
    metadata.normal_bias = candidate.normal_bias;
    metadata.cascade_count = cascade_count;
    metadata.flags = flags;
    metadata.distribution_exponent = candidate.distribution_exponent;
    metadata.resource_index = resource_index;

    float prev_depth = std::max(near_depth, 0.0F);
    const glm::vec3 light_dir_to_surface = NormalizeOrFallback(
      candidate.direction_ws, glm::vec3(0.0F, -1.0F, 0.0F));
    const glm::vec3 light_dir_to_light = -light_dir_to_surface;
    const glm::vec3 world_up
      = std::abs(glm::dot(light_dir_to_light, glm::vec3(0.0F, 0.0F, 1.0F)))
        > 0.95F
      ? glm::vec3(1.0F, 0.0F, 0.0F)
      : glm::vec3(0.0F, 0.0F, 1.0F);
    const glm::mat4 light_rotation
      = BuildDirectionalLightRotation(light_dir_to_surface, world_up);
    const glm::mat4 inv_light_rotation = glm::inverse(light_rotation);

    for (std::uint32_t cascade_index = 0; cascade_index < cascade_count;
      ++cascade_index) {
      const float cascade_begin_depth = prev_depth;
      const float end_depth = ResolveCascadeEndDepth(
        candidate, cascade_index, cascade_begin_depth, near_depth, far_depth);
      const float depth_range
        = std::max(far_depth - near_depth, kMinCascadeSpan);
      const float t0 = std::clamp(
        (cascade_begin_depth - near_depth) / depth_range, 0.0F, 1.0F);
      const float t1
        = std::clamp((end_depth - near_depth) / depth_range, 0.0F, 1.0F);

      std::array<glm::vec3, 8> slice_corners {};
      std::array<glm::vec3, 8> view_slice_corners {};
      for (std::size_t corner = 0; corner < near_corners.size(); ++corner) {
        slice_corners[corner]
          = glm::mix(near_corners[corner], far_corners[corner], t0);
        slice_corners[corner + near_corners.size()]
          = glm::mix(near_corners[corner], far_corners[corner], t1);
        view_slice_corners[corner]
          = glm::mix(view_near_corners[corner], view_far_corners[corner], t0);
        view_slice_corners[corner + near_corners.size()]
          = glm::mix(view_near_corners[corner], view_far_corners[corner], t1);
      }

      glm::vec3 slice_center_ws(0.0F);
      glm::vec3 view_slice_center(0.0F);
      for (std::size_t i = 0; i < slice_corners.size(); ++i) {
        slice_center_ws += slice_corners[i];
        view_slice_center += view_slice_corners[i];
      }
      slice_center_ws /= static_cast<float>(slice_corners.size());
      view_slice_center /= static_cast<float>(view_slice_corners.size());

      float sphere_radius = 0.0F;
      for (const auto& corner : view_slice_corners) {
        sphere_radius
          = std::max(sphere_radius, glm::length(corner - view_slice_center));
      }

      const float base_half_extent
        = std::max(std::ceil(sphere_radius * 16.0F) * (1.0F / 16.0F), 1.0F);
      const float padded_half_extent_x = ComputePaddedHalfExtent(
        base_half_extent, directional_shadow_resolution_);
      const float padded_half_extent_y = ComputePaddedHalfExtent(
        base_half_extent, directional_shadow_resolution_);
      glm::vec3 slice_center_ls
        = glm::vec3(light_rotation * glm::vec4(slice_center_ws, 1.0F));

      float ortho_half_extent_x = padded_half_extent_x;
      float ortho_half_extent_y = padded_half_extent_y;
      glm::vec3 ortho_center_ls = slice_center_ls;
      glm::vec2 receiver_min_xy {};
      glm::vec2 receiver_max_xy {};
      if (AccumulateReceiverLightSpaceExtents(visible_receiver_bounds,
            view_matrix, light_rotation, prev_depth, end_depth, receiver_min_xy,
            receiver_max_xy)) {
        const glm::vec2 receiver_center_xy
          = (receiver_min_xy + receiver_max_xy) * 0.5F;
        const glm::vec2 receiver_half_extent = glm::max(
          (receiver_max_xy - receiver_min_xy) * 0.5F, glm::vec2(1.0F));
        ortho_center_ls.x = receiver_center_xy.x;
        ortho_center_ls.y = receiver_center_xy.y;
        ortho_half_extent_x = ComputePaddedHalfExtent(
          receiver_half_extent.x, directional_shadow_resolution_);
        ortho_half_extent_y = ComputePaddedHalfExtent(
          receiver_half_extent.y, directional_shadow_resolution_);
      }

      const glm::vec3 snapped_center_ls
        = SnapLightSpaceCenter(ortho_center_ls, ortho_half_extent_x,
          ortho_half_extent_y, directional_shadow_resolution_);
      const glm::vec3 snapped_center_ws
        = glm::vec3(inv_light_rotation * glm::vec4(snapped_center_ls, 1.0F));

      const float pullback_extent = std::max(
        std::max(ortho_half_extent_x, ortho_half_extent_y), sphere_radius);

      const glm::vec3 light_eye = snapped_center_ws
        + light_dir_to_light * (pullback_extent + kLightPullbackPadding);
      const glm::mat4 light_view
        = glm::lookAtRH(light_eye, snapped_center_ws, world_up);

      float max_depth
        = -(pullback_extent + kLightPullbackPadding) + sphere_radius;
      float min_depth
        = -(pullback_extent + kLightPullbackPadding) - sphere_radius;

      [[maybe_unused]] const bool has_shadow_caster_depths
        = TightenDepthRangeWithShadowCasters(shadow_caster_bounds, light_view,
          ortho_half_extent_x, ortho_half_extent_y, min_depth, max_depth);

      const float world_units_per_texel_x = (2.0F * ortho_half_extent_x)
        / std::max(1.0F, static_cast<float>(directional_shadow_resolution_));
      const float world_units_per_texel_y = (2.0F * ortho_half_extent_y)
        / std::max(1.0F, static_cast<float>(directional_shadow_resolution_));
      const float world_units_per_texel
        = std::max(world_units_per_texel_x, world_units_per_texel_y);
      const float left = -ortho_half_extent_x;
      const float right = ortho_half_extent_x;
      const float bottom = -ortho_half_extent_y;
      const float top = ortho_half_extent_y;
      const float depth_padding = std::max(kMinShadowDepthPadding,
        std::max(ortho_half_extent_x, ortho_half_extent_y) * 0.1F);
      const float near_plane = std::max(0.1F, -max_depth - depth_padding);
      const float far_plane
        = std::max(near_plane + 1.0F, -min_depth + depth_padding);
      const glm::mat4 light_proj
        = oxygen::MakeReversedZOrthographicProjectionRH_ZO(
          left, right, bottom, top, near_plane, far_plane);

      metadata.cascade_distances[cascade_index] = end_depth;
      metadata.cascade_world_texel_size[cascade_index] = world_units_per_texel;
      metadata.cascade_view_proj[cascade_index] = light_proj * light_view;

      engine::ViewConstants cascade_view_constants = view_constants;
      cascade_view_constants.SetViewMatrix(light_view)
        .SetProjectionMatrix(light_proj)
        .SetCameraPosition(light_eye);

      state.raster_jobs.push_back(RasterShadowJob {
        .shadow_instance_index = shadow_instance_index,
        .payload_index = payload_index,
        .domain
        = static_cast<std::uint32_t>(engine::ShadowDomain::kDirectional),
        .target_kind = RasterShadowTargetKind::kTexture2DArraySlice,
        .target_array_slice = resource_index + cascade_index,
        .view_constants = cascade_view_constants.GetSnapshot(),
      });

      state.receiver_analysis_jobs.push_back(
        ConventionalShadowReceiverAnalysisJob {
          .light_rotation_matrix = light_rotation,
          .full_rect_center_half_extent = { slice_center_ls.x,
            slice_center_ls.y, padded_half_extent_x, padded_half_extent_y },
          .legacy_rect_center_half_extent = { snapped_center_ls.x,
            snapped_center_ls.y, ortho_half_extent_x, ortho_half_extent_y },
          .split_and_full_depth_range
          = { cascade_begin_depth, end_depth, slice_center_ls.z - sphere_radius,
            slice_center_ls.z + sphere_radius },
          .shading_margins = { world_units_per_texel, candidate.bias,
            candidate.normal_bias, 0.0F },
          .target_array_slice = resource_index + cascade_index,
          .flags = kConventionalShadowReceiverAnalysisFlagValid,
        });

      prev_depth = end_depth;
    }

    state.directional_metadata.push_back(metadata);
  }
}

auto ConventionalShadowBackend::PublishShadowInstances(
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
      "ConventionalShadowBackend: failed to allocate shadow instance metadata "
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

auto ConventionalShadowBackend::PublishDirectionalMetadata(
  const std::span<const engine::DirectionalShadowMetadata> metadata)
  -> ShaderVisibleIndex
{
  if (metadata.empty()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto result = directional_shadow_metadata_buffer_.Allocate(
    static_cast<std::uint32_t>(metadata.size()));
  if (!result) {
    LOG_F(ERROR,
      "ConventionalShadowBackend: failed to allocate directional shadow "
      "metadata buffer: {}",
      result.error().message());
    return kInvalidShaderVisibleIndex;
  }

  const auto& allocation = *result;
  if (allocation.mapped_ptr != nullptr) {
    std::memcpy(allocation.mapped_ptr, metadata.data(),
      metadata.size() * sizeof(engine::DirectionalShadowMetadata));
  }
  return allocation.srv;
}

} // namespace oxygen::renderer::internal
