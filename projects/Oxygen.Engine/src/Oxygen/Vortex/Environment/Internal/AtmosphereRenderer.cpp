//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/AtmosphereRenderer.h>

#include <Oxygen/Vortex/Environment/Passes/AtmosphereComposePass.h>

namespace oxygen::vortex::environment {

AtmosphereRenderer::AtmosphereRenderer(Renderer& renderer)
  : pass_(std::make_unique<AtmosphereComposePass>(renderer))
{
}

AtmosphereRenderer::~AtmosphereRenderer() = default;

auto AtmosphereRenderer::Render(
  RenderContext& ctx, const SceneTextures& scene_textures) const -> RenderState
{
  return pass_->Record(ctx, scene_textures);
}

} // namespace oxygen::vortex::environment
