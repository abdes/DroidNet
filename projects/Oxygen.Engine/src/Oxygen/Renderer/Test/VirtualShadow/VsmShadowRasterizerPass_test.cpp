//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawFrameBindings.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

#include "VirtualShadowStageGpuHarness.h"

namespace {

using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::ShaderVisibleIndex;
using oxygen::ViewId;
using oxygen::engine::BindlessDrawMetadataSlot;
using oxygen::engine::BindlessWorldsSlot;
using oxygen::engine::DepthPrePass;
using oxygen::engine::DrawFrameBindings;
using oxygen::engine::DrawMetadata;
using oxygen::engine::DrawPrimitiveFlagBits;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::ScreenHzbBuildPass;
using oxygen::engine::ScreenHzbBuildPassConfig;
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
using oxygen::graphics::Texture;
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
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPrimitiveIdentity;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmRenderedPageDirtyFlagBits;
using oxygen::renderer::vsm::VsmShaderIndirectDrawCommand;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::StageMeshVertex;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

constexpr ViewId kTestViewId { 41U };
constexpr std::uint32_t kTestMapId = 17U;
constexpr std::uint32_t kTestPageTableEntry = 8U;
constexpr std::uint32_t kTestPhysicalPage = 3U;
using TestVertex = StageMeshVertex;

class VsmShadowRasterizerPassGpuTest : public VsmStageGpuHarness {
protected:
  struct AllocatedPageSpec {
    VsmVirtualPageCoord virtual_page {
      .level = 0U,
      .page_x = 0U,
      .page_y = 0U,
    };
    std::uint32_t physical_page { kTestPhysicalPage };
    VsmPageRequestFlags flags { VsmPageRequestFlags::kRequired };
  };

  [[nodiscard]] static auto MakeFrame(std::span<const AllocatedPageSpec> pages)
    -> VsmPageAllocationFrame
  {
    auto frame = VsmPageAllocationFrame {};
    frame.snapshot = VsmPageAllocationSnapshot {};
    frame.plan.decisions.reserve(pages.size());
    for (const auto& page : pages) {
      frame.plan.decisions.push_back(VsmPageAllocationDecision {
        .request = VsmPageRequest {
          .map_id = kTestMapId,
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

  [[nodiscard]] static auto MakeFrame() -> VsmPageAllocationFrame
  {
    constexpr std::array<AllocatedPageSpec, 1> kDefaultPages {
      AllocatedPageSpec {},
    };
    return MakeFrame(kDefaultPages);
  }

  [[nodiscard]] static auto MakeProjection(const std::uint32_t pages_x = 1U,
    const std::uint32_t pages_y = 1U, const std::uint32_t level_count = 1U,
    const std::uint32_t first_page_table_entry = kTestPageTableEntry,
    const std::uint32_t map_pages_x = 0U, const std::uint32_t map_pages_y = 0U,
    const std::uint32_t page_offset_x = 0U,
    const std::uint32_t page_offset_y = 0U,
    const std::uint32_t cube_face_index
    = oxygen::renderer::vsm::kVsmInvalidCubeFaceIndex,
    const glm::mat4& view_matrix = glm::mat4 { 1.0F },
    const glm::mat4& projection_matrix = glm::mat4 { 1.0F },
    const glm::vec4& view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 1.0F })
    -> VsmPageRequestProjection
  {
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = view_matrix,
        .projection_matrix = projection_matrix,
        .view_origin_ws_pad = view_origin_ws_pad,
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(VsmProjectionLightType::kLocal),
      },
      .map_id = kTestMapId,
      .first_page_table_entry = first_page_table_entry,
      .map_pages_x = map_pages_x == 0U ? pages_x : map_pages_x,
      .map_pages_y = map_pages_y == 0U ? pages_y : map_pages_y,
      .pages_x = pages_x,
      .pages_y = pages_y,
      .page_offset_x = page_offset_x,
      .page_offset_y = page_offset_y,
      .level_count = level_count,
      .coarse_level = 0U,
      .light_index = 0U,
      .cube_face_index = cube_face_index,
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
  EXPECT_EQ(prepared_pages[0].projection_page,
    (VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U }));
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

NOLINT_TEST_F(VsmShadowRasterizerPassGpuTest,
  ExecuteRoutesSharedMapPointLightFacesToMatchingProjectionView)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase-f-shadow-point-face")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto physical_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(physical_pool.is_available);
  ASSERT_NE(physical_pool.shadow_texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 30U };

  std::array<TestVertex, 3> vertices {
    TestVertex {
      .position = { 5.0F, -0.2F, -0.2F },
      .normal = { -1.0F, 0.0F, 0.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 0.0F, 1.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 5.0F, -0.2F, 0.2F },
      .normal = { -1.0F, 0.0F, 0.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 0.0F, 1.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 5.0F, 0.2F, 0.0F },
      .normal = { -1.0F, 0.0F, 0.0F },
      .texcoord = { 0.5F, 0.0F },
      .tangent = { 0.0F, 1.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 0.0F, 0.0F, 1.0F, 1.0F },
    },
  };
  constexpr std::array<std::uint32_t, 3> kIndices { 0U, 1U, 2U };

  auto pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig {
        .debug_name = "phase-f-rasterizer-point-face" }));

  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence });

