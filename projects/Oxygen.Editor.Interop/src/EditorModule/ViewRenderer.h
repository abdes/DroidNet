//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/ViewResolver.h>

namespace oxygen::engine {
class Renderer;
} // namespace oxygen::engine

namespace oxygen::graphics {
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::interop::module {

class RenderGraph;

class ViewRenderer {
public:
  ViewRenderer();
  ~ViewRenderer();

  OXYGEN_MAKE_NON_COPYABLE(ViewRenderer)
  OXYGEN_MAKE_NON_MOVABLE(ViewRenderer)

  // Register this view with the engine's renderer module
  void RegisterWithEngine(engine::Renderer& renderer, ViewId view_id, engine::ViewResolver resolver);

  // Unregister this view from the engine's renderer module
  void UnregisterFromEngine(engine::Renderer& renderer);

  // Configure render passes (called during OnPreRender)
  void Configure();

  // Set the framebuffer for rendering
  void SetFramebuffer(std::shared_ptr<graphics::Framebuffer> fb);

private:
  ViewId view_id_ {};
  bool registered_ { false };
  std::unique_ptr<RenderGraph> render_graph_;
};

} // namespace oxygen::interop::module
