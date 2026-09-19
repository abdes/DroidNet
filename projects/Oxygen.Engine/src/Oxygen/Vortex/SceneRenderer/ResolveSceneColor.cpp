//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/RenderContext.h>
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
    "SceneRenderer: cannot resolve '{}' without a known or declared initial "
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

// Stage 21 owner: ResolveSceneColor is the only retained seam allowed to
// snapshot the ResolvedSceneColor/ResolvedSceneDepth artifacts for Stage 22
// consumption and the downstream Stage 23 handoff.
void SceneRenderer::ResolveSceneColor(
  RenderContext& ctx, const PostProcessService::PreparedExposure* prepared)
{
  auto& scene_textures = ActiveSceneTextures();
  const auto scene_color_ready
    = setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneColor)
    && scene_texture_bindings_.scene_color_srv
      != SceneTextureBindings::kInvalidIndex;
  const bool narrow = scene_color_ready && prepared
    && ctx.current_view.hdr_color_format == Format::kRGBA16Float
    && active_scene_texture_lease_;
  scene_texture_extracts_.resolved_scene_color = {
    .texture = scene_color_ready
      ? EnsureArtifactTexture(resolved_scene_color_artifact_,
          "ResolvedSceneColor", scene_textures.GetSceneColor(),
          narrow ? std::optional { Format::kRGBA16Float } : std::nullopt)
      : nullptr,
    .valid = scene_color_ready,
    .exposure = scene_color_ready ? ctx.current_view.frame_exposure : nullptr,
  };

  const auto scene_depth_ready
    = setup_mode_.IsSet(SceneTextureSetupMode::Flag::kSceneDepth)
    && scene_texture_bindings_.scene_depth_srv
      != SceneTextureBindings::kInvalidIndex;
  scene_texture_extracts_.resolved_scene_depth = {
    .texture = scene_depth_ready
      ? EnsureArtifactTexture(resolved_scene_depth_artifact_,
          "ResolvedSceneDepth", scene_textures.GetSceneDepth())
      : nullptr,
    .valid = scene_depth_ready,
  };

  if ((scene_texture_extracts_.resolved_scene_color.texture == nullptr
        || !scene_texture_extracts_.resolved_scene_color.valid)
    && (scene_texture_extracts_.resolved_scene_depth.texture == nullptr
      || !scene_texture_extracts_.resolved_scene_depth.valid)) {
    return;
  }

  bool converted = false;
  if (narrow
    && scene_texture_extracts_.resolved_scene_color.texture != nullptr) {
    auto& target = *scene_texture_extracts_.resolved_scene_color.texture;
    const auto uav = ShaderVisibleIndex { RegisterSceneTextureView(target,
      { .view_type = graphics::ResourceViewType::kTexture_UAV,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .format = target.GetDescriptor().format,
        .dimension = TextureType::kTexture2D }) };
    converted = post_process_->ConvertSceneColor(ctx, *prepared,
      { .scene_signal = scene_textures.GetSceneColorResource().get(),
        .scene_signal_srv
        = ShaderVisibleIndex { scene_texture_bindings_.scene_color_srv } },
      target, uav);
    if (converted) {
      scene_texture_extracts_.resolved_scene_color.fallback
        = scene_textures.GetSceneColorResource().get();
      scene_texture_extracts_.resolved_scene_color.source_lease
        = active_scene_texture_lease_;
    } else {
      // Submission failure cannot publish unchecked half data. Preserve the
      // solved exposure and fall back to an unconditional FP32 snapshot.
      scene_texture_extracts_.resolved_scene_color.texture
        = EnsureArtifactTexture(resolved_scene_color_artifact_,
          "ResolvedSceneColor", scene_textures.GetSceneColor());
    }
  }

  scene_texture_extracts_.resolved_scene_color.retained_texture
    = resolved_scene_color_artifact_.texture;
  scene_texture_extracts_.resolved_scene_depth.retained_texture
    = resolved_scene_depth_artifact_.texture;
  scene_texture_extracts_.resolved_scene_color.valid = scene_color_ready
    && scene_texture_extracts_.resolved_scene_color.texture != nullptr;
  scene_texture_extracts_.resolved_scene_depth.valid = scene_depth_ready
    && scene_texture_extracts_.resolved_scene_depth.texture != nullptr;

  const auto queue_key = gfx_.QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder_ptr = gfx_.AcquireCommandRecorder(
    queue_key, "Vortex ResolveSceneColor");
  CHECK_F(static_cast<bool>(recorder_ptr),
    "SceneRenderer: failed to acquire a recorder for Stage 21 resolves");
  renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(
    *recorder_ptr);
  auto& recorder = *recorder_ptr;
  graphics::GpuEventScope scope(recorder, "Vortex.ResolveSceneColor",
    profiling::ProfileGranularity::kTelemetry, profiling::ProfileCategory::kPass);

  if (!converted && scene_texture_extracts_.resolved_scene_color.valid
    && scene_texture_extracts_.resolved_scene_color.texture != nullptr) {
    CopyTextureIntoArtifact(recorder, scene_textures.GetSceneColor(),
      *scene_texture_extracts_.resolved_scene_color.texture,
      graphics::ResourceStates::kRenderTarget);
  }
  if (scene_texture_extracts_.resolved_scene_depth.valid
    && scene_texture_extracts_.resolved_scene_depth.texture != nullptr) {
    CopyTextureIntoArtifact(recorder, scene_textures.GetSceneDepth(),
      *scene_texture_extracts_.resolved_scene_depth.texture,
      graphics::ResourceStates::kDepthRead);
  }
}

} // namespace oxygen::vortex
