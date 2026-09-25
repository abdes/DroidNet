//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/ext/vector_uint3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::vortex::testing::exposure {

using graphics::ResourceStates;
using graphics::Texture;

NOLINT_TEST_F(ExposureLightingGpuTest,
  ExposureStatusReadbacksReuseWithinViewLifetimeAndInvalidateOnRecovery)
{
  using PublicationProbe = vortex::testing::RendererPublicationProbe;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  surface_view_id = 4820U;
  const auto handle = CompositionView::ViewStateHandle {
    surface_view_id,
  };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  scene->GetEnvironment()
    ->TryGetSystem<scene::environment::PostProcessVolume>()
    ->SetExposureSettings(settings);
  scene->Update();
  scene->SyncObservers();

  // Only normal frame-slot synchronization drives completion. No queue-idle
  // waits, mapped image inspection or explicit status polling drive reuse.
  const auto render_frame = [&] -> void {
    const auto slot = frame::Slot {
      sequence % frame::kFramesInFlight.get(),
    };
    Backend().BeginFrame(
      frame::SequenceNumber {
        ++sequence,
      },
      slot);
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(
      frame::SequenceNumber {
        sequence,
      },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr {
      &frame,
    });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = slot,
      .frame_sequence = frame::SequenceNumber { sequence, },
      .delta_time_seconds = 0.0F, });
    facade.SetSceneSource({ .scene = observer_ptr {
                              scene.get(),
                            } });
    facade.SetViewIntent(
      Renderer::OffscreenSceneViewInput::FromCamera("Status readback reuse",
        ViewId {
          surface_view_id,
        },
        view, camera)
        .SetViewStateHandle(handle));
    facade.SetOutputTarget({ .framebuffer = observer_ptr {
                               framebuffer.get(),
                             } });
    facade.SetPipeline(Renderer::OffscreenPipelineInput::Forward());
    auto session = facade.Finalize();
    if (!session.has_value()) {
      FAIL() << "Expected session to contain a value";
    }
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    renderer_->OnFrameEnd(observer_ptr {
      &frame,
    });
    Backend().EndFrame(
      frame::SequenceNumber {
        sequence,
      },
      slot);
  };
  const auto same_owner = [](const auto& left, const auto& right) -> auto {
    return !left.owner_before(right) && !right.owner_before(left);
  };
  std::weak_ptr<const void> stable_pool;
  std::vector<PublicationProbe::ExposureReadbackIdentity> identities;
  unsigned reused_submissions = 0U;
  PostProcessService* service = nullptr;
  constexpr auto stable_frames = 4U * frame::kFramesInFlight.get();
  for (unsigned iteration = 0U; iteration < stable_frames; ++iteration) {
    SCOPED_TRACE(iteration);
    ASSERT_NO_FATAL_FAILURE(render_frame());
    auto* owner = PublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    service = PublicationProbe::GetPostProcessService(*owner);
    ASSERT_NE(service, nullptr);
    const auto reuse
      = PublicationProbe::ExposureStatusReuseForView(*service, handle);
    ASSERT_FALSE(reuse.pool.expired());
    ASSERT_GT(reuse.pending.size() + reuse.available.size(), 0U);
    EXPECT_LE(reuse.pending.size() + reuse.available.size(),
      frame::kFramesInFlight.get());
    if (iteration == 0U) {
      stable_pool = reuse.pool;
    } else {
      EXPECT_TRUE(same_owner(stable_pool, reuse.pool));
    }
    for (const auto& current : reuse.pending) {
      ASSERT_FALSE(current.readback.expired());
      const auto seen
        = std::ranges::find_if(identities, [&](const auto& prior) -> auto {
            return same_owner(prior.readback, current.readback);
          });
      if (seen == identities.end()) {
        identities.push_back(current);
      } else if (current.frame_sequence > seen->frame_sequence) {
        // A new ticket on the same ownership identity proves actual reuse,
        // rather than observing one incomplete readback in successive frames.
        ++reused_submissions;
        seen->frame_sequence = current.frame_sequence;
      }
    }
  }
  EXPECT_GT(reused_submissions, 0U);
  EXPECT_LE(identities.size(), frame::kFramesInFlight.get());
  ASSERT_NE(service, nullptr);
  ASSERT_FALSE(stable_pool.expired());

  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
      .has_value());
  ASSERT_NO_FATAL_FAILURE(render_frame());
  const auto recovered
    = PublicationProbe::ExposureStatusReuseForView(*service, handle);
  EXPECT_TRUE(stable_pool.expired());
  ASSERT_FALSE(recovered.pool.expired());
  EXPECT_FALSE(same_owner(stable_pool, recovered.pool));
  EXPECT_LE(recovered.pending.size() + recovered.available.size(),
    frame::kFramesInFlight.get());

  ASSERT_TRUE(renderer_->ReleaseOffscreenViewState(
    ViewId {
      surface_view_id,
    },
    handle));
  EXPECT_TRUE(recovered.pool.expired());
  const auto removed
    = PublicationProbe::ExposureStatusReuseForView(*service, handle);
  EXPECT_TRUE(removed.pool.expired());
  EXPECT_TRUE(removed.pending.empty());
  EXPECT_TRUE(removed.available.empty());
  const auto remaining
    = PublicationProbe::ExposureStatusCounts(*service, handle);
  EXPECT_EQ(remaining.first, 0U);
  EXPECT_EQ(remaining.second, 0U);
  RecordProperty(
    "status_readback_stable_frames", static_cast<int>(stable_frames));
  RecordProperty(
    "status_readback_reused_submissions", static_cast<int>(reused_submissions));
  RecordProperty(
    "status_readback_unique_identities", static_cast<int>(identities.size()));
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, SceneDelayedStatusCannotAuthorizeStalePrecision)
{
  using Probe = vortex::testing::RendererPublicationProbe;
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  settings.speed_up = settings.speed_down = 1;
  frame_delta_seconds = .25F;
  SceneTextureExtractRef current;
  std::shared_ptr<const Texture> reference;
  probe->inspect = [&](const RenderContext&,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    current = color;
    reference
      = Probe::GetSceneRenderer(*renderer_)->GetResolvedSceneColorTexture();
  };
  unsigned delayed_frames = 0;
  for (const bool forward : {
         false,
         true,
       }) {
    settings.compensation_ev = 0;
    surface_view_id = 9000;
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    surface_view_id = forward ? 4701U : 4700U;
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    auto& service = OwnedExposureService();
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    if (!seed.has_value()) {
      FAIL() << "Expected seed to contain a value";
    }
    Probe::ExposureStatusJobs held;
    double expected = .0625;
    ExposureStateData state;
    for (unsigned iteration = 0; iteration < 6; ++iteration) {
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
      ASSERT_NO_FATAL_FAILURE(
        ExpectSurfaceExposure(.25F, expected, *reference, state));
      EXPECT_EQ(state.applied_generation.at(0), seed->generation);
      EXPECT_EQ(InspectRequiredTransition(handle).phase,
        ExposureTransitionPhase::kQueued);
      // Coalesce delayed delivery to the latest real completed ticket. No
      // status bytes are fabricated, and at most two ticket batches coexist.
      held = Probe::TakeExposureStatuses(service, handle);
      ASSERT_EQ(held.size(), 1U);
      ASSERT_NE(held.front().readback, nullptr);
      expected
        = ReferenceAdaptedGain(expected, UniformReferenceGain(.25F), .25);
      ++delayed_frames;
    }
    const auto eligible = Read<ExposureCompletedStatus>(
      *held.front().state->status_buffer, ResourceStates::kCopySource);
    EXPECT_EQ(eligible.flags & 7U, 5U);
    EXPECT_EQ(eligible.fp16_eligible_streak, 2U);
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, expected, *reference, state));
    EXPECT_EQ(InspectRequiredTransition(handle).phase,
      ExposureTransitionPhase::kApplied);

    // A delayed eligible packet cannot authorize a newer request generation.
    held = Probe::TakeExposureStatuses(service, handle);
    ASSERT_EQ(held.size(), 1U);
    const auto newer = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    if (!newer.has_value()) {
      FAIL() << "Expected newer to contain a value";
    }
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, 0x1p-8, *reference, state));
    EXPECT_EQ(InspectRequiredTransition(handle).request, *newer);
    EXPECT_EQ(InspectRequiredTransition(handle).phase,
      ExposureTransitionPhase::kQueued);

    // Nor can it certify changed settings, even if polled before preparation.
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto before
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    held = Probe::TakeExposureStatuses(service, handle);
    ASSERT_EQ(held.size(), 1U);
    settings.compensation_ev = 1;
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(.25F,
      ReferenceAdaptedGain(
        before.displayed_scale, UniformReferenceGain(.25F), .25),
      *reference, state));
    EXPECT_NE(state.settings_revision, before.settings_revision);

    // Retirement must reject old readers even after the same handle is reused.
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    held = Probe::TakeExposureStatuses(service, handle);
    ASSERT_EQ(held.size(), 1U);
    const auto retired_lifetime = held.front().lifetime;
    EXPECT_TRUE(renderer_->ReleaseOffscreenViewState(
      ViewId {
        surface_view_id,
      },
      handle));
    const auto replacement = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 6.0F);
    if (!replacement.has_value()) {
      FAIL() << "Expected replacement to contain a value";
    }
    EXPECT_NE(replacement->lifetime, retired_lifetime);
    settings.compensation_ev = 0;
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, 0x1p-6, *reference, state));
    EXPECT_EQ(InspectRequiredTransition(handle).request, *replacement);
    EXPECT_FALSE(renderer_->RetryExposureTransition(*newer).has_value());
    EXPECT_TRUE(renderer_->ReleaseOffscreenViewState(
      ViewId {
        surface_view_id,
      },
      handle));
  }
  probe->inspect = {};
  RecordProperty("delayed_scene_frames", delayed_frames);
  RecordProperty("stale_scene_status_cases", 6);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ProductionAdmissionRejectsCurrentAtmosphereLayoutChanges)
{
  verify_manual_p = false;
  settings.mode = engine::ExposureMode::kAuto;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
  sky.SetEnabled(true);
  // Exact vacuum transfer isolates layout compatibility from conservative
  // interpolation budgets of a nonuniform atmosphere.
  sky.SetRayleighScatteringRgb({
    0,
    0,
    0,
  });
  sky.SetMieScatteringRgb({
    0,
    0,
    0,
  });
  sky.SetMieAbsorptionRgb({
    0,
    0,
    0,
  });
  sky.SetOzoneAbsorptionRgb({
    0,
    0,
    0,
  });
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(0);
  ASSERT_EQ(
    fixture_console.Execute("vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(fixture_console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  probe->prepare = [](RenderContext& ctx) -> void {
    ctx.current_view.with_atmosphere = true;
    ctx.current_view.with_height_fog = true;
  };
  struct Snapshot {
    SceneTextureExtractRef color;
    postprocess::ExposurePass::FrameLease frame;
    glm::uvec3 aerial;
    Format aerial_format;
    Format sky_format = Format::kUnknown;
    Format fog_format = Format::kUnknown;
  };
  std::unordered_map<std::uint32_t, Snapshot> snapshots;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    const auto textures
      = vortex::testing::RendererPublicationProbe::EnvironmentTextures(
        *owner, ctx.current_view.view_id);
    Snapshot snapshot {
      .color = color,
      .frame = ctx.current_view.frame_exposure,
      .aerial = {},
      .aerial_format = Format::kUnknown,
    };
    for (const auto& texture : textures) {
      const auto& desc = texture->GetDescriptor();
      if (desc.debug_name.contains("SkyView")) {
        snapshot.sky_format = desc.format;
      }
      if (desc.debug_name.contains("IntegratedLightScattering")) {
        snapshot.fog_format = desc.format;
      }
      if (desc.texture_type == TextureType::kTexture3D
        && desc.debug_name.contains("Aerial")) {
        snapshot.aerial = {
          desc.width,
          desc.height,
          desc.depth,
        };
        snapshot.aerial_format = desc.format;
      }
    }
    snapshots.insert_or_assign(
      static_cast<std::uint32_t>(ctx.current_view.view_id.get()),
      std::move(snapshot));
  };
  const auto settle = [&](std::uint32_t id) -> void {
    surface_view_id = id;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
    for (unsigned retry = 0; retry < 10
      && snapshots.at(id).color.texture->GetDescriptor().format
        != Format::kRGBA16Float;
      ++retry) {
      const auto& f = snapshots.at(id).frame;
      const auto status = Read<ExposureCompletedStatus>(
        *f->current_state->status_buffer, ResourceStates::kCopySource);
      const auto report = Read<HdrSuitabilityData>(
        *f->suitability_buffer, ResourceStates::kShaderResource);
      std::println("admission_layout frame={} flags={} product={} kind={} "
                   "streak={} candidate={:g} report={} products={}",
        sequence, status.flags, status.first_failure_product,
        status.first_failure_kind, status.fp16_eligible_streak,
        report.candidate_pre_exposure, report.failure_flags,
        report.checked_products);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    }
    ASSERT_EQ(snapshots.at(id).color.texture->GetDescriptor().format,
      Format::kRGBA16Float);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    const auto& current = snapshots.at(id);
    EXPECT_EQ(
      current.color.texture->GetDescriptor().format, Format::kRGBA16Float);
    EXPECT_EQ(current.aerial_format, Format::kRGBA16Float);
    EXPECT_EQ(current.sky_format, Format::kRGBA16Float);
    EXPECT_EQ(current.fog_format, Format::kRGBA16Float);
    const auto conversion = Read<HdrSuitabilityData>(
      *current.frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(conversion.failure_flags, 0U);
  };
  ASSERT_NO_FATAL_FAILURE(settle(100));
  ASSERT_NO_FATAL_FAILURE(settle(101));
  struct Change {
    const char* command;
    glm::uvec3 extent;
  };
  const std::array changes {
    Change {
      .command = "vtx.sky_atmosphere.aerial_perspective_lut.width 16",
      .extent = { 16, 16, 32 },
    },
    Change {
      .command = "vtx.sky_atmosphere.aerial_perspective_lut.width 128",
      .extent = { 128, 128, 32 },
    },
    Change {
      .command
      = "vtx.sky_atmosphere.aerial_perspective_lut.depth_resolution 16",
      .extent = { 128, 128, 16 },
    },
    Change {
      .command
      = "vtx.sky_atmosphere.aerial_perspective_lut.depth_resolution 64",
      .extent = { 128, 128, 64 },
    },
  };
  unsigned cases = 0;
  for (const auto& change : changes) {
    SCOPED_TRACE(change.command);
    ASSERT_EQ(fixture_console.Execute(change.command).status,
      console::ExecutionStatus::kOk);
    // The first view must reject its stale certificate before a second view
    // can refresh the shared cache and conceal a stale-layout selection.
    for (const auto id : {
           100U,
           101U,
         }) {
      surface_view_id = id;
      const auto before = Read<ExposureStateData>(
        *snapshots.at(id).frame->current_state->buffer,
        ResourceStates::kShaderResource);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
      const auto& current = snapshots.at(id);
      EXPECT_EQ(
        current.color.texture->GetDescriptor().format, Format::kRGBA32Float);
      EXPECT_EQ(current.aerial_format, Format::kRGBA32Float);
      EXPECT_EQ(current.sky_format, Format::kRGBA32Float);
      EXPECT_EQ(current.fog_format, Format::kRGBA32Float);
      EXPECT_EQ(current.aerial, change.extent);
      EXPECT_EQ(current.frame->qualified_candidate, nullptr);
      const auto after = Read<ExposureStateData>(
        *current.frame->current_state->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(after.displayed_scale, before.displayed_scale);
      EXPECT_EQ(after.latent_scale, before.latent_scale);
      EXPECT_EQ(after.requested_generation, before.requested_generation);
      EXPECT_EQ(after.applied_generation, before.applied_generation);
      EXPECT_NE(after.flags & 1U, 0U);
      ++cases;
    }
    ASSERT_NO_FATAL_FAILURE(settle(100));
    ASSERT_NO_FATAL_FAILURE(settle(101));
  }
  surface_view_id = 100;
  const auto before_failure
    = Read<ExposureStateData>(*snapshots.at(100).frame->current_state->buffer,
      ResourceStates::kShaderResource);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetVolumetricFogEmissive({
    65504,
    65504,
    65504,
  });
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  const auto failed = snapshots.at(100).frame;
  const auto failure = Read<ExposureCompletedStatus>(
    *failed->current_state->status_buffer, ResourceStates::kCopySource);
  EXPECT_EQ(snapshots.at(100).fog_format, Format::kRGBA16Float);
  EXPECT_EQ(failure.flags & 18U, 18U);
  EXPECT_EQ(failure.first_failure_product, 10U);
  EXPECT_NE(failure.first_failure_kind & 2U, 0U);
  const auto held = Read<ExposureStateData>(
    *failed->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(held.displayed_scale, before_failure.displayed_scale);
  EXPECT_EQ(held.latent_scale, before_failure.latent_scale);
  EXPECT_EQ(held.flags & 12U, 0U);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  EXPECT_EQ(snapshots.at(100).color.texture->GetDescriptor().format,
    Format::kRGBA32Float);
  EXPECT_EQ(snapshots.at(100).fog_format, Format::kRGBA32Float);
  fog.SetExtinctionSigmaTPerMeter(0);
  fog.SetVolumetricFogEmissive({
    0,
    0,
    0,
  });
  ASSERT_NO_FATAL_FAILURE(settle(100));
  ASSERT_NO_FATAL_FAILURE(settle(101));
  probe->inspect = {};
  snapshots.clear();
  RecordProperty("current_layout_resize_view_cases", cases);
  RecordProperty("normal_half_store_failure_cases", 1);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, ProductionAdmissionInvalidatesBorrowerOnSourceChange)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& service = OwnedExposureService();
  auto source_settings = settings;
  source_settings.manual_ev = 0;
  const auto root_a = PublishExposureOwner(frame,
    ViewId {
      800U,
    },
    CompositionView::ViewStateHandle {
      800U,
    },
    source_settings);
  ctx_.scene = observer_ptr {
    scene.get(),
  };
  const auto publish_source
    = [&](ViewId view_id, CompositionView::ViewStateHandle handle,
        scene::ExposureSettings authored) -> void {
    ctx_.current_view.view_id = view_id;
    ctx_.current_view.view_state_handle = handle;
    ctx_.current_view.exposure_view_state_handle
      = CompositionView::kInvalidViewStateHandle;
    sequence_ = sequence;
    ServicePixel(service, Uniform(.25F, 4U, 4U), std::move(authored));
    sequence = static_cast<unsigned>(sequence_);
  };
  publish_source(root_a,
    CompositionView::ViewStateHandle {
      800U,
    },
    source_settings);
  surface_source_id = ViewId {
    800U,
  };
  SceneTextureExtractRef current;
  postprocess::ExposurePass::FrameLease exposure;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    current = color;
    exposure = ctx.current_view.frame_exposure;
  };
  const auto settle = [&] -> void {
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
    for (unsigned retry = 0; retry < 8
      && current.texture->GetDescriptor().format != Format::kRGBA16Float;
      ++retry) {
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    }
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto state = Read<ExposureStateData>(
      *exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_NE(state.flags & 128U, 0U);
  };
  ASSERT_NO_FATAL_FAILURE(settle());
  const auto old_candidate = exposure->qualified_candidate;
  ASSERT_NE(old_candidate, nullptr);
  source_settings.manual_ev = 4;
  const auto root_b = PublishExposureOwner(frame,
    ViewId {
      801U,
    },
    CompositionView::ViewStateHandle {
      801U,
    },
    source_settings);
  publish_source(root_b,
    CompositionView::ViewStateHandle {
      801U,
    },
    source_settings);
  surface_source_id = ViewId {
    801U,
  };
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(exposure->qualified_candidate, nullptr);
  auto state = Read<ExposureStateData>(
    *exposure->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(state.displayed_scale, 0x1p-4F);
  ASSERT_NO_FATAL_FAILURE(settle());
  EXPECT_NE(exposure->qualified_candidate, old_candidate);
  const auto request = renderer_->QueueExposureTransition(
    CompositionView::ViewStateHandle {
      801U,
    },
    ExposureTransitionPolicy::kPreserve);
  if (!request.has_value()) {
    FAIL() << "Expected request to contain a value";
  }
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(exposure->qualified_candidate, nullptr);
  publish_source(root_b,
    CompositionView::ViewStateHandle {
      801U,
    },
    source_settings);
  ASSERT_NO_FATAL_FAILURE(settle());
  state = Read<ExposureStateData>(
    *exposure->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(state.displayed_scale, 0x1p-4F);
  probe->inspect = {};
  current = {};
  exposure.reset();
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, ProductionAdmissionPinsCandidateAndRetainsFallback)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  struct Record {
    SceneTextureExtractRef color;
    postprocess::ExposurePass::FrameLease frame;
    Format accumulation;
    unsigned draws;
  };
  std::vector<Record> records;
  bool abort_recording = false;
  probe->inspect
    = [&](const RenderContext& ctx, const SceneTextureExtractRef& color,
        unsigned draws) -> void {
    if (std::exchange(abort_recording, false)) {
      throw std::runtime_error("Injected checked view discard");
    }
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    records.push_back({
      .color = color,
      .frame = ctx.current_view.frame_exposure,
      .accumulation
      = owner->GetSceneTextures().GetSceneColor().GetDescriptor().format,
      .draws = draws,
    });
  };
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
  for (unsigned retry = 0; retry < 6
    && records.back().color.texture->GetDescriptor().format
      != Format::kRGBA16Float;
    ++retry) {
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  }
  ASSERT_FALSE(records.empty());
  EXPECT_EQ(records.front().color.texture->GetDescriptor().format,
    Format::kRGBA32Float);
  ASSERT_EQ(
    records.back().color.texture->GetDescriptor().format, Format::kRGBA16Float);
  const auto half = records.back();
  ASSERT_NE(half.color.fallback, nullptr);
  ASSERT_NE(half.color.source_color, nullptr);
  ASSERT_NE(half.frame->qualified_candidate, nullptr);
  const auto domain = Read<FrameExposureData>(
    *half.frame->buffer, ResourceStates::kShaderResource);
  const auto qualified = Read<ExposureStateData>(
    *half.frame->qualified_candidate->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.pre_exposure, qualified.fp16_candidate_pre_exposure);
  const auto report = Read<HdrSuitabilityData>(
    *half.frame->conversion_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(report.failure_flags, 0U);
  EXPECT_EQ(report.checked_products, 1024U);
  const auto before = Read<ExposureStateData>(
    *half.frame->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(before.displayed_scale, 1.0F);
  const auto pixels = ReadFloatTexture(*half.color.texture, true);
  ASSERT_EQ(pixels.size(), 1U);
  EXPECT_NEAR(
    pixels.at(0).at(0) / domain.pre_exposure, .25F, (.005F * .25F) + 2e-5F);
  for (const auto& r : records) {
    EXPECT_EQ(r.accumulation, Format::kRGBA32Float);
  }
  const auto saved_fallback = ReadFloatTexture(*half.color.fallback);
  records.clear();
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 1));
  EXPECT_EQ(records.front().color.texture->GetDescriptor().format,
    Format::kRGBA32Float);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2));
  EXPECT_EQ(
    records.back().color.texture->GetDescriptor().format, Format::kRGBA16Float);
  EXPECT_EQ(ReadFloatTexture(*half.color.fallback), saved_fallback);
  EXPECT_EQ(ReadFloatTexture(*half.color.texture, true), pixels);
  const auto after
    = Read<ExposureStateData>(*records.back().frame->current_state->buffer,
      ResourceStates::kShaderResource);
  EXPECT_EQ(after.displayed_scale, .25F);
  const auto capture = BeginOptionalCapture();
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 1));
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  const auto submitted_count = records.size();
  const auto prior_output = ReadFloatTexture(
    *framebuffer->GetDescriptor().color_attachments.front().texture);
  abort_recording = true;
  ASSERT_NO_FATAL_FAILURE(
    RenderSurface(false, 2, 1, ExpectedViewOutcome::kDiscardedAfterRecording));
  EXPECT_EQ(records.size(), submitted_count);
  EXPECT_EQ(ReadFloatTexture(
              *framebuffer->GetDescriptor().color_attachments.front().texture),
    prior_output);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 1));
  EXPECT_EQ(
    records.back().color.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(records.back().frame->qualified_candidate, nullptr);
  const auto after_failure
    = Read<ExposureStateData>(*records.back().frame->current_state->buffer,
      ResourceStates::kShaderResource);
  EXPECT_EQ(after_failure.displayed_scale, after.displayed_scale);
  EXPECT_EQ(after_failure.applied_generation, after.applied_generation);
  probe->inspect = {};
  records.clear();
}

} // namespace oxygen::vortex::testing::exposure
