//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Test/Fixtures/RendererOffscreenGpuTestFixture.h>

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::DepthPrePass;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::ScreenHzbBuildPass;
using oxygen::engine::ScreenHzbBuildPassConfig;
using oxygen::engine::testing::RendererOffscreenGpuTestFixture;
using oxygen::engine::testing::RunPass;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;

constexpr ViewId kTestViewId { 21U };
class ScreenHzbBuildGpuTest : public RendererOffscreenGpuTestFixture {
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
      .far_plane = 100.0F,
    });
  }

  auto CreateDepthTexture(const std::uint32_t width, const std::uint32_t height,
    std::string_view debug_name) -> std::shared_ptr<Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.format = Format::kDepth32;
    texture_desc.texture_type = TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.is_render_target = true;
    texture_desc.is_typeless = true;
    texture_desc.use_clear_value = true;
    texture_desc.clear_value
      = oxygen::graphics::Color { 1.0F, 0.0F, 0.0F, 0.0F };
    texture_desc.initial_state = ResourceStates::kCommon;
    texture_desc.debug_name = std::string(debug_name);
    return CreateRegisteredTexture(texture_desc);
  }

  auto SeedDepthQuadrants(const std::shared_ptr<Texture>& depth_texture,
    const float top_left, const float top_right, const float bottom_left,
    const float bottom_right, std::string_view debug_name) -> void
  {
    ASSERT_NE(depth_texture, nullptr);
    constexpr std::uint32_t kRowPitch = 256U;
    const auto width = depth_texture->GetDescriptor().width;
    const auto height = depth_texture->GetDescriptor().height;
    const auto half_width = width / 2U;
    const auto half_height = height / 2U;
    auto upload_bytes = std::vector<std::byte>(
      static_cast<std::size_t>(kRowPitch) * height, std::byte { 0 });

    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        const auto value = (y < half_height)
          ? (x < half_width ? top_left : top_right)
          : (x < half_width ? bottom_left : bottom_right);
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * kRowPitch
            + static_cast<std::size_t>(x) * sizeof(float),
          &value, sizeof(value));
      }
    }

    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(upload_bytes.size()),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    ASSERT_NE(upload, nullptr);
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".SeedDepth");
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*depth_texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = kRowPitch,
          .buffer_slice_pitch = kRowPitch * height,
          .dst_slice = {
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .width = width,
            .height = height,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        },
        *depth_texture);
      recorder->RequireResourceStateFinal(
        *depth_texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto ReadMipTexel(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t mip_level, const std::uint32_t x, const std::uint32_t y,
    std::string_view debug_name) -> float
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot read from a null HZB texture");
    constexpr std::uint32_t kRowPitch = 256U;
    auto readback = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kRowPitch,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(
      readback.get(), "Failed to create Screen HZB readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".ProbeCopy");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire Screen HZB recorder");
      RegisterResource(std::const_pointer_cast<Texture>(texture));
      recorder->BeginTrackingResourceState(
        *texture, ResourceStates::kShaderResource, true);
      EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);
      recorder->RequireResourceState(*texture, ResourceStates::kCopySource);
      recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *texture,
        oxygen::graphics::TextureBufferCopyRegion {
          .buffer_offset = oxygen::OffsetBytes { 0U },
          .buffer_row_pitch = oxygen::SizeBytes { kRowPitch },
          .texture_slice = {
            .x = x,
            .y = y,
            .z = 0U,
            .width = 1U,
            .height = 1U,
            .depth = 1U,
            .mip_level = mip_level,
            .array_slice = 0U,
          },
        });
    }
    WaitForQueueIdle();

    float value = 0.0F;
    const auto* mapped
      = static_cast<const std::byte*>(readback->Map(0U, kRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map Screen HZB readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  auto ExecutePass(ScreenHzbBuildPass& pass, Renderer& renderer,
    const std::shared_ptr<Texture>& depth_texture,
    const SequenceNumber frame_sequence, std::string_view debug_name) -> void
  {
    auto depth_config = std::make_shared<DepthPrePass::Config>();
    depth_config->depth_texture = depth_texture;
    depth_config->debug_name = std::string(debug_name) + ".DepthPrePass";
    auto depth_pass = DepthPrePass(depth_config);

    auto prepared_frame = PreparedSceneFrame {};
    auto offscreen = renderer.BeginOffscreenFrame(
      { .frame_slot = Slot { 0U }, .frame_sequence = frame_sequence });
    offscreen.SetCurrentView(kTestViewId,
      MakeResolvedView(depth_texture->GetDescriptor().width,
        depth_texture->GetDescriptor().height),
      prepared_frame);
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);
      oxygen::co::testing::TestEventLoop loop;
      oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
        co_await depth_pass.PrepareResources(render_context, *recorder);
        co_return;
      });
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }
};

