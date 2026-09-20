//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::Graphics;
using oxygen::observer_ptr;
using oxygen::graphics::QueueRole;
using oxygen::vortex::internal::GpuTimelineDiagnostic;
using oxygen::vortex::internal::GpuTimelineFrame;
using oxygen::vortex::internal::GpuTimelineProfiler;
using oxygen::vortex::internal::GpuTimelineSink;
using oxygen::vortex::testing::FakeGraphics;

class CapturingSink final : public GpuTimelineSink {
public:
  auto ConsumeFrame(const GpuTimelineFrame& frame) -> bool override
  {
    frames.push_back(frame);
    return true;
  }

  std::vector<GpuTimelineFrame> frames;
};

auto MakeGraphics() -> std::unique_ptr<FakeGraphics>
{
  auto graphics = std::make_unique<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
  return graphics;
}

auto AcquireTelemetryRecorder(FakeGraphics& graphics, std::string_view name)
{
  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), name);
  return recorder;
}

auto WaitForFile(const std::filesystem::path& path) -> bool
{
  for (int i = 0; i < 250; ++i) {
    std::error_code error {};
    if (std::filesystem::exists(path, error) && !error
      && std::filesystem::is_regular_file(path, error) && !error
      && std::filesystem::file_size(path, error) > 0U) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

TEST(GpuTimelineProfilerTest, DisabledFastPathProducesNoWritesOrResolve)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });

  profiler.SetEnabled(false);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    1U,
  });

  auto recorder = AcquireTelemetryRecorder(*graphics, "DisabledFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });

  {
    oxygen::graphics::GpuEventScope scope(*recorder, "DisabledScope",
      oxygen::profiling::ProfileGranularity::kTelemetry);
  }

  profiler.OnFrameRecordTailResolve();

  EXPECT_EQ(graphics->GetTimestampQueryProvider().WriteCount(), 0U);
  EXPECT_EQ(graphics->GetTimestampQueryProvider().ResolveCount(), 0U);
}

TEST(GpuTimelineProfilerTest, PublishesNestedTimelineOnNextFrame)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(8U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    1U,
  });

  auto recorder = AcquireTelemetryRecorder(*graphics, "FrameOne");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });

  {
    oxygen::graphics::GpuEventScope outer(
      *recorder, "Outer", oxygen::profiling::ProfileGranularity::kTelemetry);
    {
      oxygen::graphics::GpuEventScope inner(
        *recorder, "Inner", oxygen::profiling::ProfileGranularity::kTelemetry);
    }
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    2U,
  });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.frame_sequence, 1U);
  ASSERT_EQ(frame.scopes.size(), 2U);
  EXPECT_EQ(frame.used_query_slots, 4U);
  EXPECT_TRUE(frame.scopes.at(0).valid);
  EXPECT_TRUE(frame.scopes.at(1).valid);
  EXPECT_EQ(frame.scopes.at(1).parent_scope_id, 0U);
  ASSERT_EQ(frame.scopes.at(0).child_scope_ids.size(), 1U);
  EXPECT_EQ(frame.scopes.at(0).child_scope_ids.front(), 1U);
  EXPECT_GT(frame.scopes.at(0).duration_ms, 0.0F);
  EXPECT_GT(frame.scopes.at(1).duration_ms, 0.0F);
  EXPECT_EQ(graphics->GetTimestampQueryProvider().ResolveCount(), 1U);
  EXPECT_EQ(graphics->GetTimestampQueryProvider().LastResolvedQueryCount(), 4U);
}

