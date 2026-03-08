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
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Scene/Light/LightCommon.h>

namespace {

constexpr std::uint32_t kInvalidShadowResourceIndex = 0xFFFFFFFFU;
constexpr float kMinCascadeSpan = 0.1F;
constexpr float kLightPullbackPadding = 32.0F;
constexpr float kMinShadowDepthPadding = 8.0F;
constexpr float kDirectionalShadowKernelPaddingTexels = 3.0F;
constexpr float kDirectionalShadowSnapPaddingTexels = 1.0F;

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

[[nodiscard]] auto ViewSpaceDepth(
  const glm::mat4& view_matrix, const glm::vec3& point) -> float
{
  return std::max(0.0F, -(view_matrix * glm::vec4(point, 1.0F)).z);
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

  // Preserve the existing authored control direction: larger values emphasize
  // nearer cascades more strongly. The practical split lambda stays bounded to
  // [0, 1) while remaining monotonic as the authored exponent grows.
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

namespace oxygen::renderer {

ShadowManager::ShadowManager(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> provider,
  const observer_ptr<CoordinatorT> inline_transfers,
  const oxygen::ShadowQualityTier quality_tier)
  : gfx_(gfx)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
  , shadow_quality_tier_(quality_tier)
  , shadow_instance_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::ShadowInstanceMetadata)),
      inline_transfers_, "ShadowManager.ShadowInstanceMetadata")
  , directional_shadow_metadata_buffer_(gfx_, *staging_provider_,
      static_cast<std::uint32_t>(sizeof(engine::DirectionalShadowMetadata)),
      inline_transfers_, "ShadowManager.DirectionalShadowMetadata")
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "expecting valid staging provider");
  DCHECK_NOTNULL_F(inline_transfers_, "expecting valid transfer coordinator");
}

ShadowManager::~ShadowManager() { ReleaseDirectionalResources(); }

auto ShadowManager::OnFrameStart(RendererTag /*tag*/,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  shadow_instance_buffer_.OnFrameStart(sequence, slot);
  directional_shadow_metadata_buffer_.OnFrameStart(sequence, slot);
  published_views_.clear();
}

auto ShadowManager::PublishForView(const ViewId view_id,
  const engine::ViewConstants& view_constants, const LightManager& lights,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const SyntheticSunShadowInput* synthetic_sun_shadow) -> PublishedViewData
{
  if (const auto* cached = TryGetPublishedViewData(view_id)) {
    return *cached;
  }

  PublishedViewState state {};
  std::vector<engine::DirectionalShadowCandidate> candidates_storage;
  const auto light_candidates = lights.GetDirectionalShadowCandidates();
  candidates_storage.assign(light_candidates.begin(), light_candidates.end());
  if (synthetic_sun_shadow != nullptr && synthetic_sun_shadow->enabled) {
    candidates_storage.push_back(engine::DirectionalShadowCandidate {
      .light_index = 0xFFFFFFFFU,
      .light_flags
      = static_cast<std::uint32_t>(engine::DirectionalLightFlags::kAffectsWorld
        | engine::DirectionalLightFlags::kCastsShadows
        | engine::DirectionalLightFlags::kSunLight
        | engine::DirectionalLightFlags::kEnvironmentContribution),
      .mobility = static_cast<std::uint32_t>(scene::LightMobility::kRealtime),
      .resolution_hint = synthetic_sun_shadow->resolution_hint,
      .direction_ws = synthetic_sun_shadow->direction_ws,
      .bias = synthetic_sun_shadow->bias,
      .normal_bias = synthetic_sun_shadow->normal_bias,
      .cascade_count = synthetic_sun_shadow->cascade_count,
      .distribution_exponent = synthetic_sun_shadow->distribution_exponent,
      .cascade_distances = synthetic_sun_shadow->cascade_distances,
    });
  }
  const auto candidates = std::span<const engine::DirectionalShadowCandidate>(
    candidates_storage.data(), candidates_storage.size());
  if (candidates.empty()) {
    LOG_F(INFO,
      "ShadowManager: view {} has no eligible directional shadow candidates",
      view_id.get());
  }
  const auto resource_config = BuildDirectionalResourceConfig(candidates);
  EnsureDirectionalResources(resource_config);
  BuildDirectionalViewState(
    view_id, view_constants, candidates, shadow_caster_bounds, state);

  if (!candidates.empty() && state.shadow_instances.empty()) {
    LOG_F(WARNING,
      "ShadowManager: view {} had {} directional shadow candidate(s), but no "
      "shadow products were published",
      view_id.get(), candidates.size());
  }

  state.published.shadow_instance_metadata_srv
    = PublishShadowInstances(state.shadow_instances);
  state.published.directional_shadow_metadata_srv
    = PublishDirectionalMetadata(state.directional_metadata);
  state.published.directional_shadow_texture_srv
    = directional_shadow_texture_srv_;
  state.published.shadow_instances = state.shadow_instances;
  state.published.directional_metadata = state.directional_metadata;
  state.published.directional_view_constants = state.directional_view_constants;
  for (std::size_t i = 0; i < state.shadow_instances.size(); ++i) {
    const auto& instance = state.shadow_instances[i];
    if ((instance.flags
          & static_cast<std::uint32_t>(engine::ShadowProductFlags::kSunLight))
      != 0U) {
      state.published.sun_shadow_index = static_cast<std::uint32_t>(i);
      break;
    }
  }

  const auto [it, inserted]
    = published_views_.emplace(view_id, std::move(state));
  DCHECK_F(inserted, "ShadowManager published view state unexpectedly existed");
  return it->second.published;
}

