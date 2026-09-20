//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Internal/PreviousViewHistoryCache.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using graphics::FrameCaptureController;
using graphics::ResourceStates;
using graphics::Texture;

NOLINT_TEST_F(
  ExposureLightingGpuTest, SceneLifecycleHdrStartupCutsSeedsAndPause)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.min_ev = -22;
  settings.max_ev = 30;
  settings.min_log_luminance = -24;
  settings.log_luminance_range = 56;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  settings.speed_up = .75F;
  settings.speed_down = .5F;
  ASSERT_TRUE(scene::ResolveExposureSettings(settings).has_value());
  std::shared_ptr<const Texture> reference;
  Format format = Format::kUnknown;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    format = color.texture->GetDescriptor().format;
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    reference = owner->GetResolvedSceneColorTexture();
  };
  ExposureStateData state;
  unsigned cases = 0;
  unsigned checks = 0;
  for (const bool forward : {
         false,
         true,
       }) {
    for (const float value : {
           0.0F,
           0x1p-24F,
           .25F,
           0x1p32F,
         }) {
      SCOPED_TRACE(
        ::testing::Message() << "forward=" << forward << " source=" << value);
      float changed = value * 4;
      if (value == 0) {
        changed = .25F;
      } else if (value == 0x1p32F) {
        changed = 0x1p30F;
      }
      const auto initial_material = MakeEmissiveMaterial(value);
      const auto changed_material = MakeEmissiveMaterial(changed);
      surface_view_id = 9000;
      frame_delta_seconds = 0;
      // Asset readiness is established on another history before first use.
      mesh_node.GetRenderable().SetMaterialOverride(0, 0, changed_material);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 4));
      mesh_node.GetRenderable().SetMaterialOverride(0, 0, initial_material);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 4));
      surface_view_id = 1600 + cases;
      const auto handle = CompositionView::ViewStateHandle {
        surface_view_id,
      };
      const auto render = [&](float input, double expected) -> void {
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
        ASSERT_NE(reference, nullptr);
        ASSERT_NO_FATAL_FAILURE(
          ExpectSurfaceExposure(input, expected, *reference, state));
        ++checks;
      };
      const double initial = UniformReferenceGain(value);
      const double target = UniformReferenceGain(changed);
      observer_ptr<FrameCaptureController> capture;
      if (!forward && value == 0x1p32F) {
        capture = BeginOptionalCapture();
      }
      ASSERT_NO_FATAL_FAILURE(render(value, initial));
      if (capture) {
        EXPECT_TRUE(capture->EndCapture());
      }
      EXPECT_EQ(format, Format::kRGBA32Float);
      EXPECT_EQ(state.flags & 31U, value <= 0x1p-24F ? 27U : 15U);
      EXPECT_EQ(state.fallback_reason, 0U);
      EXPECT_EQ(Read<FrameExposureData>(
                  *probe->exposure->buffer, ResourceStates::kShaderResource)
                  .pre_exposure,
        1);
      mesh_node.GetRenderable().SetMaterialOverride(0, 0, changed_material);
      ASSERT_NO_FATAL_FAILURE(render(changed, initial));
      EXPECT_NEAR(std::log2(static_cast<double>(state.target_scale)),
        std::log2(target), 4e-4);
      frame_delta_seconds = .25F;
      const double adapted = ReferenceAdaptedGain(initial, target, .25);
      ASSERT_NO_FATAL_FAILURE(render(changed, adapted));
      frame_delta_seconds = 0;
      ASSERT_TRUE(renderer_
          ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
          .has_value());
      ASSERT_NO_FATAL_FAILURE(render(changed, target));
      EXPECT_EQ(format, Format::kRGBA32Float);
      EXPECT_EQ(state.requested_generation, state.applied_generation);
      EXPECT_NE(state.applied_generation.at(0), 0U);
      const float seed_ev = value <= 0x1p-24F ? -24.0F : 31.0F;
      const auto seed = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kSeedFromEv100, seed_ev);
      if (!seed.has_value()) {
        FAIL() << "Expected seed to contain a value";
      }
      ASSERT_TRUE(renderer_
          ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
          .has_value());
      frame_delta_seconds = .25F;
      const double seeded = std::exp2(-static_cast<double>(seed_ev));
      ASSERT_NO_FATAL_FAILURE(render(changed, seeded));
      EXPECT_EQ(state.applied_generation.at(0), seed->generation);
      const double after_seed = ReferenceAdaptedGain(seeded, target, .25);
      ASSERT_NO_FATAL_FAILURE(render(changed, after_seed));
      const auto preserve = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kPreserve);
      if (!preserve.has_value()) {
        FAIL() << "Expected preserve to contain a value";
      }
      ASSERT_TRUE(renderer_
          ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
          .has_value());
      ASSERT_NO_FATAL_FAILURE(render(changed, after_seed));
      EXPECT_EQ(state.applied_generation.at(0), preserve->generation);
      surface_view_id += 500;
      const auto startup = renderer_->QueueExposureTransition(
        CompositionView::ViewStateHandle {
          surface_view_id,
        },
        ExposureTransitionPolicy::kSeedFromEv100, -8.0F);
      if (!startup.has_value()) {
        FAIL() << "Expected startup to contain a value";
      }
      ASSERT_TRUE(renderer_->RetryExposureTransition(*startup).has_value());
      ASSERT_NO_FATAL_FAILURE(render(changed, 256));
      EXPECT_EQ(format, Format::kRGBA32Float);
      EXPECT_EQ(state.applied_generation.at(0), startup->generation);
      ASSERT_TRUE(renderer_->RetryExposureTransition(*startup).has_value());
      ASSERT_NO_FATAL_FAILURE(
        render(changed, ReferenceAdaptedGain(256, target, .25)));
      EXPECT_EQ(state.applied_generation.at(0), startup->generation);
      reference.reset();
      ++cases;
    }
  }
  for (const bool forward : {
         false,
         true,
       }) {
    SCOPED_TRACE(
      ::testing::Message() << "unmetered startup forward=" << forward);
    surface_view_id = forward ? 2501U : 2500U;
    frame_delta_seconds = 0;
    SetSurface(data::MaterialDomain::kOpaque, -1);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(-1, 1, *reference, state));
    EXPECT_EQ(state.flags & 2U, 0U);
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
      .25F, UniformReferenceGain(.25F), *reference, state));
    EXPECT_NE(state.flags & 2U, 0U);
    surface_view_id += 100;
    SetSurface(data::MaterialDomain::kOpaque, -1);
    const auto seed = renderer_->QueueExposureTransition(
      CompositionView::ViewStateHandle {
        surface_view_id,
      },
      ExposureTransitionPolicy::kSeedFromEv100, 6.0F);
    if (!seed.has_value()) {
      FAIL() << "Expected seed to contain a value";
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(-1, 0x1p-6, *reference, state));
    EXPECT_EQ(state.flags & 12U, 0U);
    EXPECT_NE(state.flags & 2U, 0U);
    EXPECT_EQ(state.applied_generation.at(0), seed->generation);
    frame_delta_seconds = .25F;
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(.25F,
      ReferenceAdaptedGain(0x1p-6, UniformReferenceGain(.25F), .25), *reference,
      state));
    checks += 4;
  }
  probe->inspect = {};
  reference.reset();
  RecordProperty("unmetered_startup_paths", 2);
  RecordProperty("scene_hdr_lifecycle_cases", cases);
  RecordProperty("scene_hdr_lifecycle_frames_checked", checks);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  SceneLifecycleModesPhysicalCameraZeroTargetAndLockedRange)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  std::shared_ptr<const Texture> reference;
  probe->inspect = [&](const RenderContext& ctx, const SceneTextureExtractRef&,
                     unsigned) -> void {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    reference
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetResolvedSceneColorTexture();
  };
  ExposureStateData state;
  unsigned cases = 0;
  unsigned checks = 0;
  for (const bool forward : {
         false,
         true,
       }) {
    for (const bool orthographic : {
           false,
           true,
         }) {
      SCOPED_TRACE(::testing::Message()
        << "forward=" << forward << " orthographic=" << orthographic);
      scene::CameraExposure physical {
        .aperture_f = 2,
        .shutter_rate = 4,
        .iso = 100,
      };
      if (orthographic) {
        auto lens = std::make_unique<scene::OrthographicCamera>();
        lens->SetViewport(view.viewport);
        lens->SetExtents(-1, 1, -1, 1, .1F, 10);
        lens->SetExposure(physical);
        ASSERT_TRUE(camera.ReplaceCamera(std::move(lens)));
      } else {
        auto lens = std::make_unique<scene::PerspectiveCamera>();
        lens->SetViewport(view.viewport);
        lens->SetExposure(physical);
        ASSERT_TRUE(camera.ReplaceCamera(std::move(lens)));
      }
      settings = scene::ExposureSettings {};
      settings.key = 12.5F;
      settings.mode = engine::ExposureMode::kManual;
      settings.min_ev = -22;
      settings.max_ev = 30;
      settings.min_log_luminance = -24;
      settings.log_luminance_range = 56;
      settings.low_percentile = 0;
      settings.high_percentile = 1;
      settings.speed_up = settings.speed_down = .5F;
      ASSERT_TRUE(scene::ResolveExposureSettings(settings).has_value());
      frame_delta_seconds = 0;
      surface_view_id = 9000;
      SetSurface(data::MaterialDomain::kOpaque, .25F);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 4));
      surface_view_id = 3000 + cases;
      const auto handle = CompositionView::ViewStateHandle {
        surface_view_id,
      };
      struct ExpectedSample {
        float radiance;
        double gain;
      };
      const auto render = [&](ExpectedSample sample, float ev = 0) -> void {
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev, 1));
        ASSERT_NE(reference, nullptr);
        ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
          sample.radiance, sample.gain, *reference, state));
        ++checks;
      };
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = .25F,
        .gain = 1,
      }));
      const auto rejected_seed = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kSeedFromEv100, 10.0F);
      if (!rejected_seed.has_value()) {
        FAIL() << "Expected rejected_seed to contain a value";
      }
      ASSERT_NO_FATAL_FAILURE(render(
        {
          .radiance = .25F,
          .gain = 0x1p-4,
        },
        4));
      EXPECT_NE(state.flags & (1U << 12U), 0U);
      EXPECT_EQ((state.flags >> 16U) & 15U, 1U);
      ASSERT_NO_FATAL_FAILURE(render(
        {
          .radiance = .25F,
          .gain = .25,
        },
        2));
      const auto rejected = renderer_->InspectExposureTransition(handle);
      if (!rejected.has_value()) {
        FAIL() << "Expected rejected to contain a value";
      }
      EXPECT_EQ(rejected->phase, ExposureTransitionPhase::kRejected);
      EXPECT_EQ(rejected->error, ExposureTransitionError::kNotAuto);
      settings.mode = engine::ExposureMode::kAuto;
      frame_delta_seconds = .5F;
      const double target = UniformReferenceGain(.25F);
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = .25F,
        .gain = .25,
      }));
      // The Manual start crosses D=1.5 during this half-second step.
      const double adapted = ReferenceAdaptedGain(.25, target, .5);
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = .25F,
        .gain = adapted,
      }));
      frame_delta_seconds = 0;
      SetSurface(data::MaterialDomain::kOpaque, 1);
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = adapted,
      }));
      settings.compensation_ev = 1;
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = adapted,
      }));
      EXPECT_NEAR(std::log2(static_cast<double>(state.target_scale)),
        std::log2(UniformReferenceGain(1)), 4e-4);
      settings.compensation_ev = 0;
      settings.speed_up = settings.speed_down = 0;
      frame_delta_seconds = 1;
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = adapted,
      }));
      settings.mode = engine::ExposureMode::kManualCamera;
      frame_delta_seconds = 0;
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = 0x1p-4,
      }));
      physical.iso = 400;
      if (orthographic) {
        camera.GetCameraAs<scene::OrthographicCamera>()->get().SetExposure(
          physical);
      } else {
        camera.GetCameraAs<scene::PerspectiveCamera>()->get().SetExposure(
          physical);
      }
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = .25,
      }));
      settings.enabled = false;
      const auto disabled_reset = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kRemeter);
      if (!disabled_reset.has_value()) {
        FAIL() << "Expected disabled_reset to contain a value";
      }
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = 1,
      }));
      EXPECT_NE(state.flags & (1U << 12U), 0U);
      EXPECT_EQ((state.flags >> 16U) & 15U, 1U);
      settings.enabled = true;
      settings.mode = engine::ExposureMode::kAuto;
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = 0,
      }));
      EXPECT_EQ(InspectRequiredTransition(handle).phase,
        ExposureTransitionPhase::kRejected);
      EXPECT_NE(state.flags & 64U, 0U);
      settings.target_luminance = .18F;
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = UniformReferenceGain(1),
      }));
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = 0,
      }));
      const auto zero_seed = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kSeedFromEv100, 6.0F);
      if (!zero_seed.has_value()) {
        FAIL() << "Expected zero_seed to contain a value";
      }
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = 1,
        .gain = 0,
      }));
      EXPECT_FLOAT_EQ(state.latent_scale, 0x1p-6F);
      SetSurface(data::MaterialDomain::kOpaque, -1);
      settings.target_luminance = .18F;
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = -1,
        .gain = UniformReferenceGain(1),
      }));
      EXPECT_EQ(state.flags & 12U, 0U);
      settings.min_ev = settings.max_ev = 4;
      const auto remeter = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kRemeter);
      if (!remeter.has_value()) {
        FAIL() << "Expected remeter to contain a value";
      }
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = -1,
        .gain = 0x1p-4,
      }));
      EXPECT_EQ(state.applied_generation.at(0), remeter->generation);
      SetSurface(data::MaterialDomain::kOpaque, .25F);
      const auto locked_seed = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kSeedFromEv100, -5.0F);
      if (!locked_seed.has_value()) {
        FAIL() << "Expected locked_seed to contain a value";
      }
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = .25F,
        .gain = 32,
      }));
      EXPECT_EQ(state.applied_generation.at(0), locked_seed->generation);
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = .25F,
        .gain = 0x1p-4,
      }));
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(render({
        .radiance = .25F,
        .gain = 0,
      }));
      EXPECT_FLOAT_EQ(state.latent_scale, 0x1p-4F);
      ++cases;
    }
  }
  probe->inspect = {};
  reference.reset();
  RecordProperty("scene_mode_camera_cases", cases);
  RecordProperty("scene_mode_frames_checked", checks);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  OffscreenLifetimeReleasePreservesReadersAndFreshReuse)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  std::shared_ptr<const Texture> reference;
  internal::PreviousViewHistoryCache::CurrentState camera_state;
  probe->inspect = [&](const RenderContext& ctx, const SceneTextureExtractRef&,
                     unsigned) -> void {
    reference
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetResolvedSceneColorTexture();
    const auto& resolved = *ctx.current_view.resolved_view;
    camera_state = {
      .view_matrix = resolved.ViewMatrix(),
      .projection_matrix = resolved.ProjectionMatrix(),
      .stable_projection_matrix = resolved.StableProjectionMatrix(),
      .inverse_view_projection_matrix = resolved.InverseViewProjection(),
      .pixel_jitter = resolved.PixelJitter(),
      .viewport = resolved.Viewport(),
    };
  };
  ExposureStateData state;
  for (const bool forward : {
         false,
         true,
       }) {
    surface_view_id = forward ? 4201U : 4200U;
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    if (!seed.has_value()) {
      FAIL() << "Expected seed to contain a value";
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, 0x1p-4, *reference, state));
    const auto retained_state = probe->exposure->current_state;
    const auto retained_source = reference;
    const auto retained_domain = Read<FrameExposureData>(
      *probe->exposure->buffer, ResourceStates::kShaderResource);
    auto& service = OwnedExposureService();
    auto& history
      = vortex::testing::RendererPublicationProbe::PreviousViewHistory(
        *renderer_);
    EXPECT_TRUE(history.TouchCurrent(handle, camera_state).previous_valid);
    ASSERT_TRUE(renderer_->ReleaseOffscreenViewState(
      ViewId {
        surface_view_id,
      },
      handle));
    EXPECT_TRUE(renderer_->ReleaseOffscreenViewState(
      ViewId {
        surface_view_id,
      },
      handle));
    EXPECT_FALSE(
      vortex::testing::RendererPublicationProbe::HasExposureViewState(
        service, handle));
    EXPECT_FALSE(history.TouchCurrent(handle, camera_state).previous_valid);
    const auto old_retry = renderer_->RetryExposureTransition(*seed);
    ASSERT_FALSE(old_retry.has_value());
    EXPECT_EQ(old_retry.error(), ExposureTransitionError::kUnknownToken);
    SetSurface(data::MaterialDomain::kOpaque, 1);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(1, UniformReferenceGain(1), *reference, state));
    EXPECT_NE(probe->exposure->current_state->owner_lifetime, seed->lifetime);
    EXPECT_FALSE(renderer_->RetryExposureTransition(*seed).has_value());
    EXPECT_EQ(Read<ExposureStateData>(
                *retained_state->buffer, ResourceStates::kShaderResource)
                .displayed_scale,
      0x1p-4F);
    EXPECT_NE(retained_source, nullptr);
    const auto retained_pixel = ReadFloatTexture(*retained_source);
    ASSERT_EQ(retained_pixel.size(), 1U);
    for (unsigned channel = 0; channel < 3; ++channel) {
      EXPECT_EQ(
        retained_pixel.at(0).at(channel), .25F * retained_domain.pre_exposure);
    }
    reference.reset();
  }
  EXPECT_FALSE(renderer_->ReleaseOffscreenViewState(kInvalidViewId,
    CompositionView::ViewStateHandle {
      4200U,
    }));
  EXPECT_FALSE(renderer_->ReleaseOffscreenViewState(
    ViewId {
      4200U,
    },
    CompositionView::kInvalidViewStateHandle));
  probe->inspect = {};
  reference.reset();
  RecordProperty("offscreen_release_paths", 2);
}

