//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <dxgiformat.h>
#include <glm/geometric.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereView.h>
#include <Oxygen/Vortex/Environment/Internal/CapturedSkySource.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereMultiScatteringLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/DistantSkyLightLutPass.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

using graphics::BufferUsage;
using graphics::FramebufferDesc;
using graphics::FrameCaptureController;
using graphics::ResourceStates;
using graphics::Texture;

NOLINT_TEST_F(ExposureGpuTest,
  SceneSkyRadianceIsInvariantToNumericalDomainAndPreservesHighRange)
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
  auto scene = std::make_shared<scene::Scene>("SceneDomainFixture", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
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
  struct ExposureOverride final : IViewExtension {
    std::function<void(RenderContext&)> before;
    auto OnViewSetup(const ViewSetupContext& context) -> void override
    {
      before(context.render_context);
    }
  };
  bool force_nonunit = false;
  auto extension = std::make_shared<ExposureOverride>();
  extension->before = [&](RenderContext& ctx) -> void {
    if (!force_nonunit) {
      return;
    }
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    service->SetResolvedConfig(SharedConfig(settings));
    ASSERT_NE(SubmitCommands("Vortex Exposure Frame",
                [&](graphics::CommandRecorder& recorder) -> auto {
                  return service->PrepareFrameExposure(ctx, recorder, false);
                }),
      nullptr);
  };
  renderer_->RegisterViewExtension(extension);
  unsigned sequence = 0U;
  for (const bool high_range : {
         false,
         true,
       }) {
    settings.manual_ev = high_range ? 30.0F : 4.0F;
    post.SetExposureSettings(settings);
    sky.SetSolidColorRgb({
      .25F,
      .5F,
      .75F,
    });
    sky.SetIntensity(high_range ? 0x1p30F : 1.0F);
    scene->Update();
    const float expected_scale = high_range ? 1.0F : 1.0F / 16.0F;
    for (const bool nonunit : {
           false,
           true,
         }) {
      force_nonunit = nonunit;
      ++sequence;
      auto input = Renderer::OffscreenSceneViewInput::FromCamera("SkyDomain",
        ViewId {
          8100U,
        },
        view, camera);
      input.SetViewStateHandle(CompositionView::ViewStateHandle {
        8100U,
      });
      input.SetWithAtmosphere(true);
      auto facade = renderer_->ForOffscreenScene();
      facade.SetFrameSession(
        { .frame_slot = frame::Slot { (sequence - 1U) % 3U, },
          .frame_sequence = frame::SequenceNumber { sequence, },
          .delta_time_seconds = 0.0F, });
      facade.SetSceneSource({ .scene = observer_ptr {
                                scene.get(),
                              } });
      facade.SetOutputTarget({ .framebuffer = observer_ptr {
                                 framebuffer.get(),
                               } });
      facade.SetViewIntent(input);
      auto session = facade.Finalize();
      if (!session.has_value()) {
        FAIL() << "Expected session to contain a value";
      }
      ASSERT_TRUE(session->ExecuteNow());
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Scene sky domain pixel");
      {
        auto recorder = AcquireRecorder("Scene sky domain readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*output));
        ASSERT_TRUE(readback
            ->EnqueueCopy(*recorder, *output,
              { .src_slice = { .x = 1U,
                  .y = 0U,
                  .width = 1U,
                  .height = 1U,
                  .depth = 1U, }, })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      if (!mapped.has_value()) {
        FAIL() << "Expected mapped to contain a value";
      }
      Pixel pixel {};
      std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
      EXPECT_NEAR(pixel.at(0), .25F * expected_scale, 2e-5F);
      EXPECT_NEAR(pixel.at(1), .5F * expected_scale, 2e-5F);
      EXPECT_NEAR(pixel.at(2), .75F * expected_scale, 2e-5F);
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer_);
      const auto& product
        = owner->GetSceneTextureExtracts().resolved_scene_color;
      ASSERT_TRUE(product.valid);
      ASSERT_NE(product.exposure, nullptr);
      EXPECT_EQ(product.texture->GetDescriptor().format, Format::kRGBA32Float);
      const auto domain = Read<FrameExposureData>(
        *product.exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(
        domain.pre_exposure, nonunit ? std::exp2(-settings.manual_ev) : 1.0F);
    }
  }
  extension->before = [](RenderContext&) -> void { };
  FlushBackend();
}

NOLINT_TEST_F(ExposureGpuTest, DistantSkyRadiancePreservesLinearRange)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  ctx_.current_view.with_atmosphere = true;
  auto view_data = ViewConstants::GpuData {};
  auto view_buffer = CreateUploadBuffer(
    SizeBytes {
      256U,
    },
    BufferUsage::kConstant);
  view_buffer->Update(&view_data, sizeof(view_data), 0U);
  ctx_.view_constants = view_buffer;
  auto stable = environment::internal::StableAtmosphereState {};
  stable.atmosphere_revision = 1;
  stable.view_products.atmosphere.enabled = true;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto scattering = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto distant = environment::DistantSkyLightLutPass(*renderer_);
  const auto render
    = [&](const std::array<float, 2>& intensity) -> std::array<float, 4> {
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    ctx_.frame_slot = frame::Slot {
      static_cast<unsigned>(sequence_ % 3),
    };
    stable.light_revision = sequence_;
    stable.view_products.atmosphere_light_count = 2;
    for (unsigned i = 0; i < 2; ++i) {
      auto& light = stable.view_products.atmosphere_lights.at(i);
      light.enabled = true;
      light.direction_to_light_ws = i == 0
        ? glm::vec3 { 0, 0, 1, }
        : glm::normalize(glm::vec3 { 1, 0, 1, });
      light.illuminance_rgb_lux = glm::vec3 {
        intensity.at(i),
      };
    }
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    cache.RefreshForState(stable);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    scattering.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    distant.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    if (cache.NeedsTransmittanceBuild()) {
      EXPECT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
    }
    if (cache.NeedsMultiScatteringBuild()) {
      EXPECT_TRUE(scattering.Record(ctx_, stable, cache).executed);
    }
    EXPECT_TRUE(distant.Record(ctx_, stable, cache).executed);
    return Read<Pixel>(
      *cache.GetDistantSkyLightBuffer(), ResourceStates::kShaderResource);
  };
  const auto first = render({
    1,
    0,
  });
  const auto second = render({
    0,
    1,
  });
  for (unsigned channel = 0; channel < 3; ++channel) {
    ASSERT_GT(first.at(channel), 0.0F);
    ASSERT_GT(second.at(channel), 0.0F);
  }
  unsigned cases = 0;
  // Linearity of the radiative-transfer equation provides an independent
  // scaling/additivity oracle. Unit-light native anchors are not an absolute
  // atmospheric-accuracy reference; no production helper computes expected RGB.
  for (const float gain : {
         0.0F,
         0x1p-24F,
         1.0F,
         0x1p32F,
         0x1p38F,
       }) {
    for (const bool dual : {
           false,
           true,
         }) {
      SCOPED_TRACE(gain);
      SCOPED_TRACE(dual);
      const auto result = render({
        gain,
        dual ? gain * .5F : 0.0F,
      });
      for (unsigned channel = 0; channel < 3; ++channel) {
        const double expected = static_cast<double>(gain)
          * (static_cast<double>(first.at(channel))
            + (dual ? .5 * second.at(channel) : 0));
        EXPECT_NEAR(
          result.at(channel), expected, (std::abs(expected) * 2e-5) + 0x1p-120);
      }
      EXPECT_EQ(result.at(3), 0);
      ++cases;
    }
  }
  ctx_.view_constants.reset();
  RecordProperty("distant_sky_range_cases", cases);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, CanonicalAtmosphereStoresPreserveAmplifiedSmallTransfers)
{
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  stable.view_products.atmosphere.enabled = true;
  stable.atmosphere_revision = 1U;
  cache.RefreshForState(stable);
  ASSERT_TRUE(cache.EnsureResources());
  constexpr float transfer = 1.0e-12F;
  constexpr float radiance = 1.88e9F;
  const std::array textures {
    cache.GetTransmittanceTexture(),
    cache.GetMultiScatteringTexture(),
  };
  std::uint64_t allocation_bytes = 0U;
  std::uint64_t half_allocation_bytes = 0U;
  for (const auto& texture : textures) {
    ASSERT_NE(texture, nullptr);
    const auto& desc = texture->GetDescriptor();
    ASSERT_EQ(desc.format, Format::kRGBA32Float);
    auto native_desc
      = texture->GetNativeResource()->AsPointer<ID3D12Resource>()->GetDesc();
    auto* device = FailureBackend().GetCurrentDevice();
    allocation_bytes
      += device->GetResourceAllocationInfo(0U, 1U, &native_desc).SizeInBytes;
    native_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    half_allocation_bytes
      += device->GetResourceAllocationInfo(0U, 1U, &native_desc).SizeInBytes;
    const std::vector<Pixel> values(
      static_cast<std::size_t>(desc.width) * desc.height,
      Pixel {
        transfer,
        transfer,
        transfer,
        0,
      });
    const auto bytes = std::as_bytes(std::span {
      values,
    });
    auto upload = CreateUploadBuffer(SizeBytes {
      bytes.size(),
    });
    upload->Update(bytes.data(), bytes.size(), 0U);
    auto recorder = AcquireRecorder("Canonical small transfer upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    if (!recorder->AdoptKnownResourceState(*texture)) {
      recorder->BeginTrackingResourceState(*texture, desc.initial_state);
    }
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      {
        .buffer_offset = 0U,
        .buffer_row_pitch = static_cast<std::uint64_t>(desc.width) * 16U,
        .buffer_slice_pitch
        = static_cast<std::uint64_t>(desc.width) * desc.height * 16U,
        .dst_slice
        = { .width = desc.width, .height = desc.height, .depth = 1U },
      },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  const std::array<Pixel, 1> tiny_pixel {
    Pixel {
      transfer,
      transfer,
      transfer,
      0,
    },
  };
  const auto half_control
    = MakeSignal(1U, 1U, tiny_pixel, 1U, Format::kRGBA16Float);
  const std::array<std::array<std::uint32_t, 4>, 3> inputs {
    std::array {
      cache.GetState().transmittance_lut_srv.get(),
      std::bit_cast<std::uint32_t>(radiance),
      0U,
      0U,
    },
    std::array {
      cache.GetState().multi_scattering_lut_srv.get(),
      std::bit_cast<std::uint32_t>(radiance),
      0U,
      0U,
    },
    std::array {
      half_control.srv.get(),
      std::bit_cast<std::uint32_t>(radiance),
      0U,
      0U,
    },
  };
  const auto results = RunToneProbe(std::as_bytes(std::span {
                                      inputs,
                                    }),
    3U, 256U);
  for (unsigned i = 0; i < 2; ++i) {
    for (unsigned c = 0; c < 3; ++c) {
      EXPECT_NEAR(results.at(i).at(c), static_cast<double>(transfer) * radiance,
        static_cast<double>(transfer) * radiance * 2e-5);
      EXPECT_EQ(results.at(i).at(4 + c), transfer);
    }
  }
  EXPECT_EQ(results.at(2).at(0),
    0.0F); // The old half format erases the required signal.
  for (const auto format : {
         Format::kRGBA16Float,
         Format::kRGBA32Float,
       }) {
    ctx_.current_view.hdr_color_format = format;
    cache.RefreshForState(stable);
    EXPECT_EQ(cache.GetTransmittanceTexture(), textures.at(0));
    EXPECT_EQ(cache.GetMultiScatteringTexture(), textures.at(1));
  }
  RecordProperty(
    "canonical_fp32_placement_bytes", std::to_string(allocation_bytes));
  RecordProperty(
    "same_shape_fp16_placement_bytes", std::to_string(half_allocation_bytes));
  RecordProperty("canonical_generations_across_view_modes", 1);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, CanonicalAtmosphereProducersRetainTinyIlluminatedSignals)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  ctx_.current_view.with_atmosphere = true;
  auto view_data = ViewConstants::GpuData {};
  auto view_buffer = CreateUploadBuffer(
    SizeBytes {
      256U,
    },
    BufferUsage::kConstant);
  view_buffer->Update(&view_data, sizeof(view_data), 0U);
  ctx_.view_constants = view_buffer;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto scattering = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  auto& atmosphere = stable.view_products.atmosphere;
  atmosphere.enabled = true;
  atmosphere.planet_radius_m = 1000;
  atmosphere.atmosphere_height_m = 1000;
  atmosphere.rayleigh_scattering_rgb = {};
  atmosphere.mie_scattering_rgb = {};
  atmosphere.mie_absorption_rgb = {};
  atmosphere.ozone_absorption_rgb = {
    .0276F,
    .0001F,
    0,
  };
  atmosphere.ozone_density_profile.layers.at(0) = {
    .width_m = 2000,
    .constant_term = 1,
  };
  atmosphere.ozone_density_profile.layers.at(1) = {
    .constant_term = 1,
  };
  const auto begin = [&](unsigned sequence) -> void {
    ctx_.frame_sequence = frame::SequenceNumber {
      sequence,
    };
    ctx_.frame_slot = frame::Slot {
      (sequence - 1U) % 3U,
    };
    stable.atmosphere_revision = sequence;
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    cache.RefreshForState(stable);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    scattering.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  };
  begin(1U);
  ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
  const auto trans_pixels = ReadFloatTexture(*cache.GetTransmittanceTexture());
  const auto& trans_desc = cache.GetTransmittanceTexture()->GetDescriptor();
  // Constant absorption makes Beer-Lambert independent of the integration
  // quadrature. Geometry is reconstructed in double from the documented LUT UV.
  const double horizon = std::numbers::sqrt3;
  const double rho = (.5 / trans_desc.height) * horizon;
  const double radius = std::sqrt(1 + (rho * rho));
  const double length
    = 2 - radius + ((.5 / trans_desc.width) * (rho + horizon - (2 - radius)));
  const double expected_trans
    = std::exp(-static_cast<double>((.0276F * 1000.0F)) * length);
  ASSERT_GT(expected_trans, 0.0);
  EXPECT_NEAR(trans_pixels.at(0).at(0), expected_trans, expected_trans * 2e-5);
  EXPECT_EQ((data::HalfFloat {
               trans_pixels.at(0).at(0),
             })
              .ToFloat(),
    0.0F);
  struct ProbeAddress {
    unsigned pixel;
    unsigned channel;
  };
  const auto check_illumination
    = [&](const Texture& texture, ShaderVisibleIndex srv, ProbeAddress address,
        const Pixel& value) -> void {
    const auto& desc = texture.GetDescriptor();
    const auto half = MakeSignal(1U, 1U,
      std::span {
        &value,
        1U,
      },
      1U, Format::kRGBA16Float);
    constexpr float gain = 1.88e9F;
    const auto row = address.pixel / desc.width;
    // Slot 3 is the renderer's registered linear-clamp sampler.
    const std::array<std::array<std::uint32_t, 4>, 4> inputs {
      std::array {
        srv.get(),
        3U,
        std::bit_cast<std::uint32_t>(
          (static_cast<float>(address.pixel % desc.width) + .5F)
          / static_cast<float>(desc.width)),
        std::bit_cast<std::uint32_t>(
          (static_cast<float>(row) + .5F) / static_cast<float>(desc.height)),
      },
      std::array {
        std::bit_cast<std::uint32_t>(gain),
        0U,
        0U,
        0U,
      },
      std::array {
        half.srv.get(),
        3U,
        std::bit_cast<std::uint32_t>(.5F),
        std::bit_cast<std::uint32_t>(.5F),
      },
      std::array {
        std::bit_cast<std::uint32_t>(gain),
        0U,
        0U,
        0U,
      },
    };
    const auto result = RunToneProbe(std::as_bytes(std::span {
                                       inputs,
                                     }),
      2U, 512U);
    const double expected
      = static_cast<double>(value.at(address.channel)) * gain;
    EXPECT_GE(expected, 0x1p-24);
    EXPECT_LE(expected, 0x1p32);
    EXPECT_NEAR(result.at(0).at(address.channel), expected, expected * 2e-5);
    EXPECT_EQ(result.at(1).at(address.channel), 0.0F);
  };
  check_illumination(*cache.GetTransmittanceTexture(),
    cache.GetState().transmittance_lut_srv,
    {
      .pixel = 0U,
      .channel = 0U,
    },
    trans_pixels.at(0));
  atmosphere = environment::AtmosphereModel {};
  atmosphere.enabled = true;
  begin(2U);
  ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
  ASSERT_TRUE(scattering.Record(ctx_, stable, cache).executed);
  const auto baseline = ReadFloatTexture(*cache.GetMultiScatteringTexture());
  unsigned brightest = 0U;
  unsigned channel = 0U;
  for (unsigned pixel = 0; pixel < baseline.size(); ++pixel) {
    for (unsigned c = 0; c < 3; ++c) {
      if (baseline.at(pixel).at(c) > baseline.at(brightest).at(channel)) {
        brightest = pixel;
        channel = c;
      }
    }
  }
  ASSERT_GT(baseline.at(brightest).at(channel), 0.0F);
  atmosphere.multi_scattering_factor = 0x1p-30F;
  begin(3U);
  ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
  ASSERT_TRUE(scattering.Record(ctx_, stable, cache).executed);
  const auto tiny = ReadFloatTexture(*cache.GetMultiScatteringTexture());
  const double expected_scattering
    = static_cast<double>(baseline.at(brightest).at(channel)) * 0x1p-30;
  EXPECT_NEAR(tiny.at(brightest).at(channel), expected_scattering,
    expected_scattering * 2e-5);
  check_illumination(*cache.GetMultiScatteringTexture(),
    cache.GetState().multi_scattering_lut_srv,
    {
      .pixel = brightest,
      .channel = channel,
    },
    tiny.at(brightest));
  ctx_.view_constants.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, ThinMediaArithmeticPreservesAmplifiedRequiredSignals)
{
  std::vector<std::array<float, 4>> inputs;
  for (const float depth : {
         -.1F,
         -.01F,
         -1e-8F,
         0.0F,
         0x1p-40F,
         0x1p-24F,
         1e-5F,
         .00999F,
         .01F,
         .1F,
         1.0F,
         10.0F,
       }) {
    for (const float source : {
           0x1p-24F,
           1.0F,
           0x1p32F,
         }) {
      inputs.push_back({
        depth,
        source,
        0,
        0,
      });
    }
  }
  const auto output = RunToneProbe(std::as_bytes(std::span {
                                     inputs,
                                   }),
    static_cast<std::uint32_t>(inputs.size()), 1024U);
  unsigned reproduced_losses = 0;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const double opacity
      = -std::expm1(-static_cast<double>(inputs.at(i).at(0)));
    const double input_opacity
      = std::min(std::abs(static_cast<double>(inputs.at(i).at(0))), 1.0);
    const double optical_depth
      = input_opacity < 1.0 - static_cast<double>(1e-6F)
      ? -std::log1p(-input_opacity)
      : -std::log(static_cast<double>(1e-6F));
    const double radiance = opacity * static_cast<double>(inputs.at(i).at(1));
    EXPECT_NEAR(
      output.at(i).at(0), opacity, (std::abs(opacity) * 2e-5) + 0x1p-120);
    EXPECT_NEAR(
      output.at(i).at(1), optical_depth, (optical_depth * 2e-5) + 0x1p-120);
    EXPECT_NEAR(
      output.at(i).at(2), radiance, (std::abs(radiance) * 2e-5) + 0x1p-120);
    if (radiance >= 0x1p-24 && output.at(i).at(3) == 0) {
      EXPECT_GT(output.at(i).at(2), 0);
      ++reproduced_losses;
    }
  }
  EXPECT_GT(reproduced_losses, 0U);
}

NOLINT_TEST_F(ExposureGpuTest, SkyProducerPreservesThinBrightScattering)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  ctx_.current_view.with_atmosphere = true;
  ctx_.current_view.hdr_color_format = Format::kRGBA32Float;
  auto view_data = ViewConstants::GpuData {};
  auto view_buffer = CreateUploadBuffer(
    SizeBytes {
      256U,
    },
    BufferUsage::kConstant);
  view_buffer->Update(&view_data, sizeof(view_data), 0U);
  ctx_.view_constants = view_buffer;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto multiple = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto sky = environment::AtmosphereSkyViewLutPass(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  auto& atmosphere = stable.view_products.atmosphere;
  atmosphere.enabled = true;
  atmosphere.planet_radius_m = 1000;
  atmosphere.atmosphere_height_m = 1000;
  atmosphere.rayleigh_scale_height_m = 1e15F;
  atmosphere.mie_scattering_rgb = {};
  atmosphere.mie_absorption_rgb = {};
  atmosphere.ozone_absorption_rgb = {};
  atmosphere.ground_albedo_rgb = {};
  atmosphere.multi_scattering_factor = 0;
  auto environment_view = EnvironmentViewData {};
  environment_view.sky_planet_translated_world_center_km_and_view_height_km = {
    0,
    0,
    -1,
    1.25F,
  };
  unsigned sequence = 0;
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  unsigned cases = 0;
  for (const unsigned light_slot : {
         0U,
         1U,
       }) {
    for (const float illuminance : {
           .5e-6F,
           1e-6F,
           1.001e-6F,
           1.88e9F,
         }) {
      for (const float ev : {
             -32.0F,
             0.0F,
             32.0F,
           }) {
        stable.view_products.atmosphere_lights = {};
        auto& light = stable.view_products.atmosphere_lights.at(light_slot);
        light.enabled = true;
        light.direction_to_light_ws = {
          0,
          0,
          1,
        };
        light.illuminance_rgb_lux = glm::vec3 {
          illuminance,
        };
        stable.view_products.atmosphere_light_count = light_slot + 1;
        settings.manual_ev = ev;
        const auto exposure_config = SharedConfig(settings);
        SCOPED_TRACE(light_slot);
        SCOPED_TRACE(illuminance);
        SCOPED_TRACE(ev);
        for (const float extinction : {
               0.0F,
               1e-12F,
               .999e-9F,
               1e-9F,
               1.001e-9F,
               1e-6F,
             }) {
          SCOPED_TRACE(extinction);
          atmosphere.rayleigh_scattering_rgb = glm::vec3 {
            extinction / 1000.0F,
          };
          ctx_.frame_sequence = frame::SequenceNumber {
            ++sequence,
          };
          ctx_.frame_slot = frame::Slot {
            (sequence - 1U) % 3U,
          };
          auto exposure_inputs = postprocess::ExposurePass::FrameInputs {};
          exposure_inputs.use_fp32 = false;
          const auto exposure = SubmitCommands("Vortex Exposure Frame",
            [&](graphics::CommandRecorder& recorder) -> auto {
              return pass_->ResolveFrame(
                ctx_, recorder, exposure_config, exposure_inputs);
            });
          ASSERT_NE(exposure, nullptr);
          ctx_.current_view.frame_exposure = exposure;
          auto bindings = ViewFrameBindings {};
          bindings.frame_exposure_slot = exposure->srv_index;
          bindings.exposure_status_uav
            = exposure->current_state->status_uav_index;
          view_data.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
            PublishFixtureData(bindings),
          };
          view_buffer->Update(&view_data, sizeof(view_data), 0U);
          stable.atmosphere_revision = sequence;
          cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          cache.RefreshForState(stable);
          transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          multiple.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          sky.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          observer_ptr<FrameCaptureController> capture;
          if (light_slot == 1 && illuminance == .5e-6F && ev == -32
            && extinction == 1e-6F) {
            capture = BeginOptionalCapture();
          }
          ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
          ASSERT_TRUE(multiple.Record(ctx_, stable, cache).executed);
          const auto produced = SubmitCommands("Vortex test",
            [&](oxygen::graphics::CommandRecorder& recorder) -> auto {
              return sky.Record(
                ctx_, recorder, environment_view, stable, cache);
            });
          ASSERT_TRUE(produced.executed);
          const auto pixels = ReadFloatTexture(*produced.texture);
          // Row zero is exactly zenith. Constant-density, single Rayleigh
          // scattering toward a zenith sun has constant total light+view
          // attenuation along the .75 km ray: L = E * phase(1) * sigma * d *
          // exp(-sigma*d).
          const double sigma
            = static_cast<double>(atmosphere.rayleigh_scattering_rgb.x) * 1000;
          const double expected
            = static_cast<double>(light.illuminance_rgb_lux.x)
            * (3 / (8 * std::acos(-1.0))) * sigma * .75 * std::exp(-sigma * .75)
            * std::exp2(-static_cast<double>(ev));
          for (unsigned x = 0; x < produced.width; ++x) {
            for (unsigned c = 0; c < 3; ++c) {
              EXPECT_NEAR(
                pixels.at(x).at(c), expected, (expected * 2e-5) + 0x1p-120);
            }
          }
          if (capture) {
            EXPECT_TRUE(capture->EndCapture());
          }
          const auto status = Read<ExposureCompletedStatus>(
            *exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 16U, 0U);
          ++cases;
        }
      }
    }
  }
  RecordProperty("sky_scattering_endpoint_cases", cases);
  ctx_.current_view.frame_exposure.reset();
  ctx_.view_constants.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, ThinScatteringIntegralHasContinuousVacuumLimit)
{
  std::vector<Pixel> inputs;
  for (const float extinction : {
         0.0F,
         1e-12F,
         0.999e-9F,
         1e-9F,
         1.001e-9F,
         1e-5F,
         .00999F,
         .01F,
         .1F,
         1.0F,
         100.0F,
       }) {
    for (const float distance : {
           0.0F,
           1e-3F,
           1.0F,
           100.0F,
         }) {
      for (const float source : {
             .0001496056465063816F,
             1.0F,
             0x1p32F,
           }) {
        inputs.push_back({
          extinction,
          distance,
          source,
          0,
        });
      }
    }
  }
  const auto output = RunToneProbe(std::as_bytes(std::span {
                                     inputs,
                                   }),
    static_cast<std::uint32_t>(inputs.size()), 2048U);
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    SCOPED_TRACE(i);
    const double extinction = inputs.at(i).at(0);
    const double distance = inputs.at(i).at(1);
    const double integral = extinction == 0
      ? distance
      : -std::expm1(-extinction * distance) / extinction;
    const double radiance = integral * inputs.at(i).at(2);
    EXPECT_NEAR(output.at(i).at(0), integral, (integral * 2e-5) + 0x1p-120);
    EXPECT_NEAR(output.at(i).at(1), radiance, (radiance * 2e-5) + 0x1p-120);
  }
  RecordProperty("continuous_integral_cases", inputs.size());
}

