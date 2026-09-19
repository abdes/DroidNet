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
#include <span>

#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(ExposureGpuTest, OpaqueApErrorInvalidatesWhenInputCaptureChanges)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto frame = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(frame, nullptr);
  const auto small_source = Uniform(1.0F);
  const auto large = Uniform(100.0F);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *small_source.texture, small_source.srv));
  const HdrErrorBoundsData bounds { .transmittance_absolute = .125F };
  auto upload = CreateUploadBuffer(SizeBytes { sizeof(bounds) });
  upload->Update(&bounds, sizeof(bounds), 0U);
  {
    auto recorder = AcquireRecorder("Opaque AP dependency bounds");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      recorder->AdoptKnownResourceState(*frame->current_state->status_buffer));
    recorder->RequireResourceState(
      *frame->current_state->status_buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(
      *frame->current_state->status_buffer, 96U, *upload, 0U, sizeof(bounds));
    recorder->RequireResourceStateFinal(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  }
  const auto read_bound = [&] {
    return Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess)
      .opaque_ap_error;
  };
  ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, 1.0F));
  ASSERT_TRUE(pass_->HasOpaqueApError(frame));
  EXPECT_GE(read_bound().rgb_absolute, .125F);
  ASSERT_TRUE(
    pass_->CapturePreEnvironmentRange(ctx_, frame, *large.texture, large.srv));
  EXPECT_FALSE(pass_->HasOpaqueApError(frame));
  EXPECT_EQ(read_bound().valid, 0U);
  ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, 1.0F));
  EXPECT_GE(read_bound().rgb_absolute, 12.5F);

  for (const bool invalid_srv : { true, false }) {
    SCOPED_TRACE(invalid_srv);
    auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
    if (!invalid_srv) {
      backend.fail_recorder_name = "Vortex Exposure PreEnvironment Range";
    }
    EXPECT_FALSE(
      pass_->CapturePreEnvironmentRange(ctx_, frame, *small_source.texture,
        invalid_srv ? kInvalidShaderVisibleIndex : small_source.srv));
    backend.fail_recorder_name.clear();
    EXPECT_FALSE(pass_->HasPreEnvironmentRange(frame));
    EXPECT_FALSE(pass_->HasOpaqueApError(frame));
    EXPECT_FALSE(pass_->PropagateOpaqueApError(ctx_, frame, 1.0F));
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *small_source.texture, small_source.srv));
    EXPECT_FALSE(pass_->HasOpaqueApError(frame));
    EXPECT_EQ(read_bound().valid, 0U);
    ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, 1.0F));
    EXPECT_TRUE(pass_->HasOpaqueApError(frame));
    EXPECT_GE(read_bound().rgb_absolute, .125F);
    EXPECT_LT(read_bound().rgb_absolute, .126F);
  }
}

