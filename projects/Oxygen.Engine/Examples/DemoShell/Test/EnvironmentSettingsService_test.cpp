//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/EnvironmentSettingsService.h"

namespace oxygen::examples::testing {

namespace {

  class EnvironmentSettingsServiceTest : public ::testing::Test {
  protected:
    static constexpr size_t kSceneCapacity = 64;

    auto MakeScene(std::string_view name) -> std::shared_ptr<scene::Scene>
    {
      return std::make_shared<scene::Scene>(std::string(name), kSceneCapacity);
    }

    auto CreateDirectionalLightNode(scene::Scene& scene, std::string_view name,
      const bool is_sun_light) -> scene::SceneNode
    {
      auto node = scene.CreateNode(std::string(name));
      EXPECT_TRUE(node.IsAlive());
      EXPECT_TRUE(
        node.AttachLight(std::make_unique<scene::DirectionalLight>()));
      auto light = node.GetLightAs<scene::DirectionalLight>();
      EXPECT_TRUE(light.has_value());
      if (light.has_value()) {
        light->get().SetIsSunLight(is_sun_light);
        light->get().Common().affects_world = true;
        light->get().Common().casts_shadows = false;
        light->get().SetEnvironmentContribution(false);
      }
      return node;
    }

    static auto AttachSceneSunSystem(
      scene::Scene& scene, const scene::SceneNode& light_node) -> void
    {
      auto environment = std::make_unique<scene::SceneEnvironment>();
      auto& sun = environment->AddSystem<scene::environment::Sun>();
      sun.SetSunSource(scene::environment::SunSource::kFromScene);
      sun.SetLightReference(light_node);
      scene.SetEnvironment(std::move(environment));
    }

    static auto CollectSunLights(scene::Scene& scene)
      -> std::vector<scene::SceneNode>
    {
      std::vector<scene::SceneNode> result {};
      auto roots = scene.GetRootNodes();
      std::vector<scene::SceneNode> stack {};
      stack.reserve(roots.size());
      for (auto& root : roots) {
        stack.push_back(root);
      }

      while (!stack.empty()) {
        auto node = stack.back();
        stack.pop_back();
        if (!node.IsAlive()) {
          continue;
        }

        if (auto light = node.GetLightAs<scene::DirectionalLight>();
          light.has_value() && light->get().IsSunLight()) {
          result.push_back(node);
        }

        auto child = node.GetFirstChild();
        while (child.has_value()) {
          stack.push_back(*child);
          child = child->GetNextSibling();
        }
      }

      return result;
    }

    static auto FindNodeByName(scene::Scene& scene, std::string_view name)
      -> std::optional<scene::SceneNode>
    {
      auto roots = scene.GetRootNodes();
      std::vector<scene::SceneNode> stack {};
      stack.reserve(roots.size());
      for (auto& root : roots) {
        stack.push_back(root);
      }

      while (!stack.empty()) {
        auto node = stack.back();
        stack.pop_back();
        if (!node.IsAlive()) {
          continue;
        }
        if (node.GetName() == name) {
          return node;
        }

        auto child = node.GetFirstChild();
        while (child.has_value()) {
          stack.push_back(*child);
          child = child->GetNextSibling();
        }
      }

      return std::nullopt;
    }

