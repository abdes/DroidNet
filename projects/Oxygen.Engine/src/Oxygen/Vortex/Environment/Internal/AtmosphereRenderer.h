//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Vortex/Environment/Passes/AtmosphereComposePass.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace environment {

class AtmosphereRenderer {
public:
  using RenderState = AtmosphereComposePass::RecordState;

  OXGN_VRTX_API explicit AtmosphereRenderer(Renderer& renderer);
  OXGN_VRTX_API ~AtmosphereRenderer();

  AtmosphereRenderer(const AtmosphereRenderer&) = delete;
  auto operator=(const AtmosphereRenderer&) -> AtmosphereRenderer& = delete;
  AtmosphereRenderer(AtmosphereRenderer&&) = delete;
  auto operator=(AtmosphereRenderer&&) -> AtmosphereRenderer& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Render(
    RenderContext& ctx, const SceneTextures& scene_textures) const -> RenderState;

private:
  std::unique_ptr<AtmosphereComposePass> pass_;
};

} // namespace environment
} // namespace oxygen::vortex
