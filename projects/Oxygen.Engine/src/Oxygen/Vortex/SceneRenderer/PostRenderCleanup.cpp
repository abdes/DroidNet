//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>

namespace oxygen::vortex {

void SceneRenderer::PostRenderCleanup(RenderContext& /*ctx*/)
{
  const auto scene_depth_ready
    = scene_texture_extracts_.resolved_scene_depth.valid
    && scene_texture_extracts_.resolved_scene_depth.texture != nullptr;
  scene_texture_extracts_.prev_scene_depth = {
    .texture = scene_depth_ready
      ? EnsureArtifactTexture(prev_scene_depth_artifact_, "PrevSceneDepth",
          scene_textures_.GetSceneDepth())
      : nullptr,
    .valid = scene_depth_ready,
  };

  const auto* velocity_texture = ResolveVelocitySourceTexture();
  const auto velocity_ready
    = setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneVelocity)
    && scene_texture_bindings_.velocity_srv
      != SceneTextureBindings::kInvalidIndex
    && velocity_texture != nullptr;
  scene_texture_extracts_.prev_velocity = {
    .texture = velocity_ready ? EnsureArtifactTexture(prev_velocity_artifact_,
                                  "PrevVelocity", *velocity_texture)
                              : nullptr,
    .valid = velocity_ready,
  };

  ApplyStage23ExtractionState();
}

} // namespace oxygen::vortex
