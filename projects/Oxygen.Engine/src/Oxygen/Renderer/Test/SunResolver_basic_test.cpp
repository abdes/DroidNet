//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Internal/SunResolver.h>
#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::engine::internal::testing {

namespace {

  [[nodiscard]] auto MakeDirectionalLight(const glm::vec3& direction_ws,
    const glm::vec3& color_rgb, const float intensity, const bool is_sun)
    -> DirectionalLightBasic
  {
    DirectionalLightBasic light {};
    light.direction_ws = direction_ws;
    light.color_rgb = color_rgb;
    light.intensity_lux = intensity;
    light.flags = is_sun
      ? static_cast<std::uint32_t>(DirectionalLightFlags::kSunLight)
      : 0U;
    return light;
  }

  class SunResolverTest : public ::testing::Test {
  protected:
    static constexpr float kEpsilon = 0.001F;
    static constexpr size_t kTestSceneCapacity = 100;

    [[nodiscard]] static auto MakeScene(std::string_view name)
      -> std::shared_ptr<scene::Scene>
    {
      return std::make_shared<scene::Scene>(
        std::string(name), kTestSceneCapacity);
    }
  };

} // namespace

//! No tagged directional light means the renderer has no authoritative sun.
NOLINT_TEST_F(SunResolverTest, NoTaggedDirectionalLightResolvesToNoSun)
{
  auto scene = MakeScene("SunResolver.NoTaggedSun");
  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 0.8F, 0.7F, 0.6F }, 2.0F, false),
    MakeDirectionalLight(
      { 1.0F, 0.0F, 0.0F }, { 0.4F, 0.5F, 0.6F }, 5.0F, false),
  };

  const SyntheticSunData sun
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  EXPECT_FALSE(sun.enabled);
}

//! The unique sun-tagged directional light is the only renderer sun authority.
NOLINT_TEST_F(SunResolverTest, UniqueTaggedDirectionalLightResolvesSun)
{
  auto scene = MakeScene("SunResolver.UniqueTaggedSun");
  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 0.8F, 0.7F, 0.6F }, 2.0F, false),
    MakeDirectionalLight(
      { 0.0F, 0.0F, -1.0F }, { 1.0F, 0.9F, 0.8F }, 5.0F, true),
  };

  const SyntheticSunData sun
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  ASSERT_TRUE(sun.enabled);
  EXPECT_NEAR(sun.GetDirection().x, 0.0F, kEpsilon);
  EXPECT_NEAR(sun.GetDirection().y, 0.0F, kEpsilon);
  EXPECT_NEAR(sun.GetDirection().z, 1.0F, kEpsilon);
  EXPECT_NEAR(sun.GetColor().x, 1.0F, kEpsilon);
  EXPECT_NEAR(sun.GetColor().y, 0.9F, kEpsilon);
  EXPECT_NEAR(sun.GetColor().z, 0.8F, kEpsilon);
  EXPECT_NEAR(sun.GetIlluminance(), 5.0F, kEpsilon);
}

//! Environment-authored synthetic sun data does not create a renderer sun.
NOLINT_TEST_F(SunResolverTest, SyntheticEnvironmentSunDoesNotInventRendererSun)
{
  auto scene = MakeScene("SunResolver.SyntheticEnvironmentIgnored");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sun = environment->AddSystem<scene::environment::Sun>();
  sun.SetSunSource(scene::environment::SunSource::kSynthetic);
  sun.SetDirectionWs({ 0.0F, 0.0F, 1.0F });
  sun.SetColorRgb({ 0.2F, 0.3F, 0.4F });
  sun.SetIlluminanceLx(12345.0F);
  scene->SetEnvironment(std::move(environment));

  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 0.8F, 0.7F, 0.6F }, 2.0F, false),
  };

  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  EXPECT_FALSE(resolved.enabled);
}