TEST(GpuTimelineProfilerTest, FrameSpanIncludesSeparatePassRecorders)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(
    observer_ptr<Graphics> {
      graphics.get(),
    },
    true);
  auto sink = std::make_shared<CapturingSink>();
  profiler.SetEnabled(true);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    1U,
  });
  {
    auto recorder = AcquireTelemetryRecorder(*graphics, "IndependentPass");
    recorder->SetTelemetryCollector(
      observer_ptr<oxygen::graphics::IGpuProfileCollector> {
        &profiler,
      });
    oxygen::graphics::GpuEventScope scope(
      *recorder, "Pass", oxygen::profiling::ProfileGranularity::kTelemetry);
  }
  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    2U,
  });
  ASSERT_EQ(sink->frames.size(), 1U);
  ASSERT_EQ(sink->frames.at(0).scopes.size(), 2U);
  const auto& frame = sink->frames.at(0).scopes.at(0);
  const auto& pass = sink->frames.at(0).scopes.at(1);
  EXPECT_TRUE(frame.valid);
  EXPECT_TRUE(pass.valid);
  EXPECT_EQ(frame.display_name, "Vortex.Frame");
  EXPECT_EQ(pass.parent_scope_id, frame.scope_id);
  EXPECT_NEAR(frame.duration_ms, .3F, 1e-6F);
  EXPECT_NEAR(pass.duration_ms, .1F, 1e-6F);
  EXPECT_GE(pass.start_ms, frame.start_ms);
  EXPECT_LE(pass.end_ms, frame.end_ms);
}

TEST(GpuTimelineProfilerTest, RetainedLatestFramePublishesWithoutExternalSink)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });

  profiler.SetEnabled(true);
  profiler.SetRetainLatestFrame(true);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    31U,
  });

  auto recorder = AcquireTelemetryRecorder(*graphics, "RetainedFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });

  {
    oxygen::graphics::GpuEventScope scope(
      *recorder, "Retained", oxygen::profiling::ProfileGranularity::kTelemetry);
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    32U,
  });

  const auto frame = profiler.GetLastPublishedFrame();
  if (!frame.has_value()) {
    FAIL() << "Expected frame to have a value";
  }
  EXPECT_EQ(frame->frame_sequence, 31U);
  ASSERT_EQ(frame->scopes.size(), 1U);
  EXPECT_EQ(frame->scopes.front().display_name, "Retained");
  EXPECT_TRUE(frame->scopes.front().valid);
}

TEST(GpuTimelineProfilerTest, LargeAbsoluteTicksStillProduceNonZeroDurations)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();

  graphics->GetTimestampQueryProvider().SetNextTick(1'000'000'000'000U);
  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(8U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    41U,
  });

  auto recorder = AcquireTelemetryRecorder(*graphics, "LargeAbsoluteTicks");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });

  {
    oxygen::graphics::GpuEventScope outer(
      *recorder, "Outer", oxygen::profiling::ProfileGranularity::kTelemetry);
    {
      oxygen::graphics::GpuEventScope inner(
        *recorder, "Inner", oxygen::profiling::ProfileGranularity::kTelemetry);
    }
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    42U,
  });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 2U);
  EXPECT_TRUE(frame.scopes.at(0).valid);
  EXPECT_TRUE(frame.scopes.at(1).valid);
  EXPECT_GT(frame.scopes.at(0).duration_ms, 0.0F);
  EXPECT_GT(frame.scopes.at(1).duration_ms, 0.0F);
  EXPECT_GT(frame.scopes.at(1).start_ms, 0.0F);
}

TEST(GpuTimelineProfilerTest, DeeplyNestedScopesPreserveParentChain)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(32U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    41U,
  });

  auto recorder = AcquireTelemetryRecorder(*graphics, "NestedFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });

  auto scopes
    = std::vector<std::unique_ptr<oxygen::graphics::GpuEventScope>> {};
  scopes.reserve(8U);
  for (int i = 0; i < 8; ++i) {
    scopes.push_back(std::make_unique<oxygen::graphics::GpuEventScope>(
      *recorder, "Nested", oxygen::profiling::ProfileGranularity::kTelemetry));
  }
  scopes.clear();

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    42U,
  });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 8U);
  for (uint32_t i = 0; i < frame.scopes.size(); ++i) {
    EXPECT_TRUE(frame.scopes.at(i).valid);
    EXPECT_EQ(frame.scopes.at(i).depth, i);
    if (i == 0U) {
      EXPECT_EQ(frame.scopes.at(i).parent_scope_id, 0xFFFFFFFFU);
    } else {
      EXPECT_EQ(frame.scopes.at(i).parent_scope_id, i - 1U);
    }
  }
  EXPECT_EQ(
    graphics->GetTimestampQueryProvider().LastResolvedQueryCount(), 16U);
}

