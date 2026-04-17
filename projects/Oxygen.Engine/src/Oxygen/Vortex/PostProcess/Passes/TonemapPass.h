//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace postprocess {

class TonemapPass {
public:
  struct Inputs {
    const graphics::Texture* scene_signal { nullptr };
    observer_ptr<graphics::Framebuffer> post_target;
    float exposure_value { 1.0F };
    float bloom_intensity { 0.0F };
  };

  struct ExecutionState {
    bool requested { false };
    bool executed { false };
    bool wrote_visible_output { false };
  };

  OXGN_VRTX_API explicit TonemapPass(Renderer& renderer);
  OXGN_VRTX_API ~TonemapPass();

  TonemapPass(const TonemapPass&) = delete;
  auto operator=(const TonemapPass&) -> TonemapPass& = delete;
  TonemapPass(TonemapPass&&) = delete;
  auto operator=(TonemapPass&&) -> TonemapPass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Record(
    RenderContext& ctx, const SceneTextures& scene_textures, const Inputs& inputs)
    const -> ExecutionState;

private:
  Renderer& renderer_;
};

} // namespace postprocess

} // namespace oxygen::vortex