NOLINT_TEST_F(
  ExposureGpuTest, FogOnlySkyComposesOnceAndPreservesDisplayBackgroundCoverage)
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
  auto scene = std::make_shared<scene::Scene>("Fog-only sky", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(false);
  fog.SetFogDensity(0.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetMaxOpacity(0.5F);
  fog.SetFogInscatteringLuminance({ 0.2F, 0.4F, 0.6F });
  auto& background
    = scene->GetEnvironment()->AddSystem<scene::environment::Background>();
  background.SetColorRgb({ 0.1F, 0.2F, 0.3F });
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto output = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  const auto capture = BeginOptionalCapture();
  unsigned sequence = 0U;
  for (const float ev : { 0.0F, 2.0F }) {
    settings.manual_ev = ev;
    post.SetExposureSettings(settings);
    for (const bool display_background : { false, true }) {
      background.SetEnabled(display_background);
      for (const bool main_pass : { true, false }) {
        fog.SetRenderInMainPass(main_pass);
        fog.SetVisibleInRealTimeSkyCaptures(false);
        scene->Update();
        ++sequence;
        auto input = Renderer::OffscreenSceneViewInput::FromCamera(
          "Fog-only sky", ViewId { 8120U }, view, camera);
        input.SetViewStateHandle(CompositionView::ViewStateHandle { 8120U });
        input.SetWithAtmosphere(false).SetWithHeightFog(true);
        auto facade = renderer_->ForOffscreenScene();
        facade.SetFrameSession(
          { .frame_slot = frame::Slot { (sequence - 1U) % 3U },
            .frame_sequence = frame::SequenceNumber { sequence },
            .delta_time_seconds = 0.0F });
        facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
        facade.SetOutputTarget(
          { .framebuffer = observer_ptr { framebuffer.get() } });
        facade.SetViewIntent(input);
        auto session = facade.Finalize();
        ASSERT_TRUE(session.has_value());
        ASSERT_TRUE(session->ExecuteNow());
        auto* owner
          = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
            *renderer_);
        const auto& product
          = owner->GetSceneTextureExtracts().resolved_scene_color;
        ASSERT_TRUE(product.valid);
        ASSERT_NE(product.exposure, nullptr);
        const auto domain = Read<FrameExposureData>(
          *product.exposure->buffer, ResourceStates::kShaderResource);
        const auto pixels = ReadFloatTexture(*product.texture, true);
        const auto expected = std::array { 0.1F, 0.2F, 0.3F };
        for (const auto& pixel : pixels) {
          for (auto c = 0U; c < 3U; ++c)
            EXPECT_NEAR(pixel[c],
              main_pass ? expected[c] * domain.pre_exposure : 0.0F, 0.0005F);
          if (display_background)
            EXPECT_NEAR(pixel[3], main_pass ? 0.5F : 0.0F, 1.0e-5F);
        }
        // Pixel (1, 0) has zero display dither. Background is authored in
        // display-linear space and must remain independent of camera exposure.
        const auto display = ReadFloatTexture(*output);
        const auto srgb_to_linear = [](const float value) {
          return value <= 0.04045F ? value / 12.92F
                                   : std::pow((value + 0.055F) / 1.055F, 2.4F);
        };
        const auto linear_to_srgb = [](const float value) {
          return value <= 0.0031308F
            ? 12.92F * value
            : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
        };
        for (auto c = 0U; c < 3U; ++c) {
          const float coverage = main_pass ? 0.5F : 0.0F;
          const float fog_display = 2.0F * expected[c] * std::exp2(-ev);
          const float composed = display_background
            ? linear_to_srgb(coverage * srgb_to_linear(fog_display)
                + (1.0F - coverage) * expected[c])
            : coverage * fog_display;
          EXPECT_NEAR(display.at(1).at(c), composed, 0.0005F)
            << "EV=" << ev << " background=" << display_background
            << " main_pass=" << main_pass;
        }
      }
    }
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  FlushBackend();
}

NOLINT_TEST_F(
  ExposureGpuTest, CapturedAtmosphereUsesGlobalAnchorAndIgnoresViewExposure)
{
  namespace env = environment::internal;
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  auto source = std::make_unique<env::CapturedSkySource>(*renderer_);
  auto processor = env::IblGpuProcessor(Backend());
  auto brdf_owner = env::IblBrdfResources(Backend());
  const auto brdf = brdf_owner.Prepare();
  ASSERT_TRUE(brdf.has_value());
  auto state = env::StableAtmosphereState {};
  state.atmosphere_revision = 1U;
  auto& atmosphere = state.view_products.atmosphere;
  atmosphere.enabled = true;
  atmosphere.sun_disk_enabled = true;
  state.view_products.atmosphere_light_count = 2U;
  for (unsigned i = 0U; i < 2U; ++i) {
    auto& light = state.view_products.atmosphere_lights[i];
    light.direction_to_light_ws
      = glm::normalize(glm::vec3(i == 0U ? 1.0F : -1.0F, 0.0F, 1.0F));
    light.illuminance_rgb_lux = glm::vec3(1000.0F);
    light.disk_luminance_scale_rgb = glm::vec3(1.0F);
  }
  ctx_.current_view.with_atmosphere = false;
  ctx_.current_view.view_id = kInvalidViewId;
  ctx_.view_constants.reset();
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  ctx_.frame_slot = frame::Slot { 0U };
  FailureBackend().fail_recorder_name
    = "EnvironmentLightingService AtmosphereTransmittanceLut";
  const auto failed
    = source->Process(ctx_, state, {}, processor, *brdf, {}, 999U);
  FailureBackend().fail_recorder_name.clear();
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), env::IblProcessError::kRecordingFailed);
  EXPECT_EQ(processor.GetStats().storage_creations, 0U);
  unsigned sequence = 0U;
  const auto produce = [&](bool first, bool second) {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence };
    ctx_.frame_slot = frame::Slot { (sequence - 1U) % 3U };
    state.light_revision = sequence;
    state.view_products.atmosphere_lights[0].enabled = first;
    state.view_products.atmosphere_lights[1].enabled = second;
    const auto products = source->Process(ctx_, state, {}, processor, *brdf,
      { .face_size = 128U, .lower_hemisphere_solid_color = false }, sequence);
    CHECK_F(products.has_value());
    return *products;
  };
  auto primary = produce(true, false);
  auto secondary = produce(false, true);
  const auto capture = BeginOptionalCapture();
  const auto combined = produce(true, true);
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  const auto read_sh = [&](const auto& product) {
    const auto metadata = Read<environment::IblProductMetadata>(
      *product->metadata, ResourceStates::kShaderResource);
    EXPECT_EQ(metadata.processing_flags, 3U);
    EXPECT_GT(metadata.average_brightness, 0.0F);
    EXPECT_FLOAT_EQ(metadata.source_radiance_scale, 1.0F);
    return Read<std::array<glm::vec4, 8>>(
      *product->diffuse_sh, ResourceStates::kShaderResource);
  };
  const auto sh0 = read_sh(primary);
  const auto sh1 = read_sh(secondary);
  const auto sh_sum = read_sh(combined);
  const auto primary_capture = primary->AcquireCapture();
  const auto secondary_capture = secondary->AcquireCapture();
  ASSERT_TRUE(primary_capture && secondary_capture);
  primary.reset();
  secondary.reset();
  // Atmospheric transport and SH are linear in incident illuminance. Both
  // native slots must contribute without a role-None fallback or disk energy.
  for (unsigned row = 0U; row < 8U; ++row)
    for (unsigned c = 0U; c < (row == 6U ? 3U : 4U); ++c) {
      const float expected = sh0[row][c] + sh1[row][c];
      EXPECT_NEAR(
        sh_sum[row][c], expected, 0.002F * std::max(1.0F, std::abs(expected)));
    }

  auto bindings = ViewFrameBindings {};
  bindings.frame_exposure_slot = PublishFixtureData(FrameExposureData {
    .pre_exposure = 0.125F, .one_over_pre_exposure = 8.0F });
  auto view = ViewConstants::GpuData {};
  view.camera_position = { 1234.0F, -5678.0F, 9000.0F };
  view.view_matrix = glm::mat4(2.0F);
  view.projection_matrix = glm::mat4(3.0F);
  view.view_frame_bindings_bslot
    = BindlessViewFrameBindingsSlot { PublishFixtureData(bindings) };
  auto camera_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  camera_buffer->Update(&view, sizeof(view), 0U);
  ctx_.view_constants = camera_buffer;
  ctx_.current_view.view_id = ViewId { 999U };
  ctx_.current_view.with_atmosphere = true;
  ctx_.current_view.hdr_color_format = Format::kRGBA16Float;
  atmosphere.sun_disk_enabled = false;
  atmosphere.aerial_perspective_distance_scale = 20.0F;
  atmosphere.aerial_scattering_strength = 4.0F;
  atmosphere.aerial_perspective_start_depth_m = 5000.0F;
  const auto changed_view = produce(true, true);
  EXPECT_EQ(read_sh(changed_view), sh_sum);
  EXPECT_EQ(ReadFloatTexture(*changed_view->processed_cube, true),
    ReadFloatTexture(*combined->processed_cube, true));
  auto fog = GpuFogParams {};
  fog.flags = kGpuFogFlagEnabled | kGpuFogFlagHeightFogEnabled
    | kGpuFogFlagVisibleInRealTimeSkyCaptures;
  fog.primary_density = 0.01F;
  fog.min_transmittance = 0.5F;
  fog.max_opacity = 0.5F;
  fog.fog_inscattering_luminance_rgb = { 0.2F, 0.4F, 0.6F };
  fog.sky_atmosphere_ambient_contribution_color_scale_rgb = {};
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence };
  ctx_.frame_slot = frame::Slot { (sequence - 1U) % 3U };
  const auto fogged = source->Process(ctx_, state, fog, processor, *brdf,
    { .face_size = 128U, .lower_hemisphere_solid_color = false }, sequence);
  ASSERT_TRUE(fogged.has_value());
  const auto clear_pixels = ReadFloatTexture(*combined->processed_cube, true);
  const auto fog_pixels = ReadFloatTexture(*(*fogged)->processed_cube, true);
  ASSERT_EQ(clear_pixels.size(), fog_pixels.size());
  for (std::size_t i = 0U; i < clear_pixels.size(); ++i)
    for (unsigned c = 0U; c < 3U; ++c) {
      const auto expected
        = 0.5F * (clear_pixels[i][c] + fog.fog_inscattering_luminance_rgb[c]);
      EXPECT_NEAR(
        fog_pixels[i][c], expected, 0.002F * std::max(1.0F, expected));
    }
  // Submit once more, then release the source without a readback or queue
  // wait. LUT constants/descriptors must survive their CPU pass owners.
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence };
  ctx_.frame_slot = frame::Slot { (sequence - 1U) % 3U };
  state.light_revision = sequence;
  state.view_products.atmosphere_lights[0].illuminance_rgb_lux *= 2.0F;
  state.view_products.atmosphere_lights[1].illuminance_rgb_lux *= 2.0F;
  auto final = source->Process(ctx_, state, {}, processor, *brdf,
    { .face_size = 128U, .lower_hemisphere_solid_color = false }, sequence);
  ASSERT_TRUE(final.has_value());
  source.reset();
  const auto final_sh = read_sh(*final);
  for (unsigned row = 0U; row < 8U; ++row)
    for (unsigned c = 0U; c < (row == 6U ? 3U : 4U); ++c)
      EXPECT_NEAR(final_sh[row][c], 2.0F * sh_sum[row][c],
        0.002F * std::max(1.0F, std::abs(2.0F * sh_sum[row][c])));
  const auto retained_primary = Read<std::array<glm::vec4, 8>>(
    *primary_capture->DiffuseSh(), ResourceStates::kShaderResource);
  const auto retained_secondary = Read<std::array<glm::vec4, 8>>(
    *secondary_capture->DiffuseSh(), ResourceStates::kShaderResource);
  EXPECT_EQ(retained_primary, sh0);
  EXPECT_EQ(retained_secondary, sh1);
  ctx_.view_constants.reset();
  FlushBackend();
}

