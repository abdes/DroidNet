//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/CameraRigController.h"
#include "InputSystem/InputDebugPanel.h"
#include <imgui.h>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputSystem.h>

namespace {

struct KeycapColors {
  ImU32 background;
  ImU32 border;
  ImU32 text;
};

constexpr auto kKeycapColors = KeycapColors {
  .background = IM_COL32(40, 40, 46, 255),
  .border = IM_COL32(80, 80, 90, 255),
  .text = IM_COL32(230, 230, 240, 255),
};

auto TextSize(const std::string_view text) -> ImVec2
{
  return ImGui::CalcTextSize(
    std::to_address(text.begin()), std::to_address(text.end()));
}

//! The size of a keycap: its label with padding around it.
auto MeasureKeycap(const std::string_view label, const float scale) -> ImVec2
{
  const float pad = 6.0F * scale;
  const ImVec2 text_size = TextSize(label);
  return { text_size.x + (pad * 2.0F), text_size.y + (pad * 1.5F) };
}

//! Draw a keyboard or mouse keycap, a rounded box around a centered label,
//! and return its size.
auto DrawKeycap(ImDrawList* draw_list, const ImVec2 position,
  const std::string_view label, const float scale) -> ImVec2
{
  const ImVec2 size = MeasureKeycap(label, scale);
  const ImVec2 text_size = TextSize(label);
  const float rounding = 6.0F * scale;
  const ImVec2 max(position.x + size.x, position.y + size.y);
  draw_list->AddRectFilled(position, max, kKeycapColors.background, rounding);
  draw_list->AddRect(
    position, max, kKeycapColors.border, rounding, 0, 1.5F * scale);
  const ImVec2 text_position(position.x + ((size.x - text_size.x) * 0.5F),
    position.y + ((size.y - text_size.y) * 0.5F));
  draw_list->AddText(text_position, kKeycapColors.text,
    std::to_address(label.begin()), std::to_address(label.end()));
  return size;
}

//! Draw a small horizontal bar filled to `value`, clamped to [0, 1].
auto DrawValueBar(ImDrawList* draw_list, const ImVec2 position,
  const ImVec2 size, const float value) -> void
{
  constexpr auto kBackground = IM_COL32(30, 30, 34, 255);
  constexpr auto kFill = IM_COL32(90, 170, 255, 255);
  const ImVec2 max(position.x + size.x, position.y + size.y);
  draw_list->AddRectFilled(position, max, kBackground, 3.0F);
  const float filled = size.x * std::clamp(value, 0.0F, 1.0F);
  draw_list->AddRectFilled(
    position, ImVec2(position.x + filled, max.y), kFill, 3.0F);
}

//! Plot a small line of values in [-1, 1], oldest first.
auto PlotSparkline(
  const char* id, const std::vector<float>& values, const ImVec2 size) -> void
{
  if (values.empty()) {
    return;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_PlotLines, IM_COL32(140, 200, 255, 220));
  ImGui::PlotLines(id, values.data(), static_cast<int>(values.size()), 0,
    nullptr, -1.0F, 1.0F, size);
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}

//! An action's state as a short label and a badge color.
auto ActionStateBadge(const std::shared_ptr<oxygen::input::Action>& a)
  -> std::pair<const char*, ImU32>
{
  if (!a) {
    return { "<null>", IM_COL32(80, 80, 90, 255) };
  }
  if (a->WasCanceledThisFrame()) {
    return { "Canceled", IM_COL32(230, 120, 70, 255) };
  }
  if (a->WasCompletedThisFrame()) {
    return { "Completed", IM_COL32(110, 200, 120, 255) };
  }
  if (a->WasTriggeredThisFrame()) {
    return { "Triggered", IM_COL32(90, 170, 255, 255) };
  }
  if (a->WasReleasedThisFrame()) {
    return { "Released", IM_COL32(160, 160, 200, 255) };
  }
  if (a->IsOngoing()) {
    return { "Ongoing", IM_COL32(200, 200, 80, 255) };
  }
  return { "Idle", IM_COL32(70, 70, 80, 255) };
}

//! An action's value as one number in [-1, 1]: 0 or 1 for a button, the
//! magnitude for a 2D axis.
auto ActionScalarValue(const std::shared_ptr<oxygen::input::Action>& a) -> float
{
  if (!a) {
    return 0.0F;
  }
  using oxygen::input::ActionValueType;
  switch (a->GetValueType()) {
  case ActionValueType::kAxis2D: {
    const auto& axis = a->GetValue().GetAs<oxygen::Axis2D>();
    return std::min(std::hypot(axis.x, axis.y), 1.0F);
  }
  case ActionValueType::kAxis1D:
    return std::clamp(a->GetValue().GetAs<oxygen::Axis1D>().x, -1.0F, 1.0F);
  case ActionValueType::kBool:
    return a->GetValue().GetAs<bool>() ? 1.0F : 0.0F;
  }
  return 0.0F;
}

} // namespace

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg) - ImGui

