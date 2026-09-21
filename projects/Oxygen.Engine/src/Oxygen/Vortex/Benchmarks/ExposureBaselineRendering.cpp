//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

#include <d3d12.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Profiling/CpuScopeObserver.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Benchmarks/ExposureBaselineScenario.h>
#include <Oxygen/Vortex/Benchmarks/ExposureCpuTiming.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {

auto ExposureBaselineScenario::UpdatePath(const unsigned sample_frame) -> void
{
  for (unsigned index = 0U; index < view_count; ++index) {
    const auto phase = (sample_frame + (index * 600U)) % 1200U;
    path_phases.at(index) = phase;
    auto progress = 0.0F;
    if (phase >= 300U && phase < 600U) {
      progress = static_cast<float>(phase - 300U) / 300.0F;
    } else if (phase >= 600U && phase < 900U) {
      progress = 1.0F;
    } else if (phase >= 900U) {
      progress = 1.0F - (static_cast<float>(phase - 900U) / 300.0F);
    }
    progress = progress * progress * (3.0F - (2.0F * progress));
    const auto position = glm::vec3 {
      index == 0U ? 2.25F : 2.6F,
      -8.0F + (9.5F * progress),
      1.2F,
    };
    cameras.at(index).GetTransform().SetLocalPosition(position);
    const auto camera_view = glm::lookAt(position,
      glm::vec3 {
        -.75F,
        0.0F,
        .25F,
      },
      space::move::Up);
    cameras.at(index).GetTransform().SetLocalRotation(
      glm::quat_cast(glm::inverse(camera_view)));
  }
  fixture_.scene->Update();
  fixture_.scene->SyncObservers();
}

auto ExposureBaselineScenario::InspectView(const RenderContext& context,
  const SceneTextureExtractRef& color, const unsigned draws) -> void
{
  const auto index = context.current_view.view_state_handle.get() - 500U;
  CHECK_F(index < seen_views.size());
  CHECK_F(!seen_views.at(index));
  seen_views.at(index) = true;
  draw_counts.at(index) = draws;
  if (capture_event) {
    current_checkpoint.exposure.at(index) = context.current_view.frame_exposure;
    current_checkpoint.formats.at(index)
      = static_cast<unsigned>(color.texture->GetDescriptor().format);
    const auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *fixture_.renderer_);
    current_checkpoint.history_reset.at(index)
      = owner->GetLastEnvironmentLightingState()
          .stage14_volumetric_fog_temporal_history_reset
      ? 1U
      : 0U;
  }
  if (capture_endpoint) {
    CHECK_F(color.valid && color.texture != nullptr);
    endpoint_hdr.at(index) = color;
    endpoint_exposure.at(index) = context.current_view.frame_exposure;
  }
  if (require_ready) {
    CHECK_F(color.valid && color.texture != nullptr && color.exposure);
    if (moving) {
      CHECK_F(draws > 0U && draws <= 11U);
      const auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *fixture_.renderer_);
      CHECK_NOTNULL_F(owner);
      const auto* shadows
        = vortex::testing::RendererPublicationProbe::GetShadowService(*owner);
      CHECK_NOTNULL_F(shadows);
      CHECK_NOTNULL_F(
        shadows->InspectShadowSurface(context.current_view.view_id));
      CHECK_F(shadows->ResolveShadowFrameSlot(context.current_view.view_id)
        != kInvalidShaderVisibleIndex);
      const auto& shadow_state = shadows->GetLastRenderState();
      CHECK_F((shadow_state.frame_sequence
        == frame::SequenceNumber {
          fixture_.sequence,
        }));
      CHECK_F(shadow_state.rendered_cascade_count > 0U);
      CHECK_F(shadow_state.shadow_caster_draw_count > 0U);
      const auto& environment = owner->GetLastEnvironmentLightingState();
      CHECK_F(environment.stage14_integrated_light_scattering_valid);
      CHECK_F(environment.stage14_volumetric_fog_executed);
      CHECK_F(environment.stage14_volumetric_fog_temporal_history_requested);
      history_reprojected.at(index)
        = environment
            .stage14_volumetric_fog_temporal_history_reprojection_executed
        ? 1U
        : 0U;
      history_reset.at(index)
        = environment.stage14_volumetric_fog_temporal_history_reset ? 1U : 0U;
      const auto phase = path_phases.at(index);
      if (!event_cycle
        && ((phase >= 10U && phase < 300U)
          || (phase >= 610U && phase < 900U))) {
        CHECK_F(history_reprojected.at(index) == 1U);
      }
    } else if (mixed_scene) {
      CHECK_F(draws > 0U && draws <= 5U);
      CHECK_F(draws == warm_draw_counts.at(index));
    } else {
      CHECK_F(draws == 1U);
    }
    CHECK_F(color.texture->GetDescriptor().width
      == width >> target_indices.at(index));
    CHECK_F(color.texture->GetDescriptor().height
      == height >> target_indices.at(index));
    if (!mixed_scene || fp32_reference || fp32_only) {
      CHECK_F(color.texture->GetDescriptor().format == expected_format);
    }
  }
  formats.at(index) = (color.texture != nullptr)
    ? static_cast<unsigned>(color.texture->GetDescriptor().format)
    : static_cast<unsigned>(Format::kUnknown);
}

