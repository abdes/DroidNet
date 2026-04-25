//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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
    graphics.QueueKeyFor(QueueRole::kGraphics), name, true);
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
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> { graphics.get() });

  profiler.SetEnabled(false);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 1U });

  auto recorder = AcquireTelemetryRecorder(*graphics, "DisabledFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

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
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> { graphics.get() });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(8U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 1U });

  auto recorder = AcquireTelemetryRecorder(*graphics, "FrameOne");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

  {
    oxygen::graphics::GpuEventScope outer(
      *recorder, "Outer", oxygen::profiling::ProfileGranularity::kTelemetry);
    {
      oxygen::graphics::GpuEventScope inner(
        *recorder, "Inner", oxygen::profiling::ProfileGranularity::kTelemetry);
    }
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 2U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.frame_sequence, 1U);
  ASSERT_EQ(frame.scopes.size(), 2U);
  EXPECT_EQ(frame.used_query_slots, 4U);
  EXPECT_TRUE(frame.scopes[0].valid);
  EXPECT_TRUE(frame.scopes[1].valid);
  EXPECT_EQ(frame.scopes[1].parent_scope_id, 0U);
  EXPECT_EQ(frame.scopes[0].child_scope_ids.size(), 1U);
  EXPECT_EQ(frame.scopes[0].child_scope_ids.front(), 1U);
  EXPECT_GT(frame.scopes[0].duration_ms, 0.0F);
  EXPECT_GT(frame.scopes[1].duration_ms, 0.0F);
  EXPECT_EQ(graphics->GetTimestampQueryProvider().ResolveCount(), 1U);
  EXPECT_EQ(
    graphics->GetTimestampQueryProvider().LastResolvedQueryCount(), 4U);
}

TEST(GpuTimelineProfilerTest, RetainedLatestFramePublishesWithoutExternalSink)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> { graphics.get() });

  profiler.SetEnabled(true);
  profiler.SetRetainLatestFrame(true);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 31U });

  auto recorder = AcquireTelemetryRecorder(*graphics, "RetainedFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

  {
    oxygen::graphics::GpuEventScope scope(
      *recorder, "Retained", oxygen::profiling::ProfileGranularity::kTelemetry);
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 32U });

  const auto frame = profiler.GetLastPublishedFrame();
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->frame_sequence, 31U);
  ASSERT_EQ(frame->scopes.size(), 1U);
  EXPECT_EQ(frame->scopes.front().display_name, "Retained");
  EXPECT_TRUE(frame->scopes.front().valid);
}

TEST(
  GpuTimelineProfilerTest, LargeAbsoluteTicksStillProduceNonZeroDurations)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> { graphics.get() });
  auto sink = std::make_shared<CapturingSink>();

  graphics->GetTimestampQueryProvider().SetNextTick(1'000'000'000'000U);
  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(8U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 41U });

  auto recorder = AcquireTelemetryRecorder(*graphics, "LargeAbsoluteTicks");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

  {
    oxygen::graphics::GpuEventScope outer(
      *recorder, "Outer", oxygen::profiling::ProfileGranularity::kTelemetry);
    {
      oxygen::graphics::GpuEventScope inner(
        *recorder, "Inner", oxygen::profiling::ProfileGranularity::kTelemetry);
    }
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 42U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 2U);
  EXPECT_TRUE(frame.scopes[0].valid);
  EXPECT_TRUE(frame.scopes[1].valid);
  EXPECT_GT(frame.scopes[0].duration_ms, 0.0F);
  EXPECT_GT(frame.scopes[1].duration_ms, 0.0F);
  EXPECT_GT(frame.scopes[1].start_ms, 0.0F);
}

TEST(GpuTimelineProfilerTest, DeeplyNestedScopesPreserveParentChain)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> { graphics.get() });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(32U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 41U });

  auto recorder = AcquireTelemetryRecorder(*graphics, "NestedFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

  auto scopes = std::vector<std::unique_ptr<oxygen::graphics::GpuEventScope>> {};
  scopes.reserve(8U);
  for (int i = 0; i < 8; ++i) {
    scopes.push_back(std::make_unique<oxygen::graphics::GpuEventScope>(
      *recorder, "Nested", oxygen::profiling::ProfileGranularity::kTelemetry));
  }
  scopes.clear();

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 42U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 8U);
  for (uint32_t i = 0; i < frame.scopes.size(); ++i) {
    EXPECT_TRUE(frame.scopes[i].valid);
    EXPECT_EQ(frame.scopes[i].depth, i);
    if (i == 0U) {
      EXPECT_EQ(frame.scopes[i].parent_scope_id, 0xFFFFFFFFU);
    } else {
      EXPECT_EQ(frame.scopes[i].parent_scope_id, i - 1U);
    }
  }
  EXPECT_EQ(
    graphics->GetTimestampQueryProvider().LastResolvedQueryCount(), 16U);
}

TEST(
  GpuTimelineProfilerTest, OverflowStopsFurtherScopesAndPublishesDiagnostic)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> { graphics.get() });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(1U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 7U });

  auto recorder = AcquireTelemetryRecorder(*graphics, "OverflowFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

  {
    oxygen::graphics::GpuEventScope first(
      *recorder, "First", oxygen::profiling::ProfileGranularity::kTelemetry);
  }
  {
    oxygen::graphics::GpuEventScope second(
      *recorder, "Second", oxygen::profiling::ProfileGranularity::kTelemetry);
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 8U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  EXPECT_TRUE(frame.overflowed);
  ASSERT_EQ(frame.scopes.size(), 1U);
  EXPECT_THAT(frame.diagnostics,
    testing::Contains(testing::Field(
      &GpuTimelineDiagnostic::code, "gpu.timestamp.overflow")));
}

TEST(GpuTimelineProfilerTest, IncompleteScopeIsMarkedInvalid)
{
  auto graphics = MakeGraphics();
  auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> { graphics.get() });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 12U });

  auto recorder = AcquireTelemetryRecorder(*graphics, "IncompleteFrame");
  recorder->SetTelemetryCollector(
    observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

  const auto token = recorder->BeginProfileScope({
    .label = "Leaked",
    .granularity = oxygen::profiling::ProfileGranularity::kTelemetry,
  });
  EXPECT_NE(token.flags & oxygen::graphics::kGpuScopeTokenFlagActive, 0U);

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 13U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 1U);
  EXPECT_FALSE(frame.scopes.front().valid);
  EXPECT_THAT(frame.diagnostics,
    testing::Contains(testing::Field(
      &GpuTimelineDiagnostic::code, "gpu.timestamp.incomplete_scope")));
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
    auto profiler = GpuTimelineProfiler(observer_ptr<Graphics> { graphics.get() });

    profiler.SetEnabled(true);
    profiler.OnFrameStart(oxygen::frame::SequenceNumber { 21U });

    auto recorder = AcquireTelemetryRecorder(*graphics, "ExportFrame");
    recorder->SetTelemetryCollector(
      observer_ptr<oxygen::graphics::IGpuProfileCollector> { &profiler });

    {
      oxygen::graphics::GpuEventScope scope(*recorder, "Exported",
        oxygen::profiling::ProfileGranularity::kTelemetry);
    }

    profiler.OnFrameRecordTailResolve();
    profiler.RequestOneShotExport(export_path);
    profiler.OnFrameStart(oxygen::frame::SequenceNumber { 22U });

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
