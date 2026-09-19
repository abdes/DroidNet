//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>

#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(ExposureGpuTest, SceneDisplayAdmissionChecksGammaAndBackground)
{
  struct Case {
    const char* name;
    float radiance;
    float alpha;
    float gamma;
    float alpha_error;
    bool background;
    float background_value;
    std::uint32_t failure;
    engine::ToneMapper mapper { engine::ToneMapper::kNone };
  };
  const std::array cases {
    Case {
      "gamma reveals below-float-budget loss", 8e-6F, 1, 2.2F, 0, false, 0, 4 },
    Case {
      "linear display keeps loss insignificant", 8e-6F, 1, 1, 0, false, 0, 0 },
    Case { "white background exposes coverage error", 0, .5F, 2.2F, .1F, true,
      1, 4 },
    Case { "black image does not require unused coverage", 0, .5F, 2.2F, .1F,
      true, 0, 0 },
    Case {
      "half coverage changes dark background", 0, .9987F, 2.2F, 0, true, 1, 4 },
    Case { "ACES toe hides tiny radiance", 8e-6F, 1, 2.2F, 0, false, 0, 0,
      engine::ToneMapper::kAcesFitted },
    Case { "Filmic exposes tiny radiance", 8e-6F, 1, 2.2F, 0, false, 0, 4,
      engine::ToneMapper::kFilmic },
    Case { "Reinhard exposes tiny radiance", 8e-6F, 1, 2.2F, 0, false, 0, 4,
      engine::ToneMapper::kReinhard },
  };
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    auto scene = scene::Scene("Display admission", 1U);
    scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    auto& background
      = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
    background.SetEnabled(test.background);
    background.SetColorRgb(
      { test.background_value, test.background_value, test.background_value });
    ctx_.scene = observer_ptr { &scene };
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = 0.0F;
    const auto resolved
      = ResolvedPostProcessConfig::Resolve({ .exposure = settings,
        .tone_mapper = test.mapper,
        .enable_bloom = false,
        .gamma = test.gamma });
    ASSERT_TRUE(resolved.has_value());
    const std::array<Pixel, 1> pixels { Pixel {
      test.radiance, test.radiance, test.radiance, test.alpha } };
    const auto signal = MakeSignal(1U, 1U, pixels);
    const auto anchor = Uniform(0x1p33F);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame
      = pass_->ResolveFrame(ctx_, *resolved, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, *resolved).executed);
    const auto error
      = HdrSceneErrorData { .candidate_coverage_absolute = test.alpha_error,
          .candidate_pre_exposure = 0x1p-20F,
          .checked_products = (1U << 10U) | 1U,
          .flags = 3U };
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(error) });
    upload->Update(&error, sizeof(error), 0U);
    {
      auto recorder = AcquireRecorder("Display admission certificate");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, scene_error), *upload, 0U,
        sizeof(error));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    const std::array products {
      postprocess::ExposurePass::HdrProduct {
        .texture = anchor.texture.get(), .srv = anchor.srv, .id = 1U },
      postprocess::ExposurePass::HdrProduct { .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 11U,
        .coverage = test.background,
        .composed_error = true }
    };
    ASSERT_TRUE(
      pass_->EvaluateFp16Products(ctx_, frame, *resolved, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.candidate_pre_exposure, 0x1p-20F);
    EXPECT_EQ(report.failure_flags, test.failure);
    EXPECT_EQ(report.metering_failures, 0U);
    ctx_.scene.reset();
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, ComposedSceneAdmissionUsesCandidateAndCoverageIntervals)
{
  constexpr auto scene_bit = 1U << 10U;
  struct Case {
    const char* name;
    Pixel pixel;
    HdrSceneErrorData error;
    std::uint32_t failure;
    bool metering { true };
    bool current_store { false };
    std::uint32_t required_products { 0U };
  };
  const auto complete = HdrSceneErrorData {
    .candidate_pre_exposure = 1.0F,
    .checked_products = scene_bit,
    .flags = 3U,
  };
  auto candidate_error = complete;
  candidate_error.candidate_rgb_absolute = .01F;
  auto retained_error = complete;
  retained_error.current_rgb_absolute = .01F;
  auto tiny_error = complete;
  tiny_error.candidate_rgb_absolute = 1e-8F;
  auto coverage_error = complete;
  coverage_error.candidate_coverage_absolute = .001F;
  auto dark_error = complete;
  // Half spacing immediately above 2^-12 is 2^-22; cross its half-way point.
  dark_error.candidate_rgb_absolute = 2e-7F;
  auto stale = complete;
  stale.candidate_pre_exposure = 2.0F;
  auto missing = complete;
  missing.flags = 1U;
  auto incomplete = complete;
  incomplete.checked_products = 0U;
  auto invalid = complete;
  invalid.candidate_rgb_absolute = std::numeric_limits<float>::infinity();
  auto current_only = invalid;
  current_only.flags = 1U;
  current_only.checked_products = scene_bit | (1U << 9U);
  current_only.candidate_pre_exposure = 2.0F;
  auto current_retained = current_only;
  current_retained.current_rgb_absolute = .01F;
  const std::array cases {
    Case { "exact", { 1, 1, 1, 1 }, complete, 0 },
    Case { "upstream candidate differs", { 1, 1, 1, 1 }, candidate_error, 12 },
    Case { "retained reference differs", { 1, 1, 1, 1 }, retained_error, 12 },
    Case { "insignificant candidate", { 1, 1, 1, 1 }, tiny_error, 0 },
    // The bounded runtime display check cannot certify this wide coverage
    // interval. Retain both its unresolved-image and definite mass failures.
    Case { "coverage changes mass and exceeds the cheap enclosure",
      { .5F, .5F, .5F, .5F }, coverage_error, 12 },
    Case { "dark cutoff crossed", { 0x1p-12F, 0x1p-12F, 0x1p-12F, 1 },
      dark_error, 8 },
    Case { "zero weight skips normalization", { 0, 0, 0, 0 }, complete, 0 },
    Case { "another candidate", { 1, 1, 1, 1 }, stale, 16 },
    Case { "missing candidate", { 1, 1, 1, 1 }, missing, 16 },
    Case { "incomplete product mask", { 1, 1, 1, 1 }, incomplete, 16 },
    Case { "invalid coefficient", { 1, 1, 1, 1 }, invalid, 16 },
    Case { "current resolve ignores prospective fields", { 1, 1, 1, 1 },
      current_only, 0, true, true, scene_bit | (1U << 9U) },
    Case { "current resolve requires all producers", { 1, 1, 1, 1 }, complete,
      16, true, true, scene_bit | (1U << 9U) },
    Case { "current resolve preserves retained errors", { 1, 1, 1, 1 },
      current_retained, 12, true, true, scene_bit | (1U << 9U) },
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    // An exact 8192 anchor fixes candidate P=1 independently of the case.
    const std::array pixels { test.pixel, Pixel { 8192, 8192, 8192, 1 } };
    const auto signal = MakeSignal(2U, 1U, pixels);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    const auto cleared = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kCopySource);
    EXPECT_EQ(cleared.scene_error.flags, 0U);
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(test.error) });
    upload->Update(&test.error, sizeof(test.error), 0U);
    {
      auto recorder = AcquireRecorder("Composed admission fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, scene_error), *upload, 0U,
        sizeof(test.error));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = test.metering,
      .coverage = true,
      .composed_error = true } };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products,
      { .composition_products = test.required_products },
      test.current_store
        ? postprocess::ExposurePass::SuitabilityScale::kCurrentFrame
        : postprocess::ExposurePass::SuitabilityScale::kCandidate));
    const auto report = Read<HdrSuitabilityData>(
      *(test.current_store ? frame->conversion_buffer
                           : frame->suitability_buffer),
      ResourceStates::kShaderResource);
    EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
    EXPECT_EQ(report.failure_flags, test.failure);
    EXPECT_EQ(report.checked_samples, 2U);
    if (test.current_store) {
      continue;
    }
    ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = 1U,
        .expected_products = scene_bit,
        .invalidate_previous = true }));
    const auto status = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(
      status.completed.fp16_eligible_streak, test.failure == 0U ? 1U : 0U);
    EXPECT_EQ(status.scene_error.flags, test.error.flags);
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(ExposureGpuTest, ComposedCoverageRequiresValidOpaqueDepth)
{
  struct Case {
    const char* name;
    float depth;
    bool reverse;
    bool supplied;
    unsigned width;
    float alpha;
    bool admitted;
  };
  const std::array cases {
    Case { "reverse opaque", .5F, true, true, 2U, 1, true },
    Case { "forward opaque", .5F, false, true, 2U, 1, true },
    Case { "reverse near", 1, true, true, 2U, 1, true },
    Case { "forward near", 0, false, true, 2U, 1, true },
    Case { "reverse far", 0, true, true, 2U, 1, false },
    Case { "forward far", 1, false, true, 2U, 1, false },
    Case { "uncertain horizon", .0005F, true, true, 2U, 1, false },
    Case { "no depth", .5F, true, false, 2U, 1, false },
    Case { "partial coverage", .5F, true, true, 2U, .5F, false },
    Case { "mismatched depth", .5F, false, true, 1U, 1, false },
    Case { "negative depth", -.1F, true, true, 2U, 1, false },
    Case { "depth above one", 1.1F, false, true, 2U, 1, false },
    Case { "NaN depth", std::numeric_limits<float>::quiet_NaN(), true, true, 2U,
      1, false },
    Case { "infinite depth", std::numeric_limits<float>::infinity(), false,
      true, 2U, 1, false },
    Case { "negative subnormal depth", std::bit_cast<float>(0x80000001U), false,
      true, 2U, 1, false },
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    const std::array pixels { Pixel { .5F, .5F, .5F, test.alpha },
      Pixel { 8192, 8192, 8192, 1 } };
    const auto signal = MakeSignal(2U, 1U, pixels);
    const auto depth = Uniform(test.depth, test.width, 1U);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    const auto error = HdrSceneErrorData {
      .current_coverage_absolute = .001F,
      .candidate_pre_exposure = 1.0F,
      .checked_products = 1U << 10U,
      .flags = 1U,
    };
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(error) });
    upload->Update(&error, sizeof(error), 0U);
    {
      auto recorder = AcquireRecorder("Opaque coverage fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, scene_error), *upload, 0U,
        sizeof(error));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true,
      .coverage = true,
      .composed_error = true } };
    const auto composition = postprocess::ExposurePass::SceneComposition {
      .opaque_depth = test.supplied ? depth.texture.get() : nullptr,
      .opaque_depth_srv
      = test.supplied ? depth.srv : kInvalidShaderVisibleIndex,
      .reverse_z = test.reverse,
    };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products,
      { .scene_composition = composition },
      postprocess::ExposurePass::SuitabilityScale::kCurrentFrame));
    const auto result = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(result.failure_flags == 0U, test.admitted);
    if (!test.admitted) {
      EXPECT_NE(result.failure_flags & 8U, 0U);
    }
    EXPECT_EQ(result.checked_samples, 2U);
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

} // namespace oxygen::vortex::testing::exposure
