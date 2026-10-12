//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkStageTable.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkSummarizer.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>

namespace {

using oxygen::vortex::GpuWorkBucket;
using oxygen::vortex::GpuWorkGroup;
using oxygen::vortex::GpuWorkItem;
using oxygen::vortex::GpuWorkSummary;
using oxygen::vortex::GpuWorkView;
using oxygen::vortex::internal::GpuTimelineFrame;
using oxygen::vortex::internal::GpuTimelineScope;
using oxygen::vortex::internal::GpuWorkSummarizer;
namespace gpu_work = oxygen::vortex::gpu_work;

constexpr auto kNoParent = std::uint32_t { 0xFFFFFFFFU };

//! Builds a timeline frame; scopes nest by the parent given to `Add`.
class FrameBuilder {
public:
  explicit FrameBuilder(const std::uint64_t sequence)
  {
    frame_.frame_sequence = sequence;
    frame_.profiling_enabled = true;
  }

  auto Add(const std::string_view label, const float ms,
    const std::uint32_t parent = kNoParent, const std::uint64_t view = 0U,
    const std::string_view name = {}) -> std::uint32_t
  {
    const auto id = static_cast<std::uint32_t>(frame_.scopes.size());
    // As the profiler does: the view is the parent view or the parent's.
    auto view_scope = kNoParent;
    if (parent != kNoParent) {
      const auto& above = frame_.scopes.at(parent);
      view_scope
        = above.name_hash == gpu_work::HashScopeLabel(gpu_work::kViewLabel)
        ? parent
        : above.view_scope_id;
    }
    frame_.scopes.push_back(GpuTimelineScope {
      .scope_id = id,
      .parent_scope_id = parent,
      .view_scope_id = view_scope,
      .name_hash = gpu_work::HashScopeLabel(label),
      .display_name = std::string(label),
      .instance_id = view,
      .instance_name = std::string(name),
      .start_ms = 0.0F,
      .end_ms = ms,
      .duration_ms = ms,
      .valid = true,
    });
    return id;
  }

  auto Frame(const float ms) -> std::uint32_t
  {
    return Add(gpu_work::kFrameLabel, ms);
  }

  auto View(const std::uint32_t frame, const std::uint64_t id,
    const std::string_view name, const float ms) -> std::uint32_t
  {
    return Add(gpu_work::kViewLabel, ms, frame, id, name);
  }

  //! A scope on its own command list under the frame span, recorded while
  //! `view` was open.
  auto AddInView(const std::string_view label, const float ms,
    const std::uint32_t frame, const std::uint32_t view) -> std::uint32_t
  {
    const auto id = Add(label, ms, frame);
    frame_.scopes.at(id).view_scope_id = view;
    return id;
  }

  [[nodiscard]] auto Get() const -> const GpuTimelineFrame& { return frame_; }
  auto Mutable() -> GpuTimelineFrame& { return frame_; }

private:
  GpuTimelineFrame frame_;
};

//! A summarizer on a clock the test advances.
class GpuWorkSummarizerTest : public testing::Test {
protected:
  using Clock = GpuWorkSummarizer::Clock;

  void SetUp() override { summarizer_.Activate(1U); }

  //! Consumes `frame`, then closes the window.
  auto Publish(const GpuTimelineFrame& frame)
    -> std::shared_ptr<const GpuWorkSummary>
  {
    Consume(frame);
    now_ += GpuWorkSummarizer::kWindow;
    Consume(Invalid(frame.frame_sequence + 1U));
    return summarizer_.Latest();
  }

  auto Consume(const GpuTimelineFrame& frame) -> void
  {
    EXPECT_TRUE(summarizer_.ConsumeFrame(frame));
  }

  static auto Invalid(const std::uint64_t sequence) -> GpuTimelineFrame
  {
    auto frame = GpuTimelineFrame {};
    frame.frame_sequence = sequence;
    frame.profiling_enabled = true;
    frame.overflowed = true;
    return frame;
  }

  static auto FindGroup(const std::vector<GpuWorkGroup>& groups,
    const GpuWorkBucket bucket) -> const GpuWorkGroup*
  {
    for (const auto& group : groups) {
      if (group.bucket == bucket) {
        return &group;
      }
    }
    return nullptr;
  }

