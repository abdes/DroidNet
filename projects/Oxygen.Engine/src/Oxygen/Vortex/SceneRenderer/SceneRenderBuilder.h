//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <glm/vec2.hpp>

#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::vortex {

class Renderer;
class SceneRenderer;

class SceneRenderBuilder {
public:
  OXGN_VRTX_NDAPI static auto Build(Renderer& renderer, Graphics& gfx,
    CapabilitySet capabilities, glm::uvec2 initial_viewport_extent)
    -> std::unique_ptr<SceneRenderer>;
};

} // namespace oxygen::vortex