TEST(GpuTimelineProfilerTest, OverflowStopsFurtherScopesAndPublishesDiagnostic)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(1U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    7U,
  });

  auto recorder = AcquireTelemetryRecorder(*graphics, "OverflowFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });

  {
    oxygen::graphics::GpuEventScope first(
      *recorder, "First", oxygen::profiling::ProfileGranularity::kTelemetry);
  }
  {
    oxygen::graphics::GpuEventScope second(
      *recorder, "Second", oxygen::profiling::ProfileGranularity::kTelemetry);
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    8U,
  });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  EXPECT_TRUE(frame.overflowed);
  ASSERT_EQ(frame.scopes.size(), 1U);
  EXPECT_THAT(frame.diagnostics,
    testing::Contains(
      testing::Field(&GpuTimelineDiagnostic::code, "gpu.timestamp.overflow")));
}

TEST(GpuTimelineProfilerTest, IncompleteScopeIsMarkedInvalid)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    12U,
  });

  auto recorder = AcquireTelemetryRecorder(*graphics, "IncompleteFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });

  const auto token = recorder->BeginProfileScope({
    .label = "Leaked",
    .granularity = oxygen::profiling::ProfileGranularity::kTelemetry,
  });
  EXPECT_NE(token.flags & oxygen::graphics::kGpuScopeTokenFlagActive, 0U);

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    13U,
  });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 1U);
  EXPECT_FALSE(frame.scopes.front().valid);
  EXPECT_THAT(frame.diagnostics,
    testing::Contains(testing::Field(
      &GpuTimelineDiagnostic::code, "gpu.timestamp.incomplete_scope")));
}

TEST(GpuTimelineProfilerTest, DelayedFramesKeepIndependentTimestampStorage)
{
  auto graphics = MakeGraphics();
  // MakeGraphics installs FakeCommandQueue; this build intentionally has RTTI
  // disabled. NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto& queue = static_cast<oxygen::vortex::testing::FakeCommandQueue&>(
    *graphics->GetCommandQueue(QueueRole::kGraphics));
  queue.SetAutoComplete(false);
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();
  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(4U);
  profiler.AddSink(sink);

  for (uint64_t sequence = 1U; sequence <= 3U; ++sequence) {
    profiler.OnFrameStart(oxygen::frame::SequenceNumber {
      sequence,
    });
    auto recorder = AcquireTelemetryRecorder(*graphics, "DelayedFrame");
    recorder->SetTelemetryCollector(
      observer_ptr<oxygen::graphics::IGpuProfileCollector> {
        &profiler,
      });
    graphics->GetTimestampQueryProvider().SetNextTick(sequence * 1000U);
    {
      oxygen::graphics::GpuEventScope scope(*recorder, "DelayedScope",
        oxygen::profiling::ProfileGranularity::kTelemetry);
      graphics->GetTimestampQueryProvider().SetNextTick(sequence * 1100U);
    }
    static_cast<void>(recorder.Submit());
    profiler.OnFrameRecordTailResolve();
    EXPECT_TRUE(sink->frames.empty());
  }

  profiler.SetEnabled(false);
  queue.CompleteThrough(queue.GetCurrentValue());
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    4U,
  });
  ASSERT_EQ(sink->frames.size(), 3U);
  for (std::size_t i = 0; i < sink->frames.size(); ++i) {
    const auto& frame = sink->frames.at(i);
    EXPECT_EQ(frame.frame_sequence, i + 1U);
    ASSERT_EQ(frame.scopes.size(), 1U);
    EXPECT_TRUE(frame.scopes.front().valid);
    EXPECT_EQ(frame.scopes.front().display_name, "DelayedScope");
    EXPECT_NEAR(frame.scopes.front().duration_ms,
      0.1F * static_cast<float>(i + 1U), 1.0e-6F);
    EXPECT_TRUE(frame.diagnostics.empty());
  }
}

