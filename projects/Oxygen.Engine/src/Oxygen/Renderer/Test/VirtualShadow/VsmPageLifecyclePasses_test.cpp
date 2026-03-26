//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageFlagPropagationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageInitializationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageManagementPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace {

using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::VsmPageFlagPropagationPass;
using oxygen::engine::VsmPageFlagPropagationPassConfig;
using oxygen::engine::VsmPageInitializationPass;
using oxygen::engine::VsmPageInitializationPassConfig;
using oxygen::engine::VsmPageInitializationPassInput;
using oxygen::engine::VsmPageManagementFinalStage;
using oxygen::engine::VsmPageManagementPass;
using oxygen::engine::VsmPageManagementPassConfig;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::TextureSubResourceSet;
using oxygen::renderer::vsm::BuildVirtualRemapTable;
using oxygen::renderer::vsm::TryConvertToCoord;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmLocalLightDesc;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPoolConfig;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::VsmCacheManagerGpuTestBase;

constexpr oxygen::ViewId kTestViewId { 11U };

class VsmPageLifecycleGpuTestBase : public VsmCacheManagerGpuTestBase {
protected:
  static constexpr std::uint32_t kTextureUploadRowAlignment = 256U;

  [[nodiscard]] static auto AlignUp(
    const std::uint32_t value, const std::uint32_t alignment) -> std::uint32_t
  {
    CHECK_NE_F(alignment, 0U, "alignment must be non-zero");
    return (value + alignment - 1U) / alignment * alignment;
  }

