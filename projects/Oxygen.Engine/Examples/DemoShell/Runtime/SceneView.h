//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/Runtime/DemoView.h"

namespace oxygen::examples {

//! Standard implementation of a DemoView that renders a 3D scene from a
//! specific camera.
class SceneView : public DemoView {
public:
  explicit SceneView(scene::SceneNode camera)
    : camera_(std::move(camera))
  {
  }

  [[nodiscard]] auto GetCamera() const
    -> std::optional<scene::SceneNode> override
  {
    return camera_;
  }

  [[nodiscard]] auto GetViewport() const -> std::optional<ViewPort> override
  {
    // By default, fill the surface
    return std::nullopt;
  }

  void SetCamera(scene::SceneNode camera) { camera_ = std::move(camera); }

private:
  scene::SceneNode camera_;
};

} // namespace oxygen::examples