TEST(GpuTimelineProfilerTest, CaptureBacklogReportsMissingFrameWithoutOverwrite)
{
  auto graphics = MakeGraphics();
  // MakeGraphics installs FakeCommandQueue; this build intentionally has RTTI
  // disabled. NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto& queue = static_cast<oxygen::vortex::testing::FakeCommandQueue&>(
    *graphics->GetCommandQueue(QueueRole::kGraphics));
  queue.SetAutoComplete(false);
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();
  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(1U);
  profiler.AddSink(sink);
  constexpr auto capture_slots = oxygen::frame::kFramesInFlight.get() + 1U;
  for (uint64_t sequence = 1U; sequence <= capture_slots + 1U; ++sequence) {
    profiler.OnFrameStart(oxygen::frame::SequenceNumber {
      sequence,
    });
    auto recorder = AcquireTelemetryRecorder(*graphics, "BacklogFrame");
    recorder->SetTelemetryCollector(
      observer_ptr<oxygen::graphics::IGpuProfileCollector> {
        &profiler,
      });
    {
      oxygen::graphics::GpuEventScope scope(
        *recorder, "Scope", oxygen::profiling::ProfileGranularity::kTelemetry);
    }
    static_cast<void>(recorder.Submit());
    profiler.OnFrameRecordTailResolve();
  }
  EXPECT_EQ(
    graphics->GetTimestampQueryProvider().WriteCount(), 2U * capture_slots);
  EXPECT_EQ(
    graphics->GetTimestampQueryProvider().ResolveCount(), capture_slots);
  queue.CompleteThrough(queue.GetCurrentValue());
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    capture_slots + 2U,
  });
  ASSERT_EQ(sink->frames.size(), capture_slots + 1U);
  const auto missing
    = std::ranges::find(sink->frames, static_cast<uint64_t>(capture_slots + 1U),
      &GpuTimelineFrame::frame_sequence);
  ASSERT_NE(missing, sink->frames.end());
  EXPECT_FALSE(missing->profiling_enabled);
  EXPECT_THAT(missing->diagnostics,
    testing::Contains(testing::Field(
      &GpuTimelineDiagnostic::code, "gpu.timestamp.capture_backlog")));
}

TEST(GpuTimelineProfilerTest, CapacityGrowthPreservesPendingCaptures)
{
  auto graphics = MakeGraphics();
  // MakeGraphics installs FakeCommandQueue; this build intentionally has RTTI
  // disabled. NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto& queue = static_cast<oxygen::vortex::testing::FakeCommandQueue&>(
    *graphics->GetCommandQueue(QueueRole::kGraphics));
  queue.SetAutoComplete(false);
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();
  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(1U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    1U,
  });
  auto recorder = AcquireTelemetryRecorder(*graphics, "BeforeGrowth");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });
  {
    oxygen::graphics::GpuEventScope scope(
      *recorder, "Original", oxygen::profiling::ProfileGranularity::kTelemetry);
  }
  static_cast<void>(recorder.Submit());
  profiler.OnFrameRecordTailResolve();
  const auto old_capacity = graphics->GetTimestampQueryProvider().GetCapacity();
  profiler.SetMaxScopesPerFrame(8U);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    2U,
  });
  EXPECT_EQ(graphics->GetTimestampQueryProvider().GetCapacity(), old_capacity);
  queue.CompleteThrough(queue.GetCurrentValue());
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    3U,
  });
  EXPECT_GT(graphics->GetTimestampQueryProvider().GetCapacity(), old_capacity);
  const auto original = std::ranges::find(sink->frames,
    uint64_t {
      1U,
    },
    &GpuTimelineFrame::frame_sequence);
  ASSERT_NE(original, sink->frames.end());
  ASSERT_EQ(original->scopes.size(), 1U);
  EXPECT_TRUE(original->scopes.front().valid);
  EXPECT_EQ(original->scopes.front().display_name, "Original");
  EXPECT_NEAR(original->scopes.front().duration_ms, 0.1F, 1.0e-6F);
}

