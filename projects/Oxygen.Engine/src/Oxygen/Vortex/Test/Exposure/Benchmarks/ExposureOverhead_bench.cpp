//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifdef NDEBUG
#  include <array>
#  include <chrono>
#  include <filesystem>
#  include <fstream>
#  include <memory>
#  include <ratio>
#  include <string>
#  include <tuple>
#  include <vector>

#  include <nlohmann/json.hpp>
#  include <nlohmann/json_fwd.hpp>

#  include <Oxygen/Base/Logging.h>
#  include <Oxygen/Base/ObserverPtr.h>
#  include <Oxygen/Console/Command.h>
#  include <Oxygen/Core/Types/Format.h>
#  include <Oxygen/Core/Types/View.h>
#  include <Oxygen/Data/MaterialDomain.h>
#  include <Oxygen/OxCo/Co.h>
#  include <Oxygen/OxCo/Run.h>
#  include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#  include <Oxygen/Scene/Camera/Perspective.h>
#  include <Oxygen/Scene/Environment/Fog.h>
#  include <Oxygen/Scene/Environment/SceneEnvironment.h>
#  include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#  include <Oxygen/Vortex/CompositionView.h>
#  include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#  include <Oxygen/Vortex/RenderContext.h>
#  include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#  include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#endif

#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Exposure/Benchmarks/ExposureBenchmarkFixture.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;

