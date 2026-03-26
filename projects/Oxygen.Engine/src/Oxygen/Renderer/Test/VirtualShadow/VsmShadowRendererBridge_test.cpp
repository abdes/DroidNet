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
#include <string_view>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

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
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmInvalidationPass.h>
#include <Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/Types/DirectionalShadowCandidate.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include "VirtualShadowGpuTestFixtures.h"

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

namespace {

using oxygen::Format;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::NdcDepthRange;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::ShaderVisibleIndex;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::BindlessViewFrameBindingsSlot;
using oxygen::engine::DepthPrePass;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::Renderer;
using oxygen::engine::ViewConstants;
using oxygen::engine::sceneprep::RenderItemData;
using oxygen::engine::sceneprep::TransformHandle;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureUploadRegion;
using oxygen::renderer::LightManager;
using oxygen::renderer::vsm::kVsmInvalidLightIndex;
using oxygen::renderer::vsm::TryGetPageTableEntryIndex;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmProjectionData;
using oxygen::renderer::vsm::VsmProjectionLightType;
using oxygen::renderer::vsm::VsmShadowRenderer;
using oxygen::renderer::vsm::testing::VsmCacheManagerGpuTestBase;
using oxygen::scene::DirectionalLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

constexpr std::uint32_t kTextureUploadRowPitch = 256U;
constexpr ViewId kTestViewId { 91U };

auto EncodeFloatTexel(const float value) -> std::vector<std::byte>
{
  auto bytes = std::vector<std::byte>(kTextureUploadRowPitch, std::byte { 0 });
  std::memcpy(bytes.data(), &value, sizeof(value));
  return bytes;
}

class VsmShadowRendererBridgeGpuTest : public VsmCacheManagerGpuTestBase {
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

  [[nodiscard]] static auto ComputeDepthForWorldPoint(
    const ResolvedView& resolved_view, const glm::vec3 world_position) -> float
  {
    const auto clip = resolved_view.ProjectionMatrix()
      * resolved_view.ViewMatrix() * glm::vec4(world_position, 1.0F);
    return clip.z / clip.w;
  }

  auto UploadSingleChannelTexture(
    const float value, std::string_view debug_name) -> std::shared_ptr<Texture>
  {
    auto texture_desc = TextureDesc {};
    texture_desc.width = 1U;
    texture_desc.height = 1U;
    texture_desc.format = Format::kR32Float;
    texture_desc.texture_type = TextureType::kTexture2D;
    texture_desc.is_shader_resource = true;
    texture_desc.debug_name = std::string(debug_name);

    auto texture = CreateRegisteredTexture(texture_desc);
    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kTextureUploadRowPitch,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + ".Upload",
    });
    const auto upload_bytes = EncodeFloatTexel(value);
    upload->Update(upload_bytes.data(), upload_bytes.size(), 0U);

