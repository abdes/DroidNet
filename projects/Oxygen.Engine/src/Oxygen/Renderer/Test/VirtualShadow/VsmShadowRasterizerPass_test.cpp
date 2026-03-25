//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowGpuTestFixtures.h"

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawFrameBindings.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>

namespace {

using oxygen::Format;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::ShaderVisibleIndex;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::BindlessDrawMetadataSlot;
using oxygen::engine::BindlessViewFrameBindingsSlot;
using oxygen::engine::BindlessWorldsSlot;
using oxygen::engine::DrawFrameBindings;
using oxygen::engine::DrawMetadata;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::ViewConstants;
using oxygen::engine::ViewFrameBindings;
using oxygen::engine::VsmShadowRasterizerPass;
using oxygen::engine::VsmShadowRasterizerPassConfig;
using oxygen::engine::VsmShadowRasterizerPassInput;
using oxygen::engine::internal::PerViewStructuredPublisher;
using oxygen::engine::upload::TransientStructuredBuffer;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferRange;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
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
constexpr std::uint32_t kReadbackRowPitch = 256U;

struct TestVertex {
  glm::vec3 position {};
  glm::vec3 normal {};
  glm::vec2 texcoord {};
  glm::vec3 tangent {};
  glm::vec3 bitangent {};
  glm::vec4 color {};
};
static_assert(sizeof(TestVertex) == 72U);

struct ShaderVisibleBuffer {
  std::shared_ptr<Buffer> buffer {};
  ShaderVisibleIndex slot { oxygen::kInvalidShaderVisibleIndex };
};

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

  template <typename T>
  auto CreateStructuredSrvBuffer(std::span<const T> elements,
    std::string_view debug_name) -> ShaderVisibleBuffer
  {
    CHECK_F(
      !elements.empty(), "Structured SRV buffer requires at least one element");

    auto buffer = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(elements.size_bytes()),
      .usage = BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create buffer `{}`", debug_name);

    UploadBufferBytes(
      buffer, elements.data(), elements.size_bytes(), debug_name);

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      oxygen::graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate structured SRV for `{}`",
      debug_name);

    const auto slot = allocator.GetShaderVisibleIndex(handle);
    const oxygen::graphics::BufferViewDescription view_desc {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
      .range
      = BufferRange { 0U, static_cast<std::uint64_t>(elements.size_bytes()) },
      .stride = static_cast<std::uint32_t>(sizeof(T)),
    };

    auto view = Backend().GetResourceRegistry().RegisterView(
      *buffer, std::move(handle), view_desc);
    CHECK_F(view->IsValid(), "Failed to register structured SRV for `{}`",
      debug_name);