NOLINT_TEST_F(ExposureGpuTest, SkyIblCacheSeparatesSceneLifetimesAndReusesBrdf)
{
  namespace env = environment::internal;
  auto first_scene = std::make_shared<scene::Scene>("IBL scene A", 8U);
  auto second_scene = std::make_shared<scene::Scene>("IBL scene B", 8U);
  auto processor = env::IblProcessor(*renderer_);
  auto state = env::StableAtmosphereState {};
  state.view_products.sky_light.enabled = true;
  state.view_products.sky_light.source
    = environment::kSkyLightSourceCapturedScene;
  state.view_products.sky_light.lower_hemisphere_is_solid_color = false;
  state.view_products.height_fog.enabled = true;
  state.view_products.height_fog.enable_height_fog = true;
  state.view_products.height_fog.visible_in_real_time_sky_captures = true;
  auto fog = GpuFogParams {};
  fog.flags = kGpuFogFlagEnabled | kGpuFogFlagHeightFogEnabled
    | kGpuFogFlagVisibleInRealTimeSkyCaptures;
  fog.primary_density = 0.01F;
  fog.fog_inscattering_luminance_rgb = { 0.25F, 0.5F, 1.0F };
  ctx_.scene = observer_ptr { first_scene.get() };
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  ctx_.frame_slot = frame::Slot { 0U };
  const auto first_state
    = processor.RefreshSkyLightProducts({}, ctx_, state, fog, {});
  ASSERT_TRUE(first_state.refreshed);
  const auto first = processor.GetPublishedProducts();
  ASSERT_NE(first, nullptr);
  const auto first_capture = processor.AcquireCapture(first);
  ASSERT_TRUE(first_capture);
  const auto reference = ReadFloatTexture(*first->processed_cube, true);
  const auto reuse = processor.RefreshSkyLightProducts(
    first_state.probe_state, ctx_, state, fog, {});
  EXPECT_FALSE(reuse.requested);
  EXPECT_EQ(processor.GetPublishedProducts(), first);
  state.view_products.atmosphere.aerial_perspective_distance_scale = 20.0F;
  state.view_products.atmosphere.aerial_scattering_strength = 4.0F;
  state.view_products.atmosphere.aerial_perspective_start_depth_m = 5000.0F;
  state.view_products.sky_light.cubemap_resource = content::ResourceKey { 99U };
  ctx_.current_view.view_id = ViewId { 991U };
  EXPECT_FALSE(
    processor.RefreshSkyLightProducts(reuse.probe_state, ctx_, state, fog, {})
      .requested);
  EXPECT_EQ(processor.GetPublishedProducts(), first);

  ctx_.scene = observer_ptr { second_scene.get() };
  auto missing = state;
  missing.view_products.sky_light.source
    = environment::kSkyLightSourceSpecifiedCubemap;
  missing.view_products.sky_light.cubemap_resource = {};
  const auto failed = processor.RefreshSkyLightProducts(
    reuse.probe_state, ctx_, missing, fog, {});
  EXPECT_FALSE(failed.probe_state.valid);
  EXPECT_EQ(processor.GetPublishedProducts(), nullptr);
  const auto replacement = processor.RefreshSkyLightProducts(
    failed.probe_state, ctx_, state, fog, {});
  ASSERT_TRUE(replacement.refreshed);
  const auto second = processor.GetPublishedProducts();
  ASSERT_NE(second, nullptr);
  EXPECT_GT(second->revision, first->revision);
  EXPECT_NE(second->processed_cube, first->processed_cube);
  EXPECT_EQ(second->brdf, first->brdf);
  EXPECT_EQ(ReadFloatTexture(*second->processed_cube, true), reference);
  EXPECT_EQ(processor.GetCachedSceneCount(), 2U);
  ctx_.scene = observer_ptr { first_scene.get() };
  const auto resumed = processor.RefreshSkyLightProducts(
    replacement.probe_state, ctx_, state, fog, {});
  EXPECT_FALSE(resumed.requested);
  EXPECT_EQ(processor.GetPublishedProducts(), first);
  EXPECT_EQ(processor.GetCachedSceneCount(), 2U);
  const auto old_scene = std::weak_ptr(first_scene);
  ctx_.scene = observer_ptr { second_scene.get() };
  first_scene.reset();
  EXPECT_TRUE(old_scene.expired());
  const auto expired_capture = processor.AcquireCapture(first);
  ASSERT_FALSE(expired_capture);
  EXPECT_EQ(expired_capture.error(), environment::IblCaptureError::kClosed);
  // Admission cleanup must not consume the next frame's invalidation signal.
  EXPECT_TRUE(processor.OnFrameStart());
  EXPECT_FALSE(processor.OnFrameStart());
  EXPECT_FALSE(
    processor.RefreshSkyLightProducts(resumed.probe_state, ctx_, state, fog, {})
      .requested);
  EXPECT_EQ(processor.GetPublishedProducts(), second);
  EXPECT_EQ(processor.GetCachedSceneCount(), 1U);
  EXPECT_EQ(ReadFloatTexture(*first->processed_cube, true), reference);
  ctx_.scene = nullptr;
  second_scene.reset();
  EXPECT_EQ(processor.GetPublishedProducts(), nullptr);
  EXPECT_TRUE(processor.OnFrameStart());
  EXPECT_EQ(processor.GetCachedSceneCount(), 0U);
  EXPECT_EQ(ReadFloatTexture(*second->processed_cube, true), reference);
  FlushBackend();
}

