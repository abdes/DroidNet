//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

#include "VirtualShadowLiveSceneHarness.h"

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::DecodePhysicalPageIndex;
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::testing::HarnessShadowSliceSnapshot;
using oxygen::renderer::vsm::testing::HarnessSingleChannelTextureSnapshot;
using oxygen::renderer::vsm::testing::TwoBoxLiveShellProjectionResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowProjectionResult;
using oxygen::renderer::vsm::testing::TwoBoxShadowSceneData;
using oxygen::renderer::vsm::testing::VsmLiveSceneHarness;

struct AxisAlignedBox {
  glm::vec3 min {};
  glm::vec3 max {};
};

struct ProbeSample {
  glm::vec3 point_ws {};
  glm::uvec2 pixel {};
};

struct CpuProjectedSample {
  glm::vec2 atlas_uv {};
  float receiver_depth { 1.0F };
  std::uint32_t physical_page_index { 0U };
  std::uint32_t atlas_slice { 0U };
  glm::uvec2 atlas_texel_origin { 0U, 0U };
};

struct CpuProjectionVisibility {
  float directional { 1.0F };
  float composite { 1.0F };
  bool has_directional_sample { false };
};

struct CpuDirectionalRouteDebug {
  float visibility { 1.0F };
  float dynamic_before_visibility { 1.0F };
  float static_before_visibility { 1.0F };
  float dynamic_after_visibility { 1.0F };
  float receiver_depth { 1.0F };
  std::array<float, 4> tap_depths { 1.0F, 1.0F, 1.0F, 1.0F };
  float tall_box_min_depth { 1.0F };
  float tall_box_max_depth { 1.0F };
  std::uint32_t level { std::numeric_limits<std::uint32_t>::max() };
  std::uint32_t page_table_index { std::numeric_limits<std::uint32_t>::max() };
  std::uint32_t physical_page_index {
    std::numeric_limits<std::uint32_t>::max()
  };
  std::uint32_t indirect_command_count { 0U };
  std::array<std::uint32_t, 4> indirect_draw_indices {
    std::numeric_limits<std::uint32_t>::max(),
    std::numeric_limits<std::uint32_t>::max(),
    std::numeric_limits<std::uint32_t>::max(),
    std::numeric_limits<std::uint32_t>::max(),
  };
  std::uint32_t covered_texel_count { 0U };
  float page_min_depth { 1.0F };
  glm::uvec2 sample_texel { 0U, 0U };
  glm::uvec2 covered_min { 0U, 0U };
  glm::uvec2 covered_max { 0U, 0U };
  glm::uvec2 page { 0U, 0U };
  bool has_sample { false };
};

struct CpuDirectionalRouteCandidate {
  std::uint32_t level { std::numeric_limits<std::uint32_t>::max() };
  std::uint32_t page_table_index { std::numeric_limits<std::uint32_t>::max() };
  std::uint32_t physical_page_index {
    std::numeric_limits<std::uint32_t>::max()
  };
  float visibility { 1.0F };
  float receiver_depth { 1.0F };
  std::array<float, 4> tap_depths { 1.0F, 1.0F, 1.0F, 1.0F };
};

class VsmShadowProjectionLiveSceneTest : public VsmLiveSceneHarness {
protected:
  static constexpr auto kOutputWidth = 256U;
  static constexpr auto kOutputHeight = 256U;
  static constexpr auto kTextureUploadRowPitch = 256U;
  static constexpr auto kShadowCasterContentHash = 0xB0A5ULL;
  static constexpr auto kVisibilityTolerance = 0.08F;

  [[nodiscard]] static auto MakeDirectionalView() -> oxygen::ResolvedView
  {
    return MakeLookAtResolvedView(glm::vec3 { -3.2F, 3.4F, 5.8F },
      glm::vec3 { 0.2F, 0.8F, 0.0F }, kOutputWidth, kOutputHeight);
  }

  [[nodiscard]] static auto MakeDirectionalSunDirection() -> glm::vec3
  {
    return glm::normalize(glm::vec3 { 0.40558F, -0.40558F, -0.819152F });
  }

  [[nodiscard]] static auto ComputeWorldAabb(const glm::mat4& world,
    const AxisAlignedBox& local_bounds) -> AxisAlignedBox
  {
    auto world_bounds = AxisAlignedBox {
      .min = glm::vec3 { std::numeric_limits<float>::max() },
      .max = glm::vec3 { std::numeric_limits<float>::lowest() },
    };
    for (std::uint32_t mask = 0U; mask < 8U; ++mask) {
      const auto local_corner = glm::vec3 {
        (mask & 1U) != 0U ? local_bounds.max.x : local_bounds.min.x,
        (mask & 2U) != 0U ? local_bounds.max.y : local_bounds.min.y,
        (mask & 4U) != 0U ? local_bounds.max.z : local_bounds.min.z,
      };
      const auto corner_ws = glm::vec3(world * glm::vec4(local_corner, 1.0F));
      world_bounds.min = glm::min(world_bounds.min, corner_ws);
      world_bounds.max = glm::max(world_bounds.max, corner_ws);
    }
    return world_bounds;
  }

  [[nodiscard]] static auto RayAabbIntersectionLength(const glm::vec3 origin,
    const glm::vec3 direction, const glm::vec3 box_min, const glm::vec3 box_max,
    const float max_distance = std::numeric_limits<float>::infinity()) -> float
  {
    auto t_min = 0.0F;
    auto t_max = max_distance;
    for (auto axis = 0; axis < 3; ++axis) {
      const auto dir = direction[axis];
      if (std::abs(dir) < 1.0e-6F) {
        if (origin[axis] < box_min[axis] || origin[axis] > box_max[axis]) {
          return 0.0F;
        }
        continue;
      }

      auto t0 = (box_min[axis] - origin[axis]) / dir;
      auto t1 = (box_max[axis] - origin[axis]) / dir;
      if (t0 > t1) {
        std::swap(t0, t1);
      }
      t_min = std::max(t_min, t0);
      t_max = std::min(t_max, t1);
      if (t_min > t_max) {
        return 0.0F;
      }
    }

    return t_max >= t_min && t_min <= max_distance
      ? std::max(0.0F, t_max - t_min)
      : 0.0F;
  }

  static auto DisableDirectionalShadowCasts(TwoBoxShadowSceneData& scene_data)
    -> void
  {
    auto sun_impl = scene_data.sun_node.GetImpl();
    ASSERT_TRUE(sun_impl.has_value());
    auto& sun_light
      = sun_impl->get().GetComponent<oxygen::scene::DirectionalLight>();
    sun_light.Common().casts_shadows = false;
    UpdateTransforms(*scene_data.scene, scene_data.sun_node);
  }

  [[nodiscard]] static auto AnalyticShadowPenetrationLength(
    const TwoBoxShadowSceneData& scene_data, const glm::vec3& point_ws) -> float
  {
    const auto tall_box_bounds = AxisAlignedBox {
      .min = glm::vec3 { 0.55F, 0.0F, -0.65F },
      .max = glm::vec3 { 1.35F, 3.2F, 0.15F },
    };
    const auto short_box_bounds = AxisAlignedBox {
      .min = glm::vec3 { -0.95F, 0.0F, 0.25F },
      .max = glm::vec3 { -0.15F, 1.1F, 1.05F },
    };

    constexpr auto kShadowBias = 0.02F;
    const auto toward_light = -scene_data.sun_direction_ws;
    const auto origin = point_ws + toward_light * kShadowBias;
    return std::max(RayAabbIntersectionLength(origin, toward_light,
                      tall_box_bounds.min, tall_box_bounds.max),
      RayAabbIntersectionLength(
        origin, toward_light, short_box_bounds.min, short_box_bounds.max));
  }

  [[nodiscard]] static auto IsShadowedByAnalyticBoxes(
    const TwoBoxShadowSceneData& scene_data, const glm::vec3& point_ws) -> bool
  {
    return AnalyticShadowPenetrationLength(scene_data, point_ws) > 0.0F;
  }

