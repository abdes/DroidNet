//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <Oxygen/Content/Tools/ImportTool/UI/Screens/BatchImportScreen.h>

namespace oxygen::content::import::tool {
namespace {

  using ftxui::color;
  using ftxui::Color;
  using ftxui::Element;
  using ftxui::Event;
  using ftxui::hbox;
  using ftxui::size;
  using ftxui::text;
  using ftxui::vbox;
  using ftxui::window;

  constexpr std::string_view kFullBlock = "\xE2\x96\x88"; // █
  constexpr std::string_view kEmptyBlock = "\xE2\x96\x91"; // ░

  auto RepeatGlyph(std::string_view glyph, int count) -> std::string
  {
    if (count <= 0) {
      return "";
    }
    std::string out;
    out.reserve(static_cast<size_t>(count) * glyph.size());
    for (int i = 0; i < count; ++i) {
      out.append(glyph);
    }
    return out;
  }

  auto FormatElapsed(std::chrono::seconds elapsed) -> std::string
  {
    const auto total_seconds = elapsed.count();
    const auto minutes = total_seconds / 60;
    const auto seconds = total_seconds % 60;
    return std::format("{:02}:{:02}", minutes, seconds);
  }

  auto BuildMiniBar(float progress, int width) -> std::string
  {
    if (width <= 0) {
      return "";
    }
    const int filled
      = std::clamp(static_cast<int>(progress * width + 0.5f), 0, width);
    std::string bar;
    bar.reserve(static_cast<size_t>(width) * kFullBlock.size());
    for (int i = 0; i < width; ++i) {
      bar.append(i < filled ? kFullBlock : kEmptyBlock);
    }
    return bar;
  }

  auto QueueGlyph(float queue_load) -> std::string_view
  {
    constexpr std::array<std::string_view, 11> kGlyphs {
      "  ", // 0%
      "\xE2\x96\x8F ", // ▏
      "\xE2\x96\x8E ", // ▎
      "\xE2\x96\x8D ", // ▍
      "\xE2\x96\x8C ", // ▌
      "\xE2\x96\x88 ", // █
      "\xE2\x96\x88\xE2\x96\x8F", // █▏
      "\xE2\x96\x88\xE2\x96\x8E", // █▎
      "\xE2\x96\x88\xE2\x96\x8D", // █▍
      "\xE2\x96\x88\xE2\x96\x8C", // █▌
      "\xE2\x96\x88\xE2\x96\x88", // ██ 100%
    };
    const float clamped = std::clamp(queue_load, 0.0f, 1.0f);
    const int bucket
      = std::clamp(static_cast<int>(clamped * 10.0f + 1e-4f), 0, 10);
    return kGlyphs[static_cast<size_t>(bucket)];
  }

  auto ShortName(std::string_view path) -> std::string
  {
    if (path.empty()) {
      return "";
    }
    std::filesystem::path fs_path(path);
    if (fs_path.has_filename()) {
      return fs_path.filename().string();
    }
    return std::string(path);
  }

  auto PadRight(std::string_view value, int width) -> std::string
  {
    if (width <= 0) {
      return std::string(value);
    }
    std::string padded(value);
    if (static_cast<int>(padded.size()) < width) {
      padded.append(static_cast<size_t>(width) - padded.size(), ' ');
    }
    return padded;
  }