  static auto FindItem(const GpuWorkGroup& group, const std::string_view id)
    -> const GpuWorkItem*
  {
    for (const auto& item : group.items) {
      if (item.id == id) {
        return &item;
      }
    }
    return nullptr;
  }

  Clock::time_point now_ {};
  GpuWorkSummarizer summarizer_ { [this] -> Clock::time_point {
    return now_;
  } };
};

//! A sub-scope of a stage entry adds nothing; the outermost entry carries the
//! time, and the frame span minus covered work is idle.
NOLINT_TEST_F(GpuWorkSummarizerTest, NestedScopesRollUpIntoTheOutermostEntry)
{
  auto builder = FrameBuilder { 1U };
  const auto frame = builder.Frame(10.0F);
  const auto view = builder.View(frame, 7U, "Main", 8.0F);
  const auto base = builder.Add(gpu_work::kBasePassLabel, 4.0F, view);
  builder.Add("Vortex.Stage9.BasePass.MainPass", 3.0F, base);
  builder.Add(gpu_work::kDeferredLightingLabel, 2.0F, view);

  const auto summary = Publish(builder.Get());

  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->valid_frames, 1U);
  EXPECT_EQ(summary->invalid_frames, 1U);
  EXPECT_FLOAT_EQ(summary->frame_gpu.avg_ms, 10.0F);
  ASSERT_EQ(summary->views.size(), 1U);
  const auto& main = summary->views.front();
  EXPECT_EQ(main.view_id, 7U);
  EXPECT_EQ(main.name, "Main");
  EXPECT_FLOAT_EQ(main.time.avg_ms, 6.0F);
  ASSERT_EQ(main.groups.size(), 2U);
  EXPECT_EQ(main.groups[0].bucket, GpuWorkBucket::kBasePass);
  EXPECT_FLOAT_EQ(main.groups[0].time.avg_ms, 4.0F);
  ASSERT_EQ(main.groups[0].items.size(), 1U);
  EXPECT_EQ(main.groups[0].items[0].id, "base.main");
  EXPECT_EQ(main.groups[1].bucket, GpuWorkBucket::kLighting);
  EXPECT_FLOAT_EQ(summary->idle.avg_ms, 4.0F);
  EXPECT_TRUE(summary->frame_work.empty());
}

//! Scopes outside any view are frame work; background work is frame work
//! even when recorded while a view records.
NOLINT_TEST_F(GpuWorkSummarizerTest, SplitsViewsAndFrameWork)
{
  auto builder = FrameBuilder { 1U };
  const auto frame = builder.Frame(12.0F);
  const auto main = builder.View(frame, 1U, "Main", 5.0F);
  builder.Add(gpu_work::kSkyLabel, 1.0F, main);
  builder.Add(gpu_work::kIblProcessLabel, 2.0F, main);
  const auto preview = builder.View(frame, 2U, "Preview", 3.0F);
  builder.Add(gpu_work::kSkyLabel, 0.5F, preview);
  builder.Add(gpu_work::kCompositingTaskLabel, 1.5F, frame);

  const auto summary = Publish(builder.Get());

  ASSERT_NE(summary, nullptr);
  ASSERT_EQ(summary->views.size(), 2U);
  EXPECT_FLOAT_EQ(summary->views[0].time.avg_ms, 1.0F);
  EXPECT_FLOAT_EQ(summary->views[1].time.avg_ms, 0.5F);
  EXPECT_EQ(
    FindGroup(summary->views[0].groups, GpuWorkBucket::kBackground), nullptr);
  const auto* background
    = FindGroup(summary->frame_work, GpuWorkBucket::kBackground);
  ASSERT_NE(background, nullptr);
  EXPECT_FLOAT_EQ(background->time.avg_ms, 2.0F);
  EXPECT_NE(FindItem(*background, "background.ibl"), nullptr);
  EXPECT_NE(
    FindGroup(summary->frame_work, GpuWorkBucket::kComposition), nullptr);
  EXPECT_FLOAT_EQ(summary->frame_work_time.avg_ms, 3.5F);
  EXPECT_FLOAT_EQ(summary->idle.avg_ms, 12.0F - 1.0F - 0.5F - 3.5F);
}