  [[nodiscard]] auto SelectAnalyticFloorProbes(
    const TwoBoxShadowSceneData& scene_data,
    const oxygen::ResolvedView& resolved_view,
    const std::size_t max_shadow_count = 4U,
    const std::size_t max_lit_count = 4U) const
    -> std::pair<std::vector<ProbeSample>, std::vector<ProbeSample>>
  {
    const auto tall_box_bounds = AxisAlignedBox {
      .min = glm::vec3 { 0.55F, 0.0F, -0.65F },
      .max = glm::vec3 { 1.35F, 3.2F, 0.15F },
    };
    const auto short_box_bounds = AxisAlignedBox {
      .min = glm::vec3 { -0.95F, 0.0F, 0.25F },
      .max = glm::vec3 { -0.15F, 1.1F, 1.05F },
    };
    constexpr auto kFloorLocalBounds = AxisAlignedBox {
      .min = glm::vec3 { -4.5F, 0.0F, -4.5F },
      .max = glm::vec3 { 4.5F, 0.0F, 4.5F },
    };
    const auto floor_bounds
      = ComputeWorldAabb(scene_data.world_matrices[0], kFloorLocalBounds);
    const auto inverse_view_projection = glm::inverse(
      resolved_view.ProjectionMatrix() * resolved_view.ViewMatrix());

    auto raycast_distance_to_aabb
      = [](const glm::vec3 origin, const glm::vec3 direction,
          const AxisAlignedBox& box) -> std::optional<float> {
      auto t_min = 0.0F;
      auto t_max = std::numeric_limits<float>::infinity();
      for (auto axis = 0; axis < 3; ++axis) {
        const auto dir = direction[axis];
        if (std::abs(dir) < 1.0e-6F) {
          if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) {
            return std::nullopt;
          }
          continue;
        }

        auto t0 = (box.min[axis] - origin[axis]) / dir;
        auto t1 = (box.max[axis] - origin[axis]) / dir;
        if (t0 > t1) {
          std::swap(t0, t1);
        }
        t_min = std::max(t_min, t0);
        t_max = std::min(t_max, t1);
        if (t_min > t_max) {
          return std::nullopt;
        }
      }

      return t_max >= t_min ? std::optional<float> { t_min } : std::nullopt;
    };
    auto raycast_distance_to_floor
      = [&](const glm::vec3 origin,
          const glm::vec3 direction) -> std::optional<float> {
      if (std::abs(direction.y) < 1.0e-6F) {
        return std::nullopt;
      }
      const auto distance = (floor_bounds.min.y - origin.y) / direction.y;
      if (distance <= 0.0F) {
        return std::nullopt;
      }
      const auto hit = origin + direction * distance;
      if (hit.x < floor_bounds.min.x || hit.x > floor_bounds.max.x
        || hit.z < floor_bounds.min.z || hit.z > floor_bounds.max.z) {
        return std::nullopt;
      }
      return distance;
    };
    auto pixel_center_ray
      = [&](const std::uint32_t x,
          const std::uint32_t y) -> std::pair<glm::vec3, glm::vec3> {
      const auto ndc_x = (2.0F * (static_cast<float>(x) + 0.5F)
                           / static_cast<float>(kOutputWidth))
        - 1.0F;
      const auto ndc_y = 1.0F
        - (2.0F * (static_cast<float>(y) + 0.5F)
          / static_cast<float>(kOutputHeight));
      auto near_point
        = inverse_view_projection * glm::vec4 { ndc_x, ndc_y, 0.0F, 1.0F };
      auto far_point
        = inverse_view_projection * glm::vec4 { ndc_x, ndc_y, 1.0F, 1.0F };
      near_point /= near_point.w;
      far_point /= far_point.w;
      const auto origin = glm::vec3 { near_point };
      const auto direction
        = glm::normalize(glm::vec3 { far_point - near_point });
      return { origin, direction };
    };
    auto visible_floor_pixels = std::vector<ProbeSample> {};
    visible_floor_pixels.reserve(kOutputWidth * kOutputHeight);
    for (std::uint32_t y = 0U; y < kOutputHeight; ++y) {
      for (std::uint32_t x = 0U; x < kOutputWidth; ++x) {
        const auto [ray_origin, ray_direction] = pixel_center_ray(x, y);
        auto nearest_distance = std::numeric_limits<float>::infinity();
        auto hit_point = std::optional<glm::vec3> {};
        auto hit_floor = false;

        if (const auto floor_distance
          = raycast_distance_to_floor(ray_origin, ray_direction);
          floor_distance.has_value() && *floor_distance < nearest_distance) {
          nearest_distance = *floor_distance;
          hit_point = ray_origin + ray_direction * *floor_distance;
          hit_floor = true;
        }
        if (const auto tall_distance = raycast_distance_to_aabb(
              ray_origin, ray_direction, tall_box_bounds);
          tall_distance.has_value() && *tall_distance < nearest_distance) {
          nearest_distance = *tall_distance;
          hit_point = ray_origin + ray_direction * *tall_distance;
          hit_floor = false;
        }
        if (const auto short_distance = raycast_distance_to_aabb(
              ray_origin, ray_direction, short_box_bounds);
          short_distance.has_value() && *short_distance < nearest_distance) {
          nearest_distance = *short_distance;
          hit_point = ray_origin + ray_direction * *short_distance;
          hit_floor = false;
        }

        if (hit_point.has_value() && hit_floor) {
          visible_floor_pixels.push_back(
            ProbeSample { .point_ws = *hit_point, .pixel = { x, y } });
        }
      }
    }

    auto shadow_probes = std::vector<ProbeSample> {};
    auto lit_probes = std::vector<ProbeSample> {};
    for (const auto& probe : visible_floor_pixels) {
      if (probe.pixel.x < 12U || probe.pixel.x > (kOutputWidth - 13U)
        || probe.pixel.y < 12U || probe.pixel.y > (kOutputHeight - 13U)) {
        continue;
      }

      if (IsShadowedByAnalyticBoxes(scene_data, probe.point_ws)) {
        if (shadow_probes.size() < max_shadow_count) {
          shadow_probes.push_back(probe);
        }
      } else if (lit_probes.size() < max_lit_count) {
        lit_probes.push_back(probe);
      }

      if (shadow_probes.size() >= max_shadow_count
        && lit_probes.size() >= max_lit_count) {
        break;
      }
    }

