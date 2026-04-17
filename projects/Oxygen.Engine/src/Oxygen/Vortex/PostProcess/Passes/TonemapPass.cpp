//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex::postprocess {

TonemapPass::TonemapPass(Renderer& renderer)
  : renderer_(renderer)
{
}

TonemapPass::~TonemapPass() = default;

auto TonemapPass::Record(RenderContext& ctx,
  const SceneTextures& scene_textures, const Inputs& inputs) const
  -> ExecutionState
{
  static_cast<void>(renderer_);
  static_cast<void>(ctx);
  static_cast<void>(scene_textures);
  static_cast<void>(inputs.exposure_value);
  static_cast<void>(inputs.bloom_intensity);

  return {
    .requested = inputs.scene_signal != nullptr && inputs.post_target != nullptr,
    .executed = false,
    .wrote_visible_output = false,
  };
}

} // namespace oxygen::vortex::postprocess
