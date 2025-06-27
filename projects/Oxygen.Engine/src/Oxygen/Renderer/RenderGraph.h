//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class RenderController;
} // namespace oxygen::graphics

namespace oxygen::engine {

class RenderPass;
class DepthPrePass;
struct DepthPrePassConfig;

class RenderGraph {
public:
  RenderGraph(std::shared_ptr<graphics::RenderController> render_controller)
    : render_controller_(render_controller)
  {
    // Initialize the render graph with the provided render controller
  }

  virtual auto CreateDepthPrePass(std::shared_ptr<DepthPrePassConfig> config)
    -> std::shared_ptr<RenderPass>;

  // Returns a generic no-op render pass (NullRenderPass).
  OXGN_RNDR_API auto CreateNullRenderPass() -> std::shared_ptr<RenderPass>;

private:
  std::shared_ptr<graphics::RenderController> render_controller_;
};

} // namespace oxygen::engine