TEST(GpuTimelineProfilerTest, FailedResolvePublishesInvalidTiming)
{
  auto graphics = MakeGraphics();
  graphics->GetTimestampQueryProvider().SetResolveSucceeds(false);
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  auto sink = std::make_shared<CapturingSink>();
  profiler.SetEnabled(true);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    1U,
  });
  auto recorder = AcquireTelemetryRecorder(*graphics, "FailedResolve");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> {
      &profiler,
    });
  {
    oxygen::graphics::GpuEventScope scope(
      *recorder, "Scope", oxygen::profiling::ProfileGranularity::kTelemetry);
  }
  static_cast<void>(recorder.Submit());
  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    2U,
  });
  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  EXPECT_EQ(frame.frame_sequence, 1U);
  EXPECT_FALSE(frame.profiling_enabled);
  ASSERT_EQ(frame.scopes.size(), 1U);
  EXPECT_FALSE(frame.scopes.front().valid);
  EXPECT_THAT(frame.diagnostics,
    testing::Contains(testing::Field(
      &GpuTimelineDiagnostic::code, "gpu.timestamp.resolve_failed")));
}

TEST(GpuTimelineProfilerTest, RecordingRetainsInvalidFramesInExactWindow)
{
  auto graphics = MakeGraphics();
  const auto directory = std::filesystem::temp_directory_path()
    / ("oxygen_timeline_recording_"
      + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto path = directory / "frames.json";
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(1U);
  for (uint64_t sequence = 20U; sequence <= 24U; ++sequence) {
    profiler.OnFrameStart(oxygen::frame::SequenceNumber {
      sequence,
    });
    if (sequence == 21U) {
      EXPECT_FALSE(profiler.RequestRecording(path, 0U));
      ASSERT_TRUE(profiler.RequestRecording(path, 3U));
      EXPECT_FALSE(profiler.RequestRecording(directory / "second.json", 1U));
    }
    auto recorder = AcquireTelemetryRecorder(*graphics, "RecordedFrame");
    recorder->SetTelemetryCollector(
      observer_ptr<oxygen::graphics::IGpuProfileCollector> {
        &profiler,
      });
    {
      oxygen::graphics::GpuEventScope scope(*recorder, "RecordedScope",
        oxygen::profiling::ProfileGranularity::kTelemetry);
    }
    if (sequence == 22U) {
      oxygen::graphics::GpuEventScope scope(*recorder, "OverflowScope",
        oxygen::profiling::ProfileGranularity::kTelemetry);
    }
    static_cast<void>(recorder.Submit());
    profiler.OnFrameRecordTailResolve();
  }
  auto stream = std::ifstream(path);
  const auto report = nlohmann::json::parse(stream);
  stream.close();
  EXPECT_EQ(report.at("version"), 2);
  EXPECT_EQ(report.at("first_frame_seq"), 21);
  EXPECT_EQ(report.at("requested_frames"), 3);
  EXPECT_EQ(report.at("written_frames"), 3);
  EXPECT_EQ(report.at("complete"), true);
  EXPECT_EQ(report.at("timing_valid"), false);
  const auto& frames = report.at("frames");
  ASSERT_EQ(frames.size(), 3U);
  for (std::size_t i = 0U; i < frames.size(); ++i) {
    EXPECT_EQ(frames.at(i).at("frame_seq"), 21U + i);
  }
  EXPECT_EQ(frames.at(1).at("overflowed"), true);
  EXPECT_FALSE(frames.at(1).at("diagnostics").empty());
  EXPECT_FALSE(std::filesystem::exists(directory / "second.json"));
  EXPECT_EQ(std::filesystem::remove_all(directory), 2U);
}

TEST(GpuTimelineProfilerTest, EmptyAndDisabledFramesTerminateRecordingWindow)
{
  auto graphics = MakeGraphics();
  const auto directory = std::filesystem::temp_directory_path()
    / ("oxygen_timeline_gaps_"
      + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto path = directory / "frames.json";
  {
    auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
      graphics.get(),
    });
    profiler.SetEnabled(true);
    for (uint64_t sequence = 21U; sequence <= 25U; ++sequence) {
      profiler.SetEnabled(sequence != 23U);
      profiler.OnFrameStart(oxygen::frame::SequenceNumber {
        sequence,
      });
      if (sequence == 21U) {
        ASSERT_TRUE(profiler.RequestRecording(path, 4U));
      }
      if (sequence != 22U && sequence != 23U) {
        auto recorder = AcquireTelemetryRecorder(*graphics, "GapControl");
        recorder->SetTelemetryCollector(
          observer_ptr<oxygen::graphics::IGpuProfileCollector> {
            &profiler,
          });
        oxygen::graphics::GpuEventScope scope(*recorder, "Scope",
          oxygen::profiling::ProfileGranularity::kTelemetry);
      }
      profiler.OnFrameRecordTailResolve();
    }
    auto stream = std::ifstream(path);
    const auto report = nlohmann::json::parse(stream);
    stream.close();
    EXPECT_EQ(report.at("complete"), true);
    EXPECT_EQ(report.at("timing_valid"), false);
    ASSERT_EQ(report.at("frames").size(), 4U);
    for (const auto index : {
           1U,
           2U,
         }) {
      const auto& frame = report.at("frames").at(index);
      EXPECT_TRUE(frame.at("scopes").empty());
      EXPECT_EQ(
        frame.at("diagnostics").at(0).at("code"), "gpu.timestamp.unavailable");
    }
    EXPECT_TRUE(profiler.RequestRecording(directory / "next.json", 1U));
  }
  EXPECT_EQ(std::filesystem::remove_all(directory), 3U);
}