  auto vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    vertices, "phase-f-rasterizer-point-face.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-f-rasterizer-point-face.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-point-face.worlds");
  world_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto world_allocation = world_buffer.Allocate(1U);
  ASSERT_TRUE(world_allocation.has_value());
  ASSERT_TRUE(world_allocation->IsValid(kFrameSequence));
  const auto world_matrix = glm::mat4 { 1.0F };
  std::memcpy(
    world_allocation->mapped_ptr, &world_matrix, sizeof(world_matrix));

  auto draw_metadata_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(DrawMetadata),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-point-face.draws");
  draw_metadata_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto draw_allocation = draw_metadata_buffer.Allocate(1U);
  ASSERT_TRUE(draw_allocation.has_value());
  ASSERT_TRUE(draw_allocation->IsValid(kFrameSequence));

  auto shadow_caster_mask = PassMask {};
  shadow_caster_mask.Set(PassMaskBit::kOpaque);
  shadow_caster_mask.Set(PassMaskBit::kShadowCaster);

  std::array<DrawMetadata, 1> draw_records {
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
      .transform_generation = 51U,
      .submesh_index = 0U,
      .primitive_flags = 0U,
    },
  };
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

  const std::array<glm::vec4, 1> draw_bounds {
    glm::vec4 { 5.0F, 0.0F, 0.0F, 0.35F },
  };
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-f-rasterizer-point-face.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-point-face.DrawFrameBindings");
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
    "phase-f-rasterizer-point-face.ViewFrameBindings");
  view_frame_publisher.OnFrameStart(kFrameSequence, kFrameSlot);
  const auto view_frame_slot = view_frame_publisher.Publish(
    kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
  ASSERT_TRUE(view_frame_slot.IsValid());

  std::array<float, 16> world_matrix_floats {};
  std::memcpy(world_matrix_floats.data(), &world_matrix, sizeof(world_matrix));
  std::array<PreparedSceneFrame::PartitionRange, 1> partitions {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = shadow_caster_mask,
      .begin = 0U,
      .end = 1U,
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

  constexpr std::array<AllocatedPageSpec, 1> kPageSpecs {
    AllocatedPageSpec {
      .virtual_page = VsmVirtualPageCoord {
        .level = 0U,
        .page_x = 1U,
        .page_y = 0U,
      },
      .physical_page = 1U,
    },
  };
  auto frame = MakeFrame(kPageSpecs);
  AttachRasterOutputBuffers(frame, 600ULL, 4U, "phase-f-rasterizer-point-face");

  const auto face0_view = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
  const auto face1_view = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 1.0F, 0.0F, 0.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
  const auto face_projection = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
    glm::radians(90.0F), 1.0F, 0.1F, 10.0F);

  pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = frame,
    .physical_pool = physical_pool,
    .projections
    = {
        MakeProjection(1U, 1U, 1U, kTestPageTableEntry, 2U, 1U, 0U, 0U, 0U,
          face0_view, face_projection),
        MakeProjection(1U, 1U, 1U, kTestPageTableEntry, 2U, 1U, 1U, 0U, 1U,
          face1_view, face_projection),
      },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence),
  });

  ClearShadowSlice(physical_pool.shadow_texture, 0U, 0.0F,
    "phase-f-rasterizer-point-face.clear");

  offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  {
    auto recorder = AcquireRecorder("phase-f-rasterizer-point-face");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto prepared_pages = pass.GetPreparedPages();
  ASSERT_EQ(prepared_pages.size(), 1U);
  EXPECT_EQ(prepared_pages[0].virtual_page,
    (VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 0U }));
  EXPECT_EQ(prepared_pages[0].projection_page,
    (VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U }));
  EXPECT_EQ(prepared_pages[0].projection.cube_face_index, 1U);

  const auto inspection = pass.GetIndirectPartitionsForInspection();
  ASSERT_EQ(inspection.size(), 1U);
  ASSERT_NE(inspection[0].count_buffer, nullptr);

  const auto count_bytes = ReadBufferBytes(*inspection[0].count_buffer,
    sizeof(std::uint32_t), "phase-f-rasterizer-point-face.counts");
  std::uint32_t count = 0U;
  ASSERT_EQ(count_bytes.size(), sizeof(count));
  std::memcpy(&count, count_bytes.data(), sizeof(count));
  EXPECT_EQ(count, 1U);

  const float target_depth = ReadDepthTexel(physical_pool.shadow_texture, 0U,
    192U, 64U, "phase-f-rasterizer-point-face.target");
  const float neighbor_depth = ReadDepthTexel(physical_pool.shadow_texture, 0U,
    64U, 64U, "phase-f-rasterizer-point-face.neighbor");
  EXPECT_GT(target_depth, 0.0F);
  EXPECT_FLOAT_EQ(neighbor_depth, 0.0F);
  EXPECT_EQ(render_context.GetPass<VsmShadowRasterizerPass>(), &pass);
}