NOLINT_TEST_F(ScreenHzbBuildGpuTest,
  BuildPassComputesExpectedMinPyramidFromDepthPrePassOutput)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto pass
    = ScreenHzbBuildPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<ScreenHzbBuildPassConfig>(
        ScreenHzbBuildPassConfig { .debug_name = "screen-hzb" }));

  auto depth = CreateDepthTexture(4U, 4U, "screen-hzb-depth");
  ASSERT_NE(depth, nullptr);
  SeedDepthQuadrants(depth, 0.8F, 0.6F, 0.4F, 0.2F, "screen-hzb-depth");

  ExecutePass(
    pass, *renderer, depth, SequenceNumber { 1U }, "screen-hzb.frame1");

  const auto current = pass.GetCurrentOutput(kTestViewId);
  const auto previous = pass.GetPreviousFrameOutput(kTestViewId);
  ASSERT_TRUE(current.available);
  EXPECT_FALSE(previous.available);
  EXPECT_NE(current.texture, nullptr);
  EXPECT_TRUE(current.srv_index.IsValid());
  EXPECT_EQ(current.width, 4U);
  EXPECT_EQ(current.height, 4U);
  EXPECT_EQ(current.mip_count, 3U);

  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 0U, 0U, 0U, "screen-hzb.mip0.00"), 0.8F);
  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 0U, 3U, 3U, "screen-hzb.mip0.33"), 0.2F);
  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 1U, 0U, 0U, "screen-hzb.mip1.00"), 0.8F);
  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 1U, 1U, 0U, "screen-hzb.mip1.10"), 0.6F);
  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 1U, 0U, 1U, "screen-hzb.mip1.01"), 0.4F);
  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 1U, 1U, 1U, "screen-hzb.mip1.11"), 0.2F);
  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 2U, 0U, 0U, "screen-hzb.mip2.00"), 0.2F);
}

NOLINT_TEST_F(
  ScreenHzbBuildGpuTest, PreviousFrameOutputTracksPriorPyramidAcrossFrames)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto pass
    = ScreenHzbBuildPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<ScreenHzbBuildPassConfig>(
        ScreenHzbBuildPassConfig { .debug_name = "screen-hzb-history" }));

  auto frame1_depth = CreateDepthTexture(4U, 4U, "screen-hzb-history.depth1");
  ASSERT_NE(frame1_depth, nullptr);
  SeedDepthQuadrants(
    frame1_depth, 0.9F, 0.7F, 0.5F, 0.3F, "screen-hzb-history.depth1");
  ExecutePass(pass, *renderer, frame1_depth, SequenceNumber { 1U },
    "screen-hzb-history.frame1");

  auto frame2_depth = CreateDepthTexture(4U, 4U, "screen-hzb-history.depth2");
  ASSERT_NE(frame2_depth, nullptr);
  SeedDepthQuadrants(
    frame2_depth, 0.85F, 0.65F, 0.45F, 0.25F, "screen-hzb-history.depth2");
  ExecutePass(pass, *renderer, frame2_depth, SequenceNumber { 2U },
    "screen-hzb-history.frame2");

  const auto previous = pass.GetPreviousFrameOutput(kTestViewId);
  const auto current = pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(previous.available);
  ASSERT_TRUE(current.available);
  EXPECT_NE(previous.texture, nullptr);
  EXPECT_NE(current.texture, nullptr);
  EXPECT_TRUE(previous.srv_index.IsValid());
  EXPECT_TRUE(current.srv_index.IsValid());

  EXPECT_FLOAT_EQ(
    ReadMipTexel(previous.texture, 2U, 0U, 0U, "screen-hzb-history.prev"),
    0.3F);
  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 2U, 0U, 0U, "screen-hzb-history.curr"),
    0.25F);
}

NOLINT_TEST_F(ScreenHzbBuildGpuTest,
  RecreatesResourcesAndDropsPreviousHistoryWhenViewResolutionChanges)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto pass
    = ScreenHzbBuildPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<ScreenHzbBuildPassConfig>(
        ScreenHzbBuildPassConfig { .debug_name = "screen-hzb-recreate" }));

  auto frame1_depth = CreateDepthTexture(4U, 4U, "screen-hzb-recreate.depth1");
  ASSERT_NE(frame1_depth, nullptr);
  SeedDepthQuadrants(
    frame1_depth, 0.9F, 0.7F, 0.5F, 0.3F, "screen-hzb-recreate.depth1");
  ExecutePass(pass, *renderer, frame1_depth, SequenceNumber { 1U },
    "screen-hzb-recreate.frame1");

  auto frame2_depth = CreateDepthTexture(8U, 8U, "screen-hzb-recreate.depth2");
  ASSERT_NE(frame2_depth, nullptr);
  SeedDepthQuadrants(
    frame2_depth, 0.8F, 0.6F, 0.4F, 0.2F, "screen-hzb-recreate.depth2");
  ExecutePass(pass, *renderer, frame2_depth, SequenceNumber { 2U },
    "screen-hzb-recreate.frame2");

  const auto previous = pass.GetPreviousFrameOutput(kTestViewId);
  const auto current = pass.GetCurrentOutput(kTestViewId);
  EXPECT_FALSE(previous.available);
  ASSERT_TRUE(current.available);
  EXPECT_EQ(current.width, 8U);
  EXPECT_EQ(current.height, 8U);
  EXPECT_EQ(current.mip_count, 4U);
  EXPECT_FLOAT_EQ(
    ReadMipTexel(current.texture, 3U, 0U, 0U, "screen-hzb-recreate.curr"),
    0.2F);
}

} // namespace
