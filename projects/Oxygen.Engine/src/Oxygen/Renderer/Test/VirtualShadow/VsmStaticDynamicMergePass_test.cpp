//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmStaticDynamicMergePass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::VsmStaticDynamicMergePass;
using oxygen::engine::VsmStaticDynamicMergePassConfig;
using oxygen::engine::VsmStaticDynamicMergePassInput;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::TextureSubResourceSet;
using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPoolConfig;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::VsmRenderedPageDirtyFlagBits;
using oxygen::renderer::vsm::testing::VsmCacheManagerGpuTestBase;

constexpr ViewId kTestViewId { 13U };
constexpr std::uint32_t kTextureUploadRowAlignment = 256U;

class VsmStaticDynamicMergePassGpuTest : public VsmCacheManagerGpuTestBase {
protected:
  [[nodiscard]] static auto AlignUp(
    const std::uint32_t value, const std::uint32_t alignment) -> std::uint32_t
  {
    CHECK_NE_F(alignment, 0U, "alignment must be non-zero");
    return (value + alignment - 1U) / alignment * alignment;
  }

  [[nodiscard]] static auto MakeResolvedView() -> ResolvedView
  {
    auto view_config = View {};
    view_config.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 1.0F,
      .height = 1.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = 1,
      .bottom = 1,
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

  static auto CommitFrame(VsmCacheManager& manager)
    -> const VsmPageAllocationFrame&
  {
    static_cast<void>(manager.BuildPageAllocationPlan());
    return manager.CommitPageAllocationFrame();
  }

  auto ExecutePass(const VsmStaticDynamicMergePassInput& input,
    std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = VsmStaticDynamicMergePass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmStaticDynamicMergePassConfig>(
        VsmStaticDynamicMergePassConfig {
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

  auto CreateFilledSingleChannelTexture(const std::uint32_t width,
    const std::uint32_t height, const float value, std::string_view debug_name)
    -> std::shared_ptr<Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.format = Format::kR32Float;
    texture_desc.texture_type = oxygen::TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.debug_name = std::string(debug_name);

    auto texture = CreateRegisteredTexture(texture_desc);
    CHECK_NOTNULL_F(texture.get(), "Failed to create single-channel texture");

    const auto row_pitch
      = AlignUp(width * sizeof(float), kTextureUploadRowAlignment);
    auto upload = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(row_pitch) * height,
      .usage = oxygen::graphics::BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + "_Upload",
    });
    CHECK_NOTNULL_F(
      upload.get(), "Failed to create single-channel upload buffer");

    auto upload_bytes
      = std::vector<std::byte>(row_pitch * height, std::byte { 0 });
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        std::memcpy(upload_bytes.data() + y * row_pitch + x * sizeof(float),
          &value, sizeof(float));
      }
    }
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + "_Upload");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(
        *recorder, upload, oxygen::graphics::ResourceStates::kGenericRead);
      EnsureTracked(
        *recorder, texture, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *upload, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopyDest);
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
      recorder->RequireResourceStateFinal(
        *texture, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();
    return texture;
  }

  auto SeedShadowPageValue(const std::shared_ptr<const Texture>& texture,
    const std::uint32_t page_size, const std::uint32_t tile_x,
    const std::uint32_t tile_y, const std::uint32_t slice, const float value,
    std::string_view debug_name) -> void
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot seed a null shadow texture");
    auto seed_texture = CreateFilledSingleChannelTexture(
      page_size, page_size, value, std::string(debug_name) + ".Seed");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Copy");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(
        *recorder, seed_texture, oxygen::graphics::ResourceStates::kCommon);
      EnsureTracked(*recorder, std::const_pointer_cast<Texture>(texture),
        oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *seed_texture, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTexture(*seed_texture,
        TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = page_size,
          .height = page_size,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = 0U,
        },
        TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = 0U,
          .num_array_slices = 1U,
        },
        *std::const_pointer_cast<Texture>(texture),
        TextureSlice {
          .x = tile_x * page_size,
          .y = tile_y * page_size,
          .z = 0U,
          .width = page_size,
          .height = page_size,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = slice,
        },
        TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = slice,
          .num_array_slices = 1U,
        });
    }
    WaitForQueueIdle();
  }

  [[nodiscard]] auto ReadShadowDepthTexel(
    const std::shared_ptr<const Texture>& texture, const std::uint32_t x,
    const std::uint32_t y, const std::uint32_t slice,
    std::string_view debug_name) -> float
  {
    auto probe_desc = oxygen::graphics::TextureDesc {};
    probe_desc.width = 1U;
    probe_desc.height = 1U;
    probe_desc.format = Format::kR32Float;
    probe_desc.texture_type = oxygen::TextureType::kTexture2D;
    probe_desc.is_shader_resource = true;
    probe_desc.debug_name = std::string(debug_name) + ".Probe";

    auto probe = CreateRegisteredTexture(probe_desc);
    CHECK_NOTNULL_F(probe.get(), "Failed to create probe texture");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".ProbeCopy");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(*recorder, std::const_pointer_cast<Texture>(texture),
        oxygen::graphics::ResourceStates::kCommon);
      EnsureTracked(
        *recorder, probe, oxygen::graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *texture, oxygen::graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *probe, oxygen::graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTexture(*texture,
        TextureSlice {
          .x = x,
          .y = y,
          .z = 0U,
          .width = 1U,
          .height = 1U,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = slice,
        },
        TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = slice,
          .num_array_slices = 1U,
        },
        *probe,
        TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = 1U,
          .height = 1U,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = 0U,
        },
        TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = 0U,
          .num_array_slices = 1U,
        });
      recorder->RequireResourceStateFinal(
        *probe, oxygen::graphics::ResourceStates::kCommon);
    }
    WaitForQueueIdle();

    const auto readback
      = GetReadbackManager()->ReadTextureNow(*probe, {}, true);
    CHECK_F(readback.has_value(), "Texture readback failed");
    float value = 0.0F;
    std::memcpy(&value, readback->bytes.data(), sizeof(float));
    return value;
  }
};

