//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestEngine.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::Texture;

auto ExposureLightingGpuTest::QualifySharedSceneLifecycle(bool forward) -> void
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  settings.speed_up = settings.speed_down = 1;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  const auto source_mesh = mesh_node;
  auto consumer_mesh = scene->CreateNode("Consumer radiance");
  consumer_mesh.GetRenderable().SetGeometry(
    mesh_node.GetRenderable().GetGeometry());
  consumer_mesh.GetTransform().SetLocalPosition({
    20,
    0,
    0,
  });
  mesh_node = consumer_mesh;
  SetSurface(data::MaterialDomain::kOpaque, 1);
  mesh_node = source_mesh;
  auto consumer_camera = scene->CreateNode("Consumer camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(consumer_camera.AttachCamera(std::move(lens)));
  consumer_camera.GetTransform().SetLocalPosition({
    20,
    0,
    0,
  });
  const auto consumer_lens
    = consumer_camera.GetCameraAs<scene::PerspectiveCamera>();
  const auto source_lens = camera.GetCameraAs<scene::PerspectiveCamera>();
  if (!consumer_lens.has_value() || !source_lens.has_value()) {
    FAIL() << "Expected both perspective cameras";
  }
  consumer_lens->get().SetExposure({
    .aperture_f = 2,
    .shutter_rate = 4,
    .iso = 100,
  });
  source_lens->get().SetExposure({
    .aperture_f = 2,
    .shutter_rate = 4,
    .iso = 100,
  });
  // Prepared metadata includes both meshes; per-view pixels verify visibility.
  expected_draws = 2;
  surface_view_id = 9000;
  ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
  const auto source_camera = camera;
  camera = consumer_camera;
  surface_view_id = 9001;
  ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
  ASSERT_GT(ReadFloatTexture(*probe->color, true).front().at(0), 0);
  camera = source_camera;
  const std::array ids {
    ViewId {
      4500U,
    },
    ViewId {
      4501U,
    },
  };
  const std::array handles {
    CompositionView::ViewStateHandle {
      4500U,
    },
    CompositionView::ViewStateHandle {
      4501U,
    },
  };
  const std::array cameras {
    camera,
    consumer_camera,
  };
  auto consumer_output = CreateRegisteredTexture({
    .width = 1,
    .height = 1,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  const std::array targets {
    framebuffer,
    Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(consumer_output)),
  };
  auto consumer_settings = settings;
  // Deliberately different consumer settings must not alter the borrowed gain.
  consumer_settings.compensation_ev = 3;
  std::array<postprocess::ExposurePass::FrameLease, 2> exposures;
  std::array<std::shared_ptr<const Texture>, 2> references;
  std::vector<unsigned> rendered_order;
  auto* owner
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  probe->inspect
    = [&](const RenderContext& ctx, const SceneTextureExtractRef& color,
        unsigned draws) -> void {
    const unsigned index
      = ctx.current_view.view_state_handle == handles.at(0) ? 0 : 1;
    exposures.at(index) = color.exposure;
    references.at(index) = owner->GetResolvedSceneColorTexture();
    rendered_order.push_back(index);
    EXPECT_EQ(draws, 2U);
  };
  bool shared = true;
  bool continuity_frame = false;
  bool render_source = true;
  bool diagnostic = false;
  auto profile = CompositionView::ViewFeatureProfile::kDefault;
  unsigned frames_checked = 0;
  unsigned source_first = 0;
  unsigned consumer_first = 0;
  const auto run = [&](double source_gain, double consumer_gain,
                     float consumer_luminance = 1) -> void {
    SCOPED_TRACE(::testing::Message()
      << "scene frame=" << frames_checked << " source=" << source_gain
      << " consumer=" << consumer_gain);
    const auto capture = frames_checked == 0 ? BeginOptionalCapture() : nullptr;
    rendered_order.clear();
    exposures = {};
    references = {};
    scene->Update();
    scene->SyncObservers();
    auto timing = engine::ModuleTimingData {};
    timing.game_delta_time = time::CanonicalDuration {
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double> {
          frame_delta_seconds,
        }),
    };
    frame.SetModuleTimingData(
      timing, engine::internal::EngineTagFactory::Get());
    const auto slot = frame::Slot {
      sequence % 3U,
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
    const bool reversed = frames_checked % 2 != 0;
    const auto publish = [&](unsigned index) -> void {
      if (index == 0 && !render_source) {
        return;
      }
      auto input
        = CompositionView::ForScene(ids.at(index), view, cameras.at(index));
      input.view_state_handle = handles.at(index);
      input.exposure_source_view_id
        = index == 1 && shared ? ids.at(0) : kInvalidViewId;
      input.render_settings.exposure
        = index == 0 ? settings : consumer_settings;
      input.render_settings.shader_debug_mode = index == 0 && diagnostic
        ? ShaderDebugMode::kWorldNormals
        : ShaderDebugMode::kDisabled;
      input.feature_profile
        = index == 1 ? profile : CompositionView::ViewFeatureProfile::kDefault;
      // Optional depth dependencies order the native runtime path without
      // replacing either view's color; depth extraction is not required here.
      if (render_source) {
        if (index == static_cast<unsigned>(reversed)) {
          input.produced_aux_outputs.push_back(
            { .id = CompositionView::AuxOutputId { 4502U, },
              .kind = CompositionView::AuxOutputKind::kDepthTexture,
              .debug_name = "Shared exposure order", });
        } else {
          input.consumed_aux_outputs.push_back(
            { .id = CompositionView::AuxOutputId { 4502U, },
              .kind = CompositionView::AuxOutputKind::kDepthTexture,
              .required = false, });
        }
      }
      ASSERT_NE(
        renderer_->PublishRuntimeCompositionView(frame,
          { .composition_view = input,
            .render_target = observer_ptr { targets.at(index).get(), },
            .composite_source = observer_ptr { targets.at(index).get(), }, },
          forward ? ShadingMode::kForward : ShadingMode::kDeferred),
        kInvalidViewId);
    };
    const bool consumer_first_publication = reversed
      && renderer_->ResolvePublishedRuntimeViewId(ids.at(0)) != kInvalidViewId;
    ASSERT_NO_FATAL_FAILURE(publish(consumer_first_publication ? 1 : 0));
    ASSERT_NO_FATAL_FAILURE(publish(consumer_first_publication ? 0 : 1));
    auto loop = co::testing::TestEventLoop {};
    // co::Run completes synchronously before this closure and its captured
    // fixture state leave scope.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr {
        &frame,
      });
      co_await renderer_->OnRender(observer_ptr {
        &frame,
      });
    });
    renderer_->OnFrameEnd(observer_ptr {
      &frame,
    });
    Backend().EndFrame(
      frame::SequenceNumber {
        sequence,
      },
      slot);
    WaitForQueueIdle();
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
    if (render_source) {
      EXPECT_EQ(rendered_order,
        (reversed ? std::vector<unsigned> { 1, 0, }
                  : std::vector<unsigned> { 0, 1, }));
      reversed ? ++consumer_first : ++source_first;
    } else {
      EXPECT_EQ(rendered_order,
        (std::vector<unsigned> {
          1,
        }));
    }
    for (unsigned index = 0; index < 2; ++index) {
      if (index == 0 && (!render_source || diagnostic)) {
        continue;
      }
      ASSERT_NE(exposures.at(index), nullptr);
      ASSERT_NE(references.at(index), nullptr);
      const auto saved_target = framebuffer;
      framebuffer = targets.at(index);
      probe->exposure = exposures.at(index);
      ExposureStateData state;
      ExpectSurfaceExposure(index == 0 ? .25F : consumer_luminance,
        index == 0 ? source_gain : consumer_gain, *references.at(index), state);
      framebuffer = saved_target;
      if (index == 1) {
        EXPECT_EQ((state.flags & 128U) != 0,
          shared
            || (continuity_frame && consumer_settings.enabled
              && consumer_settings.mode == engine::ExposureMode::kAuto));
      }
    }
    continuity_frame = false;
    ++frames_checked;
  };
  const double auto_gain = UniformReferenceGain(.25F);
  ASSERT_NO_FATAL_FAILURE(
    run(auto_gain, 1)); // Unpublished owner's EV0 fallback.
  ASSERT_NO_FATAL_FAILURE(run(auto_gain, auto_gain));
  mesh_node = consumer_mesh;
  SetSurface(data::MaterialDomain::kOpaque, 4);
  mesh_node = source_mesh;
  ASSERT_NO_FATAL_FAILURE(run(auto_gain, auto_gain, 4));
  mesh_node = consumer_mesh;
  SetSurface(data::MaterialDomain::kOpaque, 1);
  mesh_node = source_mesh;
  settings.target_luminance = 0;
  ASSERT_NO_FATAL_FAILURE(run(0, auto_gain));
  ASSERT_NO_FATAL_FAILURE(run(0, 0));
  settings.target_luminance = .18F;
  ASSERT_NO_FATAL_FAILURE(run(auto_gain, 0));
  ASSERT_NO_FATAL_FAILURE(run(auto_gain, auto_gain));
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 3;
  ASSERT_NO_FATAL_FAILURE(run(.125, auto_gain));
  ASSERT_NO_FATAL_FAILURE(run(.125, .125));
  settings.mode = engine::ExposureMode::kManualCamera;
  ASSERT_NO_FATAL_FAILURE(run(.0625, .125));
  ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
  settings.enabled = false;
  ASSERT_NO_FATAL_FAILURE(run(1, .0625));
  ASSERT_NO_FATAL_FAILURE(run(1, 1));
  settings.enabled = true;
  settings.mode = engine::ExposureMode::kAuto;
  ASSERT_NO_FATAL_FAILURE(run(1, 1)); // Resume with last displayed fixed gain.
  const auto seed = renderer_->QueueExposureTransition(
    handles.at(0), ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
  ASSERT_TRUE(seed.has_value());
  diagnostic = true;
  for (unsigned i = 0; i < 3; ++i) {
    ASSERT_NO_FATAL_FAILURE(run(1, 1));
  }
  const auto pending = renderer_->InspectExposureTransition(handles.at(0));
  if (!pending.has_value()) {
    FAIL() << "Expected the queued transition";
  }
  EXPECT_EQ(pending->phase, ExposureTransitionPhase::kQueued);
  diagnostic = false;
  ASSERT_NO_FATAL_FAILURE(run(.0625, 1));
  ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
  EXPECT_EQ(Read<ExposureStateData>(*exposures.at(0)->current_state->buffer,
              ResourceStates::kShaderResource)
              .applied_generation.at(0),
    seed->generation);
  for (const auto variant : {
         CompositionView::ViewFeatureProfile::kNoEnvironment,
         CompositionView::ViewFeatureProfile::kNoShadowing,
         CompositionView::ViewFeatureProfile::kNoVolumetrics,
       }) {
    profile = variant;
    ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
  }
  profile = CompositionView::ViewFeatureProfile::kDefault;
  frame.RemoveView(renderer_->ResolvePublishedRuntimeViewId(ids.at(0)));
  render_source = false;
  sequence += 61U;
  ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
  EXPECT_TRUE(renderer_->PruneStalePublishedRuntimeViews(frame).empty());
  EXPECT_NE(
    renderer_->ResolvePublishedRuntimeViewId(ids.at(0)), kInvalidViewId);
  EXPECT_TRUE(vortex::testing::RendererPublicationProbe::HasExposureViewState(
    OwnedExposureService(), handles.at(0)));
  // The source is absent from this frame but still registered until explicit
  // removal.
  renderer_->RemovePublishedRuntimeView(ids.at(0));
  shared = false;
  continuity_frame = true;
  consumer_settings.compensation_ev = 0;
  frame_delta_seconds = .25F;
  ASSERT_NO_FATAL_FAILURE(run(0, .0625));
  const double adapted
    = ReferenceAdaptedGain(.0625, UniformReferenceGain(1), .25);
  ASSERT_NO_FATAL_FAILURE(run(0, adapted));
  EXPECT_FALSE(renderer_->RetryExposureTransition(*seed).has_value());
  renderer_->RemovePublishedRuntimeView(frame, ids.at(1));
  // Recreate the pair for each source-loss policy, retaining the same scene.
  for (unsigned variant = 0; variant < 6; ++variant) {
    SCOPED_TRACE(::testing::Message() << "source-loss variant=" << variant);
    settings.enabled = true;
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = 4;
    settings.target_luminance = .18F;
    consumer_settings = settings;
    if (variant == 2) {
      consumer_settings.mode = engine::ExposureMode::kManual;
    } else if (variant == 3) {
      consumer_settings.mode = engine::ExposureMode::kManualCamera;
    } else {
      consumer_settings.mode = engine::ExposureMode::kAuto;
    }
    consumer_settings.manual_ev = 2;
    consumer_settings.enabled = variant != 4;
    consumer_settings.target_luminance = variant == 5 ? 0 : .18F;
    render_source = shared = true;
    frame_delta_seconds = 0;
    ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
    ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
    if (variant == 0) {
      settings.mode = engine::ExposureMode::kAuto;
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(run(0, .0625));
      ASSERT_NO_FATAL_FAILURE(run(0, 0));
    }
    renderer_->RemovePublishedRuntimeView(frame, ids.at(0));
    render_source = shared = false;
    continuity_frame = true;
    settings.target_luminance = .18F;
    frame_delta_seconds = .25F;
    double first = .0625;
    if (variant == 0 || variant == 5) {
      first = 0;
    } else if (variant == 2) {
      first = .25;
    } else if (variant == 4) {
      first = 1;
    }
    ASSERT_NO_FATAL_FAILURE(run(0, first));
    const double next = variant <= 1
      ? ReferenceAdaptedGain(.0625, UniformReferenceGain(1), .25)
      : first;
    ASSERT_NO_FATAL_FAILURE(run(0, next));
    renderer_->RemovePublishedRuntimeView(frame, ids.at(1));
  }
  probe->inspect = {};
  RecordProperty("shared_scene_frames", frames_checked);
  RecordProperty("source_first_frames", source_first);
  RecordProperty("consumer_first_frames", consumer_first);
}

} // namespace oxygen::vortex::testing::exposure
