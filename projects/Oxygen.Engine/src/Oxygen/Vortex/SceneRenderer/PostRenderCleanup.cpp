//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>

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
    recorder.BeginTrackingResourceState(texture, initial, false);
  }

  auto CopyTextureIntoArtifact(graphics::CommandRecorder& recorder,
    const graphics::Texture& source, graphics::Texture& artifact,
    const graphics::ResourceStates source_final_state) -> void
  {
    TrackTextureFromKnownOrInitial(recorder, source);
    TrackTextureFromKnownOrInitial(recorder, artifact);

    recorder.RequireResourceState(
      source, graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      artifact, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();

    recorder.CopyTexture(source, graphics::TextureSlice {},
      graphics::TextureSubResourceSet::EntireTexture(), artifact,
      graphics::TextureSlice {},
      graphics::TextureSubResourceSet::EntireTexture());

    recorder.RequireResourceState(source, source_final_state);
    recorder.RequireResourceState(
      artifact, graphics::ResourceStates::kShaderResource);
  }

} // namespace

// Stage 23 extraction/handoff owner: PostRenderCleanup is the only retained
// seam allowed to publish PrevSceneDepth and snapshot PrevVelocity for their
// handoff after Stage 22 completes.
void SceneRenderer::PostRenderCleanup(
  RenderContext& ctx, graphics::CommandRecorder& recorder)
{
  // Both consumers read the same immutable Stage 21 snapshot. Retain its
  // ownership wrapper so either reader can outlive the view and the other
  // reader; final release still retires through the GPU-frame reclaimer.
  const auto& resolved_depth = scene_texture_extracts_.resolved_scene_depth;
  if (resolved_depth.valid && resolved_depth.texture != nullptr) {
    scene_texture_extracts_.prev_scene_depth = resolved_depth;
  } else {
    scene_texture_extracts_.prev_scene_depth = {};
  }

  const auto* velocity_texture = ResolveVelocitySourceTexture();
  const auto velocity_ready
    = setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneVelocity)
    && scene_texture_bindings_.velocity_srv
      != SceneTextureBindings::kInvalidIndex
    && velocity_texture != nullptr;
  graphics::Texture* previous_velocity = nullptr;
  if (velocity_ready) {
    previous_velocity = EnsureArtifactTexture(
      ctx, prev_velocity_artifact_, "PrevVelocity", *velocity_texture);
  }
  scene_texture_extracts_.prev_velocity = {
    .texture = previous_velocity,
    .valid = velocity_ready,
  };

  scene_texture_extracts_.prev_velocity.retained_texture
    = prev_velocity_artifact_.texture;

  if (scene_texture_extracts_.prev_velocity.texture == nullptr
    || !scene_texture_extracts_.prev_velocity.valid) {
    FinalizeSceneTextureExtractions();
    return;
  }

  graphics::GpuEventScope scope(recorder, "Vortex.PostRenderCleanup",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

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
