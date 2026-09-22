//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::Texture;
using graphics::TextureDesc;

auto ExposureGpuTest::CheckOffscreenSharing(const bool inside_frame) -> void
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto scene = std::make_shared<scene::Scene>("OffscreenExposure", 4U);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = {
    .width = 4.0F,
    .height = 4.0F,
  };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  const auto root = PublishExposureOwner(frame,
    ViewId {
      800U,
    },
    CompositionView::ViewStateHandle {
      90U,
    },
    settings);
  ASSERT_NE(root, kInvalidViewId);
  ASSERT_NE(root,
    (ViewId {
      800U,
    }));
  PublishExposureOwner(frame,
    ViewId {
      801U,
    },
    CompositionView::ViewStateHandle {
      91U,
    },
    settings,
    ViewId {
      800U,
    });
  auto output = CreateRegisteredTexture(TextureDesc {
    .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto input = Renderer::OffscreenSceneViewInput::FromCamera(
    "Offscreen", root, view, camera);
  input.SetExposureSourceViewId(ViewId {
    801U,
  });
  input.SetViewStateHandle(CompositionView::ViewStateHandle {
    92U,
  });
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession({ .frame_slot = frame::Slot { 0U, },
    .frame_sequence = frame::SequenceNumber { 1U, },
    .delta_time_seconds = 0.0F, });
  facade.SetSceneSource({ .scene = observer_ptr {
                            scene.get(),
                          } });
  facade.SetOutputTarget({ .framebuffer = observer_ptr {
                             framebuffer.get(),
                           } });
  facade.SetViewIntent(input);
  for (const auto& issue : facade.Validate().issues) {
    ADD_FAILURE() << issue.code << ": " << issue.message;
  }
  auto session = facade.Finalize();
  ASSERT_TRUE(session.has_value());
  if (inside_frame) {
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
  } else {
    ASSERT_TRUE(session->ExecuteNow());
  }
  WaitForQueueIdle();
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  EXPECT_TRUE(service->GetLastExecutionState().tonemap_executed);
  EXPECT_FALSE(service->GetLastExecutionState().auto_exposure_requested);
  const auto states
    = vortex::testing::RendererPublicationProbe::FrameExposureStates(*service,
      frame::Slot {
        0U,
      });
  ASSERT_GE(states.size(), 2U);
  const auto state = Read<ExposureStateData>(
    *states.back()->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(state.displayed_scale, 0x1p-4F);
  EXPECT_NE(state.flags & 128U, 0U);
  EXPECT_EQ(states.back()->histogram_buffer, nullptr);
  EXPECT_FALSE(vortex::testing::RendererPublicationProbe::HasExposureViewState(
    *service, CompositionView::kInvalidViewStateHandle));
  // Rejected borrowers must neither replace the source image history nor
  // consume its queued request, including ownership changes after Finalize.
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    90U,
  };
  EXPECT_NEAR(ServicePixel(*service, Uniform(.25F, 4U, 4U), settings),
    .25F / 16.0F, 2e-5F);
  const auto source_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(*service,
      CompositionView::ViewStateHandle {
        90U,
      });
  ASSERT_NE(source_state, nullptr);
  const auto request = renderer_->QueueExposureTransition(
    CompositionView::ViewStateHandle {
      90U,
    },
    ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(request.has_value());
  for (const auto handle : {
         CompositionView::kInvalidViewStateHandle,
         CompositionView::ViewStateHandle {
           90U,
         },
         CompositionView::ViewStateHandle {
           91U,
         },
       }) {
    input.SetViewStateHandle(handle);
    facade.SetViewIntent(input);
    EXPECT_FALSE(facade.Finalize().has_value());
  }
  ASSERT_NE(PublishExposureOwner(frame,
              ViewId {
                802U,
              },
              CompositionView::ViewStateHandle {
                92U,
              },
              settings),
    kInvalidViewId);
  EXPECT_FALSE(session->ExecuteNow());
  EXPECT_FALSE(session->ExecuteInsideFrame(frame));
  EXPECT_EQ(
    vortex::testing::RendererPublicationProbe::ExposureStateForView(*service,
      CompositionView::ViewStateHandle {
        90U,
      }),
    source_state);
  EXPECT_EQ(Read<ExposureStateData>(
              *source_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-4F);
  const auto status
    = renderer_->InspectExposureTransition(CompositionView::ViewStateHandle {
      90U,
    });
  if (!status.has_value()) {
    FAIL() << "Expected the queued exposure transition";
  }
  EXPECT_EQ(status->request, *request);
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  FlushBackend();
}

auto ExposureGpuTest::CheckSceneExposureRetry(
  const bool inside_frame, const bool late_failure) -> void
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto scene = std::make_shared<scene::Scene>("ExposureRetryScene", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = {
    .width = 4.0F,
    .height = 4.0F,
  };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto output = CreateRegisteredTexture({
    .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  const Pixel sentinel {
    .125F,
    .25F,
    .5F,
    1.0F,
  };
  std::array<std::byte, 1024U> bytes {};
  for (unsigned y = 0U; y < 4U; ++y) {
    for (unsigned x = 0U; x < 4U; ++x) {
      const auto offset
        = (static_cast<std::size_t>(y) * 256U) + (x * sizeof(Pixel));
      std::memcpy(
        std::span {
          bytes,
        }
          .subspan(offset, sizeof(Pixel))
          .data(),
        sentinel.data(), sizeof(Pixel));
    }
  }
  auto upload = CreateUploadBuffer(SizeBytes {
    bytes.size(),
  });
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Prior offscreen output");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, output, ResourceStates::kCommon);
    recorder->RequireResourceState(*output, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      {
        .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 1024U,
        .dst_slice = { .width = 4U, .height = 4U, .depth = 1U },
      },
      *output);
    recorder->RequireResourceStateFinal(
      *output, ResourceStates::kShaderResource);
  }
  const auto read_pixel = [&] -> Pixel {
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Offscreen retry pixel");
    {
      auto recorder = AcquireRecorder("Offscreen retry readback");
      CHECK_F(recorder->AdoptKnownResourceState(*output));
      CHECK_F(readback
          ->EnqueueCopy(*recorder, *output,
            { .src_slice
              = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U, }, })
          .has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Pixel pixel {};
    std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
    return pixel;
  };
  const auto handle = CompositionView::ViewStateHandle {
    7000U,
  };
  auto seed = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  auto input = Renderer::OffscreenSceneViewInput::FromCamera("Retry",
    ViewId {
      7000U,
    },
    view, camera);
  input.SetViewStateHandle(handle);
  auto successful_input
    = Renderer::OffscreenSceneViewInput::FromCamera("Successful sibling",
      ViewId {
        6999U,
      },
      view, camera);
  successful_input.SetViewStateHandle(CompositionView::ViewStateHandle {
    6999U,
  });
  auto successful_output = CreateRegisteredTexture(output->GetDescriptor());
  auto successful_target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(successful_output));
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr {
    scene.get(),
  });
  auto invoke
    = [&](const unsigned sequence, const bool sibling = false) -> bool {
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = frame::Slot { sequence - 1U, },
      .frame_sequence = frame::SequenceNumber { sequence, },
      .delta_time_seconds = 0.0F, });
    facade.SetSceneSource({ .scene = observer_ptr {
                              scene.get(),
                            } });
    facade.SetViewIntent(sibling ? successful_input : input);
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr {
          sibling ? successful_target.get() : framebuffer.get(), }, });
    auto session = facade.Finalize();
    CHECK_F(session.has_value());
    if (!inside_frame) {
      return session->ExecuteNow();
    }
    frame.SetFrameSequenceNumber(
      frame::SequenceNumber {
        sequence,
      },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(
      frame::Slot {
        sequence - 1U,
      },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr {
      &frame,
    });
    const auto finish_frame = ScopeGuard(
      [&] noexcept -> void { renderer_->OnFrameEnd(observer_ptr { &frame }); });
    return session->ExecuteInsideFrame(frame);
  };
  struct AbortRecordedView final : IViewExtension {
    void Arm() noexcept { armed_ = true; }
    void OnPostRenderViewGpu(const ViewRenderGpuContext& /*context*/) override
    {
      if (std::exchange(armed_, false)) {
        throw std::runtime_error("Injected view recording failure");
      }
    }

  private:
    bool armed_ { false };
  };
  auto abort = std::make_shared<AbortRecordedView>();
  renderer_->RegisterViewExtension(abort);
  auto& backend = FailureBackend();
  auto prior_output = sentinel;
  if (late_failure) {
    ASSERT_TRUE(invoke(1U));
    prior_output = read_pixel();
    seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_TRUE(invoke(2U, true));
  }
  backend.recorder_names.clear();
  if (late_failure) {
    abort->Arm();
    EXPECT_THROW(invoke(2U), std::runtime_error);
  } else {
    backend.fail_recorder_name = "Vortex View";
    EXPECT_FALSE(invoke(1U));
    backend.fail_recorder_name.clear();
  }
  for (const auto& name : backend.recorder_names) {
    if (!late_failure) {
      EXPECT_EQ(name.find("BasePass"), std::string::npos);
      EXPECT_EQ(name.find("DeferredLight"), std::string::npos);
    }
    EXPECT_EQ(name.find("Tonemap"), std::string::npos);
    EXPECT_EQ(name.find("ResolveSceneColor"), std::string::npos);
  }
  EXPECT_EQ(read_pixel(), prior_output);
  EXPECT_EQ(
    InspectRequiredTransition(handle).phase, ExposureTransitionPhase::kQueued);
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_FALSE(
    scene_renderer->GetSceneTextureExtracts().resolved_scene_color.valid);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  EXPECT_FALSE(service->GetLastExecutionState().wrote_visible_output);
  EXPECT_TRUE(invoke(late_failure ? 3U : 2U));
  EXPECT_EQ(read_pixel().at(0), 0.0F);
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, handle);
  ASSERT_NE(state, nullptr);
  const auto solved
    = Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(solved.applied_generation.at(0), seed->generation);
  EXPECT_EQ(solved.displayed_scale, 0x1p-8F);
  FlushBackend();
}

