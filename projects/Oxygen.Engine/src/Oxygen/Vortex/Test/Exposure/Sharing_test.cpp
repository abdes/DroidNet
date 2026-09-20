//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <optional>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {

using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::TextureDesc;

NOLINT_TEST_F(
  ExposureGpuTest, SharedConsumersUsePriorOwnerStateInBothRenderOrders)
{
  const auto owner_signal = Uniform(.25F);
  const auto consumer_signal = Uniform(8.0F);
  const auto config = SharedConfig();
  auto source = postprocess::ExposurePass::Source {};
  source.handle = CompositionView::ViewStateHandle {
    1U,
  };
  source.config = config;
  for (bool consumer_first : {
         false,
         true,
       }) {
    pass_->RemoveViewState(CompositionView::ViewStateHandle {
      1U,
    });
    pass_->RemoveViewState(CompositionView::ViewStateHandle {
      2U,
    });
    for (unsigned frame_index = 0; frame_index < 2U; ++frame_index) {
      ctx_.frame_sequence = frame::SequenceNumber {
        ++sequence_,
      };
      ctx_.delta_time = 0.0F;
      postprocess::ExposurePass::Result owner;
      postprocess::ExposurePass::Result consumer;
      const auto render_owner = [&] -> void {
        ctx_.current_view.view_id = ViewId {
          1U,
        };
        ctx_.current_view.view_state_handle = source.handle;
        owner = RecordShared(owner_signal, config);
      };
      const auto render_consumer = [&] -> void {
        ctx_.current_view.view_id = ViewId {
          2U,
        };
        ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
          2U,
        };
        consumer = RecordShared(consumer_signal, config, &source);
      };
      if (consumer_first) {
        render_consumer();
        render_owner();
      } else {
        render_owner();
        render_consumer();
      }
      ASSERT_TRUE(owner.executed);
      ASSERT_TRUE(consumer.executed);
      EXPECT_EQ(consumer.histogram_buffer, nullptr);
      EXPECT_NE(owner.state, consumer.state);
      EXPECT_NEAR(ReadState(owner).displayed_scale, .72F, 2e-5F);
      const auto borrowed = ReadState(consumer);
      EXPECT_NEAR(
        borrowed.displayed_scale, frame_index == 0U ? 1.0F : .72F, 2e-5F);
      EXPECT_EQ(borrowed.flags & (4U | 8U | 512U), 0U);
      EXPECT_NE(borrowed.flags & 128U, 0U);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, SharedSourceResetBecomesVisibleOnTheNextFrame)
{
  const auto signal = Uniform(.25F);
  const auto config = SharedConfig();
  auto source = postprocess::ExposurePass::Source {};
  source.handle = CompositionView::ViewStateHandle {
    1U,
  };
  source.config = config;
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  const auto first = RecordShared(signal, config);
  ASSERT_TRUE(first.executed);
  const auto seed = renderer_->QueueExposureTransition(
    source.handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  if (!seed.has_value()) {
    FAIL() << "Expected seed to contain a value";
  }
  source.transition = *seed;
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  const auto reset = RecordShared(signal, config, nullptr, *seed);
  ctx_.current_view.view_id = ViewId {
    2U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    2U,
  };
  const auto same_frame = RecordShared(signal, config, &source);
  EXPECT_EQ(ReadState(reset).displayed_scale, 0x1p-8F);
  EXPECT_NEAR(ReadState(same_frame).displayed_scale, .72F, 2e-5F);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  // The source is inactive in this frame; retain its last completed
  // publication.
  const auto next = RecordShared(signal, config, &source);
  EXPECT_EQ(ReadState(next).displayed_scale, 0x1p-8F);
}

NOLINT_TEST_F(ExposureGpuTest, SharedBootstrapUsesSourceModeBoundsAndSeed)
{
  const auto signal = Uniform(8.0F);
  auto consumer_settings = scene::ExposureSettings {};
  consumer_settings.enabled = false;
  const auto consumer_config = SharedConfig(consumer_settings);
  auto settings = scene::ExposureSettings {};
  settings.min_ev = 4.0F;
  auto source = postprocess::ExposurePass::Source {};
  source.handle = CompositionView::ViewStateHandle {
    1U,
  };
  source.config = SharedConfig(settings);
  ctx_.current_view.view_id = ViewId {
    2U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    2U,
  };
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  const auto initial = RecordShared(signal, consumer_config, &source);
  EXPECT_EQ(ReadState(initial).displayed_scale, 0x1p-4F);
  EXPECT_EQ(ReadState(initial).fallback_reason, 3U);
  const auto seed = renderer_->QueueExposureTransition(
    source.handle, ExposureTransitionPolicy::kSeedFromEv100, -2.0F);
  if (!seed.has_value()) {
    FAIL() << "Expected seed to contain a value";
  }
  source.transition = *seed;
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    4.0F);
  source.transition.reset();
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    0x1p-14F);
  settings.mode = engine::ExposureMode::kManualCamera;
  source.config
    = SharedConfig(settings, static_cast<float>(std::log2(15125.0)));
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  EXPECT_NEAR(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    1.0 / 15125.0, 2e-5 / 15125.0);
  settings.enabled = false;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    1.0F);
  settings.enabled = true;
  settings.mode = engine::ExposureMode::kAuto;
  settings.target_luminance = 0.0F;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  const auto zero = ReadState(RecordShared(signal, consumer_config, &source));
  EXPECT_EQ(zero.displayed_scale, 0.0F);
  EXPECT_EQ(zero.latent_scale, 0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharedConsumerTransitionIsRejectedAfterGpuCompletion)
{
  auto service = PostProcessService(*renderer_);
  ctx_.current_view.view_id = ViewId {
    2U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    2U,
  };
  ctx_.current_view.exposure_view_id = ViewId {
    1U,
  };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle {
        1U,
      };
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  if (!token.has_value()) {
    FAIL() << "Expected token to contain a value";
  }
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto capture = BeginOptionalCapture();
  auto pixel_options = ServicePixelOptions {};
  pixel_options.before_execute = [&] -> void {
    auto source = scene::ExposureSettings {};
    source.key = 12.5F;
    source.mode = engine::ExposureMode::kManual;
    source.manual_ev = 4.0F;
    static_cast<void>(service.CaptureViewExposureSettings(
      ViewId {
        1U,
      },
      CompositionView::ViewStateHandle {
        1U,
      },
      source));
  };
  const auto pixel = ServicePixel(service, signal, {}, pixel_options);
  EXPECT_NEAR(pixel, .25F / 16.0F, 2e-5F);
  EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_executed);
  service.OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  if (!status.has_value()) {
    FAIL() << "Expected status to contain a value";
  }
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kSharedConsumer);
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  RegisteredInactiveSourceBootstrapsConsumersWithoutConsumingItsSeed)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto texture = CreateRegisteredTexture(TextureDesc {
    .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(texture));
  auto view = CompositionView {};
  view.id = ViewId {
    50U,
  };
  view.view_state_handle = CompositionView::ViewStateHandle {
    50U,
  };
  view.render_settings.exposure = scene::ExposureSettings {};
  view.render_settings.exposure->key = 12.5F;
  const auto published = renderer_->PublishRuntimeCompositionView(frame,
    { .composition_view = view,
      .render_target = observer_ptr { target.get(), }, });
  ASSERT_NE(published, kInvalidViewId);
  const auto token = renderer_->QueueExposureTransition(
    view.view_state_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  if (!token.has_value()) {
    FAIL() << "Expected token to contain a value";
  }
  ctx_.current_view.view_id = ViewId {
    900U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    900U,
  };
  ctx_.current_view.exposure_view_id = published;
  ctx_.current_view.exposure_view_state_handle = view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F / 256.0F, 2e-5F);
  service.OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    ctx_.frame_slot);
  EXPECT_EQ(InspectRequiredTransition(token->target).phase,
    ExposureTransitionPhase::kQueued);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharingDiscardsDormantMeterHistoryAndExplicitDetachRemeters)
{
  ctx_.current_view.view_id = ViewId {
    2U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    2U,
  };
  const auto before = Run(Uniform(.25F));
  EXPECT_NEAR(before.state.displayed_scale, .72F, 2e-5F);
  const auto config = SharedConfig();
  auto source = postprocess::ExposurePass::Source {};
  source.handle = CompositionView::ViewStateHandle {
    1U,
  };
  source.config = config;
  const auto dark = Uniform(8.0F);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  const auto shared = ReadState(RecordShared(dark, config, &source));
  EXPECT_EQ(shared.displayed_scale, 1.0F);
  EXPECT_EQ(shared.flags & (4U | 8U | 512U), 0U);
  const auto remeter = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  if (!remeter.has_value()) {
    FAIL() << "Expected remeter to contain a value";
  }
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  ctx_.delta_time = 0.0F;
  const auto detached
    = ReadState(RecordShared(dark, config, nullptr, *remeter));
  EXPECT_NEAR(detached.displayed_scale, .0225F, 2e-5F);
  EXPECT_EQ(detached.applied_generation.at(0), remeter->generation);
  EXPECT_EQ(detached.flags & 128U, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveSharingConsumerRequestStaysRejectedAfterDetach)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(frame,
    ViewId {
      50U,
    },
    CompositionView::ViewStateHandle {
      50U,
    },
    settings);
  const auto handle = CompositionView::ViewStateHandle {
    60U,
  };
  const auto view = PublishExposureOwner(frame,
    ViewId {
      60U,
    },
    handle, settings,
    ViewId {
      50U,
    });
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  if (!token.has_value()) {
    FAIL() << "Expected token to contain a value";
  }
  service.OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    ctx_.frame_slot);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(InspectRequiredTransition(handle).error,
    ExposureTransitionError::kSharedConsumer);
  PublishExposureOwner(frame,
    ViewId {
      60U,
    },
    handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U), settings), .18F, 2e-5F);
  EXPECT_EQ(renderer_->RetryExposureTransition(*token),
    ExposureTransitionPhase::kSuperseded);
}

