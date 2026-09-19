//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <memory>
#include <optional>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Internal/PreviousViewHistoryCache.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(ExposureGpuTest, ManualCameraAndDisabledWriteUnifiedGpuState)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  const auto manual = Run(Signal {}, settings);
  EXPECT_EQ(manual.state.displayed_scale, 0x1p-14F);
  EXPECT_EQ(manual.state.latent_scale, 0x1p-14F);
  EXPECT_EQ(manual.state.flags & 12U, 0U);
  settings.mode = engine::ExposureMode::kManualCamera;
  const auto camera = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, true, {},
    static_cast<float>(std::log2(15125.0)));
  EXPECT_NEAR(camera.state.displayed_scale, 1.0 / 15125.0, 2e-5 / 15125.0);
  EXPECT_EQ((camera.state.flags >> 10U) & 3U, 1U);
  settings.enabled = false;
  const auto disabled = Run(Signal {}, settings);
  EXPECT_EQ(disabled.state.displayed_scale, 1.0F);
  EXPECT_EQ(disabled.state.latent_scale, 1.0F);
  EXPECT_EQ((disabled.state.flags >> 10U) & 3U, 3U);
}

NOLINT_TEST_F(
  ExposureGpuTest, ManualToAutoPreservesGainForTransitionFrameThenAdapts)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto manual = Run(Uniform(.25F), settings);
  settings.mode = engine::ExposureMode::kAuto;
  const auto transition = Run(Uniform(.25F), settings, 1.0F);
  EXPECT_EQ(transition.state.displayed_scale, manual.state.displayed_scale);
  EXPECT_NEAR(transition.state.target_scale, .72F, 2e-5);
  const auto next = Run(Uniform(.25F), settings, 1.0F);
  EXPECT_NEAR(next.state.displayed_scale, .125F, 2e-5);
}

