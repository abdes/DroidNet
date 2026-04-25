//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/SkyRenderer.h>

#include <Oxygen/Vortex/Environment/Passes/SkyPass.h>

namespace oxygen::vortex::environment {

SkyRenderer::SkyRenderer(Renderer& renderer)
  : pass_(std::make_unique<SkyPass>(renderer))
{
}

SkyRenderer::~SkyRenderer() = default;

auto SkyRenderer::Render(
  RenderContext& ctx, const SceneTextures& scene_textures) const -> RenderState
{
  return pass_->Record(ctx, scene_textures);
}

} // namespace oxygen::vortex::environment
