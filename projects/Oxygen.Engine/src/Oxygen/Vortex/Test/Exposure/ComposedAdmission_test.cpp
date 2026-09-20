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

using graphics::ResourceStates;

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
    engine::ToneMapper mapper;
  };
  const std::array cases {
    Case {
      .name = "gamma reveals below-float-budget loss",
      .radiance = 8e-6F,
      .alpha = 1,
      .gamma = 2.2F,
      .alpha_error = 0,
      .background = false,
      .background_value = 0,
      .failure = 4,
      .mapper = engine::ToneMapper::kNone,
    },
    Case {
      .name = "linear display keeps loss insignificant",
      .radiance = 8e-6F,
      .alpha = 1,
      .gamma = 1,
      .alpha_error = 0,
      .background = false,
      .background_value = 0,
      .failure = 0,
      .mapper = engine::ToneMapper::kNone,
    },
    Case {
      .name = "white background exposes coverage error",
      .radiance = 0,
      .alpha = .5F,
      .gamma = 2.2F,
      .alpha_error = .1F,
      .background = true,
      .background_value = 1,
      .failure = 4,
      .mapper = engine::ToneMapper::kNone,
    },
    Case {
      .name = "black image does not require unused coverage",
      .radiance = 0,
      .alpha = .5F,
      .gamma = 2.2F,
      .alpha_error = .1F,
      .background = true,
      .background_value = 0,
      .failure = 0,
      .mapper = engine::ToneMapper::kNone,
    },
    Case {
      .name = "half coverage changes dark background",
      .radiance = 0,
      .alpha = .9987F,
      .gamma = 2.2F,
      .alpha_error = 0,
      .background = true,
      .background_value = 1,
      .failure = 4,
      .mapper = engine::ToneMapper::kNone,
    },
    Case {
      .name = "ACES toe hides tiny radiance",
      .radiance = 8e-6F,
      .alpha = 1,
      .gamma = 2.2F,
      .alpha_error = 0,
      .background = false,
      .background_value = 0,
      .failure = 0,
      .mapper = engine::ToneMapper::kAcesFitted,
    },
    Case {
      .name = "Filmic exposes tiny radiance",
      .radiance = 8e-6F,
      .alpha = 1,
      .gamma = 2.2F,
      .alpha_error = 0,
      .background = false,
      .background_value = 0,
      .failure = 4,
      .mapper = engine::ToneMapper::kFilmic,
    },
    Case {
      .name = "Reinhard exposes tiny radiance",
      .radiance = 8e-6F,
      .alpha = 1,
      .gamma = 2.2F,
      .alpha_error = 0,
      .background = false,
      .background_value = 0,
      .failure = 4,
      .mapper = engine::ToneMapper::kReinhard,
    },
  };
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    auto scene = scene::Scene("Display admission", 1U);
    scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    auto& background
      = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
    background.SetEnabled(test.background);
    background.SetColorRgb({
      test.background_value,
      test.background_value,
      test.background_value,
    });
    ctx_.scene = observer_ptr {
      &scene,
    };
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = 0.0F;
    const auto resolved = ResolvedPostProcessConfig::Resolve({
      .exposure = settings,
      .tone_mapper = test.mapper,
      .enable_bloom = false,
      .gamma = test.gamma,
    });
    if (!resolved.has_value()) {
      FAIL() << "Expected resolved to contain a value";
    }
    const std::array<Pixel, 1> pixels {
      Pixel {
        test.radiance,
        test.radiance,
        test.radiance,
        test.alpha,
      },
    };
    const auto signal = MakeSignal(1U, 1U, pixels);
    const auto anchor = Uniform(0x1p33F);
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = SubmitCommands("Vortex Exposure Frame",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->ResolveFrame(ctx_, recorder, *resolved, frame_inputs);
      });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, *resolved).executed);
    const auto error = HdrSceneErrorData {
      .candidate_coverage_absolute = test.alpha_error,
      .candidate_pre_exposure = 0x1p-20F,
      .checked_products = (1U << 10U) | 1U,
      .flags = 3U,
    };
    auto upload = CreateUploadBuffer(SizeBytes {
      sizeof(error),
    });
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
        .texture = anchor.texture.get(),
        .srv = anchor.srv,
        .id = 1U,
      },
      postprocess::ExposurePass::HdrProduct {
        .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 11U,
        .coverage = test.background,
        .composed_error = true,
      },
    };
    ASSERT_TRUE(SubmitCommands("Vortex Exposure Suitability",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->EvaluateFp16Products(
          ctx_, recorder, frame, *resolved, products, {});
      }));
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
    const char* name {};
    Pixel pixel {
      1,
      1,
      1,
      1,
    };
    HdrSceneErrorData error;
    std::uint32_t failure {
      0U,
    };
    bool metering {
      true,
    };
    bool current_store {
      false,
    };
    std::uint32_t required_products {
      0U,
    };
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
  std::array<Case, 14> cases {};
  for (auto& test_case : cases) {
    test_case.error = complete;
  }
  cases.at(0).name = "exact";
  cases.at(1).name = "upstream candidate differs";
  cases.at(1).error = candidate_error;
  cases.at(1).failure = 12;
  cases.at(2).name = "retained reference differs";
  cases.at(2).error = retained_error;
  cases.at(2).failure = 12;
  cases.at(3).name = "insignificant candidate";
  cases.at(3).error = tiny_error;
  // The bounded runtime display check cannot certify this wide coverage
  // interval. Retain both its unresolved-image and definite mass failures.
  cases.at(4).name = "coverage changes mass and exceeds the cheap enclosure";
  cases.at(4).pixel = {
    .5F,
    .5F,
    .5F,
    .5F,
  };
  cases.at(4).error = coverage_error;
  cases.at(4).failure = 12;
  cases.at(5).name = "dark cutoff crossed";
  cases.at(5).pixel = {
    0x1p-12F,
    0x1p-12F,
    0x1p-12F,
    1,
  };
  cases.at(5).error = dark_error;
  cases.at(5).failure = 8;
  cases.at(6).name = "zero weight skips normalization";
  cases.at(6).pixel = {
    0,
    0,
    0,
    0,
  };
  cases.at(7).name = "another candidate";
  cases.at(7).error = stale;
  cases.at(7).failure = 16;
  cases.at(8).name = "missing candidate";
  cases.at(8).error = missing;
  cases.at(8).failure = 16;
  cases.at(9).name = "incomplete product mask";
  cases.at(9).error = incomplete;
  cases.at(9).failure = 16;
  cases.at(10).name = "invalid coefficient";
  cases.at(10).error = invalid;
  cases.at(10).failure = 16;
  cases.at(11).name = "current resolve ignores prospective fields";
  cases.at(11).error = current_only;
  cases.at(11).current_store = true;
  cases.at(11).required_products = scene_bit | (1U << 9U);
  cases.at(12).name = "current resolve requires all producers";
  cases.at(12).failure = 16;
  cases.at(12).current_store = true;
  cases.at(12).required_products = scene_bit | (1U << 9U);
  cases.at(13).name = "current resolve preserves retained errors";
  cases.at(13).error = current_retained;
  cases.at(13).failure = 12;
  cases.at(13).current_store = true;
  cases.at(13).required_products = scene_bit | (1U << 9U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    // An exact 8192 anchor fixes candidate P=1 independently of the case.
    const std::array pixels {
      test.pixel,
      Pixel {
        8192,
        8192,
        8192,
        1,
      },
    };
    const auto signal = MakeSignal(2U, 1U, pixels);
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = SubmitCommands("Vortex Exposure Frame",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->ResolveFrame(ctx_, recorder, config, frame_inputs);
      });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    const auto cleared = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kCopySource);
    EXPECT_EQ(cleared.scene_error.flags, 0U);
    auto upload = CreateUploadBuffer(SizeBytes {
      sizeof(test.error),
    });
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
    const std::array products {
      postprocess::ExposurePass::HdrProduct {
        .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 11U,
        .metering = test.metering,
        .coverage = true,
        .composed_error = true,
      },
    };
    {
      auto exposure_inputs = postprocess::ExposurePass::Inputs {};
      exposure_inputs.composition_products = test.required_products;
      ASSERT_TRUE(SubmitCommands("Vortex Exposure Suitability",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return pass_->EvaluateFp16Products(ctx_, recorder, frame, config,
            products, exposure_inputs,
            test.current_store
              ? postprocess::ExposurePass::SuitabilityScale::kCurrentFrame
              : postprocess::ExposurePass::SuitabilityScale::kCandidate);
        }));
    }
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
    ASSERT_TRUE(SubmitCommands("Vortex FP16 Eligibility",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->FinalizeFp16Suitability(ctx_, recorder, frame,
          {
            .product_layout_revision = 1U,
            .expected_products = scene_bit,
            .invalidate_previous = true,
          });
      }));
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
    Case {
      .name = "reverse opaque",
      .depth = .5F,
      .reverse = true,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = true,
    },
    Case {
      .name = "forward opaque",
      .depth = .5F,
      .reverse = false,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = true,
    },
    Case {
      .name = "reverse near",
      .depth = 1,
      .reverse = true,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = true,
    },
    Case {
      .name = "forward near",
      .depth = 0,
      .reverse = false,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = true,
    },
    Case {
      .name = "reverse far",
      .depth = 0,
      .reverse = true,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "forward far",
      .depth = 1,
      .reverse = false,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "uncertain horizon",
      .depth = .0005F,
      .reverse = true,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "no depth",
      .depth = .5F,
      .reverse = true,
      .supplied = false,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "partial coverage",
      .depth = .5F,
      .reverse = true,
      .supplied = true,
      .width = 2U,
      .alpha = .5F,
      .admitted = false,
    },
    Case {
      .name = "mismatched depth",
      .depth = .5F,
      .reverse = false,
      .supplied = true,
      .width = 1U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "negative depth",
      .depth = -.1F,
      .reverse = true,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "depth above one",
      .depth = 1.1F,
      .reverse = false,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "NaN depth",
      .depth = std::numeric_limits<float>::quiet_NaN(),
      .reverse = true,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "infinite depth",
      .depth = std::numeric_limits<float>::infinity(),
      .reverse = false,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
    Case {
      .name = "negative subnormal depth",
      .depth = std::bit_cast<float>(0x80000001U),
      .reverse = false,
      .supplied = true,
      .width = 2U,
      .alpha = 1,
      .admitted = false,
    },
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    const std::array pixels {
      Pixel {
        .5F,
        .5F,
        .5F,
        test.alpha,
      },
      Pixel {
        8192,
        8192,
        8192,
        1,
      },
    };
    const auto signal = MakeSignal(2U, 1U, pixels);
    const auto depth = Uniform(test.depth, test.width, 1U);
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = SubmitCommands("Vortex Exposure Frame",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->ResolveFrame(ctx_, recorder, config, frame_inputs);
      });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    const auto error = HdrSceneErrorData {
      .current_coverage_absolute = .001F,
      .candidate_pre_exposure = 1.0F,
      .checked_products = 1U << 10U,
      .flags = 1U,
    };
    auto upload = CreateUploadBuffer(SizeBytes {
      sizeof(error),
    });
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
    const std::array products {
      postprocess::ExposurePass::HdrProduct {
        .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 11U,
        .metering = true,
        .coverage = true,
        .composed_error = true,
      },
    };
    const auto composition = postprocess::ExposurePass::SceneComposition {
      .opaque_depth = test.supplied ? depth.texture.get() : nullptr,
      .opaque_depth_srv
      = test.supplied ? depth.srv : kInvalidShaderVisibleIndex,
      .reverse_z = test.reverse,
    };
    {
      auto exposure_inputs = postprocess::ExposurePass::Inputs {};
      exposure_inputs.scene_composition = composition;
      ASSERT_TRUE(SubmitCommands("Vortex Exposure Suitability",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return pass_->EvaluateFp16Products(ctx_, recorder, frame, config,
            products, exposure_inputs,
            postprocess::ExposurePass::SuitabilityScale::kCurrentFrame);
        }));
    }
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