  [[nodiscard]] static auto MakeMultiLevelLocalFrame(
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const std::string_view remap_key, const std::uint32_t level_count,
    const std::uint32_t pages_x, const std::uint32_t pages_y,
    const char* frame_name = "vsm-multi-level-frame")
    -> VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = frame_name,
      },
      frame_generation);
    address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
      .remap_key = std::string(remap_key),
      .level_count = level_count,
      .pages_per_level_x = pages_x,
      .pages_per_level_y = pages_y,
      .debug_name = std::string(remap_key),
    });
    return address_space.DescribeFrame();
  }

  [[nodiscard]] static auto MakeResolvedView() -> oxygen::ResolvedView
  {
    auto view_config = oxygen::View {};
    view_config.viewport = oxygen::ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 1.0F,
      .height = 1.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_config.scissor = oxygen::Scissors {
      .left = 0,
      .top = 0,
      .right = 1,
      .bottom = 1,
    };

    return oxygen::ResolvedView(oxygen::ResolvedView::Params {
      .view_config = view_config,
      .view_matrix = glm::mat4 { 1.0F },
      .proj_matrix = glm::mat4 { 1.0F },
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = oxygen::NdcDepthRange::ZeroToOne,
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

  auto ExecutePropagationPass(
    const VsmPageAllocationFrame& frame, std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = VsmPageFlagPropagationPass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmPageFlagPropagationPassConfig>(
        VsmPageFlagPropagationPassConfig {
          .debug_name = std::string(debug_name),
        }));
    pass.SetFrameInput(frame);

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

  auto ExecuteInitializationPass(const VsmPageInitializationPassInput& input,
    std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = VsmPageInitializationPass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmPageInitializationPassConfig>(
        VsmPageInitializationPassConfig {
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

  auto ExecutePageManagementPass(
    const VsmPageAllocationFrame& frame, std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = VsmPageManagementPass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmPageManagementPassConfig>(
        VsmPageManagementPassConfig {
          .final_stage = VsmPageManagementFinalStage::kAllocateNewPages,
          .debug_name = std::string(debug_name),
        }));
    pass.SetFrameInput(frame);

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

  template <typename T>
  auto ReadBufferAs(const std::shared_ptr<const Buffer>& buffer,
    const std::size_t element_count, std::string_view debug_name)
    -> std::vector<T>
  {
    const auto bytes = ReadBufferBytes(std::const_pointer_cast<Buffer>(buffer),
      element_count * sizeof(T), debug_name);
    auto result = std::vector<T>(element_count);
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
  }

  auto CreateFilledSingleChannelTexture(const std::uint32_t width,
    const std::uint32_t height, const float value, std::string_view debug_name)
    -> std::shared_ptr<Texture>
  {
    auto texture_desc = oxygen::graphics::TextureDesc {};
    texture_desc.width = width;
    texture_desc.height = height;
    texture_desc.format = oxygen::Format::kR32Float;
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
    CHECK_NOTNULL_F(texture.get(), "Cannot read from a null shadow texture");

    auto probe_desc = oxygen::graphics::TextureDesc {};
    probe_desc.width = 1U;
    probe_desc.height = 1U;
    probe_desc.format = oxygen::Format::kR32Float;
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
    CHECK_F(readback->bytes.size() >= sizeof(float),
      "Texture readback returned too few bytes");
    float value = 0.0F;
    std::memcpy(&value, readback->bytes.data(), sizeof(float));
    return value;
  }
};

class VsmPageFlagPropagationGpuTest : public VsmPageLifecycleGpuTestBase { };
class VsmPageInitializationGpuTest : public VsmPageLifecycleGpuTestBase { };

NOLINT_TEST_F(VsmPageFlagPropagationGpuTest,
  PropagationPassBuildsHierarchicalFlagsAndMappedDescendants)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-e-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame = MakeMultiLevelLocalFrame(
    1ULL, 10U, "local-propagate", 3U, 2U, 1U, "phase-e-propagate");
  const auto request = VsmPageRequest {
    .map_id = virtual_frame.local_light_layouts[0].id,
    .page = VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 0U },
  };

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-e-propagate" });
  manager.SetPageRequests({ &request, 1U });
  const auto& frame = CommitFrame(manager);

  ExecutePageManagementPass(frame, "vsm-phase-e-management");
  ExecutePropagationPass(frame, "vsm-phase-e-propagation");

  const auto page_flags
    = ReadBufferAs<VsmShaderPageFlags>(frame.page_flags_buffer,
      frame.snapshot.page_table.size(), "vsm-phase-e-page-flags");
  ASSERT_EQ(page_flags.size(), 6U);

  const auto allocated_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated);
  const auto dynamic_uncached_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDynamicUncached);
  const auto static_uncached_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached);
  const auto mapped_descendant_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kMappedDescendant);

  EXPECT_EQ(page_flags[0].bits, 0U);
  EXPECT_EQ(page_flags[2].bits, 0U);
  EXPECT_EQ(page_flags[4].bits, 0U);

  EXPECT_NE(page_flags[1].bits & allocated_bit, 0U);
  EXPECT_NE(page_flags[1].bits & dynamic_uncached_bit, 0U);
  EXPECT_NE(page_flags[1].bits & static_uncached_bit, 0U);

  EXPECT_NE(page_flags[3].bits & allocated_bit, 0U);
  EXPECT_NE(page_flags[3].bits & dynamic_uncached_bit, 0U);
  EXPECT_NE(page_flags[3].bits & static_uncached_bit, 0U);
  EXPECT_NE(page_flags[3].bits & mapped_descendant_bit, 0U);

  EXPECT_NE(page_flags[5].bits & allocated_bit, 0U);
  EXPECT_NE(page_flags[5].bits & dynamic_uncached_bit, 0U);
  EXPECT_NE(page_flags[5].bits & static_uncached_bit, 0U);
  EXPECT_NE(page_flags[5].bits & mapped_descendant_bit, 0U);
}