    EnvironmentSettingsService service_ {};
  };

} // namespace

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedInjectsSyntheticSunWhenSceneHasNoDirectionalLight)
{
  auto scene = MakeScene("DemoShell.InjectSyntheticSun");
  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.InjectSyntheticSun", loguru::Verbosity_INFO);

  service_.OnSceneActivated(*scene);

  auto sun_lights = CollectSunLights(*scene);
  ASSERT_EQ(sun_lights.size(), 1U);
  EXPECT_EQ(sun_lights.front().GetName(), "Synthetic Sun");
  auto light = sun_lights.front().GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  EXPECT_TRUE(light->get().Common().affects_world);
  EXPECT_TRUE(light->get().GetEnvironmentContribution());
  EXPECT_TRUE(service_.GetSunLightAvailable());
  EXPECT_EQ(service_.GetSunSource(), 1);
  EXPECT_TRUE(
    capture.Contains("selected synthetic sun fallback 'Synthetic Sun'"));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedPromotesFirstDirectionalLightWhenSceneHasNoTaggedSun)
{
  auto scene = MakeScene("DemoShell.PromoteFirstDirectional");
  auto fill = CreateDirectionalLightNode(*scene, "Fill", false);
  ASSERT_TRUE(fill.IsAlive());
  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.PromoteFirstDirectional",
    loguru::Verbosity_INFO);

  service_.OnSceneActivated(*scene);

  auto sun_lights = CollectSunLights(*scene);
  ASSERT_EQ(sun_lights.size(), 1U);
  EXPECT_EQ(sun_lights.front().GetName(), "Fill");
  auto original_light = fill.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(original_light.has_value());
  EXPECT_TRUE(original_light->get().IsSunLight());
  EXPECT_TRUE(original_light->get().Common().casts_shadows);
  EXPECT_TRUE(original_light->get().GetEnvironmentContribution());
  EXPECT_EQ(service_.GetSunSource(), 0);
  EXPECT_TRUE(capture.Contains("selected scene directional 'Fill' as sun via "
                               "first directional selection"));
  EXPECT_TRUE(capture.Contains(
    "rejected synthetic sun for scene 'DemoShell.PromoteFirstDirectional'"));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedPrefersDirectionalNamedSunOverEarlierDirectional)
{
  auto scene = MakeScene("DemoShell.PreferNamedSun");
  auto fill = CreateDirectionalLightNode(*scene, "Fill", false);
  auto named_sun = CreateDirectionalLightNode(*scene, "SUN", false);
  ASSERT_TRUE(fill.IsAlive());
  ASSERT_TRUE(named_sun.IsAlive());

  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.PreferNamedSun", loguru::Verbosity_INFO);

  service_.OnSceneActivated(*scene);

  auto sun_lights = CollectSunLights(*scene);
  ASSERT_EQ(sun_lights.size(), 1U);
  EXPECT_EQ(sun_lights.front().GetName(), "SUN");

  auto fill_light = fill.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(fill_light.has_value());
  EXPECT_FALSE(fill_light->get().IsSunLight());

  auto sun_light = named_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(sun_light.has_value());
  EXPECT_TRUE(sun_light->get().IsSunLight());
  EXPECT_TRUE(sun_light->get().Common().casts_shadows);
  EXPECT_TRUE(sun_light->get().GetEnvironmentContribution());
  EXPECT_TRUE(capture.Contains(
    "selected scene directional 'SUN' as sun via node named SUN selection"));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedPreservesAuthoredSunTaggedDirectionalLight)
{
  auto scene = MakeScene("DemoShell.PreserveTaggedSun");
  auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
  ASSERT_TRUE(authored_sun.IsAlive());
  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.PreserveTaggedSun", loguru::Verbosity_INFO);

  service_.OnSceneActivated(*scene);

  auto sun_lights = CollectSunLights(*scene);
  ASSERT_EQ(sun_lights.size(), 1U);
  EXPECT_EQ(sun_lights.front().GetName(), "AuthoredSun");
  EXPECT_TRUE(service_.GetSunLightAvailable());
  EXPECT_EQ(service_.GetSunSource(), 0);
  auto light = authored_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  EXPECT_TRUE(light->get().Common().casts_shadows);
  EXPECT_TRUE(light->get().GetEnvironmentContribution());
  EXPECT_TRUE(capture.Contains("selected scene directional 'AuthoredSun' as "
                               "sun via sun-tagged selection"));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedDoesNotWarnWhenInjectingSyntheticSunIntoCameraOnlyScene)
{
  auto scene = MakeScene("DemoShell.CameraOnlyFallback");
  auto camera = scene->CreateNode("MainCamera");
  ASSERT_TRUE(camera.IsAlive());
  ASSERT_TRUE(
    camera.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));

  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.CameraOnly", loguru::Verbosity_INFO);

  service_.OnSceneActivated(*scene);

  EXPECT_FALSE(capture.Contains(
    "activated non-empty scene 'DemoShell.CameraOnlyFallback' had no "
    "usable scene directional light"));
  EXPECT_TRUE(capture.Contains(
    "activated camera-only scene 'DemoShell.CameraOnlyFallback' without "
    "directional lights; synthetic sun fallback was expected"));
  auto sun_lights = CollectSunLights(*scene);
  ASSERT_EQ(sun_lights.size(), 1U);
  EXPECT_EQ(sun_lights.front().GetName(), "Synthetic Sun");
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedWarnsWhenInjectingSyntheticSunIntoNonEmptyScene)
{
  auto scene = MakeScene("DemoShell.NonEmptySceneWithoutSun");
  auto marker = scene->CreateNode("Marker");
  ASSERT_TRUE(marker.IsAlive());

  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.NonEmpty", loguru::Verbosity_INFO);

  service_.OnSceneActivated(*scene);

  EXPECT_TRUE(capture.Contains(
    "activated non-empty scene 'DemoShell.NonEmptySceneWithoutSun' had no "
    "usable scene directional light"));
  EXPECT_TRUE(capture.Contains(
    "selected synthetic sun fallback 'Synthetic Sun' because no scene "
    "directional candidate was available"));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedDoesNotInjectSyntheticSunWhenSceneHasMultipleTaggedSuns)
{
  auto scene = MakeScene("DemoShell.InvalidMultiSun");
  ASSERT_TRUE(CreateDirectionalLightNode(*scene, "SunA", true).IsAlive());
  ASSERT_TRUE(CreateDirectionalLightNode(*scene, "SunB", true).IsAlive());

  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.MultiSun", loguru::Verbosity_ERROR);

  service_.OnSceneActivated(*scene);

  EXPECT_TRUE(capture.Contains("invalid scene lighting configuration in scene "
                               "'DemoShell.InvalidMultiSun'"));
  auto sun_lights = CollectSunLights(*scene);
  EXPECT_EQ(sun_lights.size(), 2U);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SyntheticOverrideBecomesOnlyShadowCastingSunNode)
{
  auto scene = MakeScene("DemoShell.SyntheticOverride");
  auto authored_sun = CreateDirectionalLightNode(*scene, "SUN", true);
  ASSERT_TRUE(authored_sun.IsAlive());

  service_.OnSceneActivated(*scene);
  ASSERT_EQ(service_.GetSunSource(), 0);

  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.SyntheticOverride", loguru::Verbosity_INFO);

  service_.SetSunSource(1);
  service_.ApplyPendingChanges();

  auto authored_light = authored_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(authored_light.has_value());
  EXPECT_FALSE(authored_light->get().IsSunLight());
  EXPECT_FALSE(authored_light->get().Common().affects_world);
  EXPECT_FALSE(authored_light->get().Common().casts_shadows);
  EXPECT_FALSE(authored_light->get().GetEnvironmentContribution());

  auto synthetic_node = FindNodeByName(*scene, "Synthetic Sun");
  ASSERT_TRUE(synthetic_node.has_value());
  auto synthetic_light = synthetic_node->GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(synthetic_light.has_value());
  EXPECT_TRUE(synthetic_light->get().IsSunLight());
  EXPECT_TRUE(synthetic_light->get().Common().affects_world);
  EXPECT_TRUE(synthetic_light->get().Common().casts_shadows);
  EXPECT_TRUE(synthetic_light->get().GetEnvironmentContribution());

  auto sun_lights = CollectSunLights(*scene);
  ASSERT_EQ(sun_lights.size(), 1U);
  EXPECT_EQ(sun_lights.front().GetName(), "Synthetic Sun");
  EXPECT_EQ(service_.GetSunSource(), 1);
  EXPECT_TRUE(capture.Contains("disabled scene directional 'SUN' because "
                               "synthetic sun override is active"));
  EXPECT_TRUE(capture.Contains(
    "using synthetic sun node 'Synthetic Sun' (source=synthetic"));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  DisableThenEnablePreservesAuthoredSceneSunIdentity)
{
  auto scene = MakeScene("DemoShell.DisableEnableSceneSun");
  auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
  ASSERT_TRUE(authored_sun.IsAlive());
  AttachSceneSunSystem(*scene, authored_sun);

  service_.OnSceneActivated(*scene);

  auto light = authored_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  EXPECT_TRUE(light->get().IsSunLight());
  EXPECT_TRUE(light->get().Common().affects_world);

  service_.SetSunEnabled(false);
  service_.ApplyPendingChanges();

  EXPECT_TRUE(light->get().IsSunLight());
  EXPECT_FALSE(light->get().Common().affects_world);
  EXPECT_FALSE(light->get().Common().casts_shadows);
  EXPECT_TRUE(service_.GetSunLightAvailable());

  service_.SetSunEnabled(true);
  service_.ApplyPendingChanges();

  EXPECT_TRUE(light->get().IsSunLight());
  EXPECT_TRUE(light->get().Common().affects_world);
  EXPECT_TRUE(light->get().Common().casts_shadows);
  EXPECT_TRUE(service_.GetSunLightAvailable());
  auto sun_lights = CollectSunLights(*scene);
  ASSERT_EQ(sun_lights.size(), 1U);
  EXPECT_EQ(sun_lights.front().GetName(), "AuthoredSun");
}

} // namespace oxygen::examples::testing
