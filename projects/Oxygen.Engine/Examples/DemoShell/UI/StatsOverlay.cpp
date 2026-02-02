//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>

#include "DemoShell/UI/StatsOverlay.h"
#include "DemoShell/UI/UiSettingsVm.h"

namespace oxygen::examples::ui {

StatsOverlay::StatsOverlay(observer_ptr<UiSettingsVm> settings_vm)
  : vm_(settings_vm)
{
  DCHECK_NOTNULL_F(settings_vm, "expecting UiSettingsVm");
}

auto StatsOverlay::Draw(const engine::FrameContext& fc) const -> void
{
  const auto config = vm_->GetStatsConfig();
  if (!config.show_fps && !config.show_frame_timing_detail
    && !config.show_engine_timing && !config.show_budget_stats) {
    return;
  }

  const auto& io = ImGui::GetIO();
  const float dpi_scale = io.FontGlobalScale > 0.0F ? io.FontGlobalScale : 1.0F;
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

  auto draw_right_aligned
    = [](const char* text, const ImVec4* color = nullptr) {
        const float available = ImGui::GetContentRegionAvail().x;
        const float text_width = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX(
          ImGui::GetCursorPosX() + std::max(0.0f, available - text_width));
        if (color) {
          ImGui::TextColored(*color, "%s", text);
        } else {
          ImGui::TextUnformatted(text);
        }
      };

  std::array<char, 128> buffer {};
  bool drawn = false;

  if (config.show_fps) {
    // FPS: rounded 3-digit integer for stability
    std::snprintf(buffer.data(), buffer.size(), "FPS %03d",
      static_cast<int>(std::round(io.Framerate)));
    draw_right_aligned(buffer.data());

    // show engine fps ALWAYS to keep display stable
    const float engine_fps = fc.GetCurrentFPS();
    std::snprintf(buffer.data(), buffer.size(), "Engine FPS %03d",
      static_cast<int>(std::round(engine_fps)));
    draw_right_aligned(buffer.data());

    drawn = true;
  }

  if (config.show_frame_timing_detail) {
    if (drawn) {
      ImGui::Dummy(ImVec2(0.0f, 20.0f * dpi_scale));
    }
    const float frame_ms = io.DeltaTime * 1000.0f;
    std::snprintf(buffer.data(), buffer.size(), "Frame %04.1f ms", frame_ms);
    draw_right_aligned(buffer.data());

    const float avg_ms = io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f;
    std::snprintf(buffer.data(), buffer.size(), "Avg %04.1f ms", avg_ms);
    draw_right_aligned(buffer.data());
    drawn = true;
  }

  if (config.show_engine_timing) {
    if (drawn) {
      ImGui::Dummy(ImVec2(0.0f, 20.0f * dpi_scale));
    }
    const auto timing = fc.GetFrameTiming();
    const float total_ms
      = static_cast<float>(timing.frame_duration.count()) / 1000.0f;

    std::snprintf(buffer.data(), buffer.size(), "Engine %04.1f ms", total_ms);
    draw_right_aligned(buffer.data());

    // Show phase-level breakdown if detailed view is enabled
    if (config.show_frame_timing_detail) {
      // Find top 3 durations to highlight using a stable selection
      struct PhaseDuration {
        core::PhaseId id;
        std::chrono::microseconds duration;
      };
      std::vector<PhaseDuration> phases;
      phases.reserve(static_cast<std::size_t>(core::PhaseId::kCount));
      for (const auto phase : enum_as_index<core::PhaseId>) {
        phases.push_back({ phase.to_enum(), timing.stage_timings[phase] });
      }

      // Sort: primary key = duration (desc), secondary key = phase ID (asc)
      std::ranges::sort(phases, [](const auto& a, const auto& b) {
        if (a.duration != b.duration) {
          return a.duration > b.duration;
        }
        return a.id < b.id;
      });

      // The first 3 in the sorted list are the top ones
      core::PhaseMask top_mask = 0;
      for (size_t i = 0; i < 3 && i < phases.size(); ++i) {
        if (phases[i].duration > std::chrono::microseconds(0)) {
          top_mask |= core::MakePhaseMask(phases[i].id);
        }
      }

      constexpr ImVec4 orange(1.0f, 0.65f, 0.0f, 1.0f);

      // Draw all phases in their canonical order to prevent flashing
      for (const auto phase : enum_as_index<core::PhaseId>) {
        const auto duration = timing.stage_timings[phase];
        const float phase_ms = static_cast<float>(duration.count()) / 1000.0f;
        std::snprintf(buffer.data(), buffer.size(), "[%s] %04.1f ms",
          core::kPhaseRegistry[phase].Name(), phase_ms);

        const bool is_top
          = (top_mask & core::MakePhaseMask(phase.to_enum())) != 0;
        draw_right_aligned(buffer.data(), is_top ? &orange : nullptr);
      }

      // Add Pacing separately as it's not a PhaseId
      const float pacing_ms
        = static_cast<float>(timing.pacing_duration.count()) / 1000.0f;
      std::snprintf(
        buffer.data(), buffer.size(), "[Pacing] %04.1f ms", pacing_ms);
      draw_right_aligned(buffer.data());
    }
    drawn = true;
  }

  if (config.show_budget_stats) {
    if (drawn) {
      ImGui::Dummy(ImVec2(0.0f, 20.0f * dpi_scale));
    }
    const auto budget = fc.GetBudgetStats();
    const float cpu_budget_ms = static_cast<float>(budget.cpu_budget.count());
    const float gpu_budget_ms = static_cast<float>(budget.gpu_budget.count());

    std::snprintf(
      buffer.data(), buffer.size(), "CPU Budget %04.1f ms", cpu_budget_ms);
    draw_right_aligned(buffer.data());

    std::snprintf(
      buffer.data(), buffer.size(), "GPU Budget %04.1f ms", gpu_budget_ms);
    draw_right_aligned(buffer.data());
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

} // namespace oxygen::examples::ui