    const auto upload_region = TextureUploadRegion {
      .buffer_offset = 0U,
      .buffer_row_pitch = kTextureUploadRowPitch,
      .buffer_slice_pitch = kTextureUploadRowPitch,
      .dst_slice = {
        .x = 0U,
        .y = 0U,
        .z = 0U,
        .width = 1U,
        .height = 1U,
        .depth = 1U,
        .mip_level = 0U,
        .array_slice = 0U,
      },
    };

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + ".Init");
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload, upload_region, *texture);
      recorder->RequireResourceStateFinal(*texture, ResourceStates::kCommon);
    }
    WaitForQueueIdle();
    return texture;
  }

  [[nodiscard]] static auto MakeLocalProjectionRecord(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame,
    const ResolvedView& resolved_view) -> VsmPageRequestProjection
  {
    const auto& layout = frame.local_light_layouts[0];
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = resolved_view.ViewMatrix(),
        .projection_matrix = resolved_view.ProjectionMatrix(),
        .view_origin_ws_pad = glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F },
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
      .light_index = kVsmInvalidLightIndex,
    };
  }

  [[nodiscard]] static auto MakeDirectionalProjectionRecord(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame,
    const ResolvedView& resolved_view) -> VsmPageRequestProjection
  {
    const auto& layout = frame.directional_layouts[0];
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = resolved_view.ViewMatrix(),
        .projection_matrix = resolved_view.ProjectionMatrix(),
        .view_origin_ws_pad = glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F },
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

  [[nodiscard]] static auto MakeViewConstants(const ResolvedView& resolved_view,
    const SequenceNumber sequence, const Slot slot) -> ViewConstants
  {
    auto view_constants = ViewConstants {};
    view_constants.SetViewMatrix(resolved_view.ViewMatrix())
      .SetProjectionMatrix(resolved_view.ProjectionMatrix())
      .SetCameraPosition(resolved_view.CameraPosition())
      .SetFrameSlot(slot, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(sequence, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        BindlessViewFrameBindingsSlot { ShaderVisibleIndex { 1U } },
        ViewConstants::kRenderer)
      .SetTimeSeconds(0.0F, ViewConstants::kRenderer);
    return view_constants;
  }

  static auto UpdateTransforms(Scene& scene, SceneNode& node) -> void
  {
    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().UpdateTransforms(scene);
  }

  [[nodiscard]] static auto MakeCurrentSeam(VsmShadowRenderer& vsm_renderer)
    -> oxygen::renderer::vsm::VsmCacheManagerSeam
  {
    const auto& current_frame
      = vsm_renderer.GetVirtualAddressSpace().DescribeFrame();
    const auto* previous_frame
      = vsm_renderer.GetCacheManager().GetPreviousFrame();
    return oxygen::renderer::vsm::VsmCacheManagerSeam {
      .physical_pool
      = vsm_renderer.GetPhysicalPagePoolManager().GetShadowPoolSnapshot(),
      .hzb_pool
      = vsm_renderer.GetPhysicalPagePoolManager().GetHzbPoolSnapshot(),
      .current_frame = current_frame,
      .previous_to_current_remap = previous_frame != nullptr
        ? vsm_renderer.GetVirtualAddressSpace().BuildRemapTable(
            previous_frame->virtual_frame)
        : oxygen::renderer::vsm::VsmVirtualRemapTable {},
    };
  }

  static auto CollectDirectionalLight(LightManager& lights, SceneNode& node)
    -> oxygen::engine::DirectionalShadowCandidate
  {
    auto impl = node.GetImpl();
    if (!impl.has_value()) {
      ADD_FAILURE() << "directional light node has no implementation";
      return {};
    }
    lights.CollectFromNode(node.GetHandle(), impl->get());
    const auto candidates = lights.GetDirectionalShadowCandidates();
    EXPECT_EQ(candidates.size(), 1U);
    return candidates.empty() ? oxygen::engine::DirectionalShadowCandidate {}
                              : candidates.front();
  }

  [[nodiscard]] static auto MakeDirectionalPageRequest(
    const oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame& frame)
    -> oxygen::renderer::vsm::VsmPageRequest
  {
    const auto& layout = frame.directional_layouts[0];
    return oxygen::renderer::vsm::VsmPageRequest {
      .map_id = layout.first_id,
      .page = { .level = 0U, .page_x = 0U, .page_y = 0U },
      .flags = oxygen::renderer::vsm::VsmPageRequestFlags::kRequired,
    };
  }

  auto ExecutePreparedViewShell(VsmShadowRenderer& renderer,
    const oxygen::engine::RenderContext& render_context,
    const oxygen::observer_ptr<const Texture> scene_depth_texture = {}) -> void
  {
    EventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      auto recorder = Backend().AcquireCommandRecorder(
        Backend().QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
        "phase-ka5-live-shell");
      EXPECT_NE(recorder.get(), nullptr);
      if (recorder == nullptr) {
        co_return;
      }
      co_await renderer.ExecutePreparedViewShell(
        render_context, *recorder, scene_depth_texture);
    });
    WaitForQueueIdle();
  }

  auto CreateMetadataSeedBuffer(
    const std::span<const oxygen::renderer::vsm::VsmPhysicalPageMeta> metadata,
    std::string_view debug_name)
    -> std::shared_ptr<const oxygen::graphics::Buffer>
  {
    auto buffer = CreateRegisteredBuffer(oxygen::graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(metadata.size())
        * sizeof(oxygen::renderer::vsm::VsmPhysicalPageMeta),
      .usage = oxygen::graphics::BufferUsage::kStorage,
      .memory = oxygen::graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create metadata seed buffer");
    UploadBufferBytes(buffer, metadata.data(),
      metadata.size() * sizeof(oxygen::renderer::vsm::VsmPhysicalPageMeta),
      debug_name);
    return buffer;
  }
};

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  PageRequestReadbackBridgeCommitsAllocationFrameFromGpuRequests)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture = UploadSingleChannelTexture(depth, "phase-ka4.depth");

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  auto& pool_manager = vsm_renderer.GetPhysicalPagePoolManager();
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase-ka4-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase-ka4-hzb")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 1ULL, 11U, "phase-ka4-frame");
  const auto projection_record
    = MakeLocalProjectionRecord(seam.current_frame, resolved_view);
  const auto projection_records = std::array { projection_record };

  auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
    DepthPrePass::Config { .depth_texture = depth_texture }));
  auto prepared_frame = PreparedSceneFrame {};
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  auto bridge_committed_requests = false;
  EventLoop loop;
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    bridge_committed_requests
      = co_await vsm_renderer.ExecutePageRequestReadbackBridge(
        render_context, seam, projection_records);
  });
  WaitForQueueIdle();

  EXPECT_TRUE(bridge_committed_requests);

  const auto* committed_frame
    = vsm_renderer.GetCacheManager().GetCurrentFrame();
  ASSERT_NE(committed_frame, nullptr);
  ASSERT_TRUE(committed_frame->is_ready);
  ASSERT_EQ(committed_frame->snapshot.projection_records.size(), 1U);
  EXPECT_EQ(committed_frame->snapshot.projection_records[0], projection_record);
  ASSERT_EQ(committed_frame->plan.decisions.size(), 1U);
  EXPECT_EQ(committed_frame->plan.allocated_page_count, 1U);
  EXPECT_EQ(committed_frame->plan.decisions[0].action,
    VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(committed_frame->plan.decisions[0].request.map_id,
    seam.current_frame.local_light_layouts[0].id);
  EXPECT_EQ(committed_frame->plan.decisions[0].request.page.level, 0U);
  EXPECT_EQ(committed_frame->plan.decisions[0].request.page.page_x, 0U);
  EXPECT_EQ(committed_frame->plan.decisions[0].request.page.page_y, 0U);

  const auto page_table_entry
    = TryGetPageTableEntryIndex(seam.current_frame.local_light_layouts[0], {});
  ASSERT_TRUE(page_table_entry.has_value());
  ASSERT_LT(*page_table_entry, committed_frame->snapshot.page_table.size());
  ASSERT_TRUE(
    committed_frame->snapshot.page_table[*page_table_entry].is_mapped);
  EXPECT_EQ(
    committed_frame->snapshot.page_table[*page_table_entry].physical_page,
    committed_frame->plan.decisions[0].current_physical_page);
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  ExecutePreparedViewShellRunsStageSixThroughProjectionAndExtractsReadyFrame)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka5-live-shell.depth");
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("phase-ka5-scene", 32);
  const auto sun_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", sun_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));
  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& light = sun_impl->get().GetComponent<DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  UpdateTransforms(*scene, sun_node);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  auto frame_config = Renderer::OffscreenFrameConfig {
    .frame_slot = Slot { 0U },
    .frame_sequence = SequenceNumber { 1U },
    .scene = oxygen::observer_ptr<Scene> { scene.get() },
  };
  auto offscreen = renderer->BeginOffscreenFrame(frame_config);
  auto prepared_frame = PreparedSceneFrame {};
  offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

  auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
    DepthPrePass::Config { .depth_texture = depth_texture }));
  auto& render_context = offscreen.GetRenderContext();
  render_context.RegisterPass(&depth_pass);

  auto lights = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
    oxygen::observer_ptr { &renderer->GetStagingProvider() },
    oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
  lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    SequenceNumber { 1U }, Slot { 0U });
  static_cast<void>(CollectDirectionalLight(lights, sun_node));

  vsm_renderer.OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    SequenceNumber { 1U }, Slot { 0U });
  static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
    MakeViewConstants(resolved_view, SequenceNumber { 1U }, Slot { 0U }),
    lights, oxygen::observer_ptr<Scene> { scene.get() }, 1024.0F));
  ExecutePreparedViewShell(vsm_renderer, render_context,
    oxygen::observer_ptr<const Texture> { depth_texture.get() });

  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
    VsmCacheBuildState::kIdle);

  const auto* extracted_frame
    = vsm_renderer.GetCacheManager().GetPreviousFrame();
  ASSERT_NE(extracted_frame, nullptr);
  EXPECT_FALSE(extracted_frame->projection_records.empty());
  EXPECT_TRUE(extracted_frame->is_hzb_data_available);

  const auto projection_output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  EXPECT_TRUE(projection_output.available);
  EXPECT_NE(
    projection_output.shadow_mask_srv_index, kInvalidShaderVisibleIndex);
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  ExecutePreparedViewShellReusesProjectionShadowMaskAcrossConsecutiveFrames)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka6-live-shell-reuse.depth");
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("phase-ka6-scene", 32);
  const auto sun_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", sun_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));
  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& light = sun_impl->get().GetComponent<DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  UpdateTransforms(*scene, sun_node);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  auto run_frame = [&](const SequenceNumber sequence) {
    auto frame_config = Renderer::OffscreenFrameConfig {
      .frame_slot = Slot { 0U },
      .frame_sequence = sequence,
      .scene = oxygen::observer_ptr<Scene> { scene.get() },
    };
    auto offscreen = renderer->BeginOffscreenFrame(frame_config);
    auto prepared_frame = PreparedSceneFrame {};
    offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

    auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
      DepthPrePass::Config { .depth_texture = depth_texture }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);

    auto lights
      = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
        oxygen::observer_ptr { &renderer->GetStagingProvider() },
        oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
    lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
      sequence, Slot { 0U });
    static_cast<void>(CollectDirectionalLight(lights, sun_node));

    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(), sequence,
      Slot { 0U });
    static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
      MakeViewConstants(resolved_view, sequence, Slot { 0U }), lights,
      oxygen::observer_ptr<Scene> { scene.get() }, 1024.0F));
    ExecutePreparedViewShell(vsm_renderer, render_context,
      oxygen::observer_ptr<const Texture> { depth_texture.get() });
  };

  run_frame(SequenceNumber { 1U });
  const auto first_output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(first_output.available);
  ASSERT_NE(first_output.shadow_mask_texture, nullptr);
  const auto* first_texture = first_output.shadow_mask_texture.get();

  run_frame(SequenceNumber { 2U });
  const auto second_output
    = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(second_output.available);
  ASSERT_NE(second_output.shadow_mask_texture, nullptr);
  EXPECT_EQ(second_output.shadow_mask_texture.get(), first_texture);
  EXPECT_NE(second_output.shadow_mask_srv_index, kInvalidShaderVisibleIndex);
  EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
    VsmCacheBuildState::kIdle);
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  ExecutePreparedViewShellRemainsStableAcrossManyConsecutiveFrames)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka6-live-shell-stability.depth");
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto scene = std::make_shared<Scene>("phase-ka6-stability-scene", 32);
  const auto sun_flags
    = SceneNode::Flags {}
        .SetFlag(oxygen::scene::SceneNodeFlags::kVisible,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true))
        .SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
          oxygen::scene::SceneFlag {}.SetEffectiveValueBit(true));
  auto sun_node = scene->CreateNode("sun", sun_flags);
  ASSERT_TRUE(sun_node.IsValid());
  ASSERT_TRUE(sun_node.AttachLight(std::make_unique<DirectionalLight>()));
  auto sun_impl = sun_node.GetImpl();
  ASSERT_TRUE(sun_impl.has_value());
  auto& light = sun_impl->get().GetComponent<DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  UpdateTransforms(*scene, sun_node);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);

  const Texture* first_shadow_mask_texture = nullptr;
  for (std::uint32_t frame_index = 1U; frame_index <= 16U; ++frame_index) {
    const auto sequence = SequenceNumber { frame_index };
    auto frame_config = Renderer::OffscreenFrameConfig {
      .frame_slot = Slot { 0U },
      .frame_sequence = sequence,
      .scene = oxygen::observer_ptr<Scene> { scene.get() },
    };
    auto offscreen = renderer->BeginOffscreenFrame(frame_config);
    auto prepared_frame = PreparedSceneFrame {};
    offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

    auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
      DepthPrePass::Config { .depth_texture = depth_texture }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);

    auto lights
      = LightManager(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
        oxygen::observer_ptr { &renderer->GetStagingProvider() },
        oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() });
    lights.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
      sequence, Slot { 0U });
    static_cast<void>(CollectDirectionalLight(lights, sun_node));

    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(), sequence,
      Slot { 0U });
    static_cast<void>(vsm_renderer.PrepareView(kTestViewId,
      MakeViewConstants(resolved_view, sequence, Slot { 0U }), lights,
      oxygen::observer_ptr<Scene> { scene.get() }, 1024.0F));
    ExecutePreparedViewShell(vsm_renderer, render_context,
      oxygen::observer_ptr<const Texture> { depth_texture.get() });

    const auto projection_output
      = vsm_renderer.GetProjectionPass()->GetCurrentOutput(kTestViewId);
    ASSERT_TRUE(projection_output.available);
    ASSERT_NE(projection_output.shadow_mask_texture, nullptr);
    EXPECT_NE(
      projection_output.shadow_mask_srv_index, kInvalidShaderVisibleIndex);
    if (frame_index == 1U) {
      first_shadow_mask_texture = projection_output.shadow_mask_texture.get();
      ASSERT_NE(first_shadow_mask_texture, nullptr);
    } else {
      EXPECT_EQ(
        projection_output.shadow_mask_texture.get(), first_shadow_mask_texture);
    }
    EXPECT_EQ(vsm_renderer.GetCacheManager().DescribeBuildState(),
      VsmCacheBuildState::kIdle);
  }
}

