//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::VsmProjectionPass;
using oxygen::engine::VsmProjectionPassConfig;
using oxygen::engine::VsmProjectionPassInput;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::renderer::vsm::MakeMappedShaderPageTableEntry;
using oxygen::renderer::vsm::MakeUnmappedShaderPageTableEntry;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPoolConfig;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::testing::VsmCacheManagerGpuTestBase;

constexpr ViewId kTestViewId { 27U };
constexpr std::uint32_t kRowPitchAlignment = 256U;

class VsmProjectionPassGpuTest : public VsmCacheManagerGpuTestBase {
protected:
  [[nodiscard]] static auto AlignUp(
    const std::uint32_t value, const std::uint32_t alignment) -> std::uint32_t
  {
    CHECK_NE_F(alignment, 0U, "alignment must be non-zero");
    return (value + alignment - 1U) / alignment * alignment;
  }

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

  [[nodiscard]] static auto MakeSmallShadowPoolConfig(const char* debug_name)
    -> VsmPhysicalPoolConfig
  {
    return VsmPhysicalPoolConfig {
      .page_size_texels = 4U,
      .physical_tile_capacity = 1U,
      .array_slice_count = 1U,
      .depth_format = Format::kDepth32,
      .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth },
      .debug_name = debug_name,
    };
  }

  static auto CommitFrame(VsmCacheManager& manager)
    -> const VsmPageAllocationFrame&
  {
    static_cast<void>(manager.BuildPageAllocationPlan());
    return manager.CommitPageAllocationFrame();
  }

  [[nodiscard]] static auto MakeDirectionalFrame(
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const char* frame_name)
    -> oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = frame_name,
      },
      frame_generation);
    address_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
      .remap_key = "directional-0",
      .clip_level_count = 1U,
      .pages_per_axis = 1U,
      .page_grid_origin = { glm::ivec2 { 0, 0 } },
      .page_world_size = { 1.0F },
      .near_depth = { 0.0F },
      .far_depth = { 1.0F },
      .debug_name = "directional-0",
    });
    return address_space.DescribeFrame();
  }

  [[nodiscard]] static auto MakeDirectionalProjectionRecord(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame)
    -> VsmPageRequestProjection
  {
    const auto& layout = frame.directional_layouts[0];
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(
          VsmProjectionLightType::kDirectional),
      },
      .map_id = layout.first_id,
      .first_page_table_entry = layout.first_page_table_entry,
      .map_pages_x = layout.pages_per_axis,
      .map_pages_y = layout.pages_per_axis,
      .pages_x = layout.pages_per_axis,
      .pages_y = layout.pages_per_axis,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = layout.clip_level_count,
      .coarse_level = 0U,
    };
  }

  [[nodiscard]] static auto MakeLocalProjectionRecord(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame)
    -> VsmPageRequestProjection
  {
    const auto& layout = frame.local_light_layouts[0];
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 0.0F },
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
      .light_index = 0U,
    };
  }

  auto CreateDepthTexture(const std::uint32_t width, const std::uint32_t height,
    std::string_view debug_name) -> std::shared_ptr<Texture>
  {
    auto desc = oxygen::graphics::TextureDesc {};
    desc.width = width;
    desc.height = height;
    desc.format = Format::kDepth32;
    desc.texture_type = TextureType::kTexture2D;
    desc.is_shader_resource = true;
    desc.is_render_target = true;
    desc.use_clear_value = true;
    desc.clear_value = oxygen::graphics::Color { 1.0F, 0.0F, 0.0F, 0.0F };
    desc.initial_state = ResourceStates::kCommon;
    desc.debug_name = std::string(debug_name);
    return CreateRegisteredTexture(desc);
  }

  auto UploadDepth(const std::shared_ptr<Texture>& texture,
    const std::vector<float>& values, std::string_view debug_name) -> void
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot upload into a null depth texture");
    const auto width = texture->GetDescriptor().width;
    const auto height = texture->GetDescriptor().height;
    CHECK_EQ_F(values.size(), static_cast<std::size_t>(width) * height,
      "Depth upload size must match the texture extent");

    const auto row_pitch = AlignUp(width * sizeof(float), kRowPitchAlignment);
    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(row_pitch) * height,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    CHECK_NOTNULL_F(upload.get(), "Failed to create depth upload buffer");

    auto upload_bytes = std::vector<std::byte>(
      static_cast<std::size_t>(row_pitch) * height, std::byte { 0 });
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        const auto value = values[static_cast<std::size_t>(y) * width + x];
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * row_pitch
            + static_cast<std::size_t>(x) * sizeof(float),
          &value, sizeof(value));
      }
    }
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".UploadDepth");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = row_pitch,
          .buffer_slice_pitch = row_pitch * height,
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
        *texture);
      recorder->RequireResourceStateFinal(*texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto UploadShadowSlice(const std::shared_ptr<const Texture>& texture,
    const std::vector<float>& values, std::string_view debug_name) -> void
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot upload into a null shadow texture");
    const auto width = texture->GetDescriptor().width;
    const auto height = texture->GetDescriptor().height;
    CHECK_EQ_F(values.size(), static_cast<std::size_t>(width) * height,
      "Shadow upload size must match the texture extent");

    const auto row_pitch = AlignUp(width * sizeof(float), kRowPitchAlignment);
    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(row_pitch) * height,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    CHECK_NOTNULL_F(upload.get(), "Failed to create shadow upload buffer");

    auto upload_bytes = std::vector<std::byte>(
      static_cast<std::size_t>(row_pitch) * height, std::byte { 0 });
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        const auto value = values[static_cast<std::size_t>(y) * width + x];
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * row_pitch
            + static_cast<std::size_t>(x) * sizeof(float),
          &value, sizeof(value));
      }
    }
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder
        = AcquireRecorder(std::string(debug_name) + ".UploadShadow");
      CHECK_NOTNULL_F(recorder.get());
      auto writable_texture = std::const_pointer_cast<Texture>(texture);
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, writable_texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *writable_texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = row_pitch,
          .buffer_slice_pitch = row_pitch * height,
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
        *writable_texture);
      recorder->RequireResourceStateFinal(
        *writable_texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto ReadOutputTexel(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t x, const std::uint32_t y, std::string_view debug_name)
    -> float
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot read back a null shadow mask");
    constexpr std::uint32_t kRowPitch = 256U;
    auto readback = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = kRowPitch,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create shadow-mask readback");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Readback");
      CHECK_NOTNULL_F(recorder.get());
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
            .mip_level = 0U,
            .array_slice = 0U,
          },
        });
    }
    WaitForQueueIdle();

    float value = 0.0F;
    const auto* mapped
      = static_cast<const std::byte*>(readback->Map(0U, kRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map shadow-mask readback");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  auto ExecutePass(VsmProjectionPass& pass, const VsmProjectionPassInput& input,
    std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    pass.SetInput(input);

    auto prepared_frame = PreparedSceneFrame {};
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
    offscreen.SetCurrentView(kTestViewId,
      MakeResolvedView(input.scene_depth_texture->GetDescriptor().width,
        input.scene_depth_texture->GetDescriptor().height),
      prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }
};

NOLINT_TEST_F(VsmProjectionPassGpuTest,
  DirectionalProjectionPassBuildsScreenShadowMaskFromMappedVirtualPage)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeSmallShadowPoolConfig("phase-i-directional-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto virtual_frame
    = MakeDirectionalFrame(1ULL, 7U, "phase-i-directional-frame");
  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-i-directional-frame" });
  static_cast<void>(CommitFrame(manager));
  manager.PublishProjectionRecords(std::array {
    MakeDirectionalProjectionRecord(virtual_frame),
  });
  auto frame = *manager.GetCurrentFrame();

  frame.snapshot.page_table[0] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 0U },
  };
  auto shader_page_table = std::vector<VsmShaderPageTableEntry>(
    frame.snapshot.page_table.size(), MakeUnmappedShaderPageTableEntry());
  shader_page_table[0]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 0U });
  UploadBufferBytes(std::const_pointer_cast<Buffer>(frame.page_table_buffer),
    shader_page_table.data(),
    shader_page_table.size() * sizeof(VsmShaderPageTableEntry),
    "phase-i-directional.page-table");

  auto depth_texture = CreateDepthTexture(4U, 4U, "phase-i-directional.depth");
  UploadDepth(
    depth_texture, std::vector<float>(16U, 0.5F), "phase-i-directional.depth");
  UploadShadowSlice(pool_manager.GetShadowPoolSnapshot().shadow_texture,
    {
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
    },
    "phase-i-directional.shadow");

  auto pass
    = VsmProjectionPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmProjectionPassConfig>(
        VsmProjectionPassConfig { .debug_name = "phase-i-directional-pass" }));
  ExecutePass(pass,
    VsmProjectionPassInput {
      .frame = frame,
      .physical_pool = pool_manager.GetShadowPoolSnapshot(),
      .scene_depth_texture = depth_texture,
    },
    "phase-i-directional-pass");

  const auto output = pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.shadow_mask_texture, nullptr);
  EXPECT_NEAR(ReadOutputTexel(
                output.shadow_mask_texture, 0U, 1U, "phase-i-directional-left"),
    0.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.shadow_mask_texture, 3U, 1U,
                "phase-i-directional-right"),
    1.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmProjectionPassGpuTest,
  LocalProjectionPerLightPassBuildsScreenShadowMaskFromMappedVirtualPage)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeSmallShadowPoolConfig("phase-i-local-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto address_space = VsmVirtualAddressSpace {};
  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 21U,
      .debug_name = "phase-i-local-frame",
    },
    2ULL);
  address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {
    .remap_key = "local-0",
    .debug_name = "local-0",
  });
  const auto virtual_frame = address_space.DescribeFrame();

  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-i-local-frame" });
  static_cast<void>(CommitFrame(manager));
  manager.PublishProjectionRecords(std::array {
    MakeLocalProjectionRecord(virtual_frame),
  });
  auto frame = *manager.GetCurrentFrame();

  frame.snapshot.page_table[0] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 0U },
  };
  auto shader_page_table = std::vector<VsmShaderPageTableEntry>(
    frame.snapshot.page_table.size(), MakeUnmappedShaderPageTableEntry());
  shader_page_table[0]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 0U });
  UploadBufferBytes(std::const_pointer_cast<Buffer>(frame.page_table_buffer),
    shader_page_table.data(),
    shader_page_table.size() * sizeof(VsmShaderPageTableEntry),
    "phase-i-local.page-table");

  auto depth_texture = CreateDepthTexture(4U, 4U, "phase-i-local.depth");
  UploadDepth(
    depth_texture, std::vector<float>(16U, 0.5F), "phase-i-local.depth");
  UploadShadowSlice(pool_manager.GetShadowPoolSnapshot().shadow_texture,
    {
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
    },
    "phase-i-local.shadow");

  auto pass
    = VsmProjectionPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmProjectionPassConfig>(
        VsmProjectionPassConfig { .debug_name = "phase-i-local-pass" }));
  ExecutePass(pass,
    VsmProjectionPassInput {
      .frame = frame,
      .physical_pool = pool_manager.GetShadowPoolSnapshot(),
      .scene_depth_texture = depth_texture,
    },
    "phase-i-local-pass");

  const auto output = pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.shadow_mask_texture, nullptr);
  EXPECT_NEAR(
    ReadOutputTexel(output.shadow_mask_texture, 0U, 1U, "phase-i-local-left"),
    0.0F, 1.0e-4F);
  EXPECT_NEAR(
    ReadOutputTexel(output.shadow_mask_texture, 3U, 1U, "phase-i-local-right"),
    1.0F, 1.0e-4F);
}

} // namespace