auto ExposureBaselineScenario::RenderFrame(const bool start_recording,
  const unsigned sample_frame) -> ExposureBaselineScenario::Sample
{
  const auto started = Clock::now();
  seen_views.fill(false);
  draw_counts.fill(0U);
  formats.fill(static_cast<unsigned>(Format::kUnknown));
  history_reprojected.fill(0U);
  history_reset.fill(0U);
  const auto slot = frame::Slot {
    fixture_.sequence % 3U,
  };
  const auto frame_sequence = frame::SequenceNumber {
    ++fixture_.sequence,
  };
  std::optional<profiling::ScopedCpuScopeObserver> cpu_observer;
  if (recording_cpu_owners) {
    cpu_timing->BeginFrame(fixture_.sequence);
    cpu_observer.emplace(*cpu_timing);
  }
  fixture_.Backend().BeginFrame(frame_sequence, slot);
  const auto after_frame_start = Clock::now();
  fixture_.frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
  fixture_.frame.SetFrameSequenceNumber(
    frame_sequence, engine::internal::EngineTagFactory::Get());
  if (event_cycle && sample_frame == 906U) {
    auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
      *fixture_.renderer_);
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    vortex::testing::RendererPublicationProbe::RestoreExposureStatusDelivery(
      *service,
      CompositionView::ViewStateHandle {
        500U,
      },
      std::move(held_statuses));
    held_statuses = {};
  }
  if (moving) {
    UpdatePath(sample_frame);
  }
  fixture_.renderer_->OnFrameStart(observer_ptr {
    &fixture_.frame,
  });
  if (event_cycle) {
    ApplyEvent(sample_frame);
    if (sample_frame >= 900U && sample_frame <= 905U) {
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *fixture_.renderer_);
      auto* service
        = vortex::testing::RendererPublicationProbe::GetPostProcessService(
          *owner);
      vortex::testing::RendererPublicationProbe::RestoreExposureStatusDelivery(
        *service,
        CompositionView::ViewStateHandle {
          500U,
        },
        std::move(held_statuses));
      held_statuses = {};
    }
  }
  if (start_recording) {
    CHECK_F(
      fixture_.renderer_->GetDiagnosticsService().RequestGpuTimelineRecording(
        recording_path, recording_frames));
  }
  for (unsigned index = 0U; index < targets.size(); ++index) {
    if (!enabled_views.at(index)) {
      continue;
    }
    const auto target_index = target_indices.at(index);
    auto sized_view = fixture_.view;
    sized_view.viewport.width = static_cast<float>(width >> target_index);
    sized_view.viewport.height = static_cast<float>(height >> target_index);
    auto input = CompositionView::ForScene(
      ViewId {
        500U + index,
      },
      sized_view, cameras.at(index));
    input.view_state_handle = CompositionView::ViewStateHandle {
      500U + index,
    };
    input.render_settings.exposure = view_settings.at(index);
    if (index == 1U) {
      input.exposure_source_view_id = secondary_source;
    }
    CHECK_F(fixture_.renderer_->PublishRuntimeCompositionView(fixture_.frame,
              { .composition_view = input,
                .render_target = observer_ptr { targets.at(target_index).get(), },
                .composite_source = observer_ptr { targets.at(target_index).get(), }, },
              forward ? ShadingMode::kForward : ShadingMode::kDeferred)
      != kInvalidViewId);
  }
  auto loop = co::testing::TestEventLoop {};
  // Run completes synchronously before the closure or captures are destroyed.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
  co::Run(loop, [&] -> co::Co<void> {
    co_await fixture_.renderer_->OnPreRender(observer_ptr {
      &fixture_.frame,
    });
    co_await fixture_.renderer_->OnRender(observer_ptr {
      &fixture_.frame,
    });
    co_await fixture_.renderer_->OnCompositing(observer_ptr {
      &fixture_.frame,
    });
  });
  if (require_ready) {
    for (unsigned index = 0U; index < view_count; ++index) {
      CHECK_F(seen_views.at(index) == enabled_views.at(index));
    }
  }
  if (event_cycle) {
    ObserveEvent(sample_frame);
  }
  fixture_.renderer_->OnFrameEnd(observer_ptr {
    &fixture_.frame,
  });
  fixture_.Backend().EndFrame(frame_sequence, slot);
  const auto ended = Clock::now();
  return {
    .frame_sequence = fixture_.sequence,
    .wall_ms = Milliseconds(ended - started),
    .frame_start_ms = Milliseconds(after_frame_start - started),
    .submission_ms = Milliseconds(ended - after_frame_start),
    .formats = formats,
    .draws = draw_counts,
    .path_phases = path_phases,
    .history_reprojected = history_reprojected,
    .history_reset = history_reset,
  };
}