NOLINT_TEST_F(
  ExposureGpuTest, SeedUsesRequestedEvAndDuplicateGenerationDoesNotReapply)
{
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto event = Run(Uniform(.25F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(event.state.displayed_scale, 0x1p-8F);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next = Run(Uniform(.25F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(next.state.displayed_scale, 0x1p-7F, 2e-5);
  EXPECT_EQ(next.state.applied_generation, event.state.applied_generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemeterRemainsPendingWithoutInputAndAppliesAtZeroDelta)
{
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(token.has_value());
  const auto invalid = Run(Signal {}, {}, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(invalid.state.applied_generation[0], 0U);
  EXPECT_EQ(invalid.state.requested_generation[0], token->generation);
  const auto applied
    = Run(Uniform(.25F), {}, 0.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(applied.state.displayed_scale, .72F, 2e-5);
  EXPECT_EQ(applied.state.applied_generation[0], token->generation);
  const auto retry = Run(Uniform(8.0F), {}, 0.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(retry.state.displayed_scale, applied.state.displayed_scale);
}

NOLINT_TEST_F(
  ExposureGpuTest, RejectedManualSeedDoesNotReactivateOnLaterAutoEntry)
{
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto rejected
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(rejected.state.applied_generation[0], 0U);
  EXPECT_NE(rejected.state.flags & (1U << 12U), 0U);
  settings.mode = engine::ExposureMode::kAuto;
  const auto next
    = Run(Uniform(.25F), settings, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(next.state.displayed_scale, 0x1p-4F);
  EXPECT_EQ(next.state.applied_generation[0], 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, StatelessAutoSolvesEachInvocationWithoutAdaptation)
{
  ctx_.current_view.view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  const auto first = Run(Uniform(.25F), {}, 0.0F);
  const auto next = Run(Uniform(8.0F), {}, 0.0F);
  EXPECT_NEAR(first.state.displayed_scale, .72F, 2e-5);
  EXPECT_NEAR(next.state.displayed_scale, .0225F, 2e-5);
}

NOLINT_TEST_F(ExposureGpuTest, SceneAndDirectSettingsUseIdenticalGainsAndRates)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.speed_up = .875F;
  settings.speed_down = .625F;
  const auto initial = Uniform(.25F, 4U, 4U);
  const auto bright = Uniform(.5F, 4U, 4U);
  const auto dark = Uniform(.015625F, 4U, 4U);
  const auto compare = [&](const Signal& signal, float luminance, float dt) {
    const auto direct = Run(signal, settings, dt);
    const auto pixel = ServicePixel(service, signal, settings,
      ServicePixelOptions { .delta_time_seconds = dt });
    EXPECT_NEAR(pixel, luminance * direct.state.displayed_scale, 2e-5F);
  };
  compare(initial, .25F, 0.0F);
  compare(bright, .5F, .25F);
  compare(dark, .015625F, .25F);
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  compare(bright, .5F, 0.0F);
  EXPECT_NEAR(ServicePixel(service, bright, settings), 0x1p-15F, 1e-7F);
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticUnitStatePreservesAutoHistoryAndPendingSeed)
{
  const auto before = Run(Uniform(.25F));
  const auto persistent = last_state_;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  for (unsigned i = 0U; i < 3U; ++i) {
    const auto diagnostic
      = Run(Uniform(8.0F), {}, 5.0F, nullptr, 1.0F, true, *token, {}, true);
    EXPECT_EQ(diagnostic.state.displayed_scale, 1.0F);
    EXPECT_EQ(diagnostic.state.applied_generation[0], 0U);
    const auto retained = Read<ExposureStateData>(
      *persistent->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(retained.displayed_scale, before.state.displayed_scale);
    EXPECT_EQ(retained.applied_generation, before.state.applied_generation);
  }
  const auto resumed
    = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(resumed.state.displayed_scale, 0x1p-8F);
  const auto next = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(next.state.displayed_scale, 0x1p-7F, 2e-5);
}

NOLINT_TEST_F(ExposureGpuTest, DiagnosticUnitStateDoesNotOverwriteManualHistory)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto before = Run(Signal {}, settings);
  const auto persistent = last_state_;
  const auto diagnostic
    = Run(Signal {}, settings, 5.0F, nullptr, 1.0F, true, {}, {}, true);
  EXPECT_EQ(diagnostic.state.displayed_scale, 1.0F);
  EXPECT_EQ(Read<ExposureStateData>(
              *persistent->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    before.state.displayed_scale);
  EXPECT_EQ(Run(Signal {}, settings).state.displayed_scale, 0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, ServiceDiagnosticFramesDoNotAcknowledgePendingTransition)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5);
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  for (unsigned i = 0U; i < 3U; ++i) {
    EXPECT_NEAR(
      ServicePixel(service, signal, {},
        ServicePixelOptions { .diagnostic = true, .delta_time_seconds = 3.0F }),
      .25F, 2e-5);
    EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_requested);
    EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_executed);
    ASSERT_NE(service.InspectBindings(ctx_.current_view.view_id), nullptr);
    EXPECT_EQ(
      service.InspectBindings(ctx_.current_view.view_id)->enable_auto_exposure,
      0U);
    EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
      ExposureTransitionPhase::kQueued);
  }
  EXPECT_NEAR(ServicePixel(service, signal, {},
                ServicePixelOptions { .delta_time_seconds = 1.0F }),
    .25F / 256.0F, 2e-5);
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(status->applied_generation, token->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, CompletedOldGenerationCannotConsumeNewerQueuedTransition)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto first
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(first.has_value());
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5);
  const auto second = renderer_->QueueExposureTransition(
    first->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(second.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto intermediate = renderer_->InspectExposureTransition(first->target);
  ASSERT_TRUE(intermediate.has_value());
  EXPECT_EQ(intermediate->request.generation, second->generation);
  EXPECT_EQ(intermediate->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(intermediate->applied_generation, first->generation);
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(first->target)->applied_generation,
    second->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, TransitionQueuedAfterFrameCaptureWaitsForNextFrame)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  std::optional<ExposureTransitionToken> token;
  const auto current = ServicePixel(
    service, signal, {}, ServicePixelOptions { .before_execute = [&] {
      const auto issued = renderer_->QueueExposureTransition(
        ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
      CHECK_F(issued.has_value());
      token = *issued;
    } });
  EXPECT_NEAR(current, .18F, 2e-5);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5);
}

NOLINT_TEST_F(
  ExposureGpuTest, RetiredViewAcknowledgementCannotApplyToReusedHandle)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto first
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(first.has_value());
  ServicePixel(service, signal);
  service.RemoveViewState(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle);
  const auto next = renderer_->QueueExposureTransition(
    first->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(next.has_value());
  EXPECT_NE(next->lifetime, first->lifetime);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(next->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(status->applied_generation, 0U);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*first).has_value());
}

NOLINT_TEST_F(ExposureGpuTest, PreserveHoldsOnlyItsEventFrame)
{
  const auto before = Run(Uniform(.25F));
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  const auto event = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(event.state.displayed_scale, before.state.displayed_scale);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(
    next.state.displayed_scale, before.state.displayed_scale / 8.0F, 2e-5F);
}

NOLINT_TEST_F(ExposureGpuTest, SeedOutsideLockedRangeOwnsOnlyItsEventFrame)
{
  auto settings = scene::ExposureSettings {};
  settings.min_ev = settings.max_ev = 4.0F;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(token.has_value());
  const auto event
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(event.state.displayed_scale, 0x1p-12F);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(next.state.displayed_scale, 0x1p-4F);
}

NOLINT_TEST_F(ExposureGpuTest, ServiceManualConsumesExactGpuGain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(4096.0F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (const auto ev : { 14.0F, 16.0F, 32.0F }) {
    settings.manual_ev = ev;
    const auto expected = std::exp2(12.0F - ev);
    EXPECT_NEAR(
      ServicePixel(service, signal, settings), expected, expected * 2e-5F);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, CompletedUnsupportedSeedRejectsWithoutChangingGain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto before = ServicePixel(service, signal);
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 1000.0F);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(ServicePixel(service, signal), before);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kUnsupportedSeed);
  EXPECT_EQ(status->applied_generation, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, LateModeChangeCannotAlterCapturedTransitionSemantics)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto pixel = ServicePixel(
    service, signal, settings, ServicePixelOptions { .before_execute = [&] {
      settings.key = 12.5F;
      settings.mode = engine::ExposureMode::kAuto;
      const auto& late = service.ResolveViewExposureSettings(
        ctx_.current_view.view_state_handle, settings);
      auto config = service.GetConfig();
      config.exposure = late.resolved.authored;
      service.SetConfig(config);
    } });
  EXPECT_NEAR(pixel, .25F / 16.0F, 2e-5F);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kNotAuto);
  EXPECT_NEAR(ServicePixel(service, signal, settings,
                ServicePixelOptions { .delta_time_seconds = 1.0F }),
    .25F / 16.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveInvalidRequestsCannotReactivateAfterSettingsChange)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto signal = Uniform(.25F, 4U, 4U);
  for (unsigned kind = 0U; kind < 3U; ++kind) {
    const auto handle = CompositionView::ViewStateHandle { 50U + kind };
    const auto intent = ViewId { 50U + kind };
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    settings.mode = kind == 0U ? engine::ExposureMode::kManual
                               : engine::ExposureMode::kAuto;
    settings.enabled = kind != 1U;
    const auto view = PublishExposureOwner(frame, intent, handle, settings);
    ASSERT_NE(view, kInvalidViewId);
    const auto token = renderer_->QueueExposureTransition(handle,
      ExposureTransitionPolicy::kSeedFromEv100, kind == 2U ? 1000.0F : 8.0F);
    ASSERT_TRUE(token.has_value());
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    service.CaptureRegisteredExposureControls(ctx_);
    const auto rejected = renderer_->InspectExposureTransition(handle);
    ASSERT_TRUE(rejected.has_value());
    EXPECT_EQ(rejected->phase, ExposureTransitionPhase::kRejected);
    EXPECT_EQ(rejected->error,
      kind == 2U ? ExposureTransitionError::kUnsupportedSeed
                 : ExposureTransitionError::kNotAuto);
    settings.enabled = true;
    settings.mode = engine::ExposureMode::kAuto;
    if (kind == 2U) {
      // The formerly unsupported seed would now produce gain one; the locked
      // target instead produces 1/16. A rejected generation must remain
      // rejected.
      settings.compensation_ev = 1000.0F;
      settings.min_ev = settings.max_ev = 1004.0F;
    }
    ASSERT_EQ(PublishExposureOwner(frame, intent, handle, settings), view);
    ctx_.current_view.view_id = view;
    ctx_.current_view.view_state_handle = handle;
    EXPECT_NEAR(ServicePixel(service, signal, settings),
      kind == 2U ? .25F / 16.0F : .18F, 2e-5F);
    const auto state
      = vortex::testing::RendererPublicationProbe::ExposureStateForView(
        service, handle);
    ASSERT_NE(state, nullptr);
    const auto gpu = Read<ExposureStateData>(
      *state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(gpu.requested_generation[0], token->generation);
    EXPECT_EQ(gpu.applied_generation[0], 0U);
    EXPECT_NE(gpu.flags & (1U << 12U), 0U);
    EXPECT_EQ(renderer_->RetryExposureTransition(*token),
      ExposureTransitionPhase::kRejected);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveModeValidationCannotRejectAnObservedSubmission)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto handle = CompositionView::ViewStateHandle { 50U };
  const auto view
    = PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  Run(Uniform(.25F), settings, 0.0F, nullptr, 1.0F, true, *token);
  service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
    service, *token, last_state_, ctx_, sequence_);
  settings.mode = engine::ExposureMode::kManual;
  PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  WaitForQueueIdle();
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kApplied);
}

NOLINT_TEST_F(ExposureGpuTest, InactiveDiagnosticOwnerPreservesPendingRequest)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  const auto handle = CompositionView::ViewStateHandle { 50U };
  const auto view = PublishExposureOwner(
    frame, ViewId { 50U }, handle, settings, kInvalidViewId, true);
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  settings.mode = engine::ExposureMode::kAuto;
  PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, CameraCutRemetersOnceAndInvalidatesOnlyItsCameraHistory)
{
  auto service = PostProcessService(*renderer_);
  const auto handle = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  auto& history
    = vortex::testing::RendererPublicationProbe::PreviousViewHistory(
      *renderer_);
  auto state = internal::PreviousViewHistoryCache::CurrentState {};
  state.viewport = { .width = 4.0F, .height = 4.0F };
  history.BeginFrame(1U, {});
  history.TouchCurrent(handle, state);
  history.TouchCurrent(CompositionView::ViewStateHandle { 99U }, state);
  history.EndFrame();
  history.BeginFrame(2U, {});
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_FALSE(history.TouchCurrent(handle, state).previous_valid);
  EXPECT_TRUE(
    history.TouchCurrent(CompositionView::ViewStateHandle { 99U }, state)
      .previous_valid);
  const auto event = renderer_->InspectExposureTransition(handle);
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->request.policy, ExposureTransitionPolicy::kRemeter);
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F * .0225F, 2e-5F);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(handle)->request, event->request);
}

