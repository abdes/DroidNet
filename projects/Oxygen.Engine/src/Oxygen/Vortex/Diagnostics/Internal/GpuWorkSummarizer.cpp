//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <ratio>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkStageTable.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkSummarizer.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>

namespace oxygen::vortex::internal {

namespace {
  constexpr auto kNoParent = std::uint32_t { 0xFFFFFFFFU };
  constexpr auto kFrameHash = gpu_work::HashScopeLabel(gpu_work::kFrameLabel);
  constexpr auto kViewHash = gpu_work::HashScopeLabel(gpu_work::kViewLabel);
  constexpr auto kListHash
    = gpu_work::HashScopeLabel(graphics::kCommandListScopeLabel);

  auto ToStat(const std::span<const float> samples) -> GpuWorkStat
  {
    if (samples.empty()) {
      return {};
    }
    auto sum = 0.0;
    auto max = 0.0F;
    for (const auto ms : samples) {
      sum += ms;
      max = std::max(max, ms);
    }
    return {
      .avg_ms = static_cast<float>(sum / static_cast<double>(samples.size())),
      .max_ms = max,
    };
  }

  struct ItemSeries {
    std::string name;
    std::vector<float> ms;
  };

  //! Groups in catalog order over `frames`; adds each frame's group total
  //! to `frame_totals`.
  template <typename GroupSamples>
  auto BuildGroups(const std::span<const GroupSamples* const> frames,
    std::vector<float>& frame_totals) -> std::vector<GpuWorkGroup>
  {
    const auto count = frames.size();
    auto buckets
      = std::map<GpuWorkBucket, std::map<std::string, ItemSeries>> {};
    for (const auto [f, samples] : std::views::enumerate(frames)) {
      for (const auto& [key, sample] : *samples) {
        auto& series = buckets[key.first][key.second];
        if (series.ms.empty()) {
          series.ms.assign(count, 0.0F);
        }
        series.name = sample.name;
        series.ms.at(static_cast<std::size_t>(f)) += sample.ms;
      }
    }

    frame_totals.assign(count, 0.0F);
    auto groups = std::vector<GpuWorkGroup> {};
    groups.reserve(buckets.size());
    for (auto& [bucket, items] : buckets) {
      auto group_frames = std::vector<float>(count, 0.0F);
      auto ranked = std::vector<std::pair<GpuWorkItem, std::vector<float>>> {};
      ranked.reserve(items.size());
      for (auto& [id, series] : items) {
        for (std::size_t f = 0; f < count; ++f) {
          group_frames.at(f) += series.ms.at(f);
        }
        auto item = GpuWorkItem {
          .id = id,
          .name = std::move(series.name),
          .time = ToStat(series.ms),
        };
        ranked.emplace_back(std::move(item), std::move(series.ms));
      }
      std::ranges::stable_sort(
        ranked, [](const auto& lhs, const auto& rhs) -> bool {
          return lhs.first.time.avg_ms > rhs.first.time.avg_ms;
        });
      constexpr auto kKept = GpuWorkSummary::kMaxItemsPerGroup;
      if (ranked.size() > kKept) {
        auto folded = std::vector<float>(count, 0.0F);
        for (auto it = ranked.begin() + (kKept - 1U); it != ranked.end();
          ++it) {
          for (std::size_t f = 0; f < count; ++f) {
            folded.at(f) += it->second.at(f);
          }
        }
        ranked.resize(kKept - 1U);
        auto other = GpuWorkItem {
          .id = "other",
          .name = "Other",
          .time = ToStat(folded),
        };
        ranked.emplace_back(std::move(other), std::move(folded));
      }

      auto group = GpuWorkGroup {
        .bucket = bucket,
        .time = ToStat(group_frames),
        .items = {},
      };
      group.items.reserve(ranked.size());
      for (auto& [item, series] : ranked) {
        group.items.push_back(std::move(item));
      }
      for (std::size_t f = 0; f < count; ++f) {
        frame_totals.at(f) += group_frames.at(f);
      }
      groups.push_back(std::move(group));
    }
    return groups;
  }
} // namespace

GpuWorkSummarizer::GpuWorkSummarizer(NowFn now)
  : now_(std::move(now))
{
}

GpuWorkSummarizer::~GpuWorkSummarizer() = default;

auto GpuWorkSummarizer::Activate(const std::uint64_t first_frame) -> void
{
  std::scoped_lock lock(mutex_);
  active_ = true;
  first_frame_ = first_frame;
  window_start_.reset();
  window_.clear();
  invalid_frames_ = 0U;
  history_.clear();
  latest_.reset();
}

auto GpuWorkSummarizer::Deactivate() -> void
{
  std::scoped_lock lock(mutex_);
  active_ = false;
  window_start_.reset();
  window_.clear();
  invalid_frames_ = 0U;
  history_.clear();
  latest_.reset();
}

auto GpuWorkSummarizer::ConsumeFrame(const GpuTimelineFrame& frame) -> bool
{
  std::scoped_lock lock(mutex_);
  if (!active_ || frame.frame_sequence < first_frame_) {
    return true;
  }
  last_frame_ = std::max(last_frame_, frame.frame_sequence);
  if (auto sample = Sample(frame)) {
    history_.push_back(sample->frame_gpu_ms);
    if (history_.size() > kHistoryLength) {
      history_.erase(history_.begin());
    }
    window_.push_back(std::move(*sample));
  } else {
    ++invalid_frames_;
  }
  const auto now = now_();
  if (!window_start_) {
    window_start_ = now;
  }
  if (now - *window_start_ >= kWindow) {
    Publish(now);
  }
  return true;
}

auto GpuWorkSummarizer::Latest() const -> std::shared_ptr<const GpuWorkSummary>
{
  std::scoped_lock lock(mutex_);
  return latest_;
}

auto GpuWorkSummarizer::Sample(const GpuTimelineFrame& frame)
  -> std::optional<FrameSample>
{
  if (!frame.profiling_enabled || frame.overflowed || frame.scopes.empty()
    || std::ranges::any_of(
      frame.scopes, [](const auto& scope) -> bool { return !scope.valid; })) {
    return std::nullopt;
  }

  const auto parent_of = [&](const std::uint32_t id) -> std::uint32_t {
    return id < frame.scopes.size() ? frame.scopes.at(id).parent_scope_id
                                    : kNoParent;
  };
  const auto hash_of = [&](const std::uint32_t id) -> std::uint64_t {
    return id < frame.scopes.size() ? frame.scopes.at(id).name_hash : 0U;
  };
  // A command list span only contains scopes; skip it to find the scope's
  // logical parent.
  const auto skip_lists = [&](std::uint32_t id) -> std::uint32_t {
    while (id != kNoParent && hash_of(id) == kListHash) {
      id = parent_of(id);
    }
    return id;
  };
  const auto list_of = [&](std::uint32_t id) -> std::uint32_t {
    while (id != kNoParent && id < frame.scopes.size()) {
      if (hash_of(id) == kListHash) {
        return id;
      }
      id = parent_of(id);
    }
    return kNoParent;
  };
  const auto view_of
    = [&](const GpuTimelineScope& scope) -> const GpuTimelineScope* {
    if (scope.view_scope_id < frame.scopes.size()
      && hash_of(scope.view_scope_id) == kViewHash) {
      return &frame.scopes.at(scope.view_scope_id);
    }
    return nullptr;
  };

  auto sample = FrameSample {};
  auto span_begin = frame.scopes.front().start_ms;
  auto span_end = frame.scopes.front().end_ms;
  auto has_frame_span = false;
  // Counted time per command list, and counted time outside every list.
  auto list_covered = std::map<std::uint32_t, float> {};
  auto lists_ms = 0.0F;
  auto unlisted_ms = 0.0F;
  const auto add = [&](const GpuTimelineScope* view, ItemKey key,
                     std::string name, const float ms) -> void {
    // Background work is frame work even when recorded while a view records.
    auto* items = &sample.frame_work;
    if (view != nullptr && key.first != GpuWorkBucket::kBackground) {
      auto& view_sample = sample.views[view->instance_id];
      view_sample.name = view->instance_name;
      items = &view_sample.items;
    }
    auto& item = (*items)[key];
    item.name = std::move(name);
    item.ms += ms;
  };
  for (const auto& scope : frame.scopes) {
    span_begin = std::min(span_begin, scope.start_ms);
    span_end = std::max(span_end, scope.end_ms);
    if (scope.name_hash == kFrameHash) {
      sample.frame_gpu_ms = scope.duration_ms;
      has_frame_span = true;
      continue;
    }
    if (scope.name_hash == kListHash) {
      lists_ms += scope.duration_ms;
      list_covered.try_emplace(scope.scope_id, 0.0F);
      continue;
    }
    if (scope.name_hash == kViewHash) {
      sample.views[scope.instance_id].name = scope.instance_name;
      continue;
    }

    // Only the outermost scope under the frame or view span counts; scopes a
    // view records on its own command lists have no view parent but belong
    // to it.
    const auto logical_parent = skip_lists(scope.parent_scope_id);
    if (logical_parent != kNoParent && hash_of(logical_parent) != kViewHash
      && hash_of(logical_parent) != kFrameHash) {
      continue;
    }
    if (const auto list = list_of(scope.parent_scope_id); list != kNoParent) {
      list_covered[list] += scope.duration_ms;
    } else {
      unlisted_ms += scope.duration_ms;
    }

    auto key = ItemKey { GpuWorkBucket::kOther, scope.display_name };
    auto name = scope.display_name;
    if (const auto* entry = gpu_work::FindStageEntry(scope.name_hash)) {
      key = { entry->bucket, std::string(entry->item_id) };
      name = std::string(entry->display_name);
    }
    add(view_of(scope), std::move(key), std::move(name), scope.duration_ms);
  }

  // A list's time outside every counted scope is its own unscoped work,
  // named by the list. A view's list belongs to the view it contains.
  for (const auto& [id, covered] : list_covered) {
    const auto& list = frame.scopes.at(id);
    const auto ms = list.duration_ms - covered;
    if (ms <= 0.0F) {
      continue;
    }
    const auto* view = view_of(list);
    for (const auto& scope : frame.scopes) {
      if (view == nullptr && scope.name_hash == kViewHash
        && scope.parent_scope_id == id) {
        view = &scope;
      }
    }
    add(view, ItemKey { GpuWorkBucket::kOther, "list:" + list.instance_name },
      list.instance_name, ms);
  }
  if (!has_frame_span) {
    sample.frame_gpu_ms = span_end - span_begin;
  }
  sample.idle_ms = std::max(0.0F, sample.frame_gpu_ms - lists_ms - unlisted_ms);
  return sample;
}

auto GpuWorkSummarizer::Publish(const Clock::time_point now) -> void
{
  auto summary = std::make_shared<GpuWorkSummary>();
  summary->revision = ++revision_;
  summary->last_frame = frame::SequenceNumber { last_frame_ };
  summary->window_ms
    = std::chrono::duration<float, std::milli>(now - *window_start_).count();
  summary->valid_frames = static_cast<std::uint32_t>(window_.size());
  summary->invalid_frames = invalid_frames_;
  summary->frame_gpu_history_ms = history_;

  const auto count = window_.size();
  auto frame_gpu = std::vector<float>(count, 0.0F);
  auto idle = std::vector<float>(count, 0.0F);
  for (std::size_t f = 0; f < count; ++f) {
    frame_gpu.at(f) = window_.at(f).frame_gpu_ms;
    idle.at(f) = window_.at(f).idle_ms;
  }
  summary->frame_gpu = ToStat(frame_gpu);

  auto view_ids = std::vector<std::uint64_t> {};
  for (const auto& sample : window_) {
    for (const auto& [id, view] : sample.views) {
      if (std::ranges::find(view_ids, id) == view_ids.end()) {
        view_ids.push_back(id);
      }
    }
  }
  std::ranges::sort(view_ids);
  for (const auto id : view_ids) {
    // A view's averages count only the frames it rendered in.
    auto frames = std::vector<const GroupSamples*> {};
    auto view = GpuWorkView { .view_id = id };
    for (std::size_t f = 0; f < count; ++f) {
      const auto& views = window_.at(f).views;
      const auto it = views.find(id);
      if (it != views.end()) {
        frames.push_back(&it->second.items);
        view.name = it->second.name;
      }
    }
    auto totals = std::vector<float> {};
    view.groups = BuildGroups<GroupSamples>(frames, totals);
    view.time = ToStat(totals);
    summary->views.push_back(std::move(view));
  }

  auto frames = std::vector<const GroupSamples*> {};
  frames.reserve(count);
  for (const auto& sample : window_) {
    frames.push_back(&sample.frame_work);
  }
  auto totals = std::vector<float> {};
  summary->frame_work = BuildGroups<GroupSamples>(frames, totals);
  summary->frame_work_time = ToStat(totals);
  summary->idle = ToStat(idle);

  latest_ = std::move(summary);
  window_start_.reset();
  window_.clear();
  invalid_frames_ = 0U;
}

} // namespace oxygen::vortex::internal
