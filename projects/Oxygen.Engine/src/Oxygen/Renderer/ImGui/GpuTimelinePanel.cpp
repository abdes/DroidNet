//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <optional>

#include <imgui.h>

#include <Oxygen/Renderer/ImGui/GpuTimelinePanel.h>
#include <Oxygen/Renderer/Internal/GpuTimelineProfiler.h>

namespace oxygen::engine::imgui {

namespace {

  auto DrawTimelineBar(float start_ms, float duration_ms, float frame_span_ms)
    -> void
  {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width
      = std::max(160.0F, ImGui::GetContentRegionAvail().x - 8.0F);
    const float height = ImGui::GetTextLineHeight();
    const ImVec2 size(width, height);
    ImGui::InvisibleButton("##gpu_timeline_bar", size);

    auto* draw_list = ImGui::GetWindowDrawList();
    constexpr ImU32 kBackgroundColor = IM_COL32(36, 40, 48, 255);
    constexpr ImU32 kBorderColor = IM_COL32(72, 78, 92, 255);
    constexpr ImU32 kBarColor = IM_COL32(78, 166, 255, 255);
    const ImVec2 bottom_right(origin.x + size.x, origin.y + size.y);

    draw_list->AddRectFilled(origin, bottom_right, kBackgroundColor, 4.0F);
    draw_list->AddRect(origin, bottom_right, kBorderColor, 4.0F);

    if (frame_span_ms <= 0.0F || duration_ms <= 0.0F) {
      return;
    }

    const float normalized_start
      = std::clamp(start_ms / frame_span_ms, 0.0F, 1.0F);
    const float normalized_end = std::clamp(
      (start_ms + duration_ms) / frame_span_ms, normalized_start, 1.0F);
    const float left = origin.x + (size.x * normalized_start);
    const float right = origin.x + (size.x * normalized_end);

    draw_list->AddRectFilled(ImVec2(left, origin.y + 2.0F),
      ImVec2(std::max(left + 1.0F, right), origin.y + size.y - 2.0F), kBarColor,
      3.0F);
  }

  auto ShouldSkipCollapsedPassChild(const GpuTimelinePresentationRow& row,
    const std::optional<uint16_t> collapsed_pass_depth) -> bool
  {
    return collapsed_pass_depth.has_value()
      && row.depth > *collapsed_pass_depth;
  }

  auto DrawIndentedScopeLabel(const GpuTimelinePresentationRow& row) -> void
  {
    const float indent = static_cast<float>(row.depth) * 14.0F;
    if (indent > 0.0F) {
      ImGui::Dummy(ImVec2(indent, 0.0F));
      ImGui::SameLine(0.0F, 0.0F);
    }

    if (row.valid) {
      ImGui::TextUnformatted(row.display_name.c_str());
    } else {
      ImGui::TextDisabled("%s", row.display_name.c_str());
    }
  }

} // namespace

GpuTimelinePanel::GpuTimelinePanel(
  const observer_ptr<internal::GpuTimelineProfiler> profiler)
  : profiler_(profiler)
{
}

auto GpuTimelinePanel::SetVisible(const bool visible) -> void
{
  visible_ = visible;
}

auto GpuTimelinePanel::IsVisible() const noexcept -> bool { return visible_; }

auto GpuTimelinePanel::Draw() -> void
{
  if (!visible_) {
    return;
  }

  if (!ImGui::Begin("GPU Timeline", &visible_, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  if (profiler_ == nullptr) {
    ImGui::TextDisabled("GPU timeline profiler is unavailable.");
    ImGui::End();
    return;
  }

  const auto frame = profiler_->GetLastPublishedFrame();
  if (!frame.has_value()) {
    ImGui::TextDisabled("Waiting for a resolved GPU timestamp frame.");
    ImGui::End();
    return;
  }

  const auto& timeline = *frame;
  const auto presentation = presentation_smoother_.Apply(timeline);

  ImGui::Text("Frame %llu | Freq %llu Hz | Scopes %u | Query slots %u",
    static_cast<unsigned long long>(timeline.frame_sequence),
    static_cast<unsigned long long>(timeline.timestamp_frequency_hz),
    static_cast<unsigned>(timeline.scopes.size()), timeline.used_query_slots);
  ImGui::TextDisabled("Presentation smoothing and pass-phase grouping are "
                      "display-only. Raw frame timings are shown on hover.");
  ImGui::Checkbox("Show pass phase details", &show_pass_phase_details_);
  if (timeline.overflowed) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0F, 0.58F, 0.22F, 1.0F), "Overflow");
  }
  if (!timeline.diagnostics.empty()) {
    ImGui::TextDisabled(
      "%u diagnostic(s)", static_cast<unsigned>(timeline.diagnostics.size()));
  }
  ImGui::Separator();

