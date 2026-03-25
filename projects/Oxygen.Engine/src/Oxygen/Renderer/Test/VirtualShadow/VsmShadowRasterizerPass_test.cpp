//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowGpuTestFixtures.h"

#include <memory>
#include <string_view>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>

namespace {

using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::VsmShadowRasterizerPass;
using oxygen::engine::VsmShadowRasterizerPassConfig;
using oxygen::engine::VsmShadowRasterizerPassInput;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmPageAllocationDecision;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageAllocationPlan;
using oxygen::renderer::vsm::VsmPageAllocationSnapshot;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageCoord;
using oxygen::renderer::vsm::VsmPhysicalPageIndex;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::VsmCacheManagerGpuTestBase;

constexpr ViewId kTestViewId { 41U };
constexpr std::uint32_t kTestMapId = 17U;
constexpr std::uint32_t kTestPageTableEntry = 8U;
constexpr std::uint32_t kTestPhysicalPage = 3U;

class VsmShadowRasterizerPassGpuTest : public VsmCacheManagerGpuTestBase {
protected:
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

  [[nodiscard]] static auto MakeFrame() -> VsmPageAllocationFrame
  {
    auto frame = VsmPageAllocationFrame {};
    frame.snapshot = VsmPageAllocationSnapshot {};
    frame.plan = VsmPageAllocationPlan {
      .decisions = {
        VsmPageAllocationDecision {
          .request = VsmPageRequest {
            .map_id = kTestMapId,
            .page = VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
            .flags = VsmPageRequestFlags::kRequired,
          },
          .action = VsmAllocationAction::kAllocateNew,
          .current_physical_page = VsmPhysicalPageIndex { .value = kTestPhysicalPage },
        },
      },
      .allocated_page_count = 1U,
    };
    frame.is_ready = true;
    return frame;
  }

  [[nodiscard]] static auto MakeProjection() -> VsmPageRequestProjection
  {
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 1.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
      },
      .map_id = kTestMapId,
      .first_page_table_entry = kTestPageTableEntry,
      .pages_x = 1U,
      .pages_y = 1U,
      .level_count = 1U,
      .coarse_level = 0U,
      .light_index = 0U,
    };
  }
};

NOLINT_TEST_F(VsmShadowRasterizerPassGpuTest,
  PrepareResourcesBuildsPreparedPagesAndRegistersPass)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-f-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto physical_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(physical_pool.is_available);
  ASSERT_NE(physical_pool.shadow_texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig { .debug_name = "phase-f-rasterizer" }));
  pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = MakeFrame(),
    .physical_pool = physical_pool,
    .projections = { MakeProjection() },
  });

  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
  offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  {
    auto recorder = AcquireRecorder("phase-f-rasterizer");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto prepared_pages = pass.GetPreparedPages();
  ASSERT_EQ(pass.GetPreparedPageCount(), 1U);
  ASSERT_EQ(prepared_pages.size(), 1U);

  EXPECT_EQ(prepared_pages[0].page_table_index, kTestPageTableEntry);
  EXPECT_EQ(prepared_pages[0].map_id, kTestMapId);
  EXPECT_EQ(prepared_pages[0].physical_page.value, kTestPhysicalPage);
  EXPECT_EQ(prepared_pages[0].physical_coord,
    (VsmPhysicalPageCoord { .tile_x = 3U, .tile_y = 0U, .slice = 0U }));
  EXPECT_EQ(prepared_pages[0].scissors.left, 384);
  EXPECT_EQ(prepared_pages[0].scissors.top, 0);
  EXPECT_EQ(prepared_pages[0].scissors.right, 512);
  EXPECT_EQ(prepared_pages[0].scissors.bottom, 128);
  EXPECT_EQ(prepared_pages[0].viewport.top_left_x, 384.0F);
  EXPECT_EQ(prepared_pages[0].viewport.top_left_y, 0.0F);
  EXPECT_EQ(prepared_pages[0].viewport.width, 128.0F);
  EXPECT_EQ(prepared_pages[0].viewport.height, 128.0F);
  EXPECT_FALSE(prepared_pages[0].static_only);

  EXPECT_EQ(render_context.GetPass<VsmShadowRasterizerPass>(), &pass);
}

} // namespace
