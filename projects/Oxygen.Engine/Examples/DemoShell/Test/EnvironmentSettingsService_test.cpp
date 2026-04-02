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
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedPromotesFirstDirectionalLightWhenSceneHasNoTaggedSun)
{
  auto scene = MakeScene("DemoShell.PromoteFirstDirectional");
  auto fill = CreateDirectionalLightNode(*scene, "Fill", false);
  ASSERT_TRUE(fill.IsAlive());

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
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedPrefersDirectionalNamedSunOverEarlierDirectional)
{
  auto scene = MakeScene("DemoShell.PreferNamedSun");
  auto fill = CreateDirectionalLightNode(*scene, "Fill", false);
  auto named_sun = CreateDirectionalLightNode(*scene, "SUN", false);
  ASSERT_TRUE(fill.IsAlive());
  ASSERT_TRUE(named_sun.IsAlive());

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
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedPreservesAuthoredSunTaggedDirectionalLight)
{
  auto scene = MakeScene("DemoShell.PreserveTaggedSun");
  auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
  ASSERT_TRUE(authored_sun.IsAlive());

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
    "EnvironmentSettingsService.CameraOnly", loguru::Verbosity_9);

  service_.OnSceneActivated(*scene);

  EXPECT_FALSE(capture.Contains(
    "activated non-empty scene 'DemoShell.CameraOnlyFallback' had no "
    "usable scene directional light"));
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
    "EnvironmentSettingsService.NonEmpty", loguru::Verbosity_9);

  service_.OnSceneActivated(*scene);

  EXPECT_TRUE(capture.Contains(
    "activated non-empty scene 'DemoShell.NonEmptySceneWithoutSun' had no "
    "usable scene directional light"));
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

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  AppliesDirectionalShadowSettingsToAuthoredSceneSunLight)
{
  auto scene = MakeScene("DemoShell.SceneSunShadowSettings");
  auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
  ASSERT_TRUE(authored_sun.IsAlive());
  AttachSceneSunSystem(*scene, authored_sun);

  service_.OnSceneActivated(*scene);
  ASSERT_EQ(service_.GetSunSource(), 0);

  service_.SetSunShadowBias(0.0125F);
  service_.SetSunShadowNormalBias(0.08F);
  service_.SetSunShadowResolutionHint(
    static_cast<int>(oxygen::scene::ShadowResolutionHint::kHigh));
  service_.SetSunShadowCascadeCount(3);
  service_.SetSunShadowSplitMode(
    static_cast<int>(oxygen::scene::DirectionalCsmSplitMode::kManualDistances));
  service_.SetSunShadowMaxDistance(300.0F);
  service_.SetSunShadowDistributionExponent(4.5F);
  service_.SetSunShadowTransitionFraction(0.18F);
  service_.SetSunShadowDistanceFadeoutFraction(0.27F);
  service_.SetSunShadowCascadeDistance(0, 10.0F);
  service_.SetSunShadowCascadeDistance(1, 35.0F);
  service_.SetSunShadowCascadeDistance(2, 90.0F);
  service_.ApplyPendingChanges();

  auto light = authored_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  const auto& shadow = light->get().Common().shadow;
  const auto& csm = light->get().CascadedShadows();

  EXPECT_FLOAT_EQ(shadow.bias, 0.0125F);
  EXPECT_FLOAT_EQ(shadow.normal_bias, 0.08F);
  EXPECT_EQ(shadow.resolution_hint, oxygen::scene::ShadowResolutionHint::kHigh);
  EXPECT_EQ(csm.cascade_count, 3U);
  EXPECT_EQ(
    csm.split_mode, oxygen::scene::DirectionalCsmSplitMode::kManualDistances);
  EXPECT_FLOAT_EQ(csm.max_shadow_distance, 300.0F);
  EXPECT_FLOAT_EQ(csm.distribution_exponent, 4.5F);
  EXPECT_FLOAT_EQ(csm.transition_fraction, 0.18F);
  EXPECT_FLOAT_EQ(csm.distance_fadeout_fraction, 0.27F);
  EXPECT_FLOAT_EQ(csm.cascade_distances[0], 10.0F);
  EXPECT_FLOAT_EQ(csm.cascade_distances[1], 35.0F);
  EXPECT_FLOAT_EQ(csm.cascade_distances[2], 90.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  AppliesDirectionalShadowSettingsToSyntheticSunLight)
{
  auto scene = MakeScene("DemoShell.SyntheticSunShadowSettings");

  service_.OnSceneActivated(*scene);
  ASSERT_EQ(service_.GetSunSource(), 1);

  service_.SetSunShadowBias(0.02F);
  service_.SetSunShadowNormalBias(0.11F);
  service_.SetSunShadowResolutionHint(
    static_cast<int>(oxygen::scene::ShadowResolutionHint::kUltra));
  service_.SetSunShadowCascadeCount(2);
  service_.SetSunShadowSplitMode(
    static_cast<int>(oxygen::scene::DirectionalCsmSplitMode::kGenerated));
  service_.SetSunShadowMaxDistance(420.0F);
  service_.SetSunShadowDistributionExponent(5.0F);
  service_.SetSunShadowTransitionFraction(0.22F);
  service_.SetSunShadowDistanceFadeoutFraction(0.31F);
  service_.ApplyPendingChanges();

  auto synthetic_node = FindNodeByName(*scene, "Synthetic Sun");
  ASSERT_TRUE(synthetic_node.has_value());
  auto light = synthetic_node->GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  const auto& shadow = light->get().Common().shadow;
  const auto& csm = light->get().CascadedShadows();

  EXPECT_FLOAT_EQ(shadow.bias, 0.02F);
  EXPECT_FLOAT_EQ(shadow.normal_bias, 0.11F);
  EXPECT_EQ(
    shadow.resolution_hint, oxygen::scene::ShadowResolutionHint::kUltra);
  EXPECT_EQ(csm.cascade_count, 2U);
  EXPECT_EQ(csm.split_mode, oxygen::scene::DirectionalCsmSplitMode::kGenerated);
  EXPECT_FLOAT_EQ(csm.max_shadow_distance, 420.0F);
  EXPECT_FLOAT_EQ(csm.distribution_exponent, 5.0F);
  EXPECT_FLOAT_EQ(csm.transition_fraction, 0.22F);
  EXPECT_FLOAT_EQ(csm.distance_fadeout_fraction, 0.31F);
}

} // namespace oxygen::examples::testing