NOLINT_TEST_F(VsmShadowRasterizerPassGpuTest,
  ExecuteCompactsOpaqueShadowCasterDrawsPerPageAndRasterizesExpectedPages)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-f-shadow-draw")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto physical_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(physical_pool.is_available);
  ASSERT_NE(physical_pool.shadow_texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 2U };

  std::array<TestVertex, 3> vertices {
    TestVertex {
      .position = { -0.15F, -0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.15F, -0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.0F, 0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.5F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 0.0F, 1.0F, 1.0F },
    },
  };
  constexpr std::array<std::uint32_t, 3> kIndices { 0U, 1U, 2U };

  auto pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig {
        .debug_name = "phase-f-rasterizer-draw" }));

  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence });

  auto vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    vertices, "phase-f-rasterizer-draw.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-f-rasterizer-draw.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-draw.worlds");
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
    "phase-f-rasterizer-draw.draws");
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
      .transform_generation = 11U,
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
      .transform_generation = 12U,
      .submesh_index = 0U,
      .primitive_flags = 0U,
    },
  };
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

  const std::array<glm::vec4, 2> draw_bounds {
    glm::vec4 { -0.5F, 0.0F, 0.25F, 0.30F },
    glm::vec4 { 0.5F, 0.0F, 0.25F, 0.30F },
  };
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-f-rasterizer-draw.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-draw.DrawFrameBindings");
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
    "phase-f-rasterizer-draw.ViewFrameBindings");
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

  constexpr std::array<AllocatedPageSpec, 2> kPageSpecs {
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

  auto frame = MakeFrame(kPageSpecs);
  auto initial_meta = std::vector<VsmPhysicalPageMeta>(4U);
  initial_meta[0].dynamic_invalidated = true;
  initial_meta[0].view_uncached = true;
  initial_meta[1].dynamic_invalidated = true;
  initial_meta[1].view_uncached = true;
  AttachRasterOutputBuffers(frame, 200ULL, initial_meta.size(),
    "phase-f-rasterizer-draw", initial_meta);

  pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = frame,
    .physical_pool = physical_pool,
    .projections = { MakeProjection(2U, 1U) },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence),
  });

  ClearShadowSlice(
    physical_pool.shadow_texture, 0U, 0.0F, "phase-f-rasterizer-draw.clear");

  offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  {
    auto recorder = AcquireRecorder("phase-f-rasterizer-draw");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto inspection = pass.GetIndirectPartitionsForInspection();
  ASSERT_EQ(inspection.size(), 1U);
  ASSERT_NE(inspection[0].command_buffer, nullptr);
  ASSERT_NE(inspection[0].count_buffer, nullptr);
  ASSERT_EQ(inspection[0].draw_count, 2U);
  ASSERT_EQ(inspection[0].max_commands_per_page, 2U);

  const auto count_bytes = ReadBufferBytes(*inspection[0].count_buffer,
    sizeof(std::uint32_t) * kPageSpecs.size(),
    "phase-f-rasterizer-draw.counts");
  std::array<std::uint32_t, 2> counts {};
  ASSERT_EQ(count_bytes.size(), sizeof(counts));
  std::memcpy(counts.data(), count_bytes.data(), sizeof(counts));
  EXPECT_EQ(counts[0], 1U);
  EXPECT_EQ(counts[1], 1U);

  const auto command_bytes = ReadBufferBytes(*inspection[0].command_buffer,
    sizeof(VsmShaderIndirectDrawCommand) * kPageSpecs.size()
      * inspection[0].max_commands_per_page,
    "phase-f-rasterizer-draw.commands");
  std::array<VsmShaderIndirectDrawCommand, 4> commands {};
  ASSERT_EQ(command_bytes.size(), sizeof(commands));
  std::memcpy(commands.data(), command_bytes.data(), sizeof(commands));
  EXPECT_EQ(commands[0].draw_index, 0U);
  EXPECT_EQ(commands[0].vertex_count_per_instance, 3U);
  EXPECT_EQ(commands[0].instance_count, 1U);
  EXPECT_EQ(commands[inspection[0].max_commands_per_page].draw_index, 1U);
  EXPECT_EQ(
    commands[inspection[0].max_commands_per_page].vertex_count_per_instance,
    3U);
  EXPECT_EQ(commands[inspection[0].max_commands_per_page].instance_count, 1U);

  const float left_page_depth = ReadDepthTexel(physical_pool.shadow_texture, 0U,
    64U, 64U, "phase-f-rasterizer-draw.left-page");
  const float right_page_depth = ReadDepthTexel(physical_pool.shadow_texture,
    0U, 192U, 64U, "phase-f-rasterizer-draw.right-page");
  const float untouched_depth = ReadDepthTexel(physical_pool.shadow_texture, 0U,
    320U, 64U, "phase-f-rasterizer-draw.untouched");

  EXPECT_NEAR(left_page_depth, 0.25F, 1.0e-4F);
  EXPECT_NEAR(right_page_depth, 0.25F, 1.0e-4F);
  EXPECT_FLOAT_EQ(untouched_depth, 0.0F);

  const auto dirty_flags = ReadBufferAs<std::uint32_t>(frame.dirty_flags_buffer,
    initial_meta.size(), "phase-f-rasterizer-draw.dirty");
  EXPECT_EQ(dirty_flags[0],
    static_cast<std::uint32_t>(
      VsmRenderedPageDirtyFlagBits::kDynamicRasterized));
  EXPECT_EQ(dirty_flags[1],
    static_cast<std::uint32_t>(
      VsmRenderedPageDirtyFlagBits::kDynamicRasterized));
  EXPECT_EQ(dirty_flags[2], 0U);

  const auto metadata
    = ReadBufferAs<VsmPhysicalPageMeta>(frame.physical_page_meta_buffer,
      initial_meta.size(), "phase-f-rasterizer-draw.meta");
  EXPECT_TRUE(static_cast<bool>(metadata[0].is_dirty));
  EXPECT_TRUE(static_cast<bool>(metadata[1].is_dirty));
  EXPECT_TRUE(static_cast<bool>(metadata[0].used_this_frame));
  EXPECT_TRUE(static_cast<bool>(metadata[1].used_this_frame));
  EXPECT_FALSE(static_cast<bool>(metadata[0].view_uncached));
  EXPECT_FALSE(static_cast<bool>(metadata[1].view_uncached));
  EXPECT_FALSE(static_cast<bool>(metadata[0].dynamic_invalidated));
  EXPECT_FALSE(static_cast<bool>(metadata[1].dynamic_invalidated));
  EXPECT_EQ(metadata[0].last_touched_frame, 200ULL);
  EXPECT_EQ(metadata[1].last_touched_frame, 200ULL);

  EXPECT_EQ(pass.GetPreparedPageCount(), kPageSpecs.size());
  EXPECT_EQ(render_context.GetPass<VsmShadowRasterizerPass>(), &pass);
}

