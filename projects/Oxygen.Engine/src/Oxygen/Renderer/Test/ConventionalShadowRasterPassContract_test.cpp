//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowCasterCullingPass.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowRasterPass.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverAnalysisPass.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverMaskPass.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Types/ConventionalShadowIndirectDrawCommand.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Test/Fakes/FakeAsyncEngine.h>

namespace {

using oxygen::Graphics;
using oxygen::NdcDepthRange;
using oxygen::observer_ptr;
using oxygen::ResolvedView;
using oxygen::Scissors;
using oxygen::ShaderVisibleIndex;
using oxygen::TextureType;
using oxygen::View;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::engine::ConventionalShadowCasterCullingPass;
using oxygen::engine::ConventionalShadowCasterCullingPassConfig;
using oxygen::engine::ConventionalShadowRasterPass;
using oxygen::engine::ConventionalShadowReceiverAnalysisPass;
using oxygen::engine::ConventionalShadowReceiverAnalysisPassConfig;
using oxygen::engine::ConventionalShadowReceiverMaskPass;
using oxygen::engine::ConventionalShadowReceiverMaskPassConfig;
using oxygen::engine::DepthPrePass;
using oxygen::engine::DrawMetadata;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::RenderContext;
using oxygen::engine::Renderer;
using oxygen::engine::ScreenHzbBuildPass;
using oxygen::engine::ScreenHzbBuildPassConfig;
using oxygen::engine::ViewConstants;
using oxygen::engine::internal::BuildConventionalShadowDrawRecords;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;
using oxygen::renderer::LightManager;
using oxygen::scene::DirectionalLight;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scripting::test::FakeAsyncEngine;

constexpr ViewId kTestViewId { 31U };
constexpr Slot kFrameSlot { 0U };
constexpr SequenceNumber kFrameSequence { 11U };

template <typename T> struct UploadedStructuredBuffer {
  std::shared_ptr<Buffer> buffer {};
  ShaderVisibleIndex slot { oxygen::kInvalidShaderVisibleIndex };
};

auto RunPass(oxygen::engine::RenderPass& pass, const RenderContext& context,
  CommandRecorder& recorder) -> void
{
  oxygen::co::testing::TestEventLoop loop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    co_await pass.PrepareResources(context, recorder);
    co_await pass.Execute(context, recorder);
    co_return;
  });
}

[[nodiscard]] auto MakeResolvedView(
  const std::uint32_t width, const std::uint32_t height) -> ResolvedView
{
  const auto aspect_ratio = height == 0U
    ? 1.0F
    : static_cast<float>(width) / static_cast<float>(height);
  const auto view_matrix = glm::lookAtRH(glm::vec3 { 0.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 0.0F, -1.0F }, glm::vec3 { 0.0F, 1.0F, 0.0F });
  const auto projection_matrix
    = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
      glm::radians(90.0F), aspect_ratio, 0.1F, 100.0F);

  auto view_config = View {};
  view_config.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  view_config.scissor = Scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<std::int32_t>(width),
    .bottom = static_cast<std::int32_t>(height),
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

auto MakeShadowViewConstants(const ResolvedView& resolved_view,
  const Slot frame_slot, const SequenceNumber frame_sequence) -> ViewConstants
{
  auto view_constants = ViewConstants {};
  view_constants.SetFrameSlot(frame_slot, ViewConstants::kRenderer)
    .SetFrameSequenceNumber(frame_sequence, ViewConstants::kRenderer)
    .SetBindlessViewFrameBindingsSlot(
      oxygen::engine::BindlessViewFrameBindingsSlot {
        ShaderVisibleIndex { 1U } },
      ViewConstants::kRenderer)
    .SetTimeSeconds(0.0F, ViewConstants::kRenderer)
    .SetViewMatrix(resolved_view.ViewMatrix())
    .SetProjectionMatrix(resolved_view.ProjectionMatrix())
    .SetStableProjectionMatrix(resolved_view.StableProjectionMatrix())
    .SetCameraPosition(resolved_view.CameraPosition());
  return view_constants;
}

