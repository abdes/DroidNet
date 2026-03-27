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
#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmStaticDynamicMergePass.h>
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
using oxygen::engine::VsmShadowRasterizerPass;
using oxygen::engine::VsmShadowRasterizerPassConfig;
using oxygen::engine::VsmShadowRasterizerPassInput;
using oxygen::engine::VsmStaticDynamicMergePass;
using oxygen::engine::VsmStaticDynamicMergePassConfig;
using oxygen::engine::VsmStaticDynamicMergePassInput;
using oxygen::engine::internal::PerViewStructuredPublisher;
using oxygen::engine::upload::TransientStructuredBuffer;
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
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPoolConfig;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::VsmRenderedPageDirtyFlagBits;
using oxygen::renderer::vsm::testing::StageMeshVertex;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

constexpr ViewId kTestViewId { 13U };
class VsmStaticDynamicMergePassGpuTest : public VsmStageGpuHarness {
protected:
  auto ExecuteRasterPass(const VsmShadowRasterizerPassInput& input,
    PreparedSceneFrame& prepared_frame, const Slot frame_slot,
    const SequenceNumber frame_sequence, std::string_view debug_name) -> void
  {
    auto renderer = MakeRenderer();
    CHECK_NOTNULL_F(renderer.get());

    auto pass = VsmShadowRasterizerPass(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      std::make_shared<VsmShadowRasterizerPassConfig>(
        VsmShadowRasterizerPassConfig {
          .debug_name = std::string(debug_name),
        }));
    pass.SetInput(input);

    auto offscreen = renderer->BeginOffscreenFrame(
      { .frame_slot = frame_slot, .frame_sequence = frame_sequence });
    offscreen.SetCurrentView(kTestViewId, MakeResolvedView(), prepared_frame);
    auto& render_context = offscreen.GetRenderContext();

    {
      auto recorder = AcquireRecorder(std::string(debug_name));
      CHECK_NOTNULL_F(recorder.get());
      RunPass(pass, render_context, *recorder);
    }
    WaitForQueueIdle();
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

NOLINT_TEST_F(VsmStaticDynamicMergePassGpuTest,
  MergePassCompositesRasterizedStaticSliceIntoRasterizedDynamicPageAndLeavesCleanControlPageUntouched)
{
  auto pool_manager = VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase-g-rasterized-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  const auto pool = pool_manager.GetShadowPoolSnapshot();
  ASSERT_TRUE(pool.is_available);
  ASSERT_NE(pool.shadow_texture, nullptr);
  ASSERT_EQ(pool.slice_count, 2U);

  const auto virtual_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase-g-rasterized");
  const auto& layout = virtual_frame.local_light_layouts[0];
  const auto projection = oxygen::renderer::vsm::VsmPageRequestProjection {
    .projection = oxygen::renderer::vsm::VsmProjectionData {
      .view_matrix = glm::mat4 { 1.0F },
      .projection_matrix = glm::mat4 { 1.0F },
      .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 1.0F },
      .clipmap_corner_offset = { 0, 0 },
      .clipmap_level = 0U,
      .light_type = static_cast<std::uint32_t>(
        oxygen::renderer::vsm::VsmProjectionLightType::kLocal),
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

  const auto page0 = TryConvertToCoord(
    { .value = 0U }, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
  const auto page1 = TryConvertToCoord(
    { .value = 1U }, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
  ASSERT_TRUE(page0.has_value());
  ASSERT_TRUE(page1.has_value());

  constexpr auto kFrameSlot = Slot { 0U };

  auto run_single_triangle_raster = [&](VsmPageAllocationFrame& frame,
                                      const float depth,
                                      const bool static_shadow_caster,
                                      const SequenceNumber frame_sequence,
                                      std::string_view debug_name) {
    auto renderer = MakeRenderer();
    ASSERT_NE(renderer, nullptr);

    std::array<StageMeshVertex, 3> vertices {
      StageMeshVertex {
        .position = { -0.15F, -0.15F, depth },
        .normal = { 0.0F, 0.0F, 1.0F },
        .texcoord = { 0.0F, 1.0F },
        .tangent = { 1.0F, 0.0F, 0.0F },
        .bitangent = { 0.0F, 1.0F, 0.0F },
        .color = { 1.0F, 0.0F, 0.0F, 1.0F },
      },
      StageMeshVertex {
        .position = { 0.15F, -0.15F, depth },
        .normal = { 0.0F, 0.0F, 1.0F },
        .texcoord = { 1.0F, 1.0F },
        .tangent = { 1.0F, 0.0F, 0.0F },
        .bitangent = { 0.0F, 1.0F, 0.0F },
        .color = { 0.0F, 1.0F, 0.0F, 1.0F },
      },
      StageMeshVertex {
        .position = { 0.0F, 0.15F, depth },
        .normal = { 0.0F, 0.0F, 1.0F },
        .texcoord = { 0.5F, 0.0F },
        .tangent = { 1.0F, 0.0F, 0.0F },
        .bitangent = { 0.0F, 1.0F, 0.0F },
        .color = { 0.0F, 0.0F, 1.0F, 1.0F },
      },
    };
    constexpr std::array<std::uint32_t, 3> kIndices { 0U, 1U, 2U };

    auto vertex_buffer = CreateStructuredSrvBuffer<StageMeshVertex>(
      vertices, std::string(debug_name) + ".vertices");
    auto index_buffer
      = CreateUIntIndexBuffer(kIndices, std::string(debug_name) + ".indices");

    auto world_buffer = TransientStructuredBuffer(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      renderer->GetStagingProvider(), sizeof(glm::mat4),
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      std::string(debug_name) + ".worlds");
    world_buffer.OnFrameStart(frame_sequence, kFrameSlot);
    auto world_allocation = world_buffer.Allocate(1U);
    ASSERT_TRUE(world_allocation.has_value());
    ASSERT_TRUE(world_allocation->IsValid(frame_sequence));
    const auto world_matrix = glm::mat4 { 1.0F };
    std::memcpy(
      world_allocation->mapped_ptr, &world_matrix, sizeof(world_matrix));

    auto draw_metadata_buffer = TransientStructuredBuffer(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      renderer->GetStagingProvider(), sizeof(DrawMetadata),
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      std::string(debug_name) + ".draws");
    draw_metadata_buffer.OnFrameStart(frame_sequence, kFrameSlot);
    auto draw_allocation = draw_metadata_buffer.Allocate(1U);
    ASSERT_TRUE(draw_allocation.has_value());
    ASSERT_TRUE(draw_allocation->IsValid(frame_sequence));

    auto shadow_caster_mask = PassMask {};
    shadow_caster_mask.Set(PassMaskBit::kOpaque);
    shadow_caster_mask.Set(PassMaskBit::kShadowCaster);

    const auto primitive_flags = static_shadow_caster
      ? static_cast<std::uint32_t>(
          oxygen::engine::DrawPrimitiveFlagBits::kStaticShadowCaster)
      : 0U;
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
        .transform_generation
        = static_cast<std::uint32_t>(frame_sequence.get()),
        .submesh_index = 0U,
        .primitive_flags = primitive_flags,
      },
    };
    std::memcpy(
      draw_allocation->mapped_ptr, draw_records.data(), sizeof(draw_records));

    const std::array<glm::vec4, 1> draw_bounds {
      glm::vec4 { 0.0F, 0.0F, depth, 0.35F },
    };
    auto draw_bounds_buffer = CreateStructuredSrvBuffer<glm::vec4>(
      draw_bounds, std::string(debug_name) + ".bounds");

    auto draw_frame_publisher = PerViewStructuredPublisher<DrawFrameBindings>(
      oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      renderer->GetStagingProvider(),
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      std::string(debug_name) + ".DrawFrameBindings");
    draw_frame_publisher.OnFrameStart(frame_sequence, kFrameSlot);
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
      std::string(debug_name) + ".ViewFrameBindings");
    view_frame_publisher.OnFrameStart(frame_sequence, kFrameSlot);
    const auto view_frame_slot = view_frame_publisher.Publish(
      kTestViewId, ViewFrameBindings { .draw_frame_slot = draw_frame_slot });
    ASSERT_TRUE(view_frame_slot.IsValid());

    std::array<float, 16> world_matrix_floats {};
    std::memcpy(
      world_matrix_floats.data(), &world_matrix, sizeof(world_matrix));
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

    ExecuteRasterPass({ .frame = frame,
                        .physical_pool = pool,
                        .projections = { projection },
                        .base_view_constants = MakeBaseViewConstants(
                          view_frame_slot, kFrameSlot, frame_sequence) },
      prepared_frame, kFrameSlot, frame_sequence, debug_name);
  };

  auto make_frame = [&](const VsmPageRequest request,
                      std::string_view debug_name) -> VsmPageAllocationFrame {
    auto manager = VsmCacheManager(&Backend());
    manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
      VsmCacheManagerFrameConfig { .debug_name = std::string(debug_name) });
    manager.SetPageRequests({ &request, 1U });
    static_cast<void>(CommitFrame(manager));
    manager.PublishProjectionRecords(std::array { projection });
    return *manager.GetCurrentFrame();
  };

  auto static_frame = make_frame(
    VsmPageRequest {
      .map_id = layout.id,
      .page = {},
      .flags
      = VsmPageRequestFlags::kRequired | VsmPageRequestFlags::kStaticOnly,
    },
    "phase-g-rasterized-static");
  auto static_meta = std::vector<VsmPhysicalPageMeta>(pool.tile_capacity);
  AttachRasterOutputBuffers(
    static_frame, 900ULL, pool.tile_capacity, "phase-g-rasterized-static");
  static_meta[0].is_allocated = true;
  static_meta[0].static_invalidated = true;
  static_meta[0].view_uncached = true;
  UploadPhysicalMeta(static_frame.physical_page_meta_buffer, static_meta,
    "phase-g-rasterized-static.meta");
  ClearShadowSlice(
    pool.shadow_texture, 1U, 1.0F, "phase-g-rasterized.static-clear");
  run_single_triangle_raster(static_frame, 0.25F, true, SequenceNumber { 61U },
    "phase-g-rasterized.static");

  auto dynamic_frame
    = make_frame(VsmPageRequest { .map_id = layout.id, .page = {} },
      "phase-g-rasterized-dynamic");
  auto dynamic_meta = std::vector<VsmPhysicalPageMeta>(pool.tile_capacity);
  AttachRasterOutputBuffers(
    dynamic_frame, 901ULL, pool.tile_capacity, "phase-g-rasterized-dynamic");
  dynamic_meta[0].is_allocated = true;
  dynamic_meta[0].dynamic_invalidated = true;
  dynamic_meta[0].view_uncached = true;
  UploadPhysicalMeta(dynamic_frame.physical_page_meta_buffer, dynamic_meta,
    "phase-g-rasterized-dynamic.meta");
  ClearShadowSlice(
    pool.shadow_texture, 0U, 1.0F, "phase-g-rasterized.dynamic-clear");
  run_single_triangle_raster(dynamic_frame, 0.80F, false,
    SequenceNumber { 62U }, "phase-g-rasterized.dynamic");

  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels, page1->tile_x,
    page1->tile_y, 0U, 0.70F, "phase-g-rasterized.control.dynamic");
  SeedShadowPageValue(pool.shadow_texture, pool.page_size_texels, page1->tile_x,
    page1->tile_y, 1U, 0.20F, "phase-g-rasterized.control.static");

  ExecutePass({ .frame = dynamic_frame, .physical_pool = pool },
    "phase-g-rasterized.merge");

  const auto page0_sample_x
    = page0->tile_x * pool.page_size_texels + pool.page_size_texels / 2U;
  const auto page0_sample_y
    = page0->tile_y * pool.page_size_texels + pool.page_size_texels / 2U;
  const auto page1_sample_x
    = page1->tile_x * pool.page_size_texels + pool.page_size_texels / 2U;
  const auto page1_sample_y
    = page1->tile_y * pool.page_size_texels + pool.page_size_texels / 2U;

  EXPECT_NEAR(ReadShadowDepthTexel(pool.shadow_texture, page0_sample_x,
                page0_sample_y, 0U, "phase-g-rasterized.page0"),
    0.25F, 1.0e-4F);
  EXPECT_NEAR(ReadShadowDepthTexel(pool.shadow_texture, page1_sample_x,
                page1_sample_y, 0U, "phase-g-rasterized.page1"),
    0.70F, 1.0e-4F);
}

} // namespace
