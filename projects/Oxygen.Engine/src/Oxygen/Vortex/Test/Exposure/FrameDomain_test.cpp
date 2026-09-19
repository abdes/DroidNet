//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(
  ExposureGpuTest, FrameResolvePinsManualGainAndDistinctInFlightRecords)
{
  const auto capture = BeginOptionalCapture();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto first = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(first, nullptr);
  settings.manual_ev = 4.0F;
  EXPECT_EQ(pass_->ResolveFrame(ctx_, SharedConfig(settings), {}), first);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  ctx_.frame_slot = frame::Slot { 1U };
  const auto second = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first->buffer, second->buffer);
  const auto a
    = Read<FrameExposureData>(*first->buffer, ResourceStates::kShaderResource);
  const auto b
    = Read<FrameExposureData>(*second->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(a.pre_exposure, 0x1p-14F);
  EXPECT_EQ(a.one_over_pre_exposure, 0x1p14F);
  EXPECT_EQ(a.global_exposure_state_slot, first->current_state->srv_index);
  EXPECT_EQ(b.pre_exposure, 0x1p-4F);
  EXPECT_EQ(b.flags, 0U);
  EXPECT_EQ(Read<ExposureStateData>(
              *first->current_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-14F);
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameResolveUsesGpuPriorGainAndPositiveLatentAfterZero)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.target_luminance = 0.0F;
  Run(signal, settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(resolved, nullptr);
  const auto frame = Read<FrameExposureData>(
    *resolved->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, initial.state.latent_scale);
  EXPECT_NEAR(frame.pre_exposure * frame.one_over_pre_exposure, 1.0F, 2e-6F);
  EXPECT_EQ(frame.flags, 0U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  const auto fresh = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(fresh, nullptr);
  const auto bootstrap
    = Read<FrameExposureData>(*fresh->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(bootstrap.pre_exposure, 1.0F);
  EXPECT_EQ(bootstrap.flags, 1U);
  const auto zero = Read<ExposureStateData>(
    *fresh->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(zero.displayed_scale, 0.0F);
  EXPECT_GT(zero.latent_scale, 0.0F);
}

NOLINT_TEST_F(
  ExposureGpuTest, BorrowedUninitializedPublicationCannotAuthorizeHalfDomain)
{
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto config = SharedConfig();
  const auto invalid
    = RecordShared(Uniform(std::numeric_limits<float>::quiet_NaN()), config);
  ASSERT_TRUE(invalid.executed);
  const auto source_state = ReadState(invalid);
  ASSERT_EQ(source_state.flags & 2U, 0U);
  auto source = postprocess::ExposurePass::Source {
    .handle = ctx_.current_view.view_state_handle, .config = config
  };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  ctx_.frame_slot = frame::Slot { 1U };
  const auto borrowed = pass_->ResolveFrame(
    ctx_, config, { .use_fp32 = false, .source = &source });
  ASSERT_NE(borrowed, nullptr);
  const auto domain = Read<FrameExposureData>(
    *borrowed->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.flags & 11U, 11U);
  EXPECT_EQ(domain.pre_exposure, 1.0F);
}

NOLINT_TEST_F(ExposureGpuTest, FrameResolveBorrowsPriorRootAndTagsRootFallback)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto signal = Uniform(.25F);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto prior = RecordShared(signal, SharedConfig(settings));
  ASSERT_TRUE(prior.executed);
  auto source = postprocess::ExposurePass::Source { .handle
    = ctx_.current_view.view_state_handle,
    .config = SharedConfig(settings) };
  settings.manual_ev = 8.0F;
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  // The first solve is still queued; its transient descriptors belong to slot
  // 0.
  ctx_.frame_slot = frame::Slot { 1U };
  ASSERT_TRUE(RecordShared(signal, SharedConfig(settings)).executed);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  const auto borrowed
    = pass_->ResolveFrame(ctx_, SharedConfig(), { .source = &source });
  ASSERT_NE(borrowed, nullptr);
  const auto frame = Read<FrameExposureData>(
    *borrowed->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, 0x1p-4F);
  EXPECT_EQ(frame.global_exposure_state_slot, prior.state->srv_index);
  EXPECT_EQ(frame.flags, 2U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 21U };
  source.handle = CompositionView::ViewStateHandle { 30U };
  source.config = SharedConfig(settings);
  const auto fallback
    = pass_->ResolveFrame(ctx_, SharedConfig(), { .source = &source });
  ASSERT_NE(fallback, nullptr);
  const auto fallback_frame = Read<FrameExposureData>(
    *fallback->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(fallback_frame.pre_exposure, 1.0F);
  EXPECT_EQ(fallback_frame.flags, 11U);
  const auto fallback_state = Read<ExposureStateData>(
    *fallback->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(fallback_state.displayed_scale, 0x1p-8F);
  EXPECT_EQ(fallback_state.applied_generation[0], 0U);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameResolveSeedsWithoutAcknowledgingAndRetriesRecordingFailure)
{
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto seed
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(seed.has_value());
  static_cast<ExposureFailureGraphics&>(Backend()).fail_next_frame_recorder
    = true;
  EXPECT_EQ(pass_->ResolveFrame(ctx_, SharedConfig(),
              { .transition = *seed, .lifetime = seed->lifetime }),
    nullptr);
  const auto resolved = pass_->ResolveFrame(
    ctx_, SharedConfig(), { .transition = *seed, .lifetime = seed->lifetime });
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*resolved->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-12F);
  EXPECT_EQ(Read<ExposureStateData>(
              *resolved->current_state->buffer, ResourceStates::kShaderResource)
              .applied_generation[0],
    0U);
  EXPECT_EQ(renderer_->InspectExposureTransition(seed->target)->phase,
    ExposureTransitionPhase::kQueued);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto fp32 = pass_->ResolveFrame(ctx_, SharedConfig(),
    { .use_fp32 = true, .transition = *seed, .lifetime = seed->lifetime });
  ASSERT_NE(fp32, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*fp32->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    1.0F);
  ctx_.frame_sequence = frame::SequenceNumber { 3U };
  auto diagnostic = SharedConfig();
  diagnostic = diagnostic.WithDiagnosticOverride(true);
  const auto unit = pass_->ResolveFrame(ctx_, diagnostic, {});
  ASSERT_NE(unit, nullptr);
  const auto unit_frame
    = Read<FrameExposureData>(*unit->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(unit_frame.pre_exposure, 1.0F);
  EXPECT_EQ(unit_frame.flags, 4U);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameResolvePinsQualifiedCandidateAndRejectsIneligibleCandidate)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto seed = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(seed, nullptr);
  auto candidate = ExposureStateData {};
  candidate.flags = 1U | 256U;
  candidate.fp16_candidate_pre_exposure = 0.125F;
  candidate.fp16_eligible_streak = 2U;
  auto upload = CreateUploadBuffer(SizeBytes { sizeof(candidate) });
  upload->Update(&candidate, sizeof(candidate), 0U);
  {
    auto recorder = AcquireRecorder("Qualified candidate fixture");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      recorder->AdoptKnownResourceState(*seed->current_state->buffer));
    recorder->RequireResourceState(
      *seed->current_state->buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(
      *seed->current_state->buffer, 0U, *upload, 0U, sizeof(candidate));
    recorder->RequireResourceStateFinal(
      *seed->current_state->buffer, ResourceStates::kShaderResource);
  }
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings),
    { .qualified_candidate = seed->current_state });
  ASSERT_NE(resolved, nullptr);
  const auto frame = Read<FrameExposureData>(
    *resolved->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, 0.125F);
  EXPECT_EQ(frame.one_over_pre_exposure, 8.0F);
  EXPECT_EQ(frame.flags, 0U);
  ctx_.frame_sequence = frame::SequenceNumber { 3U };
  const auto invalid = pass_->ResolveFrame(ctx_, SharedConfig(settings),
    { .qualified_candidate = resolved->current_state });
  ASSERT_NE(invalid, nullptr);
  const auto invalid_frame = Read<FrameExposureData>(
    *invalid->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(invalid_frame.pre_exposure, 1.0F);
  EXPECT_EQ(invalid_frame.flags, 1U);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameResolvePreservesOperationalEndpointsAndCameraGain)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (const float ev : { -32.0F, 32.0F }) {
    settings.manual_ev = ev;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
    ASSERT_NE(resolved, nullptr);
    const auto frame = Read<FrameExposureData>(
      *resolved->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(frame.pre_exposure, std::exp2(-ev));
    EXPECT_EQ(frame.one_over_pre_exposure, std::exp2(ev));
  }
  settings.mode = engine::ExposureMode::kManualCamera;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto camera
    = pass_->ResolveFrame(ctx_, SharedConfig(settings, 16.0F), {});
  ASSERT_NE(camera, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*camera->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-16F);
  settings.enabled = false;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto disabled
    = pass_->ResolveFrame(ctx_, SharedConfig(settings, 16.0F), {});
  ASSERT_NE(disabled, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*disabled->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    1.0F);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainSolvesReservedStateAndAppliesManualRatio)
{
  const auto capture = BeginOptionalCapture();
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  postprocess::ExposurePass::FrameLease frame;
  for (const float ev : { 4.0F, 8.0F }) {
    settings.manual_ev = ev;
    const auto pixel
      = ServicePixel(service, Uniform(.25F * std::exp2(-ev), 4U, 4U), settings,
        ServicePixelOptions { .before_execute = [&] {
          frame = service.PrepareFrameExposure(ctx_, false);
          ASSERT_NE(frame, nullptr);
        } });
    EXPECT_NEAR(pixel, .25F * std::exp2(-ev), 2e-7F);
    ASSERT_NE(frame, nullptr);
    const auto state
      = vortex::testing::RendererPublicationProbe::ExposureStateForView(
        service, ctx_.current_view.view_state_handle);
    EXPECT_EQ(state, frame->current_state);
    EXPECT_EQ(
      Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource)
        .displayed_scale,
      std::exp2(-ev));
    EXPECT_EQ(
      Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
        .pre_exposure,
      std::exp2(-ev));
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainMetersWithGpuReciprocalAndPreservesZeroAndDisabled)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  postprocess::ExposurePass::FrameLease frame;
  const auto prepare = [&] {
    frame = service.PrepareFrameExposure(ctx_, false);
    ASSERT_NE(frame, nullptr);
  };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings,
                ServicePixelOptions { .before_execute = prepare }),
    .18F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, Uniform(.18F, 4U, 4U), settings,
                ServicePixelOptions { .before_execute = prepare }),
    .18F, 2e-5F);
  ASSERT_NE(frame, nullptr);
  const auto state = Read<ExposureStateData>(
    *frame->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_NEAR(state.raw_metered_luminance, .25F, 2e-5F);
  const auto numerical
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_NEAR(numerical.pre_exposure, .72F, 2e-5F);
  settings.target_luminance = 0.0F;
  EXPECT_EQ(ServicePixel(service, Uniform(.18F, 4U, 4U), settings,
              ServicePixelOptions { .before_execute = prepare }),
    0.0F);
  settings.enabled = false;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings,
                ServicePixelOptions { .before_execute = prepare }),
    .25F, 2e-6F);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainFailedSeedKeepsPriorDisplayedGainAndPendingRequest)
{
  auto service = PostProcessService(*renderer_);
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  const auto request
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(request.has_value());
  postprocess::ExposurePass::FrameLease frame;
  const auto pixel = ServicePixel(service, Uniform(.25F / 4096.0F, 4U, 4U), {},
    ServicePixelOptions { .before_execute = [&] {
      frame = service.PrepareFrameExposure(ctx_, false);
      ASSERT_NE(frame, nullptr);
      static_cast<ExposureFailureGraphics&>(Backend())
        .fail_next_exposure_recorder = true;
    } });
  ASSERT_NE(frame, nullptr);
  EXPECT_NEAR(pixel, .18F, 2e-5F);
  EXPECT_EQ(
    Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-12F);
  EXPECT_NEAR(Read<ExposureStateData>(
                *frame->current_state->buffer, ResourceStates::kShaderResource)
                .displayed_scale,
    .72F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(request->target)->phase,
    ExposureTransitionPhase::kQueued);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainSharedConsumerUsesOwnerGainAndItsOwnNumericalDomain)
{
  auto service = PostProcessService(*renderer_);
  auto frame_context = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto root = PublishExposureOwner(frame_context, ViewId { 50U },
    CompositionView::ViewStateHandle { 10U }, settings);
  const auto consumer = PublishExposureOwner(frame_context, ViewId { 60U },
    CompositionView::ViewStateHandle { 20U }, {}, ViewId { 50U });
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 16.0F, 2e-7F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F / 16.0F, 4U, 4U), {},
                ServicePixelOptions { .before_execute =
                                        [&] {
                                          frame = service.PrepareFrameExposure(
                                            ctx_, false);
                                          ASSERT_NE(frame, nullptr);
                                        } }),
    .25F / 16.0F, 2e-7F);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->current_state->histogram_buffer, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-4F);
}

