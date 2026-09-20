//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Test/CommandRecordingTestSupport.h>
#include <Oxygen/Testing/GTest.h>

#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
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
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
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
using oxygen::vortex::ResolvedPostProcessConfig;
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
  return {
    new Renderer(
      std::weak_ptr<Graphics>(graphics), std::move(config), kCapabilities),
    DestroyRenderer,
  };
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
  fb_desc.AddColorAttachment({
    .texture = color,
  });
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
  EXPECT_EQ(bindings.scene_fallback_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.conversion_report_srv, kInvalidShaderVisibleIndex);
  EXPECT_FLOAT_EQ(bindings.fixed_exposure, 1.0F);
  EXPECT_EQ(bindings.enable_bloom, 1U);
  EXPECT_EQ(bindings.enable_auto_exposure, 1U);
}

NOLINT_TEST(PostProcessServiceSurfaceTest,
  PostProcessConfigDefaultsRemainTonemapFirstAndFixedExposureSafe)
{
  const auto config = PostProcessConfig {};

  EXPECT_TRUE(config.enable_bloom);
  EXPECT_TRUE(config.exposure.enabled);
  EXPECT_EQ(config.exposure.mode, oxygen::engine::ExposureMode::kAuto);
  EXPECT_FLOAT_EQ(
    ResolvedPostProcessConfig::Resolve(config)->Exposure().fixed_scale, 1.0F);
  EXPECT_FLOAT_EQ(config.exposure.speed_down, 1.0F);
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

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
};

//! Discarding a frame recording releases its same-frame resolve cache entry.
NOLINT_TEST_F(PostProcessServiceBehaviorTest, DiscardedFrameResolveAllowsRetry)
{
  // Arrange
  auto pass = oxygen::vortex::postprocess::ExposurePass(*renderer_);
  const auto config = *ResolvedPostProcessConfig::Resolve(PostProcessConfig {});
  auto context = RenderContext {};
  context.frame_slot = oxygen::frame::Slot { 0U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  context.current_view.view_id = ViewId { 1U };
  const auto queue
    = graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics);
  auto recording = graphics_->AcquireCommandRecorder(
    queue, "Discarded frame", oxygen::graphics::SubmissionPolicy::kExplicit);
  const auto discarded = pass.ResolveFrame(context, *recording, config, {});
  ASSERT_NE(discarded, nullptr);

  // Act
  recording.Discard();
  const auto retry = oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Retry frame", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return pass.ResolveFrame(context, recorder, config, {});
    });

  // Assert
  ASSERT_NE(retry, nullptr);
  EXPECT_NE(retry, discarded);
}

