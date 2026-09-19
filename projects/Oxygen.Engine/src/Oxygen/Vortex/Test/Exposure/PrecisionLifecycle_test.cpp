//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <limits>

#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(ExposureGpuTest,
  Fp16EligibilityRequiresStableCompleteFramesAndPreservesExposure)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  auto config = SharedConfig(settings);
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const auto brighter = Uniform(2.0F, 4U, 4U);
  const auto invalid = Uniform(std::numeric_limits<float>::infinity(), 4U, 4U);
  auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  const auto step
    = [&](std::uint64_t sequence, std::uint64_t layout, std::uint64_t revision,
        const Signal& signal, std::uint32_t expected = 1024U,
        bool invalidate = false, bool metering_available = true) {
        config = SharedConfig(settings, {}, revision);
        return EligibilityStep(signal, config, sequence, layout, expected,
          invalidate, nullptr, *token, metering_available, sequence == 2U);
      };
  EXPECT_EQ(step(1U, 7U, 1U, ordinary).first.fp16_eligible_streak, 1U);
  const auto eligible = step(2U, 7U, 1U, ordinary);
  EXPECT_EQ(eligible.first.fp16_eligible_streak, 2U);
  EXPECT_EQ(eligible.first.flags & 256U, 256U);
  EXPECT_EQ(eligible.first.fp16_candidate_pre_exposure, 8192.0F);
  const auto failure = step(3U, 7U, 1U, invalid);
  EXPECT_EQ(failure.first.fp16_eligible_streak, 0U);
  EXPECT_NE(failure.second.flags & 2U, 0U);
  EXPECT_EQ(failure.second.first_failure_product, 11U);
  EXPECT_NE(failure.second.first_failure_kind & 1U, 0U);
  EXPECT_EQ(step(4U, 7U, 1U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(5U, 7U, 1U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(6U, 7U, 2U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(7U, 7U, 2U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(8U, 8U, 2U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(9U, 8U, 2U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(10U, 8U, 2U, brighter).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(12U, 8U, 2U, brighter).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(
    step(13U, 8U, 2U, brighter, 1024U, true).first.fp16_eligible_streak, 1U);
  const auto missing = step(14U, 8U, 2U, brighter, 1025U);
  EXPECT_EQ(missing.first.fp16_eligible_streak, 0U);
  EXPECT_EQ(missing.second.first_failure_product, 1U);
  EXPECT_NE(missing.second.first_failure_kind & 16U, 0U);
  EXPECT_EQ(
    step(0xffffffffULL, 8U, 2U, brighter).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(
    step(0x100000000ULL, 8U, 2U, brighter).first.fp16_eligible_streak, 2U);
  settings.mode = engine::ExposureMode::kAuto;
  config = SharedConfig(settings);
  token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(token.has_value());
  const auto pending
    = step(0x100000001ULL, 8U, 3U, ordinary, 1024U, false, false);
  EXPECT_EQ(pending.first.fp16_eligible_streak, 0U);
  EXPECT_NE(
    pending.first.requested_generation, pending.first.applied_generation);
  EXPECT_EQ(
    step(0x100000002ULL, 8U, 3U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(
    step(0x100000003ULL, 8U, 3U, ordinary).first.fp16_eligible_streak, 2U);
}

NOLINT_TEST_F(ExposureGpuTest, Fp16EligibilityBelongsToEachBorrowingImage)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto root = CompositionView::ViewStateHandle { 10U };
  const auto borrower = CompositionView::ViewStateHandle { 20U };
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const std::array wide_pixels { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1 },
    Pixel { 0x1p-16F, 0x1p-16F, 0x1p-16F, 1 } };
  const auto wide = MakeSignal(2U, 1U, wide_pixels);
  const auto source
    = postprocess::ExposurePass::Source { .handle = root, .config = config };
  const auto step
    = [&](std::uint64_t sequence, bool sharing, const Signal& signal) {
        ctx_.current_view.view_state_handle = sharing ? borrower : root;
        ctx_.current_view.view_id = ViewId { sharing ? 20U : 10U };
        return EligibilityStep(signal, config, sequence, 1U, 1024U, false,
          sharing ? &source : nullptr)
          .first;
      };
  EXPECT_EQ(step(1U, false, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(1U, true, ordinary).fp16_eligible_streak, 0U);
  EXPECT_EQ(step(2U, false, ordinary).fp16_eligible_streak, 2U);
  EXPECT_EQ(step(2U, true, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(3U, false, ordinary).fp16_eligible_streak, 2U);
  const auto rejected = step(3U, true, wide);
  EXPECT_EQ(rejected.fp16_eligible_streak, 0U);
  EXPECT_EQ(rejected.displayed_scale, 1.0F);
  EXPECT_EQ(step(4U, true, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(5U, true, ordinary).fp16_eligible_streak, 2U);
}

NOLINT_TEST_F(
  ExposureGpuTest, Fp16EligibilityExcludesStatelessAndDiagnosticFrames)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  auto config = SharedConfig(settings);
  const auto signal = Uniform(1.0F, 4U, 4U);
  ctx_.current_view.view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_EQ(EligibilityStep(signal, config, 1U).first.fp16_eligible_streak, 0U);
  EXPECT_EQ(EligibilityStep(signal, config, 2U).first.fp16_eligible_streak, 0U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 30U };
  config = config.WithDiagnosticOverride(true);
  EXPECT_EQ(EligibilityStep(signal, config, 3U).first.fp16_eligible_streak, 0U);
  EXPECT_EQ(EligibilityStep(signal, config, 4U).first.fp16_eligible_streak, 0U);
  config = config.WithDiagnosticOverride(false);
  EXPECT_EQ(EligibilityStep(signal, config, 5U).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(EligibilityStep(signal, config, 6U).first.fp16_eligible_streak, 2U);
  ctx_.frame_sequence = frame::SequenceNumber { 7U };
  ASSERT_TRUE(RecordShared(signal, config).executed);
  EXPECT_EQ(EligibilityStep(signal, config, 8U).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(EligibilityStep(signal, config, 9U).first.fp16_eligible_streak, 2U);
  ctx_.frame_sequence = frame::SequenceNumber { 10U };
  const auto unsolved = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(unsolved, nullptr);
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = signal.texture.get(), .srv = signal.srv, .id = 11U } };
  ASSERT_TRUE(
    pass_->EvaluateFp16Products(ctx_, unsolved, config, products, {}));
  ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, unsolved,
    { .product_layout_revision = 7U, .expected_products = 1024U }));
  const auto status = Read<ExposureCompletedStatus>(
    *unsolved->current_state->status_buffer, ResourceStates::kCopySource);
  EXPECT_EQ(status.flags & (1U | 4U), 0U);
  EXPECT_EQ(status.fp16_eligible_streak, 0U);
}

NOLINT_TEST_F(ExposureGpuTest,
  CompletedPrecisionAdmissionTracksViewSettingsLayoutAndDiagnostics)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const auto invalid = Uniform(std::numeric_limits<float>::infinity(), 4U, 4U);
  auto layout = std::uint64_t { 7U };
  const auto step = [&](bool expected_candidate, std::uint32_t expected_streak,
                      const Signal& signal, bool diagnostic = false) {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    ctx_.render_mode = diagnostic ? RenderMode::kWireframe : RenderMode::kSolid;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    const auto requirements = postprocess::ExposurePass::EligibilityInputs {
      .product_layout_revision = layout, .expected_products = 1024U
    };
    const auto candidate = service.SelectPrecisionCandidate(ctx_, requirements);
    EXPECT_EQ(candidate != nullptr, expected_candidate) << sequence_;
    EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), candidate);
    EXPECT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    CHECK_F(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    EXPECT_EQ((service.PrepareScenePrecision(ctx_, *prepared, products)
                && service.FinalizeScenePrecision(ctx_, *prepared)),
      !diagnostic);
    if (!diagnostic) {
      EXPECT_TRUE((service.PrepareScenePrecision(ctx_, *prepared, products)
        && service.FinalizeScenePrecision(ctx_, *prepared)));
      EXPECT_EQ(
        ReadState(prepared->exposure).fp16_eligible_streak, expected_streak);
    }
    const auto counts
      = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
        service, ctx_.current_view.view_state_handle);
    EXPECT_LE(counts.first, 1U);
    EXPECT_EQ(counts.second, 0U);
    return candidate;
  };
  EXPECT_EQ(step(false, 1U, ordinary), nullptr);
  EXPECT_EQ(step(false, 2U, ordinary), nullptr);
  const auto first = step(true, 2U, ordinary);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*first->buffer, ResourceStates::kShaderResource)
      .frame_sequence[0],
    2U);
  layout = 8U;
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  config.exposure.manual_ev = 1.0F;
  service.SetConfig(config);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  step(false, 0U, ordinary, true);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  step(true, 0U, invalid);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  service.RemoveViewState(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle);
  step(false, 1U, ordinary);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, PrecisionStatusRetriesTransportWithoutEarlyAdmission)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_status_recorder = true;
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  for (unsigned i = 0U; i < 6U; ++i) {
    EXPECT_EQ(begin(), nullptr);
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    ASSERT_TRUE(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    ASSERT_TRUE((service.PrepareScenePrecision(ctx_, *prepared, products)
      && service.FinalizeScenePrecision(ctx_, *prepared)));
    const auto counts
      = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
        service, ctx_.current_view.view_state_handle);
    EXPECT_EQ(counts.first, 0U);
    EXPECT_EQ(counts.second, 1U);
  }
  backend.fail_status_recorder = false;
  EXPECT_EQ(begin(),
    nullptr); // Retry records a copy; it is not an acknowledgement.
  const auto candidate = begin();
  ASSERT_NE(candidate, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*candidate->buffer, ResourceStates::kShaderResource)
      .frame_sequence[0],
    6U);
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), nullptr);
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedPrecisionRecordingCannotAdmitAnOlderDeferredCertificate)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_status_recorder = true;
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  for (unsigned i = 0U; i < 3U; ++i) {
    EXPECT_EQ(begin(), nullptr);
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    ASSERT_TRUE(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    backend.fail_next_suitability_recorder = i == 2U;
    EXPECT_EQ((service.PrepareScenePrecision(ctx_, *prepared, products)
                && service.FinalizeScenePrecision(ctx_, *prepared)),
      i != 2U);
  }
  backend.fail_status_recorder = false;
  EXPECT_EQ(begin(), nullptr);
  EXPECT_EQ(begin(), nullptr);
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedSolveFallbackRestartsPrecisionWithoutRejectingSuccessfulReuse)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto inputs
    = PostProcessService::Inputs { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv };
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = signal.texture.get(),
    .srv = signal.srv,
    .id = 11U,
    .metering = true } };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  const auto finish = [&](std::uint32_t streak, bool fail, bool reuse) {
    SCOPED_TRACE(sequence_);
    CHECK_NOTNULL_F(service.PrepareFrameExposure(ctx_, true).get());
    backend.fail_next_exposure_recorder = fail;
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_, inputs);
    CHECK_F(prepared.has_value());
    EXPECT_EQ(prepared->exposure.executed, !fail);
    CHECK_NOTNULL_F(prepared->exposure.state.get());
    if (reuse) {
      const auto reused
        = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_, inputs);
      CHECK_F(reused.has_value());
      EXPECT_FALSE(reused->exposure.executed);
      EXPECT_EQ(reused->exposure.state, prepared->exposure.state);
      EXPECT_TRUE((service.PrepareScenePrecision(ctx_, *reused, products)
        && service.FinalizeScenePrecision(ctx_, *reused)));
    }
    EXPECT_EQ((service.PrepareScenePrecision(ctx_, *prepared, products)
                && service.FinalizeScenePrecision(ctx_, *prepared)),
      !fail);
    const auto state = ReadState(prepared->exposure);
    if (!fail) {
      EXPECT_EQ(state.fp16_eligible_streak, streak);
    }
    return state;
  };
  EXPECT_EQ(begin(), nullptr);
  const auto initial = finish(1U, false, false);
  EXPECT_EQ(begin(), nullptr);
  finish(2U, false, false);
  ASSERT_NE(begin(), nullptr);
  backend.fail_status_recorder = true;
  finish(2U, false, false);
  ASSERT_NE(begin(), nullptr);
  const auto fallback = finish(0U, true, false);
  EXPECT_EQ(fallback.displayed_scale, initial.displayed_scale);
  EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), nullptr);
  backend.fail_status_recorder = false;
  EXPECT_EQ(begin(), nullptr);
  finish(1U, false, true);
  EXPECT_EQ(begin(), nullptr);
  finish(2U, false, true);
  ASSERT_NE(begin(), nullptr);
  finish(2U, false, true);

  const auto seed
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
  ASSERT_TRUE(seed.has_value());
  EXPECT_EQ(begin(), nullptr);
  const auto pending_fallback = finish(0U, true, false);
  EXPECT_EQ(pending_fallback.displayed_scale, initial.displayed_scale);
  const auto pending = renderer_->InspectExposureTransition(seed->target);
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending->phase, ExposureTransitionPhase::kQueued);
  EXPECT_LT(pending->applied_generation, seed->generation);
  EXPECT_EQ(begin(), nullptr);
  const auto seeded = finish(1U, false, true);
  EXPECT_EQ(seeded.displayed_scale, 0x1p-4F);
  EXPECT_EQ(begin(), nullptr);
  const auto applied = renderer_->InspectExposureTransition(seed->target);
  ASSERT_TRUE(applied.has_value());
  EXPECT_EQ(applied->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(applied->applied_generation, seed->generation);
  WaitForQueueIdle();
}

} // namespace oxygen::vortex::testing::exposure
