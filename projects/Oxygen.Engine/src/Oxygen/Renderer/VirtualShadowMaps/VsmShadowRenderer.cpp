//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <tuple>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmHzbUpdaterPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmInvalidationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageFlagPropagationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageInitializationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageManagementPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageRequestGeneratorPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmStaticDynamicMergePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/VsmFrameBindings.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionRouting.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>

namespace oxygen::renderer::vsm {

namespace {

  constexpr float kMinCascadeSpan = 0.1F;
  constexpr float kLightPullbackPadding = 32.0F;
  constexpr float kMinShadowDepthPadding = 8.0F;
  constexpr float kDirectionalShadowKernelPaddingTexels = 3.0F;
  constexpr float kDirectionalShadowSnapPaddingTexels = 1.0F;
  constexpr std::uint32_t kVsmShadowPoolPageSizeTexels = 128U;
  constexpr auto kPageRequestReadbackWaitBudget
    = std::chrono::milliseconds { 250 };
  constexpr auto kPageRequestReadbackPollInterval
    = std::chrono::milliseconds { 1 };

  [[nodiscard]] auto NormalizeOrFallback(
    const glm::vec3& value, const glm::vec3& fallback) -> glm::vec3
  {
    const float len_sq = glm::dot(value, value);
    if (len_sq <= oxygen::math::EpsilonDirection) {
      return fallback;
    }
    return glm::normalize(value);
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

    auto snapped = center_ls;
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

  [[nodiscard]] auto ResolveCascadeEndDepth(
    const engine::DirectionalShadowCandidate& candidate,
    const std::uint32_t cascade_index, const float prev_depth,
    const float near_depth, const float far_depth) -> float
  {
    const auto cascade_count = std::max(
      1U, std::min(candidate.cascade_count, oxygen::scene::kMaxShadowCascades));
    if (cascade_index + 1U >= cascade_count) {
      return std::max(prev_depth + kMinCascadeSpan, far_depth);
    }

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
      * std::pow(
        stabilized_far_depth / stabilized_near_depth, normalized_split);
    const float practical_lambda = MapDistributionExponentToPracticalLambda(
      candidate.distribution_exponent);
    const float generated_end
      = glm::mix(linear_split, logarithmic_split, practical_lambda);
    return std::max(
      prev_depth + kMinCascadeSpan, std::min(generated_end, far_depth));
  }

  [[nodiscard]] auto FindSliceIndex(const VsmPhysicalPoolSnapshot& pool,
    const VsmPhysicalPoolSliceRole role) -> std::optional<std::uint32_t>
  {
    for (std::uint32_t i = 0U; i < pool.slice_roles.size(); ++i) {
      if (pool.slice_roles[i] == role) {
        return i;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto BuildStaticMergeCandidateLogicalPages(
    const std::span<const VsmShadowRasterPageJob> prepared_pages,
    const VsmPhysicalPoolSnapshot& pool) -> std::vector<std::uint32_t>
  {
    const auto static_slice_index
      = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kStaticDepth);
    if (!static_slice_index.has_value() || pool.tiles_per_axis == 0U) {
      return {};
    }

    auto candidates = std::vector<std::uint32_t> {};
    candidates.reserve(prepared_pages.size());
    for (const auto& job : prepared_pages) {
      if (!job.static_only || job.physical_coord.slice != *static_slice_index) {
        continue;
      }
      candidates.push_back(job.physical_coord.tile_y * pool.tiles_per_axis
        + job.physical_coord.tile_x);
    }

    std::ranges::sort(candidates);
    const auto unique_end = std::ranges::unique(candidates).begin();
    candidates.erase(unique_end, candidates.end());
    return candidates;
  }

  auto TightenDepthRangeWithShadowCasters(
    const std::span<const glm::vec4> shadow_caster_bounds,
    const glm::mat4& light_view, const float ortho_half_extent_x,
    const float ortho_half_extent_y, float& min_depth, float& max_depth) -> bool
  {
    auto tightened = false;
    for (const auto& sphere : shadow_caster_bounds) {
      const float radius = sphere.w;
      if (radius <= 0.0F) {
        continue;
      }

      const auto center_ls
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
    const auto preferred_candidate_resolution = [&]() -> std::uint32_t {
      switch (quality_tier) {
      case oxygen::ShadowQualityTier::kLow:
        return 1024U;
      case oxygen::ShadowQualityTier::kMedium:
        return directional_candidate_count > 1U ? 1024U : 2048U;
      case oxygen::ShadowQualityTier::kHigh:
        return directional_candidate_count > 1U ? 2048U : 3072U;
      case oxygen::ShadowQualityTier::kUltra:
        return directional_candidate_count > 1U ? 3072U : 4096U;
      }
      return 2048U;
    }();

    return std::max(authored_resolution, preferred_candidate_resolution);
  }

  [[nodiscard]] auto PagesPerAxisForVirtualResolution(
    const std::uint32_t virtual_resolution) -> std::uint32_t
  {
    return std::max(1U,
      (std::max(virtual_resolution, 1U) + kVsmShadowPoolPageSizeTexels - 1U)
        / kVsmShadowPoolPageSizeTexels);
  }

  auto BuildPrimitiveFlags(
    const engine::sceneprep::RenderItemData& item) noexcept -> std::uint32_t
  {
    auto primitive_flags = std::uint32_t { 0U };
    if (item.static_shadow_caster) {
      primitive_flags |= static_cast<std::uint32_t>(
        engine::DrawPrimitiveFlagBits::kStaticShadowCaster);
    }
    if (item.main_view_visible) {
      primitive_flags |= static_cast<std::uint32_t>(
        engine::DrawPrimitiveFlagBits::kMainViewVisible);
    }
    return primitive_flags;
  }

  auto MakePrimitiveIdentity(
    const engine::sceneprep::RenderItemData& item) noexcept
    -> VsmPrimitiveIdentity
  {
    return VsmPrimitiveIdentity {
      .transform_index = item.transform_handle.get(),
      .transform_generation = item.transform_handle.GenerationValue().get(),
      .submesh_index = item.submesh_index,
      .primitive_flags = BuildPrimitiveFlags(item),
    };
  }

  auto BuildScenePrimitiveHistory(
    const std::span<const engine::sceneprep::RenderItemData> rendered_items)
    -> std::vector<VsmScenePrimitiveHistoryRecord>
  {
    auto history = std::vector<VsmScenePrimitiveHistoryRecord> {};
    history.reserve(rendered_items.size());
    for (const auto& item : rendered_items) {
      if (!item.cast_shadows || !item.transform_handle.IsValid()
        || !item.node_handle.IsValid()) {
        continue;
      }

      history.push_back(VsmScenePrimitiveHistoryRecord {
        .node_handle = item.node_handle,
        .primitive = MakePrimitiveIdentity(item),
        .world_bounding_sphere = item.world_bounding_sphere,
        .static_shadow_caster = item.static_shadow_caster,
      });
    }
    return history;
  }

  auto BuildShaderPageTableEntries(
    const std::span<const VsmPageTableEntry> page_table)
    -> std::vector<VsmShaderPageTableEntry>
  {
    auto shader_entries = std::vector<VsmShaderPageTableEntry> {};
    shader_entries.reserve(page_table.size());
    for (const auto& entry : page_table) {
      shader_entries.push_back(entry.is_mapped
          ? MakeMappedShaderPageTableEntry(entry.physical_page)
          : MakeUnmappedShaderPageTableEntry());
    }
    return shader_entries;
  }

  auto BuildLightRemapKey(
    const char* prefix, const scene::NodeHandle node_handle) -> std::string
  {
    return std::string(prefix) + ":"
      + std::string(nostd::to_string(node_handle.GetSceneId())) + ":"
      + std::string(nostd::to_string(node_handle.Index())) + ":"
      + std::string(nostd::to_string(node_handle.Generation()));
  }

  auto BuildDirectionalLightRemapKey(const scene::NodeHandle node_handle)
    -> std::string
  {
    return BuildLightRemapKey("directional", node_handle);
  }

  auto BuildLocalLightRemapKey(const scene::NodeHandle node_handle)
    -> std::string
  {
    return BuildLightRemapKey("local", node_handle);
  }

  auto PagesPerAxisForQualityTier(const oxygen::ShadowQualityTier quality_tier)
    -> std::uint32_t
  {
    switch (quality_tier) {
    case oxygen::ShadowQualityTier::kLow:
      return 4U;
    case oxygen::ShadowQualityTier::kMedium:
      return 8U;
    case oxygen::ShadowQualityTier::kHigh:
      return 16U;
    case oxygen::ShadowQualityTier::kUltra:
      return 32U;
    }

    return 16U;
  }

  auto PagedLocalLevelCountForQualityTier(
    const oxygen::ShadowQualityTier quality_tier) -> std::uint32_t
  {
    switch (quality_tier) {
    case oxygen::ShadowQualityTier::kLow:
      return 1U;
    case oxygen::ShadowQualityTier::kMedium:
      return 2U;
    case oxygen::ShadowQualityTier::kHigh:
      return 3U;
    case oxygen::ShadowQualityTier::kUltra:
      return 4U;
    }

    return 3U;
  }

  struct DirectionalFrustumContext {
    std::array<glm::vec3, 4> near_corners_ws {};
    std::array<glm::vec3, 4> far_corners_ws {};
    std::array<glm::vec3, 4> near_corners_vs {};
    std::array<glm::vec3, 4> far_corners_vs {};
    float near_depth { 0.0F };
    float far_depth { 0.0F };
  };

  struct DirectionalLightProjectionContext {
    glm::vec3 light_dir_to_surface { 0.0F, -1.0F, 0.0F };
    glm::vec3 light_dir_to_light { 0.0F, 1.0F, 0.0F };
    glm::vec3 world_up { 0.0F, 0.0F, 1.0F };
    glm::mat4 light_rotation { 1.0F };
    glm::mat4 inv_light_rotation { 1.0F };
  };

  struct DirectionalClipLevelState {
    glm::mat4 light_view { 1.0F };
    glm::mat4 light_projection { 1.0F };
    glm::vec4 view_origin_ws_pad { 0.0F };
    glm::ivec2 page_grid_origin { 0, 0 };
    float page_world_size { 1.0F };
    float near_depth { 0.01F };
    float far_depth { 1.0F };
    float resolved_end_depth { 1.0F };
  };

  [[nodiscard]] auto BuildDirectionalFrustumContext(
    const VsmShadowRenderer::PreparedViewState& prepared_view)
    -> std::optional<DirectionalFrustumContext>
  {
    const auto view_matrix = prepared_view.view_constants_snapshot.view_matrix;
    const auto projection_matrix
      = prepared_view.view_constants_snapshot.projection_matrix;
    const auto inv_view_proj = glm::inverse(projection_matrix * view_matrix);
    const auto inv_proj = glm::inverse(projection_matrix);

    constexpr std::array<glm::vec2, 4> clip_corners {
      glm::vec2(-1.0F, -1.0F),
      glm::vec2(1.0F, -1.0F),
      glm::vec2(1.0F, 1.0F),
      glm::vec2(-1.0F, 1.0F),
    };

    auto context = DirectionalFrustumContext {};
    for (std::size_t i = 0; i < clip_corners.size(); ++i) {
      context.near_corners_ws[i]
        = TransformPoint(inv_view_proj, glm::vec3(clip_corners[i], 0.0F));
      context.far_corners_ws[i]
        = TransformPoint(inv_view_proj, glm::vec3(clip_corners[i], 1.0F));
      context.near_corners_vs[i]
        = TransformPoint(inv_proj, glm::vec3(clip_corners[i], 0.0F));
      context.far_corners_vs[i]
        = TransformPoint(inv_proj, glm::vec3(clip_corners[i], 1.0F));
    }

    for (std::size_t i = 0; i < context.near_corners_vs.size(); ++i) {
      context.near_depth += std::max(0.0F, -context.near_corners_vs[i].z);
      context.far_depth += std::max(0.0F, -context.far_corners_vs[i].z);
    }
    context.near_depth /= static_cast<float>(context.near_corners_vs.size());
    context.far_depth /= static_cast<float>(context.far_corners_vs.size());

    if (context.far_depth <= context.near_depth + kMinCascadeSpan) {
      return std::nullopt;
    }

    return context;
  }

  [[nodiscard]] auto BuildDirectionalLightProjectionContext(
    const VsmShadowRenderer::PreparedViewState& prepared_view,
    const engine::DirectionalShadowCandidate& candidate,
    const oxygen::ShadowQualityTier quality_tier)
    -> DirectionalLightProjectionContext
  {
    const auto light_dir_to_surface = NormalizeOrFallback(
      candidate.direction_ws, glm::vec3(0.0F, -1.0F, 0.0F));
    const auto light_dir_to_light = -light_dir_to_surface;
    const auto world_up
      = std::abs(glm::dot(light_dir_to_light, glm::vec3(0.0F, 0.0F, 1.0F)))
        > 0.95F
      ? glm::vec3(1.0F, 0.0F, 0.0F)
      : NormalizeOrFallback(candidate.basis_up_ws, glm::vec3(0.0F, 0.0F, 1.0F));
    const auto light_rotation
      = BuildDirectionalLightRotation(light_dir_to_surface, world_up);

    return DirectionalLightProjectionContext {
      .light_dir_to_surface = light_dir_to_surface,
      .light_dir_to_light = light_dir_to_light,
      .world_up = world_up,
      .light_rotation = light_rotation,
      .inv_light_rotation = glm::inverse(light_rotation),
    };
  }

  [[nodiscard]] auto ComputeDirectionalClipLevelState(
    const VsmShadowRenderer::PreparedViewState& prepared_view,
    const DirectionalFrustumContext& frustum,
    const DirectionalLightProjectionContext& light_context,
    const engine::DirectionalShadowCandidate& candidate,
    const std::uint32_t clip_index, const std::uint32_t pages_per_axis,
    const std::uint32_t virtual_resolution, const float prev_depth)
    -> DirectionalClipLevelState
  {
    const auto end_depth = ResolveCascadeEndDepth(
      candidate, clip_index, prev_depth, frustum.near_depth, frustum.far_depth);
    const auto depth_range
      = std::max(frustum.far_depth - frustum.near_depth, kMinCascadeSpan);
    const auto t0
      = std::clamp((prev_depth - frustum.near_depth) / depth_range, 0.0F, 1.0F);
    const auto t1
      = std::clamp((end_depth - frustum.near_depth) / depth_range, 0.0F, 1.0F);

    std::array<glm::vec3, 8> slice_corners_ws {};
    std::array<glm::vec3, 8> slice_corners_vs {};
    for (std::size_t corner = 0; corner < frustum.near_corners_ws.size();
      ++corner) {
      slice_corners_ws[corner] = glm::mix(
        frustum.near_corners_ws[corner], frustum.far_corners_ws[corner], t0);
      slice_corners_ws[corner + frustum.near_corners_ws.size()] = glm::mix(
        frustum.near_corners_ws[corner], frustum.far_corners_ws[corner], t1);
      slice_corners_vs[corner] = glm::mix(
        frustum.near_corners_vs[corner], frustum.far_corners_vs[corner], t0);
      slice_corners_vs[corner + frustum.near_corners_vs.size()] = glm::mix(
        frustum.near_corners_vs[corner], frustum.far_corners_vs[corner], t1);
    }

    auto slice_center_ws = glm::vec3(0.0F);
    auto view_slice_center = glm::vec3(0.0F);
    for (std::size_t i = 0; i < slice_corners_ws.size(); ++i) {
      slice_center_ws += slice_corners_ws[i];
      view_slice_center += slice_corners_vs[i];
    }
    slice_center_ws /= static_cast<float>(slice_corners_ws.size());
    view_slice_center /= static_cast<float>(slice_corners_vs.size());

    auto sphere_radius = 0.0F;
    for (const auto& corner : slice_corners_vs) {
      sphere_radius
        = std::max(sphere_radius, glm::length(corner - view_slice_center));
    }

    const auto base_half_extent
      = std::max(std::ceil(sphere_radius * 16.0F) * (1.0F / 16.0F), 1.0F);
    const auto padded_half_extent_x
      = ComputePaddedHalfExtent(base_half_extent, virtual_resolution);
    const auto padded_half_extent_y
      = ComputePaddedHalfExtent(base_half_extent, virtual_resolution);
    const auto slice_center_ls = glm::vec3(
      light_context.light_rotation * glm::vec4(slice_center_ws, 1.0F));
    const auto snapped_center_ls = SnapLightSpaceCenter(slice_center_ls,
      padded_half_extent_x, padded_half_extent_y, virtual_resolution);
    const auto snapped_center_ws = glm::vec3(
      light_context.inv_light_rotation * glm::vec4(snapped_center_ls, 1.0F));

    const auto page_world_size_x
      = (2.0F * padded_half_extent_x) / static_cast<float>(pages_per_axis);
    const auto page_world_size_y
      = (2.0F * padded_half_extent_y) / static_cast<float>(pages_per_axis);
    if (std::abs(page_world_size_x - page_world_size_y) > 1.0e-4F) {
      DLOG_F(2,
        "VsmShadowRenderer: directional clip level {} has non-uniform page "
        "world size x={} y={}; using max for clipmap metadata",
        clip_index, page_world_size_x, page_world_size_y);
    }
    const auto clip_min_corner_ls = glm::vec2(snapped_center_ls)
      - glm::vec2(padded_half_extent_x, padded_half_extent_y);
    const auto page_grid_origin = glm::ivec2 {
      static_cast<std::int32_t>(
        std::floor(clip_min_corner_ls.x / page_world_size_x)),
      static_cast<std::int32_t>(
        std::floor(clip_min_corner_ls.y / page_world_size_y)),
    };

    const auto pullback_extent = std::max(
      std::max(padded_half_extent_x, padded_half_extent_y), sphere_radius);
    const auto light_eye = snapped_center_ws
      + light_context.light_dir_to_light
        * (pullback_extent + kLightPullbackPadding);
    const auto light_view
      = glm::lookAtRH(light_eye, snapped_center_ws, light_context.world_up);

    auto max_depth = -(pullback_extent + kLightPullbackPadding) + sphere_radius;
    auto min_depth = -(pullback_extent + kLightPullbackPadding) - sphere_radius;
    static_cast<void>(TightenDepthRangeWithShadowCasters(
      prepared_view.shadow_caster_bounds, light_view, padded_half_extent_x,
      padded_half_extent_y, min_depth, max_depth));

    const auto depth_padding = std::max(kMinShadowDepthPadding,
      std::max(padded_half_extent_x, padded_half_extent_y) * 0.1F);
    const auto near_plane = std::max(0.1F, -max_depth - depth_padding);
    const auto far_plane
      = std::max(near_plane + 1.0F, -min_depth + depth_padding);
    const auto light_projection
      = glm::orthoRH_ZO(-padded_half_extent_x, padded_half_extent_x,
        -padded_half_extent_y, padded_half_extent_y, near_plane, far_plane);

    return DirectionalClipLevelState {
      .light_view = light_view,
      .light_projection = light_projection,
      .view_origin_ws_pad = glm::vec4(light_eye, 0.0F),
      .page_grid_origin = page_grid_origin,
      .page_world_size = std::max(page_world_size_x, page_world_size_y),
      .near_depth = std::max(prev_depth, 0.01F),
      .far_depth = end_depth,
      .resolved_end_depth = end_depth,
    };
  }

  auto ShouldUseSinglePageLocalLayout(const engine::PositionalLightData& light,
    const VsmShadowRenderer::PreparedViewState& prepared_view) -> bool
  {
    const auto camera_position
      = prepared_view.view_constants_snapshot.camera_position;
    const auto light_to_camera = glm::vec3(light.position_ws) - camera_position;
    const auto distance = glm::max(glm::length(light_to_camera), 0.001F);
    const auto projected_diameter_pixels
      = (2.0F * light.range / distance) * prepared_view.camera_viewport_width;
    return projected_diameter_pixels <= 96.0F;
  }

  auto BuildDirectionalClipmapDesc(
    const VsmShadowRenderer::PreparedViewState& prepared_view,
    const engine::DirectionalShadowCandidate& candidate,
    const oxygen::ShadowQualityTier quality_tier) -> VsmDirectionalClipmapDesc
  {
    const auto clip_level_count = std::max(1U, candidate.cascade_count);
    const auto authored_resolution
      = ShadowResolutionFromHint(candidate.resolution_hint);
    const auto effective_resolution
      = ApplyDirectionalShadowQualityTier(authored_resolution, quality_tier,
        prepared_view.directional_shadow_candidates.size());
    auto desc = VsmDirectionalClipmapDesc {
      .remap_key = BuildDirectionalLightRemapKey(candidate.node_handle),
      .clip_level_count = clip_level_count,
      .pages_per_axis = PagesPerAxisForVirtualResolution(effective_resolution),
      .debug_name = candidate.node_handle.IsValid()
        ? std::string("vsm-directional-")
          + std::string(nostd::to_string(candidate.node_handle.Index()))
        : "vsm-directional-invalid",
    };
    desc.page_grid_origin.reserve(clip_level_count);
    desc.page_world_size.reserve(clip_level_count);
    desc.near_depth.reserve(clip_level_count);
    desc.far_depth.reserve(clip_level_count);

    const auto frustum_context = BuildDirectionalFrustumContext(prepared_view);
    if (!frustum_context.has_value()) {
      LOG_F(WARNING,
        "VsmShadowRenderer: falling back to synthetic directional clipmap "
        "layout for node_handle={} because the camera depth span is invalid",
        candidate.node_handle);
      auto previous_far = 0.0F;
      for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
        ++clip_index) {
        const auto current_far = glm::max(
          candidate.cascade_distances[clip_index], previous_far + 1.0F);
        const auto clip_extent_world = glm::max(current_far * 2.0F, 1.0F);
        desc.page_grid_origin.push_back(glm::ivec2 { 0, 0 });
        desc.page_world_size.push_back(
          clip_extent_world / static_cast<float>(desc.pages_per_axis));
        desc.near_depth.push_back(glm::max(previous_far, 0.01F));
        desc.far_depth.push_back(current_far);
        previous_far = current_far;
      }
      return desc;
    }

    const auto light_context = BuildDirectionalLightProjectionContext(
      prepared_view, candidate, quality_tier);
    const auto virtual_resolution
      = std::max(1U, desc.pages_per_axis) * kVsmShadowPoolPageSizeTexels;
    auto prev_depth = std::max(frustum_context->near_depth, 0.0F);
    for (std::uint32_t clip_index = 0U; clip_index < clip_level_count;
      ++clip_index) {
      const auto clip_state = ComputeDirectionalClipLevelState(prepared_view,
        *frustum_context, light_context, candidate, clip_index,
        desc.pages_per_axis, virtual_resolution, prev_depth);
      desc.page_grid_origin.push_back(clip_state.page_grid_origin);
      desc.page_world_size.push_back(clip_state.page_world_size);
      desc.near_depth.push_back(clip_state.near_depth);
      desc.far_depth.push_back(clip_state.far_depth);
      prev_depth = clip_state.resolved_end_depth;
    }

    return desc;
  }

  auto BuildPagedLocalLightDesc(
    const engine::PositionalShadowCandidate& candidate,
    const engine::PositionalLightData& light,
    const oxygen::ShadowQualityTier quality_tier) -> VsmLocalLightDesc
  {
    const auto pages_per_axis
      = std::max(1U, PagesPerAxisForQualityTier(quality_tier) / 4U);
    return VsmLocalLightDesc {
      .remap_key = BuildLocalLightRemapKey(candidate.node_handle),
      .level_count = PagedLocalLevelCountForQualityTier(quality_tier),
      .pages_per_level_x = pages_per_axis,
      .pages_per_level_y = pages_per_axis,
      .debug_name = std::string("vsm-local-paged-")
        + std::string(nostd::to_string(candidate.node_handle.Index())) + "-"
        + std::string(nostd::to_string(light.shadow_resolution_hint)),
    };
  }

  auto BuildSinglePageLocalLightDesc(
    const engine::PositionalShadowCandidate& candidate)
    -> VsmSinglePageLightDesc
  {
    return VsmSinglePageLightDesc {
      .remap_key = BuildLocalLightRemapKey(candidate.node_handle),
      .debug_name = candidate.node_handle.IsValid()
        ? std::string("vsm-local-single-")
          + std::string(nostd::to_string(candidate.node_handle.Index()))
        : "vsm-local-single-invalid",
    };
  }

  auto BuildVirtualAddressSpaceConfig(
    const VsmShadowRenderer::PreparedViewState& prepared_view)
    -> VsmVirtualAddressSpaceConfig
  {
    return VsmVirtualAddressSpaceConfig {
    .first_virtual_id = 1U,
    .clipmap_reuse_config = {
      .max_page_offset_x = 32,
      .max_page_offset_y = 32,
      .depth_range_epsilon = 0.01F,
      .page_world_size_epsilon = 0.01F,
    },
    .debug_name = "VsmShadowRenderer.View"
      + std::string(nostd::to_string(prepared_view.view_id.get())),
  };
  }

  [[nodiscard]] auto ResolvePositionalLightType(
    const engine::PositionalLightData& light) noexcept
    -> engine::PositionalLightType
  {
    return static_cast<engine::PositionalLightType>(light.flags & 0b11U);
  }

  auto AppendDirectionalProjectionRecords(
    const VsmShadowRenderer::PreparedViewState& prepared_view,
    const VsmVirtualAddressSpaceFrame& current_frame,
    const oxygen::ShadowQualityTier quality_tier,
    std::vector<VsmPageRequestProjection>& projection_records) -> void
  {
    if (prepared_view.directional_shadow_candidates.empty()
      || current_frame.directional_layouts.empty()) {
      return;
    }

    const auto frustum_context = BuildDirectionalFrustumContext(prepared_view);
    if (!frustum_context.has_value()) {
      LOG_F(WARNING,
        "VsmShadowRenderer: skipping directional projection records because "
        "the camera depth span is invalid for view {}",
        prepared_view.view_id.get());
      return;
    }

    CHECK_EQ_F(current_frame.directional_layouts.size(),
      prepared_view.directional_shadow_candidates.size(),
      "VsmShadowRenderer directional layout count={} does not match prepared "
      "directional candidate count={}",
      current_frame.directional_layouts.size(),
      prepared_view.directional_shadow_candidates.size());

    for (std::size_t candidate_index = 0U;
      candidate_index < prepared_view.directional_shadow_candidates.size();
      ++candidate_index) {
      const auto& candidate
        = prepared_view.directional_shadow_candidates[candidate_index];
      const auto& layout = current_frame.directional_layouts[candidate_index];
      const auto cascade_count = std::min(layout.clip_level_count,
        std::max(1U,
          std::min(
            candidate.cascade_count, oxygen::scene::kMaxShadowCascades)));
      const auto light_context = BuildDirectionalLightProjectionContext(
        prepared_view, candidate, quality_tier);
      const auto virtual_resolution
        = std::max(1U, layout.pages_per_axis) * kVsmShadowPoolPageSizeTexels;
      auto prev_depth = std::max(frustum_context->near_depth, 0.0F);

      for (std::uint32_t clip_index = 0U; clip_index < cascade_count;
        ++clip_index) {
        const auto clip_state = ComputeDirectionalClipLevelState(prepared_view,
          *frustum_context, light_context, candidate, clip_index,
          layout.pages_per_axis, virtual_resolution, prev_depth);
        CHECK_EQ_F(layout.page_grid_origin[clip_index].x,
          clip_state.page_grid_origin.x,
          "VsmShadowRenderer directional layout clip {} x-origin {} must "
          "match projection-state x-origin {}",
          clip_index, layout.page_grid_origin[clip_index].x,
          clip_state.page_grid_origin.x);
        CHECK_EQ_F(layout.page_grid_origin[clip_index].y,
          clip_state.page_grid_origin.y,
          "VsmShadowRenderer directional layout clip {} y-origin {} must "
          "match projection-state y-origin {}",
          clip_index, layout.page_grid_origin[clip_index].y,
          clip_state.page_grid_origin.y);

        projection_records.push_back(VsmPageRequestProjection {
        .projection = VsmProjectionData {
          .view_matrix = clip_state.light_view,
          .projection_matrix = clip_state.light_projection,
          .view_origin_ws_pad = clip_state.view_origin_ws_pad,
          .receiver_depth_range_pad
          = glm::vec4(
            prev_depth, clip_state.resolved_end_depth,
            candidate.bias, candidate.normal_bias),
          .clipmap_corner_offset = layout.page_grid_origin[clip_index],
          .clipmap_level = clip_index,
          .light_type = static_cast<std::uint32_t>(
            VsmProjectionLightType::kDirectional),
        },
        .map_id = layout.first_id + clip_index,
        .first_page_table_entry = layout.first_page_table_entry,
        .map_pages_x = layout.pages_per_axis,
        .map_pages_y = layout.pages_per_axis,
        .pages_x = layout.pages_per_axis,
        .pages_y = layout.pages_per_axis,
        .page_offset_x = 0U,
        .page_offset_y = 0U,
        .level_count = layout.clip_level_count,
        .coarse_level = 0U,
      });

        prev_depth = clip_state.resolved_end_depth;
      }
    }
  }

  auto AppendLocalProjectionRecords(
    const VsmShadowRenderer::PreparedViewState& prepared_view,
    const VsmVirtualAddressSpaceFrame& current_frame,
    std::vector<VsmPageRequestProjection>& projection_records) -> void
  {
    CHECK_EQ_F(current_frame.local_light_layouts.size(),
      prepared_view.positional_shadow_candidates.size(),
      "VsmShadowRenderer local layout count={} does not match prepared local "
      "shadow candidate count={}",
      current_frame.local_light_layouts.size(),
      prepared_view.positional_shadow_candidates.size());

    for (std::size_t candidate_index = 0U;
      candidate_index < prepared_view.positional_shadow_candidates.size();
      ++candidate_index) {
      const auto& candidate
        = prepared_view.positional_shadow_candidates[candidate_index];
      CHECK_LT_F(candidate.light_index, prepared_view.positional_lights.size(),
        "VsmShadowRenderer local shadow candidate light_index={} exceeds "
        "prepared positional light count={}",
        candidate.light_index, prepared_view.positional_lights.size());
      const auto& light
        = prepared_view.positional_lights[candidate.light_index];
      const auto& layout = current_frame.local_light_layouts[candidate_index];
      const auto light_type = ResolvePositionalLightType(light);

      if (light_type == engine::PositionalLightType::kPoint) {
        DLOG_F(2,
          "VsmShadowRenderer: skipping point-light projection routing for "
          "node_handle={} until Phase K-d point-face scheduling is wired",
          candidate.node_handle);
        continue;
      }

      const auto light_direction
        = NormalizeOrFallback(light.direction_ws, glm::vec3(0.0F, 0.0F, -1.0F));
      const auto light_position = glm::vec3(light.position_ws);
      const auto world_up
        = std::abs(glm::dot(light_direction, glm::vec3(0.0F, 0.0F, 1.0F)))
          > 0.95F
        ? glm::vec3(1.0F, 0.0F, 0.0F)
        : glm::vec3(0.0F, 0.0F, 1.0F);
      const auto light_view = glm::lookAtRH(
        light_position, light_position + light_direction, world_up);
      const auto outer_cone_angle = std::clamp(
        2.0F * std::acos(std::clamp(light.outer_cone_cos, -1.0F, 1.0F)),
        glm::radians(1.0F), glm::radians(175.0F));
      const auto light_projection = glm::perspectiveRH_ZO(
        outer_cone_angle, 1.0F, 0.1F, std::max(light.range, 0.2F));

      projection_records.push_back(VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = light_view,
        .projection_matrix = light_projection,
        .view_origin_ws_pad = glm::vec4(light_position, 0.0F),
        .receiver_depth_range_pad
        = glm::vec4(0.0F, 0.0F, light.shadow_bias, light.shadow_normal_bias),
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
      },
      .map_id = layout.id,
      .first_page_table_entry = layout.first_page_table_entry,
      .map_pages_x = layout.pages_per_level_x,
      .map_pages_y = layout.pages_per_level_y,
      .pages_x = layout.pages_per_level_x,
      .pages_y = layout.pages_per_level_y,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = layout.level_count,
      .coarse_level = 0U,
      .light_index = candidate.light_index,
    });
    }
  }

  [[nodiscard]] auto BuildCurrentProjectionRecords(
    const VsmShadowRenderer::PreparedViewState& prepared_view,
    const VsmVirtualAddressSpaceFrame& current_frame,
    const oxygen::ShadowQualityTier quality_tier)
    -> std::vector<VsmPageRequestProjection>
  {
    auto projection_records = std::vector<VsmPageRequestProjection> {};
    projection_records.reserve(current_frame.directional_layouts.size() * 4U
      + current_frame.local_light_layouts.size());
    AppendDirectionalProjectionRecords(
      prepared_view, current_frame, quality_tier, projection_records);
    AppendLocalProjectionRecords(
      prepared_view, current_frame, projection_records);
    return projection_records;
  }

  auto ComputeCurrentFrameRequiredPhysicalPageCount(
    const VsmVirtualAddressSpaceFrame& current_frame) -> std::uint32_t
  {
    return std::max(current_frame.total_page_table_entry_count, 1U);
  }

  auto ComputeShadowPoolTileCapacityForPageCount(
    const std::uint32_t required_page_count) -> std::uint32_t
  {
    const auto tiles_per_axis = std::max(16U,
      static_cast<std::uint32_t>(
        std::ceil(std::sqrt(static_cast<float>(required_page_count) / 2.0F))));
    return 2U * tiles_per_axis * tiles_per_axis;
  }

  auto ComputeRequestedShadowPoolTileCapacity(
    const VsmVirtualAddressSpaceFrame& current_frame,
    const VsmPageAllocationSnapshot* previous_frame) -> std::uint32_t
  {
    const auto current_capacity = ComputeShadowPoolTileCapacityForPageCount(
      ComputeCurrentFrameRequiredPhysicalPageCount(current_frame));
    if (previous_frame == nullptr) {
      return current_capacity;
    }

    return std::max(current_capacity, previous_frame->pool_tile_capacity);
  }

  auto BuildShadowPoolConfig(const oxygen::ShadowQualityTier quality_tier,
    const VsmVirtualAddressSpaceFrame& current_frame,
    const VsmPageAllocationSnapshot* previous_frame) -> VsmPhysicalPoolConfig
  {
    static_cast<void>(quality_tier);
    return VsmPhysicalPoolConfig {
      .page_size_texels = kVsmShadowPoolPageSizeTexels,
      .physical_tile_capacity
      = ComputeRequestedShadowPoolTileCapacity(current_frame, previous_frame),
      .array_slice_count = 2U,
      .depth_format = Format::kDepth32,
      .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
        VsmPhysicalPoolSliceRole::kStaticDepth },
      .debug_name = "VsmShadowRenderer.ShadowPool",
    };
  }

