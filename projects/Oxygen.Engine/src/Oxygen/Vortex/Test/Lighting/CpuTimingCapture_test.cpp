//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <Windows.h> // IWYU pragma: keep
#include <profileapi.h>
#include <winnt.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/CpuScopeObserver.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Support/CpuTimingCapture.h>

namespace oxygen::vortex::testing {
namespace {
  class CpuTimingCaptureTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override
    {
      const auto suffix
        = std::chrono::steady_clock::now().time_since_epoch().count();
      directory_ = std::filesystem::temp_directory_path()
        / ("oxygen-cpu-timing-" + std::to_string(suffix));
      owned_ = std::filesystem::create_directory(directory_);
      ASSERT_TRUE(owned_);
      path_ = directory_ / "timing.csv";
    }
    auto TearDown() -> void override
    {
      if (owned_) {
        auto error = std::error_code {};
        std::filesystem::remove(path_, error);
        std::filesystem::remove(directory_, error);
      }
    }
    [[nodiscard]] auto ReadCsv() const -> std::string
    {
      auto input = std::ifstream(path_);
      auto contents = std::ostringstream {};
      contents << input.rdbuf();
      return contents.str();
    }
    std::filesystem::path path_;

  private:
    std::filesystem::path directory_;
    bool owned_ { false };
  };

  NOLINT_TEST_F(
    CpuTimingCaptureTest, CompactCaptureKeepsRootAndWaitWithoutDoubleCounting)
  {
    auto capture
      = CpuTimingCapture({ .record_capacity = CpuTimingRecordCapacity { 3U } });
    LARGE_INTEGER begin {};
    LARGE_INTEGER end {};
    LARGE_INTEGER frequency {};
    ASSERT_TRUE(QueryPerformanceFrequency(&frequency));
    ASSERT_TRUE(QueryPerformanceCounter(&begin));
    capture.BeginFrame(frame::SequenceNumber { 41U });
    {
      const auto observer = profiling::ScopedCpuScopeObserver(capture);
      const auto outer = profiling::CpuProfileScope("Vortex.Lighting.Prepare");
      const auto inner
        = profiling::CpuProfileScope("Vortex.Lighting.BuildGrid");
      const auto ignored = profiling::CpuProfileScope("Graphics.Map");
      const auto wait = profiling::CpuProfileScope("D3D12.FenceWait");
    }
    capture.BeginFrame(frame::SequenceNumber { 42U });
    {
      const auto observer = profiling::ScopedCpuScopeObserver(capture);
      const auto shadow = profiling::CpuProfileScope("Vortex.Shadows.Prepare");
    }
    ASSERT_TRUE(QueryPerformanceCounter(&end));
    const auto report = capture.Save(path_);
    EXPECT_EQ(report.at("records"), 3U);
    EXPECT_EQ(report.at("record_capacity"), 3U);
    EXPECT_EQ(report.at("qpc_frequency"), frequency.QuadPart);
    const auto csv = ReadCsv();
    EXPECT_NE(csv.find(",lighting,"), std::string::npos);
    EXPECT_NE(csv.find(",fence_wait,"), std::string::npos);
    EXPECT_EQ(csv.find("BuildGrid"), std::string::npos);
    EXPECT_EQ(csv.find("Graphics.Map"), std::string::npos);
    EXPECT_NE(csv.find("\n42,"), std::string::npos);
    auto rows = std::istringstream(csv);
    auto row = std::string {};
    ASSERT_TRUE(static_cast<bool>(std::getline(rows, row)));
    unsigned checked = 0U;
    while (std::getline(rows, row)) {
      auto fields = std::istringstream(row);
      auto field = std::string {};
      for (unsigned column = 0U; column < 3U; ++column) {
        ASSERT_TRUE(static_cast<bool>(std::getline(fields, field, ',')));
      }
      ASSERT_TRUE(static_cast<bool>(std::getline(fields, field, ',')));
      const auto start = std::stoll(field);
      ASSERT_TRUE(static_cast<bool>(std::getline(fields, field, ',')));
      const auto finish = std::stoll(field);
      EXPECT_GE(start, begin.QuadPart);
      EXPECT_LE(finish, end.QuadPart);
      EXPECT_GE(finish, start);
      ++checked;
    }
    EXPECT_EQ(checked, 3U);
    EXPECT_THROW(static_cast<void>(capture.Save(path_)), std::runtime_error);
  }