NOLINT_TEST_F(ExposureLightingGpuTest, PublishedSceneIdleBoundaryAndRecreation)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  std::shared_ptr<const Texture> reference;
  probe->inspect = [&](const RenderContext&, const SceneTextureExtractRef&,
                     unsigned) -> void {
    reference
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetResolvedSceneColorTexture();
  };
  ExposureStateData state;
  for (const bool forward : {
         false,
         true,
       }) {
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    surface_view_id = 9000;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    surface_view_id = forward ? 4301U : 4300U;
    const auto intent = ViewId {
      surface_view_id,
    };
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
      .25F, UniformReferenceGain(.25F), *reference, state));
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    if (!seed.has_value()) {
      FAIL() << "Expected seed to contain a value";
    }
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    const auto published = renderer_->ResolvePublishedRuntimeViewId(intent);
    ASSERT_NE(published, kInvalidViewId);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, 0x1p-4, *reference, state));
    EXPECT_FALSE(renderer_->ReleaseOffscreenViewState(published, handle));
    EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent), published);
    const auto retained = probe->exposure->current_state;
    frame.RemoveView(published);
    const auto last_seen = sequence;
    frame.SetFrameSequenceNumber(
      frame::SequenceNumber {
        last_seen + 60U,
      },
      engine::internal::EngineTagFactory::Get());
    EXPECT_TRUE(renderer_->PruneStalePublishedRuntimeViews(frame).empty());
    EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent), published);
    EXPECT_TRUE(vortex::testing::RendererPublicationProbe::HasExposureViewState(
      OwnedExposureService(), handle));
    frame.SetFrameSequenceNumber(
      frame::SequenceNumber {
        last_seen + 61U,
      },
      engine::internal::EngineTagFactory::Get());
    const auto removed = renderer_->PruneStalePublishedRuntimeViews(frame);
    ASSERT_EQ(removed.size(), 1U);
    EXPECT_EQ(removed.front(), intent);
    EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent), kInvalidViewId);
    EXPECT_FALSE(
      vortex::testing::RendererPublicationProbe::HasExposureViewState(
        OwnedExposureService(), handle));
    EXPECT_FALSE(renderer_->RetryExposureTransition(*seed).has_value());
    sequence = last_seen + 61U;
    SetSurface(data::MaterialDomain::kOpaque, 1);
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(1, UniformReferenceGain(1), *reference, state));
    EXPECT_NE(probe->exposure->current_state->owner_lifetime, seed->lifetime);
    EXPECT_EQ(Read<ExposureStateData>(
                *retained->buffer, ResourceStates::kShaderResource)
                .displayed_scale,
      0x1p-4F);
    renderer_->RemovePublishedRuntimeView(frame, intent);
    EXPECT_FALSE(
      vortex::testing::RendererPublicationProbe::HasExposureViewState(
        OwnedExposureService(), handle));
    reference.reset();
  }
  probe->inspect = {};
  RecordProperty("published_idle_boundary_paths", 2);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, ReplacedSceneRemetersUnlessExplicitPreserveOverrides)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  std::shared_ptr<const Texture> reference;
  probe->inspect = [&](const RenderContext& ctx, const SceneTextureExtractRef&,
                     unsigned) -> void {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    reference
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetResolvedSceneColorTexture();
  };
  ExposureStateData state;
  const auto geometry = mesh_node.GetRenderable().GetGeometry();
  const auto replace_world = [&](float emission) -> void {
    scene = std::make_shared<scene::Scene>("Replacement world", 8U);
    scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    auto& post = scene->GetEnvironment()
                   ->AddSystem<scene::environment::PostProcessVolume>();
    post.SetExposureSettings(settings);
    post.SetToneMapper(engine::ToneMapper::kNone);
    post.SetDisplayGamma(1);
    post.SetBloomIntensity(0);
    camera = scene->CreateNode("Camera");
    auto lens = std::make_unique<scene::PerspectiveCamera>();
    lens->SetViewport(view.viewport);
    ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
    mesh_node = scene->CreateNode("Replacement radiance");
    mesh_node.GetRenderable().SetGeometry(geometry);
    SetSurface(data::MaterialDomain::kOpaque, emission);
    frame.SetScene(observer_ptr {
      scene.get(),
    });
  };
  for (const bool forward : {
         false,
         true,
       }) {
    frame_delta_seconds = 0;
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    surface_view_id = 9000;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    surface_view_id = forward ? 4401U : 4400U;
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
      .25F, UniformReferenceGain(.25F), *reference, state));
    const auto previous = scene;
    ASSERT_NO_FATAL_FAILURE(replace_world(4));
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(4, UniformReferenceGain(4), *reference, state));
    const auto reset = renderer_->InspectExposureTransition(handle);
    if (!reset.has_value()) {
      FAIL() << "Expected reset to contain a value";
    }
    EXPECT_EQ(reset->request.policy, ExposureTransitionPolicy::kRemeter);
    EXPECT_EQ(state.applied_generation.at(0), reset->request.generation);
    const auto held_world = scene;
    const double held_gain = UniformReferenceGain(4);
    const auto preserve = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kPreserve);
    if (!preserve.has_value()) {
      FAIL() << "Expected preserve to contain a value";
    }
    ASSERT_NO_FATAL_FAILURE(replace_world(1));
    frame_delta_seconds = .25F;
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(1, held_gain, *reference, state));
    EXPECT_EQ(state.applied_generation.at(0), preserve->generation);
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(1,
      ReferenceAdaptedGain(held_gain, UniformReferenceGain(1), .25), *reference,
      state));
    renderer_->RemovePublishedRuntimeView(frame,
      ViewId {
        surface_view_id,
      });
    reference.reset();
  }
  probe->inspect = {};
  RecordProperty("world_replacement_paths", 2);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, OffscreenExposureOverridePreservesSceneIntent)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.enabled = false;
  settings.speed_up = settings.speed_down = 7;
  frame_delta_seconds = .25F;
  std::shared_ptr<const Texture> reference;
  probe->inspect = [&](const RenderContext&, const SceneTextureExtractRef&,
                     unsigned) -> void {
    reference
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetResolvedSceneColorTexture();
  };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  for (const bool forward : {
         false,
         true,
       }) {
    SCOPED_TRACE(forward ? "forward" : "deferred");
    surface_view_id = forward ? 4801U : 4800U;
    auto local = scene::ExposureSettings {};
    local.key = 12.5F;
    local.mode = engine::ExposureMode::kManual;
    local.manual_ev = 4;
    local.compensation_ev = 1;
    surface_exposure_override = local;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    ASSERT_NE(reference, nullptr);
    auto state = ExposureStateData {};
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, .125, *reference, state));

    local.mode = engine::ExposureMode::kAuto;
    local.compensation_ev = 0;
    local.low_percentile = 0;
    local.high_percentile = 1;
    local.speed_up = .5F;
    local.speed_down = 1;
    surface_exposure_override = local;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, .125, *reference, state));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, .125 * std::exp2(.25), *reference, state));

    const auto& inherited
      = scene->GetEnvironment()
          ->TryGetSystem<scene::environment::PostProcessVolume>()
          ->GetExposureSettings();
    EXPECT_FALSE(inherited.enabled);
    EXPECT_EQ(inherited.speed_up, 7);
    EXPECT_EQ(inherited.speed_down, 7);
    surface_exposure_override.reset();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(.25F, 1, *reference, state));
    EXPECT_TRUE(renderer_->ReleaseOffscreenViewState(
      ViewId {
        surface_view_id,
      },
      CompositionView::ViewStateHandle {
        surface_view_id,
      }));
  }
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, StatelessSceneAutoRemetersEveryInvocation)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  settings.min_log_luminance = -24;
  settings.log_luminance_range = 56;
  settings.min_ev = -22;
  settings.max_ev = 30;
  const auto bright = MakeEmissiveMaterial(0x1p32F);
  frame_delta_seconds = .25F;
  std::shared_ptr<const Texture> reference;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    EXPECT_EQ(ctx.current_view.view_state_handle,
      CompositionView::kInvalidViewStateHandle);
    EXPECT_EQ(color.texture->GetDescriptor().format, Format::kRGBA32Float);
    reference
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetResolvedSceneColorTexture();
  };
  persistent_surface_state = false;
  unsigned checks = 0;
  for (const bool forward : {
         false,
         true,
       }) {
    // Warm the texture binding without introducing persistent exposure history.
    mesh_node.GetRenderable().SetMaterialOverride(0, 0, bright);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    for (const float luminance : {
           0.0F,
           0x1p-24F,
           .25F,
           4.0F,
           0x1p32F,
           1.0F,
         }) {
      if (luminance == 0x1p32F) {
        mesh_node.GetRenderable().SetMaterialOverride(0, 0, bright);
      } else {
        SetSurface(data::MaterialDomain::kOpaque, luminance);
      }
      observer_ptr<FrameCaptureController> capture;
      if (!forward && luminance == 0x1p32F) {
        capture = BeginOptionalCapture();
      }
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      if (capture) {
        EXPECT_TRUE(capture->EndCapture());
      }
      ASSERT_NE(reference, nullptr);
      ExposureStateData state;
      ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
        luminance, UniformReferenceGain(luminance), *reference, state));
      const auto domain = Read<FrameExposureData>(
        *probe->exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(domain.pre_exposure, 1);
      EXPECT_FALSE(
        vortex::testing::RendererPublicationProbe::HasExposureViewState(
          OwnedExposureService(), CompositionView::kInvalidViewStateHandle));
      EXPECT_FALSE(
        vortex::testing::RendererPublicationProbe::HasExposureViewState(
          OwnedExposureService(),
          CompositionView::ViewStateHandle {
            surface_view_id,
          }));
      ++checks;
    }
    for (const float invalid : {
           -1.0F,
           std::numeric_limits<float>::quiet_NaN(),
         }) {
      SetSurface(data::MaterialDomain::kOpaque, invalid);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      ExposureStateData state;
      ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(-1, 1, *reference, state));
      EXPECT_EQ(state.flags & 12U, 0U);
      const auto report = Read<ExposureCompletedStatus>(
        *probe->exposure->current_state->status_buffer,
        ResourceStates::kCopySource);
      EXPECT_NE(report.flags & 16U, 0U);
      EXPECT_NE(report.first_failure_kind, 0U);
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(-1, 0, *reference, state));
      settings.target_luminance = .18F;
      SetSurface(data::MaterialDomain::kOpaque, .25F);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
        .25F, UniformReferenceGain(.25F), *reference, state));
      checks += 3;
    }
  }
  probe->inspect = {};
  RecordProperty("stateless_scene_frames", checks);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, SceneDeviceRecoveryRejectsPriorEligibility)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  std::shared_ptr<const Texture> reference;
  SceneTextureExtractRef current;
  probe->inspect = [&](const RenderContext&,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    current = color;
    reference
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetResolvedSceneColorTexture();
  };
  for (const bool forward : {
         false,
         true,
       }) {
    surface_view_id = forward ? 4601U : 4600U;
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto old = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    if (!old.has_value()) {
      FAIL() << "Expected old to contain a value";
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ExposureStateData state;
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, .0625, *reference, state));
    EXPECT_EQ(state.applied_generation.at(0), old->generation);
    // Queue newer intent before polling the completed old GPU submission.
    const auto newer = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 3.0F);
    if (!newer.has_value()) {
      FAIL() << "Expected newer to contain a value";
    }
    ASSERT_TRUE(renderer_
        ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
        .has_value());
    SetSurface(data::MaterialDomain::kOpaque, 1);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(1, .125, *reference, state));
    EXPECT_EQ(state.applied_generation.at(0), newer->generation);
    EXPECT_EQ(InspectRequiredTransition(handle).request, *newer);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    ASSERT_TRUE(renderer_
        ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
        .has_value());
    SetSurface(data::MaterialDomain::kOpaque, 4);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(4, UniformReferenceGain(4), *reference, state));
    const auto domain = Read<FrameExposureData>(
      *probe->exposure->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(domain.pre_exposure, 1);
    const auto recovery = renderer_->InspectExposureTransition(handle);
    if (!recovery.has_value()) {
      FAIL() << "Expected recovery to contain a value";
    }
    EXPECT_GT(recovery->request.generation, newer->generation);
    EXPECT_EQ(recovery->request.policy, ExposureTransitionPolicy::kRemeter);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    EXPECT_EQ(InspectRequiredTransition(handle).phase,
      ExposureTransitionPhase::kApplied);
    EXPECT_TRUE(renderer_->ReleaseOffscreenViewState(
      ViewId {
        surface_view_id,
      },
      handle));
  }
  probe->inspect = {};
  RecordProperty("device_recovery_scene_paths", 2);
}

} // namespace oxygen::vortex::testing::exposure