NOLINT_TEST_F(VsmPageInitializationGpuTest,
  InitializationPassClearsOnlySelectedDynamicPages)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(VsmPhysicalPoolConfig {
      .page_size_texels = 128U,
      .physical_tile_capacity = 256U,
      .array_slice_count = 1U,
      .depth_format = oxygen::Format::kDepth32,
      .slice_roles
      = { oxygen::renderer::vsm::VsmPhysicalPoolSliceRole::kDynamicDepth },
      .debug_name = "phase-e-clear-shadow",
    }),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase-e-clear");
  const auto request = VsmPageRequest {
    .map_id = virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-e-clear" });
  manager.SetPageRequests({ &request, 1U });
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.initialization_work.size(), 1U);
  EXPECT_EQ(frame.plan.initialization_work[0].action,
    oxygen::renderer::vsm::VsmPageInitializationAction::kClearDepth);

  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);

  const auto coord = TryConvertToCoord(
    frame.plan.initialization_work[0].physical_page, shadow_pool.tile_capacity,
    shadow_pool.tiles_per_axis, shadow_pool.slice_count);
  ASSERT_TRUE(coord.has_value());
  const auto neighbor_tile_x
    = (coord->tile_x + 1U) % shadow_pool.tiles_per_axis;
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    coord->tile_x, coord->tile_y, 0U, 0.25F, "phase-e-clear-seed-target");
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    neighbor_tile_x, coord->tile_y, 0U, 0.25F, "phase-e-clear-seed-neighbor");

  ExecuteInitializationPass(VsmPageInitializationPassInput { .frame = frame,
                              .physical_pool = shadow_pool },
    "vsm-phase-e-clear");

  const auto target_x = coord->tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto target_y = coord->tile_y * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto neighbor_x = neighbor_tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto neighbor_y = target_y;

  EXPECT_FLOAT_EQ(ReadShadowDepthTexel(shadow_pool.shadow_texture, target_x,
                    target_y, 0U, "phase-e-clear-target"),
    1.0F);
  EXPECT_FLOAT_EQ(ReadShadowDepthTexel(shadow_pool.shadow_texture, neighbor_x,
                    neighbor_y, 0U, "phase-e-clear-neighbor"),
    0.25F);
}

NOLINT_TEST_F(VsmPageInitializationGpuTest,
  InitializationPassCopiesStaticSliceIntoDynamicSliceForCopyStaticWork)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-e-copy-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase-e-copy-prev");
  const auto previous_request = VsmPageRequest {
    .map_id = previous_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-e-copy-prev" });
  manager.SetPageRequests({ &previous_request, 1U });
  static_cast<void>(CommitFrame(manager));
  manager.ExtractFrameData();

  manager.InvalidateLocalLights({ "local-0" },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  const auto current_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase-e-copy-curr");
  const auto current_request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-e-copy-curr" });
  manager.SetPageRequests({ &current_request, 1U });
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.initialization_work.size(), 1U);
  EXPECT_EQ(frame.plan.initialization_work[0].action,
    oxygen::renderer::vsm::VsmPageInitializationAction::kCopyStaticSlice);

  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);

  const auto coord = TryConvertToCoord(
    frame.plan.initialization_work[0].physical_page, shadow_pool.tile_capacity,
    shadow_pool.tiles_per_axis, shadow_pool.slice_count);
  ASSERT_TRUE(coord.has_value());
  const auto neighbor_tile_x
    = (coord->tile_x + 1U) % shadow_pool.tiles_per_axis;
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    coord->tile_x, coord->tile_y, 0U, 0.20F,
    "phase-e-copy-seed-dynamic-target");
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    neighbor_tile_x, coord->tile_y, 0U, 0.20F,
    "phase-e-copy-seed-dynamic-neighbor");
  SeedShadowPageValue(shadow_pool.shadow_texture, shadow_pool.page_size_texels,
    coord->tile_x, coord->tile_y, 1U, 0.75F, "phase-e-copy-seed-static-target");

  ExecuteInitializationPass(VsmPageInitializationPassInput { .frame = frame,
                              .physical_pool = shadow_pool },
    "vsm-phase-e-copy");

  const auto target_x = coord->tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto target_y = coord->tile_y * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto neighbor_x = neighbor_tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto neighbor_y = target_y;

  EXPECT_FLOAT_EQ(ReadShadowDepthTexel(shadow_pool.shadow_texture, target_x,
                    target_y, 0U, "phase-e-copy-target"),
    0.75F);
  EXPECT_FLOAT_EQ(ReadShadowDepthTexel(shadow_pool.shadow_texture, neighbor_x,
                    neighbor_y, 0U, "phase-e-copy-neighbor"),
    0.20F);
}

