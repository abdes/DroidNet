//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

using graphics::FramebufferDesc;
using graphics::FrameCaptureController;
using graphics::ResourceStates;
using graphics::Texture;

NOLINT_TEST_F(
  ExposureGpuTest, PreEnvironmentRangePreservesStatusAndViewIsolation)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 3.0F;
  const auto config = SharedConfig(settings);
  std::array<Pixel, 27> pixels {};
  pixels.fill(Pixel {
    1,
    2,
    3,
    1,
  });
  pixels.at(8) = Pixel {
    -65536,
    4,
    5,
    1,
  };
  pixels.at(26) = Pixel {
    6,
    7,
    32768,
    1,
  };
  const auto signal = MakeSignal(9U, 3U, pixels);
  const auto smaller = Uniform(.25F, 2U, 5U);
  const auto capture = BeginOptionalCapture();
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
  frame_inputs.use_fp32 = false;
  const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    .125F);
  ASSERT_TRUE(RecordShared(signal, config).executed);
  // Distinct sentinels verify that this reduction does not clear producer
  // bounds.
  const std::array<HdrErrorBoundsData, 3> bounds {
    HdrErrorBoundsData {
      .rgb_absolute = .125F,
    },
    HdrErrorBoundsData {
      .rgb_absolute = .25F,
    },
    HdrErrorBoundsData {
      .rgb_absolute = .5F,
    },
  };
  auto upload = CreateUploadBuffer(SizeBytes {
    sizeof(bounds),
  });
  upload->Update(bounds.data(), sizeof(bounds), 0U);
  {
    auto recorder = AcquireRecorder("Pre-environment bound sentinels");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      recorder->AdoptKnownResourceState(*frame->current_state->status_buffer));
    recorder->RequireResourceState(
      *frame->current_state->status_buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(*frame->current_state->status_buffer,
      offsetof(ExposureStatusStorage, producer_errors), *upload, 0U,
      sizeof(bounds));
    recorder->RequireResourceStateFinal(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  }
  const auto before = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *signal.texture, signal.srv));
  EXPECT_TRUE(pass_->HasPreEnvironmentRange(frame));
  const auto first = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  auto expected_status = before;
  expected_status.completed.flags |= 18U;
  expected_status.completed.first_failure_product = 11U;
  expected_status.completed.first_failure_kind |= 32U;
  // Only the required producer verdict may change; identities and the
  // independently uploaded producer-error sentinels remain byte-identical.
  EXPECT_TRUE(std::ranges::equal(
    std::as_bytes(std::span {
                    &expected_status,
                    1,
                  })
      .first(offsetof(ExposureStatusStorage, composition_input)),
    std::as_bytes(std::span {
                    &first,
                    1,
                  })
      .first(offsetof(ExposureStatusStorage, composition_input))));
  EXPECT_EQ(first.composition_input.maximum_pre_exposed_rgb, 65536.0F);
  EXPECT_EQ(first.composition_input.flags, 5U);
  EXPECT_EQ(first.composition_input.checked_pixels, 27U);
  EXPECT_EQ(first.composition_input.reserved, 0U);

  // A second view cannot overwrite the first view's retained input range.
  ctx_.current_view.view_id = ViewId {
    2U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    2U,
  };
  auto other_inputs = postprocess::ExposurePass::FrameInputs {};
  other_inputs.use_fp32 = true;
  const auto other = pass_->ResolveFrame(ctx_, config, other_inputs);
  ASSERT_NE(other, nullptr);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, other, *smaller.texture, smaller.srv));
  const auto second = Read<ExposureStatusStorage>(
    *other->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  EXPECT_EQ(second.composition_input.maximum_pre_exposed_rgb, .25F);
  EXPECT_EQ(second.composition_input.checked_pixels, 10U);
  EXPECT_EQ(second.composition_input.flags, 1U);
  EXPECT_EQ(Read<ExposureStatusStorage>(*frame->current_state->status_buffer,
              ResourceStates::kUnorderedAccess)
              .composition_input.maximum_pre_exposed_rgb,
    65536.0F);

  ctx_.current_view.view_id = ViewId {
    1U,
  };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    1U,
  };
  auto& backend = FailureBackend();
  backend.fail_recorder_name = "Vortex Exposure PreEnvironment Range";
  EXPECT_FALSE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *smaller.texture, smaller.srv));
  EXPECT_FALSE(pass_->HasPreEnvironmentRange(frame));
  backend.fail_recorder_name.clear();
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *smaller.texture, smaller.srv));
  const auto retry = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  EXPECT_EQ(retry.composition_input.maximum_pre_exposed_rgb, .25F);
  EXPECT_EQ(retry.composition_input.checked_pixels, 10U);
  EXPECT_EQ(retry.composition_input.flags, 1U);
  EXPECT_TRUE(std::ranges::equal(
    std::as_bytes(std::span {
                    &expected_status,
                    1,
                  })
      .first(offsetof(ExposureStatusStorage, composition_input)),
    std::as_bytes(std::span {
                    &retry,
                    1,
                  })
      .first(offsetof(ExposureStatusStorage, composition_input))));
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = smaller.texture.get(),
      .srv = smaller.srv,
      .id = 11U,
    },
  };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, frame,
    {
      .product_layout_revision = 1U,
      .expected_products = 1U << 10U,
    }));
  const auto finalized = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                   &retry.composition_input,
                                   1,
                                 }),
    std::as_bytes(std::span {
      &finalized.composition_input,
      1,
    })));
  EXPECT_FALSE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *smaller.texture, kInvalidShaderVisibleIndex));
  EXPECT_FALSE(pass_->HasPreEnvironmentRange(frame));
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(ExposureGpuTest, PreEnvironmentRangePreservesSignedTinyInputs)
{
  const auto config = SharedConfig(scene::ExposureSettings {});
  for (const auto bits : {
         1U,
         0x80000001U,
         0x80000000U,
       }) {
    SCOPED_TRACE(bits);
    const auto signal = Uniform(std::bit_cast<float>(bits));
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *signal.texture, signal.srv));
    const auto status = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(
                status.composition_input.maximum_pre_exposed_rgb),
      bits & 0x7fffffffU);
    EXPECT_EQ(status.composition_input.flags, bits == 0x80000001U ? 5U : 1U);
    EXPECT_EQ(status.composition_input.checked_pixels, 1U);
  }
}

