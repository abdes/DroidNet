//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

struct DepthPrepassConfig {
  DepthPrePassMode mode { DepthPrePassMode::kOpaqueAndMasked };
  bool write_velocity { true };
};

class DepthPrepassModule {
public:
  OXGN_VRTX_API explicit DepthPrepassModule(Renderer& renderer);
  OXGN_VRTX_API ~DepthPrepassModule();

  DepthPrepassModule(const DepthPrepassModule&) = delete;
  auto operator=(const DepthPrepassModule&) -> DepthPrepassModule& = delete;
  DepthPrepassModule(DepthPrepassModule&&) = delete;
  auto operator=(DepthPrepassModule&&) -> DepthPrepassModule& = delete;

  OXGN_VRTX_API void Execute(RenderContext& ctx, SceneTextures& scene_textures);
  OXGN_VRTX_API void SetConfig(const DepthPrepassConfig& config);

  [[nodiscard]] OXGN_VRTX_API auto GetCompleteness() const
    -> DepthPrePassCompleteness;

private:
  Renderer& renderer_;
  DepthPrepassConfig config_ {};
  DepthPrePassCompleteness completeness_ { DepthPrePassCompleteness::kDisabled };
};

} // namespace oxygen::vortex
