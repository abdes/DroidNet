//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>

namespace oxygen::vortex {

BasePassModule::BasePassModule(Renderer& renderer)
  : renderer_(renderer)
{
}

BasePassModule::~BasePassModule() = default;

void BasePassModule::Execute(RenderContext& ctx, SceneTextures& scene_textures)
{
  // Phase 03-07 establishes the Stage 9 module boundary only. Draw-command
  // generation, shader selection, and velocity completion land in later plans.
  static_cast<void>(renderer_);
  static_cast<void>(ctx);
  static_cast<void>(scene_textures);
}

void BasePassModule::SetConfig(const BasePassConfig& config)
{
  config_ = config;
}

} // namespace oxygen::vortex