NOLINT_TEST_F(VsmStaticDynamicMergePassGpuTest,
  MergePassCompositesStaticSliceIntoDirtyDynamicPagesOnlyWhenStaticCacheIsValid)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-g-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame = MakeSinglePageLocalFrame(1ULL, 10U, "phase-g");
  const auto request = VsmPageRequest {
    .map_id = virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-g" });
  manager.SetPageRequests({ &request, 1U });
  const auto& frame = CommitFrame(manager);
  const auto pool = pool_manager.GetShadowPoolSnapshot();

  ASSERT_TRUE(pool.is_available);
  ASSERT_NE(pool.shadow_texture, nullptr);
  ASSERT_EQ(pool.slice_count, 2U);

  auto metadata = std::vector<VsmPhysicalPageMeta>(
    pool.tile_capacity, VsmPhysicalPageMeta {});
  metadata[0].is_allocated = true;
  metadata[1].is_allocated = true;
  metadata[2].is_allocated = true;
  metadata[2].static_invalidated = true;
  UploadPhysicalMeta(frame.physical_page_meta_buffer, metadata, "phase-g-meta");

  const auto dynamic_dirty = static_cast<std::uint32_t>(
    VsmRenderedPageDirtyFlagBits::kDynamicRasterized);
  auto dirty_flags = std::vector<std::uint32_t>(pool.tile_capacity, 0U);
  dirty_flags[0] = dynamic_dirty;
  dirty_flags[2] = dynamic_dirty;
  UploadDirtyFlags(frame.dirty_flags_buffer, dirty_flags, "phase-g-dirty");

  const auto coord0 = TryConvertToCoord(
    { .value = 0U }, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
  const auto coord1 = TryConvertToCoord(
    { .value = 1U }, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
  const auto coord2 = TryConvertToCoord(
    { .value = 2U }, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
  ASSERT_TRUE(coord0.has_value());
  ASSERT_TRUE(coord1.has_value());
  ASSERT_TRUE(coord2.has_value());

  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels,
    coord0->tile_x, coord0->tile_y, 0U, 0.80F, "phase-g-dyn-page0");
  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels,
    coord0->tile_x, coord0->tile_y, 1U, 0.25F, "phase-g-static-page0");
  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels,
    coord1->tile_x, coord1->tile_y, 0U, 0.70F, "phase-g-dyn-page1");
  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels,
    coord1->tile_x, coord1->tile_y, 1U, 0.20F, "phase-g-static-page1");
  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels,
    coord2->tile_x, coord2->tile_y, 0U, 0.90F, "phase-g-dyn-page2");
  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels,
    coord2->tile_x, coord2->tile_y, 1U, 0.10F, "phase-g-static-page2");

  ExecutePass(
    { .frame = frame, .physical_pool = pool }, "vsm-phase-g-merge-selection");

  EXPECT_NEAR(
    ReadShadowDepthTexel(pool.shadow_texture,
      coord0->tile_x * pool.page_size_texels,
      coord0->tile_y * pool.page_size_texels, 0U, "phase-g-read-page0"),
    0.25F, 1.0e-5F);
  EXPECT_NEAR(
    ReadShadowDepthTexel(pool.shadow_texture,
      coord1->tile_x * pool.page_size_texels,
      coord1->tile_y * pool.page_size_texels, 0U, "phase-g-read-page1"),
    0.70F, 1.0e-5F);
  EXPECT_NEAR(
    ReadShadowDepthTexel(pool.shadow_texture,
      coord2->tile_x * pool.page_size_texels,
      coord2->tile_y * pool.page_size_texels, 0U, "phase-g-read-page2"),
    0.90F, 1.0e-5F);
}