  auto BuildHzbPoolConfig(const VsmPhysicalPoolConfig& shadow_pool_config)
    -> VsmHzbPoolConfig
  {
    return VsmHzbPoolConfig {
      .mip_count = (std::max)(1U,
        static_cast<std::uint32_t>(
          std::bit_width(shadow_pool_config.page_size_texels))),
      .format = Format::kR32Float,
      .debug_name = "VsmShadowRenderer.HzbPool",
    };
  }

#if !defined(NDEBUG)

  auto DebugCheckPreparedLightNodeHandles(
    const VsmShadowRenderer::PreparedViewState& prepared_view) -> void
  {
    const auto scene = prepared_view.active_scene;
    if (scene == nullptr) {
      return;
    }

    for (const auto& candidate : prepared_view.directional_shadow_candidates) {
      DCHECK_F(candidate.node_handle.BelongsToScene(scene->GetId()),
        "prepared directional shadow candidate handle={} does not belong to "
        "active scene `{}`",
        candidate.node_handle, scene->GetName());
      DCHECK_F(scene->GetNode(candidate.node_handle).has_value(),
        "prepared directional shadow candidate handle={} is no longer valid in "
        "active scene `{}`",
        candidate.node_handle, scene->GetName());
    }

    for (const auto& candidate : prepared_view.positional_shadow_candidates) {
      DCHECK_F(candidate.node_handle.BelongsToScene(scene->GetId()),
        "prepared positional shadow candidate handle={} does not belong to "
        "active scene `{}`",
        candidate.node_handle, scene->GetName());
      DCHECK_F(scene->GetNode(candidate.node_handle).has_value(),
        "prepared positional shadow candidate handle={} is no longer valid in "
        "active scene `{}`",
        candidate.node_handle, scene->GetName());
    }

    for (const auto& record : prepared_view.scene_primitive_history) {
      DCHECK_F(record.node_handle.BelongsToScene(scene->GetId()),
        "prepared primitive history handle={} does not belong to active scene "
        "`{}`",
        record.node_handle, scene->GetName());
      DCHECK_F(scene->GetNode(record.node_handle).has_value(),
        "prepared primitive history handle={} is no longer valid in active "
        "scene `{}`",
        record.node_handle, scene->GetName());
    }
  }

#endif // !defined(NDEBUG)

} // namespace

namespace {

