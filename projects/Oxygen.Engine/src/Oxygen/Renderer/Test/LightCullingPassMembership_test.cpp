//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/glm.hpp>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/PositionalLightData.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>

#include "Fixtures/RendererOffscreenGpuTestFixture.h"

namespace {

using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::LightCullingConfig;
using oxygen::engine::LightCullingPass;
using oxygen::engine::LightCullingPassConfig;
using oxygen::engine::PositionalLightData;
using oxygen::engine::PositionalLightFlags;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::testing::RendererOffscreenGpuTestFixture;
using oxygen::engine::testing::RunPass;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::BufferRange;
using oxygen::graphics::GpuBufferReadback;
using oxygen::renderer::LightManager;
using oxygen::scene::PointLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

constexpr ViewId kTestViewId { 32U };
constexpr std::uint32_t kViewportWidth = 64U;
constexpr std::uint32_t kViewportHeight = 64U;

struct ClusterGridEntry {
  std::uint32_t light_list_offset { 0U };
  std::uint32_t light_count { 0U };
};

class LightCullingPassMembershipTest : public RendererOffscreenGpuTestFixture {
protected:
  [[nodiscard]] static auto MakeResolvedView() -> ResolvedView
  {
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(kViewportWidth),
      .height = static_cast<float>(kViewportHeight),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(kViewportWidth),
      .bottom = static_cast<std::int32_t>(kViewportHeight),
    };

    const auto projection = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
      glm::radians(90.0F), 1.0F, 0.1F, 100.0F);

    return ResolvedView(ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = glm::mat4 { 1.0F },
      .proj_matrix = projection,
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto CreatePointLightNode(Scene& scene,
    const std::string& name, const glm::vec3& position_ws, const float range)
    -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(true))
                         .SetFlag(SceneNodeFlags::kCastsShadows,
                           SceneFlag {}.SetEffectiveValueBit(false));
    auto node = scene.CreateNode(name, flags);
    EXPECT_TRUE(node.IsValid());
    EXPECT_TRUE(node.GetTransform().SetLocalPosition(position_ws));

