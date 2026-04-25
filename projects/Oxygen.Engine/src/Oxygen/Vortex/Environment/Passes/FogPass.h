//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace environment {

class FogPass {
public:
  struct RecordState {
    bool requested { false };
    bool executed { false };
    std::uint32_t draw_count { 0U };
    bool bound_scene_color { false };
    bool sampled_scene_depth { false };
  };

  OXGN_VRTX_API explicit FogPass(Renderer& renderer);
  OXGN_VRTX_API ~FogPass();

  FogPass(const FogPass&) = delete;
  auto operator=(const FogPass&) -> FogPass& = delete;
  FogPass(FogPass&&) = delete;
  auto operator=(FogPass&&) -> FogPass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Record(
    RenderContext& ctx, const SceneTextures& scene_textures) const -> RecordState;

private:
  Renderer& renderer_;
};

} // namespace environment
} // namespace oxygen::vortex
