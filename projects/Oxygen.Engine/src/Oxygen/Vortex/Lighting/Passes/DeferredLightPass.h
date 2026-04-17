//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.h>

namespace oxygen::vortex {

struct RenderContext;
class SceneTextures;
class Renderer;

namespace lighting {

class DeferredLightPass {
public:
  struct ExecutionState {
    bool consumed_packets { false };
    bool accumulated_into_scene_color { false };
    bool used_service_owned_geometry { false };
    std::uint32_t directional_draw_count { 0U };
    std::uint32_t local_light_draw_count { 0U };
  };

  explicit DeferredLightPass(Renderer& renderer);

  [[nodiscard]] auto Record(RenderContext& ctx,
    const SceneTextures& scene_textures,
    const internal::DeferredLightPacketSet& packets) -> ExecutionState;

private:
  Renderer& renderer_;
};

} // namespace lighting

} // namespace oxygen::vortex
