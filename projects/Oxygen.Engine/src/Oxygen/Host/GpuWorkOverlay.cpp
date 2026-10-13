//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <optional>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/GpuWorkOverlay.h>
#include <Oxygen/ImGui/Widgets/GpuWorkChart.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::host {

namespace {

  constexpr float kMargin = 8.0F;
  constexpr float kMinWidth = 460.0F;
  constexpr float kBackgroundAlpha = 0.6F;
  constexpr std::uint32_t kViewColor = 0x5A6A7AU;
  constexpr std::uint32_t kIdleColor = 0x505050U;

  constexpr auto kWindowFlags = static_cast<ImGuiWindowFlags>(
    static_cast<unsigned>(ImGuiWindowFlags_NoDecoration)
    | static_cast<unsigned>(ImGuiWindowFlags_AlwaysAutoResize)
    | static_cast<unsigned>(ImGuiWindowFlags_NoMove)
    | static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings)
    | static_cast<unsigned>(ImGuiWindowFlags_NoFocusOnAppearing)
    | static_cast<unsigned>(ImGuiWindowFlags_NoNav));

  auto ToCost(const vortex::GpuWorkStat& stat) -> imgui::GpuWorkChartCost
  {
    return { .avg_ms = stat.avg_ms, .max_ms = stat.max_ms };
  }

  auto ToSegment(const vortex::GpuWorkGroup& group)
    -> imgui::GpuWorkChartSegment
  {
    const auto& info = vortex::GetGpuWorkBucketInfo(group.bucket);
    auto segment = imgui::GpuWorkChartSegment {
      .label = std::string(info.display_name),
      .color_rgb = info.color.Rgb24(),
      .cost = ToCost(group.time),
      .tooltip = group.bucket == vortex::GpuWorkBucket::kOther
        ? "Scopes the stage table does not know, and command lists (by name) "
          "doing work outside every stage scope"
        : std::string {},
      .items = {},
    };
    for (const auto& item : group.items) {
      segment.items.push_back(
        { .label = item.name, .cost = ToCost(item.time) });
    }
    return segment;
  }

  auto ViewLabel(const vortex::GpuWorkView& view) -> std::string
  {
    return view.name.empty() ? fmt::format("View {}", view.view_id) : view.name;
  }

} // namespace

auto GpuWorkOverlay::RegisterConsoleBindings(console::Console& console) -> void
{
  (void)console.RegisterCVar(console::CVarDefinition {
    .name = std::string(kGpuWorkCVar),
    .help = "Show the GPU work overlay; collects GPU timing while shown",
    .default_value = false,
    .flags = console::CVarFlags::kNone,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
    .allowed_values = {},
  });
}

auto GpuWorkOverlay::Draw(const observer_ptr<vortex::Renderer> renderer,
  const bool shown, const std::optional<ViewId> main_view,
  const std::chrono::milliseconds budget, const float top_margin) -> void
{
  if (!shown || renderer != leased_renderer_) {
    Release();
  }
  if (!shown || renderer == nullptr) {
    return;
  }
  auto& diagnostics = renderer->GetDiagnosticsService();
  if (leased_renderer_ == nullptr) {
    // Inert without kDiagnosticsAndProfiling; kept so it is not retried.
    lease_ = diagnostics.AcquireGpuTimeline();
    leased_renderer_ = renderer;
  }

  const auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2 { viewport->WorkPos.x + kMargin,
                            viewport->WorkPos.y + top_margin + kMargin },
    ImGuiCond_Always);
  // At least kMinWidth; wider when the legend's columns need it.
  ImGui::SetNextWindowSizeConstraints(ImVec2 { kMinWidth, 0.0F },
    ImVec2 { viewport->WorkSize.x, viewport->WorkSize.y });
  ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
  const bool visible = ImGui::Begin("GPU Work", nullptr, kWindowFlags);
  if (visible) {
    const auto summary = diagnostics.GetLatestGpuWorkSummary();
    if (!lease_.IsActive()) {
      ImGui::TextDisabled("No GPU timing: the renderer lacks "
                          "kDiagnosticsAndProfiling");
    } else if (!summary) {
      ImGui::TextDisabled("Waiting for GPU timing");
    } else {
      if (summary->revision != revision_) {
        Rebuild(*summary, main_view);
      }
      chart_.budget_ms
        = std::chrono::duration<float, std::milli>(budget).count();
      imgui::DrawGpuWorkChart(chart_);
    }
  }
  ImGui::End();
}

auto GpuWorkOverlay::Release() noexcept -> void
{
  lease_.Release();
  leased_renderer_ = nullptr;
  revision_ = 0U;
  chart_ = {};
}

auto GpuWorkOverlay::Rebuild(const vortex::GpuWorkSummary& summary,
  const std::optional<ViewId> main_view) -> void
{
  revision_ = summary.revision;
  chart_.rows.clear();

  const vortex::GpuWorkView* main = nullptr;
  for (const auto& view : summary.views) {
    if (main_view && view.view_id == main_view->get()) {
      main = &view;
    }
  }
  if (main == nullptr && !summary.views.empty()) {
    main = &summary.views.front();
  }

  auto frame = imgui::GpuWorkChartRow {
    .label = main != nullptr ? fmt::format("Frame ({})", ViewLabel(*main))
                             : std::string { "Frame" },
    .total = ToCost(summary.frame_gpu),
    .segments = {},
    .default_open = true,
  };
  if (main != nullptr) {
    for (const auto& group : main->groups) {
      frame.segments.push_back(ToSegment(group));
    }
  }
  for (const auto& view : summary.views) {
    if (&view != main) {
      frame.segments.push_back({
        .label = ViewLabel(view),
        .color_rgb = kViewColor,
        .cost = ToCost(view.time),
        .items = {},
      });
    }
  }
  if (!summary.frame_work.empty()) {
    frame.segments.push_back({
      .label = "Shared",
      .color_rgb
      = vortex::GetGpuWorkBucketInfo(vortex::GpuWorkBucket::kBackground)
        .color.Rgb24(),
      .cost = ToCost(summary.frame_work_time),
      .items = {},
    });
  }
  frame.segments.push_back({
    .label = "GPU idle",
    .color_rgb = kIdleColor,
    .cost = ToCost(summary.idle),
    .tooltip = "No command list ran: the GPU waited for the CPU or for "
               "presentation",
    .items = {},
  });
  chart_.rows.push_back(std::move(frame));

  for (const auto& view : summary.views) {
    if (&view == main) {
      continue;
    }
    auto row = imgui::GpuWorkChartRow {
      .label = ViewLabel(view),
      .total = ToCost(view.time),
      .segments = {},
      .default_open = false,
    };
    for (const auto& group : view.groups) {
      row.segments.push_back(ToSegment(group));
    }
    chart_.rows.push_back(std::move(row));
  }

  if (!summary.frame_work.empty()) {
    auto row = imgui::GpuWorkChartRow {
      .label = "Shared",
      .total = ToCost(summary.frame_work_time),
      .segments = {},
      .default_open = true,
    };
    for (const auto& group : summary.frame_work) {
      row.segments.push_back(ToSegment(group));
    }
    chart_.rows.push_back(std::move(row));
  }

  chart_.history_ms = summary.frame_gpu_history_ms;
  chart_.footer = fmt::format("{} valid, {} invalid frames in {:.0f} ms",
    summary.valid_frames, summary.invalid_frames, summary.window_ms);
}

} // namespace oxygen::host
