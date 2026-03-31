//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string_view>

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
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
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
using oxygen::engine::testing::RendererOffscreenGpuTestFixture;
using oxygen::engine::testing::RunPass;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;

constexpr ViewId kTestViewId { 22U };
constexpr std::uint32_t kSingleTexelReadbackRowPitch = 256U;

class DepthPrePassGpuTest : public RendererOffscreenGpuTestFixture {
protected:
  [[nodiscard]] static auto MakeResolvedView(const std::uint32_t width,
    const std::uint32_t height, const bool reverse_z = false,
    const NdcDepthRange depth_range = NdcDepthRange::ZeroToOne) -> ResolvedView
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
    view_config.reverse_z = reverse_z;
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
      .depth_range = depth_range,
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

  auto ClearDepthTexture(const std::shared_ptr<Texture>& texture,
    const float depth_value, std::string_view debug_name) -> void
  {
    ASSERT_NE(texture, nullptr);

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle
      = allocator.Allocate(oxygen::graphics::ResourceViewType::kTexture_DSV,
        oxygen::graphics::DescriptorVisibility::kCpuOnly);
    ASSERT_TRUE(handle.IsValid());

    const auto dsv_desc = oxygen::graphics::TextureViewDescription {
      .view_type = oxygen::graphics::ResourceViewType::kTexture_DSV,
      .visibility = oxygen::graphics::DescriptorVisibility::kCpuOnly,
      .format = texture->GetDescriptor().format,
      .dimension = texture->GetDescriptor().texture_type,
      .sub_resources = {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = 0U,
        .num_array_slices = 1U,
      },
      .is_read_only_dsv = false,
    };
    auto dsv = Backend().GetResourceRegistry().RegisterView(
      *texture, std::move(handle), dsv_desc);
    ASSERT_TRUE(dsv->IsValid());

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*texture, ResourceStates::kDepthWrite);
      recorder->FlushBarriers();
      recorder->ClearDepthStencilView(
        *texture, dsv, oxygen::graphics::ClearFlags::kDepth, depth_value, 0U);
      recorder->RequireResourceStateFinal(*texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto ReadDepthTexel(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t x, const std::uint32_t y, std::string_view debug_name)
    -> float
  {
    auto writable_texture = std::const_pointer_cast<Texture>(texture);
    CHECK_NOTNULL_F(writable_texture.get(), "Cannot read a null depth texture");

    auto readback = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kSingleTexelReadbackRowPitch,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Probe");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire depth probe recorder");
      EnsureTracked(*recorder, writable_texture, ResourceStates::kDepthWrite);
      EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);
      recorder->RequireResourceState(
        *writable_texture, ResourceStates::kCopySource);
      recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *writable_texture,
        oxygen::graphics::TextureBufferCopyRegion {
          .buffer_offset = oxygen::OffsetBytes { 0U },
          .buffer_row_pitch
          = oxygen::SizeBytes { kSingleTexelReadbackRowPitch },
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

    float value = 0.0F;
    const auto* mapped = static_cast<const std::byte*>(
      readback->Map(0U, kSingleTexelReadbackRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map depth readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  auto ExecuteDepthPass(DepthPrePass& pass, Renderer& renderer,
    const std::shared_ptr<Texture>& depth_texture,
    const SequenceNumber frame_sequence, std::string_view debug_name) -> void
  {
    auto prepared_frame = PreparedSceneFrame {};
    auto offscreen = renderer.BeginOffscreenFrame(
      { .frame_slot = Slot { 0U }, .frame_sequence = frame_sequence });
    offscreen.SetCurrentView(kTestViewId,
      MakeResolvedView(depth_texture->GetDescriptor().width,
        depth_texture->GetDescriptor().height),
      prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);
      ASSERT_TRUE(recorder->IsResourceTracked(*depth_texture));
      recorder->RequireResourceState(*depth_texture, ResourceStates::kCommon);
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }
};

NOLINT_TEST_F(DepthPrePassGpuTest, OutputDefaultsToFullTextureRectWhenUnset)
{
  auto depth_texture = CreateDepthTexture(8U, 6U, "depth-prepass.output.full");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "depth-prepass.output.full",
    }));

  const auto output = pass.GetOutput();
  ASSERT_NE(output.depth_texture, nullptr);
  EXPECT_EQ(output.depth_texture, depth_texture.get());
  EXPECT_FALSE(output.canonical_srv_index.IsValid());
  EXPECT_EQ(output.width, 8U);
  EXPECT_EQ(output.height, 6U);
  EXPECT_FLOAT_EQ(output.viewport.top_left_x, 0.0F);
  EXPECT_FLOAT_EQ(output.viewport.top_left_y, 0.0F);
  EXPECT_FLOAT_EQ(output.viewport.width, 8.0F);
  EXPECT_FLOAT_EQ(output.viewport.height, 6.0F);
  EXPECT_EQ(output.scissors.left, 0);
  EXPECT_EQ(output.scissors.top, 0);
  EXPECT_EQ(output.scissors.right, 8);
  EXPECT_EQ(output.scissors.bottom, 6);
  EXPECT_EQ(output.valid_rect.left, 0);
  EXPECT_EQ(output.valid_rect.top, 0);
  EXPECT_EQ(output.valid_rect.right, 8);
  EXPECT_EQ(output.valid_rect.bottom, 6);
  EXPECT_EQ(output.ndc_depth_range, NdcDepthRange::ZeroToOne);
  EXPECT_FALSE(output.reverse_z);
  EXPECT_TRUE(output.has_depth_texture);
  EXPECT_FALSE(output.has_canonical_srv);
  EXPECT_FALSE(output.is_complete);
}