NOLINT_TEST_F(ExposureGpuTest, FrameDomainFailedSolveStillHonorsZeroTarget)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  auto settings = scene::ExposureSettings {};
  settings.target_luminance = 0.0F;
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_EQ(ServicePixel(service, Uniform(.18F, 4U, 4U), settings,
              ServicePixelOptions { .before_execute
                =
                  [&] {
                    frame = service.PrepareFrameExposure(ctx_, false);
                    ASSERT_NE(frame, nullptr);
                    static_cast<ExposureFailureGraphics&>(Backend())
                      .fail_next_exposure_recorder = true;
                  } }),
    0.0F);
  ASSERT_NE(frame, nullptr);
  const auto current = Read<ExposureStateData>(
    *frame->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(current.displayed_scale, 0.0F);
  EXPECT_GT(current.latent_scale, 0.0F);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainFailedSourceLossRetainsLatestBorrowInReservedState)
{
  auto& service = OwnedExposureService();
  auto publication = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, publication);
  const auto consumer_handle = ctx_.current_view.view_state_handle;
  const auto old
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, consumer_handle);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 8.0F;
  const auto root = PublishExposureOwner(publication, ViewId { 50U },
    CompositionView::ViewStateHandle { 50U }, settings);
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-7F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle = consumer_handle;
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  const auto fail_copy = [&] {
    ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr);
    static_cast<ExposureFailureGraphics&>(Backend()).fail_next_exposure_recorder
      = true;
  };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F / 256.0F, 4U, 4U), {},
                ServicePixelOptions { .before_execute = fail_copy }),
    .25F / 256.0F, 2e-7F);
  renderer_->RemovePublishedRuntimeView(publication, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle = consumer_handle;
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), {},
                ServicePixelOptions { .before_execute
                  =
                    [&] {
                      frame = service.PrepareFrameExposure(ctx_, true);
                      ASSERT_NE(frame, nullptr);
                      static_cast<ExposureFailureGraphics&>(Backend())
                        .fail_next_exposure_recorder = true;
                    } }),
    .25F / 256.0F, 2e-7F);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(Read<ExposureStateData>(
              *frame->current_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, consumer_handle),
    old);
  EXPECT_EQ(
    Read<ExposureStateData>(*old->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
}

NOLINT_TEST_F(ExposureGpuTest, FrameDomainSkipsTonemapWhenFallbackCannotSubmit)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto previous
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, ctx_.current_view.view_state_handle);
  static_cast<void>(ServicePixel(service, Uniform(.18F, 4U, 4U), {},
    ServicePixelOptions { .before_execute = [&] {
      ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr);
      auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
      backend.fail_next_exposure_recorder = true;
      backend.fail_next_fallback_recorder = true;
    } }));
  EXPECT_TRUE(service.GetLastExecutionState().tonemap_requested);
  EXPECT_FALSE(service.GetLastExecutionState().tonemap_executed);
  EXPECT_FALSE(service.GetLastExecutionState().wrote_visible_output);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, ctx_.current_view.view_state_handle),
    previous);
}

} // namespace oxygen::vortex::testing::exposure
