//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <utility>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(ExposureGpuTest, SuitabilitySelectsGpuCandidateWithTwoStopMargin)
{
  const auto capture = BeginOptionalCapture();
  const auto result = Qualify(Uniform(1.0F, 4U, 4U), true);
  EXPECT_EQ(result.candidate_pre_exposure, 8192.0F);
  EXPECT_GE(result.maximum_scene_rgb, 1.0F);
  // Unit P and zero retained error need only the four-ULP outward product
  // guard.
  EXPECT_LE(std::bit_cast<std::uint32_t>(result.maximum_scene_rgb),
    std::bit_cast<std::uint32_t>(1.0F) + 4U);
  EXPECT_EQ(result.failure_flags, 0U);
  EXPECT_EQ(result.checked_samples, 16U);
  EXPECT_EQ(result.checked_products, 1U);
  EXPECT_EQ(result.expected_products, 1U);
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityRejectsRequiredFortySixStopSignal)
{
  const std::array pixels { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-16F, 0x1p-16F, 0x1p-16F, 1.0F } };
  auto settings = scene::ExposureSettings {};
  settings.min_log_luminance = -24.0F;
  settings.log_luminance_range = 56.0F;
  const auto result = Qualify(MakeSignal(2U, 1U, pixels), true, settings);
  EXPECT_EQ(result.candidate_pre_exposure, 0x1p-17F);
  EXPECT_NE(result.failure_flags & 8U, 0U);
  EXPECT_EQ(result.metering_failures, 1U);
  EXPECT_EQ(result.first_failure_product, 1U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityAccountsForConsumerRgbAmplification)
{
  // Independent FP32 reference: an 8192 sample anchors candidate P at one;
  // AP contributes 1e-8 * 1e6 = .01 at the other scene pixel. Both local
  // narrowing checks pass without consumer gain, but half storage loses AP.
  const std::array scene_pixels { Pixel { 8192, 8192, 8192, 1 },
    Pixel { .01F, .01F, .01F, 1 } };
  const std::array<Pixel, 1> ap_pixel { Pixel { 1e-8F, 1e-8F, 1e-8F, 1 } };
  const auto scene_signal = MakeSignal(2U, 1U, scene_pixels);
  const auto ap_signal = MakeSignal(1U, 1U, ap_pixel);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const std::array gains { 0.0F, .5F, 1.0F, 1e6F, -1.0F,
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::quiet_NaN() };
  const auto capture = BeginOptionalCapture();
  for (const auto gain : gains) {
    SCOPED_TRACE(gain);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    const auto solved = RecordShared(scene_signal, config);
    ASSERT_TRUE(solved.executed);
    const std::array products { postprocess::ExposurePass::HdrProduct {
                                  .texture = scene_signal.texture.get(),
                                  .srv = scene_signal.srv,
                                  .id = 11U,
                                  .error_budget_share = .25F },
      postprocess::ExposurePass::HdrProduct {
        .texture = ap_signal.texture.get(),
        .srv = ap_signal.srv,
        .id = 6U,
        .transmittance = true,
        .error_budget_share = .25F,
        .consumer_rgb_gain = gain } };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
    EXPECT_EQ(report.checked_samples, 3U);
    if (!std::isfinite(gain) || gain < 0.0F) {
      EXPECT_EQ(report.failure_flags, 1U);
      EXPECT_EQ(report.rejected_samples, 1U);
      EXPECT_EQ(report.first_failure_product, 6U);
    } else if (gain == 1e6F) {
      EXPECT_EQ(report.failure_flags, 4U);
      EXPECT_EQ(report.image_failures, 1U);
      EXPECT_EQ(report.first_failure_product, 6U);
    } else {
      EXPECT_EQ(report.failure_flags, 0U);
    }
    EXPECT_EQ(ReadState(solved).displayed_scale, 1.0F);
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  SuitabilityIgnoresBelowBudgetComponentsButRespectsDisplayedGain)
{
  const std::array<Pixel, 1> pixel { Pixel { 1.0F, .5F, 0x1p-40F, 1.0F } };
  EXPECT_EQ(Qualify(MakeSignal(1U, 1U, pixel), true).failure_flags, 0U);
  const std::array wide { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const auto signal = MakeSignal(2U, 1U, wide);
  EXPECT_EQ(Qualify(signal, false).failure_flags, 0U);
  auto bright = scene::ExposureSettings {};
  bright.manual_ev = -32.0F;
  EXPECT_NE(Qualify(signal, false, bright).failure_flags & 4U, 0U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityUsesMeterMaskAndCoverageWeights)
{
  const std::array wide { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const std::array mask_pixels { Pixel { 1.0F, 0, 0, 1 },
    Pixel { 0, 0, 0, 1 } };
  const auto mask = MakeSignal(2U, 1U, mask_pixels);
  const auto signal = MakeSignal(2U, 1U, wide);
  auto required_dark = scene::ExposureSettings {};
  required_dark.black_influence = 1.0F;
  EXPECT_NE(Qualify(signal, true, required_dark).failure_flags & 8U, 0U);
  EXPECT_EQ(Qualify(signal, true, required_dark, &mask).failure_flags, 0U);
  const std::array<Pixel, 1> covered { Pixel { .25F, .25F, .25F, .5F } };
  EXPECT_EQ(
    Qualify(MakeSignal(1U, 1U, covered), true, {}, nullptr, true).failure_flags,
    0U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityRetainsProducerAndHistoryError)
{
  struct Case {
    float observed;
    HdrErrorBoundsData bounds;
    float gain;
    std::uint32_t failure;
    float anchor { 8192.0F };
    bool meter { false };
    bool huge_bound { false };
  };
  const std::array cases { Case { 1.01F, {}, 1, 0 },
    Case { 1.01F, { .rgb_absolute = .01F }, 1, 4 },
    Case { 1.01F, { .rgb_relative = .02F }, 1, 4 },
    Case { 1.01F, { .rgb_absolute = 1e-7F }, 1, 0 },
    Case { 0, { .rgb_absolute = 1e-8F }, 1, 0 },
    Case { 0, { .rgb_absolute = 1e-8F }, 1e6F, 4 },
    Case {
      1.01F, { .rgb_absolute = std::numeric_limits<float>::infinity() }, 1, 4 },
    Case { 1.01F, { .rgb_relative = 1.0F }, 1, 4 },
    Case { 1.01F, { .rgb_absolute = -.01F }, 1, 4 },
    Case { 1.01F, { .transmittance_absolute = .01F }, 1, 4 },
    Case { 1.01F, { .transmittance_relative = .02F }, 1, 4 },
    Case { 1.01F, { .transmittance_relative = 1.0F }, 1, 4 },
    Case { 8180.0F, { .rgb_absolute = 16.0F }, 1, 0, 1.0F },
    Case { 0, { .rgb_absolute = 1e-8F }, 1, 8, 8192.0F, true },
    Case { 0, { .rgb_absolute = std::bit_cast<float>(0x7f7ffff0U) }, 1, 4, 8192,
      false, true },
    Case { 0, { .rgb_absolute = std::bit_cast<float>(0x7f7ffff1U) }, 1, 4, 8192,
      false, true },
    Case { 0, { .rgb_absolute = std::bit_cast<float>(0x7f7ffff2U) }, 1, 4, 8192,
      false, true },
    Case { 0, { .rgb_absolute = std::bit_cast<float>(0x7f7ffff3U) }, 1, 4, 8192,
      false, true } };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  settings.black_influence = 1.0F;
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (std::size_t index = 0; index < cases.size(); ++index) {
    SCOPED_TRACE(index);
    const auto& test = cases[index];
    const auto anchor = Uniform(test.anchor, 1U, 1U);
    const std::array<Pixel, 1> pixel { Pixel {
      test.observed, test.observed, test.observed, .5F } };
    const auto fog = MakeSignal(1U, 1U, pixel);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    const auto solved = RecordShared(anchor, config);
    ASSERT_TRUE(solved.executed);
    // Controlled GPU inputs, independent of the producer's bound arithmetic.
    // A bad unrelated sky certificate must not contaminate the fog record.
    std::array<HdrErrorBoundsData, 3> tail {};
    tail[0].rgb_absolute = std::numeric_limits<float>::infinity();
    tail[2] = test.bounds;
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(tail) });
    upload->Update(tail.data(), sizeof(tail), 0U);
    {
      auto recorder = AcquireRecorder("Retained error fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(
        *frame->current_state->status_buffer, 80U, *upload, 0U, sizeof(tail));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    const std::array products {
      postprocess::ExposurePass::HdrProduct {
        .texture = anchor.texture.get(), .srv = anchor.srv, .id = 11U },
      postprocess::ExposurePass::HdrProduct { .texture = fog.texture.get(),
        .srv = fog.srv,
        .id = 10U,
        .metering = test.meter,
        .transmittance = true,
        .consumer_rgb_gain = test.gain }
    };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.failure_flags, test.failure);
    EXPECT_EQ(report.image_failures, (test.failure & 4U) != 0U ? 1U : 0U);
    EXPECT_EQ(report.metering_failures, (test.failure & 8U) != 0U ? 1U : 0U);
    if (!test.huge_bound) {
      EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
    }
    EXPECT_EQ(report.checked_samples, 2U);
    if (test.failure != 0U) {
      EXPECT_EQ(report.first_failure_product, 10U);
    }
    if (test.anchor == 1.0F) {
      EXPECT_GE(report.maximum_scene_rgb, 8196.0F);
    }
    ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = 1U,
        .expected_products = (1U << 9U) | (1U << 10U) }));
    if (test.failure != 0U) {
      const auto status = Read<ExposureCompletedStatus>(
        *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
      EXPECT_EQ(status.fp16_eligible_streak, 0U);
      EXPECT_EQ(status.flags & 4U, 0U);
    }
    EXPECT_EQ(ReadState(solved).displayed_scale, 1.0F);
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  SuitabilityRgbGainLeavesTransmissionUnscaledAndAvoidsCombinedGainOverflow)
{
  struct Case {
    float background;
    Pixel ap;
    float gain;
    float ev;
    std::uint32_t expected_failure;
  };
  // RGB gain must not amplify the tiny transmission error. In the second
  // case gain*S exceeds FP32, while (2^-100 * gain)*S is finite and its loss
  // must still be rejected.
  const std::array cases { Case { .125F, { 0, 0, 0, 1e-8F }, 1e6F, 0.0F, 0U },
    Case {
      8192.0F, { 0x1p-100F, 0x1p-100F, 0x1p-100F, 1 }, 0x1p100F, -32.0F, 4U } };
  for (const auto& test : cases) {
    SCOPED_TRACE(test.gain);
    const auto scene_signal = Uniform(test.background, 1U, 1U);
    const auto ap_signal = MakeSignal(1U, 1U, std::span { &test.ap, 1U });
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = test.ev;
    const auto config = SharedConfig(settings);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(scene_signal, config).executed);
    const std::array products { postprocess::ExposurePass::HdrProduct {
                                  .texture = scene_signal.texture.get(),
                                  .srv = scene_signal.srv,
                                  .id = 11U },
      postprocess::ExposurePass::HdrProduct {
        .texture = ap_signal.texture.get(),
        .srv = ap_signal.srv,
        .id = 6U,
        .transmittance = true,
        .consumer_rgb_gain = test.gain } };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.failure_flags, test.expected_failure);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilityRejectsNonfiniteAndMissingRequiredProducts)
{
  const auto nonfinite
    = Qualify(Uniform(std::numeric_limits<float>::infinity()), false);
  EXPECT_NE(nonfinite.failure_flags & 1U, 0U);
  EXPECT_EQ(nonfinite.rejected_samples, 1U);
  const auto signal = Uniform(.25F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto config = SharedConfig();
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(), .srv = signal.srv, .id = 1U },
    postprocess::ExposurePass::HdrProduct { .id = 6U }
  };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  const auto missing = Read<HdrSuitabilityData>(
    *frame->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(missing.failure_flags, 16U);
  EXPECT_EQ(missing.first_failure_product, 6U);
  EXPECT_EQ(missing.expected_products, 33U);
  EXPECT_EQ(missing.checked_products, 1U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityChecksVolumeRgbAndTransmittance)
{
  auto texture = CreateRegisteredTexture({ .width = 2U,
    .height = 1U,
    .depth = 2U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .initial_state = ResourceStates::kCommon });
  std::array<std::byte, 512U> bytes {};
  const Pixel value { .25F, .5F, .75F, .5F };
  for (unsigned z = 0U; z < 2U; ++z) {
    for (unsigned x = 0U; x < 2U; ++x) {
      std::memcpy(bytes.data() + z * 256U + x * sizeof(Pixel), value.data(),
        sizeof(Pixel));
    }
  }
  auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Suitability volume upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 2U, .height = 1U, .depth = 2U } },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto srv = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*texture, std::move(handle),
    TextureViewDescription {
      .format = Format::kRGBA32Float, .dimension = TextureType::kTexture3D });
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = texture.get(), .srv = srv, .id = 10U, .transmittance = true } };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  const auto result = Read<HdrSuitabilityData>(
    *frame->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_GE(result.maximum_scene_rgb, .75F);
  EXPECT_LE(std::bit_cast<std::uint32_t>(result.maximum_scene_rgb),
    std::bit_cast<std::uint32_t>(.75F) + 4U);
  EXPECT_EQ(result.candidate_pre_exposure, 16384.0F);
  EXPECT_EQ(result.checked_samples, 4U);
  EXPECT_EQ(result.checked_products, 1U << 9U);
  EXPECT_EQ(result.failure_flags, 0U);
  const Pixel tiny_transmittance { 0.0F, 0.0F, 0.0F, 0x1p-25F };
  for (unsigned z = 0U; z < 2U; ++z) {
    for (unsigned x = 0U; x < 2U; ++x) {
      std::memcpy(bytes.data() + z * 256U + x * sizeof(Pixel),
        tiny_transmittance.data(), sizeof(Pixel));
    }
  }
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Suitability transmittance update");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 2U, .height = 1U, .depth = 2U } },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  const auto bright = Uniform(0x1p30F);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto next = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  const std::array combined {
    postprocess::ExposurePass::HdrProduct {
      .texture = bright.texture.get(), .srv = bright.srv, .id = 1U },
    products[0]
  };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, next, config, combined, {}));
  const auto failed = Read<HdrSuitabilityData>(
    *next->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(failed.failure_flags, 4U);
  EXPECT_EQ(failed.first_failure_product, 10U);
  EXPECT_EQ(failed.image_failures, 4U);
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilitySharesDiscardedDarkAndSyntheticFallbackSemantics)
{
  const std::array original { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const std::array narrowed { original[0], Pixel { 0, 0, 0, 1 } };
  auto settings = scene::ExposureSettings {};
  settings.black_influence = 0.0F;
  const auto signal = MakeSignal(2U, 1U, original);
  EXPECT_EQ(Qualify(signal, true, settings).failure_flags, 0U);
  ResetHistory();
  const auto before = Run(signal, settings);
  ResetHistory();
  const auto after = Run(MakeSignal(2U, 1U, narrowed), settings);
  EXPECT_TRUE(std::equal(before.histogram.begin(),
    before.histogram.begin() + 256, after.histogram.begin()));
  EXPECT_EQ(before.histogram[257], after.histogram[257]);
  EXPECT_EQ(before.histogram[261], after.histogram[261]);
  EXPECT_EQ(before.state.displayed_scale, after.state.displayed_scale);
  EXPECT_EQ(before.state.raw_metered_ev, after.state.raw_metered_ev);
  ResetHistory();
  const auto dark_before = Run(Uniform(0x1p-24F), settings);
  ResetHistory();
  const auto dark_after = Run(Uniform(0.0F), settings);
  EXPECT_EQ(
    dark_before.state.displayed_scale, dark_after.state.displayed_scale);
  EXPECT_NE(dark_before.state.flags & 16U, 0U);
  EXPECT_NE(dark_after.state.flags & 16U, 0U);
  EXPECT_EQ(Qualify(Uniform(0x1p-24F), true, settings).failure_flags, 0U);
  settings.black_influence = 1.0F;
  EXPECT_NE(Qualify(signal, true, settings).failure_flags & 8U, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilityIgnoresCoverageMassChangesAfterDarkDiscard)
{
  auto scene = scene::Scene("DiscardedCoverage", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene.GetEnvironment()
    ->AddSystem<scene::environment::Background>()
    .SetEnabled(true);
  ctx_.scene = observer_ptr { &scene };
  const std::array original { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F * .1F, 0x1p-24F * .1F, 0x1p-24F * .1F, .1F } };
  const std::array narrowed { original[0], Pixel { 0, 0, 0, .0999755859375F } };
  auto settings = scene::ExposureSettings {};
  settings.black_influence = 0.0F;
  const auto signal = MakeSignal(2U, 1U, original);
  EXPECT_EQ(Qualify(signal, true, settings, nullptr, true).failure_flags, 0U);
  ResetHistory();
  const auto before = Run(signal, settings);
  ResetHistory();
  const auto after = Run(MakeSignal(2U, 1U, narrowed), settings);
  EXPECT_TRUE(std::equal(before.histogram.begin(),
    before.histogram.begin() + 256, after.histogram.begin()));
  EXPECT_EQ(before.histogram[257], after.histogram[257]);
  EXPECT_EQ(before.histogram[261], after.histogram[261]);
  EXPECT_EQ(before.state.displayed_scale, after.state.displayed_scale);
  ctx_.scene.reset();
}

} // namespace oxygen::vortex::testing::exposure