  auto AllocateSegments(const std::array<size_t, 4>& counts, int width)
    -> std::array<int, 4>
  {
    std::array<int, 4> lengths { 0, 0, 0, 0 };
    if (width <= 0) {
      return lengths;
    }

    const size_t total
      = std::accumulate(counts.begin(), counts.end(), size_t { 0U });
    if (total == 0U) {
      return lengths;
    }

    std::array<double, 4> fractions { 0.0, 0.0, 0.0, 0.0 };
    int used = 0;
    for (size_t index = 0; index < counts.size(); ++index) {
      const double exact = static_cast<double>(counts[index])
        * static_cast<double>(width) / static_cast<double>(total);
      const int base = static_cast<int>(exact);
      lengths[index] = base;
      fractions[index] = exact - static_cast<double>(base);
      used += base;
    }

    int remaining = width - used;
    std::vector<std::pair<size_t, double>> order;
    order.reserve(counts.size());
    for (size_t index = 0; index < counts.size(); ++index) {
      order.emplace_back(index, fractions[index]);
    }

    std::sort(order.begin(), order.end(), [](const auto& lhs, const auto& rhs) {
      if (lhs.second == rhs.second) {
        return lhs.first < rhs.first;
      }
      return lhs.second > rhs.second;
    });

    for (int i = 0; i < remaining; ++i) {
      const size_t index = order[static_cast<size_t>(i) % order.size()].first;
      lengths[index] += 1;
    }

    return lengths;
  }

  auto StatusColor(std::string_view status) -> Color
  {
    if (status == "Failed") {
      return Color::Red;
    }
    if (status == "Queued") {
      return Color::GrayLight;
    }
    if (status == "Completed") {
      return Color::Green;
    }
    return Color::White;
  }

  auto LoadColor(float queue_load) -> Color
  {
    const float clamped = std::clamp(queue_load, 0.0f, 1.0f);
    const int percent
      = std::clamp(static_cast<int>(clamped * 100.0f + 0.5f), 0, 100);
    if (percent >= 100) {
      return Color::Red;
    }
    if (percent >= 80) {
      return Color::Yellow;
    }
    return Color::GrayLight;
  }

  auto BuildSegmentedProgressBar(const BatchViewModel& state) -> Element
  {
    const int terminal_width = std::max(0, ftxui::Terminal::Size().dimx);
    const int bar_width = std::max(1, terminal_width - 2);
    const size_t failures = state.failures;
    const size_t completed_success
      = state.completed > failures ? state.completed - failures : 0U;
    const std::array<size_t, 4> counts {
      completed_success,
      state.in_flight,
      state.remaining,
      failures,
    };
    const size_t total = counts[0] + counts[1] + counts[2] + counts[3];
    std::array<int, 4> lengths { 0, 0, 0, 0 };
    if (total == 0U) {
      lengths[2] = bar_width;
    } else {
      lengths = AllocateSegments(counts, bar_width);
    }

    return hbox({
      text(RepeatGlyph(kFullBlock, lengths[0])) | color(Color::Green),
      text(RepeatGlyph(kFullBlock, lengths[1])) | color(Color::Yellow),
      text(total == 0U ? RepeatGlyph(kEmptyBlock, lengths[2])
                       : RepeatGlyph(kFullBlock, lengths[2]))
        | color(Color::GrayLight),
      text(RepeatGlyph(kFullBlock, lengths[3])) | color(Color::Red),
    });
  }

  auto BuildHeader(const BatchViewModel& state) -> Element
  {
    const bool completed = state.completed_run;
    if (completed) {
      const auto failures = state.failures;
      const auto total = state.total;
      const std::string header = std::format(
        "Completed: {} total, {} failed (press any key)", total, failures);
      const auto header_color = failures > 0U ? Color::Yellow : Color::Green;
      return vbox({
        hbox({
          text(header) | color(header_color),
        }),
        BuildSegmentedProgressBar(state),
      });
    }

    const std::string batch = std::format(
      "Batch: {} ({} jobs)", ShortName(state.manifest_path), state.total);
    const std::string elapsed
      = std::format("Elapsed: {}", FormatElapsed(state.elapsed));
    return vbox({
      hbox({
        text(batch) | color(Color::GrayLight),
        ftxui::filler(),
        text(elapsed) | color(Color::GrayLight),
      }),
      BuildSegmentedProgressBar(state),
    });
  }