NOLINT_TEST_F(
  ExposureGpuTest, CaptureSkyLutMatchesVisibleSkyAtAnchorAcrossExposureDomains)
{
  namespace env = environment::internal;
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  ctx_.current_view.with_atmosphere = true;
  ctx_.current_view.hdr_color_format = Format::kRGBA32Float;
  auto cache = env::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto multiple = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto sky = environment::AtmosphereSkyViewLutPass(*renderer_);
  auto state = env::StableAtmosphereState {};
  state.atmosphere_revision = 1U;
  state.view_products.atmosphere.enabled = true;
  state.view_products.atmosphere_light_count = 1U;
  auto& light = state.view_products.atmosphere_lights[0];
  light.enabled = true;
  light.direction_to_light_ws = glm::normalize(glm::vec3(1.0F, 0.0F, 1.0F));
  light.illuminance_rgb_lux = glm::vec3(1000.0F);
  cache.RefreshForState(state);
  const auto& quality = cache.GetState().internal_parameters;
  const auto anchor
    = env::ResolveSkyCaptureOrigin(state.view_products.atmosphere);
  const auto view
    = env::BuildAtmosphereViewData(state, quality, anchor, true, true);
  auto captured = Backend().CreateTexture({ .width = quality.sky_view_width,
    .height = quality.sky_view_height,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture2D,
    .debug_name = "Capture parity reference",
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon });
  auto registration = Backend().GetResourceRegistry().RegisterManaged(captured);
  ASSERT_TRUE(registration.has_value());
  unsigned sequence = 0U;
  for (const float pre_exposure : { 0.125F, 8.0F }) {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence };
    ctx_.frame_slot = frame::Slot { (sequence - 1U) % 3U };
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    multiple.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    sky.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    auto bindings = ViewFrameBindings {};
    bindings.frame_exposure_slot
      = PublishFixtureData(FrameExposureData { .pre_exposure = pre_exposure,
        .one_over_pre_exposure = 1.0F / pre_exposure });
    auto data = ViewConstants::GpuData {};
    data.view_frame_bindings_bslot
      = BindlessViewFrameBindingsSlot { PublishFixtureData(bindings) };
    auto camera_buffer
      = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
    camera_buffer->Update(&data, sizeof(data), 0U);
    ctx_.view_constants = camera_buffer;
    if (cache.NeedsTransmittanceBuild())
      ASSERT_TRUE(transmittance.Record(ctx_, state, cache).executed);
    if (cache.NeedsMultiScatteringBuild())
      ASSERT_TRUE(multiple.Record(ctx_, state, cache).executed);
    const auto visible = SubmitCommands(
      "Sky LUT parity", [&](graphics::CommandRecorder& recorder) {
        return sky.Record(ctx_, recorder, view, state, cache);
      });
    ASSERT_TRUE(visible.executed);
    const auto capture = SubmitCommands(
      "Capture LUT parity", [&](graphics::CommandRecorder& recorder) {
        return sky.RecordCapture(
          ctx_, recorder, view, state, cache, captured, *registration);
      });
    ASSERT_TRUE(capture.executed);
    const auto a = ReadFloatTexture(*visible.texture);
    const auto b = ReadFloatTexture(*capture.texture);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0U; i < a.size(); ++i)
      for (unsigned c = 0U; c < 4U; ++c) {
        const float expected = b[i][c] * (c == 3U ? 1.0F : pre_exposure);
        EXPECT_NEAR(
          a[i][c], expected, std::max(1.0e-7F, std::abs(expected) * 2.0e-5F));
      }
  }
  ctx_.view_constants.reset();
  FlushBackend();
}

