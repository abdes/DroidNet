//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmInvalidationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmPageManagementPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace {

using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::VsmInvalidationPass;
using oxygen::engine::VsmInvalidationPassConfig;
using oxygen::engine::VsmInvalidationPassInput;
using oxygen::engine::VsmPageManagementFinalStage;
using oxygen::engine::VsmPageManagementPass;
using oxygen::engine::VsmPageManagementPassConfig;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmInvalidationWorkItem;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::testing::VsmCacheManagerGpuTestBase;

constexpr ViewId kTestViewId { 7U };

[[nodiscard]] auto IsReusePathAction(const VsmAllocationAction action) noexcept
  -> bool
{
  return action == VsmAllocationAction::kReuseExisting
    || action == VsmAllocationAction::kInitializeOnly;
}

class VsmPageManagementPassGpuTestBase : public VsmCacheManagerGpuTestBase {
protected:
  [[nodiscard]] static auto MakeResolvedView() -> ResolvedView
  {
    const auto view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
      glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
    const auto projection_matrix
      = glm::perspectiveRH_ZO(glm::radians(90.0F), 1.0F, 0.1F, 100.0F);

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
      .view_matrix = view_matrix,
      .proj_matrix = projection_matrix,
      .camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F },
      .depth_range = NdcDepthRange::ZeroToOne,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
    });
  }

  [[nodiscard]] static auto MakeCustomLocalFrame(
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    const std::initializer_list<std::string_view> remap_keys,
    const char* frame_name = "vsm-frame")
    -> oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = frame_name,
      },
      frame_generation);

    std::uint32_t light_index = 0U;
    for (const auto remap_key : remap_keys) {
      const auto suffix = std::to_string(light_index++);
      address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {
        .remap_key = std::string(remap_key),
        .debug_name = "local-" + suffix,
      });
    }

    return address_space.DescribeFrame();
  }

  static auto CommitFrame(VsmCacheManager& manager)
    -> const VsmPageAllocationFrame&
  {
    static_cast<void>(manager.BuildPageAllocationPlan());
    return manager.CommitPageAllocationFrame();
  }

  template <typename T>
  auto ReadBufferAs(
    const std::shared_ptr<const oxygen::graphics::Buffer>& buffer,
    const std::size_t element_count, std::string_view debug_name)
    -> std::vector<T>
  {
    const auto bytes = ReadBufferBytes(
      std::const_pointer_cast<oxygen::graphics::Buffer>(buffer),
      element_count * sizeof(T), debug_name);
    EXPECT_EQ(bytes.size(), element_count * sizeof(T));
    auto result = std::vector<T>(element_count);
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
  }

  auto ExecutePass(const VsmPageAllocationFrame& frame,
    const VsmPageManagementFinalStage final_stage, std::string_view debug_name)
    -> std::shared_ptr<const oxygen::graphics::Buffer>
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = VsmPageManagementPass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmPageManagementPassConfig>(
        VsmPageManagementPassConfig {
          .final_stage = final_stage,
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
    return pass.GetAvailablePageCountBuffer();
  }

  auto CreateMetadataSeedBuffer(
    const std::span<const VsmPhysicalPageMeta> metadata,
    std::string_view debug_name)
    -> std::shared_ptr<const oxygen::graphics::Buffer>
  {
    auto buffer = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(metadata.size())
        * sizeof(VsmPhysicalPageMeta),
      .usage = oxygen::graphics::BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create metadata seed buffer");
    UploadBufferBytes(buffer, metadata.data(),
      metadata.size() * sizeof(VsmPhysicalPageMeta), debug_name);
    return buffer;
  }
};

class VsmPackAvailablePagesGpuTest : public VsmPageManagementPassGpuTestBase {
};
class VsmAllocateNewPagesGpuTest : public VsmPageManagementPassGpuTestBase { };