NOLINT_TEST_F(
  ExposureGpuTest, AutoDetachRemetersAtZeroDeltaWithOneImplicitGeneration)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(frame,
    ViewId {
      60U,
    },
    ctx_.current_view.view_state_handle, settings);
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  const auto event
    = renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle);
  if (!event.has_value()) {
    FAIL() << "Expected event to contain a value";
  }
  EXPECT_EQ(event->request.policy, ExposureTransitionPolicy::kRemeter);
  EXPECT_GT(event->request.generation, 0U);
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F * .0225F, 2e-5F);
  const auto after = InspectRequiredTransition(event->request.target);
  EXPECT_EQ(after.request, event->request);
  EXPECT_EQ(after.phase, ExposureTransitionPhase::kApplied);
}

NOLINT_TEST_F(ExposureGpuTest, ExplicitDetachPolicyOverridesDefaultRemeter)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  for (const auto policy : {
         ExposureTransitionPolicy::kPreserve,
         ExposureTransitionPolicy::kSeedFromEv100,
       }) {
    const auto consumer = StartSharedServiceView(service, frame);
    const auto token = renderer_->QueueExposureTransition(
      ctx_.current_view.view_state_handle, policy,
      policy == ExposureTransitionPolicy::kSeedFromEv100
        ? std::optional { 8.0F, }
        : std::nullopt);
    if (!token.has_value()) {
      FAIL() << "Expected token to contain a value";
    }
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    PublishExposureOwner(frame,
      ViewId {
        60U,
      },
      token->target, settings);
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle = token->target;
    EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)),
      policy == ExposureTransitionPolicy::kPreserve ? .5F : 8.0F / 256.0F,
      2e-5F);
    EXPECT_EQ(InspectRequiredTransition(token->target).request, *token);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FixedModeDetachAppliesAuthoredGainWithoutAnAutoRejection)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  for (const bool enabled : {
         true,
         false,
       }) {
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = 8.0F;
    settings.enabled = enabled;
    const auto consumer = StartSharedServiceView(service, frame, settings);
    PublishExposureOwner(frame,
      ViewId {
        60U,
      },
      ctx_.current_view.view_state_handle, settings);
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle
      = ctx_.current_view.view_state_handle;
    EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
      enabled ? .25F / 256.0F : .25F, 2e-5F);
    service.OnFrameStart(
      frame::SequenceNumber {
        ++sequence_,
      },
      ctx_.frame_slot);
    const auto status = renderer_->InspectExposureTransition(
      ctx_.current_view.view_state_handle);
    if (!status.has_value()) {
      FAIL() << "Expected status to contain a value";
    }
    EXPECT_EQ(status->phase, ExposureTransitionPhase::kApplied);
    EXPECT_EQ(status->request.policy, ExposureTransitionPolicy::kPreserve);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticDetachDefersTheImplicitEventUntilNormalRendering)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(frame,
    ViewId {
      60U,
    },
    ctx_.current_view.view_state_handle, settings);
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  {
    auto pixel_options = ServicePixelOptions {};
    pixel_options.diagnostic = true;
    EXPECT_NEAR(
      ServicePixel(service, Uniform(.25F, 4U, 4U), settings, pixel_options),
      .25F, 2e-5F);
  }
  EXPECT_FALSE(
    renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle)
      .has_value());
  EXPECT_NEAR(
    ServicePixel(service, Uniform(8.0F, 4U, 4U), settings), .18F, 2e-5F);
  EXPECT_EQ(InspectRequiredTransition(ctx_.current_view.view_state_handle)
              .request.policy,
    ExposureTransitionPolicy::kRemeter);
}

