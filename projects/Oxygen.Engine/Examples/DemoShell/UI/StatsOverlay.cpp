//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdio>

#include <imgui.h>

#include "DemoShell/UI/StatsOverlay.h"

namespace oxygen::examples::ui {

auto StatsOverlay::Draw() const -> void
{
  if (!config_.show_fps && !config_.show_frame_timing_detail) {
    return;
  }

  const auto& io = ImGui::GetIO();
  const float dpi_scale
    = (io.FontGlobalScale > 0.0F) ? io.FontGlobalScale : 1.0F;
  const float min_width = 300.0F * dpi_scale;
  const float width = std::max(io.DisplaySize.x * 0.25F, min_width);
  const float height = io.DisplaySize.y;

  ImGui::SetNextWindowPos(
    ImVec2(io.DisplaySize.x - width, 0.0F), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0F);

  constexpr auto kFlags = ImGuiWindowFlags_NoDecoration
    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
    | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus
    | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground;

  ImGui::PushStyleVar(
    ImGuiStyleVar_WindowPadding, ImVec2(12.0F * dpi_scale, 12.0F * dpi_scale));

  if (!ImGui::Begin("FrameStatsOverlay", nullptr, kFlags)) {
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }

  auto draw_right_aligned = [](const char* text) {
    const float available = ImGui::GetContentRegionAvail().x;
    const float text_width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(
      ImGui::GetCursorPosX() + std::max(0.0F, available - text_width));
    ImGui::TextUnformatted(text);
  };

  if (config_.show_fps) {
    std::array<char, 64> buffer {};
    std::snprintf(buffer.data(), buffer.size(), "FPS %.1f", io.Framerate);
    draw_right_aligned(buffer.data());
  }

  if (config_.show_frame_timing_detail) {
    std::array<char, 64> buffer {};
    const float frame_ms = io.DeltaTime * 1000.0F;
    std::snprintf(buffer.data(), buffer.size(), "Frame %.2f ms", frame_ms);
    draw_right_aligned(buffer.data());

    const float avg_ms
      = (io.Framerate > 0.0F) ? (1000.0F / io.Framerate) : 0.0F;
    std::snprintf(buffer.data(), buffer.size(), "Avg %.2f ms", avg_ms);
    draw_right_aligned(buffer.data());
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace oxygen::examples::ui