NOLINT_TEST_F(ExposureGpuTest, ExplicitSeedOverridesCameraCutDefaultPolicy)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  const auto seed = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F / 256.0F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, *seed);
}

NOLINT_TEST_F(ExposureGpuTest,
  BorrowingCameraCutPreservesRootExposureWithoutRequestingReset)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  StartSharedServiceView(service, frame);
  const auto consumer = ctx_.current_view.view_state_handle;
  const auto root_before
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, CompositionView::ViewStateHandle { 50U });
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(consumer, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .5F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(consumer).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, CompositionView::ViewStateHandle { 50U }),
    root_before);
}

NOLINT_TEST_F(
  ExposureGpuTest, WorldReplacementRemetersButOrdinaryImageChangesAdapt)
{
  auto service = PostProcessService(*renderer_);
  auto first = std::make_shared<scene::Scene>("FirstExposureWorld", 4U);
  auto second = std::make_shared<scene::Scene>("SecondExposureWorld", 4U);
  ctx_.scene = observer_ptr { first.get() };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), 1.0F, 2e-5F);
  ctx_.scene = observer_ptr { second.get() };
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle)
      ->request.policy,
    ExposureTransitionPolicy::kRemeter);
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticFramesDeferCameraCutUntilNormalExposureResumes)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), {},
                ServicePixelOptions { .diagnostic = true }),
    .25F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
}

