//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>

namespace oxygen::scene::testing {

NOLINT_TEST(EnvironmentComponentsTest, SkyAtmosphereExposesWidenedAuthoredFields)
{
  auto atmosphere = environment::SkyAtmosphere {};
  atmosphere.SetTransformMode(
    environment::SkyAtmosphereTransformMode::kPlanetCenterAtComponentTransform);
  atmosphere.SetSkyLuminanceFactorRgb({ 1.1F, 1.2F, 1.3F });
  atmosphere.SetSkyAndAerialPerspectiveLuminanceFactorRgb(
    { 0.9F, 0.8F, 0.7F });
  atmosphere.SetAerialPerspectiveStartDepthMeters(250.0F);
  atmosphere.SetHeightFogContribution(0.35F);
  atmosphere.SetTraceSampleCountScale(2.5F);
  atmosphere.SetTransmittanceMinLightElevationDeg(-12.0F);
  atmosphere.SetHoldout(true);
  atmosphere.SetRenderInMainPass(false);

  EXPECT_EQ(atmosphere.GetTransformMode(),
    environment::SkyAtmosphereTransformMode::kPlanetCenterAtComponentTransform);
  EXPECT_EQ(atmosphere.GetSkyLuminanceFactorRgb(), Vec3(1.1F, 1.2F, 1.3F));
  EXPECT_EQ(atmosphere.GetSkyAndAerialPerspectiveLuminanceFactorRgb(),
    Vec3(0.9F, 0.8F, 0.7F));
  EXPECT_FLOAT_EQ(atmosphere.GetAerialPerspectiveStartDepthMeters(), 250.0F);
  EXPECT_FLOAT_EQ(atmosphere.GetHeightFogContribution(), 0.35F);
  EXPECT_FLOAT_EQ(atmosphere.GetTraceSampleCountScale(), 2.5F);
  EXPECT_FLOAT_EQ(atmosphere.GetTransmittanceMinLightElevationDeg(), -12.0F);
  EXPECT_TRUE(atmosphere.GetHoldout());
  EXPECT_FALSE(atmosphere.GetRenderInMainPass());
}

NOLINT_TEST(EnvironmentComponentsTest,
  FogModelsHeightAndVolumetricControlsAsCoexistingAuthoredState)
{
  auto fog = environment::Fog {};
  fog.SetModel(environment::FogModel::kVolumetric);
  EXPECT_TRUE(fog.GetEnableHeightFog());
  EXPECT_TRUE(fog.GetEnableVolumetricFog());

  fog.SetEnableVolumetricFog(false);
  EXPECT_EQ(fog.GetModel(), environment::FogModel::kExponentialHeight);
  EXPECT_TRUE(fog.GetEnableHeightFog());
  EXPECT_FALSE(fog.GetEnableVolumetricFog());

  fog.SetSecondFogDensity(0.05F);
  fog.SetSecondFogHeightFalloff(0.02F);
  fog.SetSecondFogHeightOffset(-15.0F);
  fog.SetFogInscatteringLuminance({ 0.2F, 0.3F, 0.4F });
  fog.SetSkyAtmosphereAmbientContributionColorScale({ 0.5F, 0.6F, 0.7F });
  fog.SetInscatteringColorCubemapAngle(45.0F);
  fog.SetInscatteringTextureTint({ 0.8F, 0.7F, 0.6F });
  fog.SetFullyDirectionalInscatteringColorDistance(1000.0F);
  fog.SetNonDirectionalInscatteringColorDistance(5000.0F);
  fog.SetDirectionalInscatteringLuminance({ 0.9F, 0.8F, 0.7F });
  fog.SetDirectionalInscatteringExponent(8.0F);
  fog.SetDirectionalInscatteringStartDistance(64.0F);
  fog.SetEndDistanceMeters(1500.0F);
  fog.SetFogCutoffDistanceMeters(10000.0F);
  fog.SetVolumetricFogScatteringDistribution(0.4F);
  fog.SetVolumetricFogAlbedo({ 0.9F, 0.95F, 1.0F });
  fog.SetVolumetricFogEmissive({ 0.1F, 0.2F, 0.3F });
  fog.SetVolumetricFogExtinctionScale(1.5F);
  fog.SetVolumetricFogDistance(8000.0F);
  fog.SetVolumetricFogStartDistance(100.0F);
  fog.SetVolumetricFogNearFadeInDistance(50.0F);
  fog.SetVolumetricFogStaticLightingScatteringIntensity(0.75F);
  fog.SetOverrideLightColorsWithFogInscatteringColors(true);
  fog.SetHoldout(true);
  fog.SetRenderInMainPass(false);
  fog.SetVisibleInReflectionCaptures(false);
  fog.SetVisibleInRealTimeSkyCaptures(false);

  EXPECT_FLOAT_EQ(fog.GetSecondFogDensity(), 0.05F);
  EXPECT_FLOAT_EQ(fog.GetSecondFogHeightFalloff(), 0.02F);
  EXPECT_FLOAT_EQ(fog.GetSecondFogHeightOffset(), -15.0F);
  EXPECT_EQ(fog.GetFogInscatteringLuminance(), Vec3(0.2F, 0.3F, 0.4F));
  EXPECT_EQ(fog.GetSkyAtmosphereAmbientContributionColorScale(),
    Vec3(0.5F, 0.6F, 0.7F));
  EXPECT_FLOAT_EQ(fog.GetInscatteringColorCubemapAngle(), 45.0F);
  EXPECT_EQ(fog.GetInscatteringTextureTint(), Vec3(0.8F, 0.7F, 0.6F));
  EXPECT_FLOAT_EQ(fog.GetFullyDirectionalInscatteringColorDistance(), 1000.0F);
  EXPECT_FLOAT_EQ(fog.GetNonDirectionalInscatteringColorDistance(), 5000.0F);
  EXPECT_EQ(fog.GetDirectionalInscatteringLuminance(), Vec3(0.9F, 0.8F, 0.7F));
  EXPECT_FLOAT_EQ(fog.GetDirectionalInscatteringExponent(), 8.0F);
  EXPECT_FLOAT_EQ(fog.GetDirectionalInscatteringStartDistance(), 64.0F);
  EXPECT_FLOAT_EQ(fog.GetEndDistanceMeters(), 1500.0F);
  EXPECT_FLOAT_EQ(fog.GetFogCutoffDistanceMeters(), 10000.0F);
  EXPECT_FLOAT_EQ(fog.GetVolumetricFogScatteringDistribution(), 0.4F);
  EXPECT_EQ(fog.GetVolumetricFogAlbedo(), Vec3(0.9F, 0.95F, 1.0F));
  EXPECT_EQ(fog.GetVolumetricFogEmissive(), Vec3(0.1F, 0.2F, 0.3F));
  EXPECT_FLOAT_EQ(fog.GetVolumetricFogExtinctionScale(), 1.5F);
  EXPECT_FLOAT_EQ(fog.GetVolumetricFogDistance(), 8000.0F);
  EXPECT_FLOAT_EQ(fog.GetVolumetricFogStartDistance(), 100.0F);
  EXPECT_FLOAT_EQ(fog.GetVolumetricFogNearFadeInDistance(), 50.0F);
  EXPECT_FLOAT_EQ(
    fog.GetVolumetricFogStaticLightingScatteringIntensity(), 0.75F);
  EXPECT_TRUE(fog.GetOverrideLightColorsWithFogInscatteringColors());
  EXPECT_TRUE(fog.GetHoldout());
  EXPECT_FALSE(fog.GetRenderInMainPass());
  EXPECT_FALSE(fog.GetVisibleInReflectionCaptures());
  EXPECT_FALSE(fog.GetVisibleInRealTimeSkyCaptures());
}

NOLINT_TEST(EnvironmentComponentsTest, SkyLightExposesWidenedAuthoredFields)
{
  auto sky_light = environment::SkyLight {};
  sky_light.SetRealTimeCaptureEnabled(true);
  sky_light.SetLowerHemisphereColor({ 0.1F, 0.2F, 0.3F });
  sky_light.SetVolumetricScatteringIntensity(0.4F);
  sky_light.SetAffectReflections(false);
  sky_light.SetAffectGlobalIllumination(false);

  EXPECT_TRUE(sky_light.GetRealTimeCaptureEnabled());
  EXPECT_EQ(sky_light.GetLowerHemisphereColor(), Vec3(0.1F, 0.2F, 0.3F));
  EXPECT_FLOAT_EQ(sky_light.GetVolumetricScatteringIntensity(), 0.4F);
  EXPECT_FALSE(sky_light.GetAffectReflections());
  EXPECT_FALSE(sky_light.GetAffectGlobalIllumination());
}

NOLINT_TEST(EnvironmentComponentsTest,
  DirectionalLightExposesExplicitAtmosphereLightAuthoring)
{
  auto light = DirectionalLight {};
  light.SetAtmosphereLightSlot(AtmosphereLightSlot::kSecondary);
  light.SetUsePerPixelAtmosphereTransmittance(true);
  light.SetAtmosphereDiskLuminanceScale({ 1.2F, 0.9F, 0.8F });

  EXPECT_EQ(light.GetAtmosphereLightSlot(), AtmosphereLightSlot::kSecondary);
  EXPECT_TRUE(light.GetUsePerPixelAtmosphereTransmittance());
  EXPECT_EQ(light.GetAtmosphereDiskLuminanceScale(),
    Vec3(1.2F, 0.9F, 0.8F));
}

} // namespace oxygen::scene::testing
