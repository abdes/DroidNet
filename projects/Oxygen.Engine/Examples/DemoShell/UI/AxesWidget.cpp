//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/UI/AxesWidget.h"

#include <algorithm>
#include <array>

#include <imgui.h>

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

namespace oxygen::examples::render_scene::ui {

namespace {

  // Axis colors: X=Red, Y=Green, Z=Blue (matching standard conventions)
  constexpr ImU32 kAxisColorX = IM_COL32(230, 60, 60, 255);
  constexpr ImU32 kAxisColorY = IM_COL32(60, 180, 60, 255);
  constexpr ImU32 kAxisColorZ = IM_COL32(60, 100, 230, 255);

  // Slightly dimmed versions for back-facing axes
  constexpr ImU32 kAxisColorXDim = IM_COL32(140, 50, 50, 180);
  constexpr ImU32 kAxisColorYDim = IM_COL32(50, 110, 50, 180);
  constexpr ImU32 kAxisColorZDim = IM_COL32(50, 70, 140, 180);

  // Label text colors (white with slight transparency)
  constexpr ImU32 kLabelColor = IM_COL32(255, 255, 255, 220);

  struct AxisInfo {
    glm::vec3 direction;
    glm::vec2 screen_end;
    ImU32 color;
    ImU32 color_dim;
    const char* label;
    float depth; // For sorting (higher = nearer to camera)
  };

} // namespace

auto AxesWidget::ProjectAxis(const glm::vec3& axis_dir,
  const glm::mat4& view_matrix, const glm::vec2& center) const -> glm::vec2
{
  // Extract the rotation part of the view matrix (upper-left 3x3)
  // This transforms world axes to view space
  const glm::mat3 rotation(view_matrix);

  // Transform the axis direction to view space
  const glm::vec3 view_axis = rotation * axis_dir;

  // Project to 2D: view space X maps to screen X, view space Y maps to screen
  // Y (inverted because screen Y grows downward)
  const float axis_pixel_length = config_.size * config_.axis_length;

  return {
    center.x + view_axis.x * axis_pixel_length,
    center.y - view_axis.y * axis_pixel_length, // Y inverted for screen coords
  };
}

/*!
 Renders a 3D coordinate axes indicator in the bottom-left corner of the
 viewport. Each axis is color-coded (X=Red, Y=Green, Z=Blue) and the widget
 updates in real-time based on the camera's view matrix.

 The axes are depth-sorted so that axes pointing toward the camera are drawn
 on top of those pointing away. Axes pointing away from the camera are drawn
 with dimmed colors to provide visual depth cues.

 @param view_matrix The camera's view matrix used to determine axis
                    orientations. Only the rotation component is used.
 */
void AxesWidget::Draw(const glm::mat4& view_matrix)
{
  // Get main viewport and calculate widget position (bottom-left corner)
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const glm::vec2 widget_pos {
    viewport->WorkPos.x + config_.padding,
    viewport->WorkPos.y + viewport->WorkSize.y - config_.size - config_.padding,
  };

  // Center of the widget where axes originate
  const glm::vec2 center {
    widget_pos.x + config_.size * 0.5F,
    widget_pos.y + config_.size * 0.5F,
  };

  // Extract view rotation for depth calculation
  const glm::mat3 rotation(view_matrix);

  // Define the three world axes
  std::array<AxisInfo, 3> axes = { {
    {
      .direction = { 1.0F, 0.0F, 0.0F },
      .screen_end = ProjectAxis({ 1.0F, 0.0F, 0.0F }, view_matrix, center),
      .color = kAxisColorX,
      .color_dim = kAxisColorXDim,
      .label = "X",
      .depth = (rotation * glm::vec3(1.0F, 0.0F, 0.0F)).z,
    },
    {
      .direction = { 0.0F, 1.0F, 0.0F },
      .screen_end = ProjectAxis({ 0.0F, 1.0F, 0.0F }, view_matrix, center),
      .color = kAxisColorY,
      .color_dim = kAxisColorYDim,
      .label = "Y",
      .depth = (rotation * glm::vec3(0.0F, 1.0F, 0.0F)).z,
    },
    {
      .direction = { 0.0F, 0.0F, 1.0F },
      .screen_end = ProjectAxis({ 0.0F, 0.0F, 1.0F }, view_matrix, center),
      .color = kAxisColorZ,
      .color_dim = kAxisColorZDim,
      .label = "Z",
      .depth = (rotation * glm::vec3(0.0F, 0.0F, 1.0F)).z,
    },
  } };

  // Sort axes by depth (draw furthest first, nearest last)
  std::sort(
    axes.begin(), axes.end(), [](const AxisInfo& lhs, const AxisInfo& rhs) {
      return lhs.depth < rhs.depth;
    });

  // Get foreground draw list to draw on top of other UI
  ImDrawList* draw_list = ImGui::GetForegroundDrawList();

  // Draw a subtle background circle
  draw_list->AddCircleFilled(ImVec2(center.x, center.y), config_.size * 0.45F,
    IM_COL32(30, 30, 30, 150), 32);
  draw_list->AddCircle(ImVec2(center.x, center.y), config_.size * 0.45F,
    IM_COL32(80, 80, 80, 200), 32, 1.0F);

  // Draw each axis line
  for (const auto& axis : axes) {
    // Use dimmed color if axis points away from camera (negative depth)
    const ImU32 color = (axis.depth >= 0.0F) ? axis.color : axis.color_dim;
    const float thickness = (axis.depth >= 0.0F)
      ? config_.line_thickness
      : config_.line_thickness * 0.7F;

    // Draw the axis line from center to projected endpoint
    draw_list->AddLine(ImVec2(center.x, center.y),
      ImVec2(axis.screen_end.x, axis.screen_end.y), color, thickness);

    // Draw a small arrow head
    const glm::vec2 dir = axis.screen_end - center;
    const float dir_len = glm::length(dir);
    if (dir_len > 1.0F) {
      const glm::vec2 dir_norm = dir / dir_len;
      const glm::vec2 perp(-dir_norm.y, dir_norm.x);
      constexpr float kArrowSize = 4.0F;

      const ImVec2 tip(axis.screen_end.x, axis.screen_end.y);
      const ImVec2 left(axis.screen_end.x - dir_norm.x * kArrowSize
          + perp.x * kArrowSize * 0.5F,
        axis.screen_end.y - dir_norm.y * kArrowSize
          + perp.y * kArrowSize * 0.5F);
      const ImVec2 right(axis.screen_end.x - dir_norm.x * kArrowSize
          - perp.x * kArrowSize * 0.5F,
        axis.screen_end.y - dir_norm.y * kArrowSize
          - perp.y * kArrowSize * 0.5F);

      draw_list->AddTriangleFilled(tip, left, right, color);
    }

    // Draw axis label if enabled
    if (config_.show_labels && axis.depth >= -0.3F) {
      // Position label slightly beyond the axis endpoint
      const glm::vec2 label_offset = (dir_len > 1.0F)
        ? (axis.screen_end - center) / dir_len * 12.0F
        : glm::vec2(0.0F);

      const ImVec2 label_pos(axis.screen_end.x + label_offset.x - 3.0F,
        axis.screen_end.y + label_offset.y - 6.0F);

      draw_list->AddText(label_pos, kLabelColor, axis.label);
    }
  }
}

} // namespace oxygen::examples::render_scene::ui
