//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;
class BasePassMeshProcessor;

struct BasePassConfig {
  bool write_velocity { true };
  bool early_z_pass_done { true };
  ShadingMode shading_mode { ShadingMode::kDeferred };
};

class BasePassModule {
public:
  OXGN_VRTX_API explicit BasePassModule(Renderer& renderer);
  OXGN_VRTX_API ~BasePassModule();

  BasePassModule(const BasePassModule&) = delete;
  auto operator=(const BasePassModule&) -> BasePassModule& = delete;
  BasePassModule(BasePassModule&&) = delete;
  auto operator=(BasePassModule&&) -> BasePassModule& = delete;

  OXGN_VRTX_API void Execute(RenderContext& ctx, SceneTextures& scene_textures);
  OXGN_VRTX_API void SetConfig(const BasePassConfig& config);
  [[nodiscard]] OXGN_VRTX_API auto HasPublishedBasePassProducts() const -> bool;

private:
  Renderer& renderer_;
  BasePassConfig config_ {};
  bool has_published_base_pass_products_ { false };
  std::unique_ptr<BasePassMeshProcessor> mesh_processor_;
};

} // namespace oxygen::vortex
