//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <ratio>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/EnumIndexedArray.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/ImGui/Widgets/StatsOverlay.h>

namespace oxygen::imgui {

namespace {

  //! Time constant of the running average.
  constexpr float kSmoothingSeconds = 0.5F;
  //! A phase whose average stays under this does no work and gets no row.
  constexpr float kIdlePhaseMs = 0.005F;
  constexpr std::size_t kHighlightedPhases = 3;
  constexpr float kMargin = 8.0F;
  constexpr float kBackgroundAlpha = 0.45F;
  constexpr ImVec4 kSlowColor { 1.0F, 0.65F, 0.0F, 1.0F };
  constexpr ImVec4 kOverBudgetColor { 1.0F, 0.35F, 0.3F, 1.0F };
  constexpr ImVec4 kHeadingColor { 0.6F, 0.6F, 0.6F, 1.0F };

  //! ImGui flags are plain int enumerators; combine them unsigned.
  template <typename... Flag> constexpr auto Combine(Flag... flags) -> int
  {
    return static_cast<int>((static_cast<unsigned>(flags) | ...));
  }

  constexpr auto kWindowFlags
    = Combine(ImGuiWindowFlags_NoDecoration, ImGuiWindowFlags_AlwaysAutoResize,
      ImGuiWindowFlags_NoMove, ImGuiWindowFlags_NoSavedSettings,
      ImGuiWindowFlags_NoFocusOnAppearing, ImGuiWindowFlags_NoNav,
      ImGuiWindowFlags_NoBringToFrontOnFocus, ImGuiWindowFlags_NoInputs);
  constexpr auto kTableFlags
    = Combine(ImGuiTableFlags_SizingFixedFit, ImGuiTableFlags_NoSavedSettings);

  auto ToMs(const std::chrono::microseconds duration) -> float
  {
    return std::chrono::duration<float, std::milli>(duration).count();
  }

  auto Blend(float& average, const float sample, const float weight) -> void
  {
    average += (sample - average) * weight;
  }

  //! One label and value row; the value is right-aligned in its column.
  auto Row(const std::string_view label, const std::string& value,
    const ImVec4* color = nullptr) -> void
  {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(
      std::to_address(label.begin()), std::to_address(label.end()));
    ImGui::TableSetColumnIndex(1);
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = ImGui::CalcTextSize(value.c_str()).x;
    ImGui::SetCursorPosX(
      ImGui::GetCursorPosX() + std::max(0.0F, available - width));
    if (color != nullptr) {
      ImGui::PushStyleColor(ImGuiCol_Text, *color);
    }
    ImGui::TextUnformatted(value.c_str());
    if (color != nullptr) {
      ImGui::PopStyleColor();
    }
  }

  auto Heading(const std::string_view text) -> void
  {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, kHeadingColor);
    ImGui::TextUnformatted(
      std::to_address(text.begin()), std::to_address(text.end()));
    ImGui::PopStyleColor();
  }

  auto TextWidth(const std::string_view text) -> float
  {
    return ImGui::CalcTextSize(
      std::to_address(text.begin()), std::to_address(text.end()))
      .x;
  }

  auto LabelColumnWidth(const StatsOverlayConfig& config) -> float
  {
    float width = 0.0F;
    const auto fit = [&width](const std::string_view text) -> void {
      width = std::max(width, TextWidth(text));
    };
    constexpr auto kFixedLabels = std::array<std::string_view, 8> {
      "Frame #",
      "FPS",
      "Frame",
      "Engine",
      "CPU",
      "Pacing",
      "Budget",
      "GPU",
    };
    std::ranges::for_each(kFixedLabels, fit);
    if (config.show_frame_timing_detail) {
      fit("Phases");
      for (const auto phase : enum_as_index<core::PhaseId>) {
        fit(core::kPhaseRegistry[phase].Name());
      }
    }
    return width;
  }

  auto ValueColumnWidth(const StatsOverlayConfig& config) -> float
  {
    // The widest values the rows produce in practice.
    return std::max(TextWidth("0000000000"),
      TextWidth(config.show_budget_stats ? "000.0 / 000.0 ms" : "000.00 ms"));
  }

  auto Ms(const float ms) -> std::string
  {
    return fmt::format("{:.2f} ms", ms);
  }

} // namespace

