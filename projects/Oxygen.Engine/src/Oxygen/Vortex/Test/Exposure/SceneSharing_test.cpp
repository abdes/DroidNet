//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <utility>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::vortex::testing::exposure {

using graphics::FramebufferDesc;
using graphics::ResourceStates;

NOLINT_TEST_F(ExposureLightingGpuTest, SharedSceneLifecycleForward)
{
  QualifySharedSceneLifecycle(true);
}

NOLINT_TEST_F(ExposureLightingGpuTest, SharedSceneLifecycleDeferred)
{
  QualifySharedSceneLifecycle(false);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ProductionWideRangeRetainsAdaptationAndQualifiesSharedViewsIndependently)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  frame_delta_seconds = .1F;
  settings.mode = engine::ExposureMode::kAuto;
  settings.min_log_luminance = -24;
  settings.log_luminance_range = 56;
  settings.speed_up = settings.speed_down = .25F;
  view.viewport = {
    .width = 2,
    .height = 1,
  };
  auto lens = std::make_unique<scene::OrthographicCamera>();
  lens->SetExtents(-1, 1, -.5F, .5F, .1F, 10);
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.ReplaceCamera(std::move(lens)));
  auto output = CreateRegisteredTexture({
    .width = 2,
    .height = 1,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  mesh_node.GetTransform().SetLocalScale({
    .25F,
    1,
    1,
  });
  mesh_node.GetTransform().SetLocalPosition({
    -.5F,
    0,
    0,
  });
  auto dim = scene->CreateNode("Required dark signal");
  dim.GetRenderable().SetGeometry(mesh_node.GetRenderable().GetGeometry());
  dim.GetTransform().SetLocalScale({
    .25F,
    1,
    1,
  });
  dim.GetTransform().SetLocalPosition({
    .5F,
    0,
    0,
  });
  expected_draws = 2;
  const auto bright = MakeEmissiveMaterial(0x1p30F);
  const auto changed_bright = MakeEmissiveMaterial(0x1p29F);
  const auto dark = MakeEmissiveMaterial(0x1p-16F);
  const auto ordinary = MakeEmissiveMaterial(.25F);
  const auto set_pair = [&](const auto& primary_material,
                          const auto& secondary_material) -> void {
    mesh_node.GetRenderable().SetMaterialOverride(0, 0, primary_material);
    dim.GetRenderable().SetMaterialOverride(0, 0, secondary_material);
  };
  SceneTextureExtractRef current;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    current = color;
  };
  const auto read_state = [&] -> oxygen::vortex::ExposureStateData {
    return Read<ExposureStateData>(*current.exposure->current_state->buffer,
      ResourceStates::kShaderResource);
  };
  // Make both material texture bindings resident before measuring adaptation.
  set_pair(changed_bright, dark);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
  set_pair(bright, dark);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
  const auto initialized = renderer_->QueueExposureTransition(
    CompositionView::ViewStateHandle {
      100U,
    },
    ExposureTransitionPolicy::kRemeter);
  if (!initialized.has_value()) {
    FAIL() << "Expected initialized to contain a value";
  }
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 2));
  auto before = read_state();
  EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(before.flags & 12U, 12U);
  const auto pixels = ReadFloatTexture(*current.texture);
  ASSERT_EQ(pixels.size(), 2U);
  EXPECT_EQ(pixels.at(0),
    (Pixel {
      0x1p30F,
      0x1p30F,
      0x1p30F,
      1,
    }));
  EXPECT_EQ(pixels.at(1),
    (Pixel {
      0x1p-16F,
      0x1p-16F,
      0x1p-16F,
      1,
    }));
  set_pair(changed_bright, dark);
  for (unsigned index = 0; index < 8; ++index) {
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    const auto state = read_state();
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    EXPECT_EQ(state.flags & 12U, 12U);
    EXPECT_GT(state.displayed_scale, before.displayed_scale);
    EXPECT_LT(state.displayed_scale, state.target_scale);
    EXPECT_EQ(state.requested_generation, before.requested_generation);
    before = state;
  }
  set_pair(ordinary, ordinary);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
  EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
  EXPECT_EQ(read_state().requested_generation, before.requested_generation);

  auto source_settings = settings;
  source_settings.mode = engine::ExposureMode::kManual;
  source_settings.manual_ev = 0;
  const auto source_handle = CompositionView::ViewStateHandle {
    800U,
  };
  const auto root = PublishExposureOwner(frame,
    ViewId {
      800U,
    },
    source_handle, source_settings);
  ctx_.scene = observer_ptr {
    scene.get(),
  };
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle = source_handle;
  sequence_ = sequence;
  ServicePixel(OwnedExposureService(), Uniform(.25F, 4U, 4U), source_settings);
  sequence = static_cast<unsigned>(sequence_);
  surface_source_id = ViewId {
    800U,
  };
  for (unsigned frame_index = 0; frame_index < 8; ++frame_index) {
    for (unsigned order = 0; order < 2; ++order) {
      const bool wide = (order + frame_index) % 2 == 0;
      surface_view_id = wide ? 110U : 111U;
      set_pair(wide ? bright : ordinary, wide ? dark : ordinary);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
      EXPECT_EQ(read_state().displayed_scale, 1.0F);
      if (frame_index >= 5) {
        EXPECT_EQ(current.texture->GetDescriptor().format,
          wide ? Format::kRGBA32Float : Format::kRGBA16Float);
      }
      EXPECT_FALSE(renderer_
          ->InspectExposureTransition(CompositionView::ViewStateHandle {
            surface_view_id,
          })
          .has_value());
    }
  }
  EXPECT_FALSE(renderer_->InspectExposureTransition(source_handle).has_value());
  probe->inspect = {};
  current = {};
  RecordProperty("wide_range_stable_adaptation_frames", 8);
  RecordProperty("contrasting_shared_consumer_frames", 16);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ProductionRecoveryRetainsHistoryAndResetsOnlyAutoOwner)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  frame_delta_seconds = .1F;
  const auto root_handle = CompositionView::ViewStateHandle {
    800U,
  };
  auto source_settings = settings;
  const auto root = PublishExposureOwner(frame,
    ViewId {
      800U,
    },
    root_handle, source_settings);
  ctx_.scene = observer_ptr {
    scene.get(),
  };
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle = root_handle;
  ServicePixel(OwnedExposureService(), Uniform(.25F, 4U, 4U), source_settings);
  sequence = static_cast<unsigned>(sequence_);
  SceneTextureExtractRef current;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    current = color;
  };
  const auto read_state = [&] -> oxygen::vortex::ExposureStateData {
    return Read<ExposureStateData>(*current.exposure->current_state->buffer,
      ResourceStates::kShaderResource);
  };
  for (unsigned mode = 0; mode < 5; ++mode) {
    SCOPED_TRACE(mode);
    surface_view_id = 100U + mode;
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    settings.enabled = mode != 2;
    settings.mode
      = mode == 1 ? engine::ExposureMode::kManual : engine::ExposureMode::kAuto;
    surface_source_id = mode == 3 ? ViewId { 800U, } : kInvalidViewId;
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto before = read_state();
    const auto old_request = renderer_->InspectExposureTransition(handle);
    const auto generation = old_request ? old_request->request.generation : 0U;
    SetSurface(data::MaterialDomain::kOpaque, -1);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    auto status = Read<ExposureCompletedStatus>(
      *current.exposure->current_state->status_buffer,
      ResourceStates::kCopySource);
    EXPECT_NE(status.flags & 16U, 0U);
    const auto failed = read_state();
    EXPECT_EQ(failed.displayed_scale, before.displayed_scale);
    EXPECT_EQ(failed.latent_scale, before.latent_scale);
    if (mode == 4) {
      ASSERT_TRUE(renderer_
          ->QueueExposureTransition(handle, ExposureTransitionPolicy::kPreserve)
          .has_value());
    }
    for (unsigned frame_index = 0; frame_index < 6; ++frame_index) {
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
      EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
      const auto held = read_state();
      EXPECT_EQ(held.displayed_scale, before.displayed_scale);
      EXPECT_EQ(held.latent_scale, before.latent_scale);
      const auto request = renderer_->InspectExposureTransition(handle);
      EXPECT_EQ(request ? request->request.generation : 0U,
        generation + (mode == 0 || mode == 4 ? 1U : 0U));
      if (mode == 0) {
        if (!request.has_value()) {
          FAIL() << "Expected request to contain a value";
        }
        EXPECT_EQ(request->request.policy, ExposureTransitionPolicy::kRemeter);
        EXPECT_EQ(request->phase, ExposureTransitionPhase::kQueued);
      }
    }
    SetSurface(data::MaterialDomain::kOpaque, .5F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    const auto repaired = read_state();
    if (mode == 0) {
      EXPECT_EQ(repaired.requested_generation, repaired.applied_generation);
      EXPECT_EQ(repaired.displayed_scale, repaired.target_scale);
      EXPECT_LT(repaired.displayed_scale, before.displayed_scale);
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto request = renderer_->InspectExposureTransition(handle);
    EXPECT_EQ(request ? request->request.generation : 0U,
      generation + (mode == 0 || mode == 4 ? 1U : 0U));
    EXPECT_FALSE(renderer_->InspectExposureTransition(root_handle).has_value());
  }
  probe->inspect = {};
  current = {};
  RecordProperty("recovery_owner_modes", 5);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ProductionConversionRecoveryHonorsNewerExplicitTransition)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  frame_delta_seconds = .1F;
  SceneTextureExtractRef current;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    current = color;
  };
  for (const bool explicit_request : {
         false,
         true,
       }) {
    SCOPED_TRACE(explicit_request);
    surface_view_id = explicit_request ? 101U : 100U;
    const auto handle = CompositionView::ViewStateHandle {
      surface_view_id,
    };
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto old = renderer_->InspectExposureTransition(handle);
    const auto generation = old ? old->request.generation : 0U;
    SetSurface(data::MaterialDomain::kOpaque, 65504);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto status = Read<ExposureCompletedStatus>(
      *current.exposure->current_state->status_buffer,
      ResourceStates::kCopySource);
    EXPECT_NE(status.flags & 32U, 0U);
    EXPECT_EQ(status.flags & 16U, 0U);
    const auto before_recovery
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_EQ(before_recovery.flags & 12U, 12U);
    if (explicit_request) {
      ASSERT_TRUE(renderer_
          ->QueueExposureTransition(
            handle, ExposureTransitionPolicy::kSeedFromEv100, -3.0F)
          .has_value());
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    const auto request = renderer_->InspectExposureTransition(handle);
    EXPECT_EQ(request ? request->request.generation : 0U,
      generation + (explicit_request ? 1U : 0U));
    if (explicit_request) {
      if (!request.has_value()) {
        FAIL() << "Expected request to contain a value";
      }
      EXPECT_EQ(
        request->request.policy, ExposureTransitionPolicy::kSeedFromEv100);
    }
    const auto state
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_EQ(state.applied_generation, state.requested_generation);
    if (explicit_request) {
      EXPECT_EQ(state.displayed_scale, 8.0F);
    } else {
      EXPECT_LT(state.displayed_scale, before_recovery.displayed_scale);
      EXPECT_GT(state.displayed_scale, state.target_scale);
    }
  }
  probe->inspect = {};
  current = {};
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  MixedPrecisionFamilyAuxiliaryHandoffUsesMappedProducerOutput)
{
  QualifyMixedPrecisionAuxiliaryHandoff(false);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  SplitSceneAndCompositeTargetsPreserveAuxiliaryMappedOutput)
{
  QualifyMixedPrecisionAuxiliaryHandoff(true);
}

} // namespace oxygen::vortex::testing::exposure