NOLINT_TEST_F(ExposureGpuTest, PreEnvironmentRangeReportsNonfiniteInput)
{
  const auto config = SharedConfig(scene::ExposureSettings {});
  for (unsigned channel = 0U; channel < 4U; ++channel) {
    SCOPED_TRACE(channel);
    std::array<Pixel, 9> pixels {};
    pixels.fill(Pixel {
      2,
      3,
      4,
      1,
    });
    pixels.at(8).at(channel) = channel % 2U == 0U
      ? std::numeric_limits<float>::infinity()
      : std::numeric_limits<float>::quiet_NaN();
    const auto signal = MakeSignal(9U, 1U, pixels);
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *signal.texture, signal.srv));
    const auto status = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(status.composition_input.maximum_pre_exposed_rgb, 4.0F);
    EXPECT_EQ(status.composition_input.flags, 3U);
    EXPECT_EQ(status.composition_input.checked_pixels, 9U);
    EXPECT_EQ(status.completed.flags, 18U);
    EXPECT_EQ(status.completed.first_failure_product, 11U);
    EXPECT_EQ(status.completed.first_failure_kind, 1U);
    // Later attenuation may hide an invalid source. Finite final pixels must
    // not authorize adaptation after the earlier producer failure.
    const auto finite = Uniform(.25F);
    const auto solved = RecordShared(finite, config);
    ASSERT_TRUE(solved.executed);
    EXPECT_EQ(ReadState(solved).flags & 12U, 0U);
    EXPECT_NE(ReadState(solved).flags & 32U, 0U);
    const auto after = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                     &status.composition_input,
                                     1,
                                   }),
      std::as_bytes(std::span {
        &after.composition_input,
        1,
      })));
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FinalSceneRangePreservesOpaqueCertificateAndValidHistory)
{
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto prior = Run(Uniform(.25F), settings);
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
  frame_inputs.use_fp32 = true;
  const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
  ASSERT_NE(frame, nullptr);
  const auto opaque = Uniform(.5F, 4U, 4U);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *opaque.texture, opaque.srv));
  const auto before = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  const auto unsupported = Uniform(0x1p33F, 4U, 4U);
  ASSERT_TRUE(pass_->CheckSceneColorRange(
    ctx_, frame, *unsupported.texture, unsupported.srv));
  const auto after = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                   &before.composition_input,
                                   1,
                                 }),
    std::as_bytes(std::span {
      &after.composition_input,
      1,
    })));
  EXPECT_TRUE(pass_->HasPreEnvironmentRange(frame));
  EXPECT_EQ(after.completed.flags & 18U, 18U);
  EXPECT_EQ(after.completed.first_failure_product, 11U);
  EXPECT_EQ(after.completed.first_failure_kind, 32U);
  const auto solved = RecordShared(unsupported, config);
  ASSERT_TRUE(solved.executed);
  const auto state = ReadState(solved);
  EXPECT_EQ(state.displayed_scale, prior.state.displayed_scale);
  EXPECT_EQ(state.latent_scale, prior.state.latent_scale);
  EXPECT_EQ(state.flags & 12U, 0U);
  EXPECT_NE(state.flags & 32U, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, ProducerDomainDistinguishesFloatRangeFromHalfHeadroom)
{
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto tiny = std::numeric_limits<float>::denorm_min();
  const std::array scene_values {
    0.0F,
    -0.0F,
    tiny,
    -tiny,
    0x1p-25F,
    0x1p-24F,
    1.0F,
    std::nextafter(0x1p32F, 0.0F),
    0x1p32F,
    std::nextafter(0x1p32F, infinity),
    -1.0F,
    infinity,
    std::numeric_limits<float>::quiet_NaN(),
  };
  std::vector<std::array<float, 4>> inputs;
  std::vector<unsigned> expected;
  for (const float p : {
         0x1p-32F,
         .1F,
         .6184799671173096F,
         1.0F,
         1.1F,
         std::numbers::pi_v<float>,
         0x1p32F,
       }) {
    for (const float scene : scene_values) {
      for (unsigned channel = 0; channel < 3; ++channel) {
        for (const bool half : {
               false,
               true,
             }) {
          std::array<float, 4> value {
            0,
            0,
            0,
            1,
          };
          value.at(channel) = scene * p;
          inputs.push_back(value);
          inputs.push_back({
            p,
            half ? 1.0F : 0.0F,
            0,
            0,
          });
          unsigned flags = 0;
          const double reference
            = static_cast<double>(value.at(channel)) / static_cast<double>(p);
          if (!std::isfinite(value.at(channel))) {
            flags = 1U;
          } else {
            // The oracle uses the supplied float's exact mathematical value;
            // it does not call production bound or classification helpers.
            if (reference < 0 || reference > 4294967296.0) {
              flags |= 32U;
            }
            if (half
              && std::abs(static_cast<double>(value.at(channel))) > 16376.0) {
              flags |= 2U;
            }
          }
          expected.push_back(flags);
        }
      }
    }
  }
  for (const float invalid_p : {
         0.0F,
         -1.0F,
         0x1p-33F,
         0x1p33F,
         infinity,
       }) {
    inputs.push_back({
      1,
      1,
      1,
      1,
    });
    inputs.push_back({
      invalid_p,
      0,
      0,
      0,
    });
    expected.push_back(std::isfinite(invalid_p) ? 32U : 1U);
  }
  const auto results = RunToneProbe(std::as_bytes(std::span {
                                      inputs,
                                    }),
    static_cast<std::uint32_t>(expected.size()), 128U);
  ASSERT_EQ(results.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(results.at(i).at(0), static_cast<float>(expected.at(i)))
      << "case=" << i;
  }
  RecordProperty("producer_domain_cases", expected.size());
}

