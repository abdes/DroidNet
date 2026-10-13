//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Environment/Internal/IblWorkBudget.h>

namespace {
using oxygen::vortex::environment::internal::IblGpuDispatch;
using oxygen::vortex::environment::internal::IblWorkBudget;
using oxygen::vortex::internal::GpuTimelineFrame;

auto Measurement(std::uint64_t sequence, const char* shader, float ms)
  -> GpuTimelineFrame
{
  auto frame = GpuTimelineFrame {};
  frame.frame_sequence = sequence;
  frame.profiling_enabled = true;
  frame.scopes.push_back({ .display_name = IblWorkBudget::TimingLabel(shader),
    .duration_ms = ms,
    .valid = true });
  return frame;
}

TEST(IblWorkBudgetTest, AdmissionAccountsForWorkRatherThanDispatchCount)
{
  const auto budget = std::make_unique<IblWorkBudget>();
  const auto work = std::array { IblGpuDispatch { .shader = "IblPrefilterCS",
                                   .work_units = 4'000'000U },
    IblGpuDispatch { .shader = "IblPrefilterCS", .work_units = 100U },
    IblGpuDispatch { .shader = "IblPrefilterCS", .work_units = 4'000'000U } };
  const auto selected = budget->Select(work, 0.45);
  EXPECT_EQ(selected.count, 2U);
  EXPECT_LE(selected.predicted_ms, 0.45);
  EXPECT_EQ(budget->Select(work, 0.0).count, 0U);
  const auto oversized = budget->Select(work, 0.001);
  EXPECT_EQ(oversized.count, 1U);
  EXPECT_GT(oversized.predicted_ms, 0.001);
}

TEST(IblWorkBudgetTest, CompletedSamplesPredictAnUnseenDispatchSize)
{
  const auto budget = std::make_unique<IblWorkBudget>();
  for (std::uint64_t sequence = 1U; sequence <= 200U; ++sequence) {
    const auto units = sequence % 2U ? 1536U : 98304U;
    const auto work = std::array { IblGpuDispatch {
      .shader = "IblCapturePrepareCS", .work_units = units } };
    budget->Record(oxygen::frame::SequenceNumber { sequence }, work);
    // Controlled device: 8 us launch cost plus 50 us per reference face set.
    EXPECT_TRUE(budget->ConsumeFrame(Measurement(
      sequence, work[0].shader, 0.008F + 0.050F * float(units) / 98304.0F)));
  }
  EXPECT_EQ(budget->SampleCount(), 200U);
  EXPECT_NEAR(
    budget->Predict({ .shader = "IblCapturePrepareCS", .work_units = 49152U }),
    0.033, 0.001);
}

TEST(IblWorkBudgetTest, InvalidOrRejectedFramesCannotPartiallyTrain)
{
  const auto budget = std::make_unique<IblWorkBudget>();
  const auto work = std::array { IblGpuDispatch { .shader = "IblNormalizeCS",
                                   .work_units = 98304U },
    IblGpuDispatch { .shader = "IblShCS", .work_units = 98304U } };
  const auto before = budget->Predict(work[0]);
  for (std::uint64_t sequence = 1U; sequence <= 4U; ++sequence) {
    budget->Record(oxygen::frame::SequenceNumber { sequence }, work);
    auto frame = Measurement(sequence, work[0].shader, 20.0F);
    frame.scopes.push_back(
      { .display_name = IblWorkBudget::TimingLabel(work[1].shader),
        .duration_ms = 10.0F,
        .valid = true });
    if (sequence == 1U)
      frame.scopes.back().valid = false;
    else if (sequence == 2U)
      frame.scopes.pop_back();
    else if (sequence == 3U)
      frame.overflowed = true;
    else
      budget->Reject(oxygen::frame::SequenceNumber { sequence });
    EXPECT_TRUE(budget->ConsumeFrame(frame));
    EXPECT_EQ(budget->Predict(work[0]), before);
    EXPECT_EQ(budget->SampleCount(), 0U);
  }
}

TEST(IblWorkBudgetTest, DelayedFeedbackCannotTrainAReusedFrameSlot)
{
  const auto budget = std::make_unique<IblWorkBudget>();
  const auto work = std::array { IblGpuDispatch {
    .shader = "IblNormalizeCS", .work_units = 98304U } };
  budget->Record(oxygen::frame::SequenceNumber { 1U }, work);
  const auto next = 1U + oxygen::frame::kFramesInFlight.get() + 1U;
  budget->Record(oxygen::frame::SequenceNumber { next }, work);
  EXPECT_TRUE(budget->ConsumeFrame(Measurement(1U, work[0].shader, 20.0F)));
  EXPECT_EQ(budget->SampleCount(), 0U);
  EXPECT_TRUE(budget->ConsumeFrame(Measurement(next, work[0].shader, 0.04F)));
  EXPECT_EQ(budget->SampleCount(), 1U);
  budget->Close();
  EXPECT_FALSE(
    budget->ConsumeFrame(Measurement(next + 1U, work[0].shader, 0.04F)));
}
TEST(IblWorkBudgetTest, BatchSamplesSeparateLaunchCountFromWorkSize)
{
  const auto budget = std::make_unique<IblWorkBudget>();
  for (std::uint64_t sequence = 0U; sequence < 2000U; ++sequence) {
    const auto count = sequence % 2U ? 2U : 8U;
    auto work = std::array<IblGpuDispatch, 8> {};
    for (auto& step : work)
      step = { .shader = "IblCapturePrepareCS", .work_units = 98304U / count };
    budget->Record(
      oxygen::frame::SequenceNumber { sequence }, std::span(work).first(count));
    auto frame = Measurement(sequence, work[0].shader, 0.008F * count + 0.05F);
    EXPECT_TRUE(budget->ConsumeFrame(frame));
    // Duplicate feedback is ignored, including for frame sequence zero.
    EXPECT_TRUE(budget->ConsumeFrame(frame));
  }
  EXPECT_EQ(budget->SampleCount(), 2000U);
  const auto prediction = 3.0
    * budget->Predict(
      { .shader = "IblCapturePrepareCS", .work_units = 16384U });
  EXPECT_NEAR(prediction, 0.049, 0.002);
}

TEST(IblWorkBudgetTest, MipDependenciesSplitBatchesButIndependentTilesShare)
{
  const auto budget = std::make_unique<IblWorkBudget>();
  const auto work = std::array { IblGpuDispatch { .shader = "IblMipCS",
                                   .output_size = 64U,
                                   .work_units = 4096U },
    IblGpuDispatch {
      .shader = "IblMipCS", .output_size = 64U, .work_units = 4096U },
    IblGpuDispatch {
      .shader = "IblMipCS", .output_size = 32U, .work_units = 1024U } };
  budget->Record(oxygen::frame::SequenceNumber { 1U }, work);
  auto frame = Measurement(1U, work[0].shader, 0.01F);
  frame.scopes.push_back(frame.scopes.front());
  EXPECT_TRUE(budget->ConsumeFrame(frame));
  EXPECT_EQ(budget->SampleCount(), 2U);
}
} // namespace