//! Work a view records on its own command lists is not nested in the view
//! span, yet belongs to the view.
NOLINT_TEST_F(GpuWorkSummarizerTest, SeparateRecorderWorkBelongsToItsView)
{
  auto builder = FrameBuilder { 1U };
  const auto frame = builder.Frame(10.0F);
  const auto view = builder.View(frame, 3U, "Main", 2.0F);
  builder.AddInView(gpu_work::kShadowDepthsLabel, 3.0F, frame, view);
  builder.Add(gpu_work::kTonemapLabel, 1.0F, frame);

  const auto summary = Publish(builder.Get());

  ASSERT_NE(summary, nullptr);
  ASSERT_EQ(summary->views.size(), 1U);
  EXPECT_NE(
    FindGroup(summary->views[0].groups, GpuWorkBucket::kShadows), nullptr);
  EXPECT_FLOAT_EQ(summary->views[0].time.avg_ms, 3.0F);
  EXPECT_FLOAT_EQ(summary->idle.avg_ms, 6.0F);
  EXPECT_NE(
    FindGroup(summary->frame_work, GpuWorkBucket::kPostProcess), nullptr);
}

//! With command lists timed, idle is the frame span with no list running;
//! list time no stage scope covers is a `kOther` item named after the list,
//! the view's when the list holds the view.
NOLINT_TEST_F(GpuWorkSummarizerTest, CommandListsSplitIdleFromUnscopedWork)
{
  constexpr auto kList = oxygen::graphics::kCommandListScopeLabel;
  auto builder = FrameBuilder { 1U };
  const auto frame = builder.Frame(10.0F);
  const auto view_list = builder.Add(kList, 5.0F, frame, 0U, "Vortex View");
  const auto view
    = builder.Add(gpu_work::kViewLabel, 4.5F, view_list, 1U, "Main");
  builder.Add(gpu_work::kBasePassLabel, 2.0F, view);
  builder.Add(kList, 1.0F, frame, 0U, "Uploads");

  const auto summary = Publish(builder.Get());

  ASSERT_NE(summary, nullptr);
  EXPECT_FLOAT_EQ(summary->idle.avg_ms, 4.0F);
  ASSERT_EQ(summary->views.size(), 1U);
  const auto& main = summary->views[0];
  EXPECT_NE(FindGroup(main.groups, GpuWorkBucket::kBasePass), nullptr);
  const auto* view_other = FindGroup(main.groups, GpuWorkBucket::kOther);
  ASSERT_NE(view_other, nullptr);
  const auto* view_unscoped = FindItem(*view_other, "list:Vortex View");
  ASSERT_NE(view_unscoped, nullptr);
  EXPECT_FLOAT_EQ(view_unscoped->time.avg_ms, 3.0F);
  EXPECT_EQ(view_unscoped->name, "Vortex View");
  const auto* frame_other
    = FindGroup(summary->frame_work, GpuWorkBucket::kOther);
  ASSERT_NE(frame_other, nullptr);
  const auto* uploads = FindItem(*frame_other, "list:Uploads");
  ASSERT_NE(uploads, nullptr);
  EXPECT_FLOAT_EQ(uploads->time.avg_ms, 1.0F);
}

//! A telemetry scope the stage table does not list is shown under `kOther`
//! by its label instead of vanishing into the idle time.
NOLINT_TEST_F(GpuWorkSummarizerTest, UnknownScopeIsOtherByLabel)
{
  auto builder = FrameBuilder { 1U };
  const auto frame = builder.Frame(4.0F);
  const auto view = builder.View(frame, 1U, "Main", 2.0F);
  builder.Add("Vortex.Mystery", 1.0F, view);

  const auto summary = Publish(builder.Get());

  ASSERT_NE(summary, nullptr);
  ASSERT_EQ(summary->views.size(), 1U);
  const auto* other
    = FindGroup(summary->views[0].groups, GpuWorkBucket::kOther);
  ASSERT_NE(other, nullptr);
  const auto* item = FindItem(*other, "Vortex.Mystery");
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->name, "Vortex.Mystery");
}

