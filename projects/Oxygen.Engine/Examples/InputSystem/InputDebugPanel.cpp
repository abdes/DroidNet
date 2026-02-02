//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include <imgui.h>

#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputSystem.h>

#include "DemoShell/UI/CameraRigController.h"
#include "InputSystem/InputDebugPanel.h"

namespace {

//! Draw a simple keyboard/mouse keycap (rounded rect + centered label).
auto DrawKeycap(ImDrawList* dl, ImVec2 p, const char* label, ImU32 bg,
  ImU32 border, ImU32 text, float scale = 1.0F) -> void
{
  const float pad = 6.0F * scale;
  const ImVec2 text_size = ImGui::CalcTextSize(label);
  const ImVec2 size
    = ImVec2(text_size.x + pad * 2.0F, text_size.y + pad * 1.5F);
  const float r = 6.0F * scale;

  const ImVec2 p_max(p.x + size.x, p.y + size.y);
  dl->AddRectFilled(p, p_max, bg, r);
  dl->AddRect(p, p_max, border, r, 0, 1.5F * scale);

  const ImVec2 tp = ImVec2(
    p.x + (size.x - text_size.x) * 0.5F, p.y + (size.y - text_size.y) * 0.5F);
  dl->AddText(tp, text, label, label + std::strlen(label));
}

//! Compute the rendered size of a keycap for spacing/layout.
auto MeasureKeycap(const char* label, float scale = 1.0F) -> ImVec2
{
  const float pad = 6.0F * scale;
  const ImVec2 text_size = ImGui::CalcTextSize(label);
  return ImVec2(text_size.x + pad * 2.0F, text_size.y + pad * 1.5F);
}

//! Draw a tiny horizontal analog bar for scalar values in [vmin, vmax].
auto DrawAnalogBar(ImDrawList* dl, ImVec2 p, ImVec2 sz, float v,
  float vmin = -1.0F, float vmax = 1.0F, ImU32 bg = IM_COL32(30, 30, 34, 255),
  ImU32 fg = IM_COL32(90, 170, 255, 255)) -> void
{
  const ImVec2 p_max(p.x + sz.x, p.y + sz.y);
  dl->AddRectFilled(p, p_max, bg, 3.0F);
  const float t = (v - vmin) / (vmax - vmin);
  const float tt = std::clamp(t, 0.0F, 1.0F);
  const ImVec2 fill = ImVec2(p.x + sz.x * tt, p.y + sz.y);
  dl->AddRectFilled(p, ImVec2(fill.x, fill.y), fg, 3.0F);
  // zero line
  const float zt = (-vmin) / (vmax - vmin);
  const float zx = p.x + sz.x * std::clamp(zt, 0.0F, 1.0F);
  dl->AddLine(ImVec2(zx, p.y), ImVec2(zx, p.y + sz.y),
    IM_COL32(180, 180, 190, 120), 1.0F);
}

//! Plot a tiny sparkline from a circular buffer (values[filled], head==next).
auto PlotSparkline(const char* id, const float* values, int filled, int head,
  int capacity, ImVec2 size) -> void
{
  if (!values || filled <= 0) {
    return;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_PlotLines, IM_COL32(140, 200, 255, 220));
  // rotate to chronological order into a small local buffer
  static float tmp[128];
  const int N = (std::min)(filled, 128);
  const int cap = (std::min)(capacity, 128);
  // Start at the oldest sample index in the ring buffer
  const int start = (head - filled + cap) % cap;
  for (int i = 0; i < N; ++i) {
    tmp[i] = values[(start + i) % cap];
  }
  ImGui::PlotLines(id, tmp, N, 0, nullptr, -1.0F, 1.0F, size);
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}

//! Compute an action state label and color for a compact badge.
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

//! Compute a scalar analog value in [-1, 1] for plotting bars/sparklines.
//! For bool actions, returns 0 or 1. For Axis2D, returns magnitude.
auto ActionScalarValue(const std::shared_ptr<oxygen::input::Action>& a) noexcept
  -> float
{
  if (!a) {
    return 0.0F;
  }

  using oxygen::input::ActionValueType;
  const auto vt = a->GetValueType();
  switch (vt) {
  case ActionValueType::kAxis2D: {
    const auto& axis = a->GetValue().GetAs<oxygen::Axis2D>();
    const float mag = std::sqrt(axis.x * axis.x + axis.y * axis.y);
    return (std::min)(mag, 1.0F);
  }
  case ActionValueType::kAxis1D: {
    const auto& ax = a->GetValue().GetAs<oxygen::Axis1D>();
    return std::clamp(ax.x, -1.0F, 1.0F);
  }
  case ActionValueType::kBool: {
    const bool b = a->GetValue().GetAs<bool>();
    return b ? 1.0F : 0.0F;
  }
  default:
    return 0.0F;
  }
}

} // namespace