NOLINT_TEST(
  AtmosphereCaptureCoordinates, AllAuthoredPlanetModesUseTheSameNativeFrame)
{
  namespace env = environment::internal;
  auto state = env::StableAtmosphereState {};
  auto& atmosphere = state.view_products.atmosphere;
  atmosphere.planet_radius_m = 1000.0F;
  atmosphere.planet_anchor_position_ws = { 10.0F, 20.0F, 30.0F };
  for (const auto mode :
    { environment::AtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin,
      environment::AtmosphereTransformMode::kPlanetTopAtComponentTransform,
      environment::AtmosphereTransformMode::
        kPlanetCenterAtComponentTransform }) {
    atmosphere.transform_mode = mode;
    const auto origin = env::ResolveSkyCaptureOrigin(atmosphere);
    const auto expected = mode
        == environment::AtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin
      ? glm::vec3(0.0F, 0.0F, 1.0F)
      : glm::vec3(10.0F, 20.0F,
          mode
              == environment::AtmosphereTransformMode::
                kPlanetTopAtComponentTransform
            ? 31.0F
            : 1031.0F);
    EXPECT_EQ(origin, expected);
    const auto view
      = env::BuildAtmosphereViewData(state, {}, origin, true, true);
    EXPECT_EQ(glm::vec3(view.sky_view_lut_referential_row2),
      glm::vec3(0.0F, 0.0F, 1.0F));
    EXPECT_FLOAT_EQ(
      view.sky_planet_translated_world_center_km_and_view_height_km.w, 1.001F);
    EXPECT_NEAR(
      glm::dot(glm::cross(glm::vec3(view.sky_view_lut_referential_row0),
                 glm::vec3(view.sky_view_lut_referential_row1)),
        glm::vec3(view.sky_view_lut_referential_row2)),
      1.0F, 1.0e-6F);
  }
}

} // namespace oxygen::vortex::testing::exposure