    auto impl = node.GetImpl();
    EXPECT_TRUE(impl.has_value());
    impl->get().AddComponent<PointLight>();
    auto& light = impl->get().GetComponent<PointLight>();
    light.SetRange(range);
    light.Common().casts_shadows = false;
    impl->get().UpdateTransforms(scene);
    return node;
  }

  static auto CollectLightNode(LightManager& light_manager, SceneNode& node)
    -> void
  {
    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    light_manager.CollectFromNode(node.GetHandle(), impl->get());
  }

  [[nodiscard]] static auto DeviceDepthFromLinearViewDepth(
    const float linear_depth, const glm::mat4& projection_matrix) -> float
  {
    const auto clip
      = projection_matrix * glm::vec4(0.0F, 0.0F, -linear_depth, 1.0F);
    return clip.z / std::max(clip.w, 1.0e-6F);
  }

  [[nodiscard]] static auto ClipToView(const glm::vec2& clip_xy,
    const float device_depth, const glm::mat4& inv_projection_matrix)
    -> glm::vec3
  {
    const auto view
      = inv_projection_matrix * glm::vec4(clip_xy, device_depth, 1.0F);
    return glm::vec3(view) / std::max(view.w, 1.0e-6F);
  }

  [[nodiscard]] static auto ComputeCellNearViewDepthFromZSlice(
    const std::uint32_t z_slice, const LightCullingConfig::ZParams& z_params,
    const std::uint32_t cluster_dim_z) -> float
  {
    float slice_depth
      = (std::exp2(static_cast<float>(z_slice) / z_params.s) - z_params.o)
      / z_params.b;

    if (z_slice == cluster_dim_z) {
      slice_depth = 2'000'000.0F;
    }
    if (z_slice == 0U) {
      slice_depth = 0.0F;
    }

    return slice_depth;
  }

  static auto ComputeCellViewAabb(const std::uint32_t z_slice,
    const ResolvedView& resolved_view, const LightCullingConfig& config,
    glm::vec3& out_min, glm::vec3& out_max) -> void
  {
    const auto screen_dimensions
      = glm::vec2 { static_cast<float>(kViewportWidth),
          static_cast<float>(kViewportHeight) };
    const float pixel_size
      = static_cast<float>(1U << config.light_grid_pixel_size_shift);
    const auto inv_grid_size = glm::vec2 { pixel_size / screen_dimensions.x,
      pixel_size / screen_dimensions.y };
    const auto tile_size
      = glm::vec2 { 2.0F * inv_grid_size.x, -2.0F * inv_grid_size.y };
    const auto clip_origin = glm::vec2 { -1.0F, 1.0F };

    const auto clip_tile_min = clip_origin;
    const auto clip_tile_max = clip_origin + tile_size;

    const auto z_params = LightCullingConfig::ZParams {
      .b = config.light_grid_z_params_b,
      .o = config.light_grid_z_params_o,
      .s = config.light_grid_z_params_s,
    };
    const float min_tile_z = ComputeCellNearViewDepthFromZSlice(
      z_slice, z_params, config.cluster_dim_z);
    const float max_tile_z = ComputeCellNearViewDepthFromZSlice(
      z_slice + 1U, z_params, config.cluster_dim_z);

    const float min_tile_device_z = DeviceDepthFromLinearViewDepth(
      min_tile_z, resolved_view.ProjectionMatrix());
    const float max_tile_device_z = DeviceDepthFromLinearViewDepth(
      max_tile_z, resolved_view.ProjectionMatrix());

    const auto inv_projection = resolved_view.InverseProjection();
    const std::vector<glm::vec3> corners {
      ClipToView({ clip_tile_min.x, clip_tile_min.y }, min_tile_device_z,
        inv_projection),
      ClipToView({ clip_tile_max.x, clip_tile_min.y }, min_tile_device_z,
        inv_projection),
      ClipToView({ clip_tile_min.x, clip_tile_max.y }, min_tile_device_z,
        inv_projection),
      ClipToView({ clip_tile_max.x, clip_tile_max.y }, min_tile_device_z,
        inv_projection),
      ClipToView({ clip_tile_min.x, clip_tile_min.y }, max_tile_device_z,
        inv_projection),
      ClipToView({ clip_tile_max.x, clip_tile_min.y }, max_tile_device_z,
        inv_projection),
      ClipToView({ clip_tile_min.x, clip_tile_max.y }, max_tile_device_z,
        inv_projection),
      ClipToView({ clip_tile_max.x, clip_tile_max.y }, max_tile_device_z,
        inv_projection),
    };

    out_min = corners.front();
    out_max = corners.front();
    for (const auto& corner : corners) {
      out_min = glm::min(out_min, corner);
      out_max = glm::max(out_max, corner);
    }
  }

  [[nodiscard]] static auto ComputeSquaredDistanceFromBoxToPoint(
    const glm::vec3& center, const glm::vec3& extents,
    const glm::vec3& probe_position) -> float
  {
    const auto delta = glm::abs(probe_position - center) - extents;
    const auto clamped = glm::max(delta, glm::vec3 { 0.0F });
    return glm::dot(clamped, clamped);
  }

  [[nodiscard]] static auto TestPointLightAgainstCell(
    const PositionalLightData& light, const glm::vec3& view_tile_center,
    const glm::vec3& view_tile_extent) -> bool
  {
    const auto affects_world_bit
      = static_cast<std::uint32_t>(PositionalLightFlags::kAffectsWorld);
    if ((light.flags & affects_world_bit) == 0U) {
      return false;
    }

    const auto view_space_light_position = light.position_ws;
    const float light_radius = std::max(light.range, 0.0F);
    const float box_distance_sq = ComputeSquaredDistanceFromBoxToPoint(
      view_tile_center, view_tile_extent, view_space_light_position);
    return box_distance_sq < light_radius * light_radius;
  }

  template <typename T>
  [[nodiscard]] static auto CopyPodArray(const std::span<const std::byte> bytes)
    -> std::vector<T>
  {
    EXPECT_EQ(bytes.size() % sizeof(T), 0U);
    std::vector<T> values(bytes.size() / sizeof(T));
    if (!values.empty()) {
      std::memcpy(values.data(), bytes.data(), bytes.size());
    }
    return values;
  }
};

