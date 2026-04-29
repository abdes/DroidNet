//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

class OcclusionModule {
public:
  OXGN_VRTX_API explicit OcclusionModule(
    Renderer& renderer, OcclusionConfig config = {});
  OXGN_VRTX_API ~OcclusionModule();

  OXYGEN_MAKE_NON_COPYABLE(OcclusionModule)
  OXYGEN_MAKE_NON_MOVABLE(OcclusionModule)

  OXGN_VRTX_API void SetConfig(OcclusionConfig config) noexcept;
  [[nodiscard]] OXGN_VRTX_API auto GetConfig() const noexcept
    -> const OcclusionConfig&;

  OXGN_VRTX_API void Execute(RenderContext& ctx, SceneTextures& scene_textures);

  [[nodiscard]] OXGN_VRTX_API auto GetCurrentResults() const noexcept
    -> const OcclusionFrameResults&;
  [[nodiscard]] OXGN_VRTX_API auto GetStats() const noexcept
    -> const OcclusionStats&;

private:
  struct Impl;

  Renderer& renderer_;
  OcclusionConfig config_ {};
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex
