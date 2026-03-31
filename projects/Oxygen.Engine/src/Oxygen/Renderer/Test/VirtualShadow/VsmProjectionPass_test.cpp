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

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawFrameBindings.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowStageGpuHarness.h"

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::BindlessDrawMetadataSlot;
using oxygen::engine::BindlessWorldsSlot;
using oxygen::engine::DrawFrameBindings;
using oxygen::engine::DrawMetadata;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::ViewFrameBindings;
using oxygen::engine::VsmProjectionPass;
using oxygen::engine::VsmProjectionPassConfig;
using oxygen::engine::VsmProjectionPassInput;
using oxygen::engine::VsmShadowRasterizerPass;
using oxygen::engine::VsmShadowRasterizerPassConfig;
using oxygen::engine::VsmShadowRasterizerPassInput;
using oxygen::engine::internal::PerViewStructuredPublisher;
using oxygen::engine::upload::TransientStructuredBuffer;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::renderer::vsm::MakeMappedShaderPageTableEntry;
using oxygen::renderer::vsm::MakeUnmappedShaderPageTableEntry;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmPageAllocationDecision;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageAllocationSnapshot;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
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
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::StageMeshVertex;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

constexpr ViewId kTestViewId { 27U };
constexpr std::uint32_t kRowPitchAlignment = 256U;
constexpr std::uint32_t kRasterizedMapId = 17U;
constexpr std::uint32_t kRasterizedPageTableEntry = 8U;

class VsmProjectionPassGpuTest : public VsmStageGpuHarness {
protected:
  struct AllocatedPageSpec {
    VsmVirtualPageCoord virtual_page {
      .level = 0U,
      .page_x = 0U,
      .page_y = 0U,
    };
    std::uint32_t physical_page { 0U };
    VsmPageRequestFlags flags { VsmPageRequestFlags::kRequired };
  };

