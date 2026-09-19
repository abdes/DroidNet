//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <limits>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/TimestampQueryProvider.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereMultiScatteringLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

NOLINT_TEST_F(ExposureGpuTest, DISABLED_ProducerRangeAndCanonicalTiming)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  auto timestamps = Backend().GetTimestampQueryProvider();
  ASSERT_NE(timestamps, nullptr);
  ASSERT_TRUE(timestamps->EnsureCapacity(2U));
  std::uint64_t frequency = 0U;
  ASSERT_TRUE(GetQueue()->TryGetTimestampFrequency(frequency));
  ASSERT_GT(frequency, 0U);
  const auto measure = [&](const char* name, unsigned width, unsigned height,
                         unsigned iteration,
                         const std::function<void()>& work) {
    WaitForQueueIdle();
    {
      auto recorder = AcquireRecorder("Producer native timing begin");
      ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 0U));
    }
    work();
    {
      auto recorder = AcquireRecorder("Producer native timing end");
      ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 1U));
      ASSERT_TRUE(timestamps->RecordResolve(*recorder, 2U));
    }
    WaitForQueueIdle();
    const auto ticks = timestamps->GetResolvedTicks();
    ASSERT_GE(ticks.size(), 2U);
    ASSERT_GE(ticks[1], ticks[0]);
    std::printf(
      "producer_native case=%s width=%u height=%u iteration=%u queue_ms=%.6f\n",
      name, width, height, iteration,
      double(ticks[1] - ticks[0]) * 1000.0 / double(frequency));
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  const auto resolved = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const unsigned width : { 1920U, 3840U }) {
    const auto height = width * 9U / 16U;
    const auto signal = Uniform(1.0F, width, height);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame
      = pass_->ResolveFrame(ctx_, resolved, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
      measure("final_range", width, height, iteration, [&] {
        ASSERT_TRUE(pass_->CheckSceneColorRange(
          ctx_, frame, *signal.texture, signal.srv));
      });
    }
    const auto status = Read<ExposureCompletedStatus>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(status.flags & 16U, 0U);
  }
  ctx_.current_view.with_atmosphere = true;
  auto view = ViewConstants::GpuData {};
  auto constants
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  constants->Update(&view, sizeof(view), 0U);
  ctx_.view_constants = constants;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto multiple = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  stable.view_products.atmosphere.enabled = true;
  for (unsigned iteration = 0; iteration < 3; ++iteration) {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    stable.atmosphere_revision = sequence_;
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    cache.RefreshForState(stable);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    multiple.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    measure("canonical_refresh", 256, 64, iteration, [&] {
      ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
      ASSERT_TRUE(multiple.Record(ctx_, stable, cache).executed);
    });
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  ctx_.view_constants.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, DISABLED_ComposedAdmissionFullResolutionTiming)
{
  std::uint32_t width = 960U;
  char* width_text = nullptr;
  std::size_t width_size = 0U;
  if (_dupenv_s(&width_text, &width_size, "OXYGEN_EXPOSURE_TIMING_WIDTH") == 0
    && width_text) {
    width = static_cast<std::uint32_t>(std::strtoul(width_text, nullptr, 10));
    std::free(width_text);
  }
  ASSERT_GE(width, 64U);
  ASSERT_LE(width, 3840U);
  const std::uint32_t height = width * 9U / 16U;
  auto scene = scene::Scene("Admission timing", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& background
    = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
  background.SetEnabled(true);
  background.SetColorRgb({ .25F, .25F, .25F });
  ctx_.scene = observer_ptr { &scene };
  auto timestamps = Backend().GetTimestampQueryProvider();
  ASSERT_NE(timestamps, nullptr);
  ASSERT_TRUE(timestamps->EnsureCapacity(2U));
  std::uint64_t frequency = 0U;
  ASSERT_TRUE(GetQueue()->TryGetTimestampFrequency(frequency));
  ASSERT_GT(frequency, 0U);
  struct Case {
    const char* name;
    engine::ToneMapper mapper;
    float gamma;
    float alpha;
    float alpha_error;
    float rgb_error;
    bool neutral;
    unsigned views;
  };
  const std::array cases {
    Case {
      "opaque", engine::ToneMapper::kAcesFitted, 2.2F, 1, 0, .0001F, false, 1 },
    Case { "uncertain", engine::ToneMapper::kAcesFitted, 2.2F, .7F, .0001F,
      .0001F, false, 1 },
    Case { "filmic", engine::ToneMapper::kFilmic, 2.2F, .7F, .0001F, .0001F,
      false, 1 },
    Case { "gamma", engine::ToneMapper::kAcesFitted, .8F, .7F, .0001F, .0001F,
      false, 1 },
    Case {
      "wide", engine::ToneMapper::kAcesFitted, 2.2F, .5F, .001F, 0, true, 1 },
    Case { "main_pip", engine::ToneMapper::kAcesFitted, 2.2F, .7F, .0001F,
      .0001F, false, 2 },
  };
  const auto anchor = Uniform(8192.0F);
  const auto capture = BeginOptionalCapture();
  unsigned case_index = 0U;
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    background.SetColorRgb(test.neutral ? Vec3 { 0.0F } : Vec3 { .25F });
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
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    struct View {
      Signal signal;
      postprocess::ExposurePass::FrameLease frame;
      ViewId id;
      CompositionView::ViewStateHandle handle;
      std::uint32_t width;
      std::uint32_t height;
    };
    std::vector<View> views;
    for (unsigned index = 0U; index < test.views; ++index) {
      const auto view_width = index == 0U ? width : width / 2U;
      const auto view_height = index == 0U ? height : height / 2U;
      const auto id = 100U + case_index * 2U + index;
      ctx_.current_view.view_id = ViewId { id };
      ctx_.current_view.view_state_handle
        = CompositionView::ViewStateHandle { id };
      const std::array<Pixel, 1> pixel { test.neutral
          ? Pixel { .5F, .5F, .5F, test.alpha }
          : Pixel { .18F * test.alpha, .24F * test.alpha, .35F * test.alpha,
              test.alpha } };
      auto signal = MakeSignal(view_width, view_height, pixel);
      const auto frame
        = pass_->ResolveFrame(ctx_, *resolved, { .use_fp32 = true });
      ASSERT_NE(frame, nullptr);
      ASSERT_TRUE(RecordShared(signal, *resolved).executed);
      const auto error
        = HdrSceneErrorData { .candidate_rgb_relative = test.rgb_error,
            .candidate_coverage_absolute = test.alpha_error,
            .candidate_pre_exposure = 1.0F,
            .checked_products = (1U << 10U) | 1U,
            .flags = 3U };
      auto upload = CreateUploadBuffer(SizeBytes { sizeof(error) });
      upload->Update(&error, sizeof(error), 0U);
      {
        auto recorder = AcquireRecorder("Admission timing certificate");
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
          *frame->current_state->status_buffer,
          ResourceStates::kShaderResource);
      }
      views.push_back({ std::move(signal), frame, ctx_.current_view.view_id,
        ctx_.current_view.view_state_handle, view_width, view_height });
    }
    for (unsigned iteration = 0U; iteration < 3U; ++iteration) {
      WaitForQueueIdle();
      {
        auto recorder = AcquireRecorder("Admission native timing begin");
        ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 0U));
      }
      for (const auto& view : views) {
        ctx_.current_view.view_id = view.id;
        ctx_.current_view.view_state_handle = view.handle;
        const std::array products {
          postprocess::ExposurePass::HdrProduct {
            .texture = anchor.texture.get(), .srv = anchor.srv, .id = 1U },
          postprocess::ExposurePass::HdrProduct {
            .texture = view.signal.texture.get(),
            .srv = view.signal.srv,
            .id = 11U,
            .coverage = true,
            .composed_error = true },
        };
        ASSERT_TRUE(pass_->EvaluateFp16Products(
          ctx_, view.frame, *resolved, products, {}));
      }
      {
        auto recorder = AcquireRecorder("Admission native timing end");
        ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 1U));
        ASSERT_TRUE(timestamps->RecordResolve(*recorder, 2U));
      }
      WaitForQueueIdle();
      const auto ticks = timestamps->GetResolvedTicks();
      ASSERT_GE(ticks.size(), 2U);
      ASSERT_GE(ticks[1], ticks[0]);
      std::array<std::uint32_t, 2> flags {};
      for (std::size_t index = 0; index < views.size(); ++index) {
        const auto& view = views[index];
        const auto report = Read<HdrSuitabilityData>(
          *view.frame->suitability_buffer, ResourceStates::kShaderResource);
        EXPECT_EQ(report.checked_samples, view.width * view.height + 1U);
        EXPECT_EQ(report.failure_flags & ~4U, 0U);
        flags[index] = report.failure_flags;
      }
      // Queue timestamps include inter-dispatch barriers and any CPU submission
      // gaps. Replay event counters separately measure dispatch execution only.
      std::printf("admission_native case=%s width=%u height=%u views=%u "
                  "iteration=%u queue_ms=%.6f first_flags=%u second_flags=%u\n",
        test.name, width, height, test.views, iteration,
        double(ticks[1] - ticks[0]) * 1000.0 / double(frequency), flags[0],
        flags[1]);
    }
    ++case_index;
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  ctx_.scene.reset();
}

} // namespace oxygen::vortex::testing::exposure