NOLINT_TEST_F(ExposureGpuTest, OpaqueApErrorUsesInputPeakAndActualGain)
{
  struct Case {
    float peak;
    float gain;
    HdrErrorBoundsData bounds;
    bool valid { true };
    float ev { 0.0F };
  };
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const std::array cases { Case { 32, 8,
                             { .rgb_relative = .01F,
                               .rgb_absolute = .002F,
                               .transmittance_relative = .02F,
                               .transmittance_absolute = 1e-5F } },
    Case { 8192, 1, { .transmittance_absolute = 1e-4F } },
    Case { 1, 1e6F, { .rgb_absolute = 1e-8F } },
    Case { 16, 8, { .rgb_absolute = .002F, .transmittance_absolute = 1e-5F },
      true, 3 },
    Case { 0, 2, { .rgb_absolute = .125F } }, Case { 32, 1, {} },
    Case { 32, 0, { .rgb_absolute = infinity } },
    Case { 32, .00005F, { .rgb_absolute = infinity } },
    Case { 32, .0001F, { .rgb_absolute = .25F } },
    Case { 1e-30F, 1, { .transmittance_absolute = 1e-30F } },
    Case { 1, 1, { .rgb_absolute = std::bit_cast<float>(1U) } },
    Case { 1, 0x1p100F, { .rgb_absolute = std::bit_cast<float>(1U) } },
    Case { 1, 1, { .rgb_relative = std::bit_cast<float>(1U) } },
    Case { 1, infinity, {}, false }, Case { 1, -1, {}, false },
    Case { 1, nan, {}, false }, Case { 1, 1, { .rgb_relative = 1 }, false },
    Case { 1, 1, { .transmittance_relative = 1 }, false },
    Case { 1, 1, { .rgb_absolute = -1 }, false },
    Case { 1, 1, { .rgb_absolute = std::bit_cast<float>(0x80000001U) }, false },
    Case { 1, std::bit_cast<float>(0x80000001U), {}, false },
    Case { 1, 1, { .transmittance_absolute = nan }, false },
    Case { -1, 1, {}, false }, Case { infinity, 1, {}, false },
    Case {
      1, std::numeric_limits<float>::max(), { .rgb_absolute = 2 }, false } };
  const auto capture = BeginOptionalCapture();
  for (std::size_t index = 0U; index < cases.size(); ++index) {
    SCOPED_TRACE(index);
    const auto& test = cases[index];
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = test.ev;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
    ASSERT_NE(frame, nullptr);
    EXPECT_FALSE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    const auto source = Uniform(test.peak);
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *source.texture, source.srv));
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(test.bounds) });
    upload->Update(&test.bounds, sizeof(test.bounds), 0U);
    {
      auto recorder = AcquireRecorder("Opaque AP controlled bounds");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer, 96U, *upload,
        0U, sizeof(test.bounds));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    }
    const auto before = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    EXPECT_TRUE(pass_->HasOpaqueApError(frame));
    const auto after = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(std::memcmp(&before, &after, 144U), 0);
    const auto& actual = after.opaque_ap_error;
    EXPECT_EQ(actual.valid, test.valid ? 1U : 0U);
    EXPECT_EQ(actual.reserved, 0U);
    if (!test.valid) {
      EXPECT_TRUE(std::isinf(actual.rgb_absolute));
      continue;
    }
    // Independent double arithmetic over the authored binary32 inputs. These
    // cases have at most two products and a sum; the GPU rounds outward.
    const double relative = test.gain < .0001F
      ? 0
      : std::max(double(test.bounds.rgb_relative),
          double(test.bounds.transmittance_relative));
    const double absolute = test.gain < .0001F
      ? 0
      : double(test.gain) * test.bounds.rgb_absolute
        + std::ldexp(double(test.peak), int(test.ev))
          * test.bounds.transmittance_absolute;
    EXPECT_GE(double(actual.rgb_relative), relative);
    EXPECT_GE(double(actual.rgb_absolute), absolute);
    const auto promote = [](double value) {
      return value > 0 && value < std::numeric_limits<float>::min()
        ? double(std::numeric_limits<float>::min())
        : value;
    };
    const double operand_ceiling = test.gain < .0001F
      ? 0
      : promote(test.gain) * promote(test.bounds.rgb_absolute)
        + promote(std::ldexp(double(test.peak), int(test.ev)))
          * promote(test.bounds.transmittance_absolute);
    EXPECT_LE(double(actual.rgb_absolute),
      operand_ceiling * 1.00001
        + double(std::numeric_limits<float>::min()) * 1.00001);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.rgb_relative),
      test.gain < .0001F
        ? 0U
        : std::max(std::bit_cast<std::uint32_t>(test.bounds.rgb_relative)
              & 0x7fffffffU,
            std::bit_cast<std::uint32_t>(test.bounds.transmittance_relative)
              & 0x7fffffffU));
    if (absolute == 0) {
      EXPECT_EQ(actual.rgb_absolute, 0.0F);
    }
    auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
    backend.fail_recorder_name = "Vortex Exposure Opaque AP Error";
    EXPECT_FALSE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    EXPECT_FALSE(pass_->HasOpaqueApError(frame));
    backend.fail_recorder_name.clear();
    ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    EXPECT_TRUE(pass_->HasOpaqueApError(frame));
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, ProspectiveProductBoundsIncludeRetainedErrorAndSelectedScale)
{
  const std::array<float, 8> observed { 1.00075F, 1.0F / 3.0F, 0x1p-25F,
    3.0F * 0x1p-25F, 8190.0F, .25F, .5F, 0.0F };
  // Exact independent binary16 results at P=1, including ties and exponent
  // carry.
  const std::array<double, 8> narrowed { 1.0009765625, 1365.0 / 4096.0, 0.0,
    0x1p-23, 8192.0, .25, .5, 0.0 };
  std::array<Pixel, 8> pixels {};
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = { observed[i], observed[i], observed[i], 1.0F - 0x1p-16F };
  }
  const auto image = MakeSignal(4U, 2U, pixels);
  const auto volume = MakeSignal(2U, 2U, pixels, 2U);
  const auto anchor = Uniform(8192.0F);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  ASSERT_TRUE(RecordShared(anchor, config).executed);
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = anchor.texture.get(), .srv = anchor.srv, .id = 11U },
    postprocess::ExposurePass::HdrProduct {
      .texture = image.texture.get(), .srv = image.srv, .id = 5U },
    postprocess::ExposurePass::HdrProduct { .texture = volume.texture.get(),
      .srv = volume.srv,
      .id = 6U,
      .transmittance = true },
    postprocess::ExposurePass::HdrProduct { .texture = volume.texture.get(),
      .srv = volume.srv,
      .id = 10U,
      .transmittance = true },
  };
  const auto capture = BeginOptionalCapture();
  std::array<HdrErrorBoundsData, 3> exact_result {};
  for (unsigned trial = 0U; trial < 4U; ++trial) {
    SCOPED_TRACE(trial);
    std::array<HdrErrorBoundsData, 3> prior {};
    if (trial == 1U) {
      prior.fill({ .rgb_relative = .002F,
        .rgb_absolute = .01F,
        .transmittance_relative = .001F,
        .transmittance_absolute = .0001F });
    }
    if (trial == 2U) {
      prior[2].rgb_relative = 1.0F;
    }
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(prior) });
    upload->Update(prior.data(), sizeof(prior), 0U);
    {
      auto recorder = AcquireRecorder("Prospective bounds fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, producer_errors), *upload, 0U,
        sizeof(prior));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
    const auto status = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    for (std::size_t product = 0; product < prior.size(); ++product) {
      const auto& actual = status.candidate_errors[product];
      if (trial == 2U && product == 2U) {
        EXPECT_TRUE(std::isinf(actual.rgb_absolute));
        continue;
      }
      for (std::size_t i = 0; i < observed.size(); ++i) {
        const double low = std::max(0.0,
          (double(observed[i]) - prior[product].rgb_absolute)
            / (1.0 + prior[product].rgb_relative));
        const double high = (double(observed[i]) + prior[product].rgb_absolute)
          / (1.0 - prior[product].rgb_relative);
        for (const double reference : { low, high }) {
          EXPECT_LE(std::abs(narrowed[i] - reference),
            actual.rgb_relative * reference + actual.rgb_absolute);
        }
      }
      if (product != 0U) {
        const double t = double(pixels[0][3]);
        const double low = std::max(0.0,
          (t - prior[product].transmittance_absolute)
            / (1.0 + prior[product].transmittance_relative));
        const double high = std::min(1.0,
          (t + prior[product].transmittance_absolute)
            / (1.0 - prior[product].transmittance_relative));
        for (const double reference : { low, high }) {
          EXPECT_LE(std::abs(1.0 - reference),
            actual.transmittance_relative * reference
              + actual.transmittance_absolute);
        }
      }
    }
    if (trial == 0U) {
      exact_result = status.candidate_errors;
    }
    if (trial == 3U) {
      EXPECT_EQ(std::memcmp(exact_result.data(), status.candidate_errors.data(),
                  sizeof(exact_result)),
        0);
    }
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}
} // namespace oxygen::vortex::testing::exposure