  auto BuildActiveJobs(const BatchViewModel& state) -> Element
  {
    std::vector<Element> rows;
    rows.push_back(
      text(
        "#  ID       Source                 Phase       Items   Job Progress")
      | color(Color::GrayLight));

    for (size_t index = 0; index < state.active_jobs.size(); ++index) {
      const auto& job = state.active_jobs[index];
      const int percent = static_cast<int>(job.progress * 100.0f + 0.5f);
      const std::string bar = BuildMiniBar(job.progress, 18);
      const std::string source = ShortName(job.source);
      const size_t row_index = index + 1U;

      const std::string primary = std::format(
        "{:>2}  {:<6} {:<20} {:<10} {:>3}/{:<3} [{}] {:>3}%", row_index, job.id,
        source, job.status, job.items_completed, job.items_total, bar, percent);
      rows.push_back(text(primary) | color(StatusColor(job.status)));

      std::string item_line = "    Item: ";
      if (!job.item_name.empty()) {
        item_line += job.item_name;
      } else {
        item_line += "(none)";
      }
      if (!job.item_kind.empty()) {
        item_line += " (" + job.item_kind + ")";
      }
      if (job.items_total > 0U) {
        item_line
          += std::format(" item {}/{}", job.items_completed, job.items_total);
      }
      if (!job.item_event.empty()) {
        item_line += " " + job.item_event;
      }
      rows.push_back(text(item_line) | color(Color::GrayLight));
    }

    return vbox(rows);
  }

  struct UtilizationFormat {
    std::string label;
    float ratio = 0.0f;
    std::string input_glyph;
    std::string output_glyph;
    std::string counts;
    Color input_color = Color::GrayLight;
    Color output_color = Color::GrayLight;
    bool visible = true;
  };

  auto FormatUtilization(
    const std::unordered_map<std::string_view, WorkerUtilizationView>& table,
    std::string_view display_kind) -> UtilizationFormat
  {
    if (display_kind.empty()) {
      return { "", 0.0f, "", "", "", Color::GrayLight, Color::GrayLight,
        false };
    }
    const auto lookup_kind
      = (display_kind == "Mesh") ? "MeshBuild" : display_kind;
    const auto it = table.find(lookup_kind);
    WorkerUtilizationView entry {};
    const auto label_kind = std::string(display_kind);
    entry.kind = label_kind;
    if (it != table.end()) {
      entry = it->second;
      entry.kind = label_kind;
    }
    const float ratio = entry.total > 0U
      ? static_cast<float>(entry.active) / static_cast<float>(entry.total)
      : 0.0f;
    const std::string_view input_glyph = QueueGlyph(entry.input_queue_load);
    const std::string_view output_glyph = QueueGlyph(entry.output_queue_load);
    const auto active = static_cast<int>(std::min(entry.active, 99U));
    const auto total = static_cast<int>(std::min(entry.total, 99U));
    return {
      std::string(entry.kind),
      ratio,
      std::string(input_glyph),
      std::string(output_glyph),
      std::format(" {:>2}/{:<2}", active, total),
      LoadColor(entry.input_queue_load),
      LoadColor(entry.output_queue_load),
      true,
    };
  }

  auto BuildUtilizationCell(const UtilizationFormat& data, int label_width)
    -> Element
  {
    if (!data.visible) {
      return text("");
    }
    constexpr int kBarWidth = 10;
    const std::string label = PadRight(data.label, label_width);
    const std::string bar = BuildMiniBar(data.ratio, kBarWidth);
    return hbox({
      text(label),
      text(" "),
      text(data.input_glyph) | color(data.input_color),
      text(" "),
      text(bar) | color(Color::GrayLight),
      text(" "),
      text(data.output_glyph) | color(data.output_color),
      text(data.counts),
    });
  }