NOLINT_TEST_F(VsmPackAvailablePagesGpuTest,
  PackStageCompactsUnallocatedPagesIntoAscendingStack)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-d-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_virtual_frame = MakeSinglePageLocalFrame(1ULL, 10U);
  const auto previous_request = VsmPageRequest {
    .map_id = previous_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, previous_virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-d-prev" });
  manager.SetPageRequests({ &previous_request, 1U });
  static_cast<void>(CommitFrame(manager));
  manager.ExtractFrameData();

  const auto current_virtual_frame = MakeSinglePageLocalFrame(2ULL, 20U);
  const auto current_request = VsmPageRequest {
    .map_id = current_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(
    MakeSeam(pool_manager, current_virtual_frame, &previous_virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-d-current" });
  manager.SetPageRequests({ &current_request, 1U });
  const auto& frame = CommitFrame(manager);

  const auto available_count_buffer = ExecutePass(
    frame, VsmPageManagementFinalStage::kPackAvailablePages, "vsm-pack-stage");
  ASSERT_NE(available_count_buffer, nullptr);

  const auto count = ReadBufferAs<std::uint32_t>(
    available_count_buffer, 1U, "vsm-available-count");
  ASSERT_EQ(count.size(), 1U);
  EXPECT_EQ(count[0], frame.snapshot.physical_pages.size() - 1U);

  const auto available_pages = ReadBufferAs<std::uint32_t>(
    frame.physical_page_list_buffer, 8U, "vsm-available-pages");
  const auto reused_page = frame.plan.decisions[0].current_physical_page.value;
  auto expected = std::array<std::uint32_t, 8> {};
  std::uint32_t cursor = 0U;
  for (std::uint32_t page = 0U; cursor < expected.size(); ++page) {
    if (page == reused_page) {
      continue;
    }
    expected[cursor++] = page;
  }
  EXPECT_EQ(available_pages,
    std::vector<std::uint32_t>(expected.begin(), expected.end()));
}

NOLINT_TEST_F(
  VsmAllocateNewPagesGpuTest, AllocateStagePublishesMixedReuseAndFreshMappings)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-d-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_virtual_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "prev", 1U);
  const auto previous_request = VsmPageRequest {
    .map_id = previous_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, previous_virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-d-prev" });
  manager.SetPageRequests({ &previous_request, 1U });
  static_cast<void>(CommitFrame(manager));
  manager.ExtractFrameData();

  const auto current_virtual_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "curr", 2U);
  const auto current_requests = std::array {
    VsmPageRequest {
      .map_id = current_virtual_frame.local_light_layouts[0].id, .page = {} },
    VsmPageRequest {
      .map_id = current_virtual_frame.local_light_layouts[1].id, .page = {} },
  };

  manager.BeginFrame(
    MakeSeam(pool_manager, current_virtual_frame, &previous_virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-d-current" });
  manager.SetPageRequests(current_requests);
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.decisions.size(), 2U);
  EXPECT_TRUE(IsReusePathAction(frame.plan.decisions[0].action));
  EXPECT_EQ(frame.plan.decisions[1].action, VsmAllocationAction::kAllocateNew);

  static_cast<void>(ExecutePass(frame,
    VsmPageManagementFinalStage::kAllocateNewPages, "vsm-allocate-stage"));

  const auto page_table
    = ReadBufferAs<VsmShaderPageTableEntry>(frame.page_table_buffer,
      frame.snapshot.page_table.size(), "vsm-page-management-table");
  ASSERT_EQ(page_table.size(), 2U);
  EXPECT_TRUE(oxygen::renderer::vsm::IsMapped(page_table[0]));
  EXPECT_TRUE(oxygen::renderer::vsm::IsMapped(page_table[1]));
  EXPECT_EQ(oxygen::renderer::vsm::DecodePhysicalPageIndex(page_table[0]).value,
    frame.plan.decisions[0].current_physical_page.value);
  EXPECT_EQ(oxygen::renderer::vsm::DecodePhysicalPageIndex(page_table[1]).value,
    frame.plan.decisions[1].current_physical_page.value);

  const auto page_flags
    = ReadBufferAs<VsmShaderPageFlags>(frame.page_flags_buffer,
      frame.snapshot.page_table.size(), "vsm-page-management-flags");
  EXPECT_NE(page_flags[0].bits
      & static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated),
    0U);
  EXPECT_NE(page_flags[1].bits
      & static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated),
    0U);
  EXPECT_NE(page_flags[1].bits
      & static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDynamicUncached),
    0U);
  EXPECT_NE(page_flags[1].bits
      & static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached),
    0U);

  const auto metadata
    = ReadBufferAs<VsmPhysicalPageMeta>(frame.physical_page_meta_buffer,
      frame.snapshot.physical_pages.size(), "vsm-page-management-meta");
  const auto& allocated
    = metadata[frame.plan.decisions[1].current_physical_page.value];
  EXPECT_TRUE(static_cast<bool>(allocated.is_allocated));
  EXPECT_EQ(allocated.owner_id, current_requests[1].map_id);
  EXPECT_EQ(allocated.owner_page, current_requests[1].page);
  EXPECT_TRUE(static_cast<bool>(allocated.view_uncached));
}