    return { shadow_probes, lit_probes };
  }

  [[nodiscard]] auto SelectInteriorVisibleFloorProbes(
    const TwoBoxShadowSceneData& scene_data,
    const oxygen::graphics::Texture& scene_depth_texture,
    const oxygen::ResolvedView& resolved_view, const std::size_t max_count)
    -> std::vector<ProbeSample>
  {
    constexpr auto kFloorLocalBounds = AxisAlignedBox {
      .min = glm::vec3 { -4.5F, 0.0F, -4.5F },
      .max = glm::vec3 { 4.5F, 0.0F, 4.5F },
    };
    const auto floor_bounds
      = ComputeWorldAabb(scene_data.world_matrices[0], kFloorLocalBounds);
    const auto probes = ReadDepthTextureSamples(scene_depth_texture,
      resolved_view, "stage-fifteen-two-box.visible-floor");
    auto selected = std::vector<ProbeSample> {};
    selected.reserve(max_count);
    for (const auto& sample : probes) {
      if (std::abs(sample.world_position_ws.y - floor_bounds.min.y) > 0.05F
        || sample.world_position_ws.x < floor_bounds.min.x
        || sample.world_position_ws.x > floor_bounds.max.x
        || sample.world_position_ws.z < floor_bounds.min.z
        || sample.world_position_ws.z > floor_bounds.max.z) {
        continue;
      }
      const auto clip = resolved_view.ProjectionMatrix()
        * resolved_view.ViewMatrix()
        * glm::vec4(sample.world_position_ws, 1.0F);
      if (std::abs(clip.w) <= 1.0e-6F) {
        continue;
      }
      const auto ndc = glm::vec3(clip) / clip.w;
      const auto pixel = glm::uvec2 {
        static_cast<std::uint32_t>(
          std::clamp((ndc.x * 0.5F + 0.5F) * static_cast<float>(kOutputWidth),
            0.0F, static_cast<float>(kOutputWidth - 1U))),
        static_cast<std::uint32_t>(
          std::clamp((0.5F - ndc.y * 0.5F) * static_cast<float>(kOutputHeight),
            0.0F, static_cast<float>(kOutputHeight - 1U))),
      };
      if (pixel.x < 12U || pixel.x > (kOutputWidth - 13U) || pixel.y < 12U
        || pixel.y > (kOutputHeight - 13U)) {
        continue;
      }
      selected.push_back(
        ProbeSample { .point_ws = sample.world_position_ws, .pixel = pixel });
      if (selected.size() >= max_count) {
        break;
      }
    }
    return selected;
  }

  [[nodiscard]] auto SelectStableAnalyticFloorProbes(
    const TwoBoxShadowSceneData& scene_data,
    const oxygen::graphics::Texture& scene_depth_texture,
    const oxygen::ResolvedView& resolved_view,
    const std::size_t max_shadow_count, const std::size_t max_lit_count,
    const std::uint32_t neighborhood_radius = 1U)
    -> std::pair<std::vector<ProbeSample>, std::vector<ProbeSample>>
  {
    constexpr auto kFloorLocalBounds = AxisAlignedBox {
      .min = glm::vec3 { -4.5F, 0.0F, -4.5F },
      .max = glm::vec3 { 4.5F, 0.0F, 4.5F },
    };
    const auto floor_bounds
      = ComputeWorldAabb(scene_data.world_matrices[0], kFloorLocalBounds);
    const auto samples = ReadDepthTextureSamples(scene_depth_texture,
      resolved_view, "stage-fifteen-two-box.analytic-floor-selection");

    struct StableProbeCandidate {
      ProbeSample probe {};
      std::optional<bool> analytically_shadowed;
      float analytic_shadow_penetration { 0.0F };
      float receiver_view_depth { 0.0F };
    };

    auto candidates = std::vector<std::optional<StableProbeCandidate>>(
      kOutputWidth * kOutputHeight);
    for (const auto& sample : samples) {
      const auto clip = resolved_view.ProjectionMatrix()
        * resolved_view.ViewMatrix()
        * glm::vec4(sample.world_position_ws, 1.0F);
      if (std::abs(clip.w) <= 1.0e-6F) {
        continue;
      }
      const auto ndc = glm::vec3(clip) / clip.w;
      const auto pixel = glm::uvec2 {
        static_cast<std::uint32_t>(
          std::clamp((ndc.x * 0.5F + 0.5F) * static_cast<float>(kOutputWidth),
            0.0F, static_cast<float>(kOutputWidth - 1U))),
        static_cast<std::uint32_t>(
          std::clamp((0.5F - ndc.y * 0.5F) * static_cast<float>(kOutputHeight),
            0.0F, static_cast<float>(kOutputHeight - 1U))),
      };
      if (std::abs(sample.world_position_ws.y - floor_bounds.min.y) > 0.05F
        || sample.world_position_ws.x < floor_bounds.min.x
        || sample.world_position_ws.x > floor_bounds.max.x
        || sample.world_position_ws.z < floor_bounds.min.z
        || sample.world_position_ws.z > floor_bounds.max.z) {
        continue;
      }
      if (pixel.x < 12U || pixel.x > (kOutputWidth - 13U) || pixel.y < 12U
        || pixel.y > (kOutputHeight - 13U)) {
        continue;
      }

      const auto receiver_view = resolved_view.ViewMatrix()
        * glm::vec4(sample.world_position_ws, 1.0F);
      const auto analytic_shadow_penetration
        = AnalyticShadowPenetrationLength(scene_data, sample.world_position_ws);

      std::optional<bool> analytically_shadowed;
      if (analytic_shadow_penetration > 0.35F) {
        analytically_shadowed = true;
      } else if (analytic_shadow_penetration == 0.0F) {
        analytically_shadowed = false;
      }

      candidates[pixel.y * kOutputWidth + pixel.x]
        = StableProbeCandidate {
          .probe = ProbeSample {
            .point_ws = sample.world_position_ws,
            .pixel = pixel,
          },
          .analytically_shadowed = analytically_shadowed,
          .analytic_shadow_penetration = analytic_shadow_penetration,
          .receiver_view_depth = -receiver_view.z,
        };
    }

    const auto has_stable_neighborhood
      = [&](const std::uint32_t x, const std::uint32_t y,
          const bool analytically_shadowed) {
          for (auto offset_y = -static_cast<int>(neighborhood_radius);
            offset_y <= static_cast<int>(neighborhood_radius); ++offset_y) {
            for (auto offset_x = -static_cast<int>(neighborhood_radius);
              offset_x <= static_cast<int>(neighborhood_radius); ++offset_x) {
              const auto neighbor_x = static_cast<int>(x) + offset_x;
              const auto neighbor_y = static_cast<int>(y) + offset_y;
              if (neighbor_x < 0 || neighbor_x >= static_cast<int>(kOutputWidth)
                || neighbor_y < 0
                || neighbor_y >= static_cast<int>(kOutputHeight)) {
                return false;
              }

              const auto& neighbor
                = candidates[static_cast<std::size_t>(neighbor_y) * kOutputWidth
                  + static_cast<std::size_t>(neighbor_x)];
              if (!neighbor.has_value()
                || !neighbor->analytically_shadowed.has_value()
                || neighbor->analytically_shadowed.value()
                  != analytically_shadowed) {
                return false;
              }
            }
          }
          return true;
        };

    auto stable_shadow = std::vector<StableProbeCandidate> {};
    auto stable_lit = std::vector<StableProbeCandidate> {};
    stable_shadow.reserve(max_shadow_count * 4U);
    stable_lit.reserve(max_lit_count * 4U);
    for (std::uint32_t y = 12U; y < kOutputHeight - 12U; ++y) {
      for (std::uint32_t x = 12U; x < kOutputWidth - 12U; ++x) {
        const auto& candidate = candidates[y * kOutputWidth + x];
        if (!candidate.has_value()
          || !candidate->analytically_shadowed.has_value()
          || !has_stable_neighborhood(
            x, y, candidate->analytically_shadowed.value())) {
          continue;
        }

        if (candidate->analytically_shadowed.value()) {
          stable_shadow.push_back(*candidate);
        } else {
          stable_lit.push_back(*candidate);
        }
      }
    }

    std::sort(stable_shadow.begin(), stable_shadow.end(),
      [](const auto& lhs, const auto& rhs) {
        if (lhs.analytic_shadow_penetration
          != rhs.analytic_shadow_penetration) {
          return lhs.analytic_shadow_penetration
            > rhs.analytic_shadow_penetration;
        }
        return lhs.receiver_view_depth > rhs.receiver_view_depth;
      });
    std::sort(stable_lit.begin(), stable_lit.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.receiver_view_depth > rhs.receiver_view_depth;
      });

    auto shadow_probes = std::vector<ProbeSample> {};
    auto lit_probes = std::vector<ProbeSample> {};
    shadow_probes.reserve(max_shadow_count);
    lit_probes.reserve(max_lit_count);
    for (std::size_t i = 0;
      i < (std::min)(max_shadow_count, stable_shadow.size()); ++i) {
      shadow_probes.push_back(stable_shadow[i].probe);
    }
    for (std::size_t i = 0; i < (std::min)(max_lit_count, stable_lit.size());
      ++i) {
      lit_probes.push_back(stable_lit[i].probe);
    }

    return { shadow_probes, lit_probes };
  }

  [[nodiscard]] static auto TryProjectMappedSampleCpu(
    const VsmPageRequestProjection& projection,
    const std::span<const VsmShaderPageTableEntry> page_table,
    const glm::vec3& world_position_ws, const glm::mat4& main_view_matrix,
    const std::uint32_t tiles_per_axis, const std::uint32_t page_size_texels)
    -> std::optional<CpuProjectedSample>
  {
    if (projection.map_id == 0U || projection.pages_x == 0U
      || projection.pages_y == 0U || projection.map_pages_x == 0U
      || projection.map_pages_y == 0U || projection.level_count == 0U
      || page_size_texels == 0U || tiles_per_axis == 0U) {
      return std::nullopt;
    }

    if (projection.projection.clipmap_level >= projection.level_count
      || projection.page_offset_x > projection.map_pages_x
      || projection.page_offset_y > projection.map_pages_y
      || projection.pages_x > projection.map_pages_x - projection.page_offset_x
      || projection.pages_y
        > projection.map_pages_y - projection.page_offset_y) {
      return std::nullopt;
    }

    if (projection.projection.light_type
      == static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional)) {
      const auto receiver_view
        = main_view_matrix * glm::vec4(world_position_ws, 1.0F);
      const auto receiver_view_depth = -receiver_view.z;
      const auto min_depth
        = projection.projection.receiver_depth_range_pad.x - 1.0e-3F;
      const auto max_depth
        = projection.projection.receiver_depth_range_pad.y + 1.0e-3F;
      if (receiver_view_depth < min_depth || receiver_view_depth > max_depth) {
        return std::nullopt;
      }
    }

    const auto world = glm::vec4(world_position_ws, 1.0F);
    const auto view = projection.projection.view_matrix * world;
    const auto clip = projection.projection.projection_matrix * view;
    if (std::abs(clip.w) <= 1.0e-6F || clip.w < 0.0F) {
      return std::nullopt;
    }

    const auto ndc = glm::vec3(clip) / clip.w;
    if (ndc.x < -1.0F || ndc.x > 1.0F || ndc.y < -1.0F || ndc.y > 1.0F
      || ndc.z < 0.0F || ndc.z > 1.0F) {
      return std::nullopt;
    }

    const auto uv = glm::vec2 {
      ndc.x * 0.5F + 0.5F,
      0.5F - ndc.y * 0.5F,
    };
    const auto local_page_x
      = (std::min)(static_cast<std::uint32_t>(uv.x * projection.pages_x),
        projection.pages_x - 1U);
    const auto local_page_y
      = (std::min)(static_cast<std::uint32_t>(uv.y * projection.pages_y),
        projection.pages_y - 1U);
    const auto page_x = projection.page_offset_x + local_page_x;
    const auto page_y = projection.page_offset_y + local_page_y;
    const auto pages_per_level
      = projection.map_pages_x * projection.map_pages_y;
    const auto page_table_index = projection.first_page_table_entry
      + projection.projection.clipmap_level * pages_per_level
      + page_y * projection.map_pages_x + page_x;
    if (page_table_index >= page_table.size()) {
      return std::nullopt;
    }

    const auto entry = page_table[page_table_index];
    if (!IsMapped(entry)) {
      return std::nullopt;
    }

    const auto physical_page_index = DecodePhysicalPageIndex(entry).value;
    const auto tiles_per_slice = tiles_per_axis * tiles_per_axis;
    const auto atlas_slice = physical_page_index / tiles_per_slice;
    const auto in_slice_index = physical_page_index % tiles_per_slice;
    const auto tile_x = in_slice_index % tiles_per_axis;
    const auto tile_y = in_slice_index / tiles_per_axis;
    const auto atlas_texel_origin = glm::uvec2 {
      tile_x * page_size_texels,
      tile_y * page_size_texels,
    };
    const auto page_uv = glm::fract(glm::vec2 {
      uv.x * static_cast<float>(projection.pages_x),
      uv.y * static_cast<float>(projection.pages_y),
    });
    const auto atlas_extent
      = static_cast<float>(tiles_per_axis * page_size_texels);
    const auto receiver_depth_bias
      = (std::max)(projection.projection.receiver_depth_range_pad.z, 0.0F);

    return CpuProjectedSample {
      .atlas_uv
      = (glm::vec2 { static_cast<float>(tile_x), static_cast<float>(tile_y) }
            * static_cast<float>(page_size_texels)
          + page_uv * static_cast<float>(page_size_texels))
        / atlas_extent,
      .receiver_depth = std::clamp(ndc.z - receiver_depth_bias, 0.0F, 1.0F),
      .physical_page_index = physical_page_index,
      .atlas_slice = atlas_slice,
      .atlas_texel_origin = atlas_texel_origin,
    };
  }

  [[nodiscard]] static auto SampleVisibilityPcf2x2(
    const HarnessShadowSliceSnapshot& slice, const glm::vec2 atlas_uv,
    const float receiver_depth, const glm::uvec2 atlas_texel_origin,
    const std::uint32_t page_size_texels) -> float
  {
    if (page_size_texels == 0U) {
      return 1.0F;
    }

    const auto pixel = atlas_uv
      * glm::vec2 { static_cast<float>(slice.width),
          static_cast<float>(slice.height) };
    const auto base
      = glm::ivec2 { glm::floor(pixel - glm::vec2 { 0.5F, 0.5F }) };
    const auto min_coord = glm::ivec2 {
      static_cast<int>(atlas_texel_origin.x),
      static_cast<int>(atlas_texel_origin.y),
    };
    const auto max_coord = glm::ivec2 {
      (std::min)(static_cast<int>(slice.width) - 1,
        static_cast<int>(atlas_texel_origin.x + page_size_texels - 1U)),
      (std::min)(static_cast<int>(slice.height) - 1,
        static_cast<int>(atlas_texel_origin.y + page_size_texels - 1U)),
    };

    auto visibility = 0.0F;
    for (auto y = 0; y < 2; ++y) {
      for (auto x = 0; x < 2; ++x) {
        const auto coord
          = glm::clamp(base + glm::ivec2 { x, y }, min_coord, max_coord);
        const auto stored_depth = slice.At(static_cast<std::uint32_t>(coord.x),
          static_cast<std::uint32_t>(coord.y));
        visibility += receiver_depth <= stored_depth ? 1.0F : 0.0F;
      }
    }

    return visibility * 0.25F;
  }

  [[nodiscard]] auto SelectShadowSlice(
    const TwoBoxShadowProjectionResult& result,
    const std::uint32_t atlas_slice) const -> const HarnessShadowSliceSnapshot*
  {
    const auto& pool
      = result.hzb.merge.rasterization.initialization.physical_pool;
    const auto dynamic_slice
      = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
    const auto static_slice
      = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kStaticDepth);
    if (dynamic_slice.has_value() && atlas_slice == *dynamic_slice) {
      return &result.hzb.merge.dynamic_after;
    }
    if (static_slice.has_value() && atlas_slice == *static_slice) {
      return &result.hzb.merge.static_after;
    }
    return nullptr;
  }

  [[nodiscard]] auto ComputeCpuVisibility(
    const TwoBoxShadowProjectionResult& result,
    const glm::mat4& main_view_matrix, const glm::vec3& world_position_ws) const
    -> CpuProjectionVisibility
  {
    const auto& frame = result.hzb.merge.rasterization.initialization
                          .propagation.mapping.bridge.committed_frame;
    const auto& projections = frame.snapshot.projection_records;
    const auto& page_table = result.hzb.page_table_after;
    const auto& pool
      = result.hzb.merge.rasterization.initialization.physical_pool;

    auto visibility = CpuProjectionVisibility {};
    auto best_level = std::numeric_limits<std::uint32_t>::max();
    for (const auto& projection : projections) {
      const auto sample
        = TryProjectMappedSampleCpu(projection, page_table, world_position_ws,
          main_view_matrix, pool.tiles_per_axis, pool.page_size_texels);
      if (!sample.has_value()) {
        continue;
      }

      const auto* slice = SelectShadowSlice(result, sample->atlas_slice);
      if (slice == nullptr) {
        continue;
      }

      const auto sample_visibility = SampleVisibilityPcf2x2(*slice,
        sample->atlas_uv, sample->receiver_depth, sample->atlas_texel_origin,
        pool.page_size_texels);
      if (projection.projection.light_type
        == static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional)) {
        if (projection.projection.clipmap_level < best_level) {
          best_level = projection.projection.clipmap_level;
          visibility.directional = sample_visibility;
          visibility.has_directional_sample = true;
        }
      } else if (projection.projection.light_type
        == static_cast<std::uint32_t>(VsmProjectionLightType::kLocal)) {
        visibility.composite *= sample_visibility;
      }
    }

    if (visibility.has_directional_sample) {
      visibility.composite *= visibility.directional;
    }
    return visibility;
  }

  [[nodiscard]] static auto ComputeProjectedDepthRange(
    const glm::mat4& view_matrix, const glm::mat4& projection_matrix,
    const glm::mat4& world_matrix, const AxisAlignedBox& local_bounds)
    -> std::pair<float, float>
  {
    auto min_depth = std::numeric_limits<float>::max();
    auto max_depth = std::numeric_limits<float>::lowest();
    for (std::uint32_t mask = 0U; mask < 8U; ++mask) {
      const auto local_corner = glm::vec3 {
        (mask & 1U) != 0U ? local_bounds.max.x : local_bounds.min.x,
        (mask & 2U) != 0U ? local_bounds.max.y : local_bounds.min.y,
        (mask & 4U) != 0U ? local_bounds.max.z : local_bounds.min.z,
      };
      const auto world_corner = world_matrix * glm::vec4(local_corner, 1.0F);
      const auto view_corner = view_matrix * world_corner;
      const auto clip_corner = projection_matrix * view_corner;
      if (std::abs(clip_corner.w) <= 1.0e-6F) {
        continue;
      }
      const auto ndc_z = clip_corner.z / clip_corner.w;
      min_depth = (std::min)(min_depth, ndc_z);
      max_depth = (std::max)(max_depth, ndc_z);
    }
    if (min_depth == std::numeric_limits<float>::max()) {
      return { 1.0F, 1.0F };
    }
    return { min_depth, max_depth };
  }

  [[nodiscard]] auto ComputeDirectionalRouteDebug(
    const TwoBoxShadowProjectionResult& result,
    const TwoBoxShadowSceneData& scene_data, const glm::mat4& main_view_matrix,
    const glm::vec3& world_position_ws) const -> CpuDirectionalRouteDebug
  {
    const auto& frame = result.hzb.merge.rasterization.initialization
                          .propagation.mapping.bridge.committed_frame;
    const auto& projections = frame.snapshot.projection_records;
    const auto& page_table = result.hzb.page_table_after;
    const auto& pool
      = result.hzb.merge.rasterization.initialization.physical_pool;

    auto best = CpuDirectionalRouteDebug {};
    for (const auto& projection : projections) {
      if (projection.projection.light_type
        != static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional)) {
        continue;
      }

      const auto sample
        = TryProjectMappedSampleCpu(projection, page_table, world_position_ws,
          main_view_matrix, pool.tiles_per_axis, pool.page_size_texels);
      if (!sample.has_value()) {
        continue;
      }

      const auto world = glm::vec4(world_position_ws, 1.0F);
      const auto view = projection.projection.view_matrix * world;
      const auto clip = projection.projection.projection_matrix * view;
      if (std::abs(clip.w) <= 1.0e-6F || clip.w < 0.0F) {
        continue;
      }

      const auto ndc = glm::vec3(clip) / clip.w;
      const auto uv = glm::vec2 {
        ndc.x * 0.5F + 0.5F,
        0.5F - ndc.y * 0.5F,
      };
      const auto local_page_x
        = (std::min)(static_cast<std::uint32_t>(uv.x * projection.pages_x),
          projection.pages_x - 1U);
      const auto local_page_y
        = (std::min)(static_cast<std::uint32_t>(uv.y * projection.pages_y),
          projection.pages_y - 1U);
      const auto page_x = projection.page_offset_x + local_page_x;
      const auto page_y = projection.page_offset_y + local_page_y;
      const auto pages_per_level
        = projection.map_pages_x * projection.map_pages_y;
      const auto page_table_index = projection.first_page_table_entry
        + projection.projection.clipmap_level * pages_per_level
        + page_y * projection.map_pages_x + page_x;

      const auto* slice = SelectShadowSlice(result, sample->atlas_slice);
      if (slice == nullptr) {
        continue;
      }

      const auto dynamic_after_visibility = SampleVisibilityPcf2x2(*slice,
        sample->atlas_uv, sample->receiver_depth, sample->atlas_texel_origin,
        pool.page_size_texels);
      const auto dynamic_before_visibility
        = SampleVisibilityPcf2x2(result.hzb.merge.dynamic_before,
          sample->atlas_uv, sample->receiver_depth, sample->atlas_texel_origin,
          pool.page_size_texels);
      const auto static_before_visibility
        = SampleVisibilityPcf2x2(result.hzb.merge.static_before,
          sample->atlas_uv, sample->receiver_depth, sample->atlas_texel_origin,
          pool.page_size_texels);
      auto indirect_command_count = 0U;
      auto indirect_draw_indices = std::array<std::uint32_t, 4> {
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
      };
      const auto& rasterization = result.hzb.merge.rasterization;
      if (!rasterization.indirect_command_counts.empty()) {
        const auto job_it = std::find_if(rasterization.prepared_pages.begin(),
          rasterization.prepared_pages.end(), [&](const auto& prepared_page) {
            return prepared_page.page_table_index == page_table_index;
          });
        if (job_it != rasterization.prepared_pages.end()) {
          const auto job_index = static_cast<std::size_t>(
            std::distance(rasterization.prepared_pages.begin(), job_it));
          if (job_index < rasterization.indirect_command_counts.size()) {
            indirect_command_count
              = rasterization.indirect_command_counts[job_index];
          }
          if (!rasterization.indirect_commands.empty()
            && rasterization.max_indirect_commands_per_page != 0U) {
            const auto command_base
              = job_index * rasterization.max_indirect_commands_per_page;
            const auto copy_count
              = (std::min)(static_cast<std::size_t>(indirect_command_count),
                indirect_draw_indices.size());
            for (std::size_t i = 0; i < copy_count; ++i) {
              const auto command_index = command_base + i;
              if (command_index < rasterization.indirect_commands.size()) {
                indirect_draw_indices[i]
                  = rasterization.indirect_commands[command_index].draw_index;
              }
            }
          }
        }
      }
      if (!best.has_sample
        || projection.projection.clipmap_level < best.level) {
        auto covered_texel_count = 0U;
        auto page_min_depth = 1.0F;
        auto tap_depths = std::array<float, 4> { 1.0F, 1.0F, 1.0F, 1.0F };
        constexpr auto kLocalCubeBounds = AxisAlignedBox {
          .min = glm::vec3 { -0.5F, 0.0F, -0.5F },
          .max = glm::vec3 { 0.5F, 1.0F, 0.5F },
        };
        const auto [tall_box_min_depth, tall_box_max_depth]
          = ComputeProjectedDepthRange(projection.projection.view_matrix,
            projection.projection.projection_matrix,
            scene_data.world_matrices[1], kLocalCubeBounds);
        const auto sample_pixel = sample->atlas_uv
          * glm::vec2 { static_cast<float>(slice->width),
              static_cast<float>(slice->height) };
        auto sample_texel = glm::uvec2 {
          static_cast<std::uint32_t>(sample_pixel.x)
            - sample->atlas_texel_origin.x,
          static_cast<std::uint32_t>(sample_pixel.y)
            - sample->atlas_texel_origin.y,
        };
        auto covered_min
          = glm::uvec2 { pool.page_size_texels, pool.page_size_texels };
        auto covered_max = glm::uvec2 { 0U, 0U };
        const auto base = glm::ivec2 {
          glm::floor(sample_pixel - glm::vec2 { 0.5F, 0.5F }),
        };
        const auto min_coord = glm::ivec2 {
          static_cast<int>(sample->atlas_texel_origin.x),
          static_cast<int>(sample->atlas_texel_origin.y),
        };
        const auto max_coord = glm::ivec2 {
          (std::min)(static_cast<int>(slice->width) - 1,
            static_cast<int>(
              sample->atlas_texel_origin.x + pool.page_size_texels - 1U)),
          (std::min)(static_cast<int>(slice->height) - 1,
            static_cast<int>(
              sample->atlas_texel_origin.y + pool.page_size_texels - 1U)),
        };
        for (auto y = 0; y < 2; ++y) {
          for (auto x = 0; x < 2; ++x) {
            const auto coord
              = glm::clamp(base + glm::ivec2 { x, y }, min_coord, max_coord);
            tap_depths[static_cast<std::size_t>(y * 2 + x)]
              = slice->At(static_cast<std::uint32_t>(coord.x),
                static_cast<std::uint32_t>(coord.y));
          }
        }
        for (auto y = 0U; y < pool.page_size_texels; ++y) {
          for (auto x = 0U; x < pool.page_size_texels; ++x) {
            const auto depth = slice->At(sample->atlas_texel_origin.x + x,
              sample->atlas_texel_origin.y + y);
            page_min_depth = (std::min)(page_min_depth, depth);
            if (depth < 0.9999F) {
              ++covered_texel_count;
              covered_min = glm::min(covered_min, glm::uvec2 { x, y });
              covered_max = glm::max(covered_max, glm::uvec2 { x, y });
            }
          }
        }
        if (covered_texel_count == 0U) {
          covered_min = glm::uvec2 { 0U, 0U };
          covered_max = glm::uvec2 { 0U, 0U };
        }
        best = CpuDirectionalRouteDebug {
          .visibility = dynamic_after_visibility,
          .dynamic_before_visibility = dynamic_before_visibility,
          .static_before_visibility = static_before_visibility,
          .dynamic_after_visibility = dynamic_after_visibility,
          .receiver_depth = sample->receiver_depth,
          .tap_depths = tap_depths,
          .tall_box_min_depth = tall_box_min_depth,
          .tall_box_max_depth = tall_box_max_depth,
          .level = projection.projection.clipmap_level,
          .page_table_index = page_table_index,
          .physical_page_index = sample->physical_page_index,
          .indirect_command_count = indirect_command_count,
          .indirect_draw_indices = indirect_draw_indices,
          .covered_texel_count = covered_texel_count,
          .page_min_depth = page_min_depth,
          .sample_texel = sample_texel,
          .covered_min = covered_min,
          .covered_max = covered_max,
          .page = { page_x, page_y },
          .has_sample = true,
        };
      }
    }

    return best;
  }

  [[nodiscard]] auto SummarizeDirectionalRouteCandidates(
    const TwoBoxShadowProjectionResult& result,
    const glm::mat4& main_view_matrix, const glm::vec3& world_position_ws) const
    -> std::string
  {
    const auto& frame = result.hzb.merge.rasterization.initialization
                          .propagation.mapping.bridge.committed_frame;
    const auto& projections = frame.snapshot.projection_records;
    const auto& page_table = result.hzb.page_table_after;
    const auto& pool
      = result.hzb.merge.rasterization.initialization.physical_pool;

    auto candidates = std::vector<CpuDirectionalRouteCandidate> {};
    for (const auto& projection : projections) {
      if (projection.projection.light_type
        != static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional)) {
        continue;
      }

      const auto sample
        = TryProjectMappedSampleCpu(projection, page_table, world_position_ws,
          main_view_matrix, pool.tiles_per_axis, pool.page_size_texels);
      if (!sample.has_value()) {
        continue;
      }

      const auto* slice = SelectShadowSlice(result, sample->atlas_slice);
      if (slice == nullptr) {
        continue;
      }

      const auto pixel = sample->atlas_uv
        * glm::vec2 { static_cast<float>(slice->width),
            static_cast<float>(slice->height) };
      const auto base
        = glm::ivec2 { glm::floor(pixel - glm::vec2 { 0.5F, 0.5F }) };
      const auto min_coord = glm::ivec2 {
        static_cast<int>(sample->atlas_texel_origin.x),
        static_cast<int>(sample->atlas_texel_origin.y),
      };
      const auto max_coord = glm::ivec2 {
        (std::min)(static_cast<int>(slice->width) - 1,
          static_cast<int>(
            sample->atlas_texel_origin.x + pool.page_size_texels - 1U)),
        (std::min)(static_cast<int>(slice->height) - 1,
          static_cast<int>(
            sample->atlas_texel_origin.y + pool.page_size_texels - 1U)),
      };
      auto tap_depths = std::array<float, 4> { 1.0F, 1.0F, 1.0F, 1.0F };
      for (auto y = 0; y < 2; ++y) {
        for (auto x = 0; x < 2; ++x) {
          const auto coord
            = glm::clamp(base + glm::ivec2 { x, y }, min_coord, max_coord);
          tap_depths[static_cast<std::size_t>(y * 2 + x)]
            = slice->At(static_cast<std::uint32_t>(coord.x),
              static_cast<std::uint32_t>(coord.y));
        }
      }

      const auto world = glm::vec4(world_position_ws, 1.0F);
      const auto view = projection.projection.view_matrix * world;
      const auto clip = projection.projection.projection_matrix * view;
      const auto ndc = glm::vec3(clip) / clip.w;
      const auto uv = glm::vec2 {
        ndc.x * 0.5F + 0.5F,
        0.5F - ndc.y * 0.5F,
      };
      const auto local_page_x
        = (std::min)(static_cast<std::uint32_t>(uv.x * projection.pages_x),
          projection.pages_x - 1U);
      const auto local_page_y
        = (std::min)(static_cast<std::uint32_t>(uv.y * projection.pages_y),
          projection.pages_y - 1U);
      const auto page_x = projection.page_offset_x + local_page_x;
      const auto page_y = projection.page_offset_y + local_page_y;
      const auto pages_per_level
        = projection.map_pages_x * projection.map_pages_y;
      const auto page_table_index = projection.first_page_table_entry
        + projection.projection.clipmap_level * pages_per_level
        + page_y * projection.map_pages_x + page_x;

      candidates.push_back(CpuDirectionalRouteCandidate {
        .level = projection.projection.clipmap_level,
        .page_table_index = page_table_index,
        .physical_page_index = sample->physical_page_index,
        .visibility = SampleVisibilityPcf2x2(*slice, sample->atlas_uv,
          sample->receiver_depth, sample->atlas_texel_origin,
          pool.page_size_texels),
        .receiver_depth = sample->receiver_depth,
        .tap_depths = tap_depths,
      });
    }

    if (candidates.empty()) {
      return "none";
    }

    std::ranges::sort(candidates,
      [](const auto& lhs, const auto& rhs) { return lhs.level < rhs.level; });

    auto stream = std::ostringstream {};
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      const auto& candidate = candidates[i];
      if (i != 0U) {
        stream << " | ";
      }
      stream << "level=" << candidate.level
             << " page_table_index=" << candidate.page_table_index
             << " physical_page=" << candidate.physical_page_index
             << " visibility=" << candidate.visibility
             << " receiver_depth=" << candidate.receiver_depth
             << " tap_depths=[" << candidate.tap_depths[0] << ", "
             << candidate.tap_depths[1] << ", " << candidate.tap_depths[2]
             << ", " << candidate.tap_depths[3] << "]";
    }
    return stream.str();
  }

  static auto ExpectMaskMatchesCpuProjection(
    const HarnessSingleChannelTextureSnapshot& mask,
    const std::vector<ProbeSample>& probes,
    const std::function<float(const ProbeSample&)>& expected_visibility) -> void
  {
    for (const auto& probe : probes) {
      const auto actual = mask.At(probe.pixel.x, probe.pixel.y);
      const auto expected = expected_visibility(probe);
      EXPECT_NEAR(actual, expected, kVisibilityTolerance)
        << "pixel=(" << probe.pixel.x << ", " << probe.pixel.y << ") world=("
        << probe.point_ws.x << ", " << probe.point_ws.y << ", "
        << probe.point_ws.z << ")";
    }
  }

  auto ReadOutputTexelDirect(
    const std::shared_ptr<const oxygen::graphics::Texture>& texture,
    const std::uint32_t x, const std::uint32_t y, std::string_view debug_name)
    -> float
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot read a null output texture");

    auto readback = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = kTextureUploadRowPitch,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Probe");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire probe recorder");
      EnsureTracked(*recorder,
        std::const_pointer_cast<oxygen::graphics::Texture>(texture),
        oxygen::graphics::ResourceStates::kCommon);
      EnsureTracked(
        *recorder, readback, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *readback, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *texture,
        oxygen::graphics::TextureBufferCopyRegion {
          .buffer_offset = oxygen::OffsetBytes { 0U },
          .buffer_row_pitch = oxygen::SizeBytes { kTextureUploadRowPitch },
          .texture_slice = {
            .x = x,
            .y = y,
            .z = 0U,
            .width = 1U,
            .height = 1U,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        });
    }
    WaitForQueueIdle();

    auto value = 0.0F;
    const auto* mapped = static_cast<const std::byte*>(
      readback->Map(0U, kTextureUploadRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  [[nodiscard]] auto SummarizeDarkestVisibleFloorProbes(
    const TwoBoxShadowSceneData& scene_data,
    const oxygen::graphics::Texture& scene_depth_texture,
    const oxygen::ResolvedView& resolved_view,
    const HarnessSingleChannelTextureSnapshot& mask,
    const std::size_t candidate_count = 128U,
    const std::size_t darkest_count = 8U) -> std::string
  {
    auto probes = SelectInteriorVisibleFloorProbes(
      scene_data, scene_depth_texture, resolved_view, candidate_count);
    std::sort(probes.begin(), probes.end(),
      [&](const ProbeSample& lhs, const ProbeSample& rhs) {
        return mask.At(lhs.pixel.x, lhs.pixel.y)
          < mask.At(rhs.pixel.x, rhs.pixel.y);
      });

    std::ostringstream stream;
    const auto count = (std::min)(darkest_count, probes.size());
    for (std::size_t i = 0; i < count; ++i) {
      const auto& probe = probes[i];
      if (i != 0U) {
        stream << " | ";
      }
      stream << "pixel=(" << probe.pixel.x << "," << probe.pixel.y << ")"
             << " world=(" << probe.point_ws.x << "," << probe.point_ws.y << ","
             << probe.point_ws.z << ")"
             << " sample=" << mask.At(probe.pixel.x, probe.pixel.y)
             << " analytic="
             << (IsShadowedByAnalyticBoxes(scene_data, probe.point_ws) ? 1 : 0);
    }
    return stream.str();
  }
};

NOLINT_TEST_F(VsmShadowProjectionLiveSceneTest,
  DirectionalTwoBoxSceneMatchesCpuProjectionFromRealStageInputs)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene_data = CreateTwoBoxShadowScene(MakeDirectionalSunDirection(), 1U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);
  const auto resolved_view = MakeDirectionalView();

  const auto result = RunTwoBoxShadowProjectionStage(*renderer, scene_data,
    vsm_renderer, resolved_view, kOutputWidth, kOutputHeight,
    SequenceNumber { 31U }, Slot { 0U }, kShadowCasterContentHash);

  ASSERT_FALSE(result.hzb.page_table_after.empty());
  ASSERT_FALSE(result.hzb.merge.rasterization.initialization.propagation.mapping
      .bridge.committed_frame.snapshot.projection_records.empty());

  const auto probes = SelectInteriorVisibleFloorProbes(scene_data,
    *result.hzb.merge.rasterization.initialization.propagation.mapping.bridge
      .scene_depth_texture,
    resolved_view, 12U);
  ASSERT_GE(probes.size(), 8U);

  ExpectMaskMatchesCpuProjection(result.output.directional_shadow_mask, probes,
    [&](const ProbeSample& probe) {
      return ComputeCpuVisibility(
        result, resolved_view.ViewMatrix(), probe.point_ws)
        .directional;
    });
  ExpectMaskMatchesCpuProjection(
    result.output.shadow_mask, probes, [&](const ProbeSample& probe) {
      return ComputeCpuVisibility(
        result, resolved_view.ViewMatrix(), probe.point_ws)
        .composite;
    });
}

