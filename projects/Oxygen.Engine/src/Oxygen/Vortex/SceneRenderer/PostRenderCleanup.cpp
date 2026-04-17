//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>

namespace oxygen::vortex {

namespace {

auto TrackTextureFromKnownOrInitial(graphics::CommandRecorder& recorder,
  const graphics::Texture& texture) -> void
{
  if (recorder.IsResourceTracked(texture)) {
    return;
  }
  if (recorder.AdoptKnownResourceState(texture)) {
    return;
  }

  const auto initial = texture.GetDescriptor().initial_state;
  CHECK_F(initial != graphics::ResourceStates::kUnknown
      && initial != graphics::ResourceStates::kUndefined,
    "SceneRenderer: cannot extract '{}' without a known or declared initial "
    "state",
    texture.GetName());
  recorder.BeginTrackingResourceState(texture, initial);
}

auto CopyTextureIntoArtifact(graphics::CommandRecorder& recorder,
  const graphics::Texture& source, graphics::Texture& artifact,
  const graphics::ResourceStates source_final_state) -> void
{
  TrackTextureFromKnownOrInitial(recorder, source);
  TrackTextureFromKnownOrInitial(recorder, artifact);

  recorder.RequireResourceState(source, graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(artifact, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();

  recorder.CopyTexture(source, graphics::TextureSlice {},
    graphics::TextureSubResourceSet::EntireTexture(), artifact,
    graphics::TextureSlice {}, graphics::TextureSubResourceSet::EntireTexture());

  recorder.RequireResourceStateFinal(source, source_final_state);
  recorder.RequireResourceStateFinal(
    artifact, graphics::ResourceStates::kShaderResource);
}

} // namespace

// Stage 23 extraction/handoff owner: PostRenderCleanup is the only retained
// seam allowed to snapshot history artifacts and finalize their handoff after
// Stage 22 completes.
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

  if ((scene_texture_extracts_.prev_scene_depth.texture == nullptr
        || !scene_texture_extracts_.prev_scene_depth.valid)
    && (scene_texture_extracts_.prev_velocity.texture == nullptr
      || !scene_texture_extracts_.prev_velocity.valid)) {
    FinalizeSceneTextureExtractions();
    return;
  }

  const auto queue_key = gfx_.QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder_ptr = gfx_.AcquireCommandRecorder(
    queue_key, "Vortex PostRenderCleanup");
  CHECK_F(static_cast<bool>(recorder_ptr),
    "SceneRenderer: failed to acquire a recorder for Stage 23 extraction");
  auto& recorder = *recorder_ptr;

  if (scene_texture_extracts_.prev_scene_depth.valid
    && scene_texture_extracts_.prev_scene_depth.texture != nullptr
    && scene_texture_extracts_.resolved_scene_depth.valid
    && scene_texture_extracts_.resolved_scene_depth.texture != nullptr) {
    CopyTextureIntoArtifact(recorder,
      *scene_texture_extracts_.resolved_scene_depth.texture,
      *scene_texture_extracts_.prev_scene_depth.texture,
      graphics::ResourceStates::kShaderResource);
  }
  if (scene_texture_extracts_.prev_velocity.valid
    && scene_texture_extracts_.prev_velocity.texture != nullptr
    && velocity_texture != nullptr) {
    CopyTextureIntoArtifact(recorder, *velocity_texture,
      *scene_texture_extracts_.prev_velocity.texture,
      graphics::ResourceStates::kShaderResource);
  }

  FinalizeSceneTextureExtractions();
}

} // namespace oxygen::vortex