  auto BuildUtilization(const BatchViewModel& state) -> Element
  {
    const std::array<std::string_view, 4> left_order {
      "Material",
      "Geometry",
      "Audio",
      "Scene",
    };
    const std::array<std::string_view, 4> right_order {
      "Buffer",
      "Mesh",
      "Texture",
      "",
    };

    std::unordered_map<std::string_view, WorkerUtilizationView> table;
    for (const auto& entry : state.worker_utilization) {
      table[entry.kind] = entry;
    }

    const std::array<std::string_view, 7> label_names {
      "Material",
      "Geometry",
      "Audio",
      "Scene",
      "Buffer",
      "Mesh",
      "Texture",
    };
    int label_width = 0;
    for (const auto& name : label_names) {
      label_width = std::max(label_width, static_cast<int>(name.size()));
    }
    const int cell_width = label_width + 22;

    std::vector<Element> rows;
    for (size_t row = 0; row < left_order.size(); ++row) {
      const auto left = FormatUtilization(table, left_order[row]);
      const auto right = FormatUtilization(table, right_order[row]);
      rows.push_back(hbox({
        BuildUtilizationCell(left, label_width)
          | size(ftxui::WIDTH, ftxui::EQUAL, cell_width),
        text(" "),
        BuildUtilizationCell(right, label_width),
      }));
    }

    return vbox(rows);
  }

  auto BuildLogs(const BatchViewModel& state) -> Element
  {
    std::vector<Element> rows;
    constexpr size_t kMaxLogs = 200U;
    const size_t total = state.recent_logs.size();
    const size_t start = total > kMaxLogs ? total - kMaxLogs : 0U;
    for (size_t index = start; index < total; ++index) {
      rows.push_back(text(state.recent_logs[index]));
    }
    if (rows.empty()) {
      rows.push_back(text("(no recent events)") | color(Color::GrayLight));
    }
    return vbox(rows) | ftxui::yframe;
  }

} // namespace

BatchImportScreen::BatchImportScreen() = default;

void BatchImportScreen::SetDataProvider(DataProvider provider)
{
  provider_ = std::move(provider);
}

void BatchImportScreen::SetOnCompleted(CompletionCallback callback)
{
  on_completed_ = std::move(callback);
}

auto BatchImportScreen::GetStateSnapshot() const -> BatchViewModel
{
  std::scoped_lock lock(state_mutex_);
  return state_;
}

void BatchImportScreen::UpdateState(BatchViewModel state)
{
  std::scoped_lock lock(state_mutex_);
  state_ = std::move(state);
  if (state_.completed_run) {
    completed_.store(true);
  }
}

void BatchImportScreen::Run()
{
  if (!provider_) {
    return;
  }

  completed_.store(false);
  completed_signaled_.store(false);
  UpdateState(provider_());

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  std::atomic<bool> running { true };

  auto renderer = ftxui::Renderer([&]() -> Element {
    const auto state = GetStateSnapshot();
    return vbox({
      window(text("Oxygen Import Tool"), BuildHeader(state)),
      window(text("Active Jobs"),
        BuildActiveJobs(state) | ftxui::yframe
          | size(ftxui::HEIGHT, ftxui::GREATER_THAN, 8))
        | ftxui::flex,
      window(text("Worker Utilization"), BuildUtilization(state)),
      window(text("Recent Events"),
        BuildLogs(state) | size(ftxui::HEIGHT, ftxui::LESS_THAN, 8))
        | ftxui::flex,
    });
  });

  auto root = ftxui::CatchEvent(renderer, [&](const Event& event) {
    if (event == Event::Custom) {
      return true;
    }
    if (completed_.load() && !event.is_mouse()) {
      if (!completed_signaled_.exchange(true)) {
        if (on_completed_) {
          on_completed_();
        }
        screen.Exit();
      }
      return true;
    }
    return false;
  });

  std::thread updater([&]() {
    while (running.load()) {
      if (provider_) {
        UpdateState(provider_());
      }
      screen.PostEvent(Event::Custom);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  screen.Loop(root);
  running.store(false);
  if (updater.joinable()) {
    updater.join();
  }
}

} // namespace oxygen::content::import::tool