NOLINT_TEST_F(VsmShadowRendererBridgeGpuTest,
  PageRequestReadbackBridgePublishesInvalidationMetadataSeedIntoCommittedCurrentFrame)
{
  const auto resolved_view = MakeResolvedView();
  const auto world_position = glm::vec3 { 0.0F, 0.0F, -5.0F };
  const auto depth = ComputeDepthForWorldPoint(resolved_view, world_position);
  auto depth_texture
    = UploadSingleChannelTexture(depth, "phase-ka5-seed-bridge.depth");

  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto vsm_renderer
    = VsmShadowRenderer(oxygen::observer_ptr<oxygen::Graphics>(&Backend()),
      oxygen::observer_ptr { &renderer->GetStagingProvider() },
      oxygen::observer_ptr { &renderer->GetInlineTransfersCoordinator() },
      oxygen::ShadowQualityTier::kHigh);
  {
    auto frame_config = Renderer::OffscreenFrameConfig {
      .frame_slot = Slot { 0U },
      .frame_sequence = SequenceNumber { 1U },
    };
    auto offscreen = renderer->BeginOffscreenFrame(frame_config);
    auto prepared_frame = PreparedSceneFrame {};
    offscreen.SetCurrentView(kTestViewId, resolved_view, prepared_frame);

    auto depth_pass = DepthPrePass(std::make_shared<DepthPrePass::Config>(
      DepthPrePass::Config { .depth_texture = depth_texture }));
    auto& render_context = offscreen.GetRenderContext();
    render_context.RegisterPass(&depth_pass);
    vsm_renderer.OnFrameStart(
      oxygen::renderer::internal::RendererTagFactory::Get(),
      SequenceNumber { 1U }, Slot { 0U });

    auto& pool_manager = vsm_renderer.GetPhysicalPagePoolManager();
    ASSERT_EQ(pool_manager.EnsureShadowPool(
                MakeShadowPoolConfig("phase-ka5-seed-shadow")),
      VsmPhysicalPoolChangeResult::kCreated);
    ASSERT_EQ(
      pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase-ka5-seed-hzb")),
      oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

    const auto seam = MakeSeam(pool_manager, 1ULL, 21U, "phase-ka5-seed-frame");
    const auto current_projection_records = std::array {
      MakeLocalProjectionRecord(seam.current_frame, resolved_view)
    };
    auto metadata_seed
      = std::vector<oxygen::renderer::vsm::VsmPhysicalPageMeta>(
        seam.physical_pool.tile_capacity);
    auto physical_page_meta_seed_buffer
      = CreateMetadataSeedBuffer(metadata_seed, "phase-ka5-seed-bridge.meta");

    auto bridge_committed_requests = false;
    EventLoop loop;
    oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
      bridge_committed_requests
        = co_await vsm_renderer.ExecutePageRequestReadbackBridge(render_context,
          seam, current_projection_records, physical_page_meta_seed_buffer);
    });
    WaitForQueueIdle();
    EXPECT_TRUE(bridge_committed_requests);

    const auto* committed_frame
      = vsm_renderer.GetCacheManager().GetCurrentFrame();
    ASSERT_NE(committed_frame, nullptr);
    ASSERT_TRUE(committed_frame->is_ready);
    ASSERT_NE(committed_frame->physical_page_meta_seed_buffer, nullptr);
    EXPECT_EQ(committed_frame->physical_page_meta_seed_buffer.get(),
      physical_page_meta_seed_buffer.get());
  }
}

} // namespace