//! Environment-authored sun data is subordinate to the tagged directional
//! light.
NOLINT_TEST_F(
  SunResolverTest, SyntheticEnvironmentSunDoesNotOverrideTaggedDirectionalLight)
{
  auto scene = MakeScene("SunResolver.SyntheticEnvironmentSubordinate");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sun = environment->AddSystem<scene::environment::Sun>();
  sun.SetSunSource(scene::environment::SunSource::kSynthetic);
  sun.SetDirectionWs({ 1.0F, 0.0F, 0.0F });
  sun.SetColorRgb({ 0.2F, 0.3F, 0.4F });
  sun.SetIlluminanceLx(12345.0F);
  scene->SetEnvironment(std::move(environment));

  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 9.0F, true),
  };

  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  ASSERT_TRUE(resolved.enabled);
  EXPECT_NEAR(resolved.GetDirection().x, 0.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetDirection().y, 1.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetDirection().z, 0.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().x, 1.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().y, 0.9F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().z, 0.8F, kEpsilon);
  EXPECT_NEAR(resolved.GetIlluminance(), 9.0F, kEpsilon);
}

//! Multiple sun-tagged directional lights are rejected as invalid scene state.
NOLINT_TEST_F(SunResolverTest, MultipleTaggedDirectionalLightsResolveToNoSun)
{
  auto scene = MakeScene("SunResolver.MultipleTaggedSuns");
  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 20.0F, true),
    MakeDirectionalLight(
      { 0.0F, 0.0F, -1.0F }, { 0.9F, 0.8F, 0.7F }, 30.0F, true),
  };

  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  EXPECT_FALSE(resolved.enabled);
}

//! Invalid sun-tagged directional lights do not publish a broken sun payload.
NOLINT_TEST_F(SunResolverTest, ZeroLengthTaggedDirectionalLightResolvesToNoSun)
{
  auto scene = MakeScene("SunResolver.ZeroLengthSun");
  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 20.0F, true),
  };

  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  EXPECT_FALSE(resolved.enabled);
}

//! Renderer contract helper returns the unique tagged sun when the scene is
//! valid.
NOLINT_TEST_F(SunResolverTest, FindSunTaggedDirectionalLightReturnsUniqueTagged)
{
  const auto lights = std::array {
    MakeDirectionalLight(
      { 1.0F, 0.0F, 0.0F }, { 0.2F, 0.2F, 0.2F }, 10.0F, false),
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 20.0F, true),
  };

  const auto sun_light = FindSunTaggedDirectionalLight(
    std::span<const DirectionalLightBasic>(lights));

  ASSERT_TRUE(sun_light.has_value());
  EXPECT_NEAR(sun_light->direction_ws.x, 0.0F, kEpsilon);
  EXPECT_NEAR(sun_light->direction_ws.y, -1.0F, kEpsilon);
  EXPECT_NEAR(sun_light->direction_ws.z, 0.0F, kEpsilon);
  EXPECT_NEAR(sun_light->intensity_lux, 20.0F, kEpsilon);
}

//! Renderer contract helper rejects invalid multi-sun scenes.
NOLINT_TEST_F(
  SunResolverTest, FindSunTaggedDirectionalLightRejectsMultipleTagged)
{
  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 20.0F, true),
    MakeDirectionalLight(
      { 0.0F, 0.0F, -1.0F }, { 0.9F, 0.8F, 0.7F }, 30.0F, true),
  };

  EXPECT_FALSE(FindSunTaggedDirectionalLight(
    std::span<const DirectionalLightBasic>(lights))
      .has_value());
}

//! Matching resolved-sun and directional-light payloads satisfy the contract.
NOLINT_TEST_F(SunResolverTest, ResolvedSunMatchesDirectionalLightAcceptsMatch)
{
  const auto light = MakeDirectionalLight(
    { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 5.0F, true);
  const auto resolved = SyntheticSunData::FromDirectionAndLight(
    { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 5.0F, true);

  EXPECT_TRUE(ResolvedSunMatchesDirectionalLight(resolved, light));
}

//! Direction and illuminance mismatch is observable before shaders consume it.
NOLINT_TEST_F(
  SunResolverTest, ResolvedSunMatchesDirectionalLightRejectsMismatch)
{
  const auto light = MakeDirectionalLight(
    { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 5.0F, true);
  const auto resolved = SyntheticSunData::FromDirectionAndLight(
    { 0.0F, 0.0F, 1.0F }, { 1.0F, 0.9F, 0.8F }, 7.0F, true);

  EXPECT_FALSE(ResolvedSunMatchesDirectionalLight(resolved, light));
}

} // namespace oxygen::engine::internal::testing