namespace oxygen::examples::input_system {

void InputDebugPanel::Initialize(const InputDebugPanelConfig& config)
{
  config_ = config;
}

void InputDebugPanel::UpdateConfig(const InputDebugPanelConfig& config)
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

void InputDebugPanel::History::Push(float v) noexcept
{
  values[head] = v;
  head = (head + 1) % kCapacity;
  if (count < kCapacity) {
    ++count;
  }
}

auto InputDebugPanel::DrawContents() -> void
{
  const auto& io = ImGui::GetIO();
  ImGui::Text("WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");
  ImGui::Separator();

  if (config_.swimming_mode) {
    bool prev_mode = *config_.swimming_mode;
    ImGui::Checkbox("Swimming mode", config_.swimming_mode);
    if (*config_.swimming_mode != prev_mode && config_.input_system) {
      if (*config_.swimming_mode) {
        config_.input_system->DeactivateMappingContext(
          config_.ground_movement_ctx);
        config_.input_system->ActivateMappingContext(config_.swimming_ctx);
      } else {
        config_.input_system->DeactivateMappingContext(config_.swimming_ctx);
        config_.input_system->ActivateMappingContext(
          config_.ground_movement_ctx);
        if (config_.pending_ground_reset) {
          *config_.pending_ground_reset = true;
        }
      }
    }
  }

  struct Row {
    const char* label;
    std::shared_ptr<oxygen::input::Action> act;
  };

  // Build rows from demo actions + camera rig actions
  std::vector<Row> rows;

  // Demo-specific actions
  rows.push_back({ "Shift", config_.shift_action });
  rows.push_back({ "Jump", config_.jump_action });
  rows.push_back({ "Jump Higher", config_.jump_higher_action });
  rows.push_back({ "Swim Up", config_.swim_up_action });

  // Camera rig actions (if available)
  if (config_.camera_rig) {
    rows.push_back({ "RMB", config_.camera_rig->GetRmbAction() });
    rows.push_back({ "Orbit", config_.camera_rig->GetOrbitAction() });
    rows.push_back({ "Zoom In", config_.camera_rig->GetZoomInAction() });
    rows.push_back({ "Zoom Out", config_.camera_rig->GetZoomOutAction() });
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
          const float age = static_cast<float>(now - it->second);
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
        auto draw_and_advance = [&](const char* cap) {
          const ImVec2 sz = MeasureKeycap(cap, scale);
          DrawKeycap(dl, p, cap, IM_COL32(40, 40, 46, 255),
            IM_COL32(80, 80, 90, 255), IM_COL32(230, 230, 240, 255), scale);
          p.x += sz.x + gap;
          used_w += sz.x + gap;
          used_h = (std::max)(used_h, sz.y);
        };

        const std::string_view label_view = r.label;
        if (label_view == "Jump") {
          draw_and_advance("Space");
        } else if (label_view == "Jump Higher") {
          draw_and_advance("Shift");
          draw_and_advance("Space");
        } else if (label_view == "Shift") {
          draw_and_advance("Shift");
        } else if (label_view == "Swim Up") {
          draw_and_advance("Space");
        } else if (label_view == "RMB") {
          draw_and_advance("RMB");
        } else if (label_view == "Orbit") {
          draw_and_advance("RMB");
          draw_and_advance("Drag");
        } else if (label_view == "Zoom In") {
          draw_and_advance("Wheel+");
        } else if (label_view == "Zoom Out") {
          draw_and_advance("Wheel-");
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
        DrawAnalogBar(dl, p, ImVec2(160, 8), v, 0.0F, 1.0F);
        ImGui::Dummy(ImVec2(160, 10));
      }

      ImGui::TableSetColumnIndex(4);
      PlotSparkline("##spark", hist.values, hist.count, hist.head,
        History::kCapacity, ImVec2(160, 28));

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

} // namespace oxygen::examples::input_system
