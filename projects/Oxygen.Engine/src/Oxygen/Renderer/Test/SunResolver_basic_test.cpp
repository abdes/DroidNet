//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include <glm/vec3.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Internal/SunResolver.h>
#include <Oxygen/Renderer/Types/DirectionalLightBasic.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
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

  class SunResolverTest : public ::testing::Test { };

  constexpr float kEpsilon = 0.001F;

} // namespace

//! Uses the first tagged sun light when no Sun component exists.
NOLINT_TEST_F(SunResolverTest, NoSunComponentFallsBackToTaggedDirectional)
{
  // Arrange
  auto scene = std::make_shared<scene::Scene>("SunResolver.NoSun");
  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 0.8F, 0.7F, 0.6F }, 2.0F, false),
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.9F, 0.8F }, 5.0F, true),
  };

  // Act
  const SyntheticSunData sun
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(sun.enabled);
  EXPECT_NEAR(sun.GetDirection().x, 0.0F, kEpsilon);
  EXPECT_NEAR(sun.GetDirection().y, 1.0F, kEpsilon);
  EXPECT_NEAR(sun.GetDirection().z, 0.0F, kEpsilon);
  EXPECT_NEAR(sun.GetColor().x, 1.0F, kEpsilon);
  EXPECT_NEAR(sun.GetColor().y, 0.9F, kEpsilon);
  EXPECT_NEAR(sun.GetColor().z, 0.8F, kEpsilon);
  EXPECT_NEAR(sun.GetIlluminance(), 5.0F, kEpsilon);
}

//! Uses authored sun values when Sun is in synthetic mode.
NOLINT_TEST_F(SunResolverTest, SyntheticSunOverridesDirectionalLights)
{
  // Arrange
  auto scene = std::make_shared<scene::Scene>("SunResolver.Synthetic");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sun = environment->AddSystem<scene::environment::Sun>();
  sun.SetSunSource(scene::environment::SunSource::kSynthetic);
  sun.SetDirectionWs({ 0.0F, 0.0F, 1.0F });
  sun.SetColorRgb({ 0.2F, 0.3F, 0.4F });
  sun.SetIlluminanceLx(12345.0F);
  scene->SetEnvironment(std::move(environment));

  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 1.0F, 1.0F }, 10.0F, true),
  };

  // Act
  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(resolved.enabled);
  EXPECT_NEAR(resolved.GetDirection().x, 0.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetDirection().y, 0.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetDirection().z, 1.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().x, 0.2F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().y, 0.3F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().z, 0.4F, kEpsilon);
  EXPECT_NEAR(resolved.GetIlluminance(), 12345.0F, kEpsilon);
}

//! Resolves from the referenced directional light in FromScene mode.
NOLINT_TEST_F(SunResolverTest, FromSceneUsesReferencedDirectionalLight)
{
  // Arrange
  auto scene = std::make_shared<scene::Scene>("SunResolver.Reference");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sun = environment->AddSystem<scene::environment::Sun>();
  sun.SetSunSource(scene::environment::SunSource::kFromScene);
  scene->SetEnvironment(std::move(environment));

  auto node = scene->CreateNode("SunLight");
  node.AttachLight(std::make_unique<scene::DirectionalLight>());
  auto light_opt = node.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light_opt.has_value());
  auto& light = light_opt->get();
  light.Common().color_rgb = { 0.1F, 0.2F, 0.3F };
  light.SetIntensityLux(4.0F);
  sun.SetLightReference(node);
  scene->Update();

  const auto lights = std::array {
    MakeDirectionalLight(
      { 1.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, 1.0F, true),
  };

  // Act
  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(resolved.enabled);
  EXPECT_NEAR(resolved.GetDirection().x, 0.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetDirection().y, 1.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetDirection().z, 0.0F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().x, 0.1F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().y, 0.2F, kEpsilon);
  EXPECT_NEAR(resolved.GetColor().z, 0.3F, kEpsilon);
  EXPECT_NEAR(resolved.GetIlluminance(), 4.0F, kEpsilon);
}

//! Clears invalid references and resolves to no sun.
NOLINT_TEST_F(SunResolverTest, InvalidReferenceResolvesToNoSun)
{
  // Arrange
  auto scene = std::make_shared<scene::Scene>("SunResolver.InvalidReference");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sun = environment->AddSystem<scene::environment::Sun>();
  sun.SetSunSource(scene::environment::SunSource::kFromScene);
  scene->SetEnvironment(std::move(environment));

  auto node = scene->CreateNode("MissingLight");
  sun.SetLightReference(node);
  scene->Update();

  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 1.0F, 1.0F }, 1.0F, true),
  };

  // Act
  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_FALSE(resolved.enabled);
  EXPECT_FALSE(sun.GetLightReference().has_value());
}

//! Resolves to no sun when no reference is set.
NOLINT_TEST_F(SunResolverTest, FromSceneWithoutReferenceResolvesToNoSun)
{
  // Arrange
  auto scene = std::make_shared<scene::Scene>("SunResolver.NoReference");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sun = environment->AddSystem<scene::environment::Sun>();
  sun.SetSunSource(scene::environment::SunSource::kFromScene);
  scene->SetEnvironment(std::move(environment));

  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 0.2F, 0.4F, 0.6F }, 2.5F, true),
  };

  // Act
  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_FALSE(resolved.enabled);
}

//! Resolves to no sun when a referenced node is no longer alive.
NOLINT_TEST_F(SunResolverTest, DeadReferenceResolvesToNoSun)
{
  // Arrange
  auto scene = std::make_shared<scene::Scene>("SunResolver.DeadReference");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sun = environment->AddSystem<scene::environment::Sun>();
  sun.SetSunSource(scene::environment::SunSource::kFromScene);
  scene->SetEnvironment(std::move(environment));

  auto node = scene->CreateNode("TempLight");
  node.AttachLight(std::make_unique<scene::DirectionalLight>());
  sun.SetLightReference(node);
  scene->Update();
  scene->DestroyNode(node);

  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 0.7F, 0.8F, 0.9F }, 3.0F, true),
  };

  // Act
  const SyntheticSunData resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_FALSE(resolved.enabled);
}

} // namespace oxygen::engine::internal::testing