//! A discarded later range writer cannot leave a certificate on a valid frame.
NOLINT_TEST_F(
  PostProcessServiceBehaviorTest, DiscardedRangeInvalidatesCertificate)
{
  // Arrange
  auto pass = oxygen::vortex::postprocess::ExposurePass(*renderer_);
  const auto config = *ResolvedPostProcessConfig::Resolve(PostProcessConfig {});
  auto context = RenderContext {};
  context.frame_slot = oxygen::frame::Slot { 0U };
  context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  context.current_view.view_id = ViewId { 1U };
  const auto frame = oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Frame", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return pass.ResolveFrame(context, recorder, config, {});
    });
  ASSERT_NE(frame, nullptr);
  const auto source = graphics_->CreateTexture({ .width = 1U,
    .height = 1U,
    .format = Format::kRGBA32Float,
    .initial_state = oxygen::graphics::ResourceStates::kCommon });
  const auto queue
    = graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics);
  auto recording = graphics_->AcquireCommandRecorder(
    queue, "Discarded range", oxygen::graphics::SubmissionPolicy::kExplicit);
  ASSERT_TRUE(pass.CapturePreEnvironmentRange(
    context, *recording, frame, *source, oxygen::ShaderVisibleIndex { 1U }));
  ASSERT_TRUE(pass.HasPreEnvironmentRange(frame));

  // Act
  recording.Discard();

  // Assert
  EXPECT_FALSE(pass.HasPreEnvironmentRange(frame));
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest, ReadModifyApplyUsesEditedExposure)
{
  auto service = PostProcessService(*renderer_);
  auto config = service.GetConfig();
  config.exposure.mode = oxygen::engine::ExposureMode::kManual;
  config.exposure.key = 12.5F;
  config.exposure.manual_ev = 2.0F;
  config.exposure.speed_down = .75F;
  service.SetConfig(config);
  auto context = RenderContext {};
  context.current_view.view_id = ViewId {
    501U,
  };
  context.current_view.view_state_handle
    = oxygen::vortex::CompositionView::ViewStateHandle {
        501U,
      };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    1U,
  };
  context.frame_slot = oxygen::frame::Slot {
    0U,
  };
  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  auto textures
    = SceneTextures(*graphics_, SceneTexturesConfig { .extent = { 4U, 4U, }, });
  auto inputs = PostProcessService::Inputs {};
  inputs.scene_signal = &textures.GetSceneColor();
  inputs.scene_signal_srv = oxygen::ShaderVisibleIndex {
    301U,
  };
  static_cast<void>(oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return service.Record(
        context.current_view.view_id, context, recorder, inputs);
    }));
  const auto* bindings = service.InspectBindings(context.current_view.view_id);
  ASSERT_NE(bindings, nullptr);
  EXPECT_EQ(bindings->enable_auto_exposure, 0U);
  EXPECT_EQ(bindings->fixed_exposure, .25F);
  EXPECT_EQ(bindings->auto_exposure_speed_down, .75F);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  CameraContextSurvivesReadModifyApplyAndRejection)
{
  auto service = PostProcessService(*renderer_);
  auto config = service.GetConfig();
  config.exposure.mode = oxygen::engine::ExposureMode::kManualCamera;
  config.exposure.key = 12.5F;
  service.SetConfig(config, 4.0F);
  EXPECT_EQ(service.BuildBindings({}).fixed_exposure, 0x1p-4F);
  config = service.GetConfig();
  config.exposure.compensation_ev = 1.0F;
  config.exposure.speed_down = .75F;
  service.SetConfig(config);
  EXPECT_EQ(service.BuildBindings({}).fixed_exposure, 0x1p-3F);
  const auto accepted = service.GetConfig();
  config.exposure.low_percentile = config.exposure.high_percentile;
  config.gamma = 1.0F;
  service.SetConfig(config, 12.0F);
  EXPECT_EQ(service.GetConfig(), accepted);
  config = service.GetConfig();
  config.exposure.compensation_ev = 2.0F;
  service.SetConfig(config);
  EXPECT_EQ(service.BuildBindings({}).fixed_exposure, .25F);
  EXPECT_EQ(service.BuildBindings({}).auto_exposure_speed_down, .75F);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  ExecutePublishesStage22BindingsAndRecordsTonemapVisibleOutput)
{
  auto service = PostProcessService(*renderer_);
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U, },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto framebuffer
    = MakeFramebuffer(graphics_, "PostProcessServiceBehaviorTest.Output");

  auto context = RenderContext {};
  context.current_view.view_id = ViewId {
    41U,
  };
  context.frame_slot = oxygen::frame::Slot {
    1U,
  };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    9U,
  };

  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();
  auto inputs = PostProcessService::Inputs {};
  inputs.scene_signal = &scene_textures.GetSceneColor();
  inputs.scene_depth = &scene_textures.GetSceneDepth();
  inputs.scene_velocity = scene_textures.GetVelocity();
  inputs.post_target = oxygen::observer_ptr<Framebuffer> {
    framebuffer.get(),
  };
  inputs.scene_signal_srv = oxygen::ShaderVisibleIndex {
    301U,
  };
  inputs.scene_depth_srv = oxygen::ShaderVisibleIndex {
    302U,
  };
  inputs.scene_velocity_srv = oxygen::ShaderVisibleIndex {
    303U,
  };
  static_cast<void>(oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return service.Record(
        context.current_view.view_id, context, recorder, inputs);
    }));

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
    (oxygen::ShaderVisibleIndex {
      301U,
    }));
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
  config.exposure.min_ev = -3.5F;
  config.exposure.max_ev = 11.25F;
  service.SetConfig(config);

  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U, },
      .enable_velocity = true,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto framebuffer
    = MakeFramebuffer(graphics_, "PostProcessServiceBehaviorTest.ClampOutput");

  auto context = RenderContext {};
  context.current_view.view_id = ViewId {
    42U,
  };
  context.frame_slot = oxygen::frame::Slot {
    1U,
  };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    10U,
  };

  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  auto inputs = PostProcessService::Inputs {};
  inputs.scene_signal = &scene_textures.GetSceneColor();
  inputs.scene_depth = &scene_textures.GetSceneDepth();
  inputs.scene_velocity = scene_textures.GetVelocity();
  inputs.post_target = oxygen::observer_ptr<Framebuffer> {
    framebuffer.get(),
  };
  inputs.scene_signal_srv = oxygen::ShaderVisibleIndex {
    401U,
  };
  inputs.scene_depth_srv = oxygen::ShaderVisibleIndex {
    402U,
  };
  inputs.scene_velocity_srv = oxygen::ShaderVisibleIndex {
    403U,
  };
  static_cast<void>(oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return service.Record(
        context.current_view.view_id, context, recorder, inputs);
    }));

  const auto* bindings = service.InspectBindings(context.current_view.view_id);
  ASSERT_NE(bindings, nullptr);
  EXPECT_FLOAT_EQ(bindings->auto_exposure_min_ev, config.exposure.min_ev);
  EXPECT_FLOAT_EQ(bindings->auto_exposure_max_ev, config.exposure.max_ev);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  FixedEv14Through16ReachTonemapWithoutAGainFloor)
{
  auto service = PostProcessService(*renderer_);
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U, },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto framebuffer
    = MakeFramebuffer(graphics_, "PostProcessServiceBehaviorTest.FixedGain");
  auto context = RenderContext {};
  context.current_view.view_id = ViewId {
    43U,
  };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    11U,
  };
  context.frame_slot = oxygen::frame::Slot {
    1U,
  };
  service.OnFrameStart(context.frame_sequence, context.frame_slot);

  // Independent exact binary references, not a second production conversion.
  for (const float gain : {
         0x1p-14F,
         0x1p-15F,
         0x1p-16F,
       }) {
    context.frame_sequence = oxygen::frame::SequenceNumber {
      context.frame_sequence.get() + 1U,
    };
    service.OnFrameStart(context.frame_sequence, context.frame_slot);
    auto config = PostProcessConfig {};
    config.exposure.mode = oxygen::engine::ExposureMode::kManual;
    config.exposure.key = 12.5F;
    config.enable_bloom = false;
    config.exposure.manual_ev = -std::log2(gain);
    config.tone_mapper = oxygen::engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    service.SetConfig(config);
    auto inputs = PostProcessService::Inputs {};
    inputs.scene_signal = &scene_textures.GetSceneColor();
    inputs.scene_depth = &scene_textures.GetSceneDepth();
    inputs.post_target = oxygen::observer_ptr<Framebuffer> {
      framebuffer.get(),
    };
    inputs.scene_signal_srv = oxygen::ShaderVisibleIndex {
      501U,
    };
    inputs.scene_depth_srv = oxygen::ShaderVisibleIndex {
      502U,
    };
    static_cast<void>(oxygen::graphics::testing::SubmitCommands(*graphics_,
      "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
        return service.Record(
          context.current_view.view_id, context, recorder, inputs);
      }));

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
  const auto& first = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(first.revision, 1U);
  EXPECT_EQ(first.resolved.fixed_scale, 0x1p-14F);
  EXPECT_FALSE(first.last_error.has_value());

  requested.manual_ev = 16.0F;
  const auto& second = service.ResolveViewExposureSettings(
    Handle {
      2U,
    },
    requested);
  EXPECT_EQ(second.resolved.fixed_scale, 0x1p-16F);
  requested.low_percentile = requested.high_percentile;
  const auto& rejected = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(rejected.revision, 1U);
  EXPECT_EQ(rejected.resolved.fixed_scale, 0x1p-14F);
  EXPECT_EQ(rejected.last_error,
    oxygen::scene::ExposureSettingsError::kInvalidPercentiles);

  service.RemoveViewState(
    ViewId {
      1U,
    },
    Handle {
      1U,
    });
  requested.low_percentile = 0.1F;
  const auto& recreated = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(recreated.revision, 1U);
  EXPECT_EQ(recreated.resolved.fixed_scale, 0x1p-16F);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  InvalidLowLevelFixedExposureKeepsThePreviousConfig)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = oxygen::engine::ExposureMode::kManual;
  config.exposure.key = 12.5F;
  config.exposure.manual_ev = 14.0F;
  service.SetConfig(config);
  for (const float invalid : {
         100.0F,
         -100.0F,
         std::numeric_limits<float>::infinity(),
         std::numeric_limits<float>::quiet_NaN(),
       }) {
    config.exposure.manual_ev = invalid;
    config.gamma = 1.0F;
    config.exposure.speed_down = .75F;
    service.SetConfig(config);
    EXPECT_EQ(service.GetConfig().exposure.manual_ev, 14.0F);
    EXPECT_EQ(service.GetConfig().gamma, 2.2F);
    EXPECT_EQ(service.GetConfig().exposure.speed_down, 1.0F);
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
    View::ViewStateHandle {
      1U,
    },
    requested));
  EXPECT_EQ(service
              .ResolveViewExposureSettings(
                View::ViewStateHandle {
                  1U,
                },
                requested)
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
  auto service = PostProcessService(*renderer_,
    oxygen::observer_ptr {
      &loader,
    });
  const auto tag = oxygen::vortex::internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(tag,
    oxygen::frame::Slot {
      1U,
    });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      1U,
    });
  auto requested = oxygen::scene::ExposureSettings {};
  const auto first = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  ASSERT_EQ(first.revision, 1U);
  const auto payload
    = oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  requested.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  requested.compensation_ev = 2.0F;
  const auto pending = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(pending.mask_status, Status::kPending);
  EXPECT_EQ(pending.revision, 1U);
  EXPECT_EQ(pending.resolved.authored, first.resolved.authored);
  auto queue = graphics_->GetCommandQueue(
    oxygen::graphics::SingleQueueStrategy().KeyFor(QueueRole::kTransfer));
  ASSERT_NE(queue, nullptr);
  queue->Signal(std::numeric_limits<std::uint64_t>::max());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      2U,
    },
    oxygen::frame::Slot {
      2U,
    });
  renderer_->GetUploadCoordinator().OnFrameStart(tag,
    oxygen::frame::Slot {
      2U,
    });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      3U,
    },
    oxygen::frame::Slot {
      0U,
    });
  const auto accepted = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  ASSERT_EQ(accepted.mask_status, Status::kReady);
  ASSERT_NE(accepted.mask, nullptr);
  EXPECT_EQ(accepted.revision, 2U);
  EXPECT_EQ(accepted.resolved.authored, requested);
  const auto valid_request = requested;
  requested.metering_mask = loader.MintSyntheticTextureKey();
  requested.compensation_ev = -2.0F;
  const auto replacing = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(replacing.mask_status, Status::kPending);
  EXPECT_EQ(replacing.mask, accepted.mask);
  EXPECT_EQ(replacing.revision, 2U);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      4U,
    },
    oxygen::frame::Slot {
      1U,
    });
  const auto failed = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(failed.mask_status, Status::kFailed);
  EXPECT_EQ(failed.resolved.authored, valid_request);
  EXPECT_EQ(failed.mask, accepted.mask);
  EXPECT_EQ(failed.revision, 2U);
  EXPECT_FALSE(failed.mask_error.empty());
  const auto captured = service.CaptureViewExposureSettings(
    ViewId {
      1U,
    },
    Handle {
      1U,
    },
    valid_request);
  requested = valid_request;
  requested.metering_mask = {};
  const auto cleared = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_GT(cleared.revision, captured.revision);
  EXPECT_EQ(cleared.mask, nullptr);
  EXPECT_EQ(service
              .CaptureViewExposureSettings(
                ViewId {
                  1U,
                },
                Handle {
                  1U,
                },
                requested)
              .mask,
    captured.mask);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      5U,
    },
    oxygen::frame::Slot {
      2U,
    });
  EXPECT_EQ(service
              .CaptureViewExposureSettings(
                ViewId {
                  1U,
                },
                Handle {
                  1U,
                },
                requested)
              .mask,
    nullptr);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  InitialMaskFailureIsExplicitAndCannotDelayDisabledOrManualExposure)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Status = PostProcessService::ExposureMaskStatus;
  auto service = PostProcessService(*renderer_);
  auto requested = oxygen::scene::ExposureSettings {};
  requested.metering_mask = oxygen::content::ResourceKey {
    123U,
  };
  const auto failed = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(failed.mask_status, Status::kFailed);
  EXPECT_EQ(failed.revision, 0U);
  EXPECT_EQ(failed.mask, nullptr);
  requested.enabled = false;
  const auto disabled = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(disabled.mask_status, Status::kAbsent);
  EXPECT_EQ(disabled.revision, 1U);
  EXPECT_EQ(disabled.resolved.fixed_scale, 1.0F);
  requested.enabled = true;
  requested.mode = oxygen::engine::ExposureMode::kManual;
  const auto manual = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(manual.mask_status, Status::kAbsent);
  EXPECT_EQ(manual.revision, 2U);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  InitialMaskFailureSkipsHistogramAndClearingRequestRestoresMetering)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto service = PostProcessService(*renderer_);
  auto context = RenderContext {};
  context.current_view.view_id = ViewId {
    1U,
  };
  context.current_view.view_state_handle = Handle {
    1U,
  };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    1U,
  };
  context.frame_slot = oxygen::frame::Slot {
    0U,
  };
  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  auto requested = oxygen::scene::ExposureSettings {};
  requested.metering_mask = oxygen::content::ResourceKey {
    123U,
  };
  auto config = PostProcessConfig {
    .exposure = requested,
  };
  service.SetConfig(config);
  auto textures
    = SceneTextures(*graphics_, SceneTexturesConfig { .extent = { 64U, 64U, }, });
  auto inputs = PostProcessService::Inputs {};
  inputs.scene_signal = &textures.GetSceneColor();
  inputs.scene_signal_srv = oxygen::ShaderVisibleIndex {
    301U,
  };
  graphics_->dispatch_log_.dispatches.clear();
  static_cast<void>(oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return service.Record(
        context.current_view.view_id, context, recorder, inputs);
    }));
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(),
    2U); // clear + invalid/locked solve
  context.frame_sequence = oxygen::frame::SequenceNumber {
    2U,
  };
  service.OnFrameStart(context.frame_sequence, context.frame_slot);
  requested.metering_mask = {};
  config.exposure = service
                      .ResolveViewExposureSettings(
                        Handle {
                          1U,
                        },
                        requested)
                      .resolved.authored;
  service.SetConfig(config);
  graphics_->dispatch_log_.dispatches.clear();
  static_cast<void>(oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return service.Record(
        context.current_view.view_id, context, recorder, inputs);
    }));
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 3U);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  RecordedMaskLeaseSurvivesSettingsReplacementUntilItsFrameSlotRetires)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto loader = oxygen::vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_,
    oxygen::observer_ptr {
      &loader,
    });
  const auto tag = oxygen::vortex::internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(tag,
    oxygen::frame::Slot {
      0U,
    });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  const auto payload
    = oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  auto requested = oxygen::scene::ExposureSettings {};
  requested.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  static_cast<void>(service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested));
  auto queue = graphics_->GetCommandQueue(
    oxygen::graphics::SingleQueueStrategy().KeyFor(QueueRole::kTransfer));
  ASSERT_NE(queue, nullptr);
  queue->Signal(std::numeric_limits<std::uint64_t>::max());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      2U,
    },
    oxygen::frame::Slot {
      1U,
    });
  renderer_->GetUploadCoordinator().OnFrameStart(tag,
    oxygen::frame::Slot {
      1U,
    });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      3U,
    },
    oxygen::frame::Slot {
      2U,
    });
  const auto& ready = service.CaptureViewExposureSettings(
    ViewId {
      1U,
    },
    Handle {
      1U,
    },
    requested);
  ASSERT_NE(ready.mask, nullptr);
  std::weak_ptr<const oxygen::vortex::resources::TextureBinder::ReadyTexture>
    frame_lease = ready.mask;
  auto config = PostProcessConfig {};
  service.SetResolvedConfig(service.BuildPassConfig(config,
    ViewId {
      1U,
    },
    Handle {
      1U,
    }));
  auto context = RenderContext {};
  context.current_view.view_id = ViewId {
    1U,
  };
  context.current_view.view_state_handle = Handle {
    1U,
  };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    3U,
  };
  context.frame_slot = oxygen::frame::Slot {
    2U,
  };
  auto textures
    = SceneTextures(*graphics_, SceneTexturesConfig { .extent = { 64U, 64U, }, });
  auto inputs = PostProcessService::Inputs {};
  inputs.scene_signal = &textures.GetSceneColor();
  inputs.scene_signal_srv = oxygen::ShaderVisibleIndex {
    301U,
  };
  static_cast<void>(oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return service.Record(
        context.current_view.view_id, context, recorder, inputs);
    }));
  loader.EmitTextureEviction(
    requested.metering_mask, oxygen::content::EvictionReason::kRefCountZero);
  requested.metering_mask = {};
  static_cast<void>(service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested));
  EXPECT_FALSE(frame_lease.expired());
  service.RemoveViewState(
    ViewId {
      1U,
    },
    Handle {
      1U,
    });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      4U,
    },
    oxygen::frame::Slot {
      0U,
    });
  EXPECT_FALSE(frame_lease.expired());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      5U,
    },
    oxygen::frame::Slot {
      1U,
    });
  EXPECT_FALSE(frame_lease.expired());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      6U,
    },
    oxygen::frame::Slot {
      2U,
    });
  EXPECT_TRUE(frame_lease.expired());
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  LockedAutoBypassesPendingAndFailedMaskWithOrWithoutAcceptedRevision)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Status = PostProcessService::ExposureMaskStatus;
  auto loader = oxygen::vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_,
    oxygen::observer_ptr {
      &loader,
    });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  unsigned id = 1U;
  for (bool previous : {
         false,
         true,
       }) {
    for (bool failure : {
           false,
           true,
         }) {
      const auto handle = Handle {
        id++,
      };
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
          oxygen::frame::SequenceNumber {
            id,
          },
          oxygen::frame::Slot {
            1U,
          });
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
  auto service = PostProcessService(*renderer_,
    oxygen::observer_ptr {
      &loader,
    });
  const auto tag = oxygen::vortex::internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(tag,
    oxygen::frame::Slot {
      0U,
    });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  auto requested = oxygen::scene::ExposureSettings {};
  const auto initial = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  auto payload = oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  oxygen::data::pak::core::TextureResourceDesc descriptor {};
  std::memcpy(&descriptor, payload.data(), sizeof(descriptor));
  descriptor.format
    = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNormSRGB);
  std::memcpy(payload.data(), &descriptor, sizeof(descriptor));
  requested.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  requested.compensation_ev = 2.0F;
  static_cast<void>(service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested));
  auto queue = graphics_->GetCommandQueue(
    oxygen::graphics::SingleQueueStrategy().KeyFor(QueueRole::kTransfer));
  ASSERT_NE(queue, nullptr);
  queue->Signal(std::numeric_limits<std::uint64_t>::max());
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      2U,
    },
    oxygen::frame::Slot {
      1U,
    });
  renderer_->GetUploadCoordinator().OnFrameStart(tag,
    oxygen::frame::Slot {
      1U,
    });
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      3U,
    },
    oxygen::frame::Slot {
      2U,
    });
  const auto rejected = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
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
  const auto config = *ResolvedPostProcessConfig::Resolve(PostProcessConfig {
    .exposure = settings,
  });
  auto context = RenderContext {};
  context.frame_slot = oxygen::frame::Slot {
    0U,
  };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    1U,
  };
  context.current_view.view_id = ViewId {
    1U,
  };
  context.current_view.view_state_handle
    = oxygen::vortex::CompositionView::ViewStateHandle {
        1U,
      };
  auto previous = oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return pass.Execute(context, recorder, config, {});
    });
  ASSERT_TRUE(previous.executed);
  for (bool recording_failure : {
         false,
         true,
       }) {
    context.frame_sequence = oxygen::frame::SequenceNumber {
      context.frame_sequence.get() + 1U,
    };
    graphics_->SetFailSubmission(!recording_failure);
    graphics_->SetFailRecording(recording_failure);
    const auto failed = oxygen::graphics::testing::SubmitCommands(*graphics_,
      "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
        return pass.Execute(context, recorder, config, {});
      });
    EXPECT_FALSE(failed.executed);
    EXPECT_EQ(failed.state, nullptr);
    EXPECT_EQ(
      oxygen::vortex::testing::RendererPublicationProbe::ExposureStateForView(
        pass, context.current_view.view_state_handle),
      previous.state);
    graphics_->SetFailSubmission(false);
    graphics_->SetFailRecording(false);
    const auto retry = oxygen::graphics::testing::SubmitCommands(*graphics_,
      "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
        return pass.Execute(context, recorder, config, {});
      });
    EXPECT_TRUE(retry.executed);
    EXPECT_NE(retry.state, failed.state);
    const auto duplicate = oxygen::graphics::testing::SubmitCommands(*graphics_,
      "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
        return pass.Execute(context, recorder, config, {});
      });
    EXPECT_FALSE(duplicate.executed);
    EXPECT_EQ(duplicate.state, retry.state);
    previous = retry;
  }
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  CapturedSettingsStayAtomicAcrossLateModeAndBiasChanges)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto service = PostProcessService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  auto requested = oxygen::scene::ExposureSettings {};
  requested.key = 12.5F;
  requested.mode = oxygen::engine::ExposureMode::kManual;
  requested.manual_ev = 14.0F;
  const auto first = service.CaptureViewExposureSettings(
    ViewId {
      1U,
    },
    Handle {
      1U,
    },
    requested);
  requested.mode = oxygen::engine::ExposureMode::kAuto;
  requested.compensation_ev = 2.0F;
  const auto late = service.ResolveViewExposureSettings(
    Handle {
      1U,
    },
    requested);
  ASSERT_GT(late.revision, first.revision);
  // Repeated frame-start notifications must not unpin the captured revision.
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  const auto& pinned = service.CaptureViewExposureSettings(
    ViewId {
      1U,
    },
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(pinned.revision, first.revision);
  EXPECT_EQ(pinned.resolved.authored, first.resolved.authored);
  EXPECT_EQ(pinned.resolved.fixed_scale, 0x1p-14F);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      2U,
    },
    oxygen::frame::Slot {
      1U,
    });
  const auto& next = service.CaptureViewExposureSettings(
    ViewId {
      1U,
    },
    Handle {
      1U,
    },
    requested);
  EXPECT_EQ(next.revision, late.revision);
  EXPECT_EQ(next.resolved.authored, requested);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  OffscreenAndPublishedIdsDoNotAliasCapturedExposure)
{
  using View = oxygen::vortex::CompositionView;
  auto service = PostProcessService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  auto settings = oxygen::scene::ExposureSettings {};
  settings.mode = oxygen::engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  const auto owner = service.CaptureViewExposureSettings(
    ViewId {
      7U,
    },
    View::ViewStateHandle {
      90U,
    },
    settings);
  settings.manual_ev = 12.0F;
  const auto consumer = service.CaptureViewExposureSettings(
    ViewId {
      7U,
    },
    View::kInvalidViewStateHandle, settings);
  EXPECT_EQ(owner.resolved.fixed_scale, 0x1p-4F);
  EXPECT_EQ(consumer.resolved.fixed_scale, 0x1p-12F);
  service.RemoveViewState(
    ViewId {
      7U,
    },
    View::kInvalidViewStateHandle);
  EXPECT_EQ(service
              .CaptureViewExposureSettings(
                ViewId {
                  7U,
                },
                View::ViewStateHandle {
                  90U,
                },
                settings)
              .resolved.fixed_scale,
    0x1p-4F);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  StatelessCapturesAreIsolatedByLogicalViewAndRetainNoPriorFrame)
{
  using View = oxygen::vortex::CompositionView;
  auto service = PostProcessService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  auto requested = oxygen::scene::ExposureSettings {};
  requested.key = 12.5F;
  requested.mode = oxygen::engine::ExposureMode::kManual;
  requested.manual_ev = 14.0F;
  const auto first = service.CaptureViewExposureSettings(
    ViewId {
      1U,
    },
    View::kInvalidViewStateHandle, requested);
  requested.manual_ev = 16.0F;
  const auto second = service.CaptureViewExposureSettings(
    ViewId {
      2U,
    },
    View::kInvalidViewStateHandle, requested);
  EXPECT_EQ(first.resolved.fixed_scale, 0x1p-14F);
  EXPECT_EQ(second.resolved.fixed_scale, 0x1p-16F);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      2U,
    },
    oxygen::frame::Slot {
      1U,
    });
  requested.key = -1.0F;
  const auto& next = service.CaptureViewExposureSettings(
    ViewId {
      1U,
    },
    View::kInvalidViewStateHandle, requested);
  EXPECT_EQ(next.revision, 0U);
  EXPECT_EQ(next.resolved.authored.mode, oxygen::engine::ExposureMode::kAuto);
}

