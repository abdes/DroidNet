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

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmHzbUpdaterPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawFrameBindings.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowStageGpuHarness.h"

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
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
using oxygen::engine::VsmHzbUpdaterPass;
using oxygen::engine::VsmHzbUpdaterPassConfig;
using oxygen::engine::VsmHzbUpdaterPassInput;
using oxygen::engine::VsmShadowRasterizerPass;
using oxygen::engine::VsmShadowRasterizerPassConfig;
using oxygen::engine::VsmShadowRasterizerPassInput;
using oxygen::engine::internal::PerViewStructuredPublisher;
using oxygen::engine::upload::TransientStructuredBuffer;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::Texture;
using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageAllocationDecision;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageAllocationSnapshot;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmRenderedPageDirtyFlagBits;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::StageMeshVertex;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

constexpr ViewId kTestViewId { 17U };
class VsmHzbUpdaterPassGpuTest : public VsmStageGpuHarness {
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

  [[nodiscard]] static auto MakeSmallHzbPoolConfig(const char* debug_name)
    -> oxygen::renderer::vsm::VsmHzbPoolConfig
  {
    return oxygen::renderer::vsm::VsmHzbPoolConfig {
      .mip_count = 4U,
      .format = Format::kR32Float,
      .debug_name = debug_name,
    };
  }

  [[nodiscard]] static auto MakeAllocatedFrame(
    std::span<const AllocatedPageSpec> pages) -> VsmPageAllocationFrame
  {
    constexpr auto kMapId = 17U;
    auto frame = VsmPageAllocationFrame {};
    frame.snapshot = VsmPageAllocationSnapshot {};
    frame.plan.decisions.reserve(pages.size());
    for (const auto& page : pages) {
      frame.plan.decisions.push_back(VsmPageAllocationDecision {
        .request = VsmPageRequest {
          .map_id = kMapId,
          .page = page.virtual_page,
          .flags = page.flags,
        },
        .action = VsmAllocationAction::kAllocateNew,
        .current_physical_page
        = oxygen::renderer::vsm::VsmPhysicalPageIndex {
          .value = page.physical_page,
        },
      });
    }
    frame.plan.allocated_page_count
      = static_cast<std::uint32_t>(frame.plan.decisions.size());
    frame.is_ready = true;
    return frame;
  }