auto ExposureBaselineScenario::WarmUp() -> void
{
  const auto warm_start = Clock::now();
  warm_frames = 0U;
  const auto minimum_warm_frames = moving ? 1200U : 300U;
  if (acceptance) {
    startup.samples.reserve(60U);
    recording_path = startup.gpu;
    recording_frames = 60U;
  }
  while (warm_frames < minimum_warm_frames
    || Clock::now() - warm_start < std::chrono::seconds { 10, }
    || (moving && warm_frames % 1200U != 0U)) {
    const auto sample
      = RenderFrame(acceptance && warm_frames == 0U, moving ? warm_frames : 0U);
    if (acceptance && warm_frames < 60U) {
      startup.samples.push_back(sample);
      startup.seconds += sample.wall_ms / 1000.0;
    }
    ++warm_frames;
  }
  warm_seconds
    = std::chrono::duration<double>(Clock::now() - warm_start).count();
  if (automatic_sample_count) {
    const auto quantum = moving ? 1200U : 300U;
    const auto target = std::max(1800.0, 40.0 * warm_frames / warm_seconds);
    sample_count = static_cast<unsigned>(std::ceil(target / quantum)) * quantum;
    CHECK_LE_F(sample_count, 20000U);
  }
  warm_draw_counts = draw_counts;
  before = Snapshot();
  recording_path = gpu_path;
  recording_frames = sample_count;
}

