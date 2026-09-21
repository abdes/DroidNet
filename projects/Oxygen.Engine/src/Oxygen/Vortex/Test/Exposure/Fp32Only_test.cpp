//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>
#include <utility>

#include <Oxygen/Console/Command.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::vortex::testing::exposure {

using graphics::ResourceStates;

NOLINT_TEST_F(
  ExposureLightingGpuTest, ProductionPrecisionRemainsFp32WithoutAdmission)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
    HdrPrecisionControl::kProduction);
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 8));
  auto& backend = FailureBackend();
  backend.recorder_names.clear();
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 4));
  const auto domain = Read<FrameExposureData>(
    *probe->exposure->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.flags, 17U);
  EXPECT_EQ(domain.pre_exposure, 1.0F);
  EXPECT_EQ(probe->color->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(probe->exposure->qualified_candidate, nullptr);
  EXPECT_EQ(probe->exposure->suitability_buffer, nullptr);
  EXPECT_EQ(probe->exposure->conversion_buffer, nullptr);
  EXPECT_TRUE(
    std::ranges::none_of(backend.recorder_names, [](const auto& name) -> bool {
      return name.contains("Suitability") || name.contains("Gradient")
        || name == "Exposure status readback";
    }));
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  PrecisionControlChangeRejectsOldAcknowledgementAndPreservesSeed)
{
  using Probe = vortex::testing::RendererPublicationProbe;
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  frame_delta_seconds = 0.0F;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 5));
  auto& service = OwnedExposureService();
  auto& diagnostics = renderer_->GetDiagnosticsService();
  for (const bool from_production : {
         true,
         false,
       }) {
    const auto from = from_production ? HdrPrecisionControl::kProduction
                                      : HdrPrecisionControl::kQualified;
    const auto to = from_production ? HdrPrecisionControl::kQualified
                                    : HdrPrecisionControl::kProduction;
    diagnostics.SetHdrPrecisionControl(from);
    surface_view_id = from_production ? 9500U : 9501U;
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 3.0F);
    ASSERT_TRUE(seed.has_value());
    Probe::ExposureStatusJobs held;
    bool hold = true;
    probe->after_submit = [&] -> void {
      if (hold) {
        held = Probe::TakeExposureStatuses(service, handle);
      }
    };
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    ASSERT_EQ(held.size(), 1U);
    EXPECT_EQ(InspectRequiredTransition(handle).phase,
      ExposureTransitionPhase::kQueued);
    diagnostics.SetHdrPrecisionControl(to);
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    EXPECT_EQ(InspectRequiredTransition(handle).phase,
      ExposureTransitionPhase::kQueued);
    const auto state = Read<ExposureStateData>(
      *probe->exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_FLOAT_EQ(state.displayed_scale, .125F);
    EXPECT_EQ(state.applied_generation.at(0), seed->generation);
    ASSERT_EQ(held.size(), 1U);
    hold = false;
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    EXPECT_EQ(InspectRequiredTransition(handle).phase,
      ExposureTransitionPhase::kApplied);
    EXPECT_EQ(
      InspectRequiredTransition(handle).applied_generation, seed->generation);
    probe->after_submit = {};
  }
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, Fp32OnlySwitchPreservesGainAndEventsWithoutAdmission)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  frame_delta_seconds = 0.0F;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& diagnostics = renderer_->GetDiagnosticsService();
  SceneTextureExtractRef current;
  probe->inspect
    = [&](const RenderContext&, const SceneTextureExtractRef& color,
        unsigned) -> void { current = color; };
  for (const bool forward : {
         false,
         true,
       }) {
    SCOPED_TRACE(forward);
    surface_view_id = forward ? 9401U : 9400U;
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    diagnostics.SetHdrPrecisionControl(HdrPrecisionControl::kQualified);
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 3.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto saved = current;
    const auto before = Read<ExposureStateData>(
      *saved.exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_FLOAT_EQ(before.displayed_scale, .125F);

    diagnostics.SetHdrPrecisionControl(HdrPrecisionControl::kFp32Only);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_TRUE(current.valid);
    ASSERT_NE(current.exposure, nullptr);
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    EXPECT_EQ(current.fallback, nullptr);
    EXPECT_EQ(current.source_color, nullptr);
    EXPECT_EQ(current.exposure->qualified_candidate, nullptr);
    EXPECT_EQ(current.exposure->suitability_buffer, nullptr);
    EXPECT_EQ(current.exposure->conversion_buffer, nullptr);
    const auto domain = Read<FrameExposureData>(
      *current.exposure->buffer, ResourceStates::kShaderResource);
    EXPECT_FLOAT_EQ(domain.pre_exposure, 1.0F);
    EXPECT_FLOAT_EQ(domain.one_over_pre_exposure, 1.0F);
    EXPECT_EQ(domain.flags, 17U);
    const auto after
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_EQ(after.displayed_scale, before.displayed_scale);
    EXPECT_EQ(after.applied_generation, before.applied_generation);
    const auto pixels = ReadFloatTexture(
      *framebuffer->GetDescriptor().color_attachments.front().texture);
    ASSERT_EQ(pixels.size(), 1U);
    for (unsigned channel = 0U; channel < 3U; ++channel) {
      EXPECT_NEAR(
        pixels.front().at(channel), (.25F * .125F) - (.5F / 255.0F), 2e-4F);
    }
    const auto retained = Read<ExposureStateData>(
      *saved.exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(retained.displayed_scale, before.displayed_scale);
    EXPECT_EQ(retained.applied_generation, before.applied_generation);

    const auto pending = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    ASSERT_TRUE(pending.has_value());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 6));
    const auto applied
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_FLOAT_EQ(applied.displayed_scale, .0625F);
    EXPECT_EQ(applied.applied_generation.at(0), pending->generation);
    EXPECT_EQ(InspectRequiredTransition(handle).applied_generation,
      pending->generation);
    auto& backend = FailureBackend();
    backend.recorder_names.clear();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 3));
    EXPECT_TRUE(std::ranges::none_of(
      backend.recorder_names, [](const auto& name) -> bool {
        return name.contains("Suitability") || name.contains("Gradient")
          || name == "Exposure status readback";
      }));

    diagnostics.SetHdrPrecisionControl(HdrPrecisionControl::kQualified);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.exposure->qualified_candidate, nullptr);
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto restored
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_FLOAT_EQ(restored.displayed_scale, .0625F);
    EXPECT_EQ(restored.applied_generation.at(0), pending->generation);
    EXPECT_TRUE(renderer_->ReleaseOffscreenViewState(
      ViewId {
        surface_view_id,
      },
      handle));
    current = {};
  }
  probe->inspect = {};
}

