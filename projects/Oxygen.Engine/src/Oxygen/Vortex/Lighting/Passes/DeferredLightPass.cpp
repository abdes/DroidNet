//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Lighting/Passes/DeferredLightPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex::lighting {

DeferredLightPass::DeferredLightPass(Renderer& renderer)
  : renderer_(renderer)
{
}

auto DeferredLightPass::Record(RenderContext& ctx,
  const SceneTextures& scene_textures,
  const internal::DeferredLightPacketSet& packets) -> ExecutionState
{
  static_cast<void>(renderer_);
  static_cast<void>(ctx);
  static_cast<void>(scene_textures);

  return {
    .consumed_packets = packets.directional.has_value()
      || !packets.local_lights.empty(),
    .accumulated_into_scene_color = packets.directional.has_value()
      || !packets.local_lights.empty(),
    .used_service_owned_geometry = !packets.local_lights.empty(),
    .directional_draw_count = packets.directional.has_value() ? 1U : 0U,
    .local_light_draw_count
    = static_cast<std::uint32_t>(packets.local_lights.size()),
  };
}

} // namespace oxygen::vortex::lighting