NOLINT_TEST_F(VsmShadowProjectionLiveSceneTest,
  DirectionalTwoBoxLiveShellMatchesIsolatedProjectionAtAnalyticFloorProbes)
{
  auto isolated_renderer = MakeRenderer();
  ASSERT_NE(isolated_renderer, nullptr);
  auto isolated_scene
    = CreateTwoBoxShadowScene(MakeDirectionalSunDirection(), 1U);
  auto isolated_vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &isolated_renderer->GetStagingProvider() },
    oxygen::observer_ptr {
      &isolated_renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  auto live_renderer = MakeRenderer();
  ASSERT_NE(live_renderer, nullptr);
  auto live_scene = CreateTwoBoxShadowScene(MakeDirectionalSunDirection(), 1U);
  auto live_vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &live_renderer->GetStagingProvider() },
    oxygen::observer_ptr { &live_renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  const auto resolved_view = MakeDirectionalView();
  const auto isolated_result
    = RunTwoBoxShadowProjectionStage(*isolated_renderer, isolated_scene,
      isolated_vsm_renderer, resolved_view, kOutputWidth, kOutputHeight,
      SequenceNumber { 31U }, Slot { 0U }, kShadowCasterContentHash);
  const auto live_result = RunTwoBoxLiveShellProjectionFrame(*live_renderer,
    live_scene, live_vsm_renderer, resolved_view, kOutputWidth, kOutputHeight,
    SequenceNumber { 31U }, Slot { 0U }, kShadowCasterContentHash);

  const auto [shadow_probes, lit_probes]
    = SelectAnalyticFloorProbes(live_scene, resolved_view);
  ASSERT_GE(shadow_probes.size(), 2U);
  ASSERT_GE(lit_probes.size(), 2U);

  for (const auto& probe : shadow_probes) {
    EXPECT_NEAR(live_result.output.directional_shadow_mask.At(
                  probe.pixel.x, probe.pixel.y),
      isolated_result.output.directional_shadow_mask.At(
        probe.pixel.x, probe.pixel.y),
      kVisibilityTolerance)
      << "shadow probe pixel=(" << probe.pixel.x << ", " << probe.pixel.y
      << ")";
  }
  for (const auto& probe : lit_probes) {
    EXPECT_NEAR(live_result.output.directional_shadow_mask.At(
                  probe.pixel.x, probe.pixel.y),
      isolated_result.output.directional_shadow_mask.At(
        probe.pixel.x, probe.pixel.y),
      kVisibilityTolerance)
      << "lit probe pixel=(" << probe.pixel.x << ", " << probe.pixel.y << ")";
  }
}