TEST(GpuTimelineProfilerTest, RecordingKeepsDelayedFramesAfterBacklogDiagnostic)
{
  auto graphics = MakeGraphics();
  // MakeGraphics installs FakeCommandQueue; this build intentionally has RTTI
  // disabled. NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto& queue = static_cast<oxygen::vortex::testing::FakeCommandQueue&>(
    *graphics->GetCommandQueue(QueueRole::kGraphics));
  queue.SetAutoComplete(false);
  const auto directory = std::filesystem::temp_directory_path()
    / ("oxygen_timeline_delayed_recording_"
      + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto path = directory / "frames.json";
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
    graphics.get(),
  });
  profiler.SetEnabled(true);
  constexpr auto frame_count = oxygen::frame::kFramesInFlight.get() + 2U;
  for (uint64_t sequence = 1U; sequence <= frame_count; ++sequence) {
    profiler.OnFrameStart(oxygen::frame::SequenceNumber {
      sequence,
    });
    if (sequence == 1U) {
      ASSERT_TRUE(profiler.RequestRecording(path, frame_count));
    }
    {
      auto recorder = AcquireTelemetryRecorder(*graphics, "DelayedRecording");
      recorder->SetTelemetryCollector(
        observer_ptr<oxygen::graphics::IGpuProfileCollector> {
          &profiler,
        });
      oxygen::graphics::GpuEventScope scope(
        *recorder, "Scope", oxygen::profiling::ProfileGranularity::kTelemetry);
    }
    profiler.OnFrameRecordTailResolve();
  }
  // The unavailable last frame is published before the earlier GPU captures.
  // A later sequence must not truncate those genuinely pending captures.
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    frame_count + 1U,
  });
  queue.CompleteThrough(queue.GetCurrentValue());
  profiler.OnFrameStart(oxygen::frame::SequenceNumber {
    frame_count + 2U,
  });
  auto stream = std::ifstream(path);
  const auto report = nlohmann::json::parse(stream);
  stream.close();
  EXPECT_EQ(report.at("complete"), true);
  EXPECT_EQ(report.at("timing_valid"), false);
  EXPECT_EQ(report.at("written_frames"), frame_count);
  ASSERT_EQ(report.at("frames").size(), frame_count);
  EXPECT_EQ(report.at("frames").at(0).at("frame_seq"), frame_count);
  for (uint32_t index = 1U; index < frame_count; ++index) {
    EXPECT_EQ(report.at("frames").at(index).at("frame_seq"), index);
    EXPECT_EQ(
      report.at("frames").at(index).at("scopes").at(0).at("valid"), true);
  }
  EXPECT_EQ(std::filesystem::remove_all(directory), 2U);
}