NOLINT_TEST_F(VsmShadowRasterizerPassGpuTest,
  ExecuteRasterizedDirectionalPagesProjectLocalizedShadowMaskInsteadOfPageWideDarkening)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase-f-shadow-localized")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto physical_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(physical_pool.is_available);
  ASSERT_NE(physical_pool.shadow_texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 21U };
  constexpr auto kOutputWidth = 64U;
  constexpr auto kOutputHeight = 64U;

  std::array<TestVertex, 4> vertices {
    TestVertex {
      .position = { -0.15F, -0.15F, 0.85F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.15F, -0.15F, 0.85F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.15F, 0.15F, 0.85F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 0.0F, 1.0F, 1.0F },
    },
    TestVertex {
      .position = { -0.15F, 0.15F, 0.85F },
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

  auto pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig {
        .debug_name = "phase-f-rasterizer-localized" }));

  auto vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    vertices, "phase-f-rasterizer-localized.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-f-rasterizer-localized.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-localized.worlds");
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
    "phase-f-rasterizer-localized.draws");
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
    glm::vec4 { -0.5F, 0.0F, 0.85F, 0.35F },
    glm::vec4 { 0.5F, 0.0F, 0.85F, 0.35F },
  };
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-f-rasterizer-localized.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-localized.DrawFrameBindings");
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
    "phase-f-rasterizer-localized.ViewFrameBindings");
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

  constexpr std::array<AllocatedPageSpec, 2> kPageSpecs {
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

  auto frame = MakeFrame(kPageSpecs);
  auto initial_meta = std::vector<VsmPhysicalPageMeta>(4U);
  initial_meta[0].dynamic_invalidated = true;
  initial_meta[0].view_uncached = true;
  initial_meta[1].dynamic_invalidated = true;
  initial_meta[1].view_uncached = true;
  AttachRasterOutputBuffers(frame, 700ULL, initial_meta.size(),
    "phase-f-rasterizer-localized", initial_meta);

  auto projection = MakeProjection(2U, 1U);
  projection.projection.light_type
    = static_cast<std::uint32_t>(VsmProjectionLightType::kDirectional);
  frame.snapshot.projection_records = { projection };
  frame.snapshot.page_table.resize(kTestPageTableEntry + 2U);
  frame.snapshot.page_table[kTestPageTableEntry] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 0U },
  };
  frame.snapshot.page_table[kTestPageTableEntry + 1U] = {
    .is_mapped = true,
    .physical_page = VsmPhysicalPageIndex { .value = 1U },
  };
  auto shader_page_table
    = std::vector<VsmShaderPageTableEntry>(frame.snapshot.page_table.size(),
      oxygen::renderer::vsm::MakeUnmappedShaderPageTableEntry());
  shader_page_table[kTestPageTableEntry]
    = oxygen::renderer::vsm::MakeMappedShaderPageTableEntry(
      VsmPhysicalPageIndex { .value = 0U });
  shader_page_table[kTestPageTableEntry + 1U]
    = oxygen::renderer::vsm::MakeMappedShaderPageTableEntry(
      VsmPhysicalPageIndex { .value = 1U });
  frame.page_table_buffer = CreateStructuredStorageBuffer(
    std::span<const VsmShaderPageTableEntry>(
      shader_page_table.data(), shader_page_table.size()),
    "phase-f-rasterizer-localized.PageTable");

  pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = frame,
    .physical_pool = physical_pool,
    .projections = { projection },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence),
  });

  ClearShadowSlice(physical_pool.shadow_texture, 0U, 0.0F,
    "phase-f-rasterizer-localized.clear");

  {
    auto raster_offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence });
    raster_offscreen.SetCurrentView(kTestViewId,
      MakeResolvedView(kOutputWidth, kOutputHeight), prepared_frame);
    auto moved_raster_offscreen = std::move(raster_offscreen);
    auto& raster_context = moved_raster_offscreen.GetRenderContext();

    auto recorder = AcquireRecorder("phase-f-rasterizer-localized");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, raster_context, *recorder);
    WaitForQueueIdle();
  }

  auto scene_depth = CreateDepthTexture2D(
    kOutputWidth, kOutputHeight, "phase-f-rasterizer-localized.depth");
  UploadDepthTexture(scene_depth, 0.75F, "phase-f-rasterizer-localized.depth");

  auto projection_pass
    = VsmProjectionPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmProjectionPassConfig>(VsmProjectionPassConfig {
        .debug_name = "phase-f-rasterizer-localized.projection",
      }));
  projection_pass.SetInput(VsmProjectionPassInput {
    .frame = frame,
    .physical_pool = physical_pool,
    .scene_depth_texture = scene_depth,
  });

  {
    auto projection_offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = kFrameSlot, .frame_sequence = SequenceNumber { 22U } });
    auto projection_prepared_frame = PreparedSceneFrame {};
    projection_offscreen.SetCurrentView(kTestViewId,
      MakeResolvedView(kOutputWidth, kOutputHeight), projection_prepared_frame);
    auto moved_projection_offscreen = std::move(projection_offscreen);
    auto& projection_context = moved_projection_offscreen.GetRenderContext();

    auto recorder = AcquireRecorder("phase-f-rasterizer-localized.projection");
    ASSERT_NE(recorder, nullptr);
    RunPass(projection_pass, projection_context, *recorder);
    WaitForQueueIdle();
  }

  const auto output = projection_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(output.available);
  ASSERT_NE(output.directional_shadow_mask_texture, nullptr);

  const auto left_center
    = ReadDepthTexel(output.directional_shadow_mask_texture, 0U, 16U, 32U,
      "phase-f-rasterizer-localized.mask.left-center");
  const auto right_center
    = ReadDepthTexel(output.directional_shadow_mask_texture, 0U, 48U, 32U,
      "phase-f-rasterizer-localized.mask.right-center");
  const auto left_edge = ReadDepthTexel(output.directional_shadow_mask_texture,
    0U, 4U, 32U, "phase-f-rasterizer-localized.mask.left-edge");
  const auto right_edge = ReadDepthTexel(output.directional_shadow_mask_texture,
    0U, 60U, 32U, "phase-f-rasterizer-localized.mask.right-edge");
  const auto top_left = ReadDepthTexel(output.directional_shadow_mask_texture,
    0U, 4U, 4U, "phase-f-rasterizer-localized.mask.top-left");
  const auto bottom_right
    = ReadDepthTexel(output.directional_shadow_mask_texture, 0U, 60U, 60U,
      "phase-f-rasterizer-localized.mask.bottom-right");

  EXPECT_LT(left_center, 0.1F);
  EXPECT_LT(right_center, 0.1F);
  EXPECT_NEAR(left_edge, 1.0F, 1.0e-4F);
  EXPECT_NEAR(right_edge, 1.0F, 1.0e-4F);
  EXPECT_NEAR(top_left, 1.0F, 1.0e-4F);
  EXPECT_NEAR(bottom_right, 1.0F, 1.0e-4F);
}

