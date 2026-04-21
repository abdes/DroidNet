//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>

#include <imgui.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>

#include "DemoShell/UI/AxesWidget.h"
#include "DemoShell/UI/UiSettingsVm.h"

namespace oxygen::examples::ui {

namespace {

  struct AxesWidgetConfig {
    float size { 80.0F };
    float padding { 10.0F };
    float axis_length { 0.35F };
    float line_thickness { 2.0F };
    bool show_labels { true };
  };

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

  [[nodiscard]] auto ProjectAxis(const glm::vec3& axis_dir,
    const glm::mat3& world_to_view_rotation, const glm::vec2& center,
    const AxesWidgetConfig& config) -> glm::vec2
  {
    const glm::vec3 view_axis = world_to_view_rotation * axis_dir;
    const float axis_pixel_length = config.size * config.axis_length;

    return {
      center.x + view_axis.x * axis_pixel_length,
      center.y - view_axis.y * axis_pixel_length,
    };
  }

} // namespace

AxesWidget::AxesWidget(observer_ptr<UiSettingsVm> settings_vm)
  : settings_vm_(settings_vm)
{
  DCHECK_NOTNULL_F(settings_vm, "AxesWidget requires UiSettingsVm");
}

/*!
 Renders a 3D coordinate axes indicator in the bottom-left corner of the
 viewport. Each axis is color-coded (+X=Red, +Y=Green, +Z=Blue) and the widget
 updates in real-time based on the active camera's world-space view
 orientation.

 The axes are depth-sorted so that axes pointing toward the camera are drawn
 on top of those pointing away. Axes pointing away from the camera are drawn
 with dimmed colors to provide visual depth cues.

 @param world_to_view_rotation Rotation mapping world-space directions into the
                               camera's current view basis. Only orientation is
                               used; translation is irrelevant for the widget.
 */
void AxesWidget::Draw(const glm::mat3& world_to_view_rotation)
{
  if (!settings_vm_->GetAxesVisible()) {
    return;
  }

  // Get main viewport and calculate widget position (bottom-left corner)
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  constexpr AxesWidgetConfig config {};
  const glm::vec2 widget_pos {
    viewport->WorkPos.x + config.padding,
    viewport->WorkPos.y + viewport->WorkSize.y - config.size - config.padding,
  };

  // Center of the widget where axes originate
  const glm::vec2 center {
    widget_pos.x + config.size * 0.5F,
    widget_pos.y + config.size * 0.5F,
  };

  // Define the positive world axes under Oxygen engine law:
  // +X = Right, +Y = Back, +Z = Up. The widget must visualize these axes as
  // they appear in the current camera view, not the movement "forward" axis.
  std::array<AxisInfo, 3> axes = { {
    {
      .direction = space::move::Right,
      .screen_end
      = ProjectAxis(space::move::Right, world_to_view_rotation, center, config),
      .color = kAxisColorX,
      .color_dim = kAxisColorXDim,
      .label = "X",
      .depth = (world_to_view_rotation * space::move::Right).z,
    },
    {
      .direction = space::move::Back,
      .screen_end
      = ProjectAxis(space::move::Back, world_to_view_rotation, center, config),
      .color = kAxisColorY,
      .color_dim = kAxisColorYDim,
      .label = "Y",
      .depth = (world_to_view_rotation * space::move::Back).z,
    },
    {
      .direction = space::move::Up,
      .screen_end
      = ProjectAxis(space::move::Up, world_to_view_rotation, center, config),
      .color = kAxisColorZ,
      .color_dim = kAxisColorZDim,
      .label = "Z",
      .depth = (world_to_view_rotation * space::move::Up).z,
    },
  } };

  // Sort axes by depth (draw furthest first, nearest last)
  std::ranges::sort(axes, [](const AxisInfo& lhs, const AxisInfo& rhs) {
    return lhs.depth < rhs.depth;
  });

  // Get foreground draw list to draw on top of other UI
  ImDrawList* draw_list = ImGui::GetForegroundDrawList();

  // Draw a subtle background circle
  draw_list->AddCircleFilled(ImVec2(center.x, center.y), config.size * 0.45F,
    IM_COL32(30, 30, 30, 150), 32);
  draw_list->AddCircle(ImVec2(center.x, center.y), config.size * 0.45F,
    IM_COL32(80, 80, 80, 200), 32, 1.0F);

  // Draw each axis line
  for (const auto& axis : axes) {
    // Use dimmed color if axis points away from camera (negative depth)
    const ImU32 color = axis.depth >= 0.0F ? axis.color : axis.color_dim;
    const float thickness = axis.depth >= 0.0F ? config.line_thickness
                                               : config.line_thickness * 0.7F;

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
    if (axis.depth >= -0.3F) {
      // Position label slightly beyond the axis endpoint
      const glm::vec2 label_offset = dir_len > 1.0F
        ? (axis.screen_end - center) / dir_len * 12.0F
        : glm::vec2(0.0F);

      const ImVec2 label_pos(axis.screen_end.x + label_offset.x - 3.0F,
        axis.screen_end.y + label_offset.y - 6.0F);

      draw_list->AddText(label_pos, kLabelColor, axis.label);
    }
  }
}

void AxesWidget::Draw(observer_ptr<scene::SceneNode> camera)
{
  if (!camera || !camera->IsAlive()) {
    return;
  }

  const auto& tf = camera->GetTransform();
  const auto world_rotation = tf.GetWorldRotation();
  if (!world_rotation.has_value()) {
    return;
  }

  // Camera rotations in DemoShell/controllers map view-space basis vectors
  // (look::Forward = -Z, look::Up = +Y) into world space. The widget needs the
  // inverse mapping so it can express world axes in the exact basis currently
  // seen on screen.
  const glm::mat3 world_to_view_rotation
    = glm::mat3_cast(glm::conjugate(*world_rotation));
  Draw(world_to_view_rotation);
}

} // namespace oxygen::examples::ui
