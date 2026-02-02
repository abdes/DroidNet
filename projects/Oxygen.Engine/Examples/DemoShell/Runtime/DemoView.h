//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples {

//! Abstract interface representing a single renderable view in the demo.
/*!
  A DemoView defines *what* to render (Camera, Content) and simple configuration
  (Viewport, Clear Color). It does NOT define *how* to render it (Passes,
  Graphs). The RenderingPipeline is responsible for the execution strategy.
*/
class DemoView {
public:
  virtual ~DemoView() = default;

  OXYGEN_MAKE_NON_COPYABLE(DemoView)
  OXYGEN_DEFAULT_MOVABLE(DemoView)

  DemoView() = default;

  //! Returns the camera node for this view.
  //! If null, the view may be 2D only or use a default camera.
  [[nodiscard]] virtual auto GetCamera() const
    -> std::optional<scene::SceneNode>
    = 0;

  //! Returns the viewport for this view.
  //! Default implementation returns an invalid (full surface) viewport.
  [[nodiscard]] virtual auto GetViewport() const -> std::optional<ViewPort>
  {
    return std::nullopt;
  }

  //! Optional hook to draw debug overlays or UI (ImGui) on top of the view.
  //! Called by the pipeline after the main scene rendering.
  virtual auto OnOverlay(graphics::CommandRecorder& /*recorder*/) -> void { }

  //! Returns true if this view requires a clear before rendering.
  [[nodiscard]] virtual auto ShouldClear() const -> bool { return true; }

  //! Gets the ViewId for the current frame.
  [[nodiscard]] auto GetViewId() const noexcept -> ViewId
  {
    return current_view_id_;
  }

  //! Sets the ViewId for the current frame.
  auto SetViewId(ViewId id) noexcept -> void { current_view_id_ = id; }

  //! Returns true if render graph has been registered with Renderer.
  [[nodiscard]] auto IsRendererRegistered() const noexcept -> bool
  {
    return renderer_registered_;
  }

  //! Marks the view as registered with the Renderer's render graph system.
  auto SetRendererRegistered(bool registered) noexcept -> void
  {
    renderer_registered_ = registered;
  }

private:
  ViewId current_view_id_ { kInvalidViewId };
  bool renderer_registered_ { false };
};

} // namespace oxygen::examples