  struct PageRequestKey {
    VsmVirtualShadowMapId map_id { 0U };
    VsmVirtualPageCoord page {};

    auto operator==(const PageRequestKey&) const -> bool = default;
  };

  struct PageRequestKeyHash {
    auto operator()(const PageRequestKey& key) const noexcept -> std::size_t
    {
      auto hash = std::size_t { 0U };
      oxygen::HashCombine(hash, key.map_id);
      oxygen::HashCombine(hash, key.page.level);
      oxygen::HashCombine(hash, key.page.page_x);
      oxygen::HashCombine(hash, key.page.page_y);
      return hash;
    }
  };

  auto DecodePageRequestsFromGpuFlags(
    std::span<const VsmPageRequestProjection> projections,
    std::span<const VsmShaderPageRequestFlags> shader_flags)
    -> VsmPageRequestSet
  {
    auto merged = std::unordered_map<PageRequestKey, VsmPageRequestFlags,
      PageRequestKeyHash> {};

    for (const auto& projection : projections) {
      if (!IsValid(projection)) {
        continue;
      }

      const auto first_level = projection.projection.light_type
          == static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional)
        ? projection.projection.clipmap_level
        : 0U;
      const auto level_count = projection.projection.light_type
          == static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional)
        ? 1U
        : projection.level_count;
      for (std::uint32_t level_offset = 0U; level_offset < level_count;
        ++level_offset) {
        const auto level = first_level + level_offset;
        for (std::uint32_t local_y = 0U; local_y < projection.pages_y;
          ++local_y) {
          for (std::uint32_t local_x = 0U; local_x < projection.pages_x;
            ++local_x) {
            const auto page = VsmVirtualPageCoord {
              .level = level,
              .page_x = projection.page_offset_x + local_x,
              .page_y = projection.page_offset_y + local_y,
            };
            const auto page_table_index
              = TryComputePageTableIndex(projection, page);
            if (!page_table_index.has_value()
              || *page_table_index >= shader_flags.size()) {
              continue;
            }

            const auto shader_request = shader_flags[*page_table_index];
            auto request_flags = VsmPageRequestFlags::kNone;
            if (HasAnyRequestFlag(
                  shader_request, VsmShaderPageRequestFlagBits::kRequired)) {
              request_flags |= VsmPageRequestFlags::kRequired;
            }
            if (HasAnyRequestFlag(
                  shader_request, VsmShaderPageRequestFlagBits::kCoarse)) {
              request_flags |= (VsmPageRequestFlags::kRequired
                | VsmPageRequestFlags::kCoarse);
            }
            if (HasAnyRequestFlag(
                  shader_request, VsmShaderPageRequestFlagBits::kStaticOnly)) {
              request_flags |= VsmPageRequestFlags::kStaticOnly;
            }
            if (request_flags == VsmPageRequestFlags::kNone) {
              continue;
            }

            merged[PageRequestKey {
              .map_id = projection.map_id,
              .page = page,
            }] |= request_flags;
          }
        }
      }
    }