NOLINT_TEST_F(ExposureGpuTest,
  FinalSceneRangeFailureInvalidatesAdmissionAndAllowsFreshRetry)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U,
    .expected_products = 1024U,
  };
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true,
    },
  };
  const auto transition = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  if (!transition.has_value()) {
    FAIL() << "Expected transition to contain a value";
  }
  auto& backend = FailureBackend();
  for (unsigned step = 0; step < 5; ++step) {
    SCOPED_TRACE(step);
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    ctx_.frame_slot = frame::Slot {
      static_cast<unsigned>(sequence_ % 3U),
    };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    const auto candidate = service.SelectPrecisionCandidate(ctx_, requirements);
    EXPECT_EQ(candidate != nullptr, step == 2U || step == 4U);
    ctx_.current_view.frame_exposure = service.PrepareFrameExposure(ctx_, true);
    ASSERT_NE(ctx_.current_view.frame_exposure, nullptr);
    auto prepared_inputs = PostProcessService::Inputs {};
    prepared_inputs.scene_signal = signal.texture.get();
    prepared_inputs.scene_signal_srv = signal.srv;
    auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_, prepared_inputs);
    if (!prepared.has_value()) {
      FAIL() << "Expected prepared to contain a value";
    }
    const auto before = ReadState(prepared->exposure);
    if (step == 2U) {
      backend.fail_recorder_name = "Vortex Exposure Final Scene Range";
      EXPECT_FALSE(
        service.CheckSceneColorRange(ctx_, *signal.texture, signal.srv));
      backend.fail_recorder_name.clear();
      EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), nullptr);
      EXPECT_FALSE(service.PrepareScenePrecision(ctx_, *prepared, products));
      EXPECT_FALSE(service.FinalizeScenePrecision(ctx_, *prepared));
      ASSERT_TRUE(
        service.CheckSceneColorRange(ctx_, *signal.texture, signal.srv));
      {
        auto scene_inputs = PostProcessService::Inputs {};
        scene_inputs.scene_signal = signal.texture.get();
        scene_inputs.scene_signal_srv = signal.srv;
        prepared = service.PrepareSceneExposure(
          ctx_.current_view.view_id, ctx_, scene_inputs);
      }
      if (!prepared.has_value()) {
        FAIL() << "Expected prepared to contain a value";
      }
    }
    ASSERT_TRUE(service.PrepareScenePrecision(ctx_, *prepared, products));
    ASSERT_TRUE(service.FinalizeScenePrecision(ctx_, *prepared));
    const auto after = ReadState(prepared->exposure);
    EXPECT_EQ(after.displayed_scale, before.displayed_scale);
    EXPECT_EQ(after.latent_scale, before.latent_scale);
    EXPECT_EQ(after.applied_generation, before.applied_generation);
    EXPECT_EQ(after.fp16_eligible_streak, step == 0U || step == 2U ? 1U : 2U);
  }
  const auto status
    = renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle);
  if (!status.has_value()) {
    FAIL() << "Expected status to contain a value";
  }
  EXPECT_EQ(status->applied_generation, transition->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, ProducerRangeFailureSurvivesSolveAndPreventsAutoAdaptation)
{
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
    console.Execute("vtx.volumetric_fog.temporal_reprojection false").status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("Producer range", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = {
    .width = 4.0F,
    .height = 4.0F,
  };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
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
  struct Probe final : IViewExtension {
    EnvironmentLightingService half_producer;
    bool inject_half {
      false,
    };
    postprocess::ExposurePass::FrameLease frame;
    std::shared_ptr<const Texture> half_texture;
    explicit Probe(Renderer& renderer)
      : half_producer(renderer)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPreRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      EXPECT_FLOAT_EQ(hook.render_context.delta_time, 1.0F);
      if (!inject_half) {
        return;
      }
      auto& ctx = hook.render_context;
      const auto old_format = ctx.current_view.hdr_color_format;
      ctx.current_view.hdr_color_format = Format::kRGBA16Float;
      half_producer.OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
      static_cast<void>(half_producer.PublishEnvironmentBindings(ctx));
      const auto* resources
        = half_producer.InspectViewRadianceResources(ctx.current_view.view_id);
      CHECK_NOTNULL_F(resources);
      half_texture = resources->volumetric_fog;
      ctx.current_view.hdr_color_format = old_format;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      frame = hook.render_context.current_view.frame_exposure;
    }
  };
  auto probe = std::make_shared<Probe>(*renderer_);
  renderer_->RegisterViewExtension(probe);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr {
    scene.get(),
  });
  auto timing = engine::ModuleTimingData {};
  timing.game_delta_time = time::CanonicalDuration {
    std::chrono::nanoseconds {
      1000000000,
    },
  };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  ExposureStateData previous {};
  for (unsigned step = 1U; step <= 7U; ++step) {
    SCOPED_TRACE(step);
    float sky_value = 4.0F;
    if (step == 1U) {
      sky_value = .25F;
    } else if (step == 6U) {
      sky_value = 0x1p34F;
    }
    sky.SetSolidColorRgb({
      sky_value,
      sky_value,
      sky_value,
    });
    float emissive = 0x1p26F;
    if (step == 1U) {
      emissive = .125F;
    } else if (step == 3U) {
      emissive = std::numeric_limits<float>::infinity();
    } else if (step == 4U) {
      emissive = 0x1p36F;
    }
    fog.SetVolumetricFogEmissive({
      emissive,
      emissive,
      emissive,
    });
    probe->inject_half = step == 2U;
    scene->Update();
    const auto slot = frame::Slot {
      (step - 1U) % 3U,
    };
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(
      frame::SequenceNumber {
        step,
      },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr {
      &frame,
    });
    auto input = Renderer::OffscreenSceneViewInput::FromCamera("Producer range",
      ViewId {
        941U,
      },
      view, camera);
    input.SetViewStateHandle(CompositionView::ViewStateHandle {
      941U,
    });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = slot,
      .frame_sequence = frame::SequenceNumber { step, },
      .delta_time_seconds = 1.0F, });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get(), }, });
    facade.SetViewIntent(input);
    facade.SetOutputTarget({ .framebuffer = observer_ptr { target.get(), }, });
    auto session = facade.Finalize();
    if (!session.has_value()) {
      FAIL() << "Expected session to contain a value";
    }
    observer_ptr<FrameCaptureController> capture;
    if (step == 2U) {
      capture = BeginOptionalCapture();
    }
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
    ASSERT_NE(probe->frame, nullptr);
    const auto state = Read<ExposureStateData>(
      *probe->frame->current_state->buffer, ResourceStates::kShaderResource);
    const auto status = Read<ExposureCompletedStatus>(
      *probe->frame->current_state->status_buffer, ResourceStates::kCopySource);
    if (step == 2U || step == 3U || step == 4U || step == 6U) {
      EXPECT_EQ(status.flags & 18U, 18U);
      EXPECT_EQ(status.first_failure_product, step == 6U ? 7U : 10U);
      EXPECT_NE(status.first_failure_kind
          & (step == 2U    ? 2U
              : step == 3U ? 1U
                           : 32U),
        0U);
      EXPECT_EQ(status.fp16_eligible_streak, 0U);
      EXPECT_EQ(state.displayed_scale, previous.displayed_scale);
      EXPECT_EQ(state.latent_scale, previous.latent_scale);
      EXPECT_EQ(state.flags & 12U, 0U);
      EXPECT_NE(state.flags & 32U, 0U);
    } else {
      EXPECT_EQ(status.flags & 16U, 0U);
      EXPECT_NE(state.flags & 4U, 0U);
      if (step == 5U) {
        EXPECT_LT(state.displayed_scale, previous.displayed_scale);
      }
    }
    if (step == 2U) {
      ASSERT_NE(probe->half_texture, nullptr);
      EXPECT_EQ(
        probe->half_texture->GetDescriptor().format, Format::kRGBA16Float);
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Overflowed half fog");
      {
        auto recorder = AcquireRecorder("Overflowed half fog readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*probe->half_texture));
        ASSERT_TRUE(readback
            ->EnqueueCopy(*recorder, *probe->half_texture,
              { .src_slice
                = { .z = 31U, .width = 1U, .height = 1U, .depth = 1U, }, })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      if (!mapped.has_value()) {
        FAIL() << "Expected mapped to contain a value";
      }
      std::uint16_t red = 0;
      std::memcpy(&red, mapped->Data(), sizeof(red));
      EXPECT_EQ(red, 0x7bffU); // The stored half has clipped to 65504.
    }
    previous = state;
    renderer_->OnFrameEnd(observer_ptr {
      &frame,
    });
    WaitForQueueIdle();
  }
}

} // namespace oxygen::vortex::testing::exposure
