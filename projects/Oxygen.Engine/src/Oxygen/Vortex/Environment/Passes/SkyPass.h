//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace environment {

  class SkyPass {
  public:
    struct RecordState {
      bool requested { false };
      bool executed { false };
      std::uint32_t draw_count { 0U };
      bool bound_scene_color { false };
      bool sampled_scene_depth { false };
    };

    OXGN_VRTX_API explicit SkyPass(Renderer& renderer);
    OXGN_VRTX_API ~SkyPass();

    SkyPass(const SkyPass&) = delete;
    auto operator=(const SkyPass&) -> SkyPass& = delete;
    SkyPass(SkyPass&&) = delete;
    auto operator=(SkyPass&&) -> SkyPass& = delete;

    //! `local_fog_constants` names the local-fog compose constants when local
    //! fog is injected into volumetric fog: the pass then traces local fog
    //! beyond the volume. Invalid otherwise.
    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
      ShaderVisibleIndex local_fog_constants = kInvalidShaderVisibleIndex) const
      -> RecordState;

  private:
    Renderer& renderer_;
  };

} // namespace environment
} // namespace oxygen::vortex
