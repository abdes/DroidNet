//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>

#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>

namespace oxygen::vortex::testing::exposure {

using graphics::ResourceStates;

NOLINT_TEST_F(ExposureGpuTest, ProducerMaximumReuseMatchesCompleteScan)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  for (const unsigned id : {
         6U,
         10U,
       }) {
    for (const auto format : {
           Format::kRGBA32Float,
           Format::kRGBA16Float,
         }) {
      for (const bool retained : {
             false,
             true,
           }) {
        SCOPED_TRACE(::testing::Message()
          << "product=" << id << " format=" << static_cast<unsigned>(format)
          << " retained=" << retained);
        const std::array<Pixel, 1> values {
          Pixel {
            .25F,
            .5F,
            2.0F,
            .5F,
          },
        };
        const auto signal = MakeSignal(9U, 3U, values, 2U, format);
        const auto bounds = retained
          ? HdrErrorBoundsData { .rgb_relative = 1.0F / 128.0F,
              .rgb_absolute = 1.0F / 64.0F, }
          : HdrErrorBoundsData {};
        const auto run
          = [&](const bool gradients, HdrSuitabilityData& report) -> void {
          ctx_.frame_sequence = frame::SequenceNumber {
            ++sequence_,
          };
          auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
          frame_inputs.use_fp32 = true;
          const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
          ASSERT_NE(frame, nullptr);
          ASSERT_TRUE(RecordShared(meter, config).executed);
          auto upload = CreateUploadBuffer(SizeBytes {
            sizeof(bounds),
          });
          upload->Update(&bounds, sizeof(bounds), 0U);
          {
            auto recorder = AcquireRecorder("Producer maximum bounds");
            EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
            ASSERT_TRUE(recorder->AdoptKnownResourceState(
              *frame->current_state->status_buffer));
            recorder->RequireResourceState(
              *frame->current_state->status_buffer, ResourceStates::kCopyDest);
            recorder->FlushBarriers();
            recorder->CopyBuffer(*frame->current_state->status_buffer,
              id == 6U ? 96U : 112U, *upload, 0U, sizeof(bounds));
            recorder->RequireResourceStateFinal(
              *frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
          }
          const auto product = postprocess::ExposurePass::HdrProduct {
            .texture = signal.texture.get(),
            .srv = signal.srv,
            .id = id,
            .transmittance = true,
          };
          if (gradients) {
            ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame, product));
            const auto status = Read<ExposureStatusStorage>(
              *frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
            EXPECT_EQ(status.filter_gradients.at(id == 6U ? 1U : 2U).flags, 1U);
          }
          ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config,
            std::span {
              &product,
              1U,
            },
            {}));
          report = Read<HdrSuitabilityData>(
            *frame->suitability_buffer, ResourceStates::kShaderResource);
        };
        HdrSuitabilityData ordinary;
        HdrSuitabilityData reused;
        ASSERT_NO_FATAL_FAILURE(run(false, ordinary));
        ASSERT_NO_FATAL_FAILURE(run(true, reused));
        EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                         &ordinary,
                                         1,
                                       }),
          std::as_bytes(std::span {
            &reused,
            1,
          })));
        EXPECT_EQ(reused.checked_products, 1U << (id - 1U));
        EXPECT_EQ(reused.checked_samples, 54U);
        EXPECT_EQ(reused.candidate_pre_exposure, 4096.0F);
        if (retained) {
          const auto exact_maximum
            = (2.0 + (1.0 / 64.0)) / (1.0 - (1.0 / 128.0));
          EXPECT_GE(
            static_cast<double>(reused.maximum_scene_rgb), exact_maximum);
          EXPECT_LE(static_cast<double>(reused.maximum_scene_rgb),
            exact_maximum * 1.00001);
        } else {
          // One outward product rounds the exact peak 2 by four FP32 ULPs.
          EXPECT_EQ(std::bit_cast<std::uint32_t>(reused.maximum_scene_rgb),
            0x40000004U);
          EXPECT_EQ(reused.failure_flags, 0U);
        }
      }
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, ProducerMaximumReuseFallsBackForUnprovenRecords)
{
  enum class Fault : std::uint8_t {
    kMissing,
    kIncomplete,
    kInvalid,
    kInfiniteMaximum,
    kDifferentTexture,
    kSubmissionFailed,
    kTransmissionMismatch,
    kNonfiniteAlpha,
    kSignedRgb,
    kSkyAlpha
  };
  const auto faults = std::array {
    Fault::kMissing,
    Fault::kIncomplete,
    Fault::kInvalid,
    Fault::kInfiniteMaximum,
    Fault::kDifferentTexture,
    Fault::kSubmissionFailed,
    Fault::kTransmissionMismatch,
    Fault::kNonfiniteAlpha,
    Fault::kSignedRgb,
    Fault::kSkyAlpha,
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  for (const auto fault : faults) {
    SCOPED_TRACE(static_cast<unsigned>(fault));
    const auto id = fault == Fault::kSkyAlpha ? 5U : 6U;
    const auto index = id == 5U ? 0U : 1U;
    const auto nonfinite_alpha = fault == Fault::kNonfiniteAlpha
      || fault == Fault::kSkyAlpha || fault == Fault::kTransmissionMismatch;
    const auto value = Pixel {
      fault == Fault::kSignedRgb ? -2.0F : 2.0F,
      .25F,
      .5F,
      nonfinite_alpha ? std::numeric_limits<float>::quiet_NaN() : .5F,
    };
    const std::array<Pixel, 1> values {
      value,
    };
    const auto signal = MakeSignal(9U, 1U, values);
    const auto different = Uniform(.25F, 9U, 1U);
    const auto run
      = [&](const bool gradients, HdrSuitabilityData& report) -> void {
      ctx_.frame_sequence = frame::SequenceNumber {
        ++sequence_,
      };
      auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
      frame_inputs.use_fp32 = true;
      const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
      ASSERT_NE(frame, nullptr);
      ASSERT_TRUE(RecordShared(meter, config).executed);
      const auto product = postprocess::ExposurePass::HdrProduct {
        .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = id,
        .transmittance = id == 6U,
      };
      if (gradients && fault != Fault::kMissing) {
        auto recorded = product;
        if (fault == Fault::kDifferentTexture) {
          recorded.texture = different.texture.get();
          recorded.srv = different.srv;
        }
        if (fault == Fault::kTransmissionMismatch) {
          recorded.transmittance = false;
        }
        ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame, recorded));
        if (fault == Fault::kSubmissionFailed) {
          auto& backend = FailureBackend();
          backend.fail_recorder_name = "Vortex Exposure Filter Gradients";
          EXPECT_FALSE(pass_->GatherFilterGradients(ctx_, frame, product));
          backend.fail_recorder_name.clear();
          EXPECT_FALSE(pass_->HasFilterGradients(frame, id));
        }
        if (fault == Fault::kIncomplete || fault == Fault::kInvalid
          || fault == Fault::kInfiniteMaximum) {
          auto status
            = Read<ExposureStatusStorage>(*frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
          // An unusable record must not supply even a finite cached maximum.
          status.product_reference_rgb_max.at(index) = 128.0F;
          if (fault == Fault::kIncomplete) {
            status.filter_gradients.at(index).checked_texels = 1U;
          } else if (fault == Fault::kInvalid) {
            status.filter_gradients.at(index).flags = 3U;
          } else {
            status.product_reference_rgb_max.at(index)
              = std::numeric_limits<float>::infinity();
          }
          auto upload = CreateUploadBuffer(SizeBytes {
            sizeof(status),
          });
          upload->Update(&status, sizeof(status), 0U);
          {
            auto recorder
              = AcquireRecorder("Producer maximum unavailable certificate");
            EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
            ASSERT_TRUE(recorder->AdoptKnownResourceState(
              *frame->current_state->status_buffer));
            recorder->RequireResourceState(
              *frame->current_state->status_buffer, ResourceStates::kCopyDest);
            recorder->FlushBarriers();
            recorder->CopyBuffer(*frame->current_state->status_buffer, 0U,
              *upload, 0U, sizeof(status));
            recorder->RequireResourceStateFinal(
              *frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
          }
        }
      }
      ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config,
        std::span {
          &product,
          1U,
        },
        {}));
      report = Read<HdrSuitabilityData>(
        *frame->suitability_buffer, ResourceStates::kShaderResource);
    };
    HdrSuitabilityData ordinary;
    HdrSuitabilityData guarded;
    ASSERT_NO_FATAL_FAILURE(run(false, ordinary));
    ASSERT_NO_FATAL_FAILURE(run(true, guarded));
    EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                     &ordinary,
                                     1,
                                   }),
      std::as_bytes(std::span {
        &guarded,
        1,
      })));
    if (nonfinite_alpha) {
      EXPECT_NE(guarded.failure_flags & 1U, 0U);
      EXPECT_EQ(guarded.rejected_samples, 9U);
    }
  }
}
} // namespace oxygen::vortex::testing::exposure
