//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>

#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(
  ExposureGpuTest, Fp32SceneColorPreservesWideRangeRadianceAndCoverage)
{
  auto textures = SceneTextures(Backend(),
    { .extent = { 1U, 1U }, .scene_color_format = Format::kRGBA32Float });
  auto color = textures.GetSceneColorResource();
  ASSERT_EQ(color->GetDescriptor().format, Format::kRGBA32Float);
  const Pixel expected { 0x1p30F, 0x1p-24F, 1.0F, .25F };
  auto upload = CreateUploadBuffer(SizeBytes { 256U });
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
        .dst_slice = { .width = 1U, .height = 1U, .depth = 1U } },
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
  ASSERT_TRUE(mapped.has_value());
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
  for (const bool persistent : { true, false }) {
    SCOPED_TRACE(persistent);
    auto service = PostProcessService(*renderer_);
    ctx_.current_view.view_state_handle = persistent
      ? CompositionView::ViewStateHandle { 91U }
      : CompositionView::kInvalidViewStateHandle;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
    auto config = PostProcessConfig { .exposure = settings };
    config.tone_mapper = engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    config.enable_bloom = false;
    config.bloom_intensity = 0.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto accumulation = Uniform(.25F, 4U, 4U);
    const auto resolved = Uniform(.5F, 4U, 4U);
    auto& recorder_names
      = static_cast<ExposureFailureGraphics&>(Backend()).recorder_names;
    recorder_names.clear();
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
        { .scene_signal = accumulation.texture.get(),
          .scene_signal_srv = accumulation.srv });
    ASSERT_TRUE(prepared.has_value());
    ASSERT_NE(prepared->exposure.state, nullptr);
    const auto before = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_NEAR(before.displayed_scale, .72F, 2e-5F);
    EXPECT_NEAR(before.raw_metered_luminance, .25F, 2e-5F);
    // A different signal and mapper at Stage 22 must neither remeter nor
    // replace the configuration pinned when the accumulation was solved.
    EXPECT_NEAR(
      ServicePixel(service, resolved, settings,
        ServicePixelOptions { .tone_mapper = engine::ToneMapper::kAcesFitted,
          .start_new_frame = false,
          .prepared = &*prepared }),
      .36F, 2e-5F);
    const auto after = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&before, &after, sizeof(before)), 0);
    EXPECT_EQ(std::count(recorder_names.begin(), recorder_names.end(),
                "Vortex Exposure"),
      1);
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedPreparationCannotReuseAnotherViewsSuccessfulOutputStatus)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  ctx_.current_view.view_id = ViewId { 92U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 92U };
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5F);
  ctx_.current_view.view_id = ViewId { 1U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 1U };
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5F);
  ASSERT_TRUE(service.GetLastExecutionState().wrote_visible_output);
  ctx_.current_view.view_id = ViewId { 92U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 92U };
  ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_next_exposure_recorder = true;
  backend.fail_next_fallback_recorder = true;
  const auto prepared
    = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
  EXPECT_FALSE(prepared.has_value());
  EXPECT_FALSE(service.GetLastExecutionState().wrote_visible_output);
  EXPECT_EQ(service.GetLastExecutionState().view_id, ctx_.current_view.view_id);
}

} // namespace oxygen::vortex::testing::exposure