    return ShaderVisibleBuffer { .buffer = std::move(buffer), .slot = slot };
  }

  auto CreateUIntIndexBuffer(std::span<const std::uint32_t> indices,
    std::string_view debug_name) -> ShaderVisibleBuffer
  {
    CHECK_F(!indices.empty(), "Index buffer requires at least one element");

    auto buffer = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(indices.size_bytes()),
      .usage = BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create buffer `{}`", debug_name);

    UploadBufferBytes(buffer, indices.data(), indices.size_bytes(), debug_name);

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle = allocator.Allocate(ResourceViewType::kRawBuffer_SRV,
      oxygen::graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(
      handle.IsValid(), "Failed to allocate typed SRV for `{}`", debug_name);

    const auto slot = allocator.GetShaderVisibleIndex(handle);
    const oxygen::graphics::BufferViewDescription view_desc {
      .view_type = ResourceViewType::kRawBuffer_SRV,
      .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
      .format = Format::kR32UInt,
      .range
      = BufferRange { 0U, static_cast<std::uint64_t>(indices.size_bytes()) },
    };

    auto view = Backend().GetResourceRegistry().RegisterView(
      *buffer, std::move(handle), view_desc);
    CHECK_F(
      view->IsValid(), "Failed to register typed SRV for `{}`", debug_name);

    return ShaderVisibleBuffer { .buffer = std::move(buffer), .slot = slot };
  }

  auto ClearShadowSlice(const std::shared_ptr<const Texture>& shadow_texture,
    const std::uint32_t array_slice, const float depth_value,
    std::string_view debug_name) -> void
  {
    auto texture = std::const_pointer_cast<Texture>(shadow_texture);
    CHECK_NOTNULL_F(texture.get(), "Cannot clear a null shadow texture");
    CHECK_F(Backend().GetResourceRegistry().Contains(*texture),
      "Shadow pool texture must be registered before test DSV creation");

    auto& allocator
      = static_cast<oxygen::Graphics&>(Backend()).GetDescriptorAllocator();
    auto handle = allocator.Allocate(ResourceViewType::kTexture_DSV,
      oxygen::graphics::DescriptorVisibility::kCpuOnly);
    CHECK_F(handle.IsValid(), "Failed to allocate DSV for `{}`", debug_name);

    const auto dsv_desc = oxygen::graphics::TextureViewDescription {
      .view_type = ResourceViewType::kTexture_DSV,
      .visibility = oxygen::graphics::DescriptorVisibility::kCpuOnly,
      .format = texture->GetDescriptor().format,
      .dimension = texture->GetDescriptor().texture_type,
      .sub_resources = {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = array_slice,
        .num_array_slices = 1U,
      },
      .is_read_only_dsv = false,
    };
    auto dsv = Backend().GetResourceRegistry().RegisterView(
      *texture, std::move(handle), dsv_desc);
    CHECK_F(dsv->IsValid(), "Failed to register DSV for `{}`", debug_name);

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

  auto ReadDepthTexel(const std::shared_ptr<const Texture>& shadow_texture,
    const std::uint32_t array_slice, const std::uint32_t x,
    const std::uint32_t y, std::string_view debug_name) -> float
  {
    auto texture = std::const_pointer_cast<Texture>(shadow_texture);
    CHECK_NOTNULL_F(texture.get(), "Cannot read a null shadow texture");

    auto readback = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kReadbackRowPitch,
      .usage = BufferUsage::kNone,
      .memory = oxygen::graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name) + ".Readback",
    });
    CHECK_NOTNULL_F(readback.get(), "Failed to create readback buffer");

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Probe");
      CHECK_NOTNULL_F(recorder.get(), "Failed to acquire probe recorder");
      EnsureTracked(*recorder, texture, ResourceStates::kShaderResource);
      EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);
      recorder->RequireResourceState(*texture, ResourceStates::kCopySource);
      recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyTextureToBuffer(*readback, *texture,
        oxygen::graphics::TextureBufferCopyRegion {
          .buffer_offset = oxygen::OffsetBytes { 0U },
          .buffer_row_pitch = oxygen::SizeBytes { kReadbackRowPitch },
          .texture_slice = {
            .x = x,
            .y = y,
            .z = 0U,
            .width = 1U,
            .height = 1U,
            .depth = 1U,
            .mip_level = 0U,
            .array_slice = array_slice,
          },
        });
    }
    WaitForQueueIdle();

    float value = 0.0F;
    const auto* mapped
      = static_cast<const std::byte*>(readback->Map(0U, kReadbackRowPitch));
    CHECK_NOTNULL_F(mapped, "Failed to map readback buffer");
    std::memcpy(&value, mapped, sizeof(value));
    readback->UnMap();
    return value;
  }

  [[nodiscard]] static auto MakeBaseViewConstants(
    const ShaderVisibleIndex view_frame_slot, const Slot frame_slot,
    const SequenceNumber frame_sequence) -> ViewConstants::GpuData
  {
    auto base_view_constants = ViewConstants {};
    base_view_constants.SetTimeSeconds(0.0F, ViewConstants::kRenderer)
      .SetFrameSlot(frame_slot, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(frame_sequence, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        BindlessViewFrameBindingsSlot(view_frame_slot),
        ViewConstants::kRenderer);
    return base_view_constants.GetSnapshot();
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

NOLINT_TEST_F(VsmShadowRasterizerPassGpuTest,
  ExecuteRasterizesOpaqueShadowCasterIntoTargetPhysicalPageOnly)
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
      .position = { -0.8F, -0.8F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 0.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.8F, -0.8F, 0.25F },
      .normal = { 0.0F, 0.0F, 1.0F },
      .texcoord = { 1.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 0.0F, 1.0F, 0.0F, 1.0F },
    },
    TestVertex {
      .position = { 0.0F, 0.8F, 0.25F },
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
  auto world_allocation = world_buffer.Allocate(1U);
  ASSERT_TRUE(world_allocation.has_value());
  ASSERT_TRUE(world_allocation->IsValid(kFrameSequence));
  const auto world_matrix = glm::mat4 { 1.0F };
  std::memcpy(world_allocation->mapped_ptr, glm::value_ptr(world_matrix),
    sizeof(world_matrix));

  auto draw_metadata_buffer = TransientStructuredBuffer(
    oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    renderer->GetStagingProvider(), sizeof(DrawMetadata),
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
    "phase-f-rasterizer-draw.draws");
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
    },
  };
  std::memcpy(
    draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

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

  auto world_matrix_floats = std::array<float, 16> {};
  std::memcpy(world_matrix_floats.data(), glm::value_ptr(world_matrix),
    sizeof(world_matrix_floats));

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
  prepared_frame.partitions = std::span(partitions);
  prepared_frame.bindless_worlds_slot = world_allocation->srv;
  prepared_frame.bindless_draw_metadata_slot = draw_allocation->srv;

  pass.SetInput(VsmShadowRasterizerPassInput {
    .frame = MakeFrame(),
    .physical_pool = physical_pool,
    .projections = { MakeProjection() },
    .base_view_constants
    = MakeBaseViewConstants(view_frame_slot, kFrameSlot, kFrameSequence),
  });

  ClearShadowSlice(
    physical_pool.shadow_texture, 0U, 1.0F, "phase-f-rasterizer-draw.clear");

  offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  {
    auto recorder = AcquireRecorder("phase-f-rasterizer-draw");
    ASSERT_NE(recorder, nullptr);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const float target_depth = ReadDepthTexel(physical_pool.shadow_texture, 0U,
    448U, 64U, "phase-f-rasterizer-draw.target");
  const float neighbor_depth = ReadDepthTexel(physical_pool.shadow_texture, 0U,
    320U, 64U, "phase-f-rasterizer-draw.neighbor");

  EXPECT_NEAR(target_depth, 0.25F, 1.0e-4F);
  EXPECT_FLOAT_EQ(neighbor_depth, 1.0F);
  EXPECT_EQ(pass.GetPreparedPageCount(), 1U);
  EXPECT_EQ(render_context.GetPass<VsmShadowRasterizerPass>(), &pass);
}

} // namespace