auto ExposureBaselineScenario::MeasureFrames() -> void
{
  samples.clear();
  samples.reserve(sample_count);
  require_ready = true;
  if (measure_cpu_owners) {
    cpu_timing
      = std::make_unique<ExposureCpuTiming>(sample_count, measure_cpu_details);
    recording_cpu_owners = true;
  }
  const auto sample_start = Clock::now();
  auto previous_end = sample_start;
  for (unsigned index = 0U; index < sample_count; ++index) {
    auto sample = RenderFrame(index == 0U, index);
    const auto ended = Clock::now();
    sample.wall_ms = Milliseconds(ended - previous_end);
    previous_end = ended;
    samples.push_back(sample);
  }
  sample_seconds
    = std::chrono::duration<double>(Clock::now() - sample_start).count();
  recording_cpu_owners = false;
  after = Snapshot();
}

auto ExposureBaselineScenario::Snapshot() -> nlohmann::json
{
  auto textures = nlohmann::json::array();
  auto buffers = nlohmann::json::array();
  auto unique = std::unordered_set<ID3D12Resource*> {};
  std::uint64_t texture_bytes = 0U;
  std::uint64_t buffer_bytes = 0U;
  for (const auto& weak : backend.tracked_textures) {
    const auto texture = weak.lock();
    if (!texture) {
      continue;
    }
    auto* native = texture->GetNativeResource()->AsPointer<ID3D12Resource>();
    if ((native == nullptr) || !unique.insert(native).second) {
      continue;
    }
    const auto shape = native->GetDesc();
    const auto bytes = backend.GetCurrentDevice()
                         ->GetResourceAllocationInfo(0U, 1U, &shape)
                         .SizeInBytes;
    const auto& desc = texture->GetDescriptor();
    textures.push_back({
      {
        "name",
        desc.debug_name,
      },
      {
        "format",
        static_cast<unsigned>(desc.format),
      },
      {
        "width",
        desc.width,
      },
      {
        "height",
        desc.height,
      },
      {
        "depth",
        desc.depth,
      },
      {
        "array_layers",
        desc.array_size,
      },
      {
        "mips",
        desc.mip_levels,
      },
      {
        "samples",
        desc.sample_count,
      },
      {
        "placement_bytes",
        bytes,
      },
      {
        "exposure_hdr",
        ExposureFailureGraphics::IsExposureHdrTexture(desc.debug_name),
      },
    });
    texture_bytes += bytes;
  }
  for (const auto& weak : backend.tracked_buffers) {
    const auto buffer = weak.lock();
    if (!buffer) {
      continue;
    }
    auto* native = buffer->GetNativeResource()->AsPointer<ID3D12Resource>();
    if ((native == nullptr) || !unique.insert(native).second) {
      continue;
    }
    const auto shape = native->GetDesc();
    const auto bytes = backend.GetCurrentDevice()
                         ->GetResourceAllocationInfo(0U, 1U, &shape)
                         .SizeInBytes;
    const auto& desc = buffer->GetDescriptor();
    buffers.push_back({
      {
        "name",
        desc.debug_name,
      },
      {
        "logical_bytes",
        desc.size_bytes,
      },
      {
        "placement_bytes",
        bytes,
      },
    });
    buffer_bytes += bytes;
  }
  const auto* owner
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
      *fixture_.renderer_);
  const auto [families, leased]
    = vortex::testing::RendererPublicationProbe::SceneTexturePoolCounts(*owner);
  return nlohmann::json {
    {
      "textures",
      std::move(textures),
    },
    {
      "buffers",
      std::move(buffers),
    },
    {
      "texture_placement_bytes",
      texture_bytes,
    },
    {
      "buffer_placement_bytes",
      buffer_bytes,
    },
    {
      "texture_creation_count",
      backend.tracked_textures.size(),
    },
    {
      "buffer_creation_count",
      backend.tracked_buffers.size(),
    },
    {
      "scene_texture_families",
      families,
    },
    {
      "leased_scene_texture_families",
      leased,
    },
  };
}

} // namespace oxygen::vortex::testing::exposure