NOLINT_TEST_F(LightCullingPassMembershipTest,
  PointLightMembershipMatchesAnalyticCpuBaseline)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("light-culling-membership", 8U);
  auto pass
    = LightCullingPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<LightCullingPassConfig>(
        LightCullingPassConfig { .debug_name = "light-culling.membership" }));

  auto resolved_view = MakeResolvedView();
  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  auto light_manager = renderer->GetLightManager();
  ASSERT_NE(light_manager, nullptr);

  auto near_light = CreatePointLightNode(
    *scene, "near", glm::vec3 { 0.0F, 0.0F, -1.0F }, 0.45F);
  auto far_light = CreatePointLightNode(
    *scene, "far", glm::vec3 { 0.0F, 0.0F, -8.0F }, 1.75F);
  CollectLightNode(*light_manager, near_light);
  CollectLightNode(*light_manager, far_light);

  const auto positional_lights = light_manager->GetPositionalLights();
  ASSERT_EQ(positional_lights.size(), 2U);
  EXPECT_NEAR(positional_lights[0].position_ws.z, -1.0F, 1.0e-4F);
  EXPECT_NEAR(positional_lights[1].position_ws.z, -8.0F, 1.0e-4F);

  auto readback_manager = Backend().GetReadbackManager();
  ASSERT_NE(readback_manager, nullptr);
  auto cluster_grid_readback
    = readback_manager->CreateBufferReadback("light-culling-grid");
  auto light_list_readback
    = readback_manager->CreateBufferReadback("light-culling-list");
  ASSERT_NE(cluster_grid_readback, nullptr);
  ASSERT_NE(light_list_readback, nullptr);

  LightCullingConfig::GridDimensions grid_dims {};
  {
    auto recorder = AcquireRecorder("light-culling.membership");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, render_context, *recorder);

    grid_dims = pass.GetGridDimensions();
    ASSERT_EQ(grid_dims.x, 1U);
    ASSERT_EQ(grid_dims.y, 1U);
    ASSERT_EQ(grid_dims.z, LightCullingConfig::kLightGridSizeZ);

    auto cluster_grid_buffer = pass.GetClusterGridBuffer();
    auto light_index_list_buffer = pass.GetLightIndexListBuffer();
    ASSERT_NE(cluster_grid_buffer, nullptr);
    ASSERT_NE(light_index_list_buffer, nullptr);

    const auto cluster_grid_ticket = cluster_grid_readback->EnqueueCopy(
      *recorder, *cluster_grid_buffer,
      BufferRange { 0U, grid_dims.total_clusters * sizeof(ClusterGridEntry) });
    ASSERT_TRUE(cluster_grid_ticket.has_value());

    const auto light_list_ticket
      = light_list_readback->EnqueueCopy(*recorder, *light_index_list_buffer,
        BufferRange { 0U,
          static_cast<std::uint64_t>(grid_dims.total_clusters)
            * LightCullingConfig::kMaxCulledLightsPerCell
            * sizeof(std::uint32_t) });
    ASSERT_TRUE(light_list_ticket.has_value());
  }
  WaitForQueueIdle();

  const auto cluster_grid_mapped = cluster_grid_readback->MapNow();
  ASSERT_TRUE(cluster_grid_mapped.has_value());
  const auto light_list_mapped = light_list_readback->MapNow();
  ASSERT_TRUE(light_list_mapped.has_value());

  const auto cluster_grid
    = CopyPodArray<ClusterGridEntry>(cluster_grid_mapped->Bytes());
  const auto light_index_list
    = CopyPodArray<std::uint32_t>(light_list_mapped->Bytes());
  ASSERT_EQ(cluster_grid.size(), grid_dims.total_clusters);
  ASSERT_EQ(light_index_list.size(),
    static_cast<std::size_t>(grid_dims.total_clusters)
      * LightCullingConfig::kMaxCulledLightsPerCell);

  const auto config = pass.GetClusterConfig();
  ASSERT_TRUE(config.IsAvailable());

  std::vector<std::vector<std::uint32_t>> expected_membership(grid_dims.z);
  for (std::uint32_t slice = 0U; slice < grid_dims.z; ++slice) {
    glm::vec3 cell_min { 0.0F };
    glm::vec3 cell_max { 0.0F };
    ComputeCellViewAabb(slice, resolved_view, config, cell_min, cell_max);
    const auto cell_center = 0.5F * (cell_min + cell_max);
    const auto cell_extent = cell_max - cell_center;

    for (std::uint32_t light_index = 0U; light_index < positional_lights.size();
      ++light_index) {
      if (TestPointLightAgainstCell(
            positional_lights[light_index], cell_center, cell_extent)) {
        expected_membership[slice].push_back(light_index);
      }
    }
  }

  bool saw_empty_slice = false;
  bool saw_single_light_slice = false;
  for (std::uint32_t slice = 0U; slice < grid_dims.z; ++slice) {
    const auto& expected = expected_membership[slice];
    const auto& actual = cluster_grid[slice];
    if (expected.empty()) {
      saw_empty_slice = true;
    }
    if (expected.size() == 1U) {
      saw_single_light_slice = true;
    }

    EXPECT_EQ(actual.light_list_offset,
      slice * LightCullingConfig::kMaxCulledLightsPerCell);
    EXPECT_EQ(actual.light_count, static_cast<std::uint32_t>(expected.size()))
      << "slice=" << slice;
    for (std::uint32_t i = 0U; i < actual.light_count; ++i) {
      EXPECT_EQ(light_index_list[actual.light_list_offset + i], expected[i])
        << "slice=" << slice << " list_index=" << i;
    }
  }

  EXPECT_TRUE(saw_empty_slice);
  EXPECT_TRUE(saw_single_light_slice);
}

} // namespace