NOLINT_TEST_F(VsmStaticDynamicMergePassGpuTest,
  MergePassHonorsStaticRasterizedDirtyBitAndKeepsFixedSliceDirection)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase-g-static-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase-g-static");
  const auto request = VsmPageRequest {
    .map_id = virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-g-static" });
  manager.SetPageRequests({ &request, 1U });
  const auto& frame = CommitFrame(manager);
  const auto pool = pool_manager.GetShadowPoolSnapshot();

  auto metadata = std::vector<VsmPhysicalPageMeta>(
    pool.tile_capacity, VsmPhysicalPageMeta {});
  metadata[0].is_allocated = true;
  UploadPhysicalMeta(
    frame.physical_page_meta_buffer, metadata, "phase-g-static-meta");

  auto dirty_flags = std::vector<std::uint32_t>(pool.tile_capacity, 0U);
  dirty_flags[0] = static_cast<std::uint32_t>(
    VsmRenderedPageDirtyFlagBits::kStaticRasterized);
  UploadDirtyFlags(
    frame.dirty_flags_buffer, dirty_flags, "phase-g-static-dirty");

  const auto coord = TryConvertToCoord(
    { .value = 0U }, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
  ASSERT_TRUE(coord.has_value());

  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels, coord->tile_x,
    coord->tile_y, 0U, 0.60F, "phase-g-static-dyn");
  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels, coord->tile_x,
    coord->tile_y, 1U, 0.15F, "phase-g-static-cache");

  ExecutePass(
    { .frame = frame, .physical_pool = pool }, "vsm-phase-g-merge-static");

  EXPECT_NEAR(
    ReadShadowDepthTexel(pool.shadow_texture,
      coord->tile_x * pool.page_size_texels,
      coord->tile_y * pool.page_size_texels, 0U, "phase-g-static-read-dyn"),
    0.15F, 1.0e-5F);
  EXPECT_NEAR(
    ReadShadowDepthTexel(pool.shadow_texture,
      coord->tile_x * pool.page_size_texels,
      coord->tile_y * pool.page_size_texels, 1U, "phase-g-static-read-cache"),
    0.15F, 1.0e-5F);
}

NOLINT_TEST_F(VsmStaticDynamicMergePassGpuTest,
  MergePassBypassesSingleSlicePoolsWithoutMutatingDynamicDepth)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(VsmPhysicalPoolConfig {
              .page_size_texels = 128U,
              .physical_tile_capacity = 256U,
              .array_slice_count = 1U,
              .depth_format = Format::kDepth32,
              .slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth },
              .debug_name = "phase-g-single-slice",
            }),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase-g-single-slice");
  const auto request = VsmPageRequest {
    .map_id = virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-g-single-slice" });
  manager.SetPageRequests({ &request, 1U });
  const auto& frame = CommitFrame(manager);
  const auto pool = pool_manager.GetShadowPoolSnapshot();

  auto metadata = std::vector<VsmPhysicalPageMeta>(
    pool.tile_capacity, VsmPhysicalPageMeta {});
  metadata[0].is_allocated = true;
  UploadPhysicalMeta(
    frame.physical_page_meta_buffer, metadata, "phase-g-single-meta");

  auto dirty_flags = std::vector<std::uint32_t>(pool.tile_capacity, 0U);
  dirty_flags[0] = static_cast<std::uint32_t>(
    VsmRenderedPageDirtyFlagBits::kDynamicRasterized);
  UploadDirtyFlags(
    frame.dirty_flags_buffer, dirty_flags, "phase-g-single-dirty");

  const auto coord = TryConvertToCoord(
    { .value = 0U }, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
  ASSERT_TRUE(coord.has_value());

  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels, coord->tile_x,
    coord->tile_y, 0U, 0.55F, "phase-g-single-dyn");

  ExecutePass(
    { .frame = frame, .physical_pool = pool }, "vsm-phase-g-merge-single");

  EXPECT_NEAR(
    ReadShadowDepthTexel(pool.shadow_texture,
      coord->tile_x * pool.page_size_texels,
      coord->tile_y * pool.page_size_texels, 0U, "phase-g-single-read"),
    0.55F, 1.0e-5F);
}

} // namespace
