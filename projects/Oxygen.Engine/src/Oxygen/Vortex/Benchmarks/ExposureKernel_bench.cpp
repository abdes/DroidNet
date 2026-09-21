//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <print>
#include <stdlib.h>
#include <utility>
#include <vector>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/TimestampQueryProvider.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereMultiScatteringLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/Types/PostProcessConfig.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>

namespace oxygen::vortex::testing::exposure {

using graphics::BufferUsage;
using graphics::ResourceStates;

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
  const auto measure
    = [&](const char* name, unsigned width, unsigned height, unsigned iteration,
        const std::function<void()>& work) -> void {
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
    ASSERT_GE(ticks.subspan(1, 1).front(), ticks.front());
    std::println(
      "producer_native case={} width={} height={} iteration={} queue_ms={:.6f}",
      name, width, height, iteration,
      static_cast<double>(ticks.subspan(1, 1).front() - ticks.front()) * 1000.0
        / static_cast<double>(frequency));
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  const auto resolved = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const unsigned width : {
         1920U,
         3840U,
       }) {
    const auto height = width * 9U / 16U;
    const auto signal = Uniform(1.0F, width, height);
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = SubmitCommands("Vortex Exposure Frame",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->ResolveFrame(ctx_, recorder, resolved, frame_inputs);
      });
    ASSERT_NE(frame, nullptr);
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
      measure("final_range", width, height, iteration, [&] -> void {
        ASSERT_TRUE(SubmitCommands("Vortex Exposure Final Scene Range",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->CheckSceneColorRange(
              ctx_, recorder, frame, *signal.texture, signal.srv);
          }));
      });
    }
    const auto status = Read<ExposureCompletedStatus>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(status.flags & 16U, 0U);
  }
  ctx_.current_view.with_atmosphere = true;
  auto view = ViewConstants::GpuData {};
  auto constants = CreateUploadBuffer(
    SizeBytes {
      256U,
    },
    BufferUsage::kConstant);
  constants->Update(&view, sizeof(view), 0U);
  ctx_.view_constants = constants;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto multiple = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  stable.view_products.atmosphere.enabled = true;
  for (unsigned iteration = 0; iteration < 3; ++iteration) {
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    stable.atmosphere_revision = sequence_;
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    cache.RefreshForState(stable);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    multiple.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    measure("canonical_refresh", 256, 64, iteration, [&] -> void {
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
    && (width_text != nullptr)) {
    const auto owned
      = std::unique_ptr<char, decltype(&std::free)>(width_text, &std::free);
    width = static_cast<std::uint32_t>(std::strtoul(owned.get(), nullptr, 10));
  }
  ASSERT_GE(width, 64U);
  ASSERT_LE(width, 3840U);
  const std::uint32_t height = width * 9U / 16U;
  auto scene = scene::Scene("Admission timing", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& background
    = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
  background.SetEnabled(true);
  background.SetColorRgb({
    .25F,
    .25F,
    .25F,
  });
  ctx_.scene = observer_ptr {
    &scene,
  };
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
      .name = "opaque",
      .mapper = engine::ToneMapper::kAcesFitted,
      .gamma = 2.2F,
      .alpha = 1,
      .alpha_error = 0,
      .rgb_error = .0001F,
      .neutral = false,
      .views = 1,
    },
    Case {
      .name = "uncertain",
      .mapper = engine::ToneMapper::kAcesFitted,
      .gamma = 2.2F,
      .alpha = .7F,
      .alpha_error = .0001F,
      .rgb_error = .0001F,
      .neutral = false,
      .views = 1,
    },
    Case {
      .name = "filmic",
      .mapper = engine::ToneMapper::kFilmic,
      .gamma = 2.2F,
      .alpha = .7F,
      .alpha_error = .0001F,
      .rgb_error = .0001F,
      .neutral = false,
      .views = 1,
    },
    Case {
      .name = "gamma",
      .mapper = engine::ToneMapper::kAcesFitted,
      .gamma = .8F,
      .alpha = .7F,
      .alpha_error = .0001F,
      .rgb_error = .0001F,
      .neutral = false,
      .views = 1,
    },
    Case {
      .name = "wide",
      .mapper = engine::ToneMapper::kAcesFitted,
      .gamma = 2.2F,
      .alpha = .5F,
      .alpha_error = .001F,
      .rgb_error = 0,
      .neutral = true,
      .views = 1,
    },
    Case {
      .name = "main_pip",
      .mapper = engine::ToneMapper::kAcesFitted,
      .gamma = 2.2F,
      .alpha = .7F,
      .alpha_error = .0001F,
      .rgb_error = .0001F,
      .neutral = false,
      .views = 2,
    },
  };
  const auto anchor = Uniform(8192.0F);
  const auto capture = BeginOptionalCapture();
  unsigned case_index = 0U;
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    background.SetColorRgb(test.neutral ? Vec3 { 0.0F, } : Vec3 { .25F, });
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = 0.0F;
    auto post_process = PostProcessConfig {};
    post_process.exposure = settings;
    post_process.tone_mapper = test.mapper;
    post_process.enable_bloom = false;
    post_process.gamma = test.gamma;
    const auto resolved = ResolvedPostProcessConfig::Resolve(post_process);
    ASSERT_TRUE(resolved.has_value());
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
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
      const auto id = 100U + (case_index * 2U) + index;
      ctx_.current_view.view_id = ViewId {
        id,
      };
      ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
        id,
      };
      const std::array<Pixel, 1> pixel { test.neutral
          ? Pixel { .5F, .5F, .5F, test.alpha }
          : Pixel { .18F * test.alpha, .24F * test.alpha, .35F * test.alpha,
              test.alpha, }, };
      auto signal = MakeSignal(view_width, view_height, pixel);
      auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
      frame_inputs.use_fp32 = true;
      const auto frame = SubmitCommands("Vortex Exposure Frame",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return pass_->ResolveFrame(ctx_, recorder, *resolved, frame_inputs);
        });
      ASSERT_NE(frame, nullptr);
      ASSERT_TRUE(RecordShared(signal, *resolved).executed);
      auto error = HdrSceneErrorData {};
      error.candidate_rgb_relative = test.rgb_error;
      error.candidate_coverage_absolute = test.alpha_error;
      error.candidate_pre_exposure = 1.0F;
      error.checked_products = (1U << 10U) | 1U;
      error.flags = 3U;
      auto upload = CreateUploadBuffer(SizeBytes {
        sizeof(error),
      });
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
      views.push_back({
        .signal = std::move(signal),
        .frame = frame,
        .id = ctx_.current_view.view_id,
        .handle = ctx_.current_view.view_state_handle,
        .width = view_width,
        .height = view_height,
      });
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
        auto anchor_product = postprocess::ExposurePass::HdrProduct {};
        anchor_product.texture = anchor.texture.get();
        anchor_product.srv = anchor.srv;
        anchor_product.id = 1U;
        auto scene_product = postprocess::ExposurePass::HdrProduct {};
        scene_product.texture = view.signal.texture.get();
        scene_product.srv = view.signal.srv;
        scene_product.id = 11U;
        scene_product.coverage = true;
        scene_product.composed_error = true;
        const std::array products {
          anchor_product,
          scene_product,
        };
        ASSERT_TRUE(SubmitCommands("Vortex Exposure Suitability",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->EvaluateFp16Products(
              ctx_, recorder, view.frame, *resolved, products, {});
          }));
      }
      {
        auto recorder = AcquireRecorder("Admission native timing end");
        ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 1U));
        ASSERT_TRUE(timestamps->RecordResolve(*recorder, 2U));
      }
      WaitForQueueIdle();
      const auto ticks = timestamps->GetResolvedTicks();
      ASSERT_GE(ticks.size(), 2U);
      ASSERT_GE(ticks.subspan(1, 1).front(), ticks.front());
      std::array<std::uint32_t, 2> flags {};
      ASSERT_LE(views.size(), flags.size());
      for (std::size_t index = 0; index < views.size(); ++index) {
        const auto& view = views.at(index);
        const auto report = Read<HdrSuitabilityData>(
          *view.frame->suitability_buffer, ResourceStates::kShaderResource);
        EXPECT_EQ(report.checked_samples, (view.width * view.height) + 1U);
        EXPECT_EQ(report.failure_flags & ~4U, 0U);
        flags.at(index) = report.failure_flags;
      }
      // Queue timestamps include inter-dispatch barriers and any CPU submission
      // gaps. Replay event counters separately measure dispatch execution only.
      std::println(
        "admission_native case={} width={} height={} views={} iteration={} "
        "queue_ms={:.6f} first_flags={} second_flags={}",
        test.name, width, height, test.views, iteration,
        static_cast<double>(ticks.subspan(1, 1).front() - ticks.front())
          * 1000.0 / static_cast<double>(frequency),
        flags.at(0), flags.at(1));
    }
    ++case_index;
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  ctx_.scene.reset();
}

} // namespace oxygen::vortex::testing::exposure