  if (ImGui::BeginTable("GpuTimelineTable", 4,
        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg
          | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 1.8F);
    ImGui::TableSetupColumn(
      "Start (ms)", ImGuiTableColumnFlags_WidthFixed, 88.0F);
    ImGui::TableSetupColumn(
      "Duration (ms)", ImGuiTableColumnFlags_WidthFixed, 96.0F);
    ImGui::TableSetupColumn(
      "Timeline", ImGuiTableColumnFlags_WidthStretch, 2.2F);
    ImGui::TableHeadersRow();

    std::optional<uint16_t> collapsed_pass_depth {};
    for (std::size_t i = 0; i < presentation.rows.size(); ++i) {
      const auto& row = presentation.rows[i];
      if (collapsed_pass_depth.has_value()
        && row.depth <= *collapsed_pass_depth) {
        collapsed_pass_depth.reset();
      }
      if (ShouldSkipCollapsedPassChild(row, collapsed_pass_depth)) {
        continue;
      }
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      DrawIndentedScopeLabel(row);

      ImGui::TableSetColumnIndex(1);
      if (row.valid) {
        ImGui::Text("%.3f", row.display_start_ms);
      } else {
        ImGui::TextDisabled("--");
      }

      ImGui::TableSetColumnIndex(2);
      if (row.valid) {
        ImGui::Text("%.3f", row.display_duration_ms);
      } else {
        ImGui::TextDisabled("invalid");
      }

      ImGui::TableSetColumnIndex(3);
      if (row.valid) {
        ImGui::PushID(static_cast<int>(i));
        DrawTimelineBar(row.display_start_ms, row.display_duration_ms,
          presentation.display_frame_span_ms);
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          if (row.is_grouped_pass) {
            ImGui::Text(
              "Grouped pass row from %u pass(es)", row.grouped_scope_count);
            ImGui::Separator();
          }
          ImGui::Text("Raw start: %.3f ms", row.raw_start_ms);
          ImGui::Text("Raw duration: %.3f ms", row.raw_duration_ms);
          ImGui::Text("Raw end: %.3f ms", row.raw_end_ms);
          ImGui::Separator();
          ImGui::Text("Displayed start: %.3f ms", row.display_start_ms);
          ImGui::Text("Displayed duration: %.3f ms", row.display_duration_ms);
          ImGui::EndTooltip();
        }
        ImGui::PopID();
      } else {
        ImGui::TextDisabled("no timing");
      }

      if (!show_pass_phase_details_ && row.is_grouped_pass) {
        collapsed_pass_depth = row.depth;
      }
    }

    ImGui::EndTable();
  }

  if (!timeline.diagnostics.empty() && ImGui::CollapsingHeader("Diagnostics")) {
    for (const auto& diagnostic : timeline.diagnostics) {
      ImGui::BulletText(
        "%s: %s", diagnostic.code.c_str(), diagnostic.message.c_str());
    }
  }

  ImGui::End();
}

} // namespace oxygen::engine::imgui
