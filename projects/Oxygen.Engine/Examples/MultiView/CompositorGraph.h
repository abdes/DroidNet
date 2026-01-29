//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen {
namespace graphics {
  class CommandRecorder;
  class Framebuffer;
  class Texture;
} // namespace graphics
} // namespace oxygen

namespace oxygen::examples::multiview {

class DemoView;

//! Compositing graph for multi-view output.
/*!
 Encapsulates compositing logic as a render-graph stage. The graph accepts a
 view list and renders their outputs into the swapchain backbuffer, then applies
 GUI passes after compositing.
*/
class CompositorGraph {
public:
  struct Inputs {
    std::span<DemoView* const> views;
    graphics::CommandRecorder& recorder;
    graphics::Texture& backbuffer;
    const graphics::Framebuffer& backbuffer_framebuffer;
  };

  CompositorGraph() = default;
  ~CompositorGraph() = default;

  OXYGEN_MAKE_NON_COPYABLE(CompositorGraph)
  OXYGEN_DEFAULT_MOVABLE(CompositorGraph)

  auto Execute(const Inputs& inputs) const -> co::Co<>;
};

} // namespace oxygen::examples::multiview