NOLINT_TEST_F(ExposureLightingGpuTest, Fp32OnlyFixedModesAndZeroTarget)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
    HdrPrecisionControl::kFp32Only);
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  struct ModeCase {
    engine::ExposureMode mode;
    bool enabled;
    float target;
    double expected;
  };
  for (const auto& test : {
         ModeCase {
           engine::ExposureMode::kManual,
           true,
           .18F,
           .0625,
         },
         ModeCase {
           engine::ExposureMode::kManualCamera,
           true,
           .18F,
           1.0 / (121.0 * 125.0),
         },
         ModeCase {
           engine::ExposureMode::kManual,
           false,
           .18F,
           1.0,
         },
         ModeCase {
           engine::ExposureMode::kAuto,
           true,
           0.0F,
           0.0,
         },
       }) {
    settings.mode = test.mode;
    settings.enabled = test.enabled;
    settings.target_luminance = test.target;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 4, 5));
    const auto state = Read<ExposureStateData>(
      *probe->exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_NEAR(state.displayed_scale, test.expected, 1e-8);
    EXPECT_GT(state.latent_scale, 0.0F);
    const auto domain = Read<FrameExposureData>(
      *probe->exposure->buffer, ResourceStates::kShaderResource);
    EXPECT_FLOAT_EQ(domain.pre_exposure, 1.0F);
  }
}

