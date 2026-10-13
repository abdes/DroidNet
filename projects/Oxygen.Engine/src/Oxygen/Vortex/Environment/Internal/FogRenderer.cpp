//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Environment/Internal/FogRenderer.h>
#include <Oxygen/Vortex/Environment/Passes/FogPass.h>

namespace oxygen::vortex::environment {

FogRenderer::FogRenderer(Renderer& renderer)
  : pass_(std::make_unique<FogPass>(renderer))
{
}

FogRenderer::~FogRenderer() = default;

auto FogRenderer::WillRender(const RenderContext& ctx) const -> bool
{
  return pass_->WillExecute(ctx);
}

auto FogRenderer::Render(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
  const ShaderVisibleIndex local_fog_constants) const -> RenderState
{
  return pass_->Record(ctx, recorder, scene_textures, local_fog_constants);
}

} // namespace oxygen::vortex::environment