NOLINT_TEST_F(VsmAllocateNewPagesGpuTest,
  AllocateStagePreservesInvalidationBitsFromMetadataSeedBuffer)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-d-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_virtual_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "prev", 1U);
  const auto previous_request = VsmPageRequest {
    .map_id = previous_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, previous_virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-d-prev" });
  manager.SetPageRequests({ &previous_request, 1U });
  static_cast<void>(CommitFrame(manager));
  manager.ExtractFrameData();

  const auto current_virtual_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "curr", 2U);
  const auto current_requests = std::array {
    VsmPageRequest {
      .map_id = current_virtual_frame.local_light_layouts[0].id, .page = {} },
    VsmPageRequest {
      .map_id = current_virtual_frame.local_light_layouts[1].id, .page = {} },
  };

  manager.BeginFrame(
    MakeSeam(pool_manager, current_virtual_frame, &previous_virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-d-current" });
  manager.SetPageRequests(current_requests);
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.decisions.size(), 2U);
  const auto allocated_page
    = frame.plan.decisions[1].current_physical_page.value;
  auto seeded_metadata = frame.snapshot.physical_pages;
  seeded_metadata[allocated_page].static_invalidated = true;

  auto frame_with_seed = frame;
  frame_with_seed.physical_page_meta_seed_buffer = CreateMetadataSeedBuffer(
    seeded_metadata, "vsm-page-allocation-meta-seed");

  static_cast<void>(ExecutePass(frame_with_seed,
    VsmPageManagementFinalStage::kAllocateNewPages, "vsm-allocate-seeded"));

  const auto metadata = ReadBufferAs<VsmPhysicalPageMeta>(
    frame_with_seed.physical_page_meta_buffer,
    frame_with_seed.snapshot.physical_pages.size(), "vsm-allocate-seeded-meta");
  ASSERT_EQ(metadata.size(), frame_with_seed.snapshot.physical_pages.size());
  EXPECT_TRUE(static_cast<bool>(metadata[allocated_page].static_invalidated));
  EXPECT_TRUE(static_cast<bool>(metadata[allocated_page].is_allocated));
  EXPECT_EQ(metadata[allocated_page].owner_id, current_requests[1].map_id);
}

NOLINT_TEST_F(VsmAllocateNewPagesGpuTest,
  AllocateStageAcceptsInvalidationSeedBufferFromTheSameRecorder)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-d-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto current_virtual_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "curr", 1U);
  const auto current_request = VsmPageRequest {
    .map_id = current_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, current_virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase-d-current" });
  manager.SetPageRequests({ &current_request, 1U });
  const auto& frame = CommitFrame(manager);

  auto invalidation_pass
    = VsmInvalidationPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmInvalidationPassConfig>(VsmInvalidationPassConfig {
        .debug_name = "phase-d-invalidation-seed",
      }));

  auto previous_metadata
    = std::vector<VsmPhysicalPageMeta>(frame.snapshot.physical_pages.size());
  previous_metadata[0] = VsmPhysicalPageMeta {
    .is_allocated = true,
    .owner_id = 11U,
    .owner_mip_level = 0U,
    .owner_page = {},
  };
  invalidation_pass.SetInput(VsmInvalidationPassInput {
    .previous_projection_records = { VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type
        = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
      },
      .map_id = 11U,
      .first_page_table_entry = 0U,
      .map_pages_x = 1U,
      .map_pages_y = 1U,
      .pages_x = 1U,
      .pages_y = 1U,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = 1U,
      .coarse_level = 0U,
      .light_index = 0U,
    } },
    .previous_page_table_entries
    = { oxygen::renderer::vsm::MakeMappedShaderPageTableEntry({ 0U }) },
    .previous_physical_page_metadata = previous_metadata,
    .invalidation_work_items = {
      VsmInvalidationWorkItem {
        .world_bounding_sphere = glm::vec4 { -0.75F, 0.0F, 0.0F, 0.10F },
        .projection_index = 0U,
        .scope = VsmCacheInvalidationScope::kDynamicOnly,
      },
    },
  });

  auto page_management_pass = VsmPageManagementPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmPageManagementPassConfig>(VsmPageManagementPassConfig {
      .final_stage = VsmPageManagementFinalStage::kAllocateNewPages,
      .debug_name = "phase-d-allocate-seeded-from-invalidation",
    }));

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);
  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
  offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  auto frame_with_seed = frame;
  {
    auto recorder
      = AcquireRecorder("vsm-allocate-stage-shared-invalidation-seed");
    CHECK_NOTNULL_F(recorder.get());
    RunPass(invalidation_pass, render_context, *recorder);
    frame_with_seed.physical_page_meta_seed_buffer
      = invalidation_pass.GetCurrentOutputPhysicalMetadataBuffer();
    page_management_pass.SetFrameInput(frame_with_seed);
    RunPass(page_management_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto metadata = ReadBufferAs<VsmPhysicalPageMeta>(
    frame_with_seed.physical_page_meta_buffer,
    frame_with_seed.snapshot.physical_pages.size(),
    "vsm-allocate-seeded-from-invalidation.readback");
  ASSERT_FALSE(metadata.empty());
  EXPECT_TRUE(static_cast<bool>(metadata[0].dynamic_invalidated));
}

} // namespace
