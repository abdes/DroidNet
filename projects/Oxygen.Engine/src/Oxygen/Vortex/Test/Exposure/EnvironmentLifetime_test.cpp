//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(ExposureGpuTest, QueuedHzbBuildsKeepTheirOwnDepthPyramids)
{
  auto module = ScreenHzbModule(*renderer_, SceneTexturesConfig {});
  const std::array extents { glm::uvec2 { 8U, 8U }, glm::uvec2 { 16U, 8U },
    glm::uvec2 { 8U, 16U } };
  std::vector<std::unique_ptr<SceneTextures>> textures;
  std::vector<std::vector<float>> sources;
  std::vector<ScreenHzbModule::Output> outputs;
  for (unsigned view = 0U; view < extents.size(); ++view) {
    const auto extent = extents[view];
    textures.push_back(std::make_unique<SceneTextures>(
      Backend(), SceneTexturesConfig { .extent = extent }));
    auto depth = textures.back()->GetSceneDepthResource();
    sources.emplace_back(extent.x * extent.y);
    auto& registry = Backend().GetResourceRegistry();
    if (!registry.Contains(*depth)) {
      registry.Register(depth);
    }
    const auto dsv_desc
      = TextureViewDescription { .view_type = ResourceViewType::kTexture_DSV,
          .visibility = DescriptorVisibility::kCpuOnly,
          .format = depth->GetDescriptor().format,
          .dimension = TextureType::kTexture2D };
    auto dsv = registry.Find(*depth, dsv_desc);
    if (!dsv->IsValid()) {
      auto allocation
        = renderer_->GetGraphics()->GetDescriptorAllocator().AllocateRaw(
          ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
      dsv = registry.RegisterView(*depth, std::move(allocation), dsv_desc);
    }
    CHECK_F(dsv->IsValid());
    auto recorder = AcquireRecorder("HZB depth fixture pattern");
    EnsureTracked(*recorder, depth, depth->GetDescriptor().initial_state);
    recorder->RequireResourceState(*depth, ResourceStates::kDepthWrite);
    recorder->FlushBarriers();
    for (unsigned y = 0U; y < extent.y; ++y) {
      for (unsigned x = 0U; x < extent.x; ++x) {
        const float value
          = static_cast<float>((x * 3U + y * 5U + view * 17U) % 63U + 1U)
          / 64.0F;
        sources.back()[y * extent.x + x] = value;
        const std::array rects { Scissors { .left = static_cast<int>(x),
          .top = static_cast<int>(y),
          .right = static_cast<int>(x + 1U),
          .bottom = static_cast<int>(y + 1U) } };
        recorder->ClearDepthStencilView(
          *depth, dsv, ClearFlags::kDepth, value, 0U, rects);
      }
    }
    recorder->RequireResourceStateFinal(
      *depth, ResourceStates::kShaderResource);
  }
  WaitForQueueIdle();
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  ctx_.frame_slot = frame::Slot { 0U };
  ctx_.current_view.screen_hzb_request
    = { .current_furthest = true, .current_closest = true };
  const auto capture = BeginOptionalCapture();
  for (unsigned view = 0U; view < extents.size(); ++view) {
    module.OnFrameStart();
    ctx_.current_view.view_id = ViewId { 14000U + view };
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 14000U + view };
    module.Execute(ctx_, *textures[view]);
    outputs.push_back(module.GetCurrentOutput());
    ASSERT_TRUE(outputs.back().available);
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  for (unsigned view = 0U; view < extents.size(); ++view) {
    for (const bool closest : { true, false }) {
      SCOPED_TRACE(view);
      SCOPED_TRACE(closest);
      auto reference = sources[view];
      auto extent = extents[view];
      const auto& texture = closest ? outputs[view].closest_texture
                                    : outputs[view].furthest_texture;
      ASSERT_NE(texture, nullptr);
      for (unsigned mip = 0U; mip < texture->GetDescriptor().mip_levels;
        ++mip) {
        const glm::uvec2 reduced_extent { std::max(1U, extent.x / 2U),
          std::max(1U, extent.y / 2U) };
        std::vector<float> reduced(reduced_extent.x * reduced_extent.y);
        for (unsigned y = 0U; y < reduced_extent.y; ++y) {
          for (unsigned x = 0U; x < reduced_extent.x; ++x) {
            float value = closest ? 0.0F : 1.0F;
            for (unsigned dy = 0U; dy < 2U; ++dy) {
              for (unsigned dx = 0U; dx < 2U; ++dx) {
                const float sample
                  = reference[std::min(y * 2U + dy, extent.y - 1U) * extent.x
                    + std::min(x * 2U + dx, extent.x - 1U)];
                value
                  = closest ? std::max(value, sample) : std::min(value, sample);
              }
            }
            reduced[y * reduced_extent.x + x] = value;
          }
        }
        auto readback = GetReadbackManager()->CreateTextureReadback(
          "HZB independent oracle");
        {
          auto recorder = AcquireRecorder("HZB pyramid readback");
          ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
          ASSERT_TRUE(readback
              ->EnqueueCopy(*recorder, *texture,
                { .src_slice = { .width = reduced_extent.x,
                    .height = reduced_extent.y,
                    .depth = 1U,
                    .mip_level = mip } })
              .has_value());
        }
        const auto mapped = readback->MapNow();
        ASSERT_TRUE(mapped.has_value());
        for (unsigned y = 0U; y < reduced_extent.y; ++y) {
          for (unsigned x = 0U; x < reduced_extent.x; ++x) {
            float actual;
            std::memcpy(&actual,
              mapped->Data() + y * mapped->Layout().row_pitch.get()
                + x * sizeof(float),
              sizeof(float));
            EXPECT_EQ(actual, reduced[y * reduced_extent.x + x]);
          }
        }
        reference = std::move(reduced);
        extent = reduced_extent;
      }
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, RemovedViewsRetireFogHistoryAndExposureLeases)
{
  CheckFogViewRetirement(true, true);
}

NOLINT_TEST_F(ExposureGpuTest, StatelessFogViewsRetainNoPersistentHistory)
{
  CheckFogViewRetirement(false, true);
}

NOLINT_TEST_F(
  ExposureGpuTest, NonTemporalFogOutputsRetireWithoutPersistentHistory)
{
  CheckFogViewRetirement(true, false);
}

NOLINT_TEST_F(
  ExposureGpuTest, SameFrameOffscreenEnvironmentDescriptorsSurviveQueuedViews)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr { &console });
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("OffscreenRetirement", 8U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene->GetEnvironment()
    ->AddSystem<scene::environment::SkyAtmosphere>()
    .SetEnabled(true);
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  fog.SetVolumetricFogEmissive({ .125F, .25F, .5F });
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto sun = scene->CreateNode("Sun");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetEnvironmentContribution(true);
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  light->SetIntensityLux(1000.0F);
  ASSERT_TRUE(sun.AttachLight(std::move(light)));
  auto view = View {};
  view.viewport = { .width = 16.0F, .height = 16.0F };
  std::array<scene::SceneNode, 2> cameras;
  std::array<std::shared_ptr<Texture>, 2> colors;
  std::array<std::shared_ptr<Framebuffer>, 2> targets;
  for (unsigned i = 0U; i < 2U; ++i) {
    cameras[i] = scene->CreateNode(i ? "High camera" : "Low camera");
    auto lens = std::make_unique<scene::PerspectiveCamera>();
    lens->SetViewport(view.viewport);
    ASSERT_TRUE(cameras[i].AttachCamera(std::move(lens)));
    cameras[i].GetTransform().SetLocalPosition({ 0, -10, i ? 2000.0F : 2.0F });
    colors[i] = CreateRegisteredTexture({ .width = 16U,
      .height = 16U,
      .format = Format::kRGBA32Float,
      .is_shader_resource = true,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    targets[i] = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(colors[i]));
  }
  scene->Update();
  struct Capture final : IViewExtension {
    Renderer& renderer;
    std::unordered_map<ViewId, std::vector<std::shared_ptr<Texture>>> textures;
    std::unordered_map<ViewId, std::vector<ShaderVisibleIndex>> slots;
    std::unordered_map<ViewId, postprocess::ExposurePass::FrameLease> exposure;
    explicit Capture(Renderer& value)
      : renderer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      // The public extension supplies the fog participation flag absent from
      // the offscreen builder's current authoring surface.
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      const auto id = hook.render_context.current_view.view_id;
      exposure[id] = hook.render_context.current_view.frame_exposure;
      textures[id]
        = vortex::testing::RendererPublicationProbe::EnvironmentTextures(
          *owner, id);
      slots[id].clear();
      for (const auto& texture : textures[id]) {
        const auto slot
          = renderer.GetGraphics()
              ->GetResourceRegistry()
              .FindShaderVisibleIndex(*texture,
                TextureViewDescription {
                  .format = texture->GetDescriptor().format,
                  .dimension = texture->GetDescriptor().texture_type });
        CHECK_F(slot.has_value());
        slots[id].push_back(*slot);
      }
    }
  };
  auto capture = std::make_shared<Capture>(*renderer_);
  renderer_->RegisterViewExtension(capture);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  const auto begin = [&](std::uint64_t sequence, frame::Slot slot) {
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
  };
  const auto render = [&](unsigned index) {
    auto input = Renderer::OffscreenSceneViewInput::FromCamera(
      "Environment retirement", ViewId { 111U + index }, view, cameras[index]);
    input.SetWithAtmosphere(true);
    input.SetViewStateHandle(CompositionView::ViewStateHandle { 111U + index });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = frame.GetFrameSlot(),
      .frame_sequence = frame.GetFrameSequenceNumber(),
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(input);
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr { targets[index].get() } });
    auto session = facade.Finalize();
    CHECK_F(session.has_value());
    return session->ExecuteInsideFrame(frame);
  };
  const auto read = [&] {
    auto readback = GetReadbackManager()->CreateTextureReadback(
      "Offscreen environment image");
    {
      auto recorder = AcquireRecorder("Offscreen environment readback");
      CHECK_F(recorder->AdoptKnownResourceState(*colors[0]));
      CHECK_F(readback->EnqueueCopy(*recorder, *colors[0], {}).has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    std::array<Pixel, 256U> pixels;
    for (unsigned y = 0; y < 16U; ++y) {
      std::memcpy(pixels.data() + y * 16U,
        mapped->Data() + y * mapped->Layout().row_pitch.get(),
        16U * sizeof(Pixel));
    }
    return pixels;
  };
  auto& reclaimer = Backend().GetDeferredReclaimer();
  reclaimer.OnBeginFrame(frame::Slot { 0U });
  begin(1U, frame::Slot { 0U });
  ASSERT_TRUE(render(0U));
  const auto reference = read();
  renderer_->OnFrameEnd(observer_ptr { &frame });
  reclaimer.OnBeginFrame(frame::Slot { 1U });
  begin(2U, frame::Slot { 1U });
  const auto gpu_capture = BeginOptionalCapture();
  ASSERT_TRUE(render(0U));
  auto retained = capture->textures.at(ViewId { 111U });
  const auto retained_slots = capture->slots.at(ViewId { 111U });
  ASSERT_GE(retained.size(),
    4U); // Sky/AP plus replaced and current fog history.
  auto& registry = Backend().GetResourceRegistry();
  for (const auto& texture : retained) {
    ASSERT_NE(texture, nullptr);
    ASSERT_TRUE(registry.Contains(*texture)) << texture->GetName();
  }
  // No queue-idle wait or readback map occurs between these offscreen views.
  ASSERT_TRUE(render(1U));
  if (gpu_capture) {
    EXPECT_TRUE(gpu_capture->EndCapture());
  }
  for (std::size_t i = 0U; i < retained.size(); ++i) {
    const auto& texture = retained[i];
    EXPECT_TRUE(registry.Contains(*texture)) << texture->GetName();
    EXPECT_EQ(
      registry.FindShaderVisibleIndex(*texture,
        TextureViewDescription { .format = texture->GetDescriptor().format,
          .dimension = texture->GetDescriptor().texture_type }),
      retained_slots[i]);
  }
  constexpr std::uint32_t required
    = (1U << 4U) | (1U << 5U) | (1U << 9U) | (1U << 10U);
  for (const auto id : { ViewId { 111U }, ViewId { 112U } }) {
    const auto& exposure = capture->exposure.at(id);
    ASSERT_NE(exposure, nullptr);
    const auto report = Read<HdrSuitabilityData>(
      *exposure->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.expected_products, required);
    EXPECT_EQ(report.checked_products, required);
    EXPECT_GT(report.checked_samples, 256U);
    const auto state = Read<ExposureStateData>(
      *exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(state.displayed_scale, .0625F);
    EXPECT_NE(state.product_layout_revision[0], 0U);
  }
  const auto actual = read();
  unsigned nontrivial = 0U;
  for (unsigned i = 0; i < actual.size(); ++i) {
    for (unsigned c = 0; c < 3U; ++c) {
      EXPECT_TRUE(std::isfinite(actual[i][c]));
      EXPECT_NEAR(actual[i][c], reference[i][c],
        2e-5F + .005F * std::abs(reference[i][c]));
      nontrivial += reference[i][c] > .001F && reference[i][c] < .99F ? 1U : 0U;
    }
  }
  EXPECT_GT(nontrivial, 0U);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
  reclaimer.OnBeginFrame(frame::Slot { 2U });
  EXPECT_TRUE(registry.Contains(*retained.front()));
  reclaimer.OnBeginFrame(frame::Slot { 1U });
  EXPECT_TRUE(registry.Contains(*retained.front()));
  const auto original_state = Read<ExposureStateData>(
    *capture->exposure.at(ViewId { 111U })->current_state->buffer,
    ResourceStates::kShaderResource);
  scene->GetEnvironment()
    ->TryGetSystem<scene::environment::SkyAtmosphere>()
    ->SetAerialScatteringStrength(2.0F);
  scene->Update();
  reclaimer.OnBeginFrame(frame::Slot { 2U });
  begin(3U, frame::Slot { 2U });
  ASSERT_TRUE(render(0U));
  const auto amplified_state = Read<ExposureStateData>(
    *capture->exposure.at(ViewId { 111U })->current_state->buffer,
    ResourceStates::kShaderResource);
  EXPECT_NE(amplified_state.product_layout_revision,
    original_state.product_layout_revision);
  EXPECT_EQ(amplified_state.displayed_scale, original_state.displayed_scale);
  EXPECT_EQ(
    amplified_state.requested_generation, original_state.requested_generation);
  EXPECT_EQ(
    amplified_state.applied_generation, original_state.applied_generation);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
  // The producer and captured view now refer to the replacement snapshot.
  // Keep only an underlying observer when releasing the old retained readers.
  const auto retired_resource = retained.front()->shared_from_this();
  EXPECT_TRUE(registry.Contains(*retired_resource));
  retained.clear();
  capture->textures.clear();
  reclaimer.OnBeginFrame(frame::Slot { 1U });
  EXPECT_TRUE(registry.Contains(*retired_resource));
  reclaimer.OnBeginFrame(frame::Slot { 2U });
  EXPECT_FALSE(registry.Contains(*retired_resource));
  begin(4U, frame::Slot { 0U });
  ASSERT_TRUE(render(0U));
  const auto stable_state = Read<ExposureStateData>(
    *capture->exposure.at(ViewId { 111U })->current_state->buffer,
    ResourceStates::kShaderResource);
  EXPECT_EQ(stable_state.product_layout_revision,
    amplified_state.product_layout_revision);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
  begin(5U, frame::Slot { 1U });
  static_cast<ExposureFailureGraphics&>(Backend()).fail_recorder_name
    = "EnvironmentLightingService AtmosphereSkyViewLut";
  ASSERT_TRUE(render(0U));
  static_cast<ExposureFailureGraphics&>(Backend()).fail_recorder_name.clear();
  const auto& missing = capture->exposure.at(ViewId { 111U });
  const auto report = Read<HdrSuitabilityData>(
    *missing->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(report.expected_products, required);
  EXPECT_EQ(report.checked_products, required & ~(1U << 4U));
  const auto status = Read<ExposureCompletedStatus>(
    *missing->current_state->status_buffer, ResourceStates::kCopySource);
  EXPECT_NE(status.first_failure_kind & 16U, 0U);
  EXPECT_EQ(status.fp16_eligible_streak, 0U);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
}

} // namespace oxygen::vortex::testing::exposure