  [[nodiscard]] static auto MakeRasterizedDirectionalProjection()
    -> oxygen::renderer::vsm::VsmPageRequestProjection
  {
    return oxygen::renderer::vsm::VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 1.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(
          VsmProjectionLightType::kDirectional),
      },
      .map_id = 17U,
      .first_page_table_entry = 8U,
      .map_pages_x = 2U,
      .map_pages_y = 1U,
      .pages_x = 2U,
      .pages_y = 1U,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = 1U,
      .coarse_level = 0U,
    };
  }

  auto ExecutePass(
    const VsmHzbUpdaterPassInput& input, std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass
      = VsmHzbUpdaterPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
        std::make_shared<VsmHzbUpdaterPassConfig>(VsmHzbUpdaterPassConfig {
          .debug_name = std::string(debug_name),
        }));
    pass.SetInput(input);

    auto prepared_frame = PreparedSceneFrame {};
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
    offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
  }

  auto UploadDirtyFlags(const std::shared_ptr<const Buffer>& buffer,
    const std::vector<std::uint32_t>& dirty_flags, std::string_view debug_name)
    -> void
  {
    UploadBufferBytes(std::const_pointer_cast<Buffer>(buffer),
      dirty_flags.data(), dirty_flags.size() * sizeof(std::uint32_t),
      debug_name);
  }

  auto UploadPhysicalMeta(const std::shared_ptr<const Buffer>& buffer,
    const std::vector<VsmPhysicalPageMeta>& metadata,
    std::string_view debug_name) -> void
  {
    UploadBufferBytes(std::const_pointer_cast<Buffer>(buffer), metadata.data(),
      metadata.size() * sizeof(VsmPhysicalPageMeta), debug_name);
  }

  auto UploadMipData(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t mip_level, const std::uint32_t width,
    const std::uint32_t height, const std::vector<float>& values,
    std::string_view debug_name) -> void
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot upload into a null texture");
    CHECK_EQ_F(values.size(), static_cast<std::size_t>(width) * height,
      "texture upload value count must match width*height");

    const auto row_pitch
      = AlignUp(width * sizeof(float), kTextureUploadRowAlignment);
    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(row_pitch) * height,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    CHECK_NOTNULL_F(upload.get(), "Failed to create HZB upload buffer");

    auto upload_bytes
      = std::vector<std::byte>(static_cast<std::size_t>(row_pitch) * height);
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
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Copy");
      CHECK_NOTNULL_F(recorder.get());
      auto writable_texture = std::const_pointer_cast<Texture>(texture);
      EnsureTracked(
        *recorder, upload, oxygen::graphics::ResourceStates::kGenericRead);
      EnsureTracked(
        *recorder, writable_texture, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *upload, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *writable_texture, oxygen::graphics::ResourceStates::kCopyDest);
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
            .mip_level = mip_level,
            .array_slice = 0U,
          },
        },
        *writable_texture);
      recorder->RequireResourceStateFinal(
        *writable_texture, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();
  }

  auto SeedShadowPageQuadrants(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t page_size, const std::uint32_t tile_x,
    const std::uint32_t tile_y, const float top_left, const float top_right,
    const float bottom_left, const float bottom_right,
    std::string_view debug_name) -> void
  {
    auto staging_page_desc = oxygen::graphics::TextureDesc {};
    staging_page_desc.width = page_size;
    staging_page_desc.height = page_size;
    staging_page_desc.format = Format::kR32Float;
    staging_page_desc.texture_type = oxygen::TextureType::kTexture2D;
    staging_page_desc.is_shader_resource = true;
    staging_page_desc.debug_name = std::string(debug_name) + ".StagingPage";
    auto staging_page_texture = CreateRegisteredTexture(staging_page_desc);
    CHECK_NOTNULL_F(
      staging_page_texture.get(), "Failed to create shadow staging texture");

    const auto row_pitch
      = AlignUp(page_size * sizeof(float), kTextureUploadRowAlignment);
    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(row_pitch) * page_size,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    CHECK_NOTNULL_F(upload.get(), "Failed to create shadow upload buffer");

    auto upload_bytes
      = std::vector<std::byte>(static_cast<std::size_t>(row_pitch) * page_size);
    const auto half_extent = page_size / 2U;
    for (std::uint32_t y = 0U; y < page_size; ++y) {
      for (std::uint32_t x = 0U; x < page_size; ++x) {
        const auto value = (y < half_extent)
          ? (x < half_extent ? top_left : top_right)
          : (x < half_extent ? bottom_left : bottom_right);
        std::memcpy(upload_bytes.data()
            + static_cast<std::size_t>(y) * row_pitch
            + static_cast<std::size_t>(x) * sizeof(float),
          &value, sizeof(value));
      }
    }
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto upload_recorder
        = AcquireRecorder(std::string(debug_name) + ".UploadCopy");
      CHECK_NOTNULL_F(upload_recorder.get());
      EnsureTracked(*upload_recorder, upload,
        oxygen::graphics::ResourceStates::kGenericRead);
      EnsureTracked(*upload_recorder, staging_page_texture,
        oxygen::graphics::ResourceStates::kCommon);
      upload_recorder->RequireResourceState(
        *upload, oxygen::graphics::ResourceStates::kCopySource);
      upload_recorder->RequireResourceState(
        *staging_page_texture, oxygen::graphics::ResourceStates::kCopyDest);
      upload_recorder->FlushBarriers();
      upload_recorder->CopyBufferToTexture(*upload,
        oxygen::graphics::TextureUploadRegion {
          .buffer_offset = 0U,
          .buffer_row_pitch = row_pitch,
          .buffer_slice_pitch = row_pitch * page_size,
          .dst_slice = {
            .x = tile_x * page_size,
            .y = tile_y * page_size,
            .z = 0U,
            .width = page_size,
            .height = page_size,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = 0U,
          },
        },
        *staging_page_texture);
      upload_recorder->RequireResourceStateFinal(
        *staging_page_texture, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();

    {
      auto copy_recorder = AcquireRecorder(std::string(debug_name) + ".Copy");
      CHECK_NOTNULL_F(copy_recorder.get());
      auto writable_texture = std::const_pointer_cast<Texture>(texture);
      EnsureTracked(*copy_recorder, staging_page_texture,
        oxygen::graphics::ResourceStates::kCommon);
      EnsureTracked(*copy_recorder, writable_texture,
        oxygen::graphics::ResourceStates::kCommon);
      copy_recorder->RequireResourceState(
        *staging_page_texture, oxygen::graphics::ResourceStates::kCopySource);
      copy_recorder->RequireResourceState(
        *writable_texture, oxygen::graphics::ResourceStates::kCopyDest);
      copy_recorder->FlushBarriers();
      copy_recorder->CopyTexture(*staging_page_texture,
        oxygen::graphics::TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = page_size,
          .height = page_size,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = 0U,
        },
        oxygen::graphics::TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = 0U,
          .num_array_slices = 1U,
        },
        *writable_texture,
        oxygen::graphics::TextureSlice {
          .x = tile_x * page_size,
          .y = tile_y * page_size,
          .z = 0U,
          .width = page_size,
          .height = page_size,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = 0U,
        },
        oxygen::graphics::TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = 0U,
          .num_array_slices = 1U,
        });
    }
    WaitForQueueIdle();
  }

  auto ReadMipTexel(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t mip_level, const std::uint32_t x, const std::uint32_t y,
    std::string_view debug_name) -> float
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot read from a null texture");
    constexpr std::uint32_t kRowPitch = 256U;
    auto readback = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = kRowPitch,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".ProbeCopy");
      CHECK_NOTNULL_F(recorder.get());
      RegisterResource(std::const_pointer_cast<Texture>(texture));
      recorder->BeginTrackingResourceState(
        *texture, oxygen::graphics::ResourceStates::kShaderResource, true);
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
    CHECK_NOTNULL_F(mapped, "Failed to map readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }
};