auto ShadowManager::SetPublishedViewFrameBindingsSlot(const ViewId view_id,
  const engine::BindlessViewFrameBindingsSlot slot) -> void
{
  const auto it = published_views_.find(view_id);
  if (it == published_views_.end()) {
    return;
  }

  for (auto& snapshot : it->second.directional_view_constants) {
    snapshot.view_frame_bindings_bslot = slot;
  }
}

auto ShadowManager::TryGetPublishedViewData(const ViewId view_id) const noexcept
  -> const PublishedViewData*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.published : nullptr;
}

auto ShadowManager::GetDirectionalShadowTexture() const noexcept
  -> const std::shared_ptr<graphics::Texture>&
{
  return directional_shadow_texture_;
}

auto ShadowManager::BuildDirectionalResourceConfig(
  const std::span<const engine::DirectionalShadowCandidate> candidates) const
  -> DirectionalShadowResourceConfig
{
  DirectionalShadowResourceConfig config {};
  for (const auto& candidate : candidates) {
    config.required_layers += std::max(
      1U, std::min(candidate.cascade_count, oxygen::scene::kMaxShadowCascades));
    config.resolution = std::max(config.resolution,
      ApplyDirectionalShadowQualityTier(
        ShadowResolutionFromHint(candidate.resolution_hint),
        shadow_quality_tier_, candidates.size()));
  }
  return config;
}

auto ShadowManager::EnsureDirectionalResources(
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

  ReleaseDirectionalResources();

  directional_shadow_capacity_layers_ = config.required_layers;
  directional_shadow_resolution_ = config.resolution;

  graphics::TextureDesc desc {};
  desc.width = directional_shadow_resolution_;
  desc.height = directional_shadow_resolution_;
  desc.array_size = directional_shadow_capacity_layers_;
  desc.mip_levels = 1U;
  desc.format = oxygen::Format::kDepth32;
  desc.texture_type = oxygen::TextureType::kTexture2DArray;
  desc.is_render_target = true;
  desc.is_shader_resource = true;
  desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kCommon;
  desc.debug_name = "DirectionalShadowDepthArray";

  directional_shadow_texture_ = gfx_->CreateTexture(desc);
  if (!directional_shadow_texture_) {
    directional_shadow_capacity_layers_ = 0U;
    directional_shadow_resolution_ = 0U;
    throw std::runtime_error(
      "ShadowManager: failed to create directional shadow texture");
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();
  registry.Register(directional_shadow_texture_);

  auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    directional_shadow_texture_.reset();
    directional_shadow_capacity_layers_ = 0U;
    directional_shadow_resolution_ = 0U;
    throw std::runtime_error(
      "ShadowManager: failed to allocate directional shadow SRV descriptor");
  }

  const graphics::TextureViewDescription srv_desc {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = oxygen::Format::kDepth32,
    .dimension = oxygen::TextureType::kTexture2DArray,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = 0U,
      .num_array_slices = directional_shadow_capacity_layers_,
    },
    .is_read_only_dsv = false,
  };

  directional_shadow_texture_srv_ = allocator.GetShaderVisibleIndex(handle);
  directional_shadow_texture_srv_view_ = registry.RegisterView(
    *directional_shadow_texture_, std::move(handle), srv_desc);

  LOG_F(INFO,
    "ShadowManager: created directional shadow texture {}x{} layers={} srv={}",
    directional_shadow_resolution_, directional_shadow_resolution_,
    directional_shadow_capacity_layers_, directional_shadow_texture_srv_.get());
}