    auto requests = VsmPageRequestSet {};
    requests.reserve(merged.size());
    for (const auto& [key, flags] : merged) {
      requests.push_back(VsmPageRequest {
        .map_id = key.map_id,
        .page = key.page,
        .flags = flags,
      });
    }

    std::ranges::sort(
      requests, [](const VsmPageRequest& lhs, const VsmPageRequest& rhs) {
        return std::tie(
                 lhs.map_id, lhs.page.level, lhs.page.page_y, lhs.page.page_x)
          < std::tie(
            rhs.map_id, rhs.page.level, rhs.page.page_y, rhs.page.page_x);
      });

    return requests;
  }

} // namespace

VsmShadowRenderer::VsmShadowRenderer(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> staging_provider,
  const observer_ptr<CoordinatorT> inline_transfers,
  const oxygen::ShadowQualityTier quality_tier)
  : VsmShadowRenderer(gfx, staging_provider, inline_transfers, quality_tier,
      VsmCacheManagerConfig { .debug_name = "VsmShadowRenderer.CacheManager" })
{
}

VsmShadowRenderer::VsmShadowRenderer(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> staging_provider,
  const observer_ptr<CoordinatorT> inline_transfers,
  const oxygen::ShadowQualityTier quality_tier,
  VsmCacheManagerConfig cache_manager_config)
  : gfx_(gfx)
  , staging_provider_(staging_provider)
  , inline_transfers_(inline_transfers)
  , quality_tier_(quality_tier)
  , physical_page_pool_manager_(gfx_.get())
  , cache_manager_(gfx_.get(), std::move(cache_manager_config))
  , page_request_generator_config_(
      std::make_shared<engine::VsmPageRequestGeneratorPassConfig>())
  , invalidation_pass_config_(
      std::make_shared<engine::VsmInvalidationPassConfig>())
  , page_management_pass_config_(
      std::make_shared<engine::VsmPageManagementPassConfig>())
  , page_flag_propagation_pass_config_(
      std::make_shared<engine::VsmPageFlagPropagationPassConfig>())
  , page_initialization_pass_config_(
      std::make_shared<engine::VsmPageInitializationPassConfig>())
  , shadow_rasterizer_pass_config_(
      std::make_shared<engine::VsmShadowRasterizerPassConfig>())
  , static_dynamic_merge_pass_config_(
      std::make_shared<engine::VsmStaticDynamicMergePassConfig>())
  , hzb_updater_pass_config_(
      std::make_shared<engine::VsmHzbUpdaterPassConfig>())
  , projection_pass_config_(std::make_shared<engine::VsmProjectionPassConfig>())
  , page_request_generator_pass_(
      std::make_shared<engine::VsmPageRequestGeneratorPass>(
        gfx_, page_request_generator_config_))
  , invalidation_pass_(std::make_shared<engine::VsmInvalidationPass>(
      gfx_, invalidation_pass_config_))
  , page_management_pass_(std::make_shared<engine::VsmPageManagementPass>(
      gfx_, page_management_pass_config_))
  , page_flag_propagation_pass_(
      std::make_shared<engine::VsmPageFlagPropagationPass>(
        gfx_, page_flag_propagation_pass_config_))
  , page_initialization_pass_(
      std::make_shared<engine::VsmPageInitializationPass>(
        gfx_, page_initialization_pass_config_))
  , shadow_rasterizer_pass_(std::make_shared<engine::VsmShadowRasterizerPass>(
      gfx_, shadow_rasterizer_pass_config_))
  , static_dynamic_merge_pass_(
      std::make_shared<engine::VsmStaticDynamicMergePass>(
        gfx_, static_dynamic_merge_pass_config_))
  , hzb_updater_pass_(std::make_shared<engine::VsmHzbUpdaterPass>(
      gfx_, hzb_updater_pass_config_))
  , projection_pass_(std::make_shared<engine::VsmProjectionPass>(
      gfx_, projection_pass_config_))
{
  DCHECK_NOTNULL_F(gfx_.get(), "VsmShadowRenderer requires Graphics");
  DCHECK_NOTNULL_F(
    staging_provider_.get(), "VsmShadowRenderer requires a staging provider");
  DCHECK_NOTNULL_F(inline_transfers_.get(),
    "VsmShadowRenderer requires an inline transfer coordinator");
}

