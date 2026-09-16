//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessFrameBindings.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace oxygen::vortex::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::vortex::internal

namespace {

using oxygen::Format;
using oxygen::Graphics;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::RendererConfig;
using oxygen::TextureType;
using oxygen::ViewId;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::vortex::PostProcessConfig;
using oxygen::vortex::PostProcessFrameBindings;
using oxygen::vortex::PostProcessService;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneTextures;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::testing::FakeGraphics;

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics)
  -> std::shared_ptr<Renderer>
{
  auto config = RendererConfig {};
  config.upload_queue_key = graphics->QueueKeyFor(QueueRole::kGraphics).get();
  constexpr auto kCapabilities = RendererCapabilityFamily::kDeferredShading
    | RendererCapabilityFamily::kFinalOutputComposition;
  return { new Renderer(std::weak_ptr<Graphics>(graphics), std::move(config),
             kCapabilities),
    DestroyRenderer };
}

auto MakeFramebuffer(const std::shared_ptr<FakeGraphics>& graphics,
  std::string_view debug_name) -> std::shared_ptr<Framebuffer>
{
  auto color_desc = TextureDesc {};
  color_desc.width = 64U;
  color_desc.height = 64U;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.initial_state = ResourceStates::kCommon;
  color_desc.debug_name = std::string(debug_name);

  auto color = graphics->CreateTexture(color_desc);
  auto fb_desc = FramebufferDesc {};
  fb_desc.AddColorAttachment({ .texture = color });
  return graphics->CreateFramebuffer(fb_desc);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessFrameBindingsExposeStage22AuthoritySurface)
{
  const auto bindings = PostProcessFrameBindings {};

  EXPECT_EQ(bindings.resolved_scene_color_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.scene_depth_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.scene_velocity_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.bloom_texture_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.eye_adaptation_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.eye_adaptation_uav, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.post_history_srv, kInvalidShaderVisibleIndex);
  EXPECT_FLOAT_EQ(bindings.fixed_exposure, 1.0F);
  EXPECT_EQ(bindings.enable_bloom, 1U);
  EXPECT_EQ(bindings.enable_auto_exposure, 1U);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessConfigDefaultsRemainTonemapFirstAndFixedExposureSafe)
{
  const auto config = PostProcessConfig {};

  EXPECT_TRUE(config.enable_bloom);
  EXPECT_TRUE(config.enable_auto_exposure);
  EXPECT_FLOAT_EQ(config.fixed_exposure, 1.0F);
  EXPECT_FLOAT_EQ(config.bloom_intensity, 0.5F);
  EXPECT_FLOAT_EQ(config.bloom_threshold, 1.0F);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessServiceIsANonPlaceholderSubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<PostProcessService>));
  EXPECT_TRUE((std::is_destructible_v<PostProcessService>));
  EXPECT_TRUE((std::is_standard_layout_v<PostProcessFrameBindings>));
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  FixedExposureAndAutoEvClampShareTheSameEv100CalibrationBasis)
{
  constexpr float kEv100 = 14.0F;
  constexpr float kCompensationEv = 1.25F;
  constexpr float kExposureKey = 10.0F;

  const float manual_scale = oxygen::engine::ExposureScaleFromEv100(
    kEv100, kCompensationEv, kExposureKey);
  const float auto_target_luminance = oxygen::engine::kExposureMiddleGrey
    * oxygen::engine::ExposureBiasScale(kCompensationEv, kExposureKey);
  const float average_luminance
    = oxygen::engine::Ev100ToAverageLuminance(kEv100);
  const float auto_scale = auto_target_luminance / average_luminance;

  EXPECT_FLOAT_EQ(
    oxygen::engine::AverageLuminanceToEv100(average_luminance), kEv100);
  EXPECT_FLOAT_EQ(manual_scale, auto_scale);
}

class PostProcessServiceBehaviorTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    renderer_ = MakeRenderer(graphics_);
  }

  std::shared_ptr<FakeGraphics> graphics_ {};
  std::shared_ptr<Renderer> renderer_ {};
};

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  ExecutePublishesStage22BindingsAndRecordsTonemapVisibleOutput)
{
  auto service = PostProcessService(*renderer_);
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto framebuffer
    = MakeFramebuffer(graphics_, "PostProcessServiceBehaviorTest.Output");

  auto context = RenderContext {};
  context.current_view.view_id = ViewId { 41U };
  context.frame_slot = oxygen::frame::Slot { 1U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 9U };

  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  service.Execute(context.current_view.view_id, context, scene_textures,
    PostProcessService::Inputs {
      .scene_signal = &scene_textures.GetSceneColor(),
      .scene_depth = &scene_textures.GetSceneDepth(),
      .scene_velocity = scene_textures.GetVelocity(),
      .post_target = oxygen::observer_ptr<Framebuffer> { framebuffer.get() },
      .scene_signal_srv = oxygen::ShaderVisibleIndex { 301U },
      .scene_depth_srv = oxygen::ShaderVisibleIndex { 302U },
      .scene_velocity_srv = oxygen::ShaderVisibleIndex { 303U },
    });

  const auto& state = service.GetLastExecutionState();
  ASSERT_NE(service.InspectBindings(context.current_view.view_id), nullptr);
  EXPECT_TRUE(state.published_bindings);
  EXPECT_NE(state.post_process_frame_slot, kInvalidShaderVisibleIndex);
  EXPECT_TRUE(state.tonemap_requested);
  EXPECT_TRUE(state.tonemap_executed);
  EXPECT_TRUE(state.wrote_visible_output);
  EXPECT_EQ(service.ResolveBindingSlot(context.current_view.view_id),
    state.post_process_frame_slot);
  EXPECT_EQ(service.InspectBindings(context.current_view.view_id)
              ->resolved_scene_color_srv,
    oxygen::ShaderVisibleIndex { 301U });
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 1U);
  EXPECT_TRUE(std::ranges::any_of(
    graphics_->graphics_pipeline_log_.binds, [](const auto& bind) -> bool {
      return bind.desc.GetName() == "Vortex.PostProcess.Tonemap";
    }));
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  ExecutePublishesConfiguredAutoExposureEvClampBindings)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.auto_exposure_min_ev = -3.5F;
  config.auto_exposure_max_ev = 11.25F;
  service.SetConfig(config);

  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto framebuffer
    = MakeFramebuffer(graphics_, "PostProcessServiceBehaviorTest.ClampOutput");

  auto context = RenderContext {};
  context.current_view.view_id = ViewId { 42U };
  context.frame_slot = oxygen::frame::Slot { 1U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 10U };

  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  service.Execute(context.current_view.view_id, context, scene_textures,
    PostProcessService::Inputs {
      .scene_signal = &scene_textures.GetSceneColor(),
      .scene_depth = &scene_textures.GetSceneDepth(),
      .scene_velocity = scene_textures.GetVelocity(),
      .post_target = oxygen::observer_ptr<Framebuffer> { framebuffer.get() },
      .scene_signal_srv = oxygen::ShaderVisibleIndex { 401U },
      .scene_depth_srv = oxygen::ShaderVisibleIndex { 402U },
      .scene_velocity_srv = oxygen::ShaderVisibleIndex { 403U },
    });

  const auto* bindings = service.InspectBindings(context.current_view.view_id);
  ASSERT_NE(bindings, nullptr);
  EXPECT_FLOAT_EQ(bindings->auto_exposure_min_ev, config.auto_exposure_min_ev);
  EXPECT_FLOAT_EQ(bindings->auto_exposure_max_ev, config.auto_exposure_max_ev);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  FixedEv14Through16ReachTonemapWithoutAGainFloor)
{
  auto service = PostProcessService(*renderer_);
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto framebuffer
    = MakeFramebuffer(graphics_, "PostProcessServiceBehaviorTest.FixedGain");
  auto context = RenderContext {};
  context.current_view.view_id = ViewId { 43U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 11U };
  context.frame_slot = oxygen::frame::Slot { 1U };
  service.OnFrameStart(context.frame_sequence, context.frame_slot);

  // Independent exact binary references, not a second production conversion.
  for (const float gain : { 0x1p-14F, 0x1p-15F, 0x1p-16F }) {
    auto config = PostProcessConfig {};
    config.enable_auto_exposure = false;
    config.enable_bloom = false;
    config.fixed_exposure = gain;
    config.tone_mapper = oxygen::engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    service.SetConfig(config);
    service.Execute(context.current_view.view_id, context, scene_textures,
      PostProcessService::Inputs {
        .scene_signal = &scene_textures.GetSceneColor(),
        .scene_depth = &scene_textures.GetSceneDepth(),
        .post_target = oxygen::observer_ptr<Framebuffer> { framebuffer.get() },
        .scene_signal_srv = oxygen::ShaderVisibleIndex { 501U },
        .scene_depth_srv = oxygen::ShaderVisibleIndex { 502U },
      });

    const auto& state = service.GetLastExecutionState();
    ASSERT_TRUE(state.tonemap_executed);
    EXPECT_TRUE(state.used_fixed_exposure);
    EXPECT_EQ(state.exposure_value, gain);
    ASSERT_NE(service.InspectBindings(context.current_view.view_id), nullptr);
    EXPECT_EQ(
      service.InspectBindings(context.current_view.view_id)->fixed_exposure,
      gain);
  }
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  InvalidExposureRevisionRetainsOnlyItsViewsPriorSettings)
{
  auto service = PostProcessService(*renderer_);
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto requested = oxygen::scene::ExposureSettings {};
  requested.mode = oxygen::engine::ExposureMode::kManual;
  requested.manual_ev = 14.0F;
  requested.key = 12.5F;
  const auto& first
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(first.revision, 1U);
  EXPECT_EQ(first.resolved.fixed_scale, 0x1p-14F);
  EXPECT_FALSE(first.last_error.has_value());

  requested.manual_ev = 16.0F;
  const auto& second
    = service.ResolveViewExposureSettings(Handle { 2U }, requested);
  EXPECT_EQ(second.resolved.fixed_scale, 0x1p-16F);
  requested.low_percentile = requested.high_percentile;
  const auto& rejected
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(rejected.revision, 1U);
  EXPECT_EQ(rejected.resolved.fixed_scale, 0x1p-14F);
  EXPECT_EQ(rejected.last_error,
    oxygen::scene::ExposureSettingsError::kInvalidPercentiles);

  service.RemoveViewState(ViewId { 1U }, Handle { 1U });
  requested.low_percentile = 0.1F;
  const auto& recreated
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(recreated.revision, 1U);
  EXPECT_EQ(recreated.resolved.fixed_scale, 0x1p-16F);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  InvalidLowLevelFixedExposureKeepsThePreviousConfig)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.enable_auto_exposure = false;
  config.fixed_exposure = 0x1p-14F;
  service.SetConfig(config);
  for (const float invalid :
    { 0.0F, -1.0F, std::numeric_limits<float>::infinity(),
      std::numeric_limits<float>::quiet_NaN() }) {
    config.fixed_exposure = invalid;
    service.SetConfig(config);
    EXPECT_EQ(service.GetConfig().fixed_exposure, 0x1p-14F);
  }
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  RepeatedExposureSettingsKeepRevisionAndStatelessViewsDoNotKeepSettings)
{
  auto service = PostProcessService(*renderer_);
  using View = oxygen::vortex::CompositionView;
  auto requested = oxygen::scene::ExposureSettings {};
  requested.mode = oxygen::engine::ExposureMode::kManual;
  requested.key = 12.5F;
  requested.manual_ev = 14.0F;
  static_cast<void>(service.ResolveViewExposureSettings(
    View::ViewStateHandle { 1U }, requested));
  EXPECT_EQ(
    service.ResolveViewExposureSettings(View::ViewStateHandle { 1U }, requested)
      .revision,
    1U);
  EXPECT_EQ(
    service
      .ResolveViewExposureSettings(View::kInvalidViewStateHandle, requested)
      .resolved.fixed_scale,
    0x1p-14F);
  requested.key = -1.0F;
  const auto& invalid = service.ResolveViewExposureSettings(
    View::kInvalidViewStateHandle, requested);
  EXPECT_EQ(invalid.revision, 0U);
  EXPECT_TRUE(invalid.last_error.has_value());
  EXPECT_EQ(
    invalid.resolved.authored.mode, oxygen::engine::ExposureMode::kAuto);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  PendingAndFailedMaskReplacementRetainsCompleteAcceptedRevision)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Status = PostProcessService::ExposureMaskStatus;
  auto loader = oxygen::vortex::testing::FakeAssetLoader {};
  auto service
    = PostProcessService(*renderer_, oxygen::observer_ptr { &loader });
  const auto tag = oxygen::vortex::internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(
    tag, oxygen::frame::Slot { 1U });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 1U });
  auto requested = oxygen::scene::ExposureSettings {};
  const auto first
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  ASSERT_EQ(first.revision, 1U);
  const auto payload
    = oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  requested.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  requested.compensation_ev = 2.0F;
  const auto pending
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(pending.mask_status, Status::kPending);
  EXPECT_EQ(pending.revision, 1U);
  EXPECT_EQ(pending.resolved.authored, first.resolved.authored);
  auto queue = graphics_->GetCommandQueue(
    oxygen::graphics::SingleQueueStrategy().KeyFor(QueueRole::kTransfer));
  ASSERT_NE(queue, nullptr);
  queue->Signal((std::numeric_limits<std::uint64_t>::max)());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 2U }, oxygen::frame::Slot { 2U });
  renderer_->GetUploadCoordinator().OnFrameStart(
    tag, oxygen::frame::Slot { 2U });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 3U }, oxygen::frame::Slot { 0U });
  const auto accepted
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  ASSERT_EQ(accepted.mask_status, Status::kReady);
  ASSERT_NE(accepted.mask, nullptr);
  EXPECT_EQ(accepted.revision, 2U);
  EXPECT_EQ(accepted.resolved.authored, requested);
  const auto valid_request = requested;
  requested.metering_mask = loader.MintSyntheticTextureKey();
  requested.compensation_ev = -2.0F;
  const auto replacing
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(replacing.mask_status, Status::kPending);
  EXPECT_EQ(replacing.mask, accepted.mask);
  EXPECT_EQ(replacing.revision, 2U);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 4U }, oxygen::frame::Slot { 1U });
  const auto failed
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(failed.mask_status, Status::kFailed);
  EXPECT_EQ(failed.resolved.authored, valid_request);
  EXPECT_EQ(failed.mask, accepted.mask);
  EXPECT_EQ(failed.revision, 2U);
  EXPECT_FALSE(failed.mask_error.empty());
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  InitialMaskFailureIsExplicitAndCannotDelayDisabledOrManualExposure)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Status = PostProcessService::ExposureMaskStatus;
  auto service = PostProcessService(*renderer_);
  auto requested = oxygen::scene::ExposureSettings {};
  requested.metering_mask = oxygen::content::ResourceKey { 123U };
  const auto failed
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(failed.mask_status, Status::kFailed);
  EXPECT_EQ(failed.revision, 0U);
  EXPECT_EQ(failed.mask, nullptr);
  requested.enabled = false;
  const auto disabled
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(disabled.mask_status, Status::kAbsent);
  EXPECT_EQ(disabled.revision, 1U);
  EXPECT_EQ(disabled.resolved.fixed_scale, 1.0F);
  requested.enabled = true;
  requested.mode = oxygen::engine::ExposureMode::kManual;
  const auto manual
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(manual.mask_status, Status::kAbsent);
  EXPECT_EQ(manual.revision, 2U);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  InitialMaskFailureSkipsHistogramAndClearingRequestRestoresMetering)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto service = PostProcessService(*renderer_);
  auto context = RenderContext {};
  context.current_view.view_id = ViewId { 1U };
  context.current_view.view_state_handle = Handle { 1U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  context.frame_slot = oxygen::frame::Slot { 0U };
  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  auto requested = oxygen::scene::ExposureSettings {};
  requested.metering_mask = oxygen::content::ResourceKey { 123U };
  const auto failed
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  auto config = PostProcessConfig {};
  config.resolved_exposure = failed.resolved;
  service.SetConfig(config);
  auto textures
    = SceneTextures(*graphics_, SceneTexturesConfig { .extent = { 64U, 64U } });
  const auto inputs = PostProcessService::Inputs {
    .scene_signal = &textures.GetSceneColor(),
    .scene_signal_srv = oxygen::ShaderVisibleIndex { 301U },
  };
  graphics_->dispatch_log_.dispatches.clear();
  service.Execute(context.current_view.view_id, context, textures, inputs);
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(),
    2U); // clear + invalid/locked solve
  context.frame_sequence = oxygen::frame::SequenceNumber { 2U };
  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  requested.metering_mask = {};
  config.resolved_exposure
    = service.ResolveViewExposureSettings(Handle { 1U }, requested).resolved;
  service.SetConfig(config);
  graphics_->dispatch_log_.dispatches.clear();
  service.Execute(context.current_view.view_id, context, textures, inputs);
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 3U);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  RecordedMaskLeaseSurvivesSettingsReplacementUntilItsFrameSlotRetires)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto loader = oxygen::vortex::testing::FakeAssetLoader {};
  auto service
    = PostProcessService(*renderer_, oxygen::observer_ptr { &loader });
  const auto tag = oxygen::vortex::internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(
    tag, oxygen::frame::Slot { 0U });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  const auto payload
    = oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  auto requested = oxygen::scene::ExposureSettings {};
  requested.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  static_cast<void>(
    service.ResolveViewExposureSettings(Handle { 1U }, requested));
  auto queue = graphics_->GetCommandQueue(
    oxygen::graphics::SingleQueueStrategy().KeyFor(QueueRole::kTransfer));
  ASSERT_NE(queue, nullptr);
  queue->Signal((std::numeric_limits<std::uint64_t>::max)());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 2U }, oxygen::frame::Slot { 1U });
  renderer_->GetUploadCoordinator().OnFrameStart(
    tag, oxygen::frame::Slot { 1U });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 3U }, oxygen::frame::Slot { 2U });
  const auto& ready
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  ASSERT_NE(ready.mask, nullptr);
  std::weak_ptr<const oxygen::vortex::resources::TextureBinder::ReadyTexture>
    frame_lease = ready.mask;
  auto config = PostProcessConfig {};
  config.resolved_exposure = ready.resolved;
  service.SetConfig(config);
  auto context = RenderContext {};
  context.current_view.view_id = ViewId { 1U };
  context.current_view.view_state_handle = Handle { 1U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 3U };
  context.frame_slot = oxygen::frame::Slot { 2U };
  auto textures
    = SceneTextures(*graphics_, SceneTexturesConfig { .extent = { 64U, 64U } });
  service.Execute(context.current_view.view_id, context, textures,
    {
      .scene_signal = &textures.GetSceneColor(),
      .scene_signal_srv = oxygen::ShaderVisibleIndex { 301U },
    });
  loader.EmitTextureEviction(
    requested.metering_mask, oxygen::content::EvictionReason::kRefCountZero);
  requested.metering_mask = {};
  static_cast<void>(
    service.ResolveViewExposureSettings(Handle { 1U }, requested));
  EXPECT_FALSE(frame_lease.expired());
  service.RemoveViewState(ViewId { 1U }, Handle { 1U });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 4U }, oxygen::frame::Slot { 0U });
  EXPECT_FALSE(frame_lease.expired());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 5U }, oxygen::frame::Slot { 1U });
  EXPECT_FALSE(frame_lease.expired());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 2U });
  EXPECT_TRUE(frame_lease.expired());
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  LockedAutoBypassesPendingAndFailedMaskWithOrWithoutAcceptedRevision)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Status = PostProcessService::ExposureMaskStatus;
  auto loader = oxygen::vortex::testing::FakeAssetLoader {};
  auto service
    = PostProcessService(*renderer_, oxygen::observer_ptr { &loader });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  unsigned id = 1U;
  for (bool previous : { false, true }) {
    for (bool failure : { false, true }) {
      const auto handle = Handle { id++ };
      auto requested = oxygen::scene::ExposureSettings {};
      requested.key = 12.5F;
      if (previous) {
        static_cast<void>(
          service.ResolveViewExposureSettings(handle, requested));
      }
      requested.metering_mask = loader.MintSyntheticTextureKey();
      EXPECT_EQ(
        service.ResolveViewExposureSettings(handle, requested).mask_status,
        Status::kPending);
      if (failure) {
        service.OnFrameStart(
          oxygen::frame::SequenceNumber { id }, oxygen::frame::Slot { 1U });
        EXPECT_EQ(
          service.ResolveViewExposureSettings(handle, requested).mask_status,
          Status::kFailed);
      }
      requested.min_ev = requested.max_ev = 2.0F;
      const auto locked
        = service.ResolveViewExposureSettings(handle, requested);
      EXPECT_EQ(locked.mask_status, Status::kAbsent);
      EXPECT_EQ(locked.resolved.authored.min_ev, 2.0F);
      EXPECT_EQ(locked.resolved.authored.max_ev, 2.0F);
      EXPECT_EQ(
        locked.resolved.authored.metering_mask, requested.metering_mask);
      EXPECT_EQ(locked.resolved.dark_log_gain, -2.0F);
      EXPECT_EQ(locked.revision, previous ? 2U : 1U);
    }
  }
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  NonlinearMaskFormatIsRejectedAfterUploadWithoutChangingSettings)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto loader = oxygen::vortex::testing::FakeAssetLoader {};
  auto service
    = PostProcessService(*renderer_, oxygen::observer_ptr { &loader });
  const auto tag = oxygen::vortex::internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(
    tag, oxygen::frame::Slot { 0U });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto requested = oxygen::scene::ExposureSettings {};
  const auto initial
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  auto payload = oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  oxygen::data::pak::core::TextureResourceDesc descriptor {};
  std::memcpy(&descriptor, payload.data(), sizeof(descriptor));
  descriptor.format
    = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNormSRGB);
  std::memcpy(payload.data(), &descriptor, sizeof(descriptor));
  requested.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  requested.compensation_ev = 2.0F;
  static_cast<void>(
    service.ResolveViewExposureSettings(Handle { 1U }, requested));
  auto queue = graphics_->GetCommandQueue(
    oxygen::graphics::SingleQueueStrategy().KeyFor(QueueRole::kTransfer));
  ASSERT_NE(queue, nullptr);
  queue->Signal((std::numeric_limits<std::uint64_t>::max)());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 2U }, oxygen::frame::Slot { 1U });
  renderer_->GetUploadCoordinator().OnFrameStart(
    tag, oxygen::frame::Slot { 1U });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 3U }, oxygen::frame::Slot { 2U });
  const auto rejected
    = service.ResolveViewExposureSettings(Handle { 1U }, requested);
  EXPECT_EQ(
    rejected.mask_status, PostProcessService::ExposureMaskStatus::kFailed);
  EXPECT_EQ(rejected.resolved.authored, initial.resolved.authored);
  EXPECT_EQ(rejected.revision, initial.revision);
  EXPECT_EQ(rejected.mask, nullptr);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  ExposureHistoryAdvancesOnlyAfterSuccessfulSubmission)
{
  auto pass = oxygen::vortex::postprocess::ExposurePass(*renderer_);
  auto settings = oxygen::scene::ExposureSettings {};
  settings.mode = oxygen::engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  auto config = PostProcessConfig {};
  config.resolved_exposure = *oxygen::scene::ResolveExposureSettings(settings);
  auto context = RenderContext {};
  context.frame_slot = oxygen::frame::Slot { 0U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  context.current_view.view_id = ViewId { 1U };
  context.current_view.view_state_handle
    = oxygen::vortex::CompositionView::ViewStateHandle { 1U };
  auto previous = pass.Execute(context, config, {});
  ASSERT_TRUE(previous.executed);
  for (bool recording_failure : { false, true }) {
    context.frame_sequence
      = oxygen::frame::SequenceNumber { context.frame_sequence.get() + 1U };
    graphics_->SetFailSubmission(!recording_failure);
    graphics_->SetFailRecording(recording_failure);
    const auto failed = pass.Execute(context, config, {});
    EXPECT_FALSE(failed.executed);
    EXPECT_EQ(failed.state, previous.state);
    graphics_->SetFailSubmission(false);
    graphics_->SetFailRecording(false);
    const auto retry = pass.Execute(context, config, {});
    EXPECT_TRUE(retry.executed);
    EXPECT_NE(retry.state, failed.state);
    const auto duplicate = pass.Execute(context, config, {});
    EXPECT_FALSE(duplicate.executed);
    EXPECT_EQ(duplicate.state, retry.state);
    previous = retry;
  }
}

} // namespace