NOLINT_TEST_F(
  DepthPrePassGpuTest, RejectsViewportScissorsPairsThatProduceEmptyDepthRect)
{
  auto depth_texture = CreateDepthTexture(4U, 4U, "depth-prepass.empty-rect");
  ASSERT_NE(depth_texture, nullptr);

  auto make_pass = [&]() {
    return DepthPrePass(
      std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
        .depth_texture = depth_texture,
        .debug_name = "depth-prepass.empty-rect",
      }));
  };

  {
    auto pass = make_pass();
    pass.SetViewport(ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 2.0F,
      .height = 4.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
    EXPECT_THROW(pass.SetScissors(
                   Scissors { .left = 2, .top = 0, .right = 4, .bottom = 4 }),
      std::invalid_argument);
  }

  {
    auto pass = make_pass();
    pass.SetScissors(Scissors { .left = 2, .top = 0, .right = 4, .bottom = 4 });
    EXPECT_THROW(pass.SetViewport(ViewPort {
                   .top_left_x = 0.0F,
                   .top_left_y = 0.0F,
                   .width = 2.0F,
                   .height = 4.0F,
                   .min_depth = 0.0F,
                   .max_depth = 1.0F,
                 }),
      std::invalid_argument);
  }
}

NOLINT_TEST_F(DepthPrePassGpuTest, ClearHonorsEffectiveDepthRectIntersection)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_texture = CreateDepthTexture(4U, 4U, "depth-prepass.clipped");
  ASSERT_NE(depth_texture, nullptr);
  ClearDepthTexture(depth_texture, 0.25F, "depth-prepass.clipped.seed");

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "depth-prepass.clipped",
    }));
  pass.SetViewport(ViewPort {
    .top_left_x = 1.0F,
    .top_left_y = 0.0F,
    .width = 3.0F,
    .height = 4.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  pass.SetScissors(Scissors { .left = 0, .top = 1, .right = 4, .bottom = 3 });

  const auto output = pass.GetOutput();
  EXPECT_FLOAT_EQ(output.viewport.top_left_x, 1.0F);
  EXPECT_FLOAT_EQ(output.viewport.top_left_y, 0.0F);
  EXPECT_FLOAT_EQ(output.viewport.width, 3.0F);
  EXPECT_FLOAT_EQ(output.viewport.height, 4.0F);
  EXPECT_EQ(output.scissors.left, 0);
  EXPECT_EQ(output.scissors.top, 1);
  EXPECT_EQ(output.scissors.right, 4);
  EXPECT_EQ(output.scissors.bottom, 3);
  EXPECT_EQ(output.valid_rect.left, 1);
  EXPECT_EQ(output.valid_rect.top, 1);
  EXPECT_EQ(output.valid_rect.right, 4);
  EXPECT_EQ(output.valid_rect.bottom, 3);
  EXPECT_EQ(output.width, 4U);
  EXPECT_EQ(output.height, 4U);
  EXPECT_TRUE(output.has_depth_texture);
  EXPECT_FALSE(output.has_canonical_srv);
  EXPECT_FALSE(output.is_complete);

  ExecuteDepthPass(pass, *renderer, depth_texture, SequenceNumber { 1U },
    "depth-prepass.clipped.execute");

  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 0U, 0U, "depth-prepass.clipped.outside.tl"),
    0.25F);
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 0U, 3U, "depth-prepass.clipped.outside.bl"),
    0.25F);
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 2U, 0U, "depth-prepass.clipped.outside.top"),
    0.25F);
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 1U, 1U, "depth-prepass.clipped.inside.a"),
    1.0F);
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 3U, 2U, "depth-prepass.clipped.inside.b"),
    1.0F);
}

NOLINT_TEST_F(DepthPrePassGpuTest,
  ExecutionPublishesCanonicalDepthProductsForDownstreamPasses)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_texture
    = CreateDepthTexture(6U, 5U, "depth-prepass.products.texture");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "depth-prepass.products",
    }));

  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 7U } });
  offscreen.SetCurrentView(kTestViewId,
    MakeResolvedView(depth_texture->GetDescriptor().width,
      depth_texture->GetDescriptor().height, true, NdcDepthRange::ZeroToOne),
    prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  {
    auto recorder = AcquireRecorder("depth-prepass.products.execute");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto output = pass.GetOutput();
  ASSERT_NE(output.depth_texture, nullptr);
  EXPECT_EQ(output.depth_texture, depth_texture.get());
  EXPECT_TRUE(output.canonical_srv_index.IsValid());
  EXPECT_EQ(output.width, 6U);
  EXPECT_EQ(output.height, 5U);
  EXPECT_EQ(output.ndc_depth_range, NdcDepthRange::ZeroToOne);
  EXPECT_TRUE(output.reverse_z);
  EXPECT_TRUE(output.has_depth_texture);
  EXPECT_TRUE(output.has_canonical_srv);
  EXPECT_TRUE(output.is_complete);
}

} // namespace