auto ShadowManager::ReleaseDirectionalResources() -> void
{
  if (!gfx_) {
    directional_shadow_texture_.reset();
    directional_shadow_texture_srv_view_ = {};
    directional_shadow_texture_srv_ = kInvalidShaderVisibleIndex;
    directional_shadow_resolution_ = 0U;
    directional_shadow_capacity_layers_ = 0U;
    return;
  }

  if (directional_shadow_texture_
    && directional_shadow_texture_srv_view_->IsValid()) {
    gfx_->GetResourceRegistry().UnRegisterView(
      *directional_shadow_texture_, directional_shadow_texture_srv_view_);
  }

  directional_shadow_texture_.reset();
  directional_shadow_texture_srv_view_ = {};
  directional_shadow_texture_srv_ = kInvalidShaderVisibleIndex;
  directional_shadow_resolution_ = 0U;
  directional_shadow_capacity_layers_ = 0U;
}

auto ShadowManager::BuildDirectionalViewState(const ViewId view_id,
  const engine::ViewConstants& view_constants,
  const std::span<const engine::DirectionalShadowCandidate> candidates,
  const std::span<const glm::vec4> shadow_caster_bounds,
  PublishedViewState& state) -> void
{
  if (candidates.empty() || !directional_shadow_texture_) {
    return;
  }

  const auto camera_view_constants = view_constants.GetSnapshot();
  const glm::mat4 view_matrix = camera_view_constants.view_matrix;
  const glm::mat4 projection_matrix = camera_view_constants.projection_matrix;
  const glm::mat4 inv_view_proj = glm::inverse(projection_matrix * view_matrix);

  std::array<glm::vec3, 4> near_corners {};
  std::array<glm::vec3, 4> far_corners {};
  constexpr std::array<glm::vec2, 4> clip_corners {
    glm::vec2(-1.0F, -1.0F),
    glm::vec2(1.0F, -1.0F),
    glm::vec2(1.0F, 1.0F),
    glm::vec2(-1.0F, 1.0F),
  };
  for (std::size_t i = 0; i < clip_corners.size(); ++i) {
    near_corners[i]
      = TransformPoint(inv_view_proj, glm::vec3(clip_corners[i], 0.0F));
    far_corners[i]
      = TransformPoint(inv_view_proj, glm::vec3(clip_corners[i], 1.0F));
  }

  float near_depth = 0.0F;
  float far_depth = 0.0F;
  for (std::size_t i = 0; i < near_corners.size(); ++i) {
    near_depth += ViewSpaceDepth(view_matrix, near_corners[i]);
    far_depth += ViewSpaceDepth(view_matrix, far_corners[i]);
  }
  near_depth /= static_cast<float>(near_corners.size());
  far_depth /= static_cast<float>(far_corners.size());
  if (far_depth <= near_depth + kMinCascadeSpan) {
    LOG_F(WARNING,
      "ShadowManager: invalid camera depth span for view {} "
      "(near_depth={} far_depth={}); skipping directional shadows",
      view_id.get(), near_depth, far_depth);
    return;
  }

  state.shadow_instances.reserve(candidates.size());
  state.directional_metadata.reserve(candidates.size());

  std::uint32_t next_resource_layer = 0U;
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
      const float end_depth = ResolveCascadeEndDepth(
        candidate, cascade_index, prev_depth, near_depth, far_depth);
      const float depth_range
        = std::max(far_depth - near_depth, kMinCascadeSpan);
      const float t0
        = std::clamp((prev_depth - near_depth) / depth_range, 0.0F, 1.0F);
      const float t1
        = std::clamp((end_depth - near_depth) / depth_range, 0.0F, 1.0F);

      std::array<glm::vec3, 8> slice_corners {};
      for (std::size_t corner = 0; corner < near_corners.size(); ++corner) {
        slice_corners[corner]
          = glm::mix(near_corners[corner], far_corners[corner], t0);
        slice_corners[corner + near_corners.size()]
          = glm::mix(near_corners[corner], far_corners[corner], t1);
      }

      float min_x = (std::numeric_limits<float>::max)();
      float max_x = (std::numeric_limits<float>::lowest)();
      float min_y = (std::numeric_limits<float>::max)();
      float max_y = (std::numeric_limits<float>::lowest)();
      float min_z_rot = (std::numeric_limits<float>::max)();
      float max_z_rot = (std::numeric_limits<float>::lowest)();
      for (const auto& corner : slice_corners) {
        const glm::vec3 light_space_rot
          = glm::vec3(light_rotation * glm::vec4(corner, 1.0F));
        min_x = std::min(min_x, light_space_rot.x);
        max_x = std::max(max_x, light_space_rot.x);
        min_y = std::min(min_y, light_space_rot.y);
        max_y = std::max(max_y, light_space_rot.y);
        min_z_rot = std::min(min_z_rot, light_space_rot.z);
        max_z_rot = std::max(max_z_rot, light_space_rot.z);
      }

      const float base_half_extent_x = std::max((max_x - min_x) * 0.5F, 1.0F);
      const float base_half_extent_y = std::max((max_y - min_y) * 0.5F, 1.0F);
      const float padded_half_extent_x = ComputePaddedHalfExtent(
        base_half_extent_x, directional_shadow_resolution_);
      const float padded_half_extent_y = ComputePaddedHalfExtent(
        base_half_extent_y, directional_shadow_resolution_);
      const glm::vec3 slice_center_ls {
        (min_x + max_x) * 0.5F,
        (min_y + max_y) * 0.5F,
        (min_z_rot + max_z_rot) * 0.5F,
      };
      const glm::vec3 snapped_center_ls
        = SnapLightSpaceCenter(slice_center_ls, padded_half_extent_x,
          padded_half_extent_y, directional_shadow_resolution_);
      const glm::vec3 snapped_center_ws
        = glm::vec3(inv_light_rotation * glm::vec4(snapped_center_ls, 1.0F));

      const float pullback_extent
        = std::max(std::max(padded_half_extent_x, padded_half_extent_y),
          (max_z_rot - min_z_rot) * 0.5F);

      // Place the orthographic shadow camera toward the light source, looking
      // back at the texel-snapped cascade center. XY coverage is now a tighter
      // texel-snapped rectangle rather than the earlier square sphere fit.
      const glm::vec3 light_eye = snapped_center_ws
        + light_dir_to_light * (pullback_extent + kLightPullbackPadding);
      const glm::mat4 light_view
        = glm::lookAtRH(light_eye, snapped_center_ws, world_up);

      float min_depth = (std::numeric_limits<float>::max)();
      float max_depth = (std::numeric_limits<float>::lowest)();
      for (const auto& corner : slice_corners) {
        const glm::vec3 light_space
          = glm::vec3(light_view * glm::vec4(corner, 1.0F));
        min_depth = std::min(min_depth, light_space.z);
        max_depth = std::max(max_depth, light_space.z);
      }

      const float ortho_half_extent_x = padded_half_extent_x;
      const float ortho_half_extent_y = padded_half_extent_y;
      const bool has_shadow_caster_depths
        = TightenDepthRangeWithShadowCasters(shadow_caster_bounds, light_view,
          ortho_half_extent_x, ortho_half_extent_y, min_depth, max_depth);
      if (!has_shadow_caster_depths) {
        // Keep the receiver slice bounds as the safety fallback until a later
        // invalidation/cache phase can carry richer caster coverage metadata.
      }
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
        = glm::orthoRH_ZO(left, right, bottom, top, near_plane, far_plane);

      metadata.cascade_distances[cascade_index] = end_depth;
      metadata.cascade_world_texel_size[cascade_index] = world_units_per_texel;
      metadata.cascade_view_proj[cascade_index] = light_proj * light_view;

      engine::ViewConstants cascade_view_constants = view_constants;
      cascade_view_constants.SetViewMatrix(light_view)
        .SetProjectionMatrix(light_proj)
        .SetCameraPosition(light_eye);
      state.directional_view_constants.push_back(
        cascade_view_constants.GetSnapshot());

      prev_depth = end_depth;
    }

    state.directional_metadata.push_back(metadata);
  }
}

auto ShadowManager::PublishShadowInstances(
  const std::span<const engine::ShadowInstanceMetadata> instances)
  -> ShaderVisibleIndex
{
  if (instances.empty()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto result = shadow_instance_buffer_.Allocate(
    static_cast<std::uint32_t>(instances.size()));
  if (!result) {
    LOG_F(ERROR, "Failed to allocate shadow instance metadata buffer: {}",
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

auto ShadowManager::PublishDirectionalMetadata(
  const std::span<const engine::DirectionalShadowMetadata> metadata)
  -> ShaderVisibleIndex
{
  if (metadata.empty()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto result = directional_shadow_metadata_buffer_.Allocate(
    static_cast<std::uint32_t>(metadata.size()));
  if (!result) {
    LOG_F(ERROR, "Failed to allocate directional shadow metadata buffer: {}",
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

} // namespace oxygen::renderer
