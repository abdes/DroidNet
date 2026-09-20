//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using graphics::ResourceStates;

NOLINT_TEST_F(
  ExposureGpuTest, Fp32SceneColorPreservesWideRangeRadianceAndCoverage)
{
  auto textures = SceneTextures(Backend(),
    { .extent = { 1U, 1U, }, .scene_color_format = Format::kRGBA32Float, });
  const auto& color = textures.GetSceneColorResource();
  ASSERT_EQ(color->GetDescriptor().format, Format::kRGBA32Float);
  const Pixel expected {
    0x1p30F,
    0x1p-24F,
    1.0F,
    .25F,
  };
  auto upload = CreateUploadBuffer(SizeBytes {
    256U,
  });
  upload->Update(expected.data(), sizeof(expected), 0U);
  {
    auto recorder = AcquireRecorder("FP32 SceneColor fixture upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    if (!recorder->AdoptKnownResourceState(*color)) {
      recorder->BeginTrackingResourceState(
        *color, color->GetDescriptor().initial_state);
    }
    recorder->RequireResourceState(*color, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 1U, .height = 1U, .depth = 1U, }, },
      *color);
    recorder->RequireResourceStateFinal(
      *color, ResourceStates::kShaderResource);
  }
  auto readback
    = GetReadbackManager()->CreateTextureReadback("FP32 SceneColor readback");
  {
    auto recorder = AcquireRecorder("FP32 SceneColor fixture readback");
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*color));
    ASSERT_TRUE(readback->EnqueueCopy(*recorder, *color, {}).has_value());
  }
  const auto mapped = readback->MapNow();
  if (!mapped.has_value()) {
    FAIL() << "Expected mapped to contain a value";
  }
  Pixel actual {};
  std::memcpy(actual.data(), mapped->Data(), sizeof(actual));
  EXPECT_EQ(actual, expected);
  FlushBackend();
}

NOLINT_TEST_F(ExposureGpuTest,
  SceneExposurePreparationFailurePreservesOutputAndRetriesStandalone)
{
  CheckSceneExposureRetry(false);
}

NOLINT_TEST_F(ExposureGpuTest,
  SceneExposurePreparationFailurePreservesOutputAndRetriesInsideFrame)
{
  CheckSceneExposureRetry(true);
}

NOLINT_TEST_F(ExposureGpuTest,
  LateExposureFailureAfterSuccessfulSiblingPreservesStandaloneOutput)
{
  CheckSceneExposureRetry(false, true);
}

NOLINT_TEST_F(ExposureGpuTest,
  LateExposureFailureAfterSuccessfulSiblingPreservesInsideFrameOutput)
{
  CheckSceneExposureRetry(true, true);
}