//! A group keeps its largest items and folds the rest into one item whose
//! maximum is the largest per-frame sum of the folded items.
NOLINT_TEST_F(GpuWorkSummarizerTest, FoldsSmallItemsIntoOther)
{
  constexpr auto kLabels = std::array<std::string_view, 6> {
    "Vortex.A",
    "Vortex.B",
    "Vortex.C",
    "Vortex.D",
    "Vortex.E",
    "Vortex.F",
  };
  auto builder = FrameBuilder { 1U };
  const auto frame = builder.Frame(30.0F);
  const auto view = builder.View(frame, 1U, "Main", 25.0F);
  auto ms = 6.0F;
  for (const auto label : kLabels) {
    builder.Add(label, ms, view);
    ms -= 1.0F;
  }

  const auto summary = Publish(builder.Get());

  ASSERT_NE(summary, nullptr);
  const auto* other
    = FindGroup(summary->views[0].groups, GpuWorkBucket::kOther);
  ASSERT_NE(other, nullptr);
  ASSERT_EQ(other->items.size(), GpuWorkSummary::kMaxItemsPerGroup);
  EXPECT_EQ(other->items[0].id, "Vortex.A");
  EXPECT_EQ(other->items[1].id, "Vortex.B");
  EXPECT_EQ(other->items[2].id, "Vortex.C");
  EXPECT_EQ(other->items[3].id, "other");
  EXPECT_FLOAT_EQ(other->items[3].time.avg_ms, 3.0F + 2.0F + 1.0F);
  EXPECT_FLOAT_EQ(other->items[3].time.max_ms, 6.0F);
  EXPECT_FLOAT_EQ(other->time.avg_ms, 21.0F);
}

//! Averages count an absent item as zero; maxima are single frames.
NOLINT_TEST_F(GpuWorkSummarizerTest, AbsentItemCountsAsZero)
{
  auto first = FrameBuilder { 1U };
  auto view = first.View(first.Frame(5.0F), 1U, "Main", 4.0F);
  first.Add(gpu_work::kFogLabel, 2.0F, view);
  first.Add(gpu_work::kSkyLabel, 1.0F, view);
  Consume(first.Get());
  auto second = FrameBuilder { 2U };
  view = second.View(second.Frame(3.0F), 1U, "Main", 2.0F);
  second.Add(gpu_work::kSkyLabel, 1.0F, view);

  const auto summary = Publish(second.Get());

  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->valid_frames, 2U);
  EXPECT_FLOAT_EQ(summary->frame_gpu.avg_ms, 4.0F);
  EXPECT_FLOAT_EQ(summary->frame_gpu.max_ms, 5.0F);
  const auto* environment
    = FindGroup(summary->views[0].groups, GpuWorkBucket::kEnvironment);
  ASSERT_NE(environment, nullptr);
  const auto* fog = FindItem(*environment, "environment.fog");
  ASSERT_NE(fog, nullptr);
  EXPECT_FLOAT_EQ(fog->time.avg_ms, 1.0F);
  EXPECT_FLOAT_EQ(fog->time.max_ms, 2.0F);
  EXPECT_FLOAT_EQ(environment->time.avg_ms, 2.0F);
  EXPECT_FLOAT_EQ(environment->time.max_ms, 3.0F);
}

//! Overflowed, disabled or incomplete frames are counted and excluded.
NOLINT_TEST_F(GpuWorkSummarizerTest, ExcludesInvalidFrames)
{
  auto incomplete = FrameBuilder { 1U };
  incomplete.Frame(100.0F);
  incomplete.Mutable().scopes.front().valid = false;
  Consume(incomplete.Get());
  auto disabled = FrameBuilder { 2U };
  disabled.Frame(100.0F);
  disabled.Mutable().profiling_enabled = false;
  Consume(disabled.Get());
  auto valid = FrameBuilder { 3U };
  valid.Frame(2.0F);

  const auto summary = Publish(valid.Get());

  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->valid_frames, 1U);
  EXPECT_EQ(summary->invalid_frames, 3U);
  EXPECT_FLOAT_EQ(summary->frame_gpu.avg_ms, 2.0F);
}

//! A window with no valid frame still publishes, with its counts.
NOLINT_TEST_F(GpuWorkSummarizerTest, PublishesEmptyWindowWithCounts)
{
  const auto summary = Publish(Invalid(1U));

  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(summary->valid_frames, 0U);
  EXPECT_EQ(summary->invalid_frames, 2U);
  EXPECT_TRUE(summary->views.empty());
}

