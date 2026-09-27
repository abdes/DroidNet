//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereMultiScatteringLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/DistantSkyLightLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/VolumetricFogPass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  constexpr auto kSphereStrata = 8U;
  constexpr auto kSphereSamples = kSphereStrata * kSphereStrata;
  constexpr auto kSphereProbe = 524288U;
  constexpr auto kLobeExponent = 8.0;

  struct SphereCoordinates {
    double latitude {};
    double longitude {};
  };

  auto SphereDirection(const SphereCoordinates& coordinates) -> glm::dvec3
  {
    const auto latitude = coordinates.latitude;
    const auto longitude = coordinates.longitude;
    const auto z = 1.0 - (2.0 * latitude);
    const auto radius = std::sqrt(1.0 - (z * z));
    const auto phi = 2.0 * std::numbers::pi * longitude;
    return { radius * std::cos(phi), radius * std::sin(phi), z };
  }

  auto Ue57SphereSamples() -> std::array<glm::dvec3, kSphereSamples>
  {
    // SkyAtmosphereRendering.cpp's fixed-seed 8x8 stratification and
    // RandomStream.h::GetFraction, expressed without UE's pointer punning.
    constexpr auto kSeedMultiplier = 196314165U;
    constexpr auto kSeedIncrement = 907633515U;
    constexpr auto kFractionScale = 0x1p-23;
    auto seed = std::uint32_t { 0xDE4DC0DEU };
    const auto fraction = [&] -> double {
      seed = (seed * kSeedMultiplier) + kSeedIncrement;
      return static_cast<double>(seed >> 9U) * kFractionScale;
    };
    auto result = std::array<glm::dvec3, kSphereSamples> {};
    for (auto latitude = 0U; latitude < kSphereStrata; ++latitude) {
      for (auto longitude = 0U; longitude < kSphereStrata; ++longitude) {
        const auto u = (latitude + fraction()) / kSphereStrata;
        const auto v = (longitude + fraction()) / kSphereStrata;
        result.at((longitude * kSphereStrata) + latitude)
          = SphereDirection({ .latitude = u, .longitude = v });
      }
    }
    return result;
  }

  auto MeanSunLobe(
    std::span<const glm::dvec3> directions, const glm::dvec3& sun) -> double
  {
    auto sum = 0.0;
    for (const auto& direction : directions) {
      sum += std::pow(std::max(glm::dot(direction, sun), 0.0), kLobeExponent);
    }
    return sum / static_cast<double>(directions.size());
  }
} // namespace