NOLINT_TEST_F(VsmShadowRasterizerPassGpuTest,
  ExecuteRoutesStaticOnlyPagesIntoStaticSliceAndPublishesFeedback)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase-f-shadow-static")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto physical_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(physical_pool.is_available);
  ASSERT_NE(physical_pool.shadow_texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence = SequenceNumber { 3U };

  std::array<TestVertex, 3> vertices {
    TestVertex {
      .position = { -0.15F, -0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.15F, -0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.0F, 0.15F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.5F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 0.0F, 1.0F, 1.0F },
    },
  };
  constexpr std::array<std::uint32_t, 3> kIndices { 0U, 1U, 2U };

  auto pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig {
        .debug_name = "phase-f-rasterizer-static" }));

  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence });

  auto vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    vertices, "phase-f-rasterizer-static.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-f-rasterizer-static.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-static.worlds");
  world_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto world_allocation = world_buffer.Allocate(1U);
  ASSERT_TRUE(world_allocation.has_value());
  ASSERT_TRUE(world_allocation->IsValid(kFrameSequence));
  const auto world_matrix = glm::mat4 { 1.0F };
  std::memcpy(
    world_allocation->mapped_ptr, &world_matrix, sizeof(world_matrix));

  auto draw_metadata_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(DrawMetadata),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-static.draws");
  draw_metadata_buffer.OnFrameStart(kFrameSequence, kFrameSlot);
  auto draw_allocation = draw_metadata_buffer.Allocate(1U);
  ASSERT_TRUE(draw_allocation.has_value());
  ASSERT_TRUE(draw_allocation->IsValid(kFrameSequence));

  auto shadow_caster_mask = PassMask {};
  shadow_caster_mask.Set(PassMaskBit::kOpaque);
  shadow_caster_mask.Set(PassMaskBit::kShadowCaster);

  std::array<DrawMetadata, 1> draw_records {
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
      .transform_generation = 21U,
      .submesh_index = 0U,
      .primitive_flags
      = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kStaticShadowCaster),
    },
  };
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

  const std::array<glm::vec4, 1> draw_bounds {
    glm::vec4 { 0.0F, 0.0F, 0.25F, 0.35F },
  };
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-f-rasterizer-static.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-static.DrawFrameBindings");
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
    "phase-f-rasterizer-static.ViewFrameBindings");
  view_frame_publisher.OnFrameStart(kFrameSequence, kFrameSlot);
  const auto view_frame_slot = view_frame_publisher.Publish(
    kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
  ASSERT_TRUE(view_frame_slot.IsValid());

  std::array<float, 16> world_matrix_floats {};
  std::memcpy(world_matrix_floats.data(), &world_matrix, sizeof(world_matrix));
  std::array<PreparedSceneFrame::PartitionRange, 1> partitions {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = shadow_caster_mask,
      .begin = 0U,
      .end = 1U,
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

  constexpr std::array<AllocatedPageSpec, 1> kPageSpecs {
    AllocatedPageSpec {
      .virtual_page = VsmVirtualPageCoord {
        .level = 0U,
        .page_x = 0U,
        .page_y = 0U,
      },
      .physical_page = 0U,
      .flags = VsmPageRequestFlags::kRequired | VsmPageRequestFlags::kStaticOnly,
    },
  };

  auto frame = MakeFrame(kPageSpecs);
  auto initial_meta = std::vector<VsmPhysicalPageMeta>(2U);
  initial_meta[0].static_invalidated = true;
  initial_meta[0].view_uncached = true;
  AttachRasterOutputBuffers(frame, 300ULL, initial_meta.size(),
    "phase-f-rasterizer-static", initial_meta);

  pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = frame,
    .physical_pool = physical_pool,
    .projections = { MakeProjection() },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence),
  });

  ClearShadowSlice(physical_pool.shadow_texture, 0U, 0.9F,
    "phase-f-rasterizer-static.dynamic-clear");
  ClearShadowSlice(physical_pool.shadow_texture, 1U, 0.0F,
    "phase-f-rasterizer-static.static-clear");
  const float dynamic_depth_before
    = ReadDepthTexel(physical_pool.shadow_texture, 0U, 64U, 64U,
      "phase-f-rasterizer-static.dynamic-before");
  const float static_depth_before = ReadDepthTexel(physical_pool.shadow_texture,
    1U, 64U, 64U, "phase-f-rasterizer-static.static-before");
  EXPECT_FLOAT_EQ(dynamic_depth_before, 0.9F);
  EXPECT_FLOAT_EQ(static_depth_before, 0.0F);

  offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  {
    auto recorder = AcquireRecorder("phase-f-rasterizer-static");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const float dynamic_depth = ReadDepthTexel(physical_pool.shadow_texture, 0U,
    64U, 64U, "phase-f-rasterizer-static.dynamic");
  const float static_depth = ReadDepthTexel(physical_pool.shadow_texture, 1U,
    64U, 64U, "phase-f-rasterizer-static.static");
  EXPECT_FLOAT_EQ(dynamic_depth, dynamic_depth_before);
  EXPECT_NEAR(static_depth, 0.25F, 1.0e-4F);

  const auto dirty_flags = ReadBufferAs<std::uint32_t>(frame.dirty_flags_buffer,
    initial_meta.size(), "phase-f-rasterizer-static.dirty");
  EXPECT_EQ(dirty_flags[0],
    static_cast<std::uint32_t>(
      VsmRenderedPageDirtyFlagBits::kStaticRasterized));
  EXPECT_EQ(dirty_flags[1], 0U);

  const auto metadata
    = ReadBufferAs<VsmPhysicalPageMeta>(frame.physical_page_meta_buffer,
      initial_meta.size(), "phase-f-rasterizer-static.meta");
  EXPECT_TRUE(static_cast<bool>(metadata[0].is_dirty));
  EXPECT_TRUE(static_cast<bool>(metadata[0].used_this_frame));
  EXPECT_FALSE(static_cast<bool>(metadata[0].view_uncached));
  EXPECT_FALSE(static_cast<bool>(metadata[0].static_invalidated));
  EXPECT_EQ(metadata[0].last_touched_frame, 300ULL);

  const auto feedback = pass.GetStaticPageFeedback();
  ASSERT_EQ(feedback.size(), 1U);
  EXPECT_EQ(feedback[0].page_table_index, kTestPageTableEntry);
  EXPECT_EQ(feedback[0].physical_page.value, 0U);
  EXPECT_EQ(feedback[0].map_id, kTestMapId);
  EXPECT_EQ(feedback[0].virtual_page,
    (VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U }));
  EXPECT_EQ(feedback[0].primitive,
    (VsmPrimitiveIdentity {
      .transform_index = 0U,
      .transform_generation = 21U,
      .submesh_index = 0U,
      .primitive_flags
      = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kStaticShadowCaster),
    }));
  EXPECT_EQ(render_context.GetPass<VsmShadowRasterizerPass>(), &pass);
}

