//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Vortex/Environment/Passes/FogPass.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace environment {

class FogRenderer {
public:
  using RenderState = FogPass::RecordState;

  OXGN_VRTX_API explicit FogRenderer(Renderer& renderer);
  OXGN_VRTX_API ~FogRenderer();

  FogRenderer(const FogRenderer&) = delete;
  auto operator=(const FogRenderer&) -> FogRenderer& = delete;
  FogRenderer(FogRenderer&&) = delete;
  auto operator=(FogRenderer&&) -> FogRenderer& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Render(
    RenderContext& ctx, const SceneTextures& scene_textures) const -> RenderState;

private:
  std::unique_ptr<FogPass> pass_;
};

} // namespace environment
} // namespace oxygen::vortex
