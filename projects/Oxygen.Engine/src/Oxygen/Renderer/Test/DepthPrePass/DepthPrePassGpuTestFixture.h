//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

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
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/FacadePresets.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Test/Fixtures/RendererOffscreenGpuTestFixture.h>

namespace oxygen::engine::testing {

//! GPU test fixture for DepthPrePass tests.
/*!
 Adds depth-texture creation, depth-clear, depth-readback, and pass-execution
 helpers on top of RendererOffscreenGpuTestFixture.
*/
class DepthPrePassGpuTestFixture : public RendererOffscreenGpuTestFixture {
protected:
  static constexpr ViewId kTestViewId { 22U };
  static constexpr std::uint32_t kSingleTexelReadbackRowPitch = 256U;

  [[nodiscard]] static auto MakeResolvedView(const std::uint32_t width,
    const std::uint32_t height, const bool reverse_z = true,
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

  auto MakeDepthOnlyFramebuffer(
    const std::shared_ptr<graphics::Texture>& depth_texture,
    std::string_view debug_name) -> std::shared_ptr<graphics::Framebuffer>
  {
    auto fb_desc = graphics::FramebufferDesc {};
    fb_desc.SetDepthAttachment({ .texture = depth_texture });
    auto framebuffer = Backend().CreateFramebuffer(fb_desc);
    CHECK_NOTNULL_F(framebuffer.get(),
      "Failed to create depth-only framebuffer for '{}'", debug_name);
    return framebuffer;
  }

  auto CreateDepthTexture(const std::uint32_t width, const std::uint32_t height,
    std::string_view debug_name) -> std::shared_ptr<graphics::Texture>
  {
    auto texture_desc = graphics::TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.format = Format::kDepth32;
    texture_desc.texture_type = TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.is_render_target = true;
    texture_desc.is_typeless = true;
    texture_desc.use_clear_value = true;
    texture_desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
    texture_desc.initial_state = graphics::ResourceStates::kCommon;
    texture_desc.debug_name = std::string(debug_name);
    return Backend().CreateTexture(texture_desc);
  }

  auto ClearDepthTexture(const std::shared_ptr<graphics::Texture>& texture,
    const float depth_value, std::string_view debug_name) -> void
  {
    ASSERT_NE(texture, nullptr);

    auto& allocator
      = static_cast<Graphics&>(Backend()).GetDescriptorAllocator();
    auto& registry = Backend().GetResourceRegistry();
    registry.Register(texture);
    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_DSV,
        graphics::DescriptorVisibility::kCpuOnly);
    ASSERT_TRUE(handle.IsValid());

    const auto dsv_desc = graphics::TextureViewDescription {
      .view_type = graphics::ResourceViewType::kTexture_DSV,
      .visibility = graphics::DescriptorVisibility::kCpuOnly,
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
    auto dsv = registry.RegisterView(*texture, std::move(handle), dsv_desc);
    ASSERT_TRUE(dsv->IsValid());

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(*recorder, texture, graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *texture, graphics::ResourceStates::kDepthWrite);
      recorder->FlushBarriers();
      recorder->ClearDepthStencilView(
        *texture, dsv, graphics::ClearFlags::kDepth, depth_value, 0U);
      recorder->RequireResourceStateFinal(
        *texture, graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();
    registry.UnRegisterResource(*texture);
  }

  auto ReadDepthTexel(const std::shared_ptr<const graphics::Texture>& texture,
    const std::uint32_t x, const std::uint32_t y, std::string_view debug_name)
    -> float
  {
    auto writable_texture = std::const_pointer_cast<graphics::Texture>(texture);
    CHECK_NOTNULL_F(writable_texture.get(), "Cannot read a null depth texture");

    auto readback = CreateRegisteredBuffer(graphics::BufferDesc {
      .size_bytes = kSingleTexelReadbackRowPitch,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create readback buffer");
    auto& registry = Backend().GetResourceRegistry();
    registry.Register(writable_texture);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Probe");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(
        *recorder, writable_texture, graphics::ResourceStates::kDepthWrite);
      EnsureTracked(*recorder, readback, graphics::ResourceStates::kCopyDest);
      recorder->RequireResourceState(
        *writable_texture, graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *readback, graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *writable_texture,
        graphics::TextureBufferCopyRegion {
          .buffer_offset = OffsetBytes { 0U },
          .buffer_row_pitch = SizeBytes { kSingleTexelReadbackRowPitch },
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
    registry.UnRegisterResource(*writable_texture);
    return value;
  }

  auto ExecuteDepthPass(DepthPrePass& pass, Renderer& renderer,
    const std::shared_ptr<graphics::Texture>& depth_texture,
    const frame::SequenceNumber frame_sequence, std::string_view debug_name)
    -> void
  {
    auto depth_only_framebuffer
      = MakeDepthOnlyFramebuffer(depth_texture, debug_name);
    auto harness
      = renderer::harness::single_pass::presets::ForFullscreenGraphicsPass(
        renderer,
        Renderer::FrameSessionInput {
          .frame_slot = frame::Slot { 0U },
          .frame_sequence = frame_sequence,
        },
        observer_ptr<const graphics::Framebuffer> {
          depth_only_framebuffer.get(),
        },
        kTestViewId);
    auto harness_result = harness.Finalize();
    ASSERT_TRUE(harness_result.has_value());
    auto& render_context = harness_result->GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(
        *recorder, depth_texture, graphics::ResourceStates::kCommon);
      ASSERT_TRUE(recorder->IsResourceTracked(*depth_texture));
      recorder->RequireResourceState(
        *depth_texture, graphics::ResourceStates::kCommon);
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }

  auto ExecuteDepthPassWithPreparedFrame(DepthPrePass& pass, Renderer& renderer,
    const std::shared_ptr<graphics::Texture>& depth_texture,
    const ResolvedView& resolved_view, PreparedSceneFrame& prepared_frame,
    const ViewConstants& view_constants, const frame::Slot frame_slot,
    const frame::SequenceNumber frame_sequence, std::string_view debug_name)
    -> void
  {
    auto depth_only_framebuffer
      = MakeDepthOnlyFramebuffer(depth_texture, debug_name);
    auto harness
      = renderer::harness::single_pass::presets::ForPreparedSceneGraphicsPass(
        renderer,
        Renderer::FrameSessionInput {
          .frame_slot = frame_slot,
          .frame_sequence = frame_sequence,
        },
        observer_ptr<const graphics::Framebuffer> {
          depth_only_framebuffer.get(),
        },
        Renderer::ResolvedViewInput {
          .view_id = kTestViewId,
          .value = resolved_view,
        },
        Renderer::PreparedFrameInput { .value = prepared_frame });
    harness.SetCoreShaderInputs(Renderer::CoreShaderInputsInput {
      .view_id = kTestViewId,
      .value = view_constants,
    });
    auto harness_result = harness.Finalize();
    ASSERT_TRUE(harness_result.has_value());
    auto& render_context = harness_result->GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      ASSERT_NE(recorder, nullptr);
      EnsureTracked(
        *recorder, depth_texture, graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *depth_texture, graphics::ResourceStates::kCommon);
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }
};

} // namespace oxygen::engine::testing
