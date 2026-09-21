//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Types/CompositingTask.h>

namespace oxygen::vortex::testing::exposure {

using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::Texture;

NOLINT_TEST_F(ExposureGpuTest, CompositionConstantsSurviveLaterSubmission)
{
  std::ignore = OwnedExposureService();
  const std::array sources {
    Uniform(.25F),
    Uniform(.5F),
    Uniform(.75F),
  };
  auto output = CreateRegisteredTexture({
    .width = 1,
    .height = 1,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  engine::FrameContext frame;
  frame.SetFrameSlot(
    frame::Slot {
      0,
    },
    engine::internal::EngineTagFactory::Get());
  frame.SetFrameSequenceNumber(
    frame::SequenceNumber {
      1,
    },
    engine::internal::EngineTagFactory::Get());
  Backend().BeginFrame(
    frame::SequenceNumber {
      1,
    },
    frame::Slot {
      0,
    });
  renderer_->OnFrameStart(observer_ptr {
    &frame,
  });
  const ViewPort viewport {
    .width = 1,
    .height = 1,
  };
  auto first = CompositionSubmission {};
  first.composite_target = target;
  for (unsigned i = 0; i < 2; ++i) {
    first.tasks.push_back(CompositingTask::MakeTextureBlend(
      std::const_pointer_cast<Texture>(sources.at(i).texture), viewport, 1));
  }
  renderer_->RegisterComposition(std::move(first), {});
  auto last = CompositionSubmission {};
  last.composite_target = target;
  last.tasks.push_back(CompositingTask::MakeTextureBlend(
    std::const_pointer_cast<Texture>(sources.at(2).texture), viewport, 1));
  renderer_->RegisterComposition(std::move(last), {});
  const auto capture = BeginOptionalCapture();
  auto loop = co::testing::TestEventLoop {};
  // Run waits for completion, so the closure and captured locals outlive the
  // coroutine.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
  co::Run(loop, [&] -> co::Co<void> {
    co_await renderer_->OnCompositing(observer_ptr {
      &frame,
    });
  });
  renderer_->OnFrameEnd(observer_ptr {
    &frame,
  });
  Backend().EndFrame(
    frame::SequenceNumber {
      1,
    },
    frame::Slot {
      0,
    });
  WaitForQueueIdle();
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  const auto pixel = ReadFloatTexture(*output);
  ASSERT_EQ(pixel.size(), 1U);
  EXPECT_EQ(pixel.at(0),
    (Pixel {
      .75F,
      .75F,
      .75F,
      1,
    }));
}

NOLINT_TEST_F(ExposureGpuTest, NativeExposureTimelineRecordsMeteringScopes)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kDiagnosticsAndProfiling);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  auto& diagnostics = renderer_->GetDiagnosticsService();
  diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
  diagnostics.SetGpuTimelineEnabled(true);
  const auto path = std::filesystem::path { OXYGEN_EXPOSURE_WORKSPACE, }
    / "out/build-ninja/analysis/vortex/exposure-lightbench/slice51"
#ifdef NDEBUG
    / "native-timeline-smoke-Release.json";
#else
    / "native-timeline-smoke-Debug.json";
#endif
  const auto signal = Uniform(.25F, 4U, 4U);
  auto frame_context = engine::FrameContext {};
  for (unsigned sequence = 1U; sequence <= 3U; ++sequence) {
    const auto slot = frame::Slot {
      sequence - 1U,
    };
    const auto frame_sequence = frame::SequenceNumber {
      sequence,
    };
    frame_context.SetFrameSequenceNumber(
      frame_sequence, engine::internal::EngineTagFactory::Get());
    frame_context.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    Backend().BeginFrame(frame_sequence, slot);
    renderer_->OnFrameStart(observer_ptr {
      &frame_context,
    });
    if (sequence == 1U) {
      ASSERT_TRUE(diagnostics.RequestGpuTimelineRecording(path, 2U));
    }
    if (sequence <= 2U) {
      ctx_.frame_slot = slot;
      pass_->OnFrameStart(frame_sequence, slot);
      const auto snapshot = Run(signal);
      EXPECT_NEAR(snapshot.state.displayed_scale, .72F, 2e-4F);
    }
    auto loop = co::testing::TestEventLoop {};
    // Run waits for completion, so the closure and captured locals outlive the
    // coroutine.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(loop, [&] -> co::Co<void> {
      co_await renderer_->OnCompositing(observer_ptr {
        &frame_context,
      });
    });
    renderer_->OnFrameEnd(observer_ptr {
      &frame_context,
    });
    Backend().EndFrame(frame_sequence, slot);
    WaitForQueueIdle();
  }
  auto stream = std::ifstream(path);
  const auto report = nlohmann::json::parse(stream);
  EXPECT_EQ(report.at("complete"), true);
  EXPECT_EQ(report.at("timing_valid"), true);
  ASSERT_EQ(report.at("frames").size(), 2U);
  for (const auto& frame : report.at("frames")) {
    auto names = std::unordered_set<std::string> {};
    for (const auto& scope : frame.at("scopes")) {
      EXPECT_EQ(scope.at("valid"), true);
      EXPECT_GT(scope.at("duration_ms").get<double>(), 0.0);
      names.insert(scope.at("name").get<std::string>());
    }
    EXPECT_TRUE(names.contains("Vortex.Frame"));
    EXPECT_TRUE(names.contains("Vortex.PostProcess.Exposure.MeterAndAdapt"));
    EXPECT_TRUE(names.contains("Vortex.PostProcess.Exposure.Histogram"));
    EXPECT_TRUE(names.contains("Vortex.PostProcess.Exposure.Solve"));
  }
  RecordProperty("native_timeline_report", path.string());
}

} // namespace oxygen::vortex::testing::exposure