NOLINT_TEST_F(VsmHzbUpdaterPassGpuTest,
  RebuildsDirtyPageMipsSelectivelyAndClearsConsumedDirtyState)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeSingleSliceShadowPoolConfig(8U, 4U, "phase-h-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeSmallHzbPoolConfig("phase-h-hzb")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame = MakeSinglePageLocalFrame(1ULL, 10U, "phase-h", 2U);
  const auto requests = std::array {
    VsmPageRequest {
      .map_id = virtual_frame.local_light_layouts[0].id, .page = {} },
    VsmPageRequest {
      .map_id = virtual_frame.local_light_layouts[1].id, .page = {} },
  };

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-h" });
  manager.SetPageRequests(requests);
  const auto& frame = CommitFrame(manager);
  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  const auto hzb_pool = pool_manager.GetHzbPoolSnapshot();

  ASSERT_TRUE(shadow_pool.is_available);
  ASSERT_TRUE(hzb_pool.is_available);
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);
  ASSERT_NE(hzb_pool.texture, nullptr);
  ASSERT_EQ(shadow_pool.page_size_texels, 8U);
  ASSERT_EQ(hzb_pool.width, 8U);
  ASSERT_EQ(hzb_pool.mip_count, 4U);

  auto metadata = std::vector<VsmPhysicalPageMeta>(
    shadow_pool.tile_capacity, VsmPhysicalPageMeta {});
  metadata[0].is_allocated = true;
  metadata[0].is_dirty = true;
  metadata[1].is_allocated = true;
  UploadPhysicalMeta(frame.physical_page_meta_buffer, metadata, "phase-h.meta");

  auto dirty_flags = std::vector<std::uint32_t>(shadow_pool.tile_capacity, 0U);
  dirty_flags[0] = static_cast<std::uint32_t>(
    VsmRenderedPageDirtyFlagBits::kDynamicRasterized);
  UploadDirtyFlags(frame.dirty_flags_buffer, dirty_flags, "phase-h.dirty");

  const auto coord0
    = TryConvertToCoord({ .value = 0U }, shadow_pool.tile_capacity,
      shadow_pool.tiles_per_axis, shadow_pool.slice_count);
  ASSERT_TRUE(coord0.has_value());
  SeedShadowPageQuadrants(shadow_pool.shadow_texture,
    shadow_pool.page_size_texels, coord0->tile_x, coord0->tile_y, 0.90F, 0.70F,
    0.50F, 0.30F, "phase-h.shadow-page0");

  for (std::uint32_t mip_level = 0U; mip_level < hzb_pool.mip_count;
    ++mip_level) {
    const auto mip_extent = std::max(1U, hzb_pool.width >> mip_level);
    auto mip_values = std::vector<float>(
      static_cast<std::size_t>(mip_extent) * mip_extent, 0.77F);
    UploadMipData(hzb_pool.texture, mip_level, mip_extent, mip_extent,
      mip_values,
      std::string("phase-h.seed-hzb-mip") + std::to_string(mip_level));
  }

  const auto dirty_before
    = ReadBufferAs<std::uint32_t>(frame.dirty_flags_buffer,
      shadow_pool.tile_capacity, "phase-h.dirty-before");
  ASSERT_EQ(dirty_before[0],
    static_cast<std::uint32_t>(
      VsmRenderedPageDirtyFlagBits::kDynamicRasterized));
  ASSERT_EQ(dirty_before[1], 0U);

  const auto meta_before
    = ReadBufferAs<VsmPhysicalPageMeta>(frame.physical_page_meta_buffer,
      shadow_pool.tile_capacity, "phase-h.meta-before");
  ASSERT_TRUE(static_cast<bool>(meta_before[0].is_allocated));
  ASSERT_TRUE(static_cast<bool>(meta_before[0].is_dirty));
  ASSERT_TRUE(static_cast<bool>(meta_before[1].is_allocated));
  ASSERT_FALSE(static_cast<bool>(meta_before[1].is_dirty));

  ASSERT_NEAR(ReadMipTexel(shadow_pool.shadow_texture, 0U, 0U, 0U,
                "phase-h.shadow-before.tl"),
    0.90F, 1.0e-5F);
  ASSERT_NEAR(ReadMipTexel(shadow_pool.shadow_texture, 0U, 5U, 1U,
                "phase-h.shadow-before.tr"),
    0.70F, 1.0e-5F);
  ASSERT_NEAR(ReadMipTexel(shadow_pool.shadow_texture, 0U, 1U, 5U,
                "phase-h.shadow-before.bl"),
    0.50F, 1.0e-5F);
  ASSERT_NEAR(ReadMipTexel(shadow_pool.shadow_texture, 0U, 6U, 6U,
                "phase-h.shadow-before.br"),
    0.30F, 1.0e-5F);

  ASSERT_NEAR(
    ReadMipTexel(hzb_pool.texture, 0U, 0U, 0U, "phase-h.hzb-before.mip0"),
    0.77F, 1.0e-5F);
  ASSERT_NEAR(
    ReadMipTexel(hzb_pool.texture, 2U, 0U, 0U, "phase-h.hzb-before.mip2"),
    0.77F, 1.0e-5F);
  ASSERT_NEAR(
    ReadMipTexel(hzb_pool.texture, 3U, 0U, 0U, "phase-h.hzb-before.mip3"),
    0.77F, 1.0e-5F);

  ExecutePass({ .frame = frame,
                .physical_pool = shadow_pool,
                .hzb_pool = hzb_pool,
                .can_preserve_existing_hzb_contents = true,
                .force_rebuild_all_allocated_pages = false },
    "phase-h-hzb-update");

  EXPECT_NEAR(ReadMipTexel(hzb_pool.texture, 0U, 0U, 0U, "phase-h.mip0.tl"),
    0.90F, 1.0e-5F);
  EXPECT_NEAR(ReadMipTexel(hzb_pool.texture, 0U, 2U, 0U, "phase-h.mip0.tr"),
    0.70F, 1.0e-5F);
  EXPECT_NEAR(ReadMipTexel(hzb_pool.texture, 0U, 0U, 2U, "phase-h.mip0.bl"),
    0.50F, 1.0e-5F);
  EXPECT_NEAR(ReadMipTexel(hzb_pool.texture, 0U, 3U, 3U, "phase-h.mip0.br"),
    0.30F, 1.0e-5F);

  EXPECT_NEAR(ReadMipTexel(hzb_pool.texture, 2U, 0U, 0U, "phase-h.mip2.page0"),
    0.30F, 1.0e-5F);
  EXPECT_NEAR(ReadMipTexel(hzb_pool.texture, 2U, 1U, 0U, "phase-h.mip2.page1"),
    0.77F, 1.0e-5F);
  EXPECT_NEAR(ReadMipTexel(hzb_pool.texture, 3U, 0U, 0U, "phase-h.mip3.global"),
    0.30F, 1.0e-5F);

  const auto dirty_after = ReadBufferAs<std::uint32_t>(
    frame.dirty_flags_buffer, shadow_pool.tile_capacity, "phase-h.dirty-after");
  EXPECT_EQ(dirty_after[0], 0U);
  EXPECT_EQ(dirty_after[1], 0U);

  const auto meta_after
    = ReadBufferAs<VsmPhysicalPageMeta>(frame.physical_page_meta_buffer,
      shadow_pool.tile_capacity, "phase-h.meta-after");
  EXPECT_FALSE(static_cast<bool>(meta_after[0].is_dirty));
  EXPECT_TRUE(static_cast<bool>(meta_after[0].is_allocated));
  EXPECT_TRUE(static_cast<bool>(meta_after[1].is_allocated));
}

