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
      : 0u;
    return light;
  }

  class SunResolverTest : public ::testing::Test { };

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
  const SunState sun
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(sun.enabled);
  EXPECT_NEAR(sun.direction_ws.x, 0.0F, 0.001F);
  EXPECT_NEAR(sun.direction_ws.y, 1.0F, 0.001F);
  EXPECT_NEAR(sun.direction_ws.z, 0.0F, 0.001F);
  EXPECT_NEAR(sun.color_rgb.x, 1.0F, 0.001F);
  EXPECT_NEAR(sun.color_rgb.y, 0.9F, 0.001F);
  EXPECT_NEAR(sun.color_rgb.z, 0.8F, 0.001F);
  EXPECT_NEAR(sun.intensity, 5.0F, 0.001F);
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
  sun.SetIntensityLux(12345.0F);
  scene->SetEnvironment(std::move(environment));

  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 1.0F, 1.0F }, 10.0F, true),
  };

  // Act
  const SunState resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(resolved.enabled);
  EXPECT_NEAR(resolved.direction_ws.x, 0.0F, 0.001F);
  EXPECT_NEAR(resolved.direction_ws.y, 0.0F, 0.001F);
  EXPECT_NEAR(resolved.direction_ws.z, 1.0F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.x, 0.2F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.y, 0.3F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.z, 0.4F, 0.001F);
  EXPECT_NEAR(resolved.intensity, 12345.0F, 0.001F);
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
  const SunState resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(resolved.enabled);
  EXPECT_NEAR(resolved.direction_ws.x, 0.0F, 0.001F);
  EXPECT_NEAR(resolved.direction_ws.y, 1.0F, 0.001F);
  EXPECT_NEAR(resolved.direction_ws.z, 0.0F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.x, 0.1F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.y, 0.2F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.z, 0.3F, 0.001F);
  EXPECT_NEAR(resolved.intensity, 4.0F, 0.001F);
}

//! Clears invalid references and falls back to synthetic sun values.
NOLINT_TEST_F(SunResolverTest, InvalidReferenceFallsBackToSynthetic)
{
  // Arrange
  auto scene = std::make_shared<scene::Scene>("SunResolver.InvalidReference");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sun = environment->AddSystem<scene::environment::Sun>();
  sun.SetSunSource(scene::environment::SunSource::kFromScene);
  sun.SetDirectionWs({ 0.0F, 0.0F, 1.0F });
  sun.SetColorRgb({ 0.25F, 0.5F, 0.75F });
  sun.SetIntensityLux(8.0F);
  scene->SetEnvironment(std::move(environment));

  auto node = scene->CreateNode("MissingLight");
  sun.SetLightReference(node);
  scene->Update();

  const auto lights = std::array {
    MakeDirectionalLight(
      { 0.0F, -1.0F, 0.0F }, { 1.0F, 1.0F, 1.0F }, 1.0F, true),
  };

  // Act
  const SunState resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(resolved.enabled);
  EXPECT_NEAR(resolved.direction_ws.z, 1.0F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.x, 0.25F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.y, 0.5F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.z, 0.75F, 0.001F);
  EXPECT_NEAR(resolved.intensity, 8.0F, 0.001F);
  EXPECT_FALSE(sun.GetLightReference().has_value());
}

//! Falls back to tagged directional light when no reference is set.
NOLINT_TEST_F(SunResolverTest, FromSceneWithoutReferenceUsesSelectionRule)
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
  const SunState resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(resolved.enabled);
  EXPECT_NEAR(resolved.direction_ws.y, 1.0F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.x, 0.2F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.y, 0.4F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.z, 0.6F, 0.001F);
  EXPECT_NEAR(resolved.intensity, 2.5F, 0.001F);
}

//! Uses selection rule when a referenced node is no longer alive.
NOLINT_TEST_F(SunResolverTest, DeadReferenceFallsBackToSelectionRule)
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
  const SunState resolved
    = ResolveSunForView(*scene, std::span<const DirectionalLightBasic>(lights));

  // Assert
  EXPECT_TRUE(resolved.enabled);
  EXPECT_NEAR(resolved.direction_ws.y, 1.0F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.x, 0.7F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.y, 0.8F, 0.001F);
  EXPECT_NEAR(resolved.color_rgb.z, 0.9F, 0.001F);
  EXPECT_NEAR(resolved.intensity, 3.0F, 0.001F);
}

} // namespace oxygen::engine::internal::testing
