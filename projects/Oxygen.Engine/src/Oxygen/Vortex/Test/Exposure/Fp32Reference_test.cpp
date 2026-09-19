//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(
  ExposureGpuTest, Fp32ReferencePreservesQualifiedCandidateAndExposureHistory)
{
  auto settings = scene::ExposureSettings {};
  const auto config = SharedConfig(settings, {}, 71U);
  const auto signal = Uniform(.25F);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto previous = RecordShared(signal, config);
  ASSERT_TRUE(previous.executed);
  auto candidate = ReadState(previous);
  ASSERT_NEAR(candidate.displayed_scale, .72F, 2e-5F);
  ASSERT_NE(candidate.raw_metered_luminance, 0.0F);
  candidate.flags |= 256U;
  candidate.fp16_candidate_pre_exposure = .125F;
  candidate.fp16_eligible_streak = 2U;
  auto upload = CreateUploadBuffer(SizeBytes { sizeof(candidate) });
  upload->Update(&candidate, sizeof(candidate), 0U);
  {
    auto recorder = AcquireRecorder("FP32 reference qualified candidate");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*previous.state->buffer));
    recorder->RequireResourceState(
      *previous.state->buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(
      *previous.state->buffer, 0U, *upload, 0U, sizeof(candidate));
    recorder->RequireResourceStateFinal(
      *previous.state->buffer, ResourceStates::kShaderResource);
  }
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto resolved = pass_->ResolveFrame(ctx_, config,
    { .use_fp32 = true,
      .preserve_fp32_candidate_p = true,
      .qualified_candidate = previous.state });
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(resolved->selected_history, previous.state);
  const auto domain = Read<FrameExposureData>(
    *resolved->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.pre_exposure, .125F);
  EXPECT_EQ(domain.one_over_pre_exposure, 8.0F);
  EXPECT_EQ(domain.flags, 1U);
  EXPECT_EQ(
    domain.global_exposure_state_slot, resolved->current_state->srv_index);
  const auto prepared = Read<ExposureStateData>(
    *resolved->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&candidate, &prepared, 24U), 0);
  EXPECT_EQ(prepared.settings_revision, candidate.settings_revision);
  EXPECT_EQ(prepared.requested_generation, candidate.requested_generation);
  EXPECT_EQ(prepared.applied_generation, candidate.applied_generation);
  const auto retained = ReadState(previous);
  EXPECT_EQ(std::memcmp(&candidate, &retained, sizeof(candidate)), 0);
  // Execute meters the pinned frame domain, so its input already contains P.
  const auto result = RecordShared(Uniform(.25F * .125F), config);
  ASSERT_TRUE(result.executed);
  const auto solved = ReadState(result);
  EXPECT_EQ(std::memcmp(&candidate, &solved, 24U), 0);
  EXPECT_EQ(solved.settings_revision, candidate.settings_revision);
  EXPECT_EQ(solved.requested_generation, candidate.requested_generation);
  EXPECT_EQ(solved.applied_generation, candidate.applied_generation);
  EXPECT_EQ(solved.frame_sequence, (std::array<std::uint32_t, 2> { 2U, 0U }));
}

