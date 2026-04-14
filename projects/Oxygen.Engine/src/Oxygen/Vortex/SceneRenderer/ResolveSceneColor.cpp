//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>

namespace oxygen::vortex {

void SceneRenderer::ResolveSceneColor(RenderContext& /*ctx*/)
{
  const auto scene_color_ready
    = setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneColor)
    && scene_texture_bindings_.scene_color_srv
      != SceneTextureBindings::kInvalidIndex;
  scene_texture_extracts_.resolved_scene_color = {
    .texture = scene_color_ready
      ? EnsureArtifactTexture(resolved_scene_color_artifact_,
          "ResolvedSceneColor", scene_textures_.GetSceneColor())
      : nullptr,
    .valid = scene_color_ready,
  };

  const auto scene_depth_ready
    = setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneDepth)
    && scene_texture_bindings_.scene_depth_srv
      != SceneTextureBindings::kInvalidIndex;
  scene_texture_extracts_.resolved_scene_depth = {
    .texture = scene_depth_ready
      ? EnsureArtifactTexture(resolved_scene_depth_artifact_,
          "ResolvedSceneDepth", scene_textures_.GetSceneDepth())
      : nullptr,
    .valid = scene_depth_ready,
  };
}

} // namespace oxygen::vortex