NOLINT_TEST_F(PostProcessServiceBehaviorTest,
  FailedSharedBootstrapNeverPublishesDormantConsumerHistory)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Policy = oxygen::vortex::ExposureTransitionPolicy;
  auto pass = oxygen::vortex::postprocess::ExposurePass(*renderer_);
  auto settings = oxygen::scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = oxygen::engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto config = *ResolvedPostProcessConfig::Resolve(PostProcessConfig {
    .exposure = settings,
  });
  auto context = RenderContext {};
  context.current_view.view_id = ViewId {
    2U,
  };
  context.current_view.view_state_handle = Handle {
    2U,
  };
  context.frame_slot = oxygen::frame::Slot {
    0U,
  };
  context.frame_sequence = oxygen::frame::SequenceNumber {
    1U,
  };
  const auto independent = oxygen::graphics::testing::SubmitCommands(*graphics_,
    "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
      return pass.Execute(context, recorder, config, {});
    });
  ASSERT_TRUE(independent.executed);
  settings.mode = oxygen::engine::ExposureMode::kAuto;
  const auto source_config
    = *ResolvedPostProcessConfig::Resolve(PostProcessConfig {
      .exposure = settings,
    });
  const auto seed = renderer_->QueueExposureTransition(
    Handle {
      1U,
    },
    Policy::kSeedFromEv100, 8.0F);
  if (!seed.has_value()) {
    FAIL() << "Expected seed to have a value";
  }
  auto source = oxygen::vortex::postprocess::ExposurePass::Source {};
  source.handle = Handle {
    1U,
  };
  source.config = source_config;
  source.transition = *seed;
  auto source_inputs = oxygen::vortex::postprocess::ExposurePass::Inputs {};
  source_inputs.source = &source;
  for (const bool recording : {
         false,
         true,
       }) {
    context.frame_sequence = oxygen::frame::SequenceNumber {
      context.frame_sequence.get() + 1U,
    };
    const auto committed_before
      = oxygen::vortex::testing::RendererPublicationProbe::ExposureStateForView(
        pass, context.current_view.view_state_handle);
    graphics_->SetFailRecording(recording);
    graphics_->SetFailSubmission(!recording);
    const auto failed = oxygen::graphics::testing::SubmitCommands(*graphics_,
      "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
        return pass.Execute(context, recorder, config, source_inputs);
      });
    EXPECT_FALSE(failed.executed);
    EXPECT_EQ(failed.state, nullptr);
    EXPECT_EQ(failed.exposure_buffer, nullptr);
    EXPECT_EQ(
      oxygen::vortex::testing::RendererPublicationProbe::ExposureStateForView(
        pass, context.current_view.view_state_handle),
      committed_before);
    graphics_->SetFailRecording(false);
    graphics_->SetFailSubmission(false);
    const auto retry = oxygen::graphics::testing::SubmitCommands(*graphics_,
      "Vortex test", [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
        return pass.Execute(context, recorder, config, source_inputs);
      });
    EXPECT_TRUE(retry.executed);
    EXPECT_TRUE(retry.borrowed_exposure);
    EXPECT_NE(retry.state, independent.state);
    const auto transition = renderer_->InspectExposureTransition(seed->target);
    if (!transition.has_value()) {
      FAIL() << "Expected transition to have a value";
    }
    EXPECT_EQ(
      transition->phase, oxygen::vortex::ExposureTransitionPhase::kQueued);
  }
}