NOLINT_TEST_F(VsmHzbUpdaterPassGpuTest,
  RebuildsDirtyPageMipsFromRasterizedMultiPageDirectionalScene)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeSingleSliceShadowPoolConfig(
              8U, 4U, "phase-h-rasterized-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(
              MakeSmallHzbPoolConfig("phase-h-rasterized-hzb")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  const auto hzb_pool = pool_manager.GetHzbPoolSnapshot();
  ASSERT_TRUE(shadow_pool.is_available);
  ASSERT_TRUE(hzb_pool.is_available);
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);
  ASSERT_NE(hzb_pool.texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 41U };
  constexpr auto kOutputWidth = 64U;
  constexpr auto kOutputHeight = 64U;
  constexpr auto kRasterizedPageTableEntry = 8U;

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
    vertices, "phase-h-rasterized.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-h-rasterized.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-h-rasterized.worlds");
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
    "phase-h-rasterized.draws");
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
      .transform_generation = 41U,
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
      .transform_generation = 42U,
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
    draw_bounds, "phase-h-rasterized.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-h-rasterized.DrawFrameBindings");
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
    "phase-h-rasterized.ViewFrameBindings");
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
  initial_meta[0].is_allocated = true;
  initial_meta[0].dynamic_invalidated = true;
  initial_meta[0].view_uncached = true;
  initial_meta[1].is_allocated = true;
  initial_meta[1].dynamic_invalidated = true;
  initial_meta[1].view_uncached = true;
  AttachRasterOutputBuffers(
    frame, 800ULL, initial_meta.size(), "phase-h-rasterized", initial_meta);

  const auto projection = MakeRasterizedDirectionalProjection();
  frame.snapshot.projection_records = { projection };
  frame.snapshot.page_table.resize(kRasterizedPageTableEntry + 2U);
  frame.snapshot.page_table[kRasterizedPageTableEntry] = {
    .is_mapped = true,
    .physical_page
    = oxygen::renderer::vsm::VsmPhysicalPageIndex { .value = 0U },
  };
  frame.snapshot.page_table[kRasterizedPageTableEntry + 1U] = {
    .is_mapped = true,
    .physical_page
    = oxygen::renderer::vsm::VsmPhysicalPageIndex { .value = 1U },
  };

  auto shader_page_table
    = std::vector<oxygen::renderer::vsm::VsmShaderPageTableEntry>(
      frame.snapshot.page_table.size(),
      oxygen::renderer::vsm::MakeUnmappedShaderPageTableEntry());
  shader_page_table[kRasterizedPageTableEntry]
    = oxygen::renderer::vsm::MakeMappedShaderPageTableEntry(
      oxygen::renderer::vsm::VsmPhysicalPageIndex { .value = 0U });
  shader_page_table[kRasterizedPageTableEntry + 1U]
    = oxygen::renderer::vsm::MakeMappedShaderPageTableEntry(
      oxygen::renderer::vsm::VsmPhysicalPageIndex { .value = 1U });
  frame.page_table_buffer = CreateStructuredStorageBuffer(
    std::span<const oxygen::renderer::vsm::VsmShaderPageTableEntry>(
      shader_page_table.data(), shader_page_table.size()),
    "phase-h-rasterized.PageTable");

  auto rasterizer_pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig {
        .debug_name = "phase-h-rasterized.rasterizer",
      }));
  rasterizer_pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = frame,
    .physical_pool = shadow_pool,
    .projections = { projection },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence),
  });

  ClearShadowSlice(
    shadow_pool.shadow_texture, 0U, 1.0F, "phase-h-rasterized.clear");

  {
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence });
    offscreen.SetCurrentView(kTestViewId,
      MakeResolvedView(kOutputWidth, kOutputHeight), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    auto recorder = AcquireRecorder("phase-h-rasterized.rasterizer");
    ASSERT_NE(recorder, nullptr);
    RunPass(rasterizer_pass, render_context, *recorder);
    WaitForQueueIdle();
  }

  for (std::uint32_t mip_level = 0U; mip_level < hzb_pool.mip_count;
    ++mip_level) {
    const auto mip_extent = std::max(1U, hzb_pool.width >> mip_level);
    auto mip_values = std::vector<float>(
      static_cast<std::size_t>(mip_extent) * mip_extent, 0.77F);
    UploadMipData(hzb_pool.texture, mip_level, mip_extent, mip_extent,
      mip_values,
      std::string("phase-h-rasterized.seed-hzb-mip")
        + std::to_string(mip_level));
  }

  const auto dirty_before
    = ReadBufferAs<std::uint32_t>(frame.dirty_flags_buffer,
      shadow_pool.tile_capacity, "phase-h-rasterized.dirty-before");
  EXPECT_EQ(dirty_before[0],
    static_cast<std::uint32_t>(
      VsmRenderedPageDirtyFlagBits::kDynamicRasterized));
  EXPECT_EQ(dirty_before[1],
    static_cast<std::uint32_t>(
      VsmRenderedPageDirtyFlagBits::kDynamicRasterized));
  EXPECT_EQ(dirty_before[2], 0U);

  const auto meta_before
    = ReadBufferAs<VsmPhysicalPageMeta>(frame.physical_page_meta_buffer,
      shadow_pool.tile_capacity, "phase-h-rasterized.meta-before");
  EXPECT_TRUE(static_cast<bool>(meta_before[0].is_dirty));
  EXPECT_TRUE(static_cast<bool>(meta_before[1].is_dirty));
  EXPECT_FALSE(static_cast<bool>(meta_before[2].is_dirty));

  EXPECT_NEAR(ReadDepthTexel(shadow_pool.shadow_texture, 0U, 4U, 4U,
                "phase-h-rasterized.shadow-left"),
    0.25F, 1.0e-3F);
  EXPECT_NEAR(ReadDepthTexel(shadow_pool.shadow_texture, 0U, 12U, 4U,
                "phase-h-rasterized.shadow-right"),
    0.25F, 1.0e-3F);

  ExecutePass({ .frame = frame,
                .physical_pool = shadow_pool,
                .hzb_pool = hzb_pool,
                .can_preserve_existing_hzb_contents = true,
                .force_rebuild_all_allocated_pages = false },
    "phase-h-rasterized.hzb-update");

  EXPECT_NEAR(
    ReadMipTexel(hzb_pool.texture, 2U, 0U, 0U, "phase-h-rasterized.mip2.page0"),
    0.25F, 1.0e-3F);
  EXPECT_NEAR(
    ReadMipTexel(hzb_pool.texture, 2U, 1U, 0U, "phase-h-rasterized.mip2.page1"),
    0.25F, 1.0e-3F);
  EXPECT_NEAR(
    ReadMipTexel(hzb_pool.texture, 2U, 0U, 1U, "phase-h-rasterized.mip2.page2"),
    0.77F, 1.0e-5F);
  EXPECT_NEAR(ReadMipTexel(
                hzb_pool.texture, 3U, 0U, 0U, "phase-h-rasterized.mip3.global"),
    0.25F, 1.0e-3F);

  const auto dirty_after = ReadBufferAs<std::uint32_t>(frame.dirty_flags_buffer,
    shadow_pool.tile_capacity, "phase-h-rasterized.dirty-after");
  EXPECT_EQ(dirty_after[0], 0U);
  EXPECT_EQ(dirty_after[1], 0U);
  EXPECT_EQ(dirty_after[2], 0U);

  const auto meta_after
    = ReadBufferAs<VsmPhysicalPageMeta>(frame.physical_page_meta_buffer,
      shadow_pool.tile_capacity, "phase-h-rasterized.meta-after");
  EXPECT_FALSE(static_cast<bool>(meta_after[0].is_dirty));
  EXPECT_FALSE(static_cast<bool>(meta_after[1].is_dirty));
}

} // namespace
