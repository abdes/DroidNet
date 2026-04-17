//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/AtmosphereComposePass.h>

#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace oxygen::vortex::environment {

AtmosphereComposePass::AtmosphereComposePass(Renderer& renderer)
  : renderer_(renderer)
{
}

AtmosphereComposePass::~AtmosphereComposePass() = default;

auto AtmosphereComposePass::Record(
  RenderContext& ctx, const SceneTextures& /*scene_textures*/) const -> RecordState
{
  const auto* view = ctx.GetCurrentCompositionView();
  const auto requested = ctx.current_view.view_id != kInvalidViewId
    && (view == nullptr || view->with_atmosphere);
  return {
    .requested = requested,
    .executed = requested
      && renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting),
  };
}

} // namespace oxygen::vortex::environment