NOLINT_TEST_F(
  PostProcessServiceBehaviorTest, ReusedLifetimeCannotRetainOldAcceptedSettings)
{
  using View = oxygen::vortex::CompositionView;
  auto service = PostProcessService(*renderer_);
  auto frame = oxygen::engine::FrameContext {};
  const auto target = MakeFramebuffer(graphics_, "ExposureLifetime");
  auto view = View {};
  view.id = ViewId {
    1U,
  };
  view.view_state_handle = View::ViewStateHandle {
    1U,
  };
  const auto publish = [&] -> ViewId {
    return renderer_->PublishRuntimeCompositionView(frame,
      { .composition_view = view,
        .render_target = oxygen::observer_ptr { target.get(), }, });
  };
  auto settings = oxygen::scene::ExposureSettings {};
  settings.mode = oxygen::engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 14.0F;
  const auto first_view = publish();
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  const auto first = service.CaptureViewExposureSettings(
    first_view, view.view_state_handle, settings);
  ASSERT_EQ(first.revision, 1U);
  renderer_->RemovePublishedRuntimeView(frame, view.id);
  const auto next_view = publish();
  service.OnFrameStart(
    oxygen::frame::SequenceNumber {
      2U,
    },
    oxygen::frame::Slot {
      1U,
    });
  settings.key = -1.0F;
  const auto next = service.CaptureViewExposureSettings(
    next_view, view.view_state_handle, settings);
  EXPECT_NE(next.lifetime, first.lifetime);
  EXPECT_EQ(next.revision, 0U);
  EXPECT_EQ(next.resolved.authored.mode, oxygen::engine::ExposureMode::kAuto);
}

} // namespace