auto StatsOverlay::Sample(const engine::FrameContext& context) -> void
{
  const auto& io = ImGui::GetIO();
  const auto timing = context.GetFrameTiming();
  const float frame_ms = io.DeltaTime * 1000.0F;
  const float fps = context.GetCurrentFPS();

  // A time-based weight keeps the average's span the same at any frame rate.
  const float weight = primed_
    ? 1.0F - std::exp(-std::max(io.DeltaTime, 0.0F) / kSmoothingSeconds)
    : 1.0F;
  primed_ = true;
  Blend(fps_, fps, weight);
  Blend(frame_ms_, frame_ms, weight);
  Blend(engine_ms_, ToMs(timing.frame_duration), weight);
  Blend(pacing_ms_, ToMs(timing.pacing_duration), weight);
  for (const auto phase : enum_as_index<core::PhaseId>) {
    Blend(phase_ms_[phase], ToMs(timing.stage_timings[phase]), weight);
  }
}

auto StatsOverlay::Draw(const engine::FrameContext& context,
  const StatsOverlayConfig& config, const float top_margin) -> void
{
  // Keep averaging while hidden so the numbers are settled when shown.
  Sample(context);
  if (!config.ShowsAnything()) {
    return;
  }

  const auto* viewport = ImGui::GetMainViewport();
  const auto& style = ImGui::GetStyle();
  const float scale = style.FontScaleMain * style.FontScaleDpi;
  ImGui::SetNextWindowPos(
    ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - (kMargin * scale),
      viewport->WorkPos.y + top_margin + (kMargin * scale)),
    ImGuiCond_Always, ImVec2(1.0F, 0.0F));
  ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
  const bool visible = ImGui::Begin("Frame Stats", nullptr, kWindowFlags);
  if (!visible || !ImGui::BeginTable("##stats", 2, kTableFlags)) {
    ImGui::End();
    return;
  }
  // Both columns are sized for every row the enabled sections can show, so
  // the panel keeps its width when an idle phase's row comes and goes.
  ImGui::TableSetupColumn(
    "label", ImGuiTableColumnFlags_WidthFixed, LabelColumnWidth(config));
  ImGui::TableSetupColumn(
    "value", ImGuiTableColumnFlags_WidthFixed, ValueColumnWidth(config));

  // Always first, so a screenshot of a defect names its frame.
  Row("Frame #", fmt::format("{}", context.GetFrameSequenceNumber().get()));

  if (config.show_fps) {
    Row("FPS", fmt::format("{:.0f}", fps_));
    Row("Frame", Ms(frame_ms_));
  }

  if (config.show_engine_timing) {
    Heading("Engine");
    Row("CPU", Ms(engine_ms_));
    Row("Pacing", Ms(pacing_ms_));
  }

  if (config.show_frame_timing_detail) {
    // The slowest phases by their average, so the highlight does not flicker.
    std::array<float, kHighlightedPhases> slowest {};
    for (const auto phase : enum_as_index<core::PhaseId>) {
      const float ms = phase_ms_[phase];
      if (ms > slowest.back()) {
        slowest.back() = ms;
        std::ranges::sort(slowest, std::ranges::greater {});
      }
    }
    Heading("Phases");
    for (const auto phase : enum_as_index<core::PhaseId>) {
      const float ms = phase_ms_[phase];
      if (ms < kIdlePhaseMs) {
        continue;
      }
      const bool is_slow = ms >= slowest.back();
      Row(core::kPhaseRegistry[phase].Name(), Ms(ms),
        is_slow ? &kSlowColor : nullptr);
    }
  }

  if (config.show_budget_stats) {
    const auto budget = context.GetBudgetStats();
    const float cpu_budget_ms
      = std::chrono::duration<float, std::milli>(budget.cpu_budget).count();
    const float gpu_budget_ms
      = std::chrono::duration<float, std::milli>(budget.gpu_budget).count();
    Heading("Budget");
    const bool over = cpu_budget_ms > 0.0F && engine_ms_ > cpu_budget_ms;
    Row("CPU", fmt::format("{:.1f} / {:.1f} ms", engine_ms_, cpu_budget_ms),
      over ? &kOverBudgetColor : nullptr);
    Row("GPU", fmt::format("{:.1f} ms", gpu_budget_ms));
  }

  ImGui::EndTable();
  ImGui::End();
}

} // namespace oxygen::imgui
