//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

//! Configuration for the axes widget
struct AxesWidgetConfig {
  //! Enable rendering the widget.
  bool show_widget { true };
  //! Size of the widget in pixels (width and height)
  float size { 80.0F };

  //! Padding from screen edges in pixels
  float padding { 10.0F };

  //! Length of each axis line (relative to widget size, 0.0-0.5)
  float axis_length { 0.35F };

  //! Thickness of axis lines in pixels
  float line_thickness { 2.0F };

  //! Show axis labels (X, Y, Z)
  bool show_labels { true };
};

//! Draws a 3D axes indicator showing camera orientation
/*!
 Renders a small widget in the corner of the screen displaying the X, Y, Z
 axes as they appear from the current camera view. The widget updates in
 real-time as the camera rotates, providing a visual reference for scene
 orientation.

 ### Features

 - Color-coded axes: X (Red), Y (Green), Z (Blue)
 - Positioned at bottom-left corner of the screen
 - Depth-sorted so nearer axes draw on top
 - Optional axis labels

 ### Usage

 ```cpp
 AxesWidget axes_widget;
 axes_widget.SetConfig({ .size = 100.0F, .show_labels = true });

 // In ImGui update loop
 glm::mat4 view_matrix = camera.GetViewMatrix();
 axes_widget.Draw(view_matrix);
 ```

 @see AxesWidgetConfig
 */
class AxesWidget {
public:
  AxesWidget() = default;
  ~AxesWidget() = default;

  //! Set widget configuration
  void SetConfig(const AxesWidgetConfig& config) { config_ = config; }

  //! Set widget visibility.
  void SetVisible(bool visible) { config_.show_widget = visible; }

  //! Get widget visibility.
  [[nodiscard]] auto IsVisible() const -> bool { return config_.show_widget; }

  //! Get current widget configuration
  [[nodiscard]] auto GetConfig() const -> const AxesWidgetConfig&
  {
    return config_;
  }

  //! Draw the axes widget
  /*!
    Renders the 3D axes indicator based on the current camera view matrix.
    The widget is positioned at the bottom-left corner of the main viewport.

    @param view_matrix The camera's view matrix (world-to-view transform)
    @note Must be called within ImGui rendering context
   */
  void Draw(const glm::mat4& view_matrix);

  //! Draw the axes widget using the provided camera.
  void Draw(observer_ptr<oxygen::scene::SceneNode> camera);

private:
  //! Project a 3D direction to 2D widget space
  [[nodiscard]] auto ProjectAxis(const glm::vec3& axis_dir,
    const glm::mat4& view_matrix, const glm::vec2& center) const -> glm::vec2;

  AxesWidgetConfig config_;
};

} // namespace oxygen::examples::ui
