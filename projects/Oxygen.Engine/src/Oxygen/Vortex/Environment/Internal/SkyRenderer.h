//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Vortex/Environment/Passes/SkyPass.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace environment {

class SkyRenderer {
public:
  using RenderState = SkyPass::RecordState;

  OXGN_VRTX_API explicit SkyRenderer(Renderer& renderer);
  OXGN_VRTX_API ~SkyRenderer();

  SkyRenderer(const SkyRenderer&) = delete;
  auto operator=(const SkyRenderer&) -> SkyRenderer& = delete;
  SkyRenderer(SkyRenderer&&) = delete;
  auto operator=(SkyRenderer&&) -> SkyRenderer& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Render(
    RenderContext& ctx, const SceneTextures& scene_textures) const -> RenderState;

private:
  std::unique_ptr<SkyPass> pass_;
};

} // namespace environment
} // namespace oxygen::vortex
