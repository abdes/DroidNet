//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/FacadePresets.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>

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
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::testing::RendererOffscreenGpuTestFixture;
using oxygen::engine::testing::RunPass;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;

constexpr ViewId kTestViewId { 31U };

class LightCullingPassContractTest : public RendererOffscreenGpuTestFixture {
protected:
  [[nodiscard]] static auto MakeResolvedView(
    const std::uint32_t width, const std::uint32_t height) -> ResolvedView
  {
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(width),
      .bottom = static_cast<std::int32_t>(height),
    };

    return ResolvedView(ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = glm::mat4 { 1.0F },
      .proj_matrix = glm::mat4 { 1.0F },
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 1000.0F,
    });
  }

  auto MakeFramebuffer(const std::uint32_t width, const std::uint32_t height,
    std::string_view debug_name) -> std::shared_ptr<Framebuffer>
  {
    auto color_desc = TextureDesc {};
    color_desc.width = width;
    color_desc.height = height;
    color_desc.format = oxygen::Format::kRGBA8UNorm;
    color_desc.texture_type = oxygen::TextureType::kTexture2D;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.initial_state = ResourceStates::kCommon;
    color_desc.debug_name = std::string(debug_name) + ".Color";

    auto color = Backend().CreateTexture(color_desc);
    CHECK_NOTNULL_F(color.get(), "Failed to create light-culling framebuffer");
    auto framebuffer_desc = FramebufferDesc {};
    framebuffer_desc.AddColorAttachment({ .texture = color });
    auto framebuffer = Backend().CreateFramebuffer(framebuffer_desc);
    CHECK_NOTNULL_F(
      framebuffer.get(), "Failed to create light-culling framebuffer");
    return framebuffer;
  }

  auto ExecutePassTwiceSameRecorder(LightCullingPass& pass, Renderer& renderer,
    const ResolvedView& resolved_view, std::string_view debug_name) -> void
  {
    const auto viewport = resolved_view.Viewport();
    auto framebuffer
      = MakeFramebuffer(static_cast<std::uint32_t>(viewport.width),
        static_cast<std::uint32_t>(viewport.height), debug_name);
    auto harness = oxygen::renderer::harness::single_pass::presets::
      ForResolvedViewGraphicsPass(renderer,
        Renderer::FrameSessionInput {
          .frame_slot = Slot { 0U },
          .frame_sequence = SequenceNumber { 1U },
        },
        oxygen::observer_ptr<const Framebuffer> { framebuffer.get() },
        Renderer::ResolvedViewInput {
          .view_id = kTestViewId,
          .value = resolved_view,
        });
    auto harness_result = harness.Finalize();
    ASSERT_TRUE(harness_result.has_value());
    auto& render_context = harness_result->GetRenderContext();
    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      ASSERT_NE(recorder, nullptr);
      RunPass(pass, render_context, *recorder);
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }
};

NOLINT_TEST_F(LightCullingPassContractTest,
  PrepareAndExecuteWithoutDepthOrHzbPublishesFinalGridContract)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto pass
    = LightCullingPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<LightCullingPassConfig>(
        LightCullingPassConfig { .debug_name = "light-culling.contract" }));

  ExecutePassTwiceSameRecorder(
    pass, *renderer, MakeResolvedView(128U, 64U), "light-culling.contract");

  const auto dims = pass.GetGridDimensions();
  EXPECT_EQ(dims.x, 2U);
  EXPECT_EQ(dims.y, 1U);
  EXPECT_EQ(dims.z, LightCullingConfig::kLightGridSizeZ);
  EXPECT_EQ(dims.total_clusters, 2U * LightCullingConfig::kLightGridSizeZ);
  EXPECT_TRUE(pass.GetClusterGridSrvIndex().IsValid());
  EXPECT_TRUE(pass.GetLightIndexListSrvIndex().IsValid());
  EXPECT_NE(pass.GetClusterGridBuffer(), nullptr);
  EXPECT_NE(pass.GetLightIndexListBuffer(), nullptr);

  const auto& light_grid = pass.GetClusterConfig();
  EXPECT_TRUE(light_grid.IsAvailable());
  EXPECT_EQ(light_grid.cluster_dim_x, dims.x);
  EXPECT_EQ(light_grid.cluster_dim_y, dims.y);
  EXPECT_EQ(light_grid.cluster_dim_z, dims.z);
  EXPECT_EQ(light_grid.light_grid_pixel_size_shift,
    LightCullingConfig::kLightGridPixelSizeShift);
  EXPECT_EQ(light_grid.light_grid_pixel_size_px,
    LightCullingConfig::kLightGridPixelSize);
  EXPECT_EQ(light_grid.max_lights_per_cell,
    LightCullingConfig::kMaxCulledLightsPerCell);
}

NOLINT_TEST_F(LightCullingPassContractTest,
  PrepareResourcesRemainsRecorderSafeWhenInvokedRepeatedly)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto pass
    = LightCullingPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<LightCullingPassConfig>(
        LightCullingPassConfig { .debug_name = "light-culling.reprepare" }));

  NOLINT_EXPECT_NO_THROW(ExecutePassTwiceSameRecorder(
    pass, *renderer, MakeResolvedView(192U, 128U), "light-culling.reprepare"));
}

NOLINT_TEST_F(LightCullingPassContractTest,
  TelemetryDumpReportsCurrentPassStateWithoutDestructorLogging)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto pass
    = LightCullingPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<LightCullingPassConfig>(
        LightCullingPassConfig { .debug_name = "light-culling.telemetry" }));

  ExecutePassTwiceSameRecorder(
    pass, *renderer, MakeResolvedView(128U, 64U), "light-culling.telemetry");

  const auto telemetry_dump = pass.BuildTelemetryDump();
  EXPECT_THAT(
    telemetry_dump, ::testing::HasSubstr("pass: light-culling.telemetry"));
  EXPECT_THAT(telemetry_dump, ::testing::HasSubstr("latest_viewport: 128x64"));
  EXPECT_THAT(
    telemetry_dump, ::testing::HasSubstr("grid_dimensions: 2 x 1 x 32"));
  EXPECT_THAT(
    telemetry_dump, ::testing::HasSubstr("published_config_available: yes"));
  EXPECT_THAT(telemetry_dump, ::testing::HasSubstr("frames_prepared: 2"));
  EXPECT_THAT(telemetry_dump, ::testing::HasSubstr("frames_executed: 2"));
}

} // namespace