NOLINT_TEST_F(VsmShadowProjectionLiveSceneTest,
  DirectionalSingleCascadeLiveShellDarkensStableFarAnalyticShadowProbes)
{
  auto isolated_renderer = MakeRenderer();
  ASSERT_NE(isolated_renderer, nullptr);
  auto isolated_scene
    = CreateTwoBoxShadowScene(MakeDirectionalSunDirection(), 1U);
  auto isolated_vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &isolated_renderer->GetStagingProvider() },
    oxygen::observer_ptr {
      &isolated_renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene_data = CreateTwoBoxShadowScene(MakeDirectionalSunDirection(), 1U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);
  const auto resolved_view = MakeDirectionalView();
  const auto isolated_result
    = RunTwoBoxShadowProjectionStage(*isolated_renderer, isolated_scene,
      isolated_vsm_renderer, resolved_view, kOutputWidth, kOutputHeight,
      SequenceNumber { 31U }, Slot { 0U }, kShadowCasterContentHash);
  const auto result = RunTwoBoxLiveShellProjectionFrame(*renderer, scene_data,
    vsm_renderer, resolved_view, kOutputWidth, kOutputHeight,
    SequenceNumber { 31U }, Slot { 0U }, kShadowCasterContentHash);

  const auto output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);
  const auto [candidate_shadow_probes, candidate_lit_probes]
    = SelectStableAnalyticFloorProbes(scene_data,
      *result.live_frame.scene_depth_texture, resolved_view, 32U, 32U);
  auto shadow_probes = std::vector<ProbeSample> {};
  auto lit_probes = std::vector<ProbeSample> {};
  shadow_probes.reserve(8U);
  lit_probes.reserve(8U);
  for (const auto& probe : candidate_shadow_probes) {
    const auto visibility = ComputeCpuVisibility(
      isolated_result, resolved_view.ViewMatrix(), probe.point_ws);
    if (!visibility.has_directional_sample || visibility.directional > 0.25F) {
      continue;
    }
    shadow_probes.push_back(probe);
    if (shadow_probes.size() >= 8U) {
      break;
    }
  }
  for (const auto& probe : candidate_lit_probes) {
    const auto visibility = ComputeCpuVisibility(
      isolated_result, resolved_view.ViewMatrix(), probe.point_ws);
    if (!visibility.has_directional_sample || visibility.directional < 0.75F) {
      continue;
    }
    lit_probes.push_back(probe);
    if (lit_probes.size() >= 8U) {
      break;
    }
  }
  ASSERT_GE(shadow_probes.size(), 4U);
  ASSERT_GE(lit_probes.size(), 4U);
  SCOPED_TRACE("Darkest visible floor probes: "
    + SummarizeDarkestVisibleFloorProbes(scene_data,
      *result.live_frame.scene_depth_texture, resolved_view,
      result.output.directional_shadow_mask));

  for (const auto& probe : shadow_probes) {
    const auto sample
      = ReadOutputTexelDirect(output.directional_shadow_mask_texture,
        probe.pixel.x, probe.pixel.y, "stage-fifteen-two-box.shadow-band");
    const auto isolated_sample
      = isolated_result.output.directional_shadow_mask.At(
        probe.pixel.x, probe.pixel.y);
    const auto debug = ComputeDirectionalRouteDebug(
      isolated_result, scene_data, resolved_view.ViewMatrix(), probe.point_ws);
    const auto route_candidates = SummarizeDirectionalRouteCandidates(
      isolated_result, resolved_view.ViewMatrix(), probe.point_ws);
    EXPECT_LT(sample, 0.35F)
      << "Stage 15 mask kept a shadowed floor probe lit at world point ("
      << probe.point_ws.x << ", " << probe.point_ws.y << ", "
      << probe.point_ws.z << ") screen pixel (" << probe.pixel.x << ", "
      << probe.pixel.y << ") with sample " << sample
      << " isolated_sample=" << isolated_sample << " level=" << debug.level
      << " page=(" << debug.page.x << ", " << debug.page.y << ")"
      << " page_table_index=" << debug.page_table_index
      << " physical_page=" << debug.physical_page_index
      << " receiver_depth=" << debug.receiver_depth << " tap_depths=["
      << debug.tap_depths[0] << ", " << debug.tap_depths[1] << ", "
      << debug.tap_depths[2] << ", " << debug.tap_depths[3] << "]"
      << " covered_texels=" << debug.covered_texel_count << " covered_bounds=("
      << debug.covered_min.x << ", " << debug.covered_min.y << ")-("
      << debug.covered_max.x << ", " << debug.covered_max.y << ")"
      << " cpu_visibility=" << debug.visibility
      << " route_candidates=" << route_candidates;
  }
  for (const auto& probe : lit_probes) {
    const auto sample
      = ReadOutputTexelDirect(output.directional_shadow_mask_texture,
        probe.pixel.x, probe.pixel.y, "stage-fifteen-two-box.shadow-band");
    EXPECT_GT(sample, 0.65F)
      << "Stage 15 mask darkened a stably lit floor probe at world point ("
      << probe.point_ws.x << ", " << probe.point_ws.y << ", "
      << probe.point_ws.z << ") screen pixel (" << probe.pixel.x << ", "
      << probe.pixel.y << ") with sample " << sample;
  }
}