auto ExposureGpuTest::CheckFogViewRetirement(
  const bool persistent, const bool temporal) -> void
{
  auto& tracked = FailureBackend();
  tracked.track_resources = true;
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr {
    &console,
  });
  ASSERT_EQ(
    console
      .Execute(temporal ? "vtx.volumetric_fog.temporal_reprojection true"
                        : "vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("Fog retirement", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  fog.SetVolumetricFogEmissive({
    .1F,
    .2F,
    .3F,
  });
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = {
    .width = 4.0F,
    .height = 4.0F,
  };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto color = CreateRegisteredTexture({
    .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto target
    = Backend().CreateFramebuffer(FramebufferDesc {}.AddColorAttachment(color));
  struct Capture final : IViewExtension {
    // The local probe borrows its enclosing renderer; it is never reassigned
    // and is destroyed first.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Renderer& renderer;
    std::vector<std::shared_ptr<Texture>> textures;
    explicit Capture(Renderer& value)
      : renderer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      hook.recorder.OnSubmission(
        [this, owner, view_id = hook.render_context.current_view.view_id](
          const graphics::SubmissionOutcome outcome) -> void {
          if (outcome == graphics::SubmissionOutcome::kSubmitted) {
            textures
              = vortex::testing::RendererPublicationProbe::EnvironmentTextures(
                *owner, view_id);
          }
        });
    }
  };
  auto capture = std::make_shared<Capture>(*renderer_);
  renderer_->RegisterViewExtension(capture);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr {
    scene.get(),
  });
  std::optional<std::size_t> baseline_resources;
  std::uint64_t sequence = 0U;
  auto& registry = Backend().GetResourceRegistry();
  auto& reclaimer = Backend().GetDeferredReclaimer();
  for (unsigned iteration = 0U; iteration < 8U; ++iteration) {
    SCOPED_TRACE(iteration);
    const auto slot = frame::Slot {
      0U,
    };
    reclaimer.OnBeginFrame(slot);
    frame.SetFrameSequenceNumber(
      frame::SequenceNumber {
        ++sequence,
      },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr {
      &frame,
    });
    const auto intent = ViewId {
      12000U + iteration,
    };
    ViewId published = intent;
    if (persistent) {
      auto input = CompositionView::ForScene(intent, view, camera);
      input.view_state_handle = CompositionView::ViewStateHandle {
        intent.get(),
      };
      input.with_height_fog = true;
      published = renderer_->PublishRuntimeCompositionView(frame,
        { .composition_view = input,
          .render_target = observer_ptr { target.get(), }, });
      ASSERT_NE(published, kInvalidViewId);
      auto loop = co::testing::TestEventLoop {};
      // co::Run completes synchronously before this closure and its captured
      // fixture state leave scope.
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
      co::Run(loop, [&] -> co::Co<void> {
        co_await renderer_->OnPreRender(observer_ptr {
          &frame,
        });
        co_await renderer_->OnRender(observer_ptr {
          &frame,
        });
      });
    } else {
      auto input = Renderer::OffscreenSceneViewInput::FromCamera(
        "Stateless fog", intent, view, camera);
      auto facade = renderer_->ForOffscreenScene();
      facade.SetFrameSession({ .frame_slot = slot,
        .frame_sequence = frame::SequenceNumber { sequence, },
        .delta_time_seconds = 0.0F, });
      facade.SetSceneSource({ .scene = observer_ptr {
                                scene.get(),
                              } });
      facade.SetViewIntent(input);
      facade.SetOutputTarget({ .framebuffer = observer_ptr {
                                 target.get(),
                               } });
      auto session = facade.Finalize();
      ASSERT_TRUE(session.has_value());
      ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    }
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(
      vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner),
      persistent && temporal ? 1U : 0U);
    ASSERT_EQ(capture->textures.size(), 1U);
    auto texture = capture->textures.front();
    // This observer owns the underlying allocation, not the retained wrapper.
    // Keeping it alive lets the test inspect registration after wrapper
    // release.
    const auto underlying = texture->shared_from_this();
    ASSERT_EQ(underlying.get(), texture.get());
    ASSERT_TRUE(
      texture.owner_before(underlying) || underlying.owner_before(texture));
    ASSERT_TRUE(registry.Contains(*texture));
    if (persistent) {
      owner->OnFrameStart(frame);
      EXPECT_EQ(
        vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner),
        temporal ? 1U : 0U);
      renderer_->RemovePublishedRuntimeView(frame, intent);
      EXPECT_EQ(
        vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner), 0U);
    }
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Retiring fog voxel");
    {
      auto recorder = AcquireRecorder("Retiring fog output readback");
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
      ASSERT_TRUE(readback
          ->EnqueueCopy(*recorder, *texture,
            { .src_slice
              = { .z = 16U, .width = 1U, .height = 1U, .depth = 1U, }, })
          .has_value());
    }
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    Pixel voxel {};
    std::memcpy(voxel.data(), mapped->Data(), sizeof(voxel));
    EXPECT_GT(voxel.at(0), 0.0F);
    EXPECT_TRUE(std::isfinite(voxel.at(0)));
    renderer_->OnFrameEnd(observer_ptr {
      &frame,
    });
    capture->textures.clear();
    WaitForQueueIdle();
    for (unsigned retire = 0U; retire < frame::kFramesInFlight.get();
      ++retire) {
      const auto retired_slot = frame::Slot {
        retire,
      };
      owner->OnStandaloneFrameStart(
        frame::SequenceNumber {
          ++sequence,
        },
        retired_slot, std::nullopt);
      reclaimer.OnBeginFrame(retired_slot);
    }
    EXPECT_TRUE(registry.Contains(*texture));
    texture.reset();
    for (unsigned retire = 0U; retire < frame::kFramesInFlight.get();
      ++retire) {
      const auto retired_slot = frame::Slot {
        retire,
      };
      owner->OnStandaloneFrameStart(
        frame::SequenceNumber {
          ++sequence,
        },
        retired_slot, std::nullopt);
      reclaimer.OnBeginFrame(retired_slot);
    }
    EXPECT_FALSE(registry.Contains(*underlying));
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    EXPECT_EQ(
      vortex::testing::RendererPublicationProbe::RetainedExposureFrameCount(
        *service),
      0U);
    const auto resources = registry.GetRegisteredResourceCount();
    if (iteration == 2U) {
      baseline_resources = resources;
    }
    if (baseline_resources) {
      EXPECT_EQ(resources, *baseline_resources);
    }
    if (baseline_resources && resources != *baseline_resources) {
      std::map<std::string, unsigned> names;
      for (const auto& weak : tracked.tracked_buffers) {
        if (auto resource = weak.lock();
          resource && registry.Contains(*resource)) {
          ++names[std::string(resource->GetName())];
        }
      }
      for (const auto& weak : tracked.tracked_textures) {
        if (auto resource = weak.lock();
          resource && registry.Contains(*resource)) {
          ++names[std::string(resource->GetName())];
        }
      }
      for (const auto& [name, count] : names) {
        LOG_F(ERROR, "retained {} {}", count, name);
      }
    }
  }
}

} // namespace oxygen::vortex::testing::exposure