auto CreateShadowCastingDirectionalNode(Scene& scene) -> SceneNode
{
  const auto flags = SceneNode::Flags {}
                       .SetFlag(SceneNodeFlags::kVisible,
                         SceneFlag {}.SetEffectiveValueBit(true))
                       .SetFlag(SceneNodeFlags::kCastsShadows,
                         SceneFlag {}.SetEffectiveValueBit(true));

  auto node = scene.CreateNode("shadow-pass.contract.directional-light", flags);
  EXPECT_TRUE(node.IsValid());

  auto impl = node.GetImpl();
  EXPECT_TRUE(impl.has_value());
  if (impl.has_value()) {
    impl->get().AddComponent<DirectionalLight>();
    impl->get().GetComponent<DirectionalLight>().Common().casts_shadows = true;
    impl->get().UpdateTransforms(scene);
  }

  return node;
}

[[nodiscard]] auto MakeShadowCasterMask(const bool masked) -> PassMask
{
  auto mask = PassMask {};
  mask.Set(PassMaskBit::kShadowCaster);
  mask.Set(masked ? PassMaskBit::kMasked : PassMaskBit::kOpaque);
  return mask;
}

[[nodiscard]] auto MakeShadowCasterMask(
  const bool masked, const bool double_sided) -> PassMask
{
  auto mask = MakeShadowCasterMask(masked);
  if (double_sided) {
    mask.Set(PassMaskBit::kDoubleSided);
  }
  return mask;
}

[[nodiscard]] auto HasShaderDefine(
  const GraphicsPipelineDesc& desc, std::string_view name) -> bool
{
  const auto& shader = desc.PixelShader();
  if (!shader.has_value()) {
    return false;
  }
  return std::any_of(shader->defines.begin(), shader->defines.end(),
    [name](const auto& define) { return define.name == name; });
}

auto CreateRegisteredDepthTexture(oxygen::renderer::testing::FakeGraphics& gfx,
  const std::uint32_t width, const std::uint32_t height,
  std::string_view debug_name) -> std::shared_ptr<Texture>
{
  auto desc = oxygen::graphics::TextureDesc {};
  desc.width = width;
  desc.height = height;
  desc.format = oxygen::Format::kDepth32;
  desc.texture_type = TextureType::kTexture2D;
  desc.is_shader_resource = true;
  desc.is_render_target = true;
  desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = oxygen::graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = ResourceStates::kCommon;
  desc.debug_name = std::string(debug_name);

  auto texture = gfx.CreateTexture(desc);
  EXPECT_NE(texture, nullptr);
  if (texture != nullptr) {
    gfx.GetResourceRegistry().Register(texture);
  }
  return texture;
}

template <typename T>
auto UploadStructuredSrvBuffer(oxygen::renderer::testing::FakeGraphics& gfx,
  std::span<const T> elements, std::string_view debug_name)
  -> UploadedStructuredBuffer<T>
{
  const auto element_count = std::max<std::size_t>(elements.size(), 1U);
  auto buffer = gfx.CreateBuffer(BufferDesc {
    .size_bytes = static_cast<std::uint64_t>(element_count * sizeof(T)),
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = std::string(debug_name),
  });
  EXPECT_NE(buffer, nullptr);
  if (buffer == nullptr) {
    return {};
  }

  gfx.GetResourceRegistry().Register(buffer);
  if (!elements.empty()) {
    buffer->Update(elements.data(), elements.size_bytes(), 0U);
  }

  auto& allocator = gfx.GetDescriptorAllocator();
  auto handle = allocator.AllocateBindless(
    oxygen::bindless::generated::kGlobalSrvDomain,
    ResourceViewType::kStructuredBuffer_SRV);
  EXPECT_TRUE(handle.IsValid());
  if (!handle.IsValid()) {
    return { .buffer = std::move(buffer) };
  }

  const auto slot = allocator.GetShaderVisibleIndex(handle);
  auto view = gfx.GetResourceRegistry().RegisterView(*buffer, std::move(handle),
    oxygen::graphics::BufferViewDescription {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .visibility = DescriptorVisibility::kShaderVisible,
      .range = { 0U, static_cast<std::uint64_t>(element_count * sizeof(T)) },
      .stride = static_cast<std::uint32_t>(sizeof(T)),
    });
  EXPECT_TRUE(view->IsValid());

  return UploadedStructuredBuffer<T> {
    .buffer = std::move(buffer),
    .slot = slot,
  };
}

class ConventionalShadowRasterPassContractTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    gfx_ = std::make_shared<oxygen::renderer::testing::FakeGraphics>();
    gfx_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

    auto config = oxygen::RendererConfig {};
    config.upload_queue_key = gfx_->QueueKeyFor(QueueRole::kGraphics).get();
    renderer_ = std::make_shared<Renderer>(
      std::weak_ptr<Graphics>(gfx_), std::move(config));
  }

  [[nodiscard]] auto AcquireRecorder(std::string_view name)
  {
    return gfx_->AcquireCommandRecorder(
      gfx_->QueueKeyFor(QueueRole::kGraphics), name, false);
  }

  std::shared_ptr<oxygen::renderer::testing::FakeGraphics> gfx_ {};
  std::shared_ptr<Renderer> renderer_ {};
};

NOLINT_TEST_F(ConventionalShadowRasterPassContractTest,
  ExecutesPartitionVariantsWithRequiredRootCbvRebinds)
{
  ASSERT_NE(gfx_, nullptr);
  ASSERT_NE(renderer_, nullptr);

  constexpr std::uint32_t kWidth = 8U;
  constexpr std::uint32_t kHeight = 8U;

  auto scene = std::make_shared<Scene>("shadow-pass.contract.scene", 64U);
  auto directional_node = CreateShadowCastingDirectionalNode(*scene);
  auto directional_impl = directional_node.GetImpl();
  ASSERT_TRUE(directional_impl.has_value());

  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  const auto view_constants
    = MakeShadowViewConstants(resolved_view, kFrameSlot, kFrameSequence);
  const auto main_depth_texture = CreateRegisteredDepthTexture(
    *gfx_, kWidth, kHeight, "shadow-pass.contract.main-depth");
  ASSERT_NE(main_depth_texture, nullptr);

  auto opaque_draw = DrawMetadata {};
  opaque_draw.is_indexed = 0U;
  opaque_draw.instance_count = 1U;
  opaque_draw.vertex_count = 3U;
  opaque_draw.flags = MakeShadowCasterMask(false);

  auto masked_draw = opaque_draw;
  masked_draw.flags = MakeShadowCasterMask(true, true);

  const auto draws = std::array { opaque_draw, masked_draw };
  const auto partitions = std::array {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = MakeShadowCasterMask(false),
      .begin = 0U,
      .end = 1U,
    },
    PreparedSceneFrame::PartitionRange {
      .pass_mask = MakeShadowCasterMask(true, true),
      .begin = 1U,
      .end = 2U,
    },
  };
  const auto draw_bounds = std::array {
    glm::vec4 { 0.0F, 0.0F, -2.0F, 1.0F },
    glm::vec4 { 0.0F, 0.0F, -6.0F, 1.5F },
  };

  auto prepared_frame = PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes = std::as_bytes(std::span(draws));
  prepared_frame.partitions = std::span(partitions);
  prepared_frame.draw_bounding_spheres = std::span(draw_bounds);
  const auto uploaded_draw_metadata = UploadStructuredSrvBuffer(*gfx_,
    std::span<const DrawMetadata>(draws.data(), draws.size()),
    "shadow-pass.contract.draw-metadata");
  prepared_frame.bindless_draw_metadata_slot = uploaded_draw_metadata.slot;

  auto offscreen = renderer_->BeginOffscreenFrame({ .frame_slot = kFrameSlot,
    .frame_sequence = kFrameSequence,
    .scene = observer_ptr<Scene> { scene.get() } });
  offscreen.SetCurrentView(
    kTestViewId, resolved_view, prepared_frame, view_constants);
  auto& render_context = offscreen.GetRenderContext();
  ASSERT_NE(render_context.view_constants, nullptr);

  const auto light_manager = renderer_->GetLightManager();
  ASSERT_NE(light_manager.get(), nullptr);
  light_manager->CollectFromNode(
    directional_node.GetHandle(), directional_impl->get());

  const auto shadow_manager = renderer_->GetShadowManager();
  ASSERT_NE(shadow_manager.get(), nullptr);
  shadow_manager->ReserveFrameResources(1U, *light_manager);

  const auto shadow_caster_bounds
    = std::array { glm::vec4 { 0.0F, 0.0F, 0.0F, 4.0F } };
  const auto visible_receiver_bounds
    = std::array { glm::vec4 { 0.0F, 0.0F, -2.0F, 4.0F } };
  const auto publication = shadow_manager->PublishForView(kTestViewId,
    view_constants, *light_manager, observer_ptr<Scene> { scene.get() },
    static_cast<float>(kWidth), {}, shadow_caster_bounds,
    visible_receiver_bounds);
  EXPECT_TRUE(publication.directional_shadow_texture_srv.IsValid());

  const auto authoritative_depth_texture
    = shadow_manager->GetConventionalShadowDepthTexture();
  ASSERT_NE(authoritative_depth_texture, nullptr);

  const auto* raster_plan = shadow_manager->TryGetRasterRenderPlan(kTestViewId);
  ASSERT_NE(raster_plan, nullptr);
  ASSERT_NE(raster_plan->depth_texture, nullptr);
  ASSERT_FALSE(raster_plan->jobs.empty());
  EXPECT_EQ(
    raster_plan->depth_texture.get(), authoritative_depth_texture.get());

  auto conventional_draw_records
    = std::vector<oxygen::renderer::ConventionalShadowDrawRecord> {};
  BuildConventionalShadowDrawRecords(prepared_frame, conventional_draw_records);
  ASSERT_EQ(conventional_draw_records.size(), draws.size());
  const auto uploaded_draw_records = UploadStructuredSrvBuffer(*gfx_,
    std::span<const oxygen::renderer::ConventionalShadowDrawRecord>(
      conventional_draw_records.data(), conventional_draw_records.size()),
    "shadow-pass.contract.draw-records");
  prepared_frame.conventional_shadow_draw_records
    = std::span(conventional_draw_records);
  prepared_frame.bindless_conventional_shadow_draw_records_slot
    = uploaded_draw_records.slot;
  offscreen.SetCurrentView(
    kTestViewId, resolved_view, prepared_frame, view_constants);

  auto depth_pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = main_depth_texture,
      .debug_name = "shadow-pass.contract.depth",
    }));
  auto screen_hzb_pass
    = ScreenHzbBuildPass(observer_ptr<Graphics> { gfx_.get() },
      std::make_shared<ScreenHzbBuildPassConfig>(ScreenHzbBuildPassConfig {
        .debug_name = "shadow-pass.contract.screen-hzb",
      }));
  auto receiver_analysis_pass = ConventionalShadowReceiverAnalysisPass(
    observer_ptr<Graphics> { gfx_.get() },
    std::make_shared<ConventionalShadowReceiverAnalysisPassConfig>(
      ConventionalShadowReceiverAnalysisPassConfig {
        .debug_name = "shadow-pass.contract.receiver-analysis",
      }));
  auto receiver_mask_pass
    = ConventionalShadowReceiverMaskPass(observer_ptr<Graphics> { gfx_.get() },
      std::make_shared<ConventionalShadowReceiverMaskPassConfig>(
        ConventionalShadowReceiverMaskPassConfig {
          .debug_name = "shadow-pass.contract.receiver-mask",
        }));
  auto caster_culling_pass
    = ConventionalShadowCasterCullingPass(observer_ptr<Graphics> { gfx_.get() },
      std::make_shared<ConventionalShadowCasterCullingPassConfig>(
        ConventionalShadowCasterCullingPassConfig {
          .debug_name = "shadow-pass.contract.caster-culling",
        }));
  auto pass = ConventionalShadowRasterPass(
    std::make_shared<ConventionalShadowRasterPass::Config>(
      ConventionalShadowRasterPass::Config {
        .depth_texture = authoritative_depth_texture,
        .debug_name = "shadow-pass.contract.execute",
      }));

  {
    auto recorder = AcquireRecorder("shadow-pass.contract.depth.run");
    ASSERT_NE(recorder, nullptr);
    recorder->BeginTrackingResourceState(
      *main_depth_texture, ResourceStates::kCommon, true);
    NOLINT_EXPECT_NO_THROW(RunPass(depth_pass, render_context, *recorder));
    render_context.RegisterPass<DepthPrePass>(&depth_pass);
  }
  {
    auto recorder = AcquireRecorder("shadow-pass.contract.screen-hzb.run");
    ASSERT_NE(recorder, nullptr);
    NOLINT_EXPECT_NO_THROW(RunPass(screen_hzb_pass, render_context, *recorder));
    render_context.RegisterPass<ScreenHzbBuildPass>(&screen_hzb_pass);
  }
  {
    auto recorder
      = AcquireRecorder("shadow-pass.contract.receiver-analysis.run");
    ASSERT_NE(recorder, nullptr);
    NOLINT_EXPECT_NO_THROW(
      RunPass(receiver_analysis_pass, render_context, *recorder));
    render_context.RegisterPass<ConventionalShadowReceiverAnalysisPass>(
      &receiver_analysis_pass);
  }
  {
    auto recorder = AcquireRecorder("shadow-pass.contract.receiver-mask.run");
    ASSERT_NE(recorder, nullptr);
    NOLINT_EXPECT_NO_THROW(
      RunPass(receiver_mask_pass, render_context, *recorder));
    render_context.RegisterPass<ConventionalShadowReceiverMaskPass>(
      &receiver_mask_pass);
  }
  {
    auto recorder = AcquireRecorder("shadow-pass.contract.caster-culling.run");
    ASSERT_NE(recorder, nullptr);
    NOLINT_EXPECT_NO_THROW(
      RunPass(caster_culling_pass, render_context, *recorder));
    render_context.RegisterPass<ConventionalShadowCasterCullingPass>(
      &caster_culling_pass);
  }

  const auto hzb_output = screen_hzb_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(hzb_output.available);
  const auto receiver_analysis_output
    = receiver_analysis_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(receiver_analysis_output.available);
  const auto receiver_mask_output
    = receiver_mask_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(receiver_mask_output.available);
  const auto culling_output = caster_culling_pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(culling_output.available);

  const auto culling_partitions
    = caster_culling_pass.GetIndirectPartitionsForInspection(kTestViewId);
  ASSERT_EQ(culling_partitions.size(), partitions.size());

  gfx_->graphics_pipeline_log_.binds.clear();
  gfx_->root_cbv_log_.binds.clear();
  gfx_->draw_log_.draws.clear();
  gfx_->indirect_log_.counted_draws.clear();

  auto recorder = AcquireRecorder("shadow-pass.contract.execute");
  ASSERT_NE(recorder, nullptr);
  NOLINT_EXPECT_NO_THROW(RunPass(pass, render_context, *recorder));

  const auto job_count = static_cast<std::uint32_t>(raster_plan->jobs.size());
  ASSERT_GT(job_count, 0U);
  const auto raster_output = pass.GetCurrentOutput(kTestViewId);
  ASSERT_TRUE(raster_output.available);
  EXPECT_TRUE(raster_output.counted_indirect_used);
  EXPECT_EQ(raster_output.partition_count, culling_partitions.size());
  EXPECT_EQ(raster_output.job_count, job_count);

  const auto raster_partitions
    = pass.GetIndirectPartitionsForInspection(kTestViewId);
  ASSERT_EQ(raster_partitions.size(), culling_partitions.size());
  EXPECT_EQ(gfx_->draw_log_.draws.size(), 0U);
  ASSERT_EQ(gfx_->indirect_log_.counted_draws.size(),
    static_cast<std::size_t>(job_count) * raster_partitions.size());

  for (std::uint32_t job_index = 0U; job_index < job_count; ++job_index) {
    for (std::size_t partition_index = 0U;
      partition_index < raster_partitions.size(); ++partition_index) {
      const auto event_index
        = static_cast<std::size_t>(job_index) * raster_partitions.size()
        + partition_index;
      const auto& partition = raster_partitions[partition_index];
      const auto& event = gfx_->indirect_log_.counted_draws[event_index];
      EXPECT_EQ(event.argument_buffer, partition.command_buffer);
      EXPECT_EQ(event.count_buffer, partition.count_buffer);
      EXPECT_EQ(event.max_command_count, partition.max_commands_per_job);
      EXPECT_EQ(event.layout,
        CommandRecorder::IndirectCommandLayout::kDrawWithRootConstant);
      EXPECT_EQ(event.argument_buffer_offset,
        static_cast<std::uint64_t>(job_index)
          * static_cast<std::uint64_t>(partition.max_commands_per_job)
          * sizeof(oxygen::renderer::ConventionalShadowIndirectDrawCommand));
      EXPECT_EQ(
        event.count_buffer_offset, static_cast<std::uint64_t>(job_index) * 4U);
    }
  }

  ASSERT_EQ(gfx_->graphics_pipeline_log_.binds.size(),
    static_cast<std::size_t>(2U * job_count));

  for (std::size_t bind_index = 0U;
    bind_index < gfx_->graphics_pipeline_log_.binds.size(); ++bind_index) {
    const bool expect_masked = (bind_index % 2U) == 1U;
    const auto expected_cull_mode = expect_masked
      ? oxygen::graphics::CullMode::kNone
      : oxygen::graphics::CullMode::kBack;
    EXPECT_EQ(
      HasShaderDefine(
        gfx_->graphics_pipeline_log_.binds[bind_index].desc, "ALPHA_TEST"),
      expect_masked);
    EXPECT_EQ(gfx_->graphics_pipeline_log_.binds[bind_index]
                .desc.RasterizerState()
                .cull_mode,
      expected_cull_mode);
  }

  constexpr auto kViewConstantsRootParam = static_cast<std::uint32_t>(
    oxygen::bindless::generated::d3d12::RootParam::kViewConstants);
  std::vector<std::uint64_t> view_constant_addresses {};
  for (const auto& bind : gfx_->root_cbv_log_.binds) {
    if (bind.root_parameter_index == kViewConstantsRootParam) {
      view_constant_addresses.push_back(bind.buffer_gpu_address);
    }
  }

  ASSERT_EQ(view_constant_addresses.size(),
    static_cast<std::size_t>(5U * job_count - 1U));

  const auto main_view_address
    = render_context.view_constants->GetGPUVirtualAddress();
  EXPECT_EQ(view_constant_addresses.front(), main_view_address);

  const auto main_view_binds = std::count(view_constant_addresses.begin(),
    view_constant_addresses.end(), main_view_address);
  EXPECT_EQ(main_view_binds, static_cast<std::ptrdiff_t>(2U * job_count));

  std::vector<std::uint64_t> shadow_view_addresses {};
  shadow_view_addresses.reserve(view_constant_addresses.size());
  for (const auto address : view_constant_addresses) {
    if (address != main_view_address) {
      shadow_view_addresses.push_back(address);
    }
  }

  EXPECT_EQ(shadow_view_addresses.size(),
    static_cast<std::size_t>(3U * job_count - 1U));
  std::sort(shadow_view_addresses.begin(), shadow_view_addresses.end());
  shadow_view_addresses.erase(
    std::unique(shadow_view_addresses.begin(), shadow_view_addresses.end()),
    shadow_view_addresses.end());
  EXPECT_EQ(shadow_view_addresses.size(), static_cast<std::size_t>(job_count));

  for (std::size_t i = 1U; i < view_constant_addresses.size(); ++i) {
    if (view_constant_addresses[i] != main_view_address) {
      continue;
    }
    ASSERT_LT(i + 1U, view_constant_addresses.size());
    EXPECT_NE(view_constant_addresses[i + 1U], main_view_address);
  }
}

NOLINT_TEST_F(ConventionalShadowRasterPassContractTest,
  OnAttachedAbortsAfterRendererWasUsedOffscreen)
{
  ASSERT_NE(renderer_, nullptr);

  auto scene = std::make_shared<Scene>("renderer.offscreen.attach.guard", 8U);
  auto prepared_frame = PreparedSceneFrame {};
  {
    auto offscreen = renderer_->BeginOffscreenFrame({ .frame_slot = Slot { 0U },
      .frame_sequence = SequenceNumber { 1U },
      .scene = observer_ptr<Scene> { scene.get() } });
    offscreen.SetCurrentView(
      kTestViewId, MakeResolvedView(4U, 4U), prepared_frame);
  }

  auto engine = FakeAsyncEngine {};
  NOLINT_EXPECT_DEATH(
    (void)renderer_->OnAttached(observer_ptr<oxygen::IAsyncEngine> { &engine }),
    "offscreen rendering");
}

} // namespace