NOLINT_TEST_F(VsmShadowProjectionLiveSceneTest,
  DirectionalFourCascadeLiveShellMatchesAnalyticFloorClassificationAcrossDenseVisibleProbes)
{
  auto isolated_renderer = MakeRenderer();
  ASSERT_NE(isolated_renderer, nullptr);
  auto isolated_scene
    = CreateTwoBoxShadowScene(MakeDirectionalSunDirection(), 4U);
  auto isolated_vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &isolated_renderer->GetStagingProvider() },
    oxygen::observer_ptr {
      &isolated_renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene_data = CreateTwoBoxShadowScene(MakeDirectionalSunDirection(), 4U);
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);
  const auto resolved_view = MakeDirectionalView();
  const auto isolated_result
    = RunTwoBoxShadowProjectionStage(*isolated_renderer, isolated_scene,
      isolated_vsm_renderer, resolved_view, kOutputWidth, kOutputHeight,
      SequenceNumber { 61U }, Slot { 0U }, kShadowCasterContentHash);

  const auto result = RunTwoBoxLiveShellProjectionFrame(*renderer, scene_data,
    vsm_renderer, resolved_view, kOutputWidth, kOutputHeight,
    SequenceNumber { 61U }, Slot { 0U }, kShadowCasterContentHash);

  const auto [candidate_shadow_probes, candidate_lit_probes]
    = SelectStableAnalyticFloorProbes(scene_data,
      *result.live_frame.scene_depth_texture, resolved_view, 256U, 256U);
  auto shadow_probes = std::vector<ProbeSample> {};
  auto lit_probes = std::vector<ProbeSample> {};
  shadow_probes.reserve(64U);
  lit_probes.reserve(64U);
  for (const auto& probe : candidate_shadow_probes) {
    const auto visibility = ComputeCpuVisibility(
      isolated_result, resolved_view.ViewMatrix(), probe.point_ws);
    if (!visibility.has_directional_sample || visibility.directional > 0.25F) {
      continue;
    }
    shadow_probes.push_back(probe);
    if (shadow_probes.size() >= 64U) {
      break;
    }
  }
  for (const auto& probe : candidate_lit_probes) {
    const auto visibility = ComputeCpuVisibility(
      isolated_result, resolved_view.ViewMatrix(), probe.point_ws);
    if (!visibility.has_directional_sample || visibility.directional < 0.75F) {
      continue;
    }
    lit_probes.push_back(probe);
    if (lit_probes.size() >= 64U) {
      break;
    }
  }
  ASSERT_GE(shadow_probes.size(), 32U);
  ASSERT_GE(lit_probes.size(), 32U);
  SCOPED_TRACE("Darkest visible floor probes: "
    + SummarizeDarkestVisibleFloorProbes(scene_data,
      *result.live_frame.scene_depth_texture, resolved_view,
      result.output.directional_shadow_mask));

  for (const auto& probe : shadow_probes) {
    const auto sample
      = result.output.directional_shadow_mask.At(probe.pixel.x, probe.pixel.y);
    const auto isolated_sample
      = isolated_result.output.directional_shadow_mask.At(
        probe.pixel.x, probe.pixel.y);
    const auto debug = ComputeDirectionalRouteDebug(
      isolated_result, scene_data, resolved_view.ViewMatrix(), probe.point_ws);
    const auto route_candidates = SummarizeDirectionalRouteCandidates(
      isolated_result, resolved_view.ViewMatrix(), probe.point_ws);
    EXPECT_LT(sample, 0.45F)
      << "four-cascade directional projection kept an analytically shadowed "
         "floor probe too bright at pixel=("
      << probe.pixel.x << ", " << probe.pixel.y << ") world=("
      << probe.point_ws.x << ", " << probe.point_ws.y << ", "
      << probe.point_ws.z << ") sample=" << sample << " level=" << debug.level
      << " page=(" << debug.page.x << ", " << debug.page.y << ")"
      << " page_table_index=" << debug.page_table_index
      << " physical_page=" << debug.physical_page_index
      << " indirect_commands=" << debug.indirect_command_count
      << " draw_indices=[" << debug.indirect_draw_indices[0] << ", "
      << debug.indirect_draw_indices[1] << ", "
      << debug.indirect_draw_indices[2] << ", "
      << debug.indirect_draw_indices[3] << "]"
      << " covered_texels=" << debug.covered_texel_count
      << " page_min_depth=" << debug.page_min_depth
      << " receiver_depth=" << debug.receiver_depth << " tap_depths=["
      << debug.tap_depths[0] << ", " << debug.tap_depths[1] << ", "
      << debug.tap_depths[2] << ", " << debug.tap_depths[3] << "]"
      << " tall_box_depth_range=[" << debug.tall_box_min_depth << ", "
      << debug.tall_box_max_depth << "]"
      << " sample_texel=(" << debug.sample_texel.x << ", "
      << debug.sample_texel.y << ")"
      << " covered_bounds=(" << debug.covered_min.x << ", "
      << debug.covered_min.y << ")-(" << debug.covered_max.x << ", "
      << debug.covered_max.y << ")"
      << " dynamic_before=" << debug.dynamic_before_visibility
      << " static_before=" << debug.static_before_visibility
      << " dynamic_after=" << debug.dynamic_after_visibility
      << " cpu_visibility=" << debug.visibility
      << " isolated_sample=" << isolated_sample
      << " route_candidates=" << route_candidates;
  }
  for (const auto& probe : lit_probes) {
    const auto sample
      = result.output.directional_shadow_mask.At(probe.pixel.x, probe.pixel.y);
    const auto isolated_sample
      = isolated_result.output.directional_shadow_mask.At(
        probe.pixel.x, probe.pixel.y);
    const auto debug = ComputeDirectionalRouteDebug(
      isolated_result, scene_data, resolved_view.ViewMatrix(), probe.point_ws);
    const auto route_candidates = SummarizeDirectionalRouteCandidates(
      isolated_result, resolved_view.ViewMatrix(), probe.point_ws);
    EXPECT_GT(sample, 0.65F)
      << "four-cascade directional projection darkened an analytically lit "
         "floor probe at pixel=("
      << probe.pixel.x << ", " << probe.pixel.y << ") world=("
      << probe.point_ws.x << ", " << probe.point_ws.y << ", "
      << probe.point_ws.z << ") sample=" << sample << " level=" << debug.level
      << " page=(" << debug.page.x << ", " << debug.page.y << ")"
      << " page_table_index=" << debug.page_table_index
      << " physical_page=" << debug.physical_page_index
      << " indirect_commands=" << debug.indirect_command_count
      << " draw_indices=[" << debug.indirect_draw_indices[0] << ", "
      << debug.indirect_draw_indices[1] << ", "
      << debug.indirect_draw_indices[2] << ", "
      << debug.indirect_draw_indices[3] << "]"
      << " covered_texels=" << debug.covered_texel_count
      << " page_min_depth=" << debug.page_min_depth
      << " receiver_depth=" << debug.receiver_depth << " tap_depths=["
      << debug.tap_depths[0] << ", " << debug.tap_depths[1] << ", "
      << debug.tap_depths[2] << ", " << debug.tap_depths[3] << "]"
      << " tall_box_depth_range=[" << debug.tall_box_min_depth << ", "
      << debug.tall_box_max_depth << "]"
      << " sample_texel=(" << debug.sample_texel.x << ", "
      << debug.sample_texel.y << ")"
      << " covered_bounds=(" << debug.covered_min.x << ", "
      << debug.covered_min.y << ")-(" << debug.covered_max.x << ", "
      << debug.covered_max.y << ")"
      << " dynamic_before=" << debug.dynamic_before_visibility
      << " static_before=" << debug.static_before_visibility
      << " dynamic_after=" << debug.dynamic_after_visibility
      << " cpu_visibility=" << debug.visibility
      << " isolated_sample=" << isolated_sample
      << " route_candidates=" << route_candidates;
  }
}