  [[nodiscard]] static auto MakeStaticDynamicShadowPoolConfig(
    const std::uint32_t page_size, const std::uint32_t physical_tile_capacity,
    const char* debug_name) -> VsmPhysicalPoolConfig
  {
    return VsmPhysicalPoolConfig {
      .page_size_texels = page_size,
      .physical_tile_capacity = physical_tile_capacity,
      .array_slice_count = 2U,
      .depth_format = Format::kDepth32,
      .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth,
        VsmPhysicalPoolSliceRole::kStaticDepth },
      .debug_name = debug_name,
    };
  }

  [[nodiscard]] static auto MakeDirectionalFrame(
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const char* frame_name, const std::uint32_t clip_level_count = 1U,
    const std::uint32_t pages_per_axis = 1U)
    -> oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame
  {
    auto page_grid_origin = std::vector<glm::ivec2>(clip_level_count);
    auto page_world_size = std::vector<float> {};
    auto near_depth = std::vector<float> {};
    auto far_depth = std::vector<float> {};
    page_world_size.reserve(clip_level_count);
    near_depth.reserve(clip_level_count);
    far_depth.reserve(clip_level_count);
    for (std::uint32_t clip_level = 0U; clip_level < clip_level_count;
      ++clip_level) {
      page_world_size.push_back(1.0F + static_cast<float>(clip_level));
      near_depth.push_back(static_cast<float>(clip_level));
      far_depth.push_back(static_cast<float>(clip_level + 1U));
    }

    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = frame_name,
      },
      frame_generation);
    address_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
      .remap_key = "directional-0",
      .clip_level_count = clip_level_count,
      .pages_per_axis = pages_per_axis,
      .page_grid_origin = std::move(page_grid_origin),
      .page_world_size = std::move(page_world_size),
      .near_depth = std::move(near_depth),
      .far_depth = std::move(far_depth),
      .debug_name = "directional-0",
    });
    return address_space.DescribeFrame();
  }

  [[nodiscard]] static auto MakeDirectionalProjectionRecord(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame,
    const std::uint32_t clip_level = 0U) -> VsmPageRequestProjection
  {
    const auto& layout = frame.directional_layouts[0];
    CHECK_LT_F(clip_level, layout.clip_level_count,
      "directional clip level {} exceeds layout clip level count {}",
      clip_level, layout.clip_level_count);
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = clip_level,
        .light_type = static_cast<std::uint32_t>(
          VsmProjectionLightType::kDirectional),
      },
      .map_id = layout.first_id + clip_level,
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

  [[nodiscard]] static auto MakeDirectionalProjectionRecordWithMatrices(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame,
    const glm::mat4& view_matrix, const glm::mat4& projection_matrix,
    const glm::ivec2 clipmap_corner_offset, const std::uint32_t clip_level = 0U)
    -> VsmPageRequestProjection
  {
    const auto& layout = frame.directional_layouts[0];
    CHECK_LT_F(clip_level, layout.clip_level_count,
      "directional clip level {} exceeds layout clip level count {}",
      clip_level, layout.clip_level_count);
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = view_matrix,
        .projection_matrix = projection_matrix,
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = clipmap_corner_offset,
        .clipmap_level = clip_level,
        .light_type = static_cast<std::uint32_t>(
          VsmProjectionLightType::kDirectional),
      },
      .map_id = layout.first_id + clip_level,
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

  [[nodiscard]] static auto MakeAllocatedFrame(
    std::span<const AllocatedPageSpec> pages) -> VsmPageAllocationFrame
  {
    auto frame = VsmPageAllocationFrame {};
    frame.snapshot = VsmPageAllocationSnapshot {};
    frame.plan.decisions.reserve(pages.size());
    for (const auto& page : pages) {
      frame.plan.decisions.push_back(VsmPageAllocationDecision {
        .request = VsmPageRequest {
          .map_id = kRasterizedMapId,
          .page = page.virtual_page,
          .flags = page.flags,
        },
        .action = VsmAllocationAction::kAllocateNew,
        .current_physical_page
        = VsmPhysicalPageIndex { .value = page.physical_page },
      });
    }
    frame.plan.allocated_page_count
      = static_cast<std::uint32_t>(frame.plan.decisions.size());
    frame.is_ready = true;
    return frame;
  }

  [[nodiscard]] static auto MakeRasterizedDirectionalProjection(
    const std::uint32_t pages_x, const std::uint32_t pages_y)
    -> VsmPageRequestProjection
  {
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 1.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(
          VsmProjectionLightType::kDirectional),
      },
      .map_id = kRasterizedMapId,
      .first_page_table_entry = kRasterizedPageTableEntry,
      .map_pages_x = pages_x,
      .map_pages_y = pages_y,
      .pages_x = pages_x,
      .pages_y = pages_y,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = 1U,
      .coarse_level = 0U,
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
    desc.is_typeless = true;
    desc.use_clear_value = true;
    desc.clear_value = oxygen::graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
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
    const std::vector<float>& values, std::string_view debug_name,
    const std::uint32_t array_slice = 0U) -> void
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
             .array_slice = array_slice,
           },
          .dst_subresources = {
            .base_mip_level = 0U,
            .num_mip_levels = 1U,
            .base_array_slice = array_slice,
            .num_array_slices = 1U,
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
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeSingleSliceShadowPoolConfig(
              4U, 1U, "phase-i-directional-shadow")),
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
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);
  ASSERT_NE(output.shadow_mask_texture, nullptr);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 0U, 1U,
                "phase-i-directional-directional-left"),
    0.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 3U, 1U,
                "phase-i-directional-directional-right"),
    1.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(
                output.shadow_mask_texture, 0U, 1U, "phase-i-directional-left"),
    0.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.shadow_mask_texture, 3U, 1U,
                "phase-i-directional-right"),
    1.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmProjectionPassGpuTest,
  DirectionalProjectionPassMapsDistinctPagesToExpectedScreenQuadrants)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeSingleSliceShadowPoolConfig(
              4U, 4U, "phase-i-directional-quadrants-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto virtual_frame = MakeDirectionalFrame(
    9ULL, 29U, "phase-i-directional-quadrants-frame", 1U, 2U);
  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "phase-i-directional-quadrants-frame",
    });
  static_cast<void>(CommitFrame(manager));
  manager.PublishProjectionRecords(std::array {
    MakeDirectionalProjectionRecord(virtual_frame),
  });
  auto frame = *manager.GetCurrentFrame();

  ASSERT_GE(frame.snapshot.page_table.size(), 4U);
  frame.snapshot.page_table[0] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 0U },
  };
  frame.snapshot.page_table[1] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 1U },
  };
  frame.snapshot.page_table[2] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 2U },
  };
  frame.snapshot.page_table[3] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 3U },
  };

  auto shader_page_table = std::vector<VsmShaderPageTableEntry>(
    frame.snapshot.page_table.size(), MakeUnmappedShaderPageTableEntry());
  shader_page_table[0]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 0U });
  shader_page_table[1]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 1U });
  shader_page_table[2]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 2U });
  shader_page_table[3]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 3U });
  UploadBufferBytes(std::const_pointer_cast<Buffer>(frame.page_table_buffer),
    shader_page_table.data(),
    shader_page_table.size() * sizeof(VsmShaderPageTableEntry),
    "phase-i-directional-quadrants.page-table");

  auto depth_texture
    = CreateDepthTexture(4U, 4U, "phase-i-directional-quadrants.depth");
  UploadDepth(depth_texture, std::vector<float>(16U, 0.5F),
    "phase-i-directional-quadrants.depth");
  UploadShadowSlice(pool_manager.GetShadowPoolSnapshot().shadow_texture,
    {
      0.25F,
      0.25F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.25F,
      0.25F,
      0.75F,
      0.75F,
      0.75F,
      0.75F,
      0.25F,
      0.25F,
      0.25F,
      0.25F,
    },
    "phase-i-directional-quadrants.shadow");

  auto pass
    = VsmProjectionPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmProjectionPassConfig>(VsmProjectionPassConfig {
        .debug_name = "phase-i-directional-quadrants-pass",
      }));
  ExecutePass(pass,
    VsmProjectionPassInput {
      .frame = frame,
      .physical_pool = pool_manager.GetShadowPoolSnapshot(),
      .scene_depth_texture = depth_texture,
    },
    "phase-i-directional-quadrants-pass");

  const auto output = pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);
  ASSERT_NE(output.shadow_mask_texture, nullptr);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 0U, 0U,
                "phase-i-directional-quadrants.top-left"),
    0.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 3U, 0U,
                "phase-i-directional-quadrants.top-right"),
    1.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 0U, 3U,
                "phase-i-directional-quadrants.bottom-left"),
    1.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 3U, 3U,
                "phase-i-directional-quadrants.bottom-right"),
    0.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmProjectionPassGpuTest,
  DirectionalProjectionPassSamplesMappedPhysicalSliceInsteadOfGlobalDynamicSlice)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeStaticDynamicShadowPoolConfig(
              4U, 2U, "phase-i-directional-static-slice-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto virtual_frame = MakeDirectionalFrame(
    11ULL, 41U, "phase-i-directional-static-slice-frame");
  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "phase-i-directional-static-slice-frame",
    });
  static_cast<void>(CommitFrame(manager));
  manager.PublishProjectionRecords(std::array {
    MakeDirectionalProjectionRecord(virtual_frame),
  });
  auto frame = *manager.GetCurrentFrame();

  ASSERT_FALSE(frame.snapshot.page_table.empty());
  frame.snapshot.page_table[0] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 1U },
  };
  auto shader_page_table = std::vector<VsmShaderPageTableEntry>(
    frame.snapshot.page_table.size(), MakeUnmappedShaderPageTableEntry());
  shader_page_table[0]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 1U });
  UploadBufferBytes(std::const_pointer_cast<Buffer>(frame.page_table_buffer),
    shader_page_table.data(),
    shader_page_table.size() * sizeof(VsmShaderPageTableEntry),
    "phase-i-directional-static-slice.page-table");

  auto depth_texture
    = CreateDepthTexture(4U, 4U, "phase-i-directional-static-slice.depth");
  UploadDepth(depth_texture, std::vector<float>(16U, 0.5F),
    "phase-i-directional-static-slice.depth");
  UploadShadowSlice(pool_manager.GetShadowPoolSnapshot().shadow_texture,
    std::vector<float>(16U, 0.0F),
    "phase-i-directional-static-slice.shadow-dynamic", 0U);
  UploadShadowSlice(pool_manager.GetShadowPoolSnapshot().shadow_texture,
    std::vector<float>(16U, 1.0F),
    "phase-i-directional-static-slice.shadow-static", 1U);

  auto pass
    = VsmProjectionPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmProjectionPassConfig>(VsmProjectionPassConfig {
        .debug_name = "phase-i-directional-static-slice-pass",
      }));
  ExecutePass(pass,
    VsmProjectionPassInput {
      .frame = frame,
      .physical_pool = pool_manager.GetShadowPoolSnapshot(),
      .scene_depth_texture = depth_texture,
    },
    "phase-i-directional-static-slice-pass");

  const auto output = pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);
  ASSERT_NE(output.shadow_mask_texture, nullptr);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 1U, 1U,
                "phase-i-directional-static-slice.directional"),
    1.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.shadow_mask_texture, 1U, 1U,
                "phase-i-directional-static-slice.composite"),
    1.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmProjectionPassGpuTest,
  DirectionalProjectionPassSamplesNonZeroClipLevelWhenProjectionCarriesFullLevelCount)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeSingleSliceShadowPoolConfig(
              4U, 1U, "phase-i-directional-clip1-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto virtual_frame
    = MakeDirectionalFrame(3ULL, 19U, "phase-i-directional-clip1-frame", 2U);
  auto manager = VsmCacheManager(&Backend());
  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "phase-i-directional-clip1-frame" });
  static_cast<void>(CommitFrame(manager));
  manager.PublishProjectionRecords(std::array {
    MakeDirectionalProjectionRecord(virtual_frame, 1U),
  });
  auto frame = *manager.GetCurrentFrame();

  const auto& layout = virtual_frame.directional_layouts[0];
  const auto second_level_page_table_index = layout.first_page_table_entry
    + layout.pages_per_axis * layout.pages_per_axis;
  ASSERT_LT(second_level_page_table_index, frame.snapshot.page_table.size());
  frame.snapshot.page_table[second_level_page_table_index] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 0U },
  };
  auto shader_page_table = std::vector<VsmShaderPageTableEntry>(
    frame.snapshot.page_table.size(), MakeUnmappedShaderPageTableEntry());
  shader_page_table[second_level_page_table_index]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 0U });
  UploadBufferBytes(std::const_pointer_cast<Buffer>(frame.page_table_buffer),
    shader_page_table.data(),
    shader_page_table.size() * sizeof(VsmShaderPageTableEntry),
    "phase-i-directional-clip1.page-table");

  auto depth_texture
    = CreateDepthTexture(4U, 4U, "phase-i-directional-clip1.depth");
  UploadDepth(depth_texture, std::vector<float>(16U, 0.5F),
    "phase-i-directional-clip1.depth");
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
    "phase-i-directional-clip1.shadow");

  auto pass
    = VsmProjectionPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmProjectionPassConfig>(VsmProjectionPassConfig {
        .debug_name = "phase-i-directional-clip1-pass",
      }));
  ExecutePass(pass,
    VsmProjectionPassInput {
      .frame = frame,
      .physical_pool = pool_manager.GetShadowPoolSnapshot(),
      .scene_depth_texture = depth_texture,
    },
    "phase-i-directional-clip1-pass");

  const auto output = pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);
  ASSERT_NE(output.shadow_mask_texture, nullptr);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 0U, 1U,
                "phase-i-directional-clip1-directional-left"),
    0.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 3U, 1U,
                "phase-i-directional-clip1-directional-right"),
    1.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.shadow_mask_texture, 0U, 1U,
                "phase-i-directional-clip1-left"),
    0.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.shadow_mask_texture, 3U, 1U,
                "phase-i-directional-clip1-right"),
    1.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmProjectionPassGpuTest,
  DirectionalProjectionPassPreservesOverlapAcrossPageAlignedPan)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeSingleSliceShadowPoolConfig(
              4U, 4U, "phase-i-directional-pan-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto virtual_frame
    = MakeDirectionalFrame(13ULL, 61U, "phase-i-directional-pan-frame", 1U, 2U);

  auto build_frame = [&](const VsmPageRequestProjection& projection,
                       const std::array<std::uint32_t, 4U>& physical_pages,
                       std::string_view debug_name) {
    auto manager = VsmCacheManager(&Backend());
    manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
      VsmCacheManagerFrameConfig { .debug_name = std::string(debug_name) });
    static_cast<void>(CommitFrame(manager));
    manager.PublishProjectionRecords(std::array { projection });
    auto frame = *manager.GetCurrentFrame();

    CHECK_GE_F(frame.snapshot.page_table.size(), physical_pages.size(),
      "directional pan test requires at least {} page-table entries, got {}",
      physical_pages.size(), frame.snapshot.page_table.size());
    auto shader_page_table = std::vector<VsmShaderPageTableEntry>(
      frame.snapshot.page_table.size(), MakeUnmappedShaderPageTableEntry());
    for (std::uint32_t index = 0U; index < physical_pages.size(); ++index) {
      frame.snapshot.page_table[index] = {
        .is_mapped = true,
        .physical_page
        = VsmPhysicalPageIndex { .value = physical_pages[index] },
      };
      shader_page_table[index] = MakeMappedShaderPageTableEntry(
        VsmPhysicalPageIndex { .value = physical_pages[index] });
    }
    UploadBufferBytes(std::const_pointer_cast<Buffer>(frame.page_table_buffer),
      shader_page_table.data(),
      shader_page_table.size() * sizeof(VsmShaderPageTableEntry),
      std::string(debug_name) + ".page-table");
    return frame;
  };

  auto build_shadow_values = [&]() {
    auto values = std::vector<float>(64U, 1.0F);
    constexpr std::uint32_t kTileSize = 4U;
    constexpr std::uint32_t kAtlasWidth = 8U;
    for (std::uint32_t tile_y = 0U; tile_y < 2U; ++tile_y) {
      for (std::uint32_t tile_x = 0U; tile_x < 2U; ++tile_x) {
        const auto value = tile_x == 0U ? 0.25F : 0.75F;
        for (std::uint32_t y = 0U; y < kTileSize; ++y) {
          for (std::uint32_t x = 0U; x < kTileSize; ++x) {
            const auto atlas_x = tile_x * kTileSize + x;
            const auto atlas_y = tile_y * kTileSize + y;
            values[atlas_y * kAtlasWidth + atlas_x] = value;
          }
        }
      }
    }
    return values;
  };

  auto sample_mask
    = [&](const VsmPageAllocationFrame& frame, std::string_view debug_name) {
        auto depth_texture
          = CreateDepthTexture(4U, 4U, std::string(debug_name) + ".depth");
        UploadDepth(depth_texture, std::vector<float>(16U, 0.5F),
          std::string(debug_name) + ".depth");
        UploadShadowSlice(pool_manager.GetShadowPoolSnapshot().shadow_texture,
          build_shadow_values(), std::string(debug_name) + ".shadow");

        auto pass = VsmProjectionPass(
          oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
          std::make_shared<VsmProjectionPassConfig>(VsmProjectionPassConfig {
            .debug_name = std::string(debug_name),
          }));
        ExecutePass(pass,
          VsmProjectionPassInput {
            .frame = frame,
            .physical_pool = pool_manager.GetShadowPoolSnapshot(),
            .scene_depth_texture = depth_texture,
          },
          debug_name);

        const auto output = pass.GetCurrentOutput(kTestViewId);
        EXPECT_TRUE(output.available);
        EXPECT_NE(output.directional_shadow_mask_texture, nullptr);
        return std::array<float, 4U> {
          ReadOutputTexel(output.directional_shadow_mask_texture, 0U, 0U,
            std::string(debug_name) + ".x0y0"),
          ReadOutputTexel(output.directional_shadow_mask_texture, 3U, 0U,
            std::string(debug_name) + ".x3y0"),
          ReadOutputTexel(output.directional_shadow_mask_texture, 0U, 3U,
            std::string(debug_name) + ".x0y3"),
          ReadOutputTexel(output.directional_shadow_mask_texture, 3U, 3U,
            std::string(debug_name) + ".x3y3"),
        };
      };

  const auto base_projection = MakeDirectionalProjectionRecordWithMatrices(
    virtual_frame, glm::mat4 { 1.0F }, glm::mat4 { 1.0F }, { 0, 0 });
  const auto panned_projection
    = MakeDirectionalProjectionRecordWithMatrices(virtual_frame,
      glm::translate(glm::mat4 { 1.0F }, glm::vec3 { -1.0F, 0.0F, 0.0F }),
      glm::mat4 { 1.0F }, { 1, 0 });

  const auto base_frame = build_frame(
    base_projection, { 0U, 1U, 2U, 3U }, "phase-i-directional-pan.base");
  const auto panned_frame = build_frame(
    panned_projection, { 1U, 0U, 3U, 2U }, "phase-i-directional-pan.panned");

  const auto base_samples
    = sample_mask(base_frame, "phase-i-directional-pan.base");
  const auto panned_samples
    = sample_mask(panned_frame, "phase-i-directional-pan.panned");

  EXPECT_NEAR(base_samples[0], 0.0F, 1.0e-4F);
  EXPECT_NEAR(base_samples[1], 1.0F, 1.0e-4F);
  EXPECT_NEAR(base_samples[2], 0.0F, 1.0e-4F);
  EXPECT_NEAR(base_samples[3], 1.0F, 1.0e-4F);

  EXPECT_NEAR(panned_samples[0], 1.0F, 1.0e-4F);
  EXPECT_NEAR(panned_samples[1], 1.0F, 1.0e-4F);
  EXPECT_NEAR(panned_samples[2], 1.0F, 1.0e-4F);
  EXPECT_NEAR(panned_samples[3], 1.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmProjectionPassGpuTest,
  LocalProjectionPerLightPassBuildsScreenShadowMaskFromMappedVirtualPage)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeSingleSliceShadowPoolConfig(4U, 1U, "phase-i-local-shadow")),
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
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);
  ASSERT_NE(output.shadow_mask_texture, nullptr);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 0U, 1U,
                "phase-i-local-directional-left"),
    1.0F, 1.0e-4F);
  EXPECT_NEAR(ReadOutputTexel(output.directional_shadow_mask_texture, 3U, 1U,
                "phase-i-local-directional-right"),
    1.0F, 1.0e-4F);
  EXPECT_NEAR(
    ReadOutputTexel(output.shadow_mask_texture, 0U, 1U, "phase-i-local-left"),
    0.0F, 1.0e-4F);
  EXPECT_NEAR(
    ReadOutputTexel(output.shadow_mask_texture, 3U, 1U, "phase-i-local-right"),
    1.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmProjectionPassGpuTest,
  DirectionalProjectionPassCompositesRasterizedMultiPageShadowMaskFromRealGeometry)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeStaticDynamicShadowPoolConfig(
              128U, 512U, "phase-i-rasterized-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto physical_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(physical_pool.is_available);
  ASSERT_NE(physical_pool.shadow_texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 31U };
  constexpr auto kOutputWidth = 64U;
  constexpr auto kOutputHeight = 64U;

  std::array<StageMeshVertex, 4> vertices {
    StageMeshVertex {
      .position = { -0.15F, -0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    StageMeshVertex {
      .position = { 0.15F, -0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    StageMeshVertex {
      .position = { 0.15F, 0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 0.0F, 1.0F, 1.0F },
    },
    StageMeshVertex {
      .position = { -0.15F, 0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 1.0F, 0.0F, 1.0F },
    },
  };
  constexpr std::array<std::uint32_t, 6> kIndices {
    0U,
    1U,
    2U,
    0U,
    2U,
    3U,
  };

  auto vertex_buffer = CreateStructuredSrvBuffer<StageMeshVertex>(
    vertices, "phase-i-rasterized.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-i-rasterized.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-i-rasterized.worlds");
  world_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto world_allocation = world_buffer.Allocate(2U);
  ASSERT_TRUE(world_allocation.has_value());
  ASSERT_TRUE(world_allocation->IsValid(kFrameSequence));
  const std::array<glm::mat4, 2> world_matrices {
    glm::translate(glm::mat4 { 1.0F }, glm::vec3 { -0.5F, 0.0F, 0.0F }),
    glm::translate(glm::mat4 { 1.0F }, glm::vec3 { 0.5F, 0.0F, 0.0F }),
  };
  std::memcpy(world_allocation->mapped_ptr, world_matrices.data(),
    sizeof(world_matrices));

  auto draw_metadata_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(DrawMetadata),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-i-rasterized.draws");
  draw_metadata_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto draw_allocation = draw_metadata_buffer.Allocate(2U);
  ASSERT_TRUE(draw_allocation.has_value());
  ASSERT_TRUE(draw_allocation->IsValid(kFrameSequence));

  auto shadow_caster_mask = PassMask {};
  shadow_caster_mask.Set(PassMaskBit::kOpaque);
  shadow_caster_mask.Set(PassMaskBit::kShadowCaster);

  std::array<DrawMetadata, 2> draw_records {
    DrawMetadata {
      .vertex_buffer_index = vertex_buffer.slot,
      .index_buffer_index = index_buffer.slot,
      .first_index = 0U,
      .base_vertex = 0,
      .is_indexed = 1U,
      .instance_count = 1U,
      .index_count = static_cast<std::uint32_t>(kIndices.size()),
      .vertex_count = 0U,
      .material_handle = 0U,
      .transform_index = 0U,
      .instance_metadata_buffer_index = 0U,
      .instance_metadata_offset = 0U,
      .flags = shadow_caster_mask,
      .transform_generation = 31U,
      .submesh_index = 0U,
      .primitive_flags = 0U,
    },
    DrawMetadata {
      .vertex_buffer_index = vertex_buffer.slot,
      .index_buffer_index = index_buffer.slot,
      .first_index = 0U,
      .base_vertex = 0,
      .is_indexed = 1U,
      .instance_count = 1U,
      .index_count = static_cast<std::uint32_t>(kIndices.size()),
      .vertex_count = 0U,
      .material_handle = 0U,
      .transform_index = 1U,
      .instance_metadata_buffer_index = 0U,
      .instance_metadata_offset = 0U,
      .flags = shadow_caster_mask,
      .transform_generation = 32U,
      .submesh_index = 0U,
      .primitive_flags = 0U,
    },
  };
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

  const std::array<glm::vec4, 2> draw_bounds {
    glm::vec4 { -0.5F, 0.0F, 0.25F, 0.35F },
    glm::vec4 { 0.5F, 0.0F, 0.25F, 0.35F },
  };
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-i-rasterized.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-i-rasterized.DrawFrameBindings");
  draw_frame_publisher.OnFrameStart(kFrameSequence, kFrameSlot);
  const auto draw_frame_slot = draw_frame_publisher.Publish(kTestViewId,
    DrawFrameBindings {
      .draw_metadata_slot = BindlessDrawMetadataSlot(draw_allocation->srv),
      .transforms_slot = BindlessWorldsSlot(world_allocation->srv),
    });
  ASSERT_TRUE(draw_frame_slot.IsValid());

  auto view_frame_publisher = PerViewStructuredPublisher<ViewFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-i-rasterized.ViewFrameBindings");
  view_frame_publisher.OnFrameStart(kFrameSequence, kFrameSlot);
  const auto view_frame_slot = view_frame_publisher.Publish(
    kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
  ASSERT_TRUE(view_frame_slot.IsValid());

  auto world_matrix_floats = std::array<float, 32> {};
  std::memcpy(
    world_matrix_floats.data(), world_matrices.data(), sizeof(world_matrices));

  std::array<PreparedSceneFrame::PartitionRange, 1> partitions {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = shadow_caster_mask,
      .begin = 0U,
      .end = 2U,
    },
  };

  auto prepared_frame = PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes = std::as_bytes(std::span(draw_records));
  prepared_frame.world_matrices = std::span<const float>(
    world_matrix_floats.data(), world_matrix_floats.size());
  prepared_frame.draw_bounding_spheres = std::span(draw_bounds);
  prepared_frame.partitions = std::span(partitions);
  prepared_frame.bindless_worlds_slot = world_allocation->srv;
  prepared_frame.bindless_draw_metadata_slot = draw_allocation->srv;
  prepared_frame.bindless_draw_bounds_slot = draw_bounds_buffer.slot;

  constexpr std::array<AllocatedPageSpec, 2> kPages {
    AllocatedPageSpec {
      .virtual_page = VsmVirtualPageCoord {
        .level = 0U,
        .page_x = 0U,
        .page_y = 0U,
      },
      .physical_page = 0U,
    },
    AllocatedPageSpec {
      .virtual_page = VsmVirtualPageCoord {
        .level = 0U,
        .page_x = 1U,
        .page_y = 0U,
      },
      .physical_page = 1U,
    },
  };

  auto frame = MakeAllocatedFrame(kPages);
  auto initial_meta = std::vector<VsmPhysicalPageMeta>(4U);
  initial_meta[0].dynamic_invalidated = true;
  initial_meta[0].view_uncached = true;
  initial_meta[1].dynamic_invalidated = true;
  initial_meta[1].view_uncached = true;
  AttachRasterOutputBuffers(
    frame, 700ULL, initial_meta.size(), "phase-i-rasterized", initial_meta);

  const auto projection = MakeRasterizedDirectionalProjection(2U, 1U);
  frame.snapshot.projection_records = { projection };
  frame.snapshot.page_table.resize(kRasterizedPageTableEntry + 2U);
  frame.snapshot.page_table[kRasterizedPageTableEntry] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 0U },
  };
  frame.snapshot.page_table[kRasterizedPageTableEntry + 1U] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 1U },
  };

  auto shader_page_table = std::vector<VsmShaderPageTableEntry>(
    frame.snapshot.page_table.size(), MakeUnmappedShaderPageTableEntry());
  shader_page_table[kRasterizedPageTableEntry]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 0U });
  shader_page_table[kRasterizedPageTableEntry + 1U]
    = MakeMappedShaderPageTableEntry(VsmPhysicalPageIndex { .value = 1U });
  frame.page_table_buffer = CreateStructuredStorageBuffer(
    std::span<const VsmShaderPageTableEntry>(
      shader_page_table.data(), shader_page_table.size()),
    "phase-i-rasterized.PageTable");

  auto rasterizer_pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig {
        .debug_name = "phase-i-rasterized.rasterizer",
      }));
  rasterizer_pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = frame,
    .physical_pool = physical_pool,
    .projections = { projection },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence),
  });

  ClearShadowSlice(
    physical_pool.shadow_texture, 0U, 1.0F, "phase-i-rasterized.clear");

  {
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence });
    offscreen.SetCurrentView(kTestViewId,
      MakeResolvedView(kOutputWidth, kOutputHeight), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    auto recorder = AcquireRecorder("phase-i-rasterized.rasterizer");
    ASSERT_NE(recorder, nullptr);
    RunPass(rasterizer_pass, render_context, *recorder);
    WaitForQueueIdle();
  }

  auto depth_texture = CreateDepthTexture2D(
    kOutputWidth, kOutputHeight, "phase-i-rasterized.depth");
  UploadDepthTexture(depth_texture, 0.75F, "phase-i-rasterized.depth");

  auto pass
    = VsmProjectionPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmProjectionPassConfig>(VsmProjectionPassConfig {
        .debug_name = "phase-i-rasterized.projection",
      }));
  ExecutePass(pass,
    VsmProjectionPassInput {
      .frame = frame,
      .physical_pool = physical_pool,
      .scene_depth_texture = depth_texture,
    },
    "phase-i-rasterized.projection");

  const auto output = pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);
  ASSERT_NE(output.shadow_mask_texture, nullptr);

  const auto left_center
    = ReadOutputTexel(output.directional_shadow_mask_texture, 16U, 32U,
      "phase-i-rasterized.mask.left-center");
  const auto right_center
    = ReadOutputTexel(output.directional_shadow_mask_texture, 48U, 32U,
      "phase-i-rasterized.mask.right-center");
  const auto left_edge = ReadOutputTexel(output.directional_shadow_mask_texture,
    4U, 32U, "phase-i-rasterized.mask.left-edge");
  const auto right_edge
    = ReadOutputTexel(output.directional_shadow_mask_texture, 60U, 32U,
      "phase-i-rasterized.mask.right-edge");
  const auto top_left = ReadOutputTexel(output.directional_shadow_mask_texture,
    4U, 4U, "phase-i-rasterized.mask.top-left");
  const auto bottom_right
    = ReadOutputTexel(output.directional_shadow_mask_texture, 60U, 60U,
      "phase-i-rasterized.mask.bottom-right");

  EXPECT_LT(left_center, 0.1F);
  EXPECT_LT(right_center, 0.1F);
  EXPECT_NEAR(left_edge, 1.0F, 1.0e-4F);
  EXPECT_NEAR(right_edge, 1.0F, 1.0e-4F);
  EXPECT_NEAR(top_left, 1.0F, 1.0e-4F);
  EXPECT_NEAR(bottom_right, 1.0F, 1.0e-4F);
}

} // namespace