NOLINT_TEST_F(
  VsmShadowRasterizerPassGpuTest, ExecuteUsesPreviousFrameHzbToCullDraws)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-f-shadow-hzb")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto physical_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(physical_pool.is_available);
  ASSERT_NE(physical_pool.shadow_texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence1 = SequenceNumber { 10U };
  constexpr auto kFrameSequence2 = SequenceNumber { 11U };

  auto screen_hzb_pass
    = ScreenHzbBuildPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<ScreenHzbBuildPassConfig>(
        ScreenHzbBuildPassConfig { .debug_name = "phase-f-hzb-history" }));

  auto previous_depth = CreateDepthTexture2D(4U, 4U, "phase-f-hzb.depth1");
  ASSERT_NE(previous_depth, nullptr);
  UploadDepthTexture(previous_depth, 0.1F, "phase-f-hzb.depth1");

  {
    auto depth_config = std::make_shared<DepthPrePass::Config>();
    depth_config->depth_texture = previous_depth;
    depth_config->debug_name = "phase-f-hzb.depth1";
    auto depth_pass = DepthPrePass(depth_config);
    auto prepared_frame = PreparedSceneFrame {};
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence1 });
    offscreen.SetCurrentView(
      kTestViewId, MakeResolvedView(4U, 4U), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);

    auto recorder = AcquireRecorder("phase-f-hzb.frame1");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(
      *recorder, previous_depth, oxygen::graphics::ResourceStates::kCommon);
    oxygen::co::testing::TestEventLoop publish_loop;
    oxygen::co::Run(publish_loop, [&]() -> oxygen::co::Co<> {
      co_await depth_pass.PrepareResources(render_context, *recorder);
    });
    RunPass(screen_hzb_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  // Mirror runtime ordering: frame-local transient uploads are allocated after
  // renderer frame services advance staging retirement for this slot.
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence2 });

  std::array<TestVertex, 3> vertices {
    TestVertex {
      .position = { -0.2F, -0.2F, 0.8F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.2F, -0.2F, 0.8F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.0F, 0.2F, 0.8F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.5F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 0.0F, 1.0F, 1.0F },
    },
  };
  constexpr std::array<std::uint32_t, 3> kIndices { 0U, 1U, 2U };

  auto pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig {
        .debug_name = "phase-f-rasterizer-hzb" }));

  auto vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    vertices, "phase-f-rasterizer-hzb.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-f-rasterizer-hzb.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-hzb.worlds");
  world_buffer.OnFrameStart(kFrameSequence2, kFrameSlot);
  auto world_allocation = world_buffer.Allocate(1U);
  ASSERT_TRUE(world_allocation.has_value());
  ASSERT_TRUE(world_allocation->IsValid(kFrameSequence2));
  const auto world_matrix = glm::mat4 { 1.0F };
  std::memcpy(
    world_allocation->mapped_ptr, &world_matrix, sizeof(world_matrix));

  auto draw_metadata_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(DrawMetadata),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-hzb.draws");
  draw_metadata_buffer.OnFrameStart(kFrameSequence2, kFrameSlot);
  auto draw_allocation = draw_metadata_buffer.Allocate(1U);
  ASSERT_TRUE(draw_allocation.has_value());
  ASSERT_TRUE(draw_allocation->IsValid(kFrameSequence2));

  auto shadow_caster_mask = PassMask {};
  shadow_caster_mask.Set(PassMaskBit::kOpaque);
  shadow_caster_mask.Set(PassMaskBit::kShadowCaster);

  std::array<DrawMetadata, 1> draw_records {
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
      .primitive_flags
      = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible),
    },
  };
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

  const std::array<glm::vec4, 1> draw_bounds {
    glm::vec4 { 0.0F, 0.0F, 0.8F, 0.35F },
  };
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-f-rasterizer-hzb.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-hzb.DrawFrameBindings");
  draw_frame_publisher.OnFrameStart(kFrameSequence2, kFrameSlot);
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
    "phase-f-rasterizer-hzb.ViewFrameBindings");
  view_frame_publisher.OnFrameStart(kFrameSequence2, kFrameSlot);
  const auto view_frame_slot = view_frame_publisher.Publish(
    kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
  ASSERT_TRUE(view_frame_slot.IsValid());

  std::array<float, 16> world_matrix_floats {};
  std::memcpy(world_matrix_floats.data(), &world_matrix, sizeof(world_matrix));
  std::array<PreparedSceneFrame::PartitionRange, 1> partitions {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = shadow_caster_mask,
      .begin = 0U,
      .end = 1U,
    },
  };

  auto current_depth = CreateDepthTexture2D(4U, 4U, "phase-f-hzb.depth2");
  ASSERT_NE(current_depth, nullptr);
  UploadDepthTexture(current_depth, 1.0F, "phase-f-hzb.depth2");

  auto depth_config = std::make_shared<DepthPrePass::Config>();
  depth_config->depth_texture = current_depth;
  depth_config->debug_name = "phase-f-hzb.depth2";
  auto depth_pass = DepthPrePass(depth_config);

  auto prepared_frame = PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes = std::as_bytes(std::span(draw_records));
  prepared_frame.world_matrices = std::span<const float>(
    world_matrix_floats.data(), world_matrix_floats.size());
  prepared_frame.draw_bounding_spheres = std::span(draw_bounds);
  prepared_frame.partitions = std::span(partitions);
  prepared_frame.bindless_worlds_slot = world_allocation->srv;
  prepared_frame.bindless_draw_metadata_slot = draw_allocation->srv;
  prepared_frame.bindless_draw_bounds_slot = draw_bounds_buffer.slot;

  constexpr std::array<AllocatedPageSpec, 1> kPageSpecs {
    AllocatedPageSpec {
      .virtual_page = VsmVirtualPageCoord {
        .level = 0U,
        .page_x = 0U,
        .page_y = 0U,
      },
      .physical_page = 0U,
    },
  };
  auto frame = MakeFrame(kPageSpecs);
  AttachRasterOutputBuffers(frame, 400ULL, 2U, "phase-f-rasterizer-hzb");

  pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = frame,
    .physical_pool = physical_pool,
    .projections = { MakeProjection() },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence2),
    .previous_visible_shadow_primitives = {
      VsmPrimitiveIdentity {
        .transform_index = 0U,
        .transform_generation = 31U,
        .submesh_index = 0U,
        .primitive_flags = static_cast<std::uint32_t>(
          DrawPrimitiveFlagBits::kMainViewVisible),
      },
    },
  });

  ClearShadowSlice(
    physical_pool.shadow_texture, 0U, 0.0F, "phase-f-rasterizer-hzb.clear");

  offscreen.SetCurrentView(
    kTestViewId, MakeResolvedView(4U, 4U), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  {
    auto recorder = AcquireRecorder("phase-f-hzb.frame2");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(
      *recorder, current_depth, oxygen::graphics::ResourceStates::kCommon);
    oxygen::co::testing::TestEventLoop publish_loop;
    oxygen::co::Run(publish_loop, [&]() -> oxygen::co::Co<> {
      co_await depth_pass.PrepareResources(render_context, *recorder);
    });
    RunPass(screen_hzb_pass, render_context, *recorder);
  }

  const auto previous_hzb = screen_hzb_pass.GetPreviousFrameOutput(kTestViewId);
  ASSERT_TRUE(previous_hzb.available);
  ASSERT_NE(previous_hzb.texture, nullptr);
  ASSERT_TRUE(previous_hzb.srv_index.IsValid());
  EXPECT_FLOAT_EQ(ReadTextureMipTexel(
                    previous_hzb.texture, 0U, 0U, 0U, "phase-f-hzb.prev.mip0"),
    0.1F);
  EXPECT_FLOAT_EQ(
    ReadTextureMipTexel(previous_hzb.texture, previous_hzb.mip_count - 1U, 0U,
      0U, "phase-f-hzb.prev.last-mip"),
    0.1F);

  {
    auto recorder = AcquireRecorder("phase-f-rasterizer-hzb");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto inspection = pass.GetIndirectPartitionsForInspection();
  ASSERT_EQ(inspection.size(), 1U);
  ASSERT_NE(inspection[0].count_buffer, nullptr);

  const auto count_bytes = ReadBufferBytes(*inspection[0].count_buffer,
    sizeof(std::uint32_t), "phase-f-rasterizer-hzb.counts");
  std::uint32_t count = 0U;
  ASSERT_EQ(count_bytes.size(), sizeof(count));
  std::memcpy(&count, count_bytes.data(), sizeof(count));
  EXPECT_EQ(count, 0U);

  const auto dirty_flags = ReadBufferAs<std::uint32_t>(
    frame.dirty_flags_buffer, 2U, "phase-f-rasterizer-hzb.dirty");
  EXPECT_EQ(dirty_flags[0], 0U);

  const float page_depth = ReadDepthTexel(
    physical_pool.shadow_texture, 0U, 64U, 64U, "phase-f-rasterizer-hzb.page");
  EXPECT_FLOAT_EQ(page_depth, 0.0F);
  EXPECT_EQ(pass.GetVisibleShadowPrimitives().size(), 1U);
  EXPECT_EQ(pass.GetPreparedPageCount(), 1U);
  EXPECT_EQ(render_context.GetPass<VsmShadowRasterizerPass>(), &pass);
}

NOLINT_TEST_F(VsmShadowRasterizerPassGpuTest,
  ExecuteRevealForcesPreviouslyOccludedDrawsAndPublishesVisiblePrimitives)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase-f-shadow-reveal")),
    VsmPhysicalPoolChangeResult::kCreated);

  const auto physical_pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(physical_pool.is_available);
  ASSERT_NE(physical_pool.shadow_texture, nullptr);

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr auto kFrameSlot = Slot { 0U };
  constexpr auto kFrameSequence1 = SequenceNumber { 20U };
  constexpr auto kFrameSequence2 = SequenceNumber { 21U };

  auto screen_hzb_pass
    = ScreenHzbBuildPass(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<ScreenHzbBuildPassConfig>(
        ScreenHzbBuildPassConfig { .debug_name = "phase-f-reveal-history" }));

  auto previous_depth = CreateDepthTexture2D(4U, 4U, "phase-f-reveal.depth1");
  ASSERT_NE(previous_depth, nullptr);
  UploadDepthTexture(previous_depth, 0.1F, "phase-f-reveal.depth1");

  {
    auto depth_config = std::make_shared<DepthPrePass::Config>();
    depth_config->depth_texture = previous_depth;
    depth_config->debug_name = "phase-f-reveal.depth1";
    auto depth_pass = DepthPrePass(depth_config);
    auto prepared_frame = PreparedSceneFrame {};
    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence1 });
    offscreen.SetCurrentView(
      kTestViewId, MakeResolvedView(4U, 4U), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);

    auto recorder = AcquireRecorder("phase-f-reveal.frame1");
    ASSERT_NE(recorder, nullptr);
    RunPass(screen_hzb_pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  // Mirror runtime ordering: frame-local transient uploads are allocated after
  // renderer frame services advance staging retirement for this slot.
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = kFrameSlot, .frame_sequence = kFrameSequence2 });

  std::array<TestVertex, 3> vertices {
    TestVertex {
      .position = { -0.2F, -0.2F, 0.8F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.2F, -0.2F, 0.8F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.0F, 0.2F, 0.8F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.5F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 0.0F, 1.0F, 1.0F },
    },
  };
  constexpr std::array<std::uint32_t, 3> kIndices { 0U, 1U, 2U };

  auto pass = VsmShadowRasterizerPass(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    std::make_shared<VsmShadowRasterizerPassConfig>(
      VsmShadowRasterizerPassConfig {
        .debug_name = "phase-f-rasterizer-reveal" }));

  auto vertex_buffer = CreateStructuredSrvBuffer<TestVertex>(
    vertices, "phase-f-rasterizer-reveal.vertices");
  auto index_buffer
    = CreateUIntIndexBuffer(kIndices, "phase-f-rasterizer-reveal.indices");

  auto world_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(glm::mat4),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-reveal.worlds");
  world_buffer.OnFrameStart(kFrameSequence2, kFrameSlot);
  auto world_allocation = world_buffer.Allocate(1U);
  ASSERT_TRUE(world_allocation.has_value());
  ASSERT_TRUE(world_allocation->IsValid(kFrameSequence2));
  const auto world_matrix = glm::mat4 { 1.0F };
  std::memcpy(
    world_allocation->mapped_ptr, &world_matrix, sizeof(world_matrix));

  auto draw_metadata_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(DrawMetadata),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-reveal.draws");
  draw_metadata_buffer.OnFrameStart(kFrameSequence2, kFrameSlot);
  auto draw_allocation = draw_metadata_buffer.Allocate(1U);
  ASSERT_TRUE(draw_allocation.has_value());
  ASSERT_TRUE(draw_allocation->IsValid(kFrameSequence2));

  auto shadow_caster_mask = PassMask {};
  shadow_caster_mask.Set(PassMaskBit::kOpaque);
  shadow_caster_mask.Set(PassMaskBit::kShadowCaster);

  std::array<DrawMetadata, 1> draw_records {
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
      .primitive_flags
      = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible),
    },
  };
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

  const std::array<glm::vec4, 1> draw_bounds {
    glm::vec4 { 0.0F, 0.0F, 0.8F, 0.35F },
  };
  auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
    draw_bounds, "phase-f-rasterizer-reveal.bounds");

  auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-reveal.DrawFrameBindings");
  draw_frame_publisher.OnFrameStart(kFrameSequence2, kFrameSlot);
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
    "phase-f-rasterizer-reveal.ViewFrameBindings");
  view_frame_publisher.OnFrameStart(kFrameSequence2, kFrameSlot);
  const auto view_frame_slot = view_frame_publisher.Publish(
    kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
  ASSERT_TRUE(view_frame_slot.IsValid());

  std::array<float, 16> world_matrix_floats {};
  std::memcpy(world_matrix_floats.data(), &world_matrix, sizeof(world_matrix));
  std::array<PreparedSceneFrame::PartitionRange, 1> partitions {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = shadow_caster_mask,
      .begin = 0U,
      .end = 1U,
    },
  };

  auto current_depth = CreateDepthTexture2D(4U, 4U, "phase-f-reveal.depth2");
  ASSERT_NE(current_depth, nullptr);
  UploadDepthTexture(current_depth, 1.0F, "phase-f-reveal.depth2");

  auto depth_config = std::make_shared<DepthPrePass::Config>();
  depth_config->depth_texture = current_depth;
  depth_config->debug_name = "phase-f-reveal.depth2";
  auto depth_pass = DepthPrePass(depth_config);

  auto prepared_frame = PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes = std::as_bytes(std::span(draw_records));
  prepared_frame.world_matrices = std::span<const float>(
    world_matrix_floats.data(), world_matrix_floats.size());
  prepared_frame.draw_bounding_spheres = std::span(draw_bounds);
  prepared_frame.partitions = std::span(partitions);
  prepared_frame.bindless_worlds_slot = world_allocation->srv;
  prepared_frame.bindless_draw_metadata_slot = draw_allocation->srv;
  prepared_frame.bindless_draw_bounds_slot = draw_bounds_buffer.slot;

  constexpr std::array<AllocatedPageSpec, 1> kPageSpecs {
    AllocatedPageSpec {
      .virtual_page = VsmVirtualPageCoord {
        .level = 0U,
        .page_x = 0U,
        .page_y = 0U,
      },
      .physical_page = 0U,
    },
  };
  auto frame = MakeFrame(kPageSpecs);
  AttachRasterOutputBuffers(frame, 500ULL, 2U, "phase-f-rasterizer-reveal");

  pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = frame,
    .physical_pool = physical_pool,
    .projections = { MakeProjection() },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence2),
  });

  ClearShadowSlice(
    physical_pool.shadow_texture, 0U, 0.0F, "phase-f-rasterizer-reveal.clear");

  offscreen.SetCurrentView(
    kTestViewId, MakeResolvedView(4U, 4U), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  {
    auto recorder = AcquireRecorder("phase-f-reveal.frame2");
    ASSERT_NE(recorder, nullptr);
    RunPass(screen_hzb_pass, render_context, *recorder);
  }

  {
    auto recorder = AcquireRecorder("phase-f-rasterizer-reveal");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto inspection = pass.GetIndirectPartitionsForInspection();
  ASSERT_EQ(inspection.size(), 1U);
  ASSERT_NE(inspection[0].count_buffer, nullptr);

  const auto count_bytes = ReadBufferBytes(*inspection[0].count_buffer,
    sizeof(std::uint32_t), "phase-f-rasterizer-reveal.counts");
  std::uint32_t count = 0U;
  ASSERT_EQ(count_bytes.size(), sizeof(count));
  std::memcpy(&count, count_bytes.data(), sizeof(count));
  EXPECT_EQ(count, 1U);

  const auto dirty_flags = ReadBufferAs<std::uint32_t>(
    frame.dirty_flags_buffer, 2U, "phase-f-rasterizer-reveal.dirty");
  EXPECT_EQ(dirty_flags[0],
    static_cast<std::uint32_t>(VsmRenderedPageDirtyFlagBits::kDynamicRasterized)
      | static_cast<std::uint32_t>(
        VsmRenderedPageDirtyFlagBits::kRevealForced));

  const float page_depth = ReadDepthTexel(physical_pool.shadow_texture, 0U, 64U,
    64U, "phase-f-rasterizer-reveal.page");
  EXPECT_NEAR(page_depth, 0.8F, 1.0e-4F);

  const auto visible_primitives = pass.GetVisibleShadowPrimitives();
  ASSERT_EQ(visible_primitives.size(), 1U);
  EXPECT_EQ(visible_primitives[0],
    (VsmPrimitiveIdentity {
      .transform_index = 0U,
      .transform_generation = 41U,
      .submesh_index = 0U,
      .primitive_flags
      = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible),
    }));
  const auto rendered_history = pass.GetRenderedPrimitiveHistory();
  ASSERT_EQ(rendered_history.size(), 1U);
  EXPECT_EQ(rendered_history[0].map_id, kTestMapId);
  EXPECT_EQ(rendered_history[0].primitive, visible_primitives[0]);
  EXPECT_EQ(render_context.GetPass<VsmShadowRasterizerPass>(), &pass);
}

} // namespace