NOLINT_TEST_F(ExposureGpuTest,
  PreparedSceneExposureIsSolvedOnceAndPinnedAcrossResolvedColorConsumption)
{
  for (const bool persistent : {
         true,
         false,
       }) {
    SCOPED_TRACE(persistent);
    auto service = PostProcessService(*renderer_);
    ctx_.current_view.view_state_handle = persistent
      ? CompositionView::ViewStateHandle { 91U, }
      : CompositionView::kInvalidViewStateHandle;
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
    auto config = PostProcessConfig {
      .exposure = settings,
    };
    config.tone_mapper = engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    config.enable_bloom = false;
    config.bloom_intensity = 0.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    ASSERT_NE(SubmitCommands("Vortex Exposure Frame",
                [&](graphics::CommandRecorder& recorder) -> auto {
                  return service.PrepareFrameExposure(ctx_, recorder, true);
                }),
      nullptr);
    const auto accumulation = Uniform(.25F, 4U, 4U);
    const auto resolved = Uniform(.5F, 4U, 4U);
    auto& recorder_names = FailureBackend().recorder_names;
    recorder_names.clear();
    auto prepared_inputs = PostProcessService::Inputs {};
    prepared_inputs.scene_signal = accumulation.texture.get();
    prepared_inputs.scene_signal_srv = accumulation.srv;
    prepared_inputs.require_scene_range = true;
    const auto prepared = SubmitCommands(
      "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, prepared_inputs);
      });
    if (!prepared.has_value()) {
      FAIL() << "Expected prepared to contain a value";
    }
    ASSERT_NE(prepared->exposure.state, nullptr);
    const auto before = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_NEAR(before.displayed_scale, .72F, 2e-5F);
    EXPECT_NEAR(before.raw_metered_luminance, .25F, 2e-5F);
    {
      auto pixel_options = ServicePixelOptions {};
      pixel_options.tone_mapper = engine::ToneMapper::kAcesFitted;
      pixel_options.start_new_frame = false;
      pixel_options.prepared = &*prepared;
      // A different signal and mapper at Stage 22 must neither remeter nor
      // replace the configuration pinned when the accumulation was solved.
      EXPECT_NEAR(
        ServicePixel(service, resolved, settings, pixel_options), .36F, 2e-5F);
    }
    const auto after = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                     &before,
                                     1,
                                   }),
      std::as_bytes(std::span {
        &after,
        1,
      })));
    EXPECT_EQ(std::count(recorder_names.begin(), recorder_names.end(),
                "Vortex Exposure"),
      2);
    EXPECT_EQ(std::count(recorder_names.begin(), recorder_names.end(),
                "Vortex Exposure Final Scene Range"),
      0);
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  CombinedSceneRangePreservesSeedRetryInvalidMeterAndRetainedState)
{
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto invalid = Uniform(std::numeric_limits<float>::infinity(), 4U, 4U);
  for (const bool fp32_only : {
         false,
         true,
       }) {
    SCOPED_TRACE(fp32_only);
    auto service = PostProcessService(*renderer_);
    auto config = PostProcessConfig {};
    config.exposure.key = 12.5F;
    service.SetConfig(config);
    ctx_.current_view.view_id = ViewId {
      fp32_only ? 94U : 93U,
    };
    ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
      fp32_only ? 94U : 93U,
    };
    ctx_.current_view.hdr_fp32_only = fp32_only;
    ctx_.delta_time = 0.0F;
    const auto start_frame = [&]() -> void {
      WaitForQueueIdle();
      ctx_.frame_sequence = frame::SequenceNumber {
        ++sequence_,
      };
      ctx_.frame_slot = frame::Slot {
        static_cast<unsigned>(sequence_ % 3U),
      };
      service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
      ctx_.current_view.frame_exposure = SubmitCommands("Vortex Exposure Frame",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return service.PrepareFrameExposure(ctx_, recorder, true);
        });
      ASSERT_NE(ctx_.current_view.frame_exposure, nullptr);
    };
    auto inputs = PostProcessService::Inputs {};
    inputs.scene_signal = signal.texture.get();
    inputs.scene_signal_srv = signal.srv;
    inputs.require_scene_range = true;
    ASSERT_NO_FATAL_FAILURE(start_frame());
    const auto initial = SubmitCommands(
      "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, inputs);
      });
    ASSERT_TRUE(initial.has_value());
    const auto retained = ReadState(initial->exposure);
    EXPECT_NEAR(retained.displayed_scale, .72F, 2e-5F);

    const auto seed
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, 3.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_NO_FATAL_FAILURE(start_frame());
    auto& backend = FailureBackend();
    backend.recorder_names.clear();
    backend.fail_next_exposure_recorder = true;
    EXPECT_FALSE(SubmitCommands(
      "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, inputs);
      }).has_value());
    EXPECT_EQ(
      InspectRequiredTransition(ctx_.current_view.view_state_handle).phase,
      ExposureTransitionPhase::kQueued);
    const auto retry = SubmitCommands(
      "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, inputs);
      });
    ASSERT_TRUE(retry.has_value());
    EXPECT_FALSE(retry->exposure.solve_failed);
    const auto solved = ReadState(retry->exposure);
    EXPECT_FLOAT_EQ(solved.displayed_scale, .125F);
    EXPECT_EQ(solved.applied_generation.at(0), seed->generation);
    EXPECT_EQ(std::count(backend.recorder_names.begin(),
                backend.recorder_names.end(), "Vortex Exposure"),
      2);
    EXPECT_EQ(
      std::count(backend.recorder_names.begin(), backend.recorder_names.end(),
        "Vortex Exposure Final Scene Range"),
      0);

    auto repeated_inputs = inputs;
    repeated_inputs.scene_signal = invalid.texture.get();
    repeated_inputs.scene_signal_srv = invalid.srv;
    backend.fail_recorder_name = "Vortex Exposure Final Scene Range";
    EXPECT_FALSE(SubmitCommands("Vortex Exposure Final Scene Range",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, repeated_inputs);
      }).has_value());
    backend.fail_recorder_name.clear();
    const auto reused = SubmitCommands(
      "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, repeated_inputs);
      });
    ASSERT_TRUE(reused.has_value());
    EXPECT_FALSE(reused->exposure.executed);
    EXPECT_EQ(reused->exposure.state, retry->exposure.state);
    const auto reused_status = Read<ExposureCompletedStatus>(
      *reused->exposure.state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_NE(reused_status.flags & 16U, 0U);
    EXPECT_FLOAT_EQ(ReadState(reused->exposure).displayed_scale, .125F);

    ASSERT_NO_FATAL_FAILURE(start_frame());
    EXPECT_EQ(InspectRequiredTransition(ctx_.current_view.view_state_handle)
                .applied_generation,
      seed->generation);
    inputs.scene_signal = invalid.texture.get();
    inputs.scene_signal_srv = invalid.srv;
    const auto rejected_meter = SubmitCommands(
      "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
        return service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, recorder, inputs);
      });
    ASSERT_TRUE(rejected_meter.has_value());
    const auto protected_state = ReadState(rejected_meter->exposure);
    EXPECT_FLOAT_EQ(protected_state.displayed_scale, .125F);
    EXPECT_EQ(protected_state.flags & 12U, 0U);
    const auto status = Read<ExposureCompletedStatus>(
      *rejected_meter->exposure.state->status_buffer,
      ResourceStates::kCopySource);
    EXPECT_NE(status.flags & 16U, 0U);
    const auto still_retained = ReadState(initial->exposure);
    EXPECT_EQ(still_retained.displayed_scale, retained.displayed_scale);
    EXPECT_EQ(still_retained.applied_generation, retained.applied_generation);
    ctx_.current_view.frame_exposure.reset();
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, UnavailableRecordingDoesNotFabricateCurrentViewResult)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  ctx_.current_view.view_id = ViewId {
    92U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    92U,
  };
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5F);
  ctx_.current_view.view_id = ViewId {
    1U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    1U,
  };
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5F);
  ASSERT_TRUE(service.GetLastExecutionState().wrote_visible_output);
  ctx_.current_view.view_id = ViewId {
    92U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    92U,
  };
  ASSERT_NE(SubmitCommands("Vortex Exposure Frame",
              [&](graphics::CommandRecorder& recorder) -> auto {
                return service.PrepareFrameExposure(ctx_, recorder, true);
              }),
    nullptr);
  auto& backend = FailureBackend();
  backend.fail_next_exposure_recorder = true;
  auto prepared_inputs = PostProcessService::Inputs {};
  prepared_inputs.scene_signal = signal.texture.get();
  prepared_inputs.scene_signal_srv = signal.srv;
  const auto prepared = SubmitCommands(
    "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
      return service.PrepareSceneExposure(
        ctx_.current_view.view_id, ctx_, recorder, prepared_inputs);
    });
  EXPECT_FALSE(prepared.has_value());
  // The owner failed before this view could record. The last execution still
  // describes the successful sibling and cannot stand in for this result.
  EXPECT_NE(service.GetLastExecutionState().view_id, ctx_.current_view.view_id);
}

} // namespace oxygen::vortex::testing::exposure
