//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/ext/matrix_float4x4.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::ui {

class UiSettingsVm;

//! Draws a 3D axes indicator showing the current camera view orientation.
/*!
 Renders a small widget in the corner of the screen displaying the X, Y, Z
 axes as they appear from the current camera view. The widget updates in
 real-time as the camera rotates, providing a visual reference for scene
 orientation.

 ### Features

 - Color-coded positive axes: +X (Red), +Y (Green), +Z (Blue)
 - Positioned at bottom-left corner of the screen
 - Depth-sorted so nearer axes draw on top
 - Optional axis labels

 ### Usage

 ```cpp
 AxesWidget axes_widget(settings_vm);

 // In ImGui update loop
 axes_widget.Draw(observer_ptr { &camera_node });
 ```
 */
class AxesWidget {
public:
  explicit AxesWidget(observer_ptr<UiSettingsVm> settings_vm);
  ~AxesWidget() = default;

  //! Draw the axes widget using the provided camera.
  void Draw(observer_ptr<scene::SceneNode> camera);

private:
  void Draw(const glm::mat3& world_to_view_rotation);

  observer_ptr<UiSettingsVm> settings_vm_ { nullptr };
};

} // namespace oxygen::examples::ui