namespace oxygen::examples::input_system {

auto InputDebugPanel::Initialize(const InputDebugPanelConfig& config) -> void
{
  config_ = config;
}

auto InputDebugPanel::GetName() const noexcept -> std::string_view
{
  return "Input Debug";
}

auto InputDebugPanel::GetPreferredWidth() const noexcept -> float
{
  return 460.0F;
}

auto InputDebugPanel::GetIcon() const noexcept -> std::string_view
{
  return oxygen::imgui::icons::kIconDemoPanel;
}

auto InputDebugPanel::OnLoaded() -> void { }

auto InputDebugPanel::OnUnloaded() -> void { }

auto InputDebugPanel::History::Push(const float value) noexcept -> void
{
  values_.at(next_) = value;
  next_ = (next_ + 1) % kCapacity;
  count_ = std::min(count_ + 1, kCapacity);
}

auto InputDebugPanel::History::InOrder() const -> std::vector<float>
{
  auto values = std::vector<float> {};
  values.reserve(count_);
  const auto oldest = (next_ + kCapacity - count_) % kCapacity;
  for (std::size_t i = 0; i < count_; ++i) {
    values.push_back(values_.at((oldest + i) % kCapacity));
  }
  return values;
}

auto InputDebugPanel::DrawContents() -> void
{
  const auto& io = ImGui::GetIO();
  ImGui::Text("WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");
  ImGui::Separator();

  if (config_.swimming_mode && config_.commands) {
    bool swimming = *config_.swimming_mode;
    if (ImGui::Checkbox("Swimming mode", &swimming)) {
      config_.commands->RunBool(kSwimmingCommand, swimming);
    }
  }

  struct Row {
    const char* label;
    std::shared_ptr<oxygen::input::Action> act;
    std::vector<std::string_view> keys;
  };

  // The demo's actions, then the camera rig's when there is one.
  std::vector<Row> rows;
  rows.push_back({
    .label = "Shift",
    .act = config_.shift_action,
    .keys = { "Shift" },
  });
  rows.push_back({
    .label = "Jump",
    .act = config_.jump_action,
    .keys = { "Space" },
  });
  rows.push_back({
    .label = "Jump Higher",
    .act = config_.jump_higher_action,
    .keys = { "Shift", "Space" },
  });
  rows.push_back({
    .label = "Swim Up",
    .act = config_.swim_up_action,
    .keys = { "Space" },
  });
  if (config_.camera_rig) {
    rows.push_back({
      .label = "RMB",
      .act = config_.camera_rig->GetRmbAction(),
      .keys = { "RMB" },
    });
    rows.push_back({
      .label = "Orbit",
      .act = config_.camera_rig->GetOrbitAction(),
      .keys = { "RMB", "Drag" },
    });
    rows.push_back({
      .label = "Zoom In",
      .act = config_.camera_rig->GetZoomInAction(),
      .keys = { "Wheel+" },
    });
    rows.push_back({
      .label = "Zoom Out",
      .act = config_.camera_rig->GetZoomOutAction(),
      .keys = { "Wheel-" },
    });
  }

  ImGui::Checkbox("Show inactive", &show_inactive_);
  ImGui::Spacing();

  if (ImGui::BeginTable("##actions", 5, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 110.0F);
    ImGui::TableSetupColumn(
      "Bindings", ImGuiTableColumnFlags_WidthFixed, 200.0F);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0F);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn(
      "History", ImGuiTableColumnFlags_WidthStretch, 1.0F);

    const double now = ImGui::GetTime();

    for (const auto& r : rows) {
      const bool active = r.act
        && (r.act->IsOngoing() || r.act->WasTriggeredThisFrame()
          || r.act->WasCompletedThisFrame() || r.act->WasReleasedThisFrame()
          || r.act->WasCanceledThisFrame()
          || r.act->WasValueUpdatedThisFrame());
      if (!show_inactive_ && !active) {
        continue;
      }

      if (r.act && r.act->WasTriggeredThisFrame()) {
        last_trigger_time_[r.label] = now;
      }

      ImGui::PushID(r.label);

      const float v = ActionScalarValue(r.act);
      auto& hist = histories_[r.label];
      hist.Push(v);

      const auto [state_text, state_col] = ActionStateBadge(r.act);

      ImGui::TableNextRow();
      {
        float flash_alpha = 0.0F;
        constexpr float kFlashDuration = 1.5F;
        auto it = last_trigger_time_.find(r.label);
        if (it != last_trigger_time_.end()) {
          const auto age = static_cast<float>(now - it->second);
          if (age >= 0.0F && age < kFlashDuration) {
            float t = 1.0F - (age / kFlashDuration);
            t = t * t;
            flash_alpha = t;
          }
        }
        if (flash_alpha > 0.0F) {
          const int a = static_cast<int>(flash_alpha * 110.0F);
          const ImU32 col = IM_COL32(255, 220, 120, a);
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col);
        }
      }

      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(r.label);

      ImGui::TableSetColumnIndex(1);
      {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float scale = ImGui::GetIO().FontGlobalScale;
        const float gap = 8.0F * scale;
        float used_w = 0.0F;
        float used_h = 0.0F;
        for (const auto key : r.keys) {
          const ImVec2 size = DrawKeycap(dl, p, key, scale);
          p.x += size.x + gap;
          used_w += size.x + gap;
          used_h = std::max(used_h, size.y);
        }
        if (used_w > 0.0F && used_h > 0.0F) {
          used_w -= gap;
          ImGui::Dummy(ImVec2(used_w, used_h));
        } else {
          ImGui::Dummy(ImVec2(1, 26.0F * scale));
        }
      }

      ImGui::TableSetColumnIndex(2);
      ImGui::AlignTextToFramePadding();
      {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, state_col);
        ImGui::TextDisabled("  %s  ", state_text);
        ImGui::PopStyleColor();
      }

      ImGui::TableSetColumnIndex(3);
      {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        DrawValueBar(dl, p, ImVec2(160, 8), v);
        ImGui::Dummy(ImVec2(160, 10));
      }

      ImGui::TableSetColumnIndex(4);
      PlotSparkline("##spark", hist.InOrder(), ImVec2(160, 28));

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

} // namespace oxygen::examples::input_system

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