//! Nothing publishes before the window closes; each window increments the
//! revision and leaves earlier snapshots unchanged.
NOLINT_TEST_F(GpuWorkSummarizerTest, PublishesOncePerWindow)
{
  auto builder = FrameBuilder { 1U };
  builder.Frame(1.0F);
  Consume(builder.Get());
  now_ += GpuWorkSummarizer::kWindow / 2;
  Consume(builder.Get());
  EXPECT_EQ(summarizer_.Latest(), nullptr);

  now_ += GpuWorkSummarizer::kWindow / 2;
  Consume(builder.Get());
  const auto first = summarizer_.Latest();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->valid_frames, 3U);
  EXPECT_FLOAT_EQ(first->window_ms, 500.0F);

  auto later = FrameBuilder { 5U };
  later.Frame(9.0F);
  const auto second = Publish(later.Get());
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->revision, first->revision + 1U);
  EXPECT_EQ(second->last_frame.get(), 6U);
  EXPECT_FLOAT_EQ(first->frame_gpu.avg_ms, 1.0F);
}

//! The sparkline keeps the last valid frame spans, newest last.
NOLINT_TEST_F(GpuWorkSummarizerTest, KeepsBoundedHistory)
{
  const auto count = GpuWorkSummarizer::kHistoryLength + 5U;
  for (std::size_t i = 0; i < count; ++i) {
    auto builder = FrameBuilder { i + 1U };
    builder.Frame(static_cast<float>(i));
    Consume(builder.Get());
  }
  now_ += GpuWorkSummarizer::kWindow;
  auto last = FrameBuilder { count + 1U };
  last.Frame(1000.0F);
  Consume(last.Get());

  const auto summary = summarizer_.Latest();
  ASSERT_NE(summary, nullptr);
  ASSERT_EQ(
    summary->frame_gpu_history_ms.size(), GpuWorkSummarizer::kHistoryLength);
  EXPECT_FLOAT_EQ(summary->frame_gpu_history_ms.back(), 1000.0F);
  EXPECT_FLOAT_EQ(summary->frame_gpu_history_ms.front(), 6.0F);
}

//! Frames before activation, or while inactive, are ignored; deactivation
//! drops the published snapshot.
NOLINT_TEST_F(GpuWorkSummarizerTest, IgnoresFramesOutsideActivation)
{
  summarizer_.Activate(10U);
  auto early = FrameBuilder { 9U };
  early.Frame(1.0F);
  Consume(early.Get());
  now_ += GpuWorkSummarizer::kWindow;
  Consume(early.Get());
  EXPECT_EQ(summarizer_.Latest(), nullptr);

  auto frame = FrameBuilder { 10U };
  frame.Frame(1.0F);
  ASSERT_NE(Publish(frame.Get()), nullptr);

  summarizer_.Deactivate();
  EXPECT_EQ(summarizer_.Latest(), nullptr);
  Consume(frame.Get());
  now_ += GpuWorkSummarizer::kWindow;
  Consume(frame.Get());
  EXPECT_EQ(summarizer_.Latest(), nullptr);
}

//! Every label is listed once and found by its hash; the catalog covers
//! every bucket in display order.
NOLINT_TEST(GpuWorkStageTableTest, LabelsAreUniqueAndFound)
{
  auto labels = std::set<std::string_view> {};
  for (const auto& entry : gpu_work::EnumerateStageEntries()) {
    EXPECT_TRUE(labels.insert(entry.label).second) << entry.label;
    EXPECT_EQ(
      gpu_work::FindStageEntry(gpu_work::HashScopeLabel(entry.label)), &entry);
    EXPECT_NE(entry.bucket, GpuWorkBucket::kOther) << entry.label;
  }
  EXPECT_EQ(
    gpu_work::FindStageEntry(gpu_work::HashScopeLabel(gpu_work::kViewLabel)),
    nullptr);

  const auto buckets = oxygen::vortex::EnumerateGpuWorkBuckets();
  for (const auto [index, info] : std::views::enumerate(buckets)) {
    EXPECT_EQ(static_cast<std::ptrdiff_t>(info.bucket), index);
    EXPECT_FALSE(info.id.empty());
  }
  EXPECT_EQ(buckets.back().bucket, GpuWorkBucket::kOther);
}

} // namespace