VsmShadowRenderer::~VsmShadowRenderer() = default;

auto VsmShadowRenderer::OnFrameStart(const RendererTag tag,
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  static_cast<void>(tag);
  current_frame_sequence_ = sequence;
  current_frame_slot_ = slot;
  prepared_views_.clear();
}

auto VsmShadowRenderer::ResetCachedState() -> void
{
  prepared_views_.clear();
  invalidation_coordinator_.Reset();
  cache_manager_.Reset();
  physical_page_pool_manager_.Reset();
}

auto VsmShadowRenderer::PrepareView(const ViewId view_id,
  const engine::ViewConstants& view_constants, const LightManager& lights,
  const observer_ptr<scene::Scene> active_scene,
  const float camera_viewport_width,
  const std::span<const engine::sceneprep::RenderItemData> rendered_items,
  const std::span<const glm::vec4> shadow_caster_bounds,
  const std::span<const glm::vec4> visible_receiver_bounds,
  const std::chrono::milliseconds gpu_budget,
  const std::uint64_t shadow_caster_content_hash) -> bool
{
  invalidation_coordinator_.SyncObservedScene(active_scene);

  const auto directional_shadow_candidates
    = lights.GetDirectionalShadowCandidates();
  const auto positional_lights = lights.GetPositionalLights();
  const auto positional_shadow_candidates
    = lights.GetPositionalShadowCandidates();

  auto& prepared_view = prepared_views_[view_id];
  prepared_view = PreparedViewState {
    .view_id = view_id,
    .frame_sequence = current_frame_sequence_,
    .frame_slot = current_frame_slot_,
    .active_scene = active_scene,
    .view_constants_snapshot = view_constants.GetSnapshot(),
    .camera_viewport_width = camera_viewport_width,
    .directional_shadow_candidates = { directional_shadow_candidates.begin(),
      directional_shadow_candidates.end() },
    .positional_lights = { positional_lights.begin(), positional_lights.end() },
    .positional_shadow_candidates = { positional_shadow_candidates.begin(),
      positional_shadow_candidates.end() },
    .scene_primitive_history = BuildScenePrimitiveHistory(rendered_items),
    .shadow_caster_bounds
    = { shadow_caster_bounds.begin(), shadow_caster_bounds.end() },
    .visible_receiver_bounds
    = { visible_receiver_bounds.begin(), visible_receiver_bounds.end() },
    .gpu_budget = gpu_budget,
    .shadow_caster_content_hash = shadow_caster_content_hash,
    .has_virtual_shadow_work
    = !directional_shadow_candidates.empty() || !positional_lights.empty(),
  };

  LOG_F(INFO,
    "VsmShadowRenderer: prepared view={} frame_seq={} frame_slot={} "
    "active_scene={} directional_candidates={} positional_lights={} "
    "positional_shadow_candidates={} scene_primitives={} "
    "caster_bounds={} receiver_bounds={} shadow_caster_content_hash={}",
    view_id.get(), prepared_view.frame_sequence.get(),
    prepared_view.frame_slot.get(), fmt::ptr(prepared_view.active_scene.get()),
    prepared_view.directional_shadow_candidates.size(),
    prepared_view.positional_lights.size(),
    prepared_view.positional_shadow_candidates.size(),
    prepared_view.scene_primitive_history.size(),
    prepared_view.shadow_caster_bounds.size(),
    prepared_view.visible_receiver_bounds.size(),
    prepared_view.shadow_caster_content_hash);

  return prepared_view.has_virtual_shadow_work;
}

