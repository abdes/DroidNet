//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Oxygen/ImGui/api_export.h>

namespace oxygen::imgui {

//! A cost in milliseconds: the average and the largest single sample.
struct GpuWorkChartCost {
  float avg_ms { 0.0F };
  float max_ms { 0.0F };
};

struct GpuWorkChartItem {
  std::string label;
  GpuWorkChartCost cost;
};

//! One coloured part of a row's bar, listed in the legend with its items.
struct GpuWorkChartSegment {
  std::string label;
  //! 0xRRGGBB.
  std::uint32_t color_rgb { 0x808080U };
  GpuWorkChartCost cost;
  //! Shown on hover of the legend entry; may be empty.
  std::string tooltip;
  //! Shown when the segment's legend entry is expanded.
  std::vector<GpuWorkChartItem> items;
};

//! A stacked bar of `total` split into its segments, and their legend.
struct GpuWorkChartRow {
  //! Unique within the chart; also the expander label.
  std::string label;
  GpuWorkChartCost total;
  std::vector<GpuWorkChartSegment> segments;
  bool default_open { true };
};

struct GpuWorkChartData {
  //! Marker on every bar; zero hides it.
  float budget_ms { 0.0F };
  std::vector<GpuWorkChartRow> rows;
  //! Totals of recent frames, newest last; empty hides the sparkline.
  std::vector<float> history_ms;
  //! Dim text under the chart.
  std::string footer;
};

//! Draws GPU work in the current window: per row a stacked bar with a budget
//! marker and a legend in the bar's order (ms, % of the row, max), each entry
//! expandable to its items; then a sparkline of recent totals.
OXGN_IMGUI_API auto DrawGpuWorkChart(const GpuWorkChartData& data) -> void;

} // namespace oxygen::imgui
