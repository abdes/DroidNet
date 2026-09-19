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
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereMultiScatteringLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/DistantSkyLightLutPass.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

using namespace oxygen::graphics;

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
  struct ExposureOverride final : IViewExtension {
    std::function<void(RenderContext&)> before;
    auto OnViewSetup(const ViewSetupContext& context) -> void override
    {
      before(context.render_context);
    }
  };
  bool force_nonunit = false;
  auto extension = std::make_shared<ExposureOverride>();
  extension->before = [&](RenderContext& ctx) {
    if (!force_nonunit) {
      return;
    }
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    service->SetResolvedConfig(SharedConfig(settings));
    ASSERT_NE(service->PrepareFrameExposure(ctx, false), nullptr);
  };
  renderer_->RegisterViewExtension(extension);
  unsigned sequence = 0U;
  for (const bool high_range : { false, true }) {
    settings.manual_ev = high_range ? 30.0F : 4.0F;
    post.SetExposureSettings(settings);
    sky.SetSolidColorRgb({ .25F, .5F, .75F });
    sky.SetIntensity(high_range ? 0x1p30F : 1.0F);
    scene->Update();
    const float expected_scale = high_range ? 1.0F : 1.0F / 16.0F;
    for (const bool nonunit : { false, true }) {
      force_nonunit = nonunit;
      ++sequence;
      auto input = Renderer::OffscreenSceneViewInput::FromCamera(
        "SkyDomain", ViewId { 8100U }, view, camera);
      input.SetViewStateHandle(CompositionView::ViewStateHandle { 8100U });
      input.SetWithAtmosphere(true);
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
                  .depth = 1U } })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      Pixel pixel {};
      std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
      EXPECT_NEAR(pixel[0], .25F * expected_scale, 2e-5F);
      EXPECT_NEAR(pixel[1], .5F * expected_scale, 2e-5F);
      EXPECT_NEAR(pixel[2], .75F * expected_scale, 2e-5F);
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
  extension->before = [](RenderContext&) { };
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
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  view_buffer->Update(&view_data, sizeof(view_data), 0U);
  ctx_.view_constants = view_buffer;
  auto stable = environment::internal::StableAtmosphereState {};
  stable.atmosphere_revision = 1;
  stable.view_products.atmosphere.enabled = true;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto scattering = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto distant = environment::DistantSkyLightLutPass(*renderer_);
  const auto render = [&](const std::array<float, 2>& intensity) {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { unsigned(sequence_ % 3) };
    stable.light_revision = sequence_;
    stable.view_products.atmosphere_light_count = 2;
    for (unsigned i = 0; i < 2; ++i) {
      auto& light = stable.view_products.atmosphere_lights[i];
      light.enabled = true;
      light.direction_to_light_ws = i == 0
        ? glm::vec3 { 0, 0, 1 }
        : glm::normalize(glm::vec3 { 1, 0, 1 });
      light.illuminance_rgb_lux = glm::vec3 { intensity[i] };
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
  const auto first = render({ 1, 0 });
  const auto second = render({ 0, 1 });
  for (unsigned channel = 0; channel < 3; ++channel) {
    ASSERT_GT(first[channel], 0.0F);
    ASSERT_GT(second[channel], 0.0F);
  }
  unsigned cases = 0;
  // Linearity of the radiative-transfer equation provides an independent
  // scaling/additivity oracle. Unit-light native anchors are not an absolute
  // atmospheric-accuracy reference; no production helper computes expected RGB.
  for (const float gain : { 0.0F, 0x1p-24F, 1.0F, 0x1p32F, 0x1p38F }) {
    for (const bool dual : { false, true }) {
      SCOPED_TRACE(gain);
      SCOPED_TRACE(dual);
      const auto result = render({ gain, dual ? gain * .5F : 0.0F });
      for (unsigned channel = 0; channel < 3; ++channel) {
        const double expected = double(gain)
          * (double(first[channel]) + (dual ? .5 * second[channel] : 0));
        EXPECT_NEAR(
          result[channel], expected, std::abs(expected) * 2e-5 + 0x1p-120);
      }
      EXPECT_EQ(result[3], 0);
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
  const std::array textures { cache.GetTransmittanceTexture(),
    cache.GetMultiScatteringTexture() };
  std::uint64_t allocation_bytes = 0U;
  std::uint64_t half_allocation_bytes = 0U;
  for (const auto& texture : textures) {
    ASSERT_NE(texture, nullptr);
    const auto& desc = texture->GetDescriptor();
    ASSERT_EQ(desc.format, Format::kRGBA32Float);
    auto native_desc
      = texture->GetNativeResource()->AsPointer<ID3D12Resource>()->GetDesc();
    auto* device
      = static_cast<ExposureFailureGraphics&>(Backend()).GetCurrentDevice();
    allocation_bytes
      += device->GetResourceAllocationInfo(0U, 1U, &native_desc).SizeInBytes;
    native_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    half_allocation_bytes
      += device->GetResourceAllocationInfo(0U, 1U, &native_desc).SizeInBytes;
    const std::vector<Pixel> values(std::size_t(desc.width) * desc.height,
      Pixel { transfer, transfer, transfer, 0 });
    const auto bytes = std::as_bytes(std::span { values });
    auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
    upload->Update(bytes.data(), bytes.size(), 0U);
    auto recorder = AcquireRecorder("Canonical small transfer upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    if (!recorder->AdoptKnownResourceState(*texture)) {
      recorder->BeginTrackingResourceState(*texture, desc.initial_state);
    }
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = desc.width * 16U,
        .buffer_slice_pitch = std::uint64_t(desc.width) * desc.height * 16U,
        .dst_slice
        = { .width = desc.width, .height = desc.height, .depth = 1U } },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  const std::array<Pixel, 1> tiny_pixel { Pixel {
    transfer, transfer, transfer, 0 } };
  const auto half_control
    = MakeSignal(1U, 1U, tiny_pixel, 1U, Format::kRGBA16Float);
  const std::array<std::array<std::uint32_t, 4>, 3> inputs {
    std::array { cache.GetState().transmittance_lut_srv.get(),
      std::bit_cast<std::uint32_t>(radiance), 0U, 0U },
    std::array { cache.GetState().multi_scattering_lut_srv.get(),
      std::bit_cast<std::uint32_t>(radiance), 0U, 0U },
    std::array {
      half_control.srv.get(), std::bit_cast<std::uint32_t>(radiance), 0U, 0U }
  };
  const auto results
    = RunToneProbe(std::as_bytes(std::span { inputs }), 3U, 256U);
  for (unsigned i = 0; i < 2; ++i) {
    for (unsigned c = 0; c < 3; ++c) {
      EXPECT_NEAR(results[i][c], double(transfer) * radiance,
        double(transfer) * radiance * 2e-5);
      EXPECT_EQ(results[i][4 + c], transfer);
    }
  }
  EXPECT_EQ(results[2][0],
    0.0F); // The old half format erases the required signal.
  for (const auto format : { Format::kRGBA16Float, Format::kRGBA32Float }) {
    ctx_.current_view.hdr_color_format = format;
    cache.RefreshForState(stable);
    EXPECT_EQ(cache.GetTransmittanceTexture(), textures[0]);
    EXPECT_EQ(cache.GetMultiScatteringTexture(), textures[1]);
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
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
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
  atmosphere.ozone_absorption_rgb = { .0276F, .0001F, 0 };
  atmosphere.ozone_density_profile.layers[0]
    = { .width_m = 2000, .constant_term = 1 };
  atmosphere.ozone_density_profile.layers[1] = { .constant_term = 1 };
  const auto begin = [&](unsigned sequence) {
    ctx_.frame_sequence = frame::SequenceNumber { sequence };
    ctx_.frame_slot = frame::Slot { (sequence - 1U) % 3U };
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
  const double horizon = std::sqrt(3.0);
  const double rho = (.5 / trans_desc.height) * horizon;
  const double radius = std::sqrt(1 + rho * rho);
  const double length
    = 2 - radius + (.5 / trans_desc.width) * (rho + horizon - (2 - radius));
  const double expected_trans
    = std::exp(-double(float(.0276F * 1000.0F)) * length);
  ASSERT_GT(expected_trans, 0.0);
  EXPECT_NEAR(trans_pixels[0][0], expected_trans, expected_trans * 2e-5);
  EXPECT_EQ(data::HalfFloat { trans_pixels[0][0] }.ToFloat(), 0.0F);
  const auto check_illumination = [&](const Texture& texture,
                                    ShaderVisibleIndex srv, unsigned pixel,
                                    unsigned channel, const Pixel& value) {
    const auto& desc = texture.GetDescriptor();
    const auto half
      = MakeSignal(1U, 1U, std::span { &value, 1U }, 1U, Format::kRGBA16Float);
    constexpr float gain = 1.88e9F;
    // Slot 3 is the renderer's registered linear-clamp sampler.
    const std::array<std::array<std::uint32_t, 4>, 4> inputs {
      std::array { srv.get(), 3U,
        std::bit_cast<std::uint32_t>(
          (float(pixel % desc.width) + .5F) / float(desc.width)),
        std::bit_cast<std::uint32_t>(
          (float(pixel / desc.width) + .5F) / float(desc.height)) },
      std::array { std::bit_cast<std::uint32_t>(gain), 0U, 0U, 0U },
      std::array { half.srv.get(), 3U, std::bit_cast<std::uint32_t>(.5F),
        std::bit_cast<std::uint32_t>(.5F) },
      std::array { std::bit_cast<std::uint32_t>(gain), 0U, 0U, 0U }
    };
    const auto result
      = RunToneProbe(std::as_bytes(std::span { inputs }), 2U, 512U);
    const double expected = double(value[channel]) * gain;
    EXPECT_GE(expected, 0x1p-24);
    EXPECT_LE(expected, 0x1p32);
    EXPECT_NEAR(result[0][channel], expected, expected * 2e-5);
    EXPECT_EQ(result[1][channel], 0.0F);
  };
  check_illumination(*cache.GetTransmittanceTexture(),
    cache.GetState().transmittance_lut_srv, 0U, 0U, trans_pixels[0]);
  atmosphere = environment::AtmosphereModel {};
  atmosphere.enabled = true;
  begin(2U);
  ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
  ASSERT_TRUE(scattering.Record(ctx_, stable, cache).executed);
  const auto baseline = ReadFloatTexture(*cache.GetMultiScatteringTexture());
  unsigned brightest = 0U, channel = 0U;
  for (unsigned pixel = 0; pixel < baseline.size(); ++pixel) {
    for (unsigned c = 0; c < 3; ++c) {
      if (baseline[pixel][c] > baseline[brightest][channel]) {
        brightest = pixel;
        channel = c;
      }
    }
  }
  ASSERT_GT(baseline[brightest][channel], 0.0F);
  atmosphere.multi_scattering_factor = 0x1p-30F;
  begin(3U);
  ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
  ASSERT_TRUE(scattering.Record(ctx_, stable, cache).executed);
  const auto tiny = ReadFloatTexture(*cache.GetMultiScatteringTexture());
  const double expected_scattering
    = double(baseline[brightest][channel]) * 0x1p-30;
  EXPECT_NEAR(
    tiny[brightest][channel], expected_scattering, expected_scattering * 2e-5);
  check_illumination(*cache.GetMultiScatteringTexture(),
    cache.GetState().multi_scattering_lut_srv, brightest, channel,
    tiny[brightest]);
  ctx_.view_constants.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, ThinMediaArithmeticPreservesAmplifiedRequiredSignals)
{
  std::vector<std::array<float, 4>> inputs;
  for (const float depth : { -.1F, -.01F, -1e-8F, 0.0F, 0x1p-40F, 0x1p-24F,
         1e-5F, .00999F, .01F, .1F, 1.0F, 10.0F }) {
    for (const float source : { 0x1p-24F, 1.0F, 0x1p32F }) {
      inputs.push_back({ depth, source, 0, 0 });
    }
  }
  const auto output = RunToneProbe(std::as_bytes(std::span { inputs }),
    static_cast<std::uint32_t>(inputs.size()), 1024U);
  unsigned reproduced_losses = 0;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const double opacity = -std::expm1(-double(inputs[i][0]));
    const double input_opacity = std::min(std::abs(double(inputs[i][0])), 1.0);
    const double optical_depth = input_opacity < 1.0 - double(1e-6F)
      ? -std::log1p(-input_opacity)
      : -std::log(double(1e-6F));
    const double radiance = opacity * double(inputs[i][1]);
    EXPECT_NEAR(output[i][0], opacity, std::abs(opacity) * 2e-5 + 0x1p-120);
    EXPECT_NEAR(output[i][1], optical_depth, optical_depth * 2e-5 + 0x1p-120);
    EXPECT_NEAR(output[i][2], radiance, std::abs(radiance) * 2e-5 + 0x1p-120);
    if (radiance >= 0x1p-24 && output[i][3] == 0) {
      EXPECT_GT(output[i][2], 0);
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
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
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
  environment_view.sky_planet_translated_world_center_km_and_view_height_km
    = { 0, 0, -1, 1.25F };
  unsigned sequence = 0;
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  unsigned cases = 0;
  for (const unsigned light_slot : { 0U, 1U }) {
    for (const float illuminance : { .5e-6F, 1e-6F, 1.001e-6F, 1.88e9F }) {
      for (const float ev : { -32.0F, 0.0F, 32.0F }) {
        stable.view_products.atmosphere_lights = {};
        auto& light = stable.view_products.atmosphere_lights[light_slot];
        light.enabled = true;
        light.direction_to_light_ws = { 0, 0, 1 };
        light.illuminance_rgb_lux = glm::vec3 { illuminance };
        stable.view_products.atmosphere_light_count = light_slot + 1;
        settings.manual_ev = ev;
        const auto exposure_config = SharedConfig(settings);
        SCOPED_TRACE(light_slot);
        SCOPED_TRACE(illuminance);
        SCOPED_TRACE(ev);
        for (const float extinction :
          { 0.0F, 1e-12F, .999e-9F, 1e-9F, 1.001e-9F, 1e-6F }) {
          SCOPED_TRACE(extinction);
          atmosphere.rayleigh_scattering_rgb
            = glm::vec3 { extinction / 1000.0F };
          ctx_.frame_sequence = frame::SequenceNumber { ++sequence };
          ctx_.frame_slot = frame::Slot { (sequence - 1U) % 3U };
          const auto exposure
            = pass_->ResolveFrame(ctx_, exposure_config, { .use_fp32 = false });
          ASSERT_NE(exposure, nullptr);
          ctx_.current_view.frame_exposure = exposure;
          auto bindings = ViewFrameBindings {};
          bindings.frame_exposure_slot = exposure->srv_index;
          bindings.exposure_status_uav
            = exposure->current_state->status_uav_index;
          view_data.view_frame_bindings_bslot
            = BindlessViewFrameBindingsSlot { PublishFixtureData(bindings) };
          view_buffer->Update(&view_data, sizeof(view_data), 0U);
          stable.atmosphere_revision = sequence;
          cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          cache.RefreshForState(stable);
          transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          multiple.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          sky.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          const auto capture = light_slot == 1 && illuminance == .5e-6F
              && ev == -32 && extinction == 1e-6F
            ? BeginOptionalCapture()
            : observer_ptr<FrameCaptureController> {};
          ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
          ASSERT_TRUE(multiple.Record(ctx_, stable, cache).executed);
          const auto produced
            = sky.Record(ctx_, environment_view, stable, cache);
          ASSERT_TRUE(produced.executed);
          const auto pixels = ReadFloatTexture(*produced.texture);
          // Row zero is exactly zenith. Constant-density, single Rayleigh
          // scattering toward a zenith sun has constant total light+view
          // attenuation along the .75 km ray: L = E * phase(1) * sigma * d *
          // exp(-sigma*d).
          const double sigma
            = double(atmosphere.rayleigh_scattering_rgb.x) * 1000;
          const double expected = double(light.illuminance_rgb_lux.x)
            * (3 / (8 * std::acos(-1.0))) * sigma * .75 * std::exp(-sigma * .75)
            * std::exp2(-double(ev));
          for (unsigned x = 0; x < produced.width; ++x) {
            for (unsigned c = 0; c < 3; ++c) {
              EXPECT_NEAR(pixels[x][c], expected, expected * 2e-5 + 0x1p-120);
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
  for (const float extinction : { 0.0F, 1e-12F, 0.999e-9F, 1e-9F, 1.001e-9F,
         1e-5F, .00999F, .01F, .1F, 1.0F, 100.0F }) {
    for (const float distance : { 0.0F, 1e-3F, 1.0F, 100.0F }) {
      for (const float source : { .0001496056465063816F, 1.0F, 0x1p32F }) {
        inputs.push_back({ extinction, distance, source, 0 });
      }
    }
  }
  const auto output = RunToneProbe(std::as_bytes(std::span { inputs }),
    static_cast<std::uint32_t>(inputs.size()), 2048U);
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    SCOPED_TRACE(i);
    const double extinction = inputs[i][0], distance = inputs[i][1];
    const double integral = extinction == 0
      ? distance
      : -std::expm1(-extinction * distance) / extinction;
    const double radiance = integral * inputs[i][2];
    EXPECT_NEAR(output[i][0], integral, integral * 2e-5 + 0x1p-120);
    EXPECT_NEAR(output[i][1], radiance, radiance * 2e-5 + 0x1p-120);
  }
  RecordProperty("continuous_integral_cases", inputs.size());
}

} // namespace oxygen::vortex::testing::exposure