auto VsmShadowRenderer::BuildCurrentVirtualFrame(
  const PreparedViewState& prepared_view) -> const VsmVirtualAddressSpaceFrame&
{
  virtual_address_space_.BeginFrame(
    BuildVirtualAddressSpaceConfig(prepared_view),
    prepared_view.frame_sequence.get());

  for (const auto& candidate : prepared_view.directional_shadow_candidates) {
    virtual_address_space_.AllocateDirectionalClipmap(
      BuildDirectionalClipmapDesc(prepared_view, candidate, quality_tier_));
  }

  for (const auto& candidate : prepared_view.positional_shadow_candidates) {
    CHECK_LT_F(candidate.light_index, prepared_view.positional_lights.size(),
      "VsmShadowRenderer positional shadow candidate light_index={} exceeds "
      "positional light count={}",
      candidate.light_index, prepared_view.positional_lights.size());
    const auto& light = prepared_view.positional_lights[candidate.light_index];
    if (ShouldUseSinglePageLocalLayout(light, prepared_view)) {
      virtual_address_space_.AllocateSinglePageLocalLight(
        BuildSinglePageLocalLightDesc(candidate));
    } else {
      virtual_address_space_.AllocatePagedLocalLight(
        BuildPagedLocalLightDesc(candidate, light, quality_tier_));
    }
  }

  return virtual_address_space_.DescribeFrame();
}

auto VsmShadowRenderer::BuildCurrentSeam(
  const VsmVirtualAddressSpaceFrame& current_frame) -> VsmCacheManagerSeam
{
  const auto* previous_frame = cache_manager_.GetPreviousFrame();
  const auto shadow_pool_config
    = BuildShadowPoolConfig(quality_tier_, current_frame, previous_frame);
  const auto hzb_pool_config = BuildHzbPoolConfig(shadow_pool_config);

  const auto shadow_pool_change
    = physical_page_pool_manager_.EnsureShadowPool(shadow_pool_config);
  const auto hzb_pool_change
    = physical_page_pool_manager_.EnsureHzbPool(hzb_pool_config);

  LOG_F(INFO,
    "VsmShadowRenderer: prepared physical seam frame_generation={} "
    "shadow_pool_change={} hzb_pool_change={} total_page_table_entries={}",
    current_frame.frame_generation, shadow_pool_change, hzb_pool_change,
    current_frame.total_page_table_entry_count);

  return VsmCacheManagerSeam {
    .physical_pool = physical_page_pool_manager_.GetShadowPoolSnapshot(),
    .hzb_pool = physical_page_pool_manager_.GetHzbPoolSnapshot(),
    .current_frame = current_frame,
    .previous_to_current_remap = previous_frame != nullptr
      ? virtual_address_space_.BuildRemapTable(previous_frame->virtual_frame)
      : VsmVirtualRemapTable {},
  };
}

auto VsmShadowRenderer::BuildSceneLightRemapBindings(
  const PreparedViewState& prepared_view,
  const VsmVirtualAddressSpaceFrame& current_frame) const
  -> std::vector<VsmSceneLightRemapBinding>
{
  auto bindings = std::vector<VsmSceneLightRemapBinding> {};
  bindings.reserve(prepared_view.directional_shadow_candidates.size()
    + prepared_view.positional_shadow_candidates.size());

  CHECK_EQ_F(current_frame.directional_layouts.size(),
    prepared_view.directional_shadow_candidates.size(),
    "VsmShadowRenderer directional layout count={} does not match prepared "
    "directional candidate count={}",
    current_frame.directional_layouts.size(),
    prepared_view.directional_shadow_candidates.size());
  for (std::size_t i = 0;
    i < prepared_view.directional_shadow_candidates.size(); ++i) {
    bindings.push_back(VsmSceneLightRemapBinding {
      .node_handle = prepared_view.directional_shadow_candidates[i].node_handle,
      .kind = VsmLightCacheKind::kDirectional,
      .remap_keys = { current_frame.directional_layouts[i].remap_key },
    });
  }

  CHECK_EQ_F(current_frame.local_light_layouts.size(),
    prepared_view.positional_shadow_candidates.size(),
    "VsmShadowRenderer local layout count={} does not match prepared local "
    "shadow candidate count={}",
    current_frame.local_light_layouts.size(),
    prepared_view.positional_shadow_candidates.size());
  for (std::size_t i = 0; i < prepared_view.positional_shadow_candidates.size();
    ++i) {
    bindings.push_back(VsmSceneLightRemapBinding {
      .node_handle = prepared_view.positional_shadow_candidates[i].node_handle,
      .kind = VsmLightCacheKind::kLocal,
      .remap_keys = { current_frame.local_light_layouts[i].remap_key },
    });
  }

  return bindings;
}