NOLINT_TEST_F(VsmPageInitializationGpuTest,
  InitializationPassClearsLargeRectBatchesWithoutTouchingUntargetedPages)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(VsmPhysicalPoolConfig {
      .page_size_texels = 128U,
      .physical_tile_capacity = 576U,
      .array_slice_count = 1U,
      .depth_format = oxygen::Format::kDepth32,
      .slice_roles
      = { oxygen::renderer::vsm::VsmPhysicalPoolSliceRole::kDynamicDepth },
      .debug_name = "phase-e-large-clear-shadow",
    }),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto shadow_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_NE(shadow_pool.shadow_texture, nullptr);

  auto frame = VsmPageAllocationFrame {};
  frame.snapshot.frame_generation = 1ULL;
  frame.is_ready = true;
  frame.plan.initialization_work.reserve(300U);
  for (std::uint32_t physical_page = 0U; physical_page < 300U;
    ++physical_page) {
    frame.plan.initialization_work.push_back(
      oxygen::renderer::vsm::VsmPageInitializationWorkItem {
        .physical_page = { physical_page },
        .action
        = oxygen::renderer::vsm::VsmPageInitializationAction::kClearDepth,
      });
  }

  constexpr auto kSeedValue = 0.25F;
  const auto seed_pages = std::array { 0U, 150U, 299U, 300U };
  for (const auto physical_page : seed_pages) {
    const auto coord = TryConvertToCoord(
      oxygen::renderer::vsm::VsmPhysicalPageIndex {
        .value = physical_page,
      },
      shadow_pool.tile_capacity, shadow_pool.tiles_per_axis,
      shadow_pool.slice_count);
    ASSERT_TRUE(coord.has_value());
    SeedShadowPageValue(shadow_pool.shadow_texture,
      shadow_pool.page_size_texels, coord->tile_x, coord->tile_y, coord->slice,
      kSeedValue,
      std::string("phase-e-large-clear-seed-")
        + std::string(nostd::to_string(physical_page)));
  }

  ExecuteInitializationPass(VsmPageInitializationPassInput { .frame = frame,
                              .physical_pool = shadow_pool },
    "vsm-phase-e-large-clear");

  for (const auto physical_page : std::array { 0U, 150U, 299U }) {
    const auto coord = TryConvertToCoord(
      oxygen::renderer::vsm::VsmPhysicalPageIndex {
        .value = physical_page,
      },
      shadow_pool.tile_capacity, shadow_pool.tiles_per_axis,
      shadow_pool.slice_count);
    ASSERT_TRUE(coord.has_value());
    const auto texel_x = coord->tile_x * shadow_pool.page_size_texels
      + shadow_pool.page_size_texels / 2U;
    const auto texel_y = coord->tile_y * shadow_pool.page_size_texels
      + shadow_pool.page_size_texels / 2U;
    EXPECT_FLOAT_EQ(ReadShadowDepthTexel(shadow_pool.shadow_texture, texel_x,
                      texel_y, coord->slice,
                      std::string("phase-e-large-clear-target-")
                        + std::string(nostd::to_string(physical_page))),
      1.0F);
  }

  const auto untouched_coord = TryConvertToCoord(
    oxygen::renderer::vsm::VsmPhysicalPageIndex {
      .value = 300U,
    },
    shadow_pool.tile_capacity, shadow_pool.tiles_per_axis,
    shadow_pool.slice_count);
  ASSERT_TRUE(untouched_coord.has_value());
  const auto untouched_x
    = untouched_coord->tile_x * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  const auto untouched_y
    = untouched_coord->tile_y * shadow_pool.page_size_texels
    + shadow_pool.page_size_texels / 2U;
  EXPECT_FLOAT_EQ(
    ReadShadowDepthTexel(shadow_pool.shadow_texture, untouched_x, untouched_y,
      untouched_coord->slice, "phase-e-large-clear-untouched"),
    kSeedValue);
}

} // namespace