  NOLINT_TEST_F(CpuTimingCaptureTest, DetailedCaptureRetainsNestedPhases)
  {
    auto capture = CpuTimingCapture(
      { .record_capacity = CpuTimingRecordCapacity { 4U }, .detailed = true });
    capture.BeginFrame(frame::SequenceNumber { 1U });
    {
      const auto observer = profiling::ScopedCpuScopeObserver(capture);
      const auto unrelated = profiling::CpuProfileScope("Editor.Update");
      const auto outer = profiling::CpuProfileScope("Vortex.Lighting.Prepare");
      const auto inner
        = profiling::CpuProfileScope("Vortex.Lighting.BuildGrid");
      const auto detail = profiling::CpuProfileScope("Graphics.AllocateBuffer");
      const auto wait = profiling::CpuProfileScope("D3D12.FenceWait");
    }
    EXPECT_EQ(capture.Save(path_).at("records"), 4U);
    const auto csv = ReadCsv();
    EXPECT_NE(csv.find("Vortex.Lighting.BuildGrid"), std::string::npos);
    EXPECT_NE(csv.find("Graphics.AllocateBuffer"), std::string::npos);
    EXPECT_EQ(csv.find("Editor.Update"), std::string::npos);
  }

  NOLINT_TEST_F(CpuTimingCaptureTest, ExposureAttributionKeepsSharedViewOwner)
  {
    auto capture = CpuTimingCapture({
      .domain = CpuTimingDomain::kExposure,
      .record_capacity = CpuTimingRecordCapacity { 2U },
    });
    capture.BeginFrame(frame::SequenceNumber { 5U });
    EXPECT_FALSE(
      capture.OnScopeBegin({ .label = "Vortex.PostProcess.Execute" }));
    EXPECT_FALSE(capture.OnScopeBegin({ .label = "Vortex.Lighting.Prepare" }));
    EXPECT_FALSE(capture.OnScopeBegin({ .label = "D3D12.FenceWait" }));
    ASSERT_TRUE(
      capture.OnScopeBegin({ .label = "Graphics.AcquireCommandRecorder",
        .variables
        = profiling::Vars(profiling::Var("recording", "Vortex View")) }));
    capture.OnScopeEnd();
    ASSERT_TRUE(capture.OnScopeBegin(
      { .label = "Vortex.SceneRenderer.PrepareExposureDomain" }));
    capture.OnScopeEnd();
    EXPECT_EQ(capture.Save(path_).at("records"), 2U);
    EXPECT_NE(ReadCsv().find(",exposure,"), std::string::npos);
  }

  NOLINT_TEST_F(CpuTimingCaptureTest, RecordCapacityOverflowRejectsCapture)
  {
    auto capture
      = CpuTimingCapture({ .record_capacity = CpuTimingRecordCapacity { 1U } });
    capture.BeginFrame(frame::SequenceNumber { 1U });
    for (unsigned index = 0U; index < 2U; ++index) {
      ASSERT_TRUE(capture.OnScopeBegin({ .label = "Vortex.Lighting.Prepare" }));
      capture.OnScopeEnd();
    }
    EXPECT_THROW(static_cast<void>(capture.Save(path_)), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(path_));
  }

  NOLINT_TEST_F(CpuTimingCaptureTest, StackOverflowRejectsCapture)
  {
    auto capture
      = CpuTimingCapture({ .record_capacity = CpuTimingRecordCapacity { 1U } });
    capture.BeginFrame(frame::SequenceNumber { 1U });
    for (unsigned index = 0U; index < 128U; ++index) {
      ASSERT_TRUE(capture.OnScopeBegin({ .label = "Vortex.Lighting.Nested" }));
    }
    EXPECT_FALSE(capture.OnScopeBegin({ .label = "Vortex.Lighting.Overflow" }));
    for (unsigned index = 0U; index < 128U; ++index) {
      capture.OnScopeEnd();
    }
    EXPECT_THROW(static_cast<void>(capture.Save(path_)), std::runtime_error);
  }