NOLINT_TEST_F(ExposureGpuTest, DistantSkyPreservesSlotsFactorsAndFogAmbient)
{
  using graphics::ResourceStates;
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  auto view_params = ResolvedView::Params {};
  view_params.view_config.viewport = ViewPort { .width = 8.0F, .height = 8.0F };
  const auto resolved_view = ResolvedView(view_params);
  const auto view_data = ViewConstants::GpuData {};
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, graphics::BufferUsage::kConstant);
  view_buffer->Update(&view_data, sizeof(view_data), 0U);
  ctx_.view_constants = view_buffer;
  ctx_.current_view.resolved_view = observer_ptr { &resolved_view };
  ctx_.current_view.view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  ctx_.current_view.with_atmosphere = true;
  ctx_.current_view.with_height_fog = true;
  ctx_.current_view.hdr_color_format = Format::kRGBA32Float;

  auto stable = environment::internal::StableAtmosphereState {};
  stable.atmosphere_revision = 1U;
  stable.light_revision = 1U;
  stable.view_products.atmosphere.enabled = true;
  for (auto& light : stable.view_products.atmosphere_lights) {
    light.direction_to_light_ws = { 0.0F, 0.0F, 1.0F };
    light.illuminance_rgb_lux = glm::vec3 { 100000.0F };
  }
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto scattering = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto distant = environment::DistantSkyLightLutPass(*renderer_);
  auto fog = environment::VolumetricFogPass(*renderer_);
  struct Sample {
    Pixel sky {};
    std::vector<Pixel> fog;
  };
  const auto render = [&](bool expect_rebuild) -> Sample {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    cache.RefreshForState(stable);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    scattering.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    distant.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    fog.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    if (cache.NeedsTransmittanceBuild()) {
      EXPECT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
    }
    if (cache.NeedsMultiScatteringBuild()) {
      EXPECT_TRUE(scattering.Record(ctx_, stable, cache).executed);
    }
    EXPECT_EQ(distant.Record(ctx_, stable, cache).executed, expect_rebuild);
    const auto sky = Read<Pixel>(
      *cache.GetDistantSkyLightBuffer(), ResourceStates::kShaderResource);
    auto ambient = stable;
    for (auto& light : ambient.view_products.atmosphere_lights) {
      light.enabled = false;
    }
    auto& height = ambient.view_products.height_fog;
    height.enabled = true;
    height.enable_height_fog = true;
    height.fog_density = 0.5F;
    height.fog_height_falloff = 0.0F;
    auto& volume = ambient.view_products.volumetric_fog;
    volume.enabled = true;
    volume.start_distance = 1.0F;
    volume.distance = 32.0F;
    auto& sky_light = ambient.view_products.sky_light;
    sky_light.enabled = true;
    sky_light.intensity_mul = 1.0F;
    sky_light.diffuse_intensity = 1.0F;
    sky_light.volumetric_scattering_intensity = 1.0F;
    auto fog_state = environment::VolumetricFogPass::RecordState {};
    {
      auto recorder = AcquireRecorder("Distant sky ambient fog consumer");
      fog_state = fog.Record(
        ctx_, *recorder, ambient, cache.GetState().distant_sky_light_lut_srv);
    }
    EXPECT_TRUE(fog_state.executed);
    EXPECT_TRUE(fog_state.sky_light_injection_executed);
    if (!fog_state.texture) {
      ADD_FAILURE() << "Missing production volumetric output";
      return { .sky = sky, .fog = {} };
    }
    return { .sky = sky, .fog = ReadFloatTexture(*fog_state.texture) };
  };
  const auto check = [](const Sample& actual, const Sample& reference,
                       const glm::vec3& scale) -> void {
    ASSERT_EQ(actual.fog.size(), reference.fog.size());
    ASSERT_FALSE(actual.fog.empty());
    const auto gains = std::array { scale.x, scale.y, scale.z };
    for (std::size_t channel = 0; channel < gains.size(); ++channel) {
      const auto gain = gains.at(channel);
      const auto expected = reference.sky.at(channel) * gain;
      EXPECT_NEAR(actual.sky.at(channel), expected,
        std::max(1.0e-6F, std::abs(expected) * 1.0e-4F));
      for (std::size_t pixel = 0; pixel < actual.fog.size(); ++pixel) {
        const auto expected_fog = reference.fog.at(pixel).at(channel) * gain;
        EXPECT_NEAR(actual.fog.at(pixel).at(channel), expected_fog,
          std::max(1.0e-8F, std::abs(expected_fog) * 1.0e-4F));
        EXPECT_EQ(actual.fog.at(pixel).at(3), reference.fog.at(pixel).at(3));
      }
    }
  };
  const auto empty = render(true);
  stable.view_products.atmosphere_lights.at(0).enabled = true;
  stable.view_products.atmosphere_light_count = 1U;
  ++stable.light_revision;
  const auto primary = render(true);
  ASSERT_GT(primary.sky.at(0), 0.0F);
  ASSERT_FALSE(primary.fog.empty());
  ASSERT_GT(primary.fog.back().at(0), 0.0F);
  check(empty, primary, glm::vec3 { 0.0F });
  check(render(false), primary, glm::vec3 { 1.0F });
  stable.view_products.atmosphere_lights.at(0).enabled = false;
  stable.view_products.atmosphere_lights.at(1).enabled = true;
  ++stable.light_revision;
  check(render(true), primary, glm::vec3 { 1.0F });
  stable.view_products.atmosphere_lights.at(0).enabled = true;
  stable.view_products.atmosphere_light_count = 2U;
  ++stable.light_revision;
  check(render(true), primary, glm::vec3 { 2.0F });
  for (auto& light : stable.view_products.atmosphere_lights) {
    light.enabled = false;
  }
  stable.view_products.atmosphere_light_count = 0U;
  ++stable.light_revision;
  check(render(true), primary, glm::vec3 { 0.0F });
  stable.view_products.atmosphere_lights.at(1).enabled = true;
  stable.view_products.atmosphere_light_count = 1U;
  ++stable.light_revision;
  check(render(true), primary, glm::vec3 { 1.0F });
  constexpr auto kSkyFactor = glm::vec3 { 0.5F, 2.0F, 1.5F };
  stable.view_products.atmosphere.sky_luminance_factor_rgb = kSkyFactor;
  for (const auto& factor : std::array {
         glm::vec3 { 0.0F },
         glm::vec3 { 2.0F },
         glm::vec3 { 0.25F, 1.5F, 3.0F },
       }) {
    SCOPED_TRACE(::testing::PrintToString(factor));
    stable.view_products.atmosphere
      .sky_and_aerial_perspective_luminance_factor_rgb = factor;
    ++stable.atmosphere_revision;
    check(render(true), primary, factor * kSkyFactor);
    check(render(false), primary, factor * kSkyFactor);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, DistantSkyStrataRemoveDirectionalBiasAtFixedRayBudget)
{
  const auto input
    = std::array<std::array<std::uint32_t, 4>, kSphereSamples> {};
  const auto result = RunToneProbe(
    std::as_bytes(std::span(input)), kSphereSamples, kSphereProbe, false);
  ASSERT_EQ(result.size(), kSphereSamples);
  auto directions = std::array<glm::dvec3, kSphereSamples> {};
  auto moment = glm::dvec3 { 0.0 };
  auto solid_angle = 0.0;
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto& sample = result.at(index);
    const auto direction
      = glm::dvec3 { sample.at(0), sample.at(1), sample.at(2) };
    EXPECT_NEAR(glm::length(direction), 1.0, 2.0e-6);
    directions.at(index) = direction;
    moment += direction / static_cast<double>(kSphereSamples);
    solid_angle += sample.at(3);
  }
  EXPECT_NEAR(solid_angle, 4.0 * std::numbers::pi, 2.0e-6);
  EXPECT_NEAR(moment.x, 0.0, 1.0e-6);
  EXPECT_NEAR(moment.y, 0.0, 1.0e-6);
  EXPECT_NEAR(moment.z, 0.0, 1.0e-6);
  // Unit solid angle and first moments independently certify constant and
  // linear radiance. The old one-dimensional spiral biases L=1+x by 14.18%.
  EXPECT_NEAR(1.0 + moment.x, 1.0, 1.0e-6);
  constexpr auto kDenseLatitudeSteps = 128U;
  constexpr auto kDenseLongitudeSteps = 256U;
  auto dense = std::vector<glm::dvec3> {};
  dense.reserve(
    static_cast<std::size_t>(kDenseLatitudeSteps) * kDenseLongitudeSteps);
  for (auto latitude = 0U; latitude < kDenseLatitudeSteps; ++latitude) {
    for (auto longitude = 0U; longitude < kDenseLongitudeSteps; ++longitude) {
      dense.push_back(SphereDirection({
        .latitude = (latitude + 0.5) / kDenseLatitudeSteps,
        .longitude = (longitude + 0.5) / kDenseLongitudeSteps,
      }));
    }
  }
  const auto ue_samples = Ue57SphereSamples();
  constexpr auto kAzimuthSteps = 72U;
  constexpr auto kSunElevation = 0.3;
  constexpr auto kAnalyticMean = 1.0 / (2.0 * (kLobeExponent + 1.0));
  auto maximum_error = 0.0;
  auto ue_maximum_error = 0.0;
  for (auto step = 0U; step < kAzimuthSteps; ++step) {
    const auto azimuth = 2.0 * std::numbers::pi * step / kAzimuthSteps;
    const auto sun = glm::dvec3 { std::cos(azimuth) * std::cos(kSunElevation),
      std::sin(azimuth) * std::cos(kSunElevation), std::sin(kSunElevation) };
    const auto reference = MeanSunLobe(dense, sun);
    EXPECT_NEAR(reference, kAnalyticMean, kAnalyticMean * 1.0e-3);
    maximum_error = std::max(maximum_error,
      std::abs((MeanSunLobe(directions, sun) / reference) - 1.0));
    ue_maximum_error = std::max(ue_maximum_error,
      std::abs((MeanSunLobe(ue_samples, sun) / reference) - 1.0));
  }
  // Sampling a broad directional lobe qualifies azimuth bias, not atmosphere
  // transport accuracy. Keep the 64-ray budget and compare the UE5.7 seed too.
  EXPECT_LT(maximum_error, 0.03);
  EXPECT_LT(maximum_error, ue_maximum_error);
  RecordProperty("maximum_relative_error", std::to_string(maximum_error));
  RecordProperty(
    "ue57_maximum_relative_error", std::to_string(ue_maximum_error));
}

} // namespace oxygen::vortex::testing::exposure
