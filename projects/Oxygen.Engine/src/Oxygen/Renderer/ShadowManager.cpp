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
constexpr float kShadowDepthPadding = 64.0F;
constexpr float kLightPullbackPadding = 32.0F;

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
  const float distributed_split = std::pow(
    normalized_split, std::max(0.001F, candidate.distribution_exponent));
  const float generated_end
    = near_depth + (far_depth - near_depth) * distributed_split;
  return std::max(
    prev_depth + kMinCascadeSpan, std::min(generated_end, far_depth));
}

} // namespace

namespace oxygen::renderer {

ShadowManager::ShadowManager(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> provider,
  const observer_ptr<CoordinatorT> inline_transfers)
  : gfx_(gfx)
  , staging_provider_(provider)
  , inline_transfers_(inline_transfers)
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
  BuildDirectionalViewState(view_id, view_constants, candidates, state);

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
    config.resolution = std::max(
      config.resolution, ShadowResolutionFromHint(candidate.resolution_hint));
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

      glm::vec3 slice_center { 0.0F };
      for (const auto& corner : slice_corners) {
        slice_center += corner;
      }
      slice_center /= static_cast<float>(slice_corners.size());

      float radius = 0.0F;
      for (const auto& corner : slice_corners) {
        radius = std::max(radius, glm::length(corner - slice_center));
      }

      // Place the orthographic shadow camera toward the light source, looking
      // back at the cascade slice center. Using the opposite sign here flips
      // the light-space depth direction and projects shadows onto caster-facing
      // surfaces instead of downstream receivers.
      const glm::vec3 light_eye
        = slice_center + light_dir_to_light * (radius + kLightPullbackPadding);
      const glm::mat4 light_view
        = glm::lookAtRH(light_eye, slice_center, world_up);

      glm::vec3 min_corner { (std::numeric_limits<float>::max)() };
      glm::vec3 max_corner { (std::numeric_limits<float>::lowest)() };
      for (const auto& corner : slice_corners) {
        const glm::vec3 light_space
          = glm::vec3(light_view * glm::vec4(corner, 1.0F));
        min_corner = glm::min(min_corner, light_space);
        max_corner = glm::max(max_corner, light_space);
      }

      const float left = min_corner.x;
      const float right = std::max(max_corner.x, left + 1.0F);
      const float bottom = min_corner.y;
      const float top = std::max(max_corner.y, bottom + 1.0F);
      const float near_plane
        = std::max(0.1F, -max_corner.z - kShadowDepthPadding);
      const float far_plane
        = std::max(near_plane + 1.0F, -min_corner.z + kShadowDepthPadding);
      const glm::mat4 light_proj
        = glm::orthoRH_ZO(left, right, bottom, top, near_plane, far_plane);

      metadata.cascade_distances[cascade_index] = end_depth;
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