NOLINT_TEST_F(ExposureLightingGpuTest, Fp32OnlySharedLifecycleForward)
{
  renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
    HdrPrecisionControl::kFp32Only);
  QualifySharedSceneLifecycle(true);
}

NOLINT_TEST_F(ExposureLightingGpuTest, Fp32OnlySharedLifecycleDeferred)
{
  renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
    HdrPrecisionControl::kFp32Only);
  QualifySharedSceneLifecycle(false);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  Fp32OnlyKeepsTemporalRenderingAndCurrentRangeProtection)
{
  verify_manual_p = false;
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
  sky.SetEnabled(true);
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
    fixture_console.Execute("vtx.volumetric_fog.temporal_reprojection true")
      .status,
    console::ExecutionStatus::kOk);
  probe->prepare = [](RenderContext& ctx) -> void {
    ctx.current_view.with_atmosphere = true;
    ctx.current_view.with_height_fog = true;
  };
  bool history_reprojected = false;
  bool history_reset = false;
  // Offscreen execution resets the renderer's per-frame observations on return.
  // Capture the actual submitted view before that boundary.
  probe->inspect = [&](const RenderContext&, const SceneTextureExtractRef&,
                     unsigned) -> void {
    const auto* rendering_owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(rendering_owner, nullptr);
    const auto& rendered = rendering_owner->GetLastEnvironmentLightingState();
    history_reprojected
      = rendered.stage14_volumetric_fog_temporal_history_reprojection_executed;
    history_reset = rendered.stage14_volumetric_fog_temporal_history_reset;
  };
  auto& diagnostics = renderer_->GetDiagnosticsService();
  diagnostics.SetHdrPrecisionControl(HdrPrecisionControl::kFp32Only);
  settings.mode = engine::ExposureMode::kAuto;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 8));
  auto* owner
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(owner, nullptr);
  EXPECT_TRUE(history_reprojected);
  const auto textures
    = vortex::testing::RendererPublicationProbe::EnvironmentTextures(*owner,
      ViewId {
        surface_view_id,
      });
  ASSERT_FALSE(textures.empty());
  for (const auto& texture : textures) {
    EXPECT_EQ(texture->GetDescriptor().format, Format::kRGBA32Float);
  }
  const auto status = Read<ExposureStatusStorage>(
    *probe->exposure->current_state->status_buffer,
    ResourceStates::kCopySource);
  for (const auto& error : status.producer_errors) {
    EXPECT_EQ(error.rgb_relative, 0.0F);
    EXPECT_EQ(error.rgb_absolute, 0.0F);
    EXPECT_EQ(error.transmittance_relative, 0.0F);
    EXPECT_EQ(error.transmittance_absolute, 0.0F);
  }
  diagnostics.SetHdrPrecisionControl(HdrPrecisionControl::kQualified);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  EXPECT_TRUE(history_reset);
  diagnostics.SetHdrPrecisionControl(HdrPrecisionControl::kFp32Only);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  EXPECT_TRUE(history_reset);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 4));
  EXPECT_TRUE(history_reprojected);

  mesh_node.GetRenderable().SetMaterialOverride(
    0, 0, MakeEmissiveMaterial(std::numeric_limits<float>::infinity()));
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 4));
  const auto failed = Read<ExposureStatusStorage>(
    *probe->exposure->current_state->status_buffer,
    ResourceStates::kCopySource);
  EXPECT_NE(failed.completed.flags & 16U, 0U);
  const auto held = Read<ExposureStateData>(
    *probe->exposure->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(held.flags & 12U, 0U);
  mesh_node.GetRenderable().SetMaterialOverride(
    0, 0, MakeEmissiveMaterial(.25F));
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 5));
  const auto recovered = Read<ExposureStatusStorage>(
    *probe->exposure->current_state->status_buffer,
    ResourceStates::kCopySource);
  EXPECT_EQ(recovered.completed.flags & 16U, 0U);
  const auto valid = Read<ExposureStateData>(
    *probe->exposure->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(valid.flags & 12U, 12U);
  probe->inspect = {};
}

} // namespace oxygen::vortex::testing::exposure
