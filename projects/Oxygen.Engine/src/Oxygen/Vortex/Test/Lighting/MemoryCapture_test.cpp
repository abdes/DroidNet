//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <nlohmann/json.hpp>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Direct3D12/MemoryStatistics.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Support/D3D12MemoryCapture.h>

namespace oxygen::vortex::testing {
namespace {
  NOLINT_TEST(
    MemoryCaptureTest, PreservesSegmentsAndSeparatesHeapSlackFromBudget)
  {
    auto capture = D3D12MemoryCapture(MemorySampleCapacity { 2U });
    const auto snapshot = graphics::d3d12::MemoryStatistics {
      .local = { .allocation_count = 3U, .block_count = 2U,
        .allocation_bytes = SizeBytes { 300U }, .block_bytes = SizeBytes { 512U },
        .estimated_usage_bytes = SizeBytes { 900U },
        .estimated_budget_bytes = SizeBytes { 800U }, },
      .non_local = { .allocation_count = 1U, .block_count = 1U,
        .allocation_bytes = SizeBytes { 64U }, .block_bytes = SizeBytes { 128U },
        .estimated_usage_bytes = SizeBytes { 200U },
        .estimated_budget_bytes = SizeBytes { 1024U }, },
    };
    ASSERT_TRUE(capture.Record(
      MemorySampleId { 4U }, frame::SequenceNumber { 9U }, snapshot));
    ASSERT_TRUE(capture.Record(
      MemorySampleId { 5U }, frame::SequenceNumber { 9U }, snapshot));
    const auto report = capture.Report();
    EXPECT_TRUE(report.at("complete").get<bool>());
    ASSERT_EQ(report.at("samples").size(), 2U);
    const auto& sample = report.at("samples").front();
    EXPECT_EQ(sample.at("sample_id"), 4U);
    EXPECT_EQ(sample.at("frame_sequence"), 9U);
    const auto& local = sample.at("local");
    EXPECT_EQ(local.at("allocation_count"), 3U);
    EXPECT_EQ(local.at("block_count"), 2U);
    EXPECT_EQ(local.at("allocation_bytes"), 300U);
    EXPECT_EQ(local.at("block_bytes"), 512U);
    EXPECT_EQ(local.at("unallocated_block_bytes"), 212U);
    // Budget pressure is a valid measurement, not a malformed capture.
    EXPECT_EQ(local.at("estimated_usage_bytes"), 900U);
    EXPECT_EQ(local.at("estimated_budget_bytes"), 800U);
    EXPECT_EQ(sample.at("non_local").at("allocation_bytes"), 64U);
    EXPECT_EQ(sample.at("non_local").at("unallocated_block_bytes"), 64U);
    EXPECT_EQ(sample.at("non_local").at("estimated_usage_bytes"), 200U);
    EXPECT_EQ(sample.at("non_local").at("estimated_budget_bytes"), 1024U);
  }

  NOLINT_TEST(MemoryCaptureTest, RejectsOverflowAndCannotExportPartialEvidence)
  {
    EXPECT_THROW(
      D3D12MemoryCapture(MemorySampleCapacity { 0U }), std::invalid_argument);
    auto capture = D3D12MemoryCapture(MemorySampleCapacity { 1U });
    EXPECT_THROW(static_cast<void>(capture.Report()), std::runtime_error);
    ASSERT_TRUE(
      capture.Record(MemorySampleId { 0U }, frame::SequenceNumber { 1U }, {}));
    EXPECT_FALSE(
      capture.Record(MemorySampleId { 1U }, frame::SequenceNumber { 2U }, {}));
    EXPECT_THROW(static_cast<void>(capture.Report()), std::runtime_error);
  }

  NOLINT_TEST(MemoryCaptureTest, RejectsInvalidOrderingAndInconsistentSegments)
  {
    for (unsigned invalid = 0U; invalid < 5U; ++invalid) {
      SCOPED_TRACE(invalid);
      auto capture = D3D12MemoryCapture(MemorySampleCapacity { 2U });
      ASSERT_TRUE(capture.Record(
        MemorySampleId { 1U }, frame::SequenceNumber { 3U }, {}));
      auto statistics = graphics::d3d12::MemoryStatistics {};
      if (invalid == 3U) {
        statistics.local.allocation_bytes = SizeBytes { 1U };
      }
      if (invalid == 4U) {
        statistics.non_local.allocation_bytes = SizeBytes { 1U };
      }
      const auto frame = invalid == 1U
        ? frame::kInvalidSequenceNumber
        : frame::SequenceNumber { invalid == 2U ? 2U : 3U };
      EXPECT_FALSE(capture.Record(
        MemorySampleId { invalid == 0U ? 1U : 2U }, frame, statistics));
      EXPECT_FALSE(capture.Record(
        MemorySampleId { 3U }, frame::SequenceNumber { 4U }, {}));
      EXPECT_THROW(static_cast<void>(capture.Report()), std::runtime_error);
    }
  }
} // namespace
} // namespace oxygen::vortex::testing
