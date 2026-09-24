//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_float.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Lighting/Types/ClusterLightRange.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridBuildStatus.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Support/LightingWorkload.h>
#include <Oxygen/Vortex/Test/Support/LightingWorkloadScene.h>
#include <Oxygen/Vortex/Types/LightCullingConfig.h>

namespace oxygen::vortex::testing {
namespace {
  class SpatialLightGridGpuTest : public exposure::ExposureLightingGpuTest {
  protected:
    LightingFrameBindings grid_bindings {};
    LightGridResources grid_resources {};
    scene::NodeHandle near_light;
    std::uint32_t near_light_index = kInvalidLightSelectionIndex.get();
    void SetUp() override
    {
      initial_scene_capacity = 16U;
      ExposureLightingGpuTest::SetUp();
      probe->inspect = [this](const auto& ctx, const auto&, unsigned) {
        auto* lighting
          = RendererPublicationProbe::GetLightingService(*renderer_);
        const auto* bindings
          = lighting->InspectForwardLightBindings(ctx.current_view.view_id);
        ASSERT_NE(bindings, nullptr);
        grid_bindings = *bindings;
        grid_resources
          = lighting->InspectGridResources(ctx.current_view.view_id);
        auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
        const auto& lights
          = RendererPublicationProbe::GetFrameLightSelection(*owner)
              .local_lights;
        for (std::uint32_t i = 0U; i < lights.size(); ++i) {
          if (lights[i].source_node == near_light) {
            near_light_index = i;
          }
        }
      };
    }
    void TearDown() override
    {
      grid_resources = {};
      ExposureLightingGpuTest::TearDown();
    }
    void CheckSpatialMembership(const bool fallback)
    {
      for (unsigned index = 0U; index < 2U; ++index) {
        auto node
          = scene->CreateNode("Spatial grid light " + std::to_string(index));
        auto light = std::make_unique<scene::PointLight>();
        light->SetRange(1.0F);
        light->SetLuminousFluxLm(100.0F);
        ASSERT_TRUE(node.AttachLight(std::move(light)));
        if (index == 0U) {
          near_light = node.GetHandle();
        }
        if (index != 0U) {
          node.GetTransform().SetLocalPosition(
            { 100000.0F, 100000.0F, 100000.0F });
        }
      }
      SetSurface(data::MaterialDomain::kOpaque, 1.0F);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0.0F, 3U));
      const auto* bindings = &grid_bindings;
      ASSERT_NE(bindings, nullptr);
      ASSERT_EQ(bindings->local_count, 2U);
      const auto resources = grid_resources;
      ASSERT_NE(resources.status, nullptr);
      const auto status = Read<LightGridBuildStatus>(
        *resources.status, graphics::ResourceStates::kShaderResource);
      EXPECT_EQ(status.state, kLightGridBuildValid);
      EXPECT_EQ(status.required_index_count[1], 0U);
      EXPECT_GT(status.required_index_count[0], 0U);
      EXPECT_LT(status.required_index_count[0],
        bindings->cluster_count * bindings->local_count);
      if (fallback) {
        EXPECT_EQ(bindings->index_capacity, 0U);
        EXPECT_GT(status.fallback_cell_count, 0U);
        EXPECT_EQ(status.written_index_count, 0U);
      } else {
        ASSERT_NE(resources.indices, nullptr);
        EXPECT_EQ(status.fallback_cell_count, 0U);
        ASSERT_EQ(status.written_index_count, status.required_index_count[0]);
        const auto values
          = GetReadbackManager()->ReadBufferNow(*resources.indices,
            { 0U,
              static_cast<std::uint64_t>(status.written_index_count)
                * sizeof(std::uint32_t) });
        ASSERT_TRUE(values.has_value());
        for (std::uint32_t index = 0U; index < status.written_index_count;
          ++index) {
          auto value = std::uint32_t {};
          std::memcpy(
            &value, values->data() + index * sizeof(value), sizeof(value));
          EXPECT_EQ(value, near_light_index)
            << "An out-of-frustum light entered a compact cell";
        }
      }
    }
  };

  NOLINT_TEST_F(SpatialLightGridGpuTest, ProductionGridRejectsDistantLight)
  {
    CheckSpatialMembership(false);
  }

  NOLINT_TEST_F(
    SpatialLightGridGpuTest, BoundaryPointAndSpotContributeToEveryAdjacentTile)
  {
    constexpr auto extent = 2U * LightCullingConfig::kLightGridPixelSize;
    view.viewport.width = view.viewport.height = static_cast<float>(extent);
    auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
    ASSERT_TRUE(lens.has_value());
    lens->get().SetViewport(view.viewport);
    lens->get().SetAspectRatio(1.0F);
    lens->get().SetNearPlane(0.1F);
    lens->get().SetFarPlane(10.0F);
    auto output = CreateRegisteredTexture({ .width = extent,
      .height = extent,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon });
    framebuffer = Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(output));
    auto point_node = scene->CreateNode("Boundary point");
    auto spot_node = scene->CreateNode("Boundary spot");
    point_node.GetTransform().SetLocalPosition({ 0.0F, 0.0F, -1.0F });
    spot_node.GetTransform().SetLocalPosition({ 0.0F, 0.0F, -1.0F });
    auto point = std::make_unique<scene::PointLight>();
    point->SetRange(0.01F);
    point->SetLuminousFluxLm(100.0F);
    auto spot = std::make_unique<scene::SpotLight>();
    spot->SetRange(0.01F);
    spot->SetLuminousFluxLm(100.0F);
    ASSERT_TRUE(point_node.AttachLight(std::move(point)));
    ASSERT_TRUE(spot_node.AttachLight(std::move(spot)));
    SetSurface(data::MaterialDomain::kOpaque, 1.0F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0.0F, 3U));
    const auto resources = grid_resources;
    ASSERT_NE(resources.status, nullptr);
    const auto status = Read<LightGridBuildStatus>(
      *resources.status, graphics::ResourceStates::kShaderResource);
    ASSERT_EQ(status.state, kLightGridBuildValid);
    ASSERT_EQ(status.fallback_cell_count, 0U);
    const auto bytes = GetReadbackManager()->ReadBufferNow(*resources.ranges,
      { 0U,
        4U * LightCullingConfig::kLightGridSizeZ * sizeof(ClusterLightRange) });
    ASSERT_TRUE(bytes.has_value());
    auto contributing_slices = 0U;
    for (unsigned z = 0U; z < LightCullingConfig::kLightGridSizeZ; ++z) {
      auto ranges = std::array<ClusterLightRange, 4> {};
      std::memcpy(
        ranges.data(), bytes->data() + z * sizeof(ranges), sizeof(ranges));
      if (ranges[0].count == 0U) {
        continue;
      }
      ++contributing_slices;
      for (const auto& range : ranges) {
        EXPECT_EQ(range.count, 2U);
      }
    }
    EXPECT_GT(contributing_slices, 0U);
  }

  NOLINT_TEST_F(SpatialLightGridGpuTest, CompletedCountsGrowEveryFrameSlot)
  {
    for (unsigned i = 0U; i < 8U; ++i) {
      auto node = scene->CreateNode("Dense grid light " + std::to_string(i));
      auto light = std::make_unique<scene::PointLight>();
      light->SetRange(10000.0F);
      light->SetLuminousFluxLm(100.0F);
      ASSERT_TRUE(node.AttachLight(std::move(light)));
    }
    SetSurface(data::MaterialDomain::kOpaque, 1.0F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0.0F, 9U));
    for (unsigned slot = 0U; slot < 3U; ++slot) {
      ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0.0F, 1U));
      const auto* bindings = &grid_bindings;
      ASSERT_NE(bindings, nullptr);
      const auto resources = grid_resources;
      ASSERT_NE(resources.status, nullptr);
      const auto status = Read<LightGridBuildStatus>(
        *resources.status, graphics::ResourceStates::kShaderResource);
      EXPECT_EQ(status.state, kLightGridBuildValid);
      EXPECT_GT(status.required_index_count[0], bindings->cluster_count * 4U);
      EXPECT_LE(status.required_index_count[0], bindings->index_capacity);
      EXPECT_EQ(status.fallback_cell_count, 0U);
    }
  }

  NOLINT_TEST_F(SpatialLightGridGpuTest,
    LightBatchTailPreservesCompleteOrderedListsAfterMove)
  {
    constexpr auto light_count = 65U;
    for (unsigned i = 0U; i < light_count; ++i) {
      auto node = scene->CreateNode("Batch light " + std::to_string(i));
      auto light = std::make_unique<scene::PointLight>();
      light->SetRange(10000.0F);
      light->SetLuminousFluxLm(1.0F);
      light->Common().casts_shadows = false;
      ASSERT_TRUE(node.AttachLight(std::move(light)));
      if (i + 1U == light_count) {
        near_light = node.GetHandle();
      }
    }
    SetSurface(data::MaterialDomain::kOpaque, 1.0F);
    // The fixture's single XY tile has 32 depth cells, exercising a partial
    // 64-thread group as well as the second, single-light batch.
    ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0.0F, 9U));
    ASSERT_EQ(grid_bindings.cluster_count, LightCullingConfig::kLightGridSizeZ);
    for (const bool moved : { false, true }) {
      SCOPED_TRACE(moved);
      if (moved) {
        auto tail = scene->GetNode(near_light);
        ASSERT_TRUE(tail.has_value());
        tail->GetTransform().SetLocalPosition({ 100000.0F, 0.0F, 0.0F });
        ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0.0F, 3U));
      }
      ASSERT_EQ(grid_bindings.local_count, light_count);
      ASSERT_LT(near_light_index, light_count);
      ASSERT_NE(grid_resources.status, nullptr);
      ASSERT_NE(grid_resources.ranges, nullptr);
      ASSERT_NE(grid_resources.indices, nullptr);
      const auto expected_count = light_count - static_cast<unsigned>(moved);
      const auto status = Read<LightGridBuildStatus>(
        *grid_resources.status, graphics::ResourceStates::kShaderResource);
      ASSERT_EQ(status.state, kLightGridBuildValid);
      ASSERT_EQ(status.fallback_cell_count, 0U);
      ASSERT_EQ(status.written_index_count,
        grid_bindings.cluster_count * expected_count);
      const auto ranges
        = GetReadbackManager()->ReadBufferNow(*grid_resources.ranges,
          { 0U, grid_bindings.cluster_count * sizeof(ClusterLightRange) });
      const auto indices
        = GetReadbackManager()->ReadBufferNow(*grid_resources.indices,
          { 0U, status.written_index_count * sizeof(std::uint32_t) });
      ASSERT_TRUE(ranges.has_value());
      ASSERT_TRUE(indices.has_value());
      for (unsigned cell = 0U; cell < grid_bindings.cluster_count; ++cell) {
        auto range = ClusterLightRange {};
        std::memcpy(
          &range, ranges->data() + cell * sizeof(range), sizeof(range));
        ASSERT_EQ(range.count, expected_count);
        ASSERT_LE(range.offset.get() + range.count, status.written_index_count);
        unsigned ordinal = 0U;
        for (unsigned selected = 0U; selected < light_count; ++selected) {
          if (moved && selected == near_light_index) {
            continue;
          }
          std::uint32_t actual {};
          std::memcpy(&actual,
            indices->data() + (range.offset.get() + ordinal) * sizeof(actual),
            sizeof(actual));
          EXPECT_EQ(actual, selected) << "cell " << cell;
          ++ordinal;
        }
      }
    }
  }

  class SpatialLightGridFallbackGpuTest : public SpatialLightGridGpuTest {
  protected:
    void ConfigureRenderer(RendererConfig& config) const override
    {
      config.lighting_compact_index_limit_bytes = 0U;
    }
  };

  NOLINT_TEST_F(
    SpatialLightGridFallbackGpuTest, ExhaustedIndexBudgetPreservesCompleteCells)
  {
    CheckSpatialMembership(true);
  }

  class LightingWorkloadGpuTest : public exposure::ExposureLightingGpuTest {
  protected:
    auto SetUp() -> void override
    {
      initial_scene_capacity = LightingWorkloadOptions {}.light_count + 2U;
      ExposureLightingGpuTest::SetUp();
    }
  };

  NOLINT_TEST_F(
    LightingWorkloadGpuTest, PrimaryRecipePublishesAndRendersBothFamilies)
  {
    const auto workload = BuildLightingWorkload({});
    ASSERT_EQ(workload.lights.size(), 1024U);
    EXPECT_GE(
      CountGuaranteedVisibleContributors(workload, workload.views.front()),
      256U);
    auto native = CreateLightingWorkloadScene(workload);
    scene = native.scene;
    camera = native.cameras.front();
    mesh_node = native.floor;
    frame.SetScene(observer_ptr { scene.get() });
    auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
    ASSERT_TRUE(lens.has_value());
    // Same aspect/frustum and all 1,024 sources; a small correctness preview
    // keeps this out of the long official performance suite.
    constexpr std::uint32_t width = 64U;
    constexpr std::uint32_t height = 36U;
    view.viewport.width = static_cast<float>(width);
    view.viewport.height = static_cast<float>(height);
    lens->get().SetViewport(view.viewport);
    lens->get().SetAspectRatio(static_cast<float>(width) / height);
    auto output = CreateRegisteredTexture({
      .width = width,
      .height = height,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon,
    });
    framebuffer = Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(output));
    auto observed_counts = std::array<std::uint32_t, 5> {};
    probe->inspect = [&](const auto&, const auto&, unsigned) -> void {
      const auto* lighting
        = RendererPublicationProbe::GetLightingService(*renderer_);
      ASSERT_NE(lighting, nullptr);
      observed_counts = {
        lighting->GetLastGridBuildState().local_light_count,
        lighting->GetLastDeferredLightingState().point_light_count,
        lighting->GetLastDeferredLightingState().spot_light_count,
        lighting->GetLastDeferredLightingState().pipeline_bind_count,
        lighting->GetLastDeferredLightingState().local_light_draw_count,
      };
    };
    for (const bool forward : { false, true }) {
      SCOPED_TRACE(forward);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
      EXPECT_EQ(observed_counts.at(0), 1024U);
      if (!forward) {
        EXPECT_LE(observed_counts.at(3), 11U);
        EXPECT_LT(observed_counts.at(3), observed_counts.at(4));
        RecordProperty("deferred_pipeline_binds", observed_counts.at(3));
        RecordProperty("deferred_local_draws", observed_counts.at(4));
        EXPECT_LE(observed_counts.at(1), 512U);
        EXPECT_LE(observed_counts.at(2), 512U);
        const auto drawn = observed_counts.at(1) + observed_counts.at(2);
        EXPECT_GE(drawn,
          CountGuaranteedVisibleContributors(workload, workload.views.front()));
        EXPECT_LT(drawn, workload.lights.size());
      }
      const auto image = ReadFloatTexture(*probe->color);
      ASSERT_EQ(image.size(), width * height);
      unsigned lit = 0U;
      for (const auto& pixel : image) {
        for (const auto value : pixel) {
          EXPECT_TRUE(std::isfinite(value));
        }
        lit += pixel.front() > 1.0e-5F ? 1U : 0U;
      }
      EXPECT_GT(lit, width * height * 9U / 10U);
      RecordProperty(
        forward ? "forward_lit_preview_pixels" : "deferred_lit_preview_pixels",
        lit);
    }
    RecordProperty("source_lights", workload.lights.size());
    RecordProperty("guaranteed_visible_contributors",
      CountGuaranteedVisibleContributors(workload, workload.views.front()));
  }
} // namespace
} // namespace oxygen::vortex::testing