TEST(GpuTimelineProfilerTest, RecordingShutdownMarksMissingFramesIncomplete)
{
  auto graphics = MakeGraphics();
  const auto directory = std::filesystem::temp_directory_path()
    / ("oxygen_timeline_partial_"
      + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto path = directory / "frames.json";
  {
    auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
      graphics.get(),
    });
    EXPECT_FALSE(profiler.RequestRecording(path, 4U));
    profiler.SetEnabled(true);
    profiler.OnFrameStart(oxygen::frame::SequenceNumber {
      1U,
    });
    EXPECT_FALSE(profiler.RequestRecording(path, 1'000'001U));
    ASSERT_TRUE(profiler.RequestRecording(path, 4U));
    auto recorder = AcquireTelemetryRecorder(*graphics, "PartialRecording");
    recorder->SetTelemetryCollector(
      observer_ptr<oxygen::graphics::IGpuProfileCollector> {
        &profiler,
      });
    {
      oxygen::graphics::GpuEventScope scope(
        *recorder, "Scope", oxygen::profiling::ProfileGranularity::kTelemetry);
    }
    static_cast<void>(recorder.Submit());
    profiler.OnFrameRecordTailResolve();
    profiler.OnFrameStart(oxygen::frame::SequenceNumber {
      2U,
    });
  }
  auto stream = std::ifstream(path);
  const auto report = nlohmann::json::parse(stream);
  stream.close();
  EXPECT_EQ(report.at("complete"), false);
  EXPECT_EQ(report.at("timing_valid"), false);
  EXPECT_EQ(report.at("requested_frames"), 4);
  EXPECT_EQ(report.at("written_frames"), 1);
  EXPECT_EQ(report.at("frames").size(), 1U);
  EXPECT_EQ(std::filesystem::remove_all(directory), 2U);
}

TEST(GpuTimelineProfilerTest, OneShotExportWritesJsonFrame)
{
  auto graphics = MakeGraphics();
  const auto unique_suffix = std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count());
  const auto export_dir = std::filesystem::temp_directory_path()
    / ("oxygen_vortex_gpu_timeline_test_" + unique_suffix);
  const auto export_path = export_dir / "frame.json";

  {
    auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> {
      graphics.get(),
    });

    profiler.SetEnabled(true);
    profiler.OnFrameStart(oxygen::frame::SequenceNumber {
      21U,
    });

    auto recorder = AcquireTelemetryRecorder(*graphics, "ExportFrame");
    recorder->SetTelemetryCollector(
      observer_ptr<oxygen::graphics::IGpuProfileCollector> {
        &profiler,
      });

    {
      oxygen::graphics::GpuEventScope scope(*recorder, "Exported",
        oxygen::profiling::ProfileGranularity::kTelemetry);
    }

    profiler.OnFrameRecordTailResolve();
    profiler.RequestOneShotExport(export_path);
    profiler.OnFrameStart(oxygen::frame::SequenceNumber {
      22U,
    });

    ASSERT_TRUE(WaitForFile(export_path));
  }

  auto in = std::ifstream(export_path);
  const auto text = std::string(
    (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  EXPECT_THAT(text, testing::HasSubstr("\"frame_seq\": 21"));
  EXPECT_THAT(text, testing::HasSubstr("\"name\": \"Exported\""));
  EXPECT_EQ(std::filesystem::remove_all(export_dir), 2U);
}

} // namespace