NOLINT_TEST_F(ExposureProfilingOverheadTest, DISABLED_ReleaseCollectionOnOff)
{
#ifndef NDEBUG
  FAIL() << "This performance measurement requires Release.";
#else
  // Reuse C01: the existing two-view accounting recipe. This run removes its
  // correctness readbacks and explicit queue drains from the timed loop.
  constexpr unsigned width = 1920U;
  constexpr unsigned height = 1080U;
  view.viewport = {
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
  };
  const auto camera_lens = camera.GetCameraAs<scene::PerspectiveCamera>();
  if (!camera_lens.has_value()) {
    FAIL() << "Expected a perspective camera";
  }
  camera_lens->get().SetViewport(view.viewport);
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
  sky.SetEnabled(true);
  sky.SetRayleighScatteringRgb({
    0,
    0,
    0,
  });
  sky.SetMieScatteringRgb({
    0,
    0,
    0,
  });
  sky.SetMieAbsorptionRgb({
    0,
    0,
    0,
  });
  sky.SetOzoneAbsorptionRgb({
    0,
    0,
    0,
  });
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(0);
  ASSERT_EQ(
    fixture_console.Execute("vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(fixture_console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  probe->prepare = [](RenderContext& context) -> void {
    context.current_view.with_atmosphere = true;
    context.current_view.with_height_fog = true;
  };
  scene->Update();
  scene->SyncObservers();
  std::array<std::shared_ptr<Framebuffer>, 2> targets;
  for (unsigned index = 0U; index < targets.size(); ++index) {
    auto output = CreateRegisteredTexture({
      .width = width >> index,
      .height = height >> index,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    targets.at(index) = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
  }
  auto timing = frame.GetModuleTimingData();
  timing.game_delta_time = time::CanonicalDuration {
    std::chrono::nanoseconds {
      16'666'667,
    },
  };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  auto& diagnostics = renderer_->GetDiagnosticsService();
  diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
  const auto directory = std::filesystem::path {
    OXYGEN_EXPOSURE_WORKSPACE,
  } / "out/build-ninja/analysis/vortex/exposure-lightbench/slice51";
  std::filesystem::create_directories(directory);
  const auto recording = directory / "overhead-on-Release.gpu.json";
  using Clock = std::chrono::steady_clock;
  struct Sample {
    unsigned frame_sequence;
    double wall_ms;
    double frame_start_ms;
    double submission_ms;
  };
  const auto milliseconds = [](const auto duration) -> double {
    return std::chrono::duration<double, std::milli>(duration).count();
  };
  constexpr auto sample_count = 3600U;
  auto require_fp16 = false;
  auto seen_views = std::array<bool, 2> {};
  probe->inspect
    = [&](const RenderContext& context, const SceneTextureExtractRef& color,
        const unsigned draws) -> void {
    const auto index = context.current_view.view_state_handle.get() - 500U;
    CHECK_F(index < seen_views.size());
    if (require_fp16) {
      CHECK_F(color.valid && color.texture != nullptr && draws == 1U);
      CHECK_F(color.texture->GetDescriptor().format == Format::kRGBA16Float);
    }
    seen_views.at(index) = true;
  };
  const auto render = [&](const bool start_recording) -> Sample {
    const auto started = Clock::now();
    seen_views.fill(false);
    const auto slot = frame::Slot {
      sequence % 3U,
    };
    const auto frame_sequence = frame::SequenceNumber {
      ++sequence,
    };
    Backend().BeginFrame(frame_sequence, slot);
    const auto after_frame_start = Clock::now();
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(
      frame_sequence, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr {
      &frame,
    });
    if (start_recording) {
      CHECK_F(diagnostics.RequestGpuTimelineRecording(recording, sample_count));
    }
    for (unsigned index = 0U; index < targets.size(); ++index) {
      auto sized_view = view;
      sized_view.viewport.width = static_cast<float>(width >> index);
      sized_view.viewport.height = static_cast<float>(height >> index);
      auto input = CompositionView::ForScene(
        ViewId {
          500U + index,
        },
        sized_view, camera);
      input.view_state_handle = CompositionView::ViewStateHandle {
        500U + index,
      };
      input.render_settings.exposure = settings;
      CHECK_F(
        renderer_->PublishRuntimeCompositionView(frame,
          { .composition_view = input,
            .render_target = observer_ptr { targets.at(index).get(), },
            .composite_source = observer_ptr { targets.at(index).get(), }, })
        != kInvalidViewId);
    }
    auto loop = co::testing::TestEventLoop {};
    // Run completes synchronously before the closure or captures are destroyed.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr {
        &frame,
      });
      co_await renderer_->OnRender(observer_ptr {
        &frame,
      });
      co_await renderer_->OnCompositing(observer_ptr {
        &frame,
      });
    });
    if (require_fp16) {
      CHECK_F(seen_views.at(0) && seen_views.at(1));
    }
    renderer_->OnFrameEnd(observer_ptr {
      &frame,
    });
    Backend().EndFrame(frame_sequence, slot);
    const auto ended = Clock::now();
    return {
      .frame_sequence = sequence,
      .wall_ms = milliseconds(ended - started),
      .frame_start_ms = milliseconds(after_frame_start - started),
      .submission_ms = milliseconds(ended - after_frame_start),
    };
  };
  for (const bool enabled : {
         false,
         true,
       }) {
    diagnostics.SetGpuTimelineEnabled(enabled);
    require_fp16 = false;
    const auto warm_start = Clock::now();
    unsigned warm_frames = 0U;
    while (warm_frames < 300U
      || Clock::now() - warm_start < std::chrono::seconds {
           10,
         }) {
      std::ignore = render(false);
      ++warm_frames;
    }
    std::vector<Sample> samples;
    samples.reserve(sample_count);
    require_fp16 = true;
    const auto sample_start = Clock::now();
    auto previous_end = sample_start;
    for (unsigned index = 0U; index < sample_count; ++index) {
      auto sample = render(enabled && index == 0U);
      const auto ended = Clock::now();
      sample.wall_ms = milliseconds(ended - previous_end);
      previous_end = ended;
      samples.push_back(sample);
    }
    const auto elapsed
      = std::chrono::duration<double>(Clock::now() - sample_start).count();
    // One ordinary frame publishes the final GPU capture. Its time is outside
    // the declared steady-state population, and no explicit GPU wait is added.
    const auto* const label = enabled ? "on" : "off";
    const auto finish_sample = render(false);
    RecordProperty(
      std::string {
        label,
      } + "_finish_wall_ms",
      std::to_string(finish_sample.wall_ms));
    RecordProperty(
      std::string {
        label,
      } + "_finish_submission_ms",
      std::to_string(finish_sample.submission_ms));
    const auto path = directory
      / (std::string {
           "overhead-",
         }
        + label + "-Release.csv");
    auto stream = std::ofstream(path);
    ASSERT_TRUE(stream.is_open());
    stream << "frame_seq,wall_ms,frame_start_ms,submission_ms\n";
    for (const auto& sample : samples) {
      stream << sample.frame_sequence << ',' << sample.wall_ms << ','
             << sample.frame_start_ms << ',' << sample.submission_ms << '\n';
    }
    stream.close();
    ASSERT_TRUE(stream.good());
    RecordProperty(
      std::string {
        label,
      } + "_sample_count",
      sample_count);
    RecordProperty(
      std::string {
        label,
      } + "_elapsed_seconds",
      std::to_string(elapsed));
    RecordProperty(
      std::string {
        label,
      } + "_raw_samples",
      path.string());
    EXPECT_GE(elapsed, 30.0);
  }
  auto stream = std::ifstream(recording);
  ASSERT_TRUE(stream.is_open());
  const auto report = nlohmann::json::parse(stream);
  EXPECT_EQ(report.at("complete"), true);
  EXPECT_EQ(report.at("timing_valid"), true);
  const auto adapter = Backend().GetCurrentDevice()->GetAdapterLuid();
  RecordProperty("adapter_luid_low", std::to_string(adapter.LowPart));
  RecordProperty("adapter_luid_high", std::to_string(adapter.HighPart));
  RecordProperty("workload",
    "C01: 1920x1080 + 960x540, Manual EV0, emissive triangle, "
    "vacuum atmosphere, zero-extinction fog, temporal off, fixed "
    "dt 1/60, native offscreen");
  RecordProperty("scope",
    "Collection/export on versus runtime off. Scope wrappers exist in both. "
    "Wall interval includes ordinary frame-start queue waits; submission span "
    "includes driver calls and recording enqueue work. The separate "
    "finalization "
    "frame includes writer drain. Not a presented FPS or exposure CPU-budget "
    "acceptance test.");
#endif
}

} // namespace oxygen::vortex::testing::exposure