auto VsmShadowRenderer::ExecutePreparedViewShell(
  const engine::RenderContext& render_context,
  graphics::CommandRecorder& recorder,
  const observer_ptr<const graphics::Texture> scene_depth_texture) -> co::Co<>
{
  const auto view_id = render_context.current_view.view_id;
  CHECK_F(view_id != ViewId {},
    "VsmShadowRenderer::ExecutePreparedViewShell requires a valid current "
    "view");

  const auto* prepared_view = TryGetPreparedViewState(view_id);
  if (prepared_view == nullptr) {
    LOG_F(INFO,
      "VsmShadowRenderer: skipping live shell for view={} because no prepared "
      "view state was captured",
      view_id.get());
    render_context.GetRenderer().UpdateCurrentViewVirtualShadowFrameBindings(
      render_context, engine::VsmFrameBindings {});
    co_return;
  }

#if !defined(NDEBUG)
  DebugCheckPreparedLightNodeHandles(*prepared_view);
#endif // !defined(NDEBUG)

  invalidation_coordinator_.SyncObservedScene(prepared_view->active_scene);
  const auto prepared_products = BuildPreparedViewProducts(view_id);
  CHECK_F(prepared_products.has_value(),
    "VsmShadowRenderer::ExecutePreparedViewShell requires prepared products "
    "for the active view");
  const auto current_virtual_frame = prepared_products->virtual_frame;
  auto seam = prepared_products->seam;
  auto scene_light_remap_bindings
    = BuildSceneLightRemapBindings(*prepared_view, current_virtual_frame);
  invalidation_coordinator_.PublishSceneLightRemapBindings(
    scene_light_remap_bindings);
  invalidation_coordinator_.PublishScenePrimitiveHistory(
    prepared_view->scene_primitive_history);
  const auto frame_inputs = invalidation_coordinator_.DrainFrameInputs();
  VsmSceneInvalidationCoordinator::ApplyLightInvalidationRequests(
    cache_manager_, frame_inputs.light_invalidation_requests);

  auto frame_open = false;
  try {
    cache_manager_.BeginFrame(seam,
      VsmCacheManagerFrameConfig {
        .allow_reuse = true,
        .force_invalidate_all = false,
        .debug_name = "VsmShadowRenderer.LiveShell",
      });
    frame_open = true;

    const auto& invalidation_workload = cache_manager_.BuildInvalidationWork(
      frame_inputs.primitive_invalidations);
    auto invalidation_metadata_seed_buffer
      = std::shared_ptr<const graphics::Buffer> {};
    if (const auto* previous_frame = cache_manager_.GetPreviousFrame();
      previous_frame != nullptr && !invalidation_workload.work_items.empty()) {
      invalidation_pass_->SetInput(engine::VsmInvalidationPassInput {
        .previous_projection_records = previous_frame->projection_records,
        .previous_page_table_entries
        = BuildShaderPageTableEntries(previous_frame->page_table),
        .previous_physical_page_metadata = previous_frame->physical_pages,
        .invalidation_work_items = invalidation_workload.work_items,
      });
      co_await invalidation_pass_->PrepareResources(render_context, recorder);
      co_await invalidation_pass_->Execute(render_context, recorder);
      invalidation_metadata_seed_buffer
        = invalidation_pass_->GetCurrentOutputPhysicalMetadataBuffer();
    } else {
      invalidation_pass_->ResetInput();
    }

    const auto& current_projection_records
      = prepared_products->projection_records;
    const auto has_page_requests
      = co_await ExecutePageRequestReadbackBridge(render_context, seam,
        current_projection_records, invalidation_metadata_seed_buffer);

    const auto* committed_frame = cache_manager_.GetCurrentFrame();
    if (committed_frame == nullptr || !committed_frame->is_ready) {
      LOG_F(ERROR,
        "VsmShadowRenderer: live shell failed to commit a current frame for "
        "view={}",
        view_id.get());
      cache_manager_.AbortFrame();
      frame_open = false;
      render_context.GetRenderer().UpdateCurrentViewVirtualShadowFrameBindings(
        render_context, engine::VsmFrameBindings {});
      co_return;
    }
    const auto current_frame = *committed_frame;

    page_management_pass_->SetFrameInput(current_frame);
    co_await page_management_pass_->PrepareResources(render_context, recorder);
    co_await page_management_pass_->Execute(render_context, recorder);

    page_flag_propagation_pass_->SetFrameInput(current_frame);
    co_await page_flag_propagation_pass_->PrepareResources(
      render_context, recorder);
    co_await page_flag_propagation_pass_->Execute(render_context, recorder);

    page_initialization_pass_->SetInput(engine::VsmPageInitializationPassInput {
      .frame = current_frame,
      .physical_pool = seam.physical_pool,
    });
    co_await page_initialization_pass_->PrepareResources(
      render_context, recorder);
    co_await page_initialization_pass_->Execute(render_context, recorder);

    const auto* previous_frame = cache_manager_.GetPreviousFrame();
    const auto previous_visible_shadow_primitives = previous_frame != nullptr
      ? std::span<const VsmPrimitiveIdentity>(
          previous_frame->visible_shadow_primitives)
      : std::span<const VsmPrimitiveIdentity> {};
    shadow_rasterizer_pass_->SetInput(engine::VsmShadowRasterizerPassInput {
      .frame = current_frame,
      .physical_pool = seam.physical_pool,
      .projections = current_projection_records,
      .base_view_constants = prepared_view->view_constants_snapshot,
      .previous_visible_shadow_primitives = {
        previous_visible_shadow_primitives.begin(),
        previous_visible_shadow_primitives.end(),
      },
    });
    co_await shadow_rasterizer_pass_->PrepareResources(
      render_context, recorder);
    co_await shadow_rasterizer_pass_->Execute(render_context, recorder);
    cache_manager_.PublishVisibleShadowPrimitives(
      shadow_rasterizer_pass_->GetVisibleShadowPrimitives());
    cache_manager_.PublishRenderedPrimitiveHistory(
      shadow_rasterizer_pass_->GetRenderedPrimitiveHistory());
    cache_manager_.PublishStaticPrimitivePageFeedback(
      shadow_rasterizer_pass_->GetStaticPageFeedback());
    const auto merge_candidate_logical_pages
      = BuildStaticMergeCandidateLogicalPages(
        shadow_rasterizer_pass_->GetPreparedPages(), seam.physical_pool);

    static_dynamic_merge_pass_->SetInput(
      engine::VsmStaticDynamicMergePassInput {
        .frame = current_frame,
        .physical_pool = seam.physical_pool,
        .merge_candidate_logical_pages = merge_candidate_logical_pages,
      });
    co_await static_dynamic_merge_pass_->PrepareResources(
      render_context, recorder);
    co_await static_dynamic_merge_pass_->Execute(render_context, recorder);

    hzb_updater_pass_->SetInput(engine::VsmHzbUpdaterPassInput {
      .frame = current_frame,
      .physical_pool = seam.physical_pool,
      .hzb_pool = seam.hzb_pool,
      .can_preserve_existing_hzb_contents = cache_manager_.IsHzbDataAvailable(),
      .force_rebuild_all_allocated_pages = false,
    });
    co_await hzb_updater_pass_->PrepareResources(render_context, recorder);
    co_await hzb_updater_pass_->Execute(render_context, recorder);
    cache_manager_.PublishCurrentFrameHzbAvailability(
      seam.hzb_pool.is_available);

    auto scene_depth_output = engine::DepthPrePassOutput {};
    if (const auto* depth_pass = render_context.GetPass<engine::DepthPrePass>();
      depth_pass != nullptr) {
      scene_depth_output = depth_pass->GetOutput();
    }
    auto scene_depth_texture_ref = std::shared_ptr<const graphics::Texture> {};
    if (scene_depth_texture) {
      scene_depth_texture_ref = std::shared_ptr<const graphics::Texture>(
        scene_depth_texture.get(), [](const graphics::Texture*) { });
    }
    projection_pass_->SetInput(engine::VsmProjectionPassInput {
      .frame = current_frame,
      .physical_pool = seam.physical_pool,
      .scene_depth_texture = std::move(scene_depth_texture_ref),
      .scene_depth_output = scene_depth_output,
    });
    co_await projection_pass_->PrepareResources(render_context, recorder);
    co_await projection_pass_->Execute(render_context, recorder);

    const auto projection_output = projection_pass_->GetCurrentOutput(view_id);
    render_context.GetRenderer().UpdateCurrentViewVirtualShadowFrameBindings(
      render_context,
      engine::VsmFrameBindings {
        .directional_shadow_mask_slot = projection_output.available
          ? projection_output.directional_shadow_mask_srv_index
          : kInvalidShaderVisibleIndex,
        .screen_shadow_mask_slot = projection_output.available
          ? projection_output.shadow_mask_srv_index
          : kInvalidShaderVisibleIndex,
      });

    cache_manager_.QueueFrameExtraction(recorder);
    frame_open = false;

    LOG_F(INFO,
      "VsmShadowRenderer: entering live shell view={} depth_texture={} "
      "has_virtual_shadow_work={} active_scene={} directional_candidates={} "
      "positional_lights={} positional_shadow_candidates={} local_layouts={} "
      "directional_layouts={} projection_records={} page_requests={} "
      "shadow_mask_available={} primitive_invalidations={} "
      "light_invalidations={}",
      view_id.get(), fmt::ptr(scene_depth_texture.get()),
      prepared_view->has_virtual_shadow_work,
      fmt::ptr(prepared_view->active_scene.get()),
      prepared_view->directional_shadow_candidates.size(),
      prepared_view->positional_lights.size(),
      prepared_view->positional_shadow_candidates.size(),
      current_virtual_frame.local_light_layouts.size(),
      current_virtual_frame.directional_layouts.size(),
      current_projection_records.size(), has_page_requests,
      projection_output.available, frame_inputs.primitive_invalidations.size(),
      frame_inputs.light_invalidation_requests.size());
  } catch (...) {
    if (frame_open) {
      cache_manager_.AbortFrame();
    }
    invalidation_pass_->ResetInput();
    page_request_generator_pass_->ResetFrameInputs();
    render_context.GetRenderer().UpdateCurrentViewVirtualShadowFrameBindings(
      render_context, engine::VsmFrameBindings {});
    throw;
  }
  co_return;
}

