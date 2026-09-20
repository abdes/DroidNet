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

using graphics::ResourceStates;

NOLINT_TEST_F(ExposureGpuTest, OpaqueApErrorInvalidatesWhenInputCaptureChanges)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  const auto frame = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(frame, nullptr);
  const auto small_source = Uniform(1.0F);
  const auto large = Uniform(100.0F);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *small_source.texture, small_source.srv));
  const HdrErrorBoundsData bounds {
    .transmittance_absolute = .125F,
  };
  auto upload = CreateUploadBuffer(SizeBytes {
    sizeof(bounds),
  });
  upload->Update(&bounds, sizeof(bounds), 0U);
  {
    auto recorder = AcquireRecorder("Opaque AP dependency bounds");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      recorder->AdoptKnownResourceState(*frame->current_state->status_buffer));
    recorder->RequireResourceState(
      *frame->current_state->status_buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(*frame->current_state->status_buffer,
      offsetof(ExposureStatusStorage, producer_errors)
        + sizeof(HdrErrorBoundsData),
      *upload, 0U, sizeof(bounds));
    recorder->RequireResourceStateFinal(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  }
  const auto read_bound = [&] -> HdrOpaqueApErrorData {
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

  for (const bool invalid_srv : {
         true,
         false,
       }) {
    SCOPED_TRACE(invalid_srv);
    auto& backend = FailureBackend();
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
    float peak {
      1.0F,
    };
    float gain {
      1.0F,
    };
    HdrErrorBoundsData bounds;
    bool valid {
      true,
    };
    float ev {
      0.0F,
    };
  };
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  std::array<Case, 25> cases {};
  cases.at(0).peak = 32;
  cases.at(0).gain = 8;
  cases.at(0).bounds.rgb_relative = .01F;
  cases.at(0).bounds.rgb_absolute = .002F;
  cases.at(0).bounds.transmittance_relative = .02F;
  cases.at(0).bounds.transmittance_absolute = 1e-5F;
  cases.at(1).peak = 8192;
  cases.at(1).bounds.transmittance_absolute = 1e-4F;
  cases.at(2).gain = 1e6F;
  cases.at(2).bounds.rgb_absolute = 1e-8F;
  cases.at(3).peak = 16;
  cases.at(3).gain = 8;
  cases.at(3).bounds.rgb_absolute = .002F;
  cases.at(3).bounds.transmittance_absolute = 1e-5F;
  cases.at(3).ev = 3;
  cases.at(4).peak = 0;
  cases.at(4).gain = 2;
  cases.at(4).bounds.rgb_absolute = .125F;
  cases.at(5).peak = 32;
  cases.at(6).peak = 32;
  cases.at(6).gain = 0;
  cases.at(6).bounds.rgb_absolute = infinity;
  cases.at(7).peak = 32;
  cases.at(7).gain = .00005F;
  cases.at(7).bounds.rgb_absolute = infinity;
  cases.at(8).peak = 32;
  cases.at(8).gain = .0001F;
  cases.at(8).bounds.rgb_absolute = .25F;
  cases.at(9).peak = 1e-30F;
  cases.at(9).bounds.transmittance_absolute = 1e-30F;
  cases.at(10).bounds.rgb_absolute = std::bit_cast<float>(1U);
  cases.at(11).gain = 0x1p100F;
  cases.at(11).bounds.rgb_absolute = std::bit_cast<float>(1U);
  cases.at(12).bounds.rgb_relative = std::bit_cast<float>(1U);
  cases.at(13).gain = infinity;
  cases.at(13).valid = false;
  cases.at(14).gain = -1;
  cases.at(14).valid = false;
  cases.at(15).gain = nan;
  cases.at(15).valid = false;
  cases.at(16).bounds.rgb_relative = 1;
  cases.at(16).valid = false;
  cases.at(17).bounds.transmittance_relative = 1;
  cases.at(17).valid = false;
  cases.at(18).bounds.rgb_absolute = -1;
  cases.at(18).valid = false;
  cases.at(19).bounds.rgb_absolute = std::bit_cast<float>(0x80000001U);
  cases.at(19).valid = false;
  cases.at(20).gain = std::bit_cast<float>(0x80000001U);
  cases.at(20).valid = false;
  cases.at(21).bounds.transmittance_absolute = nan;
  cases.at(21).valid = false;
  cases.at(22).peak = -1;
  cases.at(22).valid = false;
  cases.at(23).peak = infinity;
  cases.at(23).valid = false;
  cases.at(24).gain = std::numeric_limits<float>::max();
  cases.at(24).bounds.rgb_absolute = 2;
  cases.at(24).valid = false;
  const auto capture = BeginOptionalCapture();
  for (std::size_t index = 0U; index < cases.size(); ++index) {
    SCOPED_TRACE(index);
    const auto& test = cases.at(index);
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = test.ev;
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    const auto frame = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
    ASSERT_NE(frame, nullptr);
    EXPECT_FALSE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    const auto source = Uniform(test.peak);
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *source.texture, source.srv));
    auto upload = CreateUploadBuffer(SizeBytes {
      sizeof(test.bounds),
    });
    upload->Update(&test.bounds, sizeof(test.bounds), 0U);
    {
      auto recorder = AcquireRecorder("Opaque AP controlled bounds");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, producer_errors)
          + sizeof(HdrErrorBoundsData),
        *upload, 0U, sizeof(test.bounds));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    }
    const auto before = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    EXPECT_TRUE(pass_->HasOpaqueApError(frame));
    const auto after = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_TRUE(std::ranges::equal(
      std::as_bytes(std::span {
                      &before,
                      1,
                    })
        .first(offsetof(ExposureStatusStorage, opaque_ap_error)),
      std::as_bytes(std::span {
                      &after,
                      1,
                    })
        .first(offsetof(ExposureStatusStorage, opaque_ap_error))));
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
      : std::max(static_cast<double>(test.bounds.rgb_relative),
          static_cast<double>(test.bounds.transmittance_relative));
    const double absolute = test.gain < .0001F
      ? 0
      : (static_cast<double>(test.gain) * test.bounds.rgb_absolute)
        + (std::ldexp(static_cast<double>(test.peak), static_cast<int>(test.ev))
          * test.bounds.transmittance_absolute);
    EXPECT_GE(static_cast<double>(actual.rgb_relative), relative);
    EXPECT_GE(static_cast<double>(actual.rgb_absolute), absolute);
    const auto promote = [](double value) -> double {
      return value > 0 && value < std::numeric_limits<float>::min()
        ? static_cast<double>(std::numeric_limits<float>::min())
        : value;
    };
    const double operand_ceiling = test.gain < .0001F
      ? 0
      : (promote(test.gain) * promote(test.bounds.rgb_absolute))
        + (promote(std::ldexp(
             static_cast<double>(test.peak), static_cast<int>(test.ev)))
          * promote(test.bounds.transmittance_absolute));
    EXPECT_LE(static_cast<double>(actual.rgb_absolute),
      (operand_ceiling * 1.00001)
        + (static_cast<double>(std::numeric_limits<float>::min()) * 1.00001));
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
    auto& backend = FailureBackend();
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
  const std::array<float, 8> observed {
    1.00075F,
    1.0F / 3.0F,
    0x1p-25F,
    3.0F * 0x1p-25F,
    8190.0F,
    .25F,
    .5F,
    0.0F,
  };
  // Exact independent binary16 results at P=1, including ties and exponent
  // carry.
  const std::array<double, 8> narrowed {
    1.0009765625,
    1365.0 / 4096.0,
    0.0,
    0x1p-23,
    8192.0,
    .25,
    .5,
    0.0,
  };
  std::array<Pixel, 8> pixels {};
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels.at(i) = {
      observed.at(i),
      observed.at(i),
      observed.at(i),
      1.0F - 0x1p-16F,
    };
  }
  const auto image = MakeSignal(4U, 2U, pixels);
  const auto volume = MakeSignal(2U, 2U, pixels, 2U);
  const auto anchor = Uniform(8192.0F);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
  frame_inputs.use_fp32 = true;
  const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
  ASSERT_NE(frame, nullptr);
  ASSERT_TRUE(RecordShared(anchor, config).executed);
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = anchor.texture.get(),
      .srv = anchor.srv,
      .id = 11U,
    },
    postprocess::ExposurePass::HdrProduct {
      .texture = image.texture.get(),
      .srv = image.srv,
      .id = 5U,
    },
    postprocess::ExposurePass::HdrProduct {
      .texture = volume.texture.get(),
      .srv = volume.srv,
      .id = 6U,
      .transmittance = true,
    },
    postprocess::ExposurePass::HdrProduct {
      .texture = volume.texture.get(),
      .srv = volume.srv,
      .id = 10U,
      .transmittance = true,
    },
  };
  const auto capture = BeginOptionalCapture();
  std::array<HdrErrorBoundsData, 3> exact_result {};
  for (unsigned trial = 0U; trial < 4U; ++trial) {
    SCOPED_TRACE(trial);
    std::array<HdrErrorBoundsData, 3> prior {};
    if (trial == 1U) {
      prior.fill({
        .rgb_relative = .002F,
        .rgb_absolute = .01F,
        .transmittance_relative = .001F,
        .transmittance_absolute = .0001F,
      });
    }
    if (trial == 2U) {
      prior.at(2).rgb_relative = 1.0F;
    }
    auto upload = CreateUploadBuffer(SizeBytes {
      sizeof(prior),
    });
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
      const auto& actual = status.candidate_errors.at(product);
      if (trial == 2U && product == 2U) {
        EXPECT_TRUE(std::isinf(actual.rgb_absolute));
        continue;
      }
      for (std::size_t i = 0; i < observed.size(); ++i) {
        const double low = std::max(0.0,
          (static_cast<double>(observed.at(i)) - prior.at(product).rgb_absolute)
            / (1.0 + prior.at(product).rgb_relative));
        const double high = (static_cast<double>(observed.at(i))
                              + prior.at(product).rgb_absolute)
          / (1.0 - prior.at(product).rgb_relative);
        for (const double reference : {
               low,
               high,
             }) {
          EXPECT_LE(std::abs(narrowed.at(i) - reference),
            (actual.rgb_relative * reference) + actual.rgb_absolute);
        }
      }
      if (product != 0U) {
        const auto t = static_cast<double>(pixels.at(0).at(3));
        const double low = std::max(0.0,
          (t - prior.at(product).transmittance_absolute)
            / (1.0 + prior.at(product).transmittance_relative));
        const double high = std::min(1.0,
          (t + prior.at(product).transmittance_absolute)
            / (1.0 - prior.at(product).transmittance_relative));
        for (const double reference : {
               low,
               high,
             }) {
          EXPECT_LE(std::abs(1.0 - reference),
            (actual.transmittance_relative * reference)
              + actual.transmittance_absolute);
        }
      }
    }
    if (trial == 0U) {
      exact_result = status.candidate_errors;
    }
    if (trial == 3U) {
      EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                       exact_result,
                                     }),
        std::as_bytes(std::span {
          status.candidate_errors,
        })));
    }
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}
} // namespace oxygen::vortex::testing::exposure