  NOLINT_TEST_F(CpuTimingCaptureTest,
    MissingFrameIncompleteScopeAndInvalidLabelsAreRejected)
  {
    auto missing
      = CpuTimingCapture({ .record_capacity = CpuTimingRecordCapacity { 1U } });
    EXPECT_FALSE(missing.OnScopeBegin({ .label = "Vortex.Lighting.Prepare" }));
    EXPECT_THROW(static_cast<void>(missing.Save(path_)), std::runtime_error);
    auto incomplete
      = CpuTimingCapture({ .record_capacity = CpuTimingRecordCapacity { 1U } });
    incomplete.BeginFrame(frame::SequenceNumber { 1U });
    ASSERT_TRUE(
      incomplete.OnScopeBegin({ .label = "Vortex.Lighting.Prepare" }));
    EXPECT_THROW(static_cast<void>(incomplete.Save(path_)), std::runtime_error);
    EXPECT_THROW(
      incomplete.BeginFrame(frame::SequenceNumber { 2U }), std::logic_error);
    incomplete.OnScopeEnd();
    EXPECT_THROW(static_cast<void>(incomplete.Save(path_)), std::runtime_error);
    for (const auto& label : {
           std::string("Vortex.Lighting.Bad,Label"),
           std::string("Vortex.Lighting.") + std::string(128U, 'x'),
         }) {
      auto invalid = CpuTimingCapture(
        { .record_capacity = CpuTimingRecordCapacity { 1U } });
      invalid.BeginFrame(frame::SequenceNumber { 1U });
      ASSERT_TRUE(invalid.OnScopeBegin({ .label = label }));
      invalid.OnScopeEnd();
      EXPECT_THROW(static_cast<void>(invalid.Save(path_)), std::runtime_error);
    }
  }

  NOLINT_TEST_F(
    CpuTimingCaptureTest, NonmonotonicFramesAndCrossThreadUseInvalidateCapture)
  {
    auto capture
      = CpuTimingCapture({ .record_capacity = CpuTimingRecordCapacity { 1U } });
    capture.BeginFrame(frame::SequenceNumber { 7U });
    ASSERT_TRUE(capture.OnScopeBegin({ .label = "Vortex.Lighting.Prepare" }));
    capture.OnScopeEnd();
    EXPECT_THROW(
      capture.BeginFrame(frame::SequenceNumber { 7U }), std::logic_error);
    EXPECT_THROW(static_cast<void>(capture.Save(path_)), std::runtime_error);
    auto cross_thread
      = CpuTimingCapture({ .record_capacity = CpuTimingRecordCapacity { 1U } });
    cross_thread.BeginFrame(frame::SequenceNumber { 1U });
    auto worker = std::thread([&] -> void {
      EXPECT_FALSE(
        cross_thread.OnScopeBegin({ .label = "Vortex.Lighting.Prepare" }));
    });
    worker.join();
    EXPECT_THROW(
      static_cast<void>(cross_thread.Save(path_)), std::runtime_error);
  }

  NOLINT_TEST_F(CpuTimingCaptureTest, EmptyCapacityAndUnmatchedEndAreRejected)
  {
    EXPECT_THROW(CpuTimingCapture(CpuTimingOptions {}), std::invalid_argument);
    auto capture
      = CpuTimingCapture({ .record_capacity = CpuTimingRecordCapacity { 1U } });
    capture.BeginFrame(frame::SequenceNumber { 0U });
    capture.OnScopeEnd();
    EXPECT_THROW(static_cast<void>(capture.Save(path_)), std::runtime_error);
  }
} // namespace
} // namespace oxygen::vortex::testing