auto VsmShadowRenderer::ExecutePageRequestReadbackBridge(
  const engine::RenderContext& render_context, const VsmCacheManagerSeam& seam,
  const std::span<const VsmPageRequestProjection> current_projection_records,
  std::shared_ptr<const graphics::Buffer> physical_page_meta_seed_buffer)
  -> co::Co<bool>
{
  if (cache_manager_.DescribeBuildState() == VsmCacheBuildState::kIdle) {
    cache_manager_.BeginFrame(seam,
      VsmCacheManagerFrameConfig {
        .allow_reuse = true,
        .force_invalidate_all = false,
        .debug_name = "VsmShadowRenderer.PageRequestReadbackBridge",
      });
  } else {
    CHECK_F(
      cache_manager_.DescribeBuildState() == VsmCacheBuildState::kFrameOpen,
      "VsmShadowRenderer page-request bridge requires idle or frame-open "
      "cache-manager state");
  }

  auto finalize_bridge_commit
    = [&](const VsmPageRequestSet& page_requests) -> bool {
    cache_manager_.SetPageRequests(page_requests);
    static_cast<void>(cache_manager_.BuildPageAllocationPlan());
    static_cast<void>(cache_manager_.CommitPageAllocationFrame());
    cache_manager_.PublishPhysicalPageMetaSeedBuffer(
      std::move(physical_page_meta_seed_buffer));
    cache_manager_.PublishProjectionRecords(current_projection_records);
    page_request_generator_pass_->ResetFrameInputs();
    return !page_requests.empty();
  };

  if (seam.current_frame.total_page_table_entry_count == 0U
    || current_projection_records.empty()) {
    co_return finalize_bridge_commit({});
  }

  page_request_generator_config_->max_projection_count
    = std::max(page_request_generator_config_->max_projection_count,
      static_cast<std::uint32_t>(current_projection_records.size()));
  page_request_generator_config_->max_virtual_page_count
    = std::max(page_request_generator_config_->max_virtual_page_count,
      seam.current_frame.total_page_table_entry_count);
  page_request_generator_config_->enable_light_grid_pruning
    = render_context.GetPass<engine::LightCullingPass>() != nullptr;
  page_request_generator_config_->debug_name
    = "VsmShadowRenderer.PageRequestGenerator";

  page_request_generator_pass_->SetFrameInputs(
    { current_projection_records.begin(), current_projection_records.end() },
    seam.current_frame.total_page_table_entry_count);

  const auto queue_key = gfx_->QueueKeyFor(graphics::QueueRole::kGraphics);
  {
    auto recorder = gfx_->AcquireCommandRecorder(
      queue_key, "VsmShadowRenderer.PageRequestBridge");
    CHECK_NOTNULL_F(recorder.get(),
      "VsmShadowRenderer page-request bridge requires a command recorder");

    co_await page_request_generator_pass_->PrepareResources(
      render_context, *recorder);
    co_await page_request_generator_pass_->Execute(render_context, *recorder);
  }

  const auto request_flags_buffer = std::const_pointer_cast<graphics::Buffer>(
    page_request_generator_pass_->GetPageRequestFlagsBuffer());
  if (request_flags_buffer == nullptr) {
    LOG_F(ERROR,
      "VsmShadowRenderer: page-request bridge failed because the request-flags "
      "buffer is unavailable");
    co_return finalize_bridge_commit({});
  }

  const auto readback_manager = gfx_->GetReadbackManager();
  CHECK_NOTNULL_F(readback_manager.get(),
    "VsmShadowRenderer page-request bridge requires ReadbackManager");

  const auto expected_readback_byte_count
    = static_cast<std::size_t>(seam.current_frame.total_page_table_entry_count)
    * sizeof(VsmShaderPageRequestFlags);
  const auto readback_range = graphics::BufferRange { 0U,
    static_cast<std::uint64_t>(expected_readback_byte_count) };
  auto readback = readback_manager->CreateBufferReadback(
    "VsmShadowRenderer.PageRequestFlagsReadback");
  if (readback == nullptr) {
    LOG_F(ERROR,
      "VsmShadowRenderer: page-request bridge failed to create a buffer "
      "readback object");
    co_return finalize_bridge_commit({});
  }

  auto readback_ticket = graphics::ReadbackTicket {};
  {
    auto readback_recorder = gfx_->AcquireCommandRecorder(
      queue_key, "VsmShadowRenderer.PageRequestFlagsReadback");
    CHECK_NOTNULL_F(readback_recorder.get(),
      "VsmShadowRenderer page-request flag readback requires a command "
      "recorder");
    readback_recorder->BeginTrackingResourceState(
      *request_flags_buffer, graphics::ResourceStates::kCommon, true);
    const auto enqueue_result = readback->EnqueueCopy(
      *readback_recorder, *request_flags_buffer, readback_range);
    if (!enqueue_result.has_value()) {
      LOG_F(ERROR,
        "VsmShadowRenderer: page-request bridge failed to enqueue the flag "
        "readback for frame_generation={} error={}",
        seam.current_frame.frame_generation, enqueue_result.error());
      co_return finalize_bridge_commit({});
    }
    readback_ticket = *enqueue_result;
  }

  const auto readback_deadline
    = std::chrono::steady_clock::now() + kPageRequestReadbackWaitBudget;
  while (true) {
    const auto ready_result = readback->IsReady();
    if (!ready_result.has_value()) {
      LOG_F(ERROR,
        "VsmShadowRenderer: page-request readback readiness check failed for "
        "frame_generation={} ticket={} fence={} error={}",
        seam.current_frame.frame_generation, readback_ticket.id,
        readback_ticket.fence.get(), ready_result.error());
      co_return finalize_bridge_commit({});
    }
    if (*ready_result) {
      break;
    }

    if (std::chrono::steady_clock::now() >= readback_deadline) {
      LOG_F(ERROR,
        "VsmShadowRenderer: page-request readback timed out after {} ms for "
        "frame_generation={} ticket={} fence={} projection_count={} "
        "virtual_page_count={}",
        kPageRequestReadbackWaitBudget.count(),
        seam.current_frame.frame_generation, readback_ticket.id,
        readback_ticket.fence.get(), current_projection_records.size(),
        seam.current_frame.total_page_table_entry_count);
      co_return finalize_bridge_commit({});
    }

    std::this_thread::sleep_for(kPageRequestReadbackPollInterval);
  }

  const auto mapped_readback = readback->TryMap();
  if (!mapped_readback.has_value()) {
    LOG_F(ERROR,
      "VsmShadowRenderer: page-request readback map failed for "
      "frame_generation={} ticket={} fence={} error={}",
      seam.current_frame.frame_generation, readback_ticket.id,
      readback_ticket.fence.get(), mapped_readback.error());
    co_return finalize_bridge_commit({});
  }

  const auto readback_bytes = mapped_readback->Bytes();
  if (readback_bytes.size() != expected_readback_byte_count) {
    LOG_F(ERROR,
      "VsmShadowRenderer: page-request readback size mismatch for "
      "frame_generation={} expected_bytes={} actual_bytes={}",
      seam.current_frame.frame_generation, expected_readback_byte_count,
      readback_bytes.size());
    co_return finalize_bridge_commit({});
  }

  const auto* shader_flags
    = reinterpret_cast<const VsmShaderPageRequestFlags*>(readback_bytes.data());
  const auto page_requests
    = DecodePageRequestsFromGpuFlags(current_projection_records,
      std::span<const VsmShaderPageRequestFlags>(
        shader_flags, seam.current_frame.total_page_table_entry_count));

  LOG_F(INFO,
    "VsmShadowRenderer: page-request readback bridge committed frame "
    "generation={} projection_count={} request_count={}",
    seam.current_frame.frame_generation, current_projection_records.size(),
    page_requests.size());

  co_return finalize_bridge_commit(page_requests);
}

auto VsmShadowRenderer::TryGetPreparedViewState(
  const ViewId view_id) const noexcept -> const PreparedViewState*
{
  const auto it = prepared_views_.find(view_id);
  return it != prepared_views_.end() ? &it->second : nullptr;
}

auto VsmShadowRenderer::BuildPreparedViewProducts(const ViewId view_id)
  -> std::optional<PreparedViewProducts>
{
  const auto* prepared_view = TryGetPreparedViewState(view_id);
  if (prepared_view == nullptr) {
    return std::nullopt;
  }

  const auto& current_virtual_frame = BuildCurrentVirtualFrame(*prepared_view);
  return PreparedViewProducts {
    .virtual_frame = current_virtual_frame,
    .seam = BuildCurrentSeam(current_virtual_frame),
    .projection_records = BuildCurrentProjectionRecords(
      *prepared_view, current_virtual_frame, quality_tier_),
  };
}

auto VsmShadowRenderer::GetCacheManager() noexcept -> VsmCacheManager&
{
  return cache_manager_;
}

auto VsmShadowRenderer::GetPhysicalPagePoolManager() noexcept
  -> VsmPhysicalPagePoolManager&
{
  return physical_page_pool_manager_;
}

auto VsmShadowRenderer::GetVirtualAddressSpace() noexcept
  -> VsmVirtualAddressSpace&
{
  return virtual_address_space_;
}

auto VsmShadowRenderer::GetSceneInvalidationCoordinator() noexcept
  -> VsmSceneInvalidationCoordinator&
{
  return invalidation_coordinator_;
}

auto VsmShadowRenderer::GetPageRequestGeneratorPass() noexcept
  -> observer_ptr<engine::VsmPageRequestGeneratorPass>
{
  return observer_ptr { page_request_generator_pass_.get() };
}

auto VsmShadowRenderer::GetInvalidationPass() noexcept
  -> observer_ptr<engine::VsmInvalidationPass>
{
  return observer_ptr { invalidation_pass_.get() };
}

auto VsmShadowRenderer::GetPageManagementPass() noexcept
  -> observer_ptr<engine::VsmPageManagementPass>
{
  return observer_ptr { page_management_pass_.get() };
}

auto VsmShadowRenderer::GetPageFlagPropagationPass() noexcept
  -> observer_ptr<engine::VsmPageFlagPropagationPass>
{
  return observer_ptr { page_flag_propagation_pass_.get() };
}

auto VsmShadowRenderer::GetPageInitializationPass() noexcept
  -> observer_ptr<engine::VsmPageInitializationPass>
{
  return observer_ptr { page_initialization_pass_.get() };
}

auto VsmShadowRenderer::GetShadowRasterizerPass() noexcept
  -> observer_ptr<engine::VsmShadowRasterizerPass>
{
  return observer_ptr { shadow_rasterizer_pass_.get() };
}

auto VsmShadowRenderer::GetStaticDynamicMergePass() noexcept
  -> observer_ptr<engine::VsmStaticDynamicMergePass>
{
  return observer_ptr { static_dynamic_merge_pass_.get() };
}

auto VsmShadowRenderer::GetHzbUpdaterPass() noexcept
  -> observer_ptr<engine::VsmHzbUpdaterPass>
{
  return observer_ptr { hzb_updater_pass_.get() };
}

auto VsmShadowRenderer::GetProjectionPass() noexcept
  -> observer_ptr<engine::VsmProjectionPass>
{
  return observer_ptr { projection_pass_.get() };
}

} // namespace oxygen::renderer::vsm