NOLINT_TEST_F(VsmShadowProjectionLiveSceneTest,
  PagedSpotLightTwoBoxSceneMatchesCpuProjectionFromRealStageInputs)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto scene_data = CreateTwoBoxShadowScene(MakeDirectionalSunDirection(), 1U);
  DisableDirectionalShadowCasts(scene_data);
  const auto camera_eye = glm::vec3 { -3.2F, 3.4F, 5.8F };
  AttachSpotLightToTwoBoxScene(scene_data, camera_eye,
    PrimarySpotTargetForTwoBoxScene(scene_data), 18.0F, glm::radians(30.0F),
    glm::radians(50.0F));
  auto vsm_renderer = oxygen::renderer::vsm::VsmShadowRenderer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    oxygen::ShadowQualityTier::kHigh);
  const auto resolved_view = MakeDirectionalView();

  const auto result = RunTwoBoxShadowProjectionStage(*renderer, scene_data,
    vsm_renderer, resolved_view, kOutputWidth, kOutputHeight,
    SequenceNumber { 47U }, Slot { 0U }, 0x5A07ULL);

  const auto probes = SelectInteriorVisibleFloorProbes(scene_data,
    *result.hzb.merge.rasterization.initialization.propagation.mapping.bridge
      .scene_depth_texture,
    resolved_view, 12U);
  ASSERT_GE(probes.size(), 8U);

  ExpectMaskMatchesCpuProjection(
    result.output.shadow_mask, probes, [&](const ProbeSample& probe) {
      return ComputeCpuVisibility(
        result, resolved_view.ViewMatrix(), probe.point_ws)
        .composite;
    });
}

} // namespace
