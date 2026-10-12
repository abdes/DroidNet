//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkStageTable.h>
#include <Oxygen/Vortex/Internal/SurfaceLayers.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SurfaceLayerSource.h>
#include <Oxygen/Vortex/Types/CompositingTask.h>

namespace oxygen::vortex::internal {

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
    recorder.BeginTrackingResourceState(
      texture, texture.GetDescriptor().initial_state);
  }

} // namespace

auto SurfaceLayers::Register(const CompositionView::SurfaceRouteId surface,
  const observer_ptr<SurfaceLayerSource> source) -> SurfaceLayerToken
{
  CHECK_NOTNULL_F(source, "a surface layer needs a source");
  std::scoped_lock lock(mutex_);
  const auto token = SurfaceLayerToken { next_token_++ };
  layers_.push_back(Layer {
    .token = token,
    .surface_id = surface,
    .source = source,
    .texture = {},
    .framebuffer = {},
  });
  return token;
}

auto SurfaceLayers::Unregister(const SurfaceLayerToken token) -> void
{
  std::scoped_lock lock(mutex_);
  std::erase_if(layers_,
    [token](const Layer& layer) -> bool { return layer.token == token; });
}

auto SurfaceLayers::Record(Renderer& renderer, std::span<const Target> targets)
  -> std::vector<Composition>
{
  auto compositions = std::vector<Composition> {};
  std::scoped_lock lock(mutex_);
  for (auto& layer : layers_) {
    if (!layer.source->IsActive()) {
      continue;
    }
    const auto target = std::ranges::find_if(
      targets, [&layer](const Target& candidate) -> bool {
        return candidate.surface_id == layer.surface_id;
      });
    if (target == targets.end() || !target->composite_target
      || !target->target_surface) {
      continue;
    }

    const auto& target_desc = target->composite_target->GetDescriptor();
    if (target_desc.color_attachments.empty()
      || !target_desc.color_attachments.front().texture) {
      continue;
    }
    const auto& color_desc
      = target_desc.color_attachments.front().texture->GetDescriptor();
    if (!EnsureLayerTarget(renderer, layer, color_desc.width, color_desc.height)
      || !RecordLayer(renderer, layer)) {
      continue;
    }

    auto submission = CompositionSubmission {};
    submission.surface_id = layer.surface_id;
    submission.debug_name = "Vortex.SurfaceLayer";
    submission.composite_target = target->composite_target;
    submission.tasks.push_back(CompositingTask::MakeTextureBlend(layer.texture,
      ViewPort {
        .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = static_cast<float>(color_desc.width),
        .height = static_cast<float>(color_desc.height),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      },
      1.0F, "Surface Layer"));
    compositions.push_back(Composition {
      .submission = std::move(submission),
      .target_surface = target->target_surface,
    });
  }
  return compositions;
}

auto SurfaceLayers::Clear() noexcept -> void
{
  std::scoped_lock lock(mutex_);
  layers_.clear();
}

auto SurfaceLayers::EnsureLayerTarget(Renderer& renderer, Layer& layer,
  const std::uint32_t width, const std::uint32_t height) -> bool
{
  if (width == 0U || height == 0U) {
    return false;
  }
  if (layer.framebuffer && layer.texture
    && layer.texture->GetDescriptor().width == width
    && layer.texture->GetDescriptor().height == height) {
    return true;
  }

  layer.framebuffer.reset();
  layer.texture.reset();
  const auto gfx = renderer.GetGraphics();
  if (!gfx) {
    return false;
  }

  graphics::TextureDesc color_desc {};
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.initial_state = graphics::ResourceStates::kCommon;
  color_desc.use_clear_value = true;
  color_desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  color_desc.debug_name = "Vortex.SurfaceLayer";

  auto texture = gfx->CreateTexture(color_desc);
  if (!texture) {
    return false;
  }
  auto framebuffer_desc = graphics::FramebufferDesc {};
  framebuffer_desc.AddColorAttachment({ .texture = texture });
  auto framebuffer = gfx->CreateFramebuffer(framebuffer_desc);
  if (!framebuffer) {
    return false;
  }

  layer.texture = std::move(texture);
  layer.framebuffer = std::move(framebuffer);
  return true;
}

auto SurfaceLayers::RecordLayer(Renderer& renderer, const Layer& layer) -> bool
{
  const auto gfx = renderer.GetGraphics();
  if (!gfx) {
    return false;
  }
  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "Vortex SurfaceLayer");
  if (!recorder) {
    return false;
  }
  renderer.GetDiagnosticsService().AttachGpuTimelineCollector(*recorder);

  TrackTextureFromKnownOrInitial(*recorder, *layer.texture);
  recorder->RequireResourceState(
    *layer.texture, graphics::ResourceStates::kRenderTarget);
  recorder->FlushBarriers();
  recorder->BindFrameBuffer(*layer.framebuffer);
  recorder->ClearFramebuffer(*layer.framebuffer);
  {
    graphics::GpuEventScope scope(*recorder, gpu_work::kSurfaceLayerLabel,
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    layer.source->Record(*recorder);
  }
  recorder->RequireResourceStateFinal(
    *layer.texture, graphics::ResourceStates::kCommon);
  recorder->FlushBarriers();
  return true;
}

} // namespace oxygen::vortex::internal