NOLINT_TEST_F(
  ExposureGpuTest, Fp32ReferenceRequiresValidCandidateAndHonorsUnitFallbacks)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto previous = RecordShared(Uniform(.25F), config);
  ASSERT_TRUE(previous.executed);
  const auto original = ReadState(previous);
  struct Case {
    const char* name;
    float candidate_p;
    unsigned streak;
    bool eligible;
    bool absent;
    bool diagnostic;
    bool missing_source;
  };
  const std::array cases {
    Case { "absent", .125F, 2U, true, true, false, false },
    Case { "not eligible", .125F, 2U, false, false, false, false },
    Case { "one eligible frame", .125F, 1U, true, false, false, false },
    Case { "zero", 0, 2U, true, false, false, false },
    Case { "below supported P", 0x1p-33F, 2U, true, false, false, false },
    Case { "above supported P", 0x1p33F, 2U, true, false, false, false },
    Case { "NaN", std::numeric_limits<float>::quiet_NaN(), 2U, true, false,
      false, false },
    Case { "infinite", std::numeric_limits<float>::infinity(), 2U, true, false,
      false, false },
    Case { "diagnostic wins", .125F, 2U, true, false, true, false },
    Case { "missing source wins", .125F, 2U, true, false, false, true },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto candidate = original;
    candidate.flags
      = test_case.eligible ? original.flags | 256U : original.flags & ~256U;
    candidate.fp16_candidate_pre_exposure = test_case.candidate_p;
    candidate.fp16_eligible_streak = test_case.streak;
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(candidate) });
    upload->Update(&candidate, sizeof(candidate), 0U);
    {
      auto recorder = AcquireRecorder("FP32 reference fallback candidate");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*previous.state->buffer));
      recorder->RequireResourceState(
        *previous.state->buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(
        *previous.state->buffer, 0U, *upload, 0U, sizeof(candidate));
      recorder->RequireResourceStateFinal(
        *previous.state->buffer, ResourceStates::kShaderResource);
    }
    const auto source = postprocess::ExposurePass::Source {
      .handle = CompositionView::ViewStateHandle { 900U }, .config = config
    };
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto resolved = pass_->ResolveFrame(ctx_,
      test_case.diagnostic ? config.WithDiagnosticOverride(true) : config,
      { .use_fp32 = true,
        .preserve_fp32_candidate_p = true,
        .qualified_candidate = test_case.absent ? nullptr : previous.state,
        .source = test_case.missing_source ? &source : nullptr });
    ASSERT_NE(resolved, nullptr);
    const auto domain = Read<FrameExposureData>(
      *resolved->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(domain.pre_exposure, 1.0F);
    EXPECT_EQ(domain.one_over_pre_exposure, 1.0F);
    EXPECT_EQ(domain.flags,
      test_case.diagnostic         ? 5U
        : test_case.missing_source ? 11U
                                   : 1U);
    const auto state = Read<ExposureStateData>(
      *resolved->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(state.displayed_scale, test_case.diagnostic ? 1.0F : 0x1p-4F);
    const auto retained = ReadState(previous);
    EXPECT_EQ(std::memcmp(&candidate, &retained, sizeof(candidate)), 0);
  }
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, Fp32ReferenceSwitchPreservesSceneExposureAndHistory)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& diagnostics = renderer_->GetDiagnosticsService();
  ASSERT_FALSE(diagnostics.IsHdrFp32ReferenceEnabled());
  SceneTextureExtractRef current;
  unsigned current_draws = 0U;
  probe->inspect = [&](const RenderContext& context,
                     const SceneTextureExtractRef& color,
                     const unsigned draws) {
    current_draws = draws;
    EXPECT_FLOAT_EQ(context.delta_time, frame_delta_seconds);
    const auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    EXPECT_EQ(owner->GetSceneTextures().GetSceneColor().GetDescriptor().format,
      Format::kRGBA32Float);
    current = color;
  };
  for (const bool forward : { false, true }) {
    SCOPED_TRACE(::testing::Message() << "forward=" << forward);
    surface_view_id = forward ? 9301U : 9300U;
    frame_delta_seconds = 0.0F;
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 3.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    ASSERT_EQ(current_draws, 1U);
    ASSERT_TRUE(current.valid);
    ASSERT_NE(current.texture, nullptr);
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    ASSERT_NE(current.exposure, nullptr);
    ASSERT_NE(current.exposure->qualified_candidate, nullptr);
    const auto before_domain = Read<FrameExposureData>(
      *current.exposure->buffer, ResourceStates::kShaderResource);
    const auto before
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_EQ(before_domain.flags, 0U);
    ASSERT_NE(before_domain.pre_exposure, 1.0F);
    EXPECT_EQ(before.displayed_scale, .125F);
    EXPECT_EQ(before.latent_scale, .125F);
    EXPECT_NE(before.target_scale, before.displayed_scale);
    EXPECT_EQ(before.requested_generation, before.applied_generation);
    EXPECT_EQ(before.applied_generation[0], seed->generation);
    EXPECT_NEAR(std::log2(double(before.target_scale)),
      std::log2(UniformReferenceGain(.25F)), 4e-4);
    const auto output_texture
      = framebuffer->GetDescriptor().color_attachments.front().texture;
    const auto before_output = ReadFloatTexture(*output_texture);
    ASSERT_EQ(before_output.size(), 1U);
    constexpr auto expected_output = .25F * .125F - .5F / 255.0F;
    for (unsigned channel = 0U; channel < 3U; ++channel) {
      EXPECT_NEAR(before_output[0][channel], expected_output, 2e-4F);
    }
    for (const bool reference : { true, false }) {
      SCOPED_TRACE(::testing::Message() << "reference=" << reference);
      diagnostics.SetHdrFp32ReferenceEnabled(reference);
      EXPECT_EQ(diagnostics.IsHdrFp32ReferenceEnabled(), reference);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      ASSERT_EQ(current_draws, 1U);
      ASSERT_TRUE(current.valid);
      ASSERT_NE(current.texture, nullptr);
      ASSERT_EQ(current.texture->GetDescriptor().format,
        reference ? Format::kRGBA32Float : Format::kRGBA16Float);
      ASSERT_NE(current.exposure, nullptr);
      ASSERT_NE(current.exposure->qualified_candidate, nullptr);
      ASSERT_NE(current.exposure->selected_history, nullptr);
      const auto domain = Read<FrameExposureData>(
        *current.exposure->buffer, ResourceStates::kShaderResource);
      const auto state
        = Read<ExposureStateData>(*current.exposure->current_state->buffer,
          ResourceStates::kShaderResource);
      EXPECT_EQ(domain.flags, reference ? 1U : 0U);
      EXPECT_EQ(domain.pre_exposure, before_domain.pre_exposure);
      EXPECT_EQ(
        domain.one_over_pre_exposure, before_domain.one_over_pre_exposure);
      EXPECT_EQ(state.displayed_scale, .125F);
      EXPECT_EQ(state.latent_scale, .125F);
      EXPECT_EQ(state.target_scale, before.target_scale);
      EXPECT_EQ(state.latent_target_scale, before.latent_target_scale);
      EXPECT_EQ(state.raw_metered_luminance, before.raw_metered_luminance);
      EXPECT_EQ(state.raw_metered_ev, before.raw_metered_ev);
      EXPECT_EQ(state.settings_revision, before.settings_revision);
      EXPECT_EQ(state.requested_generation, before.requested_generation);
      EXPECT_EQ(state.applied_generation, before.applied_generation);
      EXPECT_EQ(state.product_layout_revision, before.product_layout_revision);
      EXPECT_GE(state.fp16_eligible_streak, 2U);
      EXPECT_EQ(state.fallback_reason, before.fallback_reason);
      const auto pixels = ReadFloatTexture(*current.texture, !reference);
      ASSERT_EQ(pixels.size(), 1U);
      const auto output = ReadFloatTexture(*output_texture);
      ASSERT_EQ(output.size(), 1U);
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(
          pixels[0][channel] / domain.pre_exposure, .25F, .005F * .25F + 2e-5F);
        EXPECT_NEAR(output[0][channel], expected_output, 2e-4F);
        EXPECT_NEAR(output[0][channel], before_output[0][channel], 1.0F / 255);
      }
    }
    frame_delta_seconds = .25F;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_EQ(current_draws, 1U);
    const auto adapted
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    const auto expected_gain
      = ReferenceAdaptedGain(.125, UniformReferenceGain(.25F), .25);
    EXPECT_NEAR(std::log2(double(adapted.displayed_scale)),
      std::log2(expected_gain), 4e-4);
    EXPECT_GT(adapted.displayed_scale, .125F);
    EXPECT_EQ(adapted.applied_generation, before.applied_generation);
    EXPECT_EQ(adapted.requested_generation, before.requested_generation);
    EXPECT_TRUE(
      renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id }, handle));
    current = {};
  }
  probe->inspect = {};
}

} // namespace oxygen::vortex::testing::exposure