NOLINT_TEST_F(
  ExposureGpuTest, ReusedLifetimeCannotConsumeOldOwnOrSharedPriorState)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto signal = Uniform(.25F);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  const auto old
    = RecordShared(signal, SharedConfig(settings), nullptr, {}, 100U);
  EXPECT_EQ(ReadState(old).displayed_scale, 0x1p-4F);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  pass_->OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  const auto config = SharedConfig();
  const auto new_owner = RecordShared(signal, config, nullptr, {}, 101U);
  EXPECT_NEAR(ReadState(new_owner).displayed_scale, .72F, 2e-5F);
  auto source = postprocess::ExposurePass::Source {};
  source.handle = CompositionView::ViewStateHandle {
    1U,
  };
  source.config = config;
  source.lifetime = 101U;
  ctx_.current_view.view_id = ViewId {
    2U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    2U,
  };
  const auto consumer = RecordShared(signal, config, &source, {}, 200U);
  EXPECT_EQ(ReadState(consumer).displayed_scale, 1.0F);
  EXPECT_EQ(consumer.state->owner_lifetime, 200U);
}

NOLINT_TEST_F(ExposureGpuTest, CanceledDetachDoesNotIssueAnImplicitGeneration)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto handle = ctx_.current_view.view_state_handle;
  PublishExposureOwner(frame,
    ViewId {
      60U,
    },
    handle, settings);
  PublishExposureOwner(frame,
    ViewId {
      60U,
    },
    handle, settings,
    ViewId {
      50U,
    });
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .5F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
}

NOLINT_TEST_F(
  ExposureGpuTest, OffscreenFacadeResolvesSharedRootDespitePublishedIdCollision)
{
  CheckOffscreenSharing(false);
}

NOLINT_TEST_F(ExposureGpuTest, OffscreenFacadeSharesRootInsideFrame)
{
  CheckOffscreenSharing(true);
}

} // namespace oxygen::vortex::testing::exposure