NOLINT_TEST_F(ExposureGpuTest,
  RegisteredDebugOverrideControlsCameraCutsIndependentlyOfGlobalMode)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto normal_handle = CompositionView::ViewStateHandle { 81U };
  const auto debug_handle = CompositionView::ViewStateHandle { 82U };
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto normal = PublishExposureOwner(frame, ViewId { 81U }, normal_handle,
    settings, kInvalidViewId, false, ShaderDebugMode::kDisabled);
  auto debug = PublishExposureOwner(frame, ViewId { 82U }, debug_handle,
    settings, kInvalidViewId, false, ShaderDebugMode::kDisabled);
  const auto dim = Uniform(.25F, 4U, 4U);
  const auto bright = Uniform(8.0F, 4U, 4U);
  const auto run = [&](ViewId id, CompositionView::ViewStateHandle handle,
                     const Signal& signal, bool diagnostic) {
    ctx_.current_view.view_id = id;
    ctx_.current_view.view_state_handle = handle;
    ctx_.shader_debug_mode = diagnostic ? ShaderDebugMode::kWorldNormals
                                        : ShaderDebugMode::kDisabled;
    return ServicePixel(service, signal, settings,
      ServicePixelOptions {
        .diagnostic = diagnostic, .start_new_frame = false });
  };
  const auto capture_frame = [&] {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    ctx_.shader_debug_mode = ShaderDebugMode::kWorldNormals;
    ctx_.render_mode = RenderMode::kSolid;
    service.CaptureRegisteredExposureControls(ctx_);
  };
  renderer_->SetShaderDebugMode(ShaderDebugMode::kWorldNormals);
  capture_frame();
  EXPECT_NEAR(run(normal, normal_handle, dim, false), .18F, 2e-5F);
  EXPECT_NEAR(run(debug, debug_handle, dim, false), .18F, 2e-5F);

  ASSERT_EQ(PublishExposureOwner(frame, ViewId { 82U }, debug_handle, settings,
              kInvalidViewId, false, ShaderDebugMode::kWorldNormals),
    debug);
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(normal_handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(debug_handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  capture_frame();
  EXPECT_NEAR(run(normal, normal_handle, bright, false), .18F, 2e-5F);
  EXPECT_NEAR(run(debug, debug_handle, dim, true), .25F, 2e-5F);
  EXPECT_TRUE(renderer_->InspectExposureTransition(normal_handle).has_value());
  EXPECT_FALSE(renderer_->InspectExposureTransition(debug_handle).has_value());

  ASSERT_EQ(PublishExposureOwner(frame, ViewId { 82U }, debug_handle, settings,
              kInvalidViewId, false, ShaderDebugMode::kDisabled),
    debug);
  capture_frame();
  EXPECT_NEAR(run(debug, debug_handle, bright, false), .18F, 2e-5F);
  EXPECT_TRUE(renderer_->InspectExposureTransition(debug_handle).has_value());
}

NOLINT_TEST_F(
  ExposureGpuTest, LateCameraCutWaitsForNextCaptureAndRecoveryRemeters)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U), {},
                ServicePixelOptions { .before_execute =
                                        [&] {
                                          EXPECT_TRUE(renderer_
                                              ->NotifyViewDiscontinuity(handle,
                                                ViewDiscontinuity::kCameraCut)
                                              .has_value());
                                        } }),
    1.0F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  const auto cut
    = renderer_->InspectExposureTransition(handle)->request.generation;
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_GT(
    renderer_->InspectExposureTransition(handle)->request.generation, cut);
}

NOLINT_TEST_F(
  ExposureGpuTest, PublishingADifferentCameraTriggersTheDefaultCutPolicy)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto scene = std::make_shared<scene::Scene>("CameraSelection", 8U);
  auto first = scene->CreateNode("First");
  auto second = scene->CreateNode("Second");
  ASSERT_TRUE(first.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
  ASSERT_TRUE(
    second.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
  auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(texture));
  auto view = CompositionView {};
  view.id = ViewId { 50U };
  view.view_state_handle = CompositionView::ViewStateHandle { 50U };
  view.view.viewport = { .width = 4.0F, .height = 4.0F };
  view.camera = first;
  const auto publish = [&] {
    return renderer_->PublishRuntimeCompositionView(frame,
      { .composition_view = view,
        .render_target = observer_ptr { target.get() } });
  };
  const auto id = publish();
  ctx_.scene = observer_ptr { scene.get() };
  ctx_.current_view.view_id = id;
  ctx_.current_view.view_state_handle = view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  view.camera = second;
  ASSERT_EQ(publish(), id);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(view.view_state_handle)
              ->request.policy,
    ExposureTransitionPolicy::kRemeter);
}

} // namespace oxygen::vortex::testing::exposure
