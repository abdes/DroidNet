//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>

#include "DemoShell/Services/DefaultSceneLighting.h"
#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/SkyboxService.h"
#include "DemoShell/UI/EnvironmentVm.h"

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
        light->get().SetEnvironmentContribution(is_sun_light);
      }
      return node;
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

    static auto CollectLocalFogVolumeNodes(scene::Scene& scene)
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

        const auto impl_opt = node.GetImpl();
        if (impl_opt.has_value()
          && impl_opt->get()
            .HasComponent<scene::environment::LocalFogVolume>()) {
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

    static auto ResetDemoSettings() -> void
    {
      const auto settings = SettingsService::ForDemoApp();
      ASSERT_NE(settings, nullptr);
      std::error_code ec;
      std::filesystem::remove(settings->GetStoragePath(), ec);
      settings->Load();
    }

    static auto PersistPendingSettings(EnvironmentSettingsService& service)
      -> void
    {
      engine::FrameContext frame_context {};
      service.OnFrameStart(frame_context);
      const auto settings = SettingsService::ForDemoApp();
      ASSERT_NE(settings, nullptr);
      settings->Save();
      settings->Load();
    }

    EnvironmentSettingsService service_ {};
  };

} // namespace

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedLeavesSceneWithoutSunWhenSceneHasNoDirectionalLight)
{
  auto scene = MakeScene("DemoShell.InjectSyntheticSun");

  service_.OnSceneActivated(*scene);

  auto sun_lights = CollectSunLights(*scene);
  EXPECT_TRUE(sun_lights.empty());
  EXPECT_FALSE(service_.GetSunLightAvailable());
  EXPECT_FALSE(FindNodeByName(*scene, "Synthetic Sun").has_value());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedLeavesUnresolvedWhenNoDirectionalLightIsTaggedAsSun)
{
  auto scene = MakeScene("DemoShell.PromoteFirstDirectional");
  auto fill = CreateDirectionalLightNode(*scene, "Fill", false);
  ASSERT_TRUE(fill.IsAlive());

  service_.OnSceneActivated(*scene);

  auto sun_lights = CollectSunLights(*scene);
  EXPECT_TRUE(sun_lights.empty());
  auto original_light = fill.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(original_light.has_value());
  EXPECT_FALSE(original_light->get().IsSunLight());
  EXPECT_FALSE(original_light->get().GetEnvironmentContribution());
  EXPECT_FALSE(service_.GetSunLightAvailable());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedDoesNotPromoteNamedDirectionalWithoutSunTag)
{
  auto scene = MakeScene("DemoShell.PreferNamedSun");
  auto fill = CreateDirectionalLightNode(*scene, "Fill", false);
  auto named_sun = CreateDirectionalLightNode(*scene, "SUN", false);
  ASSERT_TRUE(fill.IsAlive());
  ASSERT_TRUE(named_sun.IsAlive());

  service_.OnSceneActivated(*scene);

  auto sun_lights = CollectSunLights(*scene);
  EXPECT_TRUE(sun_lights.empty());

  auto fill_light = fill.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(fill_light.has_value());
  EXPECT_FALSE(fill_light->get().IsSunLight());

  auto sun_light = named_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(sun_light.has_value());
  EXPECT_FALSE(sun_light->get().IsSunLight());
  EXPECT_FALSE(sun_light->get().GetEnvironmentContribution());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedPreservesAuthoredSunTaggedDirectionalLight)
{
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .force_environment_override = false,
  });
  for (const bool enabled : { true, false }) {
    SCOPED_TRACE(enabled);
    auto scene = MakeScene("DemoShell.PreserveTaggedSun");
    auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
    ASSERT_TRUE(authored_sun.IsAlive());
    auto light = authored_sun.GetLightAs<scene::DirectionalLight>();
    ASSERT_TRUE(light.has_value());
    light->get().Common().affects_world = enabled;
    light->get().Common().color_rgb = { 0.25F, 0.5F, 0.75F };
    light->get().SetIntensityLux(4321.0F);
    light->get().SetAngularSizeRadians(0.02F);
    light->get().SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
    light->get().SetUsePerPixelAtmosphereTransmittance(true);
    auto flags = authored_sun.GetFlags();
    ASSERT_TRUE(flags.has_value());
    flags->get().SetLocalValue(scene::SceneNodeFlags::kCastsShadows, false);
    // Flag writes become effective during the native scene update.
    scene->Update(false);
    ASSERT_FALSE(
      flags->get().GetEffectiveValue(scene::SceneNodeFlags::kCastsShadows));

    service_.OnSceneActivated(*scene);

    auto sun_lights = CollectSunLights(*scene);
    ASSERT_EQ(sun_lights.size(), 1U);
    EXPECT_EQ(sun_lights.front().GetName(), "AuthoredSun");
    EXPECT_TRUE(service_.GetSunLightAvailable());
    EXPECT_EQ(service_.GetSunEnabled(), enabled);
    EXPECT_EQ(service_.GetSunColorRgb(), glm::vec3(0.25F, 0.5F, 0.75F));
    EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), 4321.0F);
    EXPECT_EQ(light->get().Common().affects_world, enabled);
    EXPECT_FALSE(light->get().Common().casts_shadows);
    EXPECT_FALSE(
      flags->get().GetEffectiveValue(scene::SceneNodeFlags::kCastsShadows));
    EXPECT_TRUE(light->get().IsSunLight());
    EXPECT_TRUE(light->get().GetEnvironmentContribution());
    EXPECT_EQ(light->get().GetAtmosphereLightSlot(),
      scene::AtmosphereLightSlot::kPrimary);
    EXPECT_TRUE(light->get().GetUsePerPixelAtmosphereTransmittance());
    EXPECT_FLOAT_EQ(light->get().GetIntensityLux(), 4321.0F);
    EXPECT_FLOAT_EQ(light->get().GetAngularSizeRadians(), 0.02F);
    EXPECT_FALSE(service_.HasPendingChanges());
  }
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedDoesNotRetagExplicitAtmospherePrimaryAsSun)
{
  auto scene = MakeScene("DemoShell.ExplicitAtmospherePrimary");
  auto primary = CreateDirectionalLightNode(*scene, "Primary", false);
  auto light = primary.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  light->get().SetEnvironmentContribution(true);
  light->get().SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);

  service_.OnSceneActivated(*scene);

  EXPECT_TRUE(service_.GetSunLightAvailable());
  EXPECT_TRUE(service_.GetSunEnabled());
  EXPECT_FALSE(light->get().IsSunLight());
  EXPECT_TRUE(light->get().GetEnvironmentContribution());
  EXPECT_FALSE(light->get().Common().casts_shadows);
  EXPECT_EQ(light->get().GetAtmosphereLightSlot(),
    scene::AtmosphereLightSlot::kPrimary);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SceneSwapBindsDisabledSunDirectionWithoutEnvironmentOrStaleLightState)
{
  ResetDemoSettings();
  auto first_scene = MakeScene("DemoShell.FirstSunScene");
  auto first_sun = CreateDirectionalLightNode(*first_scene, "FirstSun", true);
  auto first_light = first_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(first_light.has_value());
  first_light->get().SetIntensityLux(4321.0F);
  service_.OnSceneActivated(*first_scene);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { first_scene.get() },
    .force_environment_override = false,
  });
  EXPECT_NEAR(service_.GetSunAzimuthDeg(), 90.0F, 0.001F);

  auto second_scene = MakeScene("DemoShell.DisabledSunScene");
  auto parent = second_scene->CreateNode("RotatedParent");
  const auto rotation = glm::angleAxis(
    std::numbers::pi_v<float> * 0.5F, glm::vec3(0.0F, 0.0F, 1.0F));
  ASSERT_TRUE(parent.GetTransform().SetLocalRotation(rotation));
  auto second_sun = second_scene->CreateChildNode(parent, "DisabledSun");
  ASSERT_TRUE(second_sun.has_value());
  ASSERT_TRUE(
    second_sun->AttachLight(std::make_unique<scene::DirectionalLight>()));
  auto second_light = second_sun->GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(second_light.has_value());
  second_light->get().SetIsSunLight(true);
  second_light->get().SetEnvironmentContribution(true);
  second_light->get().Common().affects_world = false;
  second_light->get().Common().casts_shadows = false;
  second_light->get().SetIntensityLux(8765.0F);

  // No transform update or environment block precedes this activation.
  service_.OnSceneActivated(*second_scene);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { second_scene.get() },
    .force_environment_override = false,
  });
  ASSERT_TRUE(service_.GetSunLightAvailable());
  EXPECT_FALSE(service_.GetSunEnabled());
  EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), 8765.0F);
  EXPECT_NEAR(service_.GetSunAzimuthDeg(), 180.0F, 0.001F);
  EXPECT_NEAR(service_.GetSunElevationDeg(), 0.0F, 0.001F);
  EXPECT_FALSE(second_scene->GetEnvironment());
  EXPECT_FALSE(second_light->get().Common().affects_world);
  EXPECT_FALSE(second_light->get().Common().casts_shadows);

  service_.SetSunEnabled(true);
  service_.ApplyPendingChanges();
  EXPECT_TRUE(second_light->get().Common().affects_world);
  EXPECT_FLOAT_EQ(second_light->get().GetIntensityLux(), 8765.0F);
  const auto local_rotation = second_sun->GetTransform().GetLocalRotation();
  ASSERT_TRUE(local_rotation.has_value());
  EXPECT_NEAR(
    std::abs(glm::dot(*local_rotation, glm::quat(1.0F, 0.0F, 0.0F, 0.0F))),
    1.0F, 0.0001F);
  EXPECT_TRUE(first_light->get().Common().affects_world);
  EXPECT_FALSE(first_light->get().Common().casts_shadows);
  EXPECT_FLOAT_EQ(first_light->get().GetIntensityLux(), 4321.0F);

  auto empty_scene = MakeScene("DemoShell.NoSunScene");
  service_.OnSceneActivated(*empty_scene);
  EXPECT_FALSE(service_.GetSunLightAvailable());
  EXPECT_FALSE(service_.GetSunEnabled());
  EXPECT_FALSE(service_.HasPendingChanges());
  service_.SetSunIlluminanceLx(125.0F);
  service_.ApplyPendingChanges();
  EXPECT_TRUE(CollectSunLights(*empty_scene).empty());
  EXPECT_FLOAT_EQ(second_light->get().GetIntensityLux(), 8765.0F);
  EXPECT_FLOAT_EQ(first_light->get().GetIntensityLux(), 4321.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  ForcedCustomSunStateSurvivesActivationAndSceneSwapWithoutBindingMutations)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  settings->SetFloat("environment_preset_index", -1.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetBool("env.sun.enabled", true);
  settings->SetFloat("env.sun.illuminance_lx", 16000.0F);
  settings->SetFloat("env.sun.azimuth_deg", 35.0F);
  settings->SetFloat("env.sun.elevation_deg", 12.0F);
  settings->SetFloat("env.sun.source_angle_deg", 0.85F);
  settings->Save();
  settings->Load();

  // DemoShell can announce the scene before its first runtime config update.
  auto first_scene = MakeScene("DemoShell.ForcedCustomFirstScene");
  auto first_sun = CreateDirectionalLightNode(*first_scene, "FirstSun", true);
  auto first_light = first_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(first_light.has_value());
  first_light->get().Common().affects_world = false;
  first_light->get().SetIntensityLux(4321.0F);
  service_.OnSceneActivated(*first_scene);
  EXPECT_FALSE(first_light->get().Common().affects_world);
  EXPECT_FALSE(first_light->get().Common().casts_shadows);
  EXPECT_FLOAT_EQ(first_light->get().GetIntensityLux(), 4321.0F);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { first_scene.get() },
    .force_environment_override = true,
  });
  EXPECT_TRUE(service_.GetSunEnabled());
  EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), 16000.0F);
  EXPECT_FLOAT_EQ(service_.GetSunAzimuthDeg(), 35.0F);
  EXPECT_FLOAT_EQ(service_.GetSunElevationDeg(), 12.0F);
  EXPECT_FLOAT_EQ(service_.GetSunSourceAngleDeg(), 0.85F);
  service_.ApplyPendingChanges();
  EXPECT_TRUE(first_light->get().Common().affects_world);
  EXPECT_FLOAT_EQ(first_light->get().GetIntensityLux(), 16000.0F);

  // A pending UI edit remains the requested override when the scene changes.
  service_.SetSunIlluminanceLx(23000.0F);
  service_.SetSunAzimuthDeg(75.0F);
  service_.SetSunElevationDeg(25.0F);
  service_.SetSunColorRgb({ 0.2F, 0.4F, 0.6F });
  service_.SetSunSourceAngleDeg(1.1F);
  auto second_scene = MakeScene("DemoShell.ForcedCustomSecondScene");
  auto second_sun
    = CreateDirectionalLightNode(*second_scene, "SecondSun", true);
  auto second_light = second_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(second_light.has_value());
  second_light->get().Common().affects_world = false;
  second_light->get().SetIntensityLux(8765.0F);

  service_.OnSceneActivated(*second_scene);
  EXPECT_FALSE(second_light->get().Common().affects_world);
  EXPECT_FALSE(second_light->get().Common().casts_shadows);
  EXPECT_TRUE(second_light->get().IsSunLight());
  EXPECT_TRUE(second_light->get().GetEnvironmentContribution());
  EXPECT_FLOAT_EQ(second_light->get().GetIntensityLux(), 8765.0F);
  EXPECT_TRUE(service_.GetSunEnabled());
  EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), 23000.0F);
  EXPECT_FLOAT_EQ(service_.GetSunAzimuthDeg(), 75.0F);
  EXPECT_FLOAT_EQ(service_.GetSunElevationDeg(), 25.0F);
  EXPECT_EQ(service_.GetSunColorRgb(), glm::vec3(0.2F, 0.4F, 0.6F));
  EXPECT_FLOAT_EQ(service_.GetSunSourceAngleDeg(), 1.1F);

  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { second_scene.get() },
    .force_environment_override = true,
  });
  service_.ApplyPendingChanges();
  EXPECT_TRUE(second_light->get().Common().affects_world);
  EXPECT_FLOAT_EQ(second_light->get().GetIntensityLux(), 23000.0F);
  EXPECT_EQ(
    second_light->get().Common().color_rgb, glm::vec3(0.2F, 0.4F, 0.6F));
  EXPECT_FLOAT_EQ(first_light->get().GetIntensityLux(), 16000.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  DirectionalOnlySceneResyncPublishesUpdatedSunAndEpoch)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.DirectionalOnlyResync");
  auto sun = CreateDirectionalLightNode(*scene, "Sun", true);
  auto light = sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  light->get().SetIntensityLux(4321.0F);
  service_.OnSceneActivated(*scene);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
  });
  const auto previous_epoch = service_.GetEpoch();
  light->get().SetIntensityLux(8765.0F);

  service_.RequestResync();
  service_.SyncFromSceneIfNeeded();

  EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), 8765.0F);
  EXPECT_GT(service_.GetEpoch(), previous_epoch);
  EXPECT_FALSE(service_.HasPendingChanges());
  EXPECT_FALSE(scene->GetEnvironment());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedDoesNotInjectSyntheticSunIntoCameraOnlyScene)
{
  auto scene = MakeScene("DemoShell.CameraOnlyFallback");
  auto camera = scene->CreateNode("MainCamera");
  ASSERT_TRUE(camera.IsAlive());
  ASSERT_TRUE(
    camera.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));

  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.CameraOnly", loguru::Verbosity_9);

  service_.OnSceneActivated(*scene);

  auto sun_lights = CollectSunLights(*scene);
  EXPECT_TRUE(sun_lights.empty());
  EXPECT_FALSE(FindNodeByName(*scene, "Synthetic Sun").has_value());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedWarnsWhenNoResolvedSceneSunExistsInNonEmptyScene)
{
  auto scene = MakeScene("DemoShell.NonEmptySceneWithoutSun");
  auto marker = scene->CreateNode("Marker");
  ASSERT_TRUE(marker.IsAlive());

  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.NonEmpty", loguru::Verbosity_9);

  service_.OnSceneActivated(*scene);

  EXPECT_TRUE(capture.Contains("no resolved sun directional light"));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  OnSceneActivatedRejectsSceneWithMultipleTaggedSuns)
{
  auto scene = MakeScene("DemoShell.InvalidMultiSun");
  ASSERT_TRUE(CreateDirectionalLightNode(*scene, "SunA", true).IsAlive());
  ASSERT_TRUE(CreateDirectionalLightNode(*scene, "SunB", true).IsAlive());

  oxygen::testing::ScopedLogCapture capture(
    "EnvironmentSettingsService.MultiSun", loguru::Verbosity_ERROR);

  EXPECT_THROW(
    service_.OnSceneActivated(*scene), scene::DirectionalLightContractError);
  EXPECT_TRUE(capture.Contains("more than one directional light"));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SceneOnlySunControlsDoNotCreateSyntheticFallback)
{
  auto scene = MakeScene("DemoShell.SyntheticOverride");
  auto authored_sun = CreateDirectionalLightNode(*scene, "SUN", true);
  ASSERT_TRUE(authored_sun.IsAlive());

  service_.OnSceneActivated(*scene);
  service_.ApplyPendingChanges();

  auto authored_light = authored_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(authored_light.has_value());
  EXPECT_TRUE(authored_light->get().IsSunLight());
  EXPECT_TRUE(authored_light->get().Common().affects_world);
  EXPECT_FALSE(authored_light->get().Common().casts_shadows);
  EXPECT_TRUE(authored_light->get().GetEnvironmentContribution());

  auto sun_lights = CollectSunLights(*scene);
  ASSERT_EQ(sun_lights.size(), 1U);
  EXPECT_EQ(sun_lights.front().GetName(), "SUN");
  EXPECT_FALSE(FindNodeByName(*scene, "Synthetic Sun").has_value());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  DisableThenEnablePreservesAuthoredSceneSunIdentity)
{
  auto scene = MakeScene("DemoShell.DisableEnableSceneSun");
  auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
  ASSERT_TRUE(authored_sun.IsAlive());

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

  service_.RequestResync();
  service_.SyncFromSceneIfNeeded();
  EXPECT_TRUE(service_.GetSunLightAvailable());
  EXPECT_FALSE(service_.GetSunEnabled());

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
  SkyboxServiceCanFeedSkyLightWithoutEnablingVisualSkySphere)
{
  auto scene = MakeScene("DemoShell.SkyboxSkylightOnly");
  auto env = std::make_unique<scene::SceneEnvironment>();
  auto& sky = env->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(false);
  auto& sky_light = env->AddSystem<scene::environment::SkyLight>();
  sky_light.SetEnabled(true);
  sky_light.SetSource(scene::environment::SkyLightSource::kSpecifiedCubemap);
  scene->SetEnvironment(std::move(env));

  SkyboxService skybox_service(nullptr, observer_ptr { scene.get() });
  const auto key = content::ResourceKey { 42U };
  skybox_service.SetSkyboxResourceKey(key);
  skybox_service.ApplyToScene(SkyboxService::SkyLightParams {
    .enable_sky_sphere = false,
    .enable_sky_light = true,
    .intensity_mul = 11.0F,
    .diffuse_intensity = 2.0F,
    .specular_intensity = 0.5F,
    .tint_rgb = { 0.7F, 0.8F, 0.9F },
  });

  auto updated_env = scene->GetEnvironment();
  ASSERT_NE(updated_env.get(), nullptr);
  auto updated_sky = updated_env->TryGetSystem<scene::environment::SkySphere>();
  ASSERT_NE(updated_sky.get(), nullptr);
  EXPECT_FALSE(updated_sky->IsEnabled());

  auto updated_sky_light
    = updated_env->TryGetSystem<scene::environment::SkyLight>();
  ASSERT_NE(updated_sky_light.get(), nullptr);
  EXPECT_TRUE(updated_sky_light->IsEnabled());
  EXPECT_EQ(updated_sky_light->GetSource(),
    scene::environment::SkyLightSource::kSpecifiedCubemap);
  EXPECT_EQ(updated_sky_light->GetCubemapResource(), key);
  EXPECT_FLOAT_EQ(updated_sky_light->GetIntensityMul(), 11.0F);
  EXPECT_FLOAT_EQ(updated_sky_light->GetDiffuseIntensity(), 2.0F);
  EXPECT_FLOAT_EQ(updated_sky_light->GetSpecularIntensity(), 0.5F);
  EXPECT_EQ(updated_sky_light->GetTintRgb(), glm::vec3(0.7F, 0.8F, 0.9F));
}

NOLINT_TEST_F(
  EnvironmentSettingsServiceTest, EnablingSkyAtmosphereDisablesSceneSkySphere)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.AtmosphereDisablesSkySphere");
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sky = environment->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  sky.SetSolidColorRgb({ 0.1F, 0.2F, 0.3F });
  sky.SetIntensity(100.0F);
  scene->SetEnvironment(std::move(environment));

  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
  });
  ASSERT_TRUE(service_.GetSkySphereEnabled());

  service_.SetSkyAtmosphereEnabled(true);
  service_.ApplyPendingChanges();

  auto env = scene->GetEnvironment();
  ASSERT_NE(env.get(), nullptr);
  auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>();
  ASSERT_NE(atmo.get(), nullptr);
  EXPECT_TRUE(atmo->IsEnabled());

  auto updated_sky = env->TryGetSystem<scene::environment::SkySphere>();
  ASSERT_NE(updated_sky.get(), nullptr);
  EXPECT_FALSE(updated_sky->IsEnabled());
  EXPECT_FALSE(service_.GetSkySphereEnabled());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  AppliesDirectionalShadowSettingsToAuthoredSceneSunLight)
{
  auto scene = MakeScene("DemoShell.SceneSunShadowSettings");
  auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
  ASSERT_TRUE(authored_sun.IsAlive());

  service_.OnSceneActivated(*scene);

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
  AppliesAtmosphereLightMetadataToAuthoredSceneSunLight)
{
  auto scene = MakeScene("DemoShell.SceneSunAtmosphereLightMetadata");
  auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
  ASSERT_TRUE(authored_sun.IsAlive());

  service_.OnSceneActivated(*scene);

  service_.SetSunAtmosphereLightSlot(
    static_cast<int>(oxygen::scene::AtmosphereLightSlot::kSecondary));
  service_.SetSunUsePerPixelAtmosphereTransmittance(true);
  service_.SetSunAtmosphereDiskLuminanceScale({ 1.2F, 0.9F, 0.8F, 0.5F });
  service_.ApplyPendingChanges();

  auto light = authored_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  EXPECT_EQ(light->get().GetAtmosphereLightSlot(),
    oxygen::scene::AtmosphereLightSlot::kSecondary);
  EXPECT_TRUE(light->get().GetUsePerPixelAtmosphereTransmittance());
  EXPECT_EQ(light->get().GetAtmosphereDiskLuminanceScale(),
    glm::vec4(1.2F, 0.9F, 0.8F, 0.5F));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  LoadSettings_ReadsLegacyCsmKeysAfterNamespaceMigration)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);

  settings->SetFloat("environment_preset_index", -1.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetFloat("env.sun.csm.cascade_count", 3.0F);
  settings->SetFloat("env.sun.csm.split_mode", 1.0F);
  settings->SetFloat("env.sun.csm.max_shadow_distance", 275.0F);
  settings->SetFloat("env.sun.csm.distribution_exponent", 4.0F);
  settings->SetFloat("env.sun.csm.transition_fraction", 0.2F);
  settings->SetFloat("env.sun.csm.distance_fadeout_fraction", 0.3F);
  settings->SetFloat("env.sun.csm.cascade_distance.0", 12.0F);
  settings->SetFloat("env.sun.csm.cascade_distance.1", 36.0F);
  settings->SetFloat("env.sun.csm.cascade_distance.2", 92.0F);

  auto scene = MakeScene("DemoShell.LegacyCsmSettings");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  EXPECT_EQ(service_.GetSunShadowCascadeCount(), 3);
  EXPECT_EQ(service_.GetSunShadowSplitMode(),
    static_cast<int>(scene::DirectionalCsmSplitMode::kManualDistances));
  EXPECT_FLOAT_EQ(service_.GetSunShadowMaxDistance(), 275.0F);
  EXPECT_FLOAT_EQ(service_.GetSunShadowDistributionExponent(), 4.0F);
  EXPECT_FLOAT_EQ(service_.GetSunShadowTransitionFraction(), 0.2F);
  EXPECT_FLOAT_EQ(service_.GetSunShadowDistanceFadeoutFraction(), 0.3F);
  EXPECT_FLOAT_EQ(service_.GetSunShadowCascadeDistance(0), 12.0F);
  EXPECT_FLOAT_EQ(service_.GetSunShadowCascadeDistance(1), 36.0F);
  EXPECT_FLOAT_EQ(service_.GetSunShadowCascadeDistance(2), 92.0F);
}

NOLINT_TEST_F(
  EnvironmentSettingsServiceTest, AppliesSunSourceAngleToAuthoredSceneSunLight)
{
  auto scene = MakeScene("DemoShell.SceneSunSourceAngle");
  auto authored_sun = CreateDirectionalLightNode(*scene, "AuthoredSun", true);
  ASSERT_TRUE(authored_sun.IsAlive());

  service_.OnSceneActivated(*scene);
  service_.SetSunSourceAngleDeg(0.9F);
  service_.ApplyPendingChanges();

  auto light = authored_sun.GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(light.has_value());
  EXPECT_NEAR(light->get().GetAngularSizeRadians(),
    0.9F * (std::numbers::pi_v<float> / 180.0F), 1.0e-6F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  LoadSettings_ReadsSunAtmosphereLightMetadataKeys)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);

  settings->SetFloat("environment_preset_index", -1.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetFloat("env.settings.schema_version", 4.0F);
  settings->SetFloat("env.sun.atmosphere_light_slot",
    static_cast<float>(oxygen::scene::AtmosphereLightSlot::kSecondary));
  settings->SetBool("env.sun.per_pixel_atmosphere_transmittance", true);
  settings->SetFloat("env.sun.atmosphere_disk_luminance_scale.x", 1.4F);
  settings->SetFloat("env.sun.atmosphere_disk_luminance_scale.y", 1.1F);
  settings->SetFloat("env.sun.atmosphere_disk_luminance_scale.z", 0.7F);
  settings->SetFloat("env.sun.atmosphere_disk_luminance_scale.w", 0.25F);

  auto scene = MakeScene("DemoShell.LoadSunAtmosphereLightMetadata");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  EXPECT_EQ(service_.GetSunAtmosphereLightSlot(),
    static_cast<int>(oxygen::scene::AtmosphereLightSlot::kSecondary));
  EXPECT_TRUE(service_.GetSunUsePerPixelAtmosphereTransmittance());
  EXPECT_EQ(service_.GetSunAtmosphereDiskLuminanceScale(),
    glm::vec4(1.4F, 1.1F, 0.7F, 0.25F));
}

NOLINT_TEST_F(
  EnvironmentSettingsServiceTest, LoadSettings_ReadsSunSourceAngleKey)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);

  settings->SetFloat("environment_preset_index", -1.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetFloat("env.settings.schema_version", 5.0F);
  settings->SetFloat("env.sun.source_angle_deg", 0.85F);

  auto scene = MakeScene("DemoShell.LoadSunSourceAngle");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  EXPECT_FLOAT_EQ(service_.GetSunSourceAngleDeg(), 0.85F);
}

NOLINT_TEST_F(
  EnvironmentSettingsServiceTest, SaveSettings_DoesNotPersistLegacySunSourceKey)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.SaveSettingsWithoutLegacySunSource");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });
  service_.ActivateCustomMode();
  service_.SetSunAzimuthDeg(35.0F);
  service_.SetSunElevationDeg(12.0F);

  PersistPendingSettings(service_);

  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  EXPECT_FALSE(settings->GetFloat("env.sun.source").has_value());
  EXPECT_EQ(
    settings->GetFloat("env.settings.schema_version").value_or(0.0F), 5.0F);
}

NOLINT_TEST_F(
  EnvironmentSettingsServiceTest, SaveSettings_PersistsSunSourceAngleKey)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.SaveSunSourceAngle");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });
  service_.ActivateCustomMode();
  service_.SetSunSourceAngleDeg(1.1F);

  PersistPendingSettings(service_);

  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  ASSERT_TRUE(settings->GetFloat("env.sun.source_angle_deg").has_value());
  EXPECT_FLOAT_EQ(settings->GetFloat("env.sun.source_angle_deg").value(), 1.1F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  LoadSettings_RemovesLegacySunSourceKeyDuringMigration)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);

  settings->SetFloat("environment_preset_index", -1.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetFloat("env.settings.schema_version", 4.0F);
  settings->SetFloat("env.sun.source", 1.0F);
  settings->SetFloat("env.sun.azimuth_deg", 80.0F);
  settings->SetFloat("env.sun.elevation_deg", 10.0F);
  settings->Save();
  settings->Load();

  auto scene = MakeScene("DemoShell.RemoveLegacySunSource");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  PersistPendingSettings(service_);

  EXPECT_FALSE(settings->GetFloat("env.sun.source").has_value());
  EXPECT_FLOAT_EQ(service_.GetSunAzimuthDeg(), 80.0F);
  EXPECT_FLOAT_EQ(service_.GetSunElevationDeg(), 10.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  HeightFogPanelFacingServiceApiAppliesCorrectlyToSceneFogMetadata)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.HeightFogMetadata");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  service_.SetFogEnabled(true);
  service_.SetFogModel(
    static_cast<int>(scene::environment::FogModel::kVolumetric));
  service_.SetFogExtinctionSigmaTPerMeter(0.5F);
  service_.SetFogHeightFalloffPerMeter(1.25F);
  service_.SetFogHeightOffsetMeters(18.0F);
  service_.SetFogStartDistanceMeters(96.0F);
  service_.SetFogMaxOpacity(0.55F);
  service_.SetFogSingleScatteringAlbedoRgb({ 0.2F, 0.3F, 0.4F });

  EXPECT_TRUE(service_.GetFogEnabled());
  EXPECT_EQ(service_.GetFogModel(),
    static_cast<int>(scene::environment::FogModel::kVolumetric));
  EXPECT_FLOAT_EQ(service_.GetFogExtinctionSigmaTPerMeter(), 0.5F);
  EXPECT_FLOAT_EQ(service_.GetFogHeightFalloffPerMeter(), 1.25F);
  EXPECT_FLOAT_EQ(service_.GetFogHeightOffsetMeters(), 18.0F);
  EXPECT_FLOAT_EQ(service_.GetFogStartDistanceMeters(), 96.0F);
  EXPECT_FLOAT_EQ(service_.GetFogMaxOpacity(), 0.55F);
  EXPECT_EQ(
    service_.GetFogSingleScatteringAlbedoRgb(), glm::vec3(0.2F, 0.3F, 0.4F));

  service_.ApplyPendingChanges();

  auto* updated_fog
    = scene->GetEnvironment()->TryGetSystem<scene::environment::Fog>().get();
  ASSERT_NE(updated_fog, nullptr);
  EXPECT_TRUE(updated_fog->IsEnabled());
  EXPECT_EQ(updated_fog->GetModel(), scene::environment::FogModel::kVolumetric);
  EXPECT_FLOAT_EQ(updated_fog->GetExtinctionSigmaTPerMeter(), 0.5F);
  EXPECT_FLOAT_EQ(updated_fog->GetHeightFalloffPerMeter(), 1.25F);
  EXPECT_FLOAT_EQ(updated_fog->GetHeightOffsetMeters(), 18.0F);
  EXPECT_FLOAT_EQ(updated_fog->GetStartDistanceMeters(), 96.0F);
  EXPECT_FLOAT_EQ(updated_fog->GetMaxOpacity(), 0.55F);
  EXPECT_EQ(
    updated_fog->GetSingleScatteringAlbedoRgb(), glm::vec3(0.2F, 0.3F, 0.4F));
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  WidenedFogMetadataAppliesThroughPanelFacingServiceApi)
{
  auto scene = MakeScene("DemoShell.WidenedFogMetadata");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  service_.SetFogEnabled(true);
  service_.SetFogModel(
    static_cast<int>(scene::environment::FogModel::kVolumetric));
  service_.SetSecondFogDensity(0.02F);
  service_.SetSecondFogHeightFalloff(0.03F);
  service_.SetSecondFogHeightOffset(4.0F);
  service_.SetFogInscatteringLuminance({ 0.3F, 0.4F, 0.5F });
  service_.SetSkyAtmosphereAmbientContributionColorScale({ 0.6F, 0.7F, 0.8F });
  service_.SetInscatteringColorCubemapAngle(25.0F);
  service_.SetInscatteringTextureTint({ 0.9F, 0.8F, 0.7F });
  service_.SetFullyDirectionalInscatteringColorDistance(1000.0F);
  service_.SetNonDirectionalInscatteringColorDistance(2000.0F);
  service_.SetDirectionalInscatteringLuminance({ 0.4F, 0.5F, 0.6F });
  service_.SetDirectionalInscatteringExponent(12.0F);
  service_.SetDirectionalInscatteringStartDistance(32.0F);
  service_.SetFogEndDistanceMeters(500.0F);
  service_.SetFogCutoffDistanceMeters(1200.0F);
  service_.SetVolumetricFogScatteringDistribution(0.25F);
  service_.SetVolumetricFogAlbedo({ 0.7F, 0.8F, 0.9F });
  service_.SetVolumetricFogEmissive({ 0.1F, 0.2F, 0.3F });
  service_.SetVolumetricFogExtinctionScale(1.5F);
  service_.SetVolumetricFogDistanceMeters(6000.0F);
  service_.SetVolumetricFogStartDistanceMeters(100.0F);
  service_.SetVolumetricFogNearFadeInDistanceMeters(25.0F);
  service_.SetVolumetricFogStaticLightingScatteringIntensity(0.5F);
  service_.SetOverrideLightColorsWithFogInscatteringColors(true);
  service_.SetFogHoldout(true);
  service_.SetFogRenderInMainPass(false);
  service_.SetFogVisibleInReflectionCaptures(false);
  service_.SetFogVisibleInRealTimeSkyCaptures(false);
  service_.ApplyPendingChanges();

  auto env = scene->GetEnvironment();
  ASSERT_NE(env.get(), nullptr);
  auto fog = env->TryGetSystem<scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  EXPECT_TRUE(fog->GetEnableVolumetricFog());
  EXPECT_FLOAT_EQ(fog->GetSecondFogDensity(), 0.02F);
  EXPECT_FLOAT_EQ(fog->GetSecondFogHeightFalloff(), 0.03F);
  EXPECT_FLOAT_EQ(fog->GetSecondFogHeightOffset(), 4.0F);
  EXPECT_EQ(fog->GetFogInscatteringLuminance(), glm::vec3(0.3F, 0.4F, 0.5F));
  EXPECT_EQ(fog->GetSkyAtmosphereAmbientContributionColorScale(),
    glm::vec3(0.6F, 0.7F, 0.8F));
  EXPECT_FLOAT_EQ(fog->GetInscatteringColorCubemapAngle(), 25.0F);
  EXPECT_EQ(fog->GetInscatteringTextureTint(), glm::vec3(0.9F, 0.8F, 0.7F));
  EXPECT_FLOAT_EQ(fog->GetFullyDirectionalInscatteringColorDistance(), 1000.0F);
  EXPECT_FLOAT_EQ(fog->GetNonDirectionalInscatteringColorDistance(), 2000.0F);
  EXPECT_EQ(
    fog->GetDirectionalInscatteringLuminance(), glm::vec3(0.4F, 0.5F, 0.6F));
  EXPECT_FLOAT_EQ(fog->GetDirectionalInscatteringExponent(), 12.0F);
  EXPECT_FLOAT_EQ(fog->GetDirectionalInscatteringStartDistance(), 32.0F);
  EXPECT_FLOAT_EQ(fog->GetEndDistanceMeters(), 500.0F);
  EXPECT_FLOAT_EQ(fog->GetFogCutoffDistanceMeters(), 1200.0F);
  EXPECT_FLOAT_EQ(fog->GetVolumetricFogScatteringDistribution(), 0.25F);
  EXPECT_EQ(fog->GetVolumetricFogAlbedo(), glm::vec3(0.7F, 0.8F, 0.9F));
  EXPECT_EQ(fog->GetVolumetricFogEmissive(), glm::vec3(0.1F, 0.2F, 0.3F));
  EXPECT_FLOAT_EQ(fog->GetVolumetricFogExtinctionScale(), 1.5F);
  EXPECT_FLOAT_EQ(fog->GetVolumetricFogDistance(), 6000.0F);
  EXPECT_FLOAT_EQ(fog->GetVolumetricFogStartDistance(), 100.0F);
  EXPECT_FLOAT_EQ(fog->GetVolumetricFogNearFadeInDistance(), 25.0F);
  EXPECT_FLOAT_EQ(
    fog->GetVolumetricFogStaticLightingScatteringIntensity(), 0.5F);
  EXPECT_TRUE(fog->GetOverrideLightColorsWithFogInscatteringColors());
  EXPECT_TRUE(fog->GetHoldout());
  EXPECT_FALSE(fog->GetRenderInMainPass());
  EXPECT_FALSE(fog->GetVisibleInReflectionCaptures());
  EXPECT_FALSE(fog->GetVisibleInRealTimeSkyCaptures());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  HeightFogVisualVerificationSettingsPersistSupportedControls)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.HeightFogVisualPersistence");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  service_.SetFogModel(
    static_cast<int>(scene::environment::FogModel::kExponentialHeight));
  service_.SetFogEnabled(true);
  service_.SetFogExtinctionSigmaTPerMeter(0.125F);
  service_.SetFogHeightFalloffPerMeter(0.031F);
  service_.SetFogHeightOffsetMeters(-14.0F);
  service_.SetFogStartDistanceMeters(40.0F);
  service_.SetSecondFogDensity(0.018F);
  service_.SetSecondFogHeightFalloff(0.044F);
  service_.SetSecondFogHeightOffset(9.0F);
  service_.SetFogMaxOpacity(0.72F);
  service_.SetFogInscatteringLuminance({ 0.21F, 0.32F, 0.43F });
  service_.SetSkyAtmosphereAmbientContributionColorScale({ 0.6F, 0.7F, 0.8F });
  service_.SetDirectionalInscatteringLuminance({ 1.4F, 1.2F, 0.9F });
  service_.SetDirectionalInscatteringExponent(15.0F);
  service_.SetDirectionalInscatteringStartDistance(75.0F);
  service_.SetFogEndDistanceMeters(800.0F);
  service_.SetFogCutoffDistanceMeters(1800.0F);
  service_.SetFogHoldout(true);
  service_.SetFogRenderInMainPass(false);
  service_.SetFogVisibleInReflectionCaptures(false);
  service_.SetFogVisibleInRealTimeSkyCaptures(false);

  EXPECT_FALSE(service_.GetHeightFogPassRequested());
  service_.SetFogRenderInMainPass(true);
  EXPECT_TRUE(service_.GetHeightFogPassRequested());

  PersistPendingSettings(service_);

  EnvironmentSettingsService loaded {};
  auto loaded_scene = MakeScene("DemoShell.HeightFogVisualPersistence.Loaded");
  loaded.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { loaded_scene.get() },
  });

  EXPECT_TRUE(loaded.GetFogEnabled());
  EXPECT_TRUE(loaded.GetHeightFogPassRequested());
  EXPECT_EQ(loaded.GetFogModel(),
    static_cast<int>(scene::environment::FogModel::kExponentialHeight));
  EXPECT_FLOAT_EQ(loaded.GetFogExtinctionSigmaTPerMeter(), 0.125F);
  EXPECT_FLOAT_EQ(loaded.GetFogHeightFalloffPerMeter(), 0.031F);
  EXPECT_FLOAT_EQ(loaded.GetFogHeightOffsetMeters(), -14.0F);
  EXPECT_FLOAT_EQ(loaded.GetFogStartDistanceMeters(), 40.0F);
  EXPECT_FLOAT_EQ(loaded.GetSecondFogDensity(), 0.018F);
  EXPECT_FLOAT_EQ(loaded.GetSecondFogHeightFalloff(), 0.044F);
  EXPECT_FLOAT_EQ(loaded.GetSecondFogHeightOffset(), 9.0F);
  EXPECT_FLOAT_EQ(loaded.GetFogMaxOpacity(), 0.72F);
  EXPECT_EQ(
    loaded.GetFogInscatteringLuminance(), glm::vec3(0.21F, 0.32F, 0.43F));
  EXPECT_EQ(loaded.GetSkyAtmosphereAmbientContributionColorScale(),
    glm::vec3(0.6F, 0.7F, 0.8F));
  EXPECT_EQ(
    loaded.GetDirectionalInscatteringLuminance(), glm::vec3(1.4F, 1.2F, 0.9F));
  EXPECT_FLOAT_EQ(loaded.GetDirectionalInscatteringExponent(), 15.0F);
  EXPECT_FLOAT_EQ(loaded.GetDirectionalInscatteringStartDistance(), 75.0F);
  EXPECT_FLOAT_EQ(loaded.GetFogEndDistanceMeters(), 800.0F);
  EXPECT_FLOAT_EQ(loaded.GetFogCutoffDistanceMeters(), 1800.0F);
  EXPECT_TRUE(loaded.GetFogHoldout());
  EXPECT_TRUE(loaded.GetFogRenderInMainPass());
  EXPECT_FALSE(loaded.GetFogVisibleInReflectionCaptures());
  EXPECT_FALSE(loaded.GetFogVisibleInRealTimeSkyCaptures());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  AtmosphereAndSkyLightWidenedMetadataApplyThroughPanelFacingServiceApi)
{
  auto scene = MakeScene("DemoShell.AtmosphereSkyLightMetadata");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  service_.SetSkyAtmosphereEnabled(true);
  service_.SetSkyAtmosphereTransformMode(2);
  service_.SetPlanetRadiusKm(7000.0F);
  service_.SetAtmosphereHeightKm(120.0F);
  service_.SetSkyLuminanceFactor({ 1.1F, 1.2F, 1.3F });
  service_.SetSkyAndAerialPerspectiveLuminanceFactor({ 0.8F, 0.9F, 1.0F });
  service_.SetAerialPerspectiveScale(750.0F);
  service_.SetAerialPerspectiveStartDepthMeters(0.0F);
  service_.SetAerialScatteringStrength(3.5F);
  service_.SetHeightFogContribution(0.4F);
  service_.SetTraceSampleCountScale(2.0F);
  service_.SetTransmittanceMinLightElevationDeg(-8.0F);
  service_.SetAtmosphereHoldout(true);
  service_.SetAtmosphereRenderInMainPass(false);

  service_.SetSkyLightEnabled(true);
  service_.SetSkyLightSource(1);
  service_.SetSkyLightIntensityMul(1.5F);
  service_.SetSkyLightDiffuse(0.7F);
  service_.SetSkyLightSpecular(0.9F);
  service_.SetSkyLightRealTimeCaptureEnabled(true);
  service_.SetSkyLightLowerHemisphereColor({ 0.1F, 0.2F, 0.3F });
  service_.SetSkyLightVolumetricScatteringIntensity(0.6F);
  service_.SetSkyLightAffectReflections(false);
  service_.ApplyPendingChanges();

  auto env = scene->GetEnvironment();
  ASSERT_NE(env.get(), nullptr);

  auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>();
  ASSERT_NE(atmo.get(), nullptr);
  EXPECT_EQ(atmo->GetTransformMode(),
    scene::environment::SkyAtmosphereTransformMode::
      kPlanetCenterAtComponentTransform);
  EXPECT_FLOAT_EQ(atmo->GetPlanetRadiusMeters(), 7000000.0F);
  EXPECT_FLOAT_EQ(atmo->GetAtmosphereHeightMeters(), 120000.0F);
  EXPECT_EQ(atmo->GetSkyLuminanceFactorRgb(), glm::vec3(1.1F, 1.2F, 1.3F));
  EXPECT_EQ(atmo->GetSkyAndAerialPerspectiveLuminanceFactorRgb(),
    glm::vec3(0.8F, 0.9F, 1.0F));
  EXPECT_FLOAT_EQ(atmo->GetAerialPerspectiveDistanceScale(), 750.0F);
  EXPECT_FLOAT_EQ(atmo->GetAerialPerspectiveStartDepthMeters(), 0.0F);
  EXPECT_FLOAT_EQ(atmo->GetAerialScatteringStrength(), 3.5F);
  EXPECT_FLOAT_EQ(atmo->GetHeightFogContribution(), 0.4F);
  EXPECT_FLOAT_EQ(atmo->GetTraceSampleCountScale(), 2.0F);
  EXPECT_FLOAT_EQ(atmo->GetTransmittanceMinLightElevationDeg(), -8.0F);
  EXPECT_TRUE(atmo->GetHoldout());
  EXPECT_FALSE(atmo->GetRenderInMainPass());

  auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
  ASSERT_NE(sky_light.get(), nullptr);
  EXPECT_EQ(sky_light->GetSource(),
    scene::environment::SkyLightSource::kSpecifiedCubemap);
  EXPECT_FLOAT_EQ(sky_light->GetIntensityMul(), 1.5F);
  EXPECT_FLOAT_EQ(sky_light->GetDiffuseIntensity(), 0.7F);
  EXPECT_FLOAT_EQ(sky_light->GetSpecularIntensity(), 0.9F);
  EXPECT_TRUE(sky_light->GetRealTimeCaptureEnabled());
  EXPECT_EQ(sky_light->GetLowerHemisphereColor(), glm::vec3(0.1F, 0.2F, 0.3F));
  EXPECT_FLOAT_EQ(sky_light->GetVolumetricScatteringIntensity(), 0.6F);
  EXPECT_FALSE(sky_light->GetAffectReflections());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  AerialPerspectiveVisualVerificationSettingsPersistSupportedControls)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.AerialPerspectiveVisualPersistence");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  service_.SetSkyAtmosphereEnabled(true);
  service_.SetSkyAndAerialPerspectiveLuminanceFactor({ 1.2F, 1.1F, 1.0F });
  service_.SetAerialPerspectiveScale(1000.0F);
  service_.SetAerialPerspectiveStartDepthMeters(0.0F);
  service_.SetAerialScatteringStrength(2.5F);
  service_.SetHeightFogContribution(0.75F);
  service_.SetTraceSampleCountScale(1.5F);
  service_.SetTransmittanceMinLightElevationDeg(-5.0F);
  service_.SetAtmosphereRenderInMainPass(true);

  EXPECT_TRUE(service_.GetAerialPerspectiveEnabled());
  service_.SetAerialPerspectiveEnabled(false);
  EXPECT_FALSE(service_.GetAerialPerspectiveEnabled());
  EXPECT_FLOAT_EQ(service_.GetAerialScatteringStrength(), 0.0F);

  PersistPendingSettings(service_);

  EnvironmentSettingsService loaded {};
  auto loaded_scene
    = MakeScene("DemoShell.AerialPerspectiveVisualPersistence.Loaded");
  loaded.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { loaded_scene.get() },
  });

  EXPECT_TRUE(loaded.GetSkyAtmosphereEnabled());
  EXPECT_EQ(loaded.GetSkyAndAerialPerspectiveLuminanceFactor(),
    glm::vec3(1.2F, 1.1F, 1.0F));
  EXPECT_FLOAT_EQ(loaded.GetAerialPerspectiveScale(), 1000.0F);
  EXPECT_FLOAT_EQ(loaded.GetAerialPerspectiveStartDepthMeters(), 0.0F);
  EXPECT_FALSE(loaded.GetAerialPerspectiveEnabled());
  EXPECT_FLOAT_EQ(loaded.GetAerialScatteringStrength(), 0.0F);
  EXPECT_FLOAT_EQ(loaded.GetHeightFogContribution(), 0.75F);
  EXPECT_FLOAT_EQ(loaded.GetTraceSampleCountScale(), 1.5F);
  EXPECT_FLOAT_EQ(loaded.GetTransmittanceMinLightElevationDeg(), -5.0F);
  EXPECT_TRUE(loaded.GetAtmosphereRenderInMainPass());

  loaded.SetAerialPerspectiveEnabled(true);
  EXPECT_TRUE(loaded.GetAerialPerspectiveEnabled());
  EXPECT_FLOAT_EQ(loaded.GetAerialScatteringStrength(), 1.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SceneAuthoredEnvironmentModeSyncsAtmosphereFogAndLocalFogRequests)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.SceneAuthoredEnvironmentMode");

  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& atmosphere
    = environment->AddSystem<scene::environment::SkyAtmosphere>();
  atmosphere.SetEnabled(true);
  atmosphere.SetAerialPerspectiveDistanceScale(700.0F);
  atmosphere.SetAerialPerspectiveStartDepthMeters(40.0F);
  atmosphere.SetAerialScatteringStrength(1.0F);

  auto& fog = environment->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetModel(scene::environment::FogModel::kVolumetric);
  fog.SetRenderInMainPass(true);
  fog.SetExtinctionSigmaTPerMeter(0.00028F);
  fog.SetHeightFalloffPerMeter(0.02F);
  scene->SetEnvironment(std::move(environment));

  auto local_fog_node = scene->CreateNode("SceneLocalFog");
  const auto impl_opt = local_fog_node.GetImpl();
  ASSERT_TRUE(impl_opt.has_value());
  impl_opt->get().AddComponent<scene::environment::LocalFogVolume>();
  auto& local_fog
    = impl_opt->get().GetComponent<scene::environment::LocalFogVolume>();
  local_fog.SetEnabled(true);
  local_fog.SetRadialFogExtinction(0.02F);

  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
  });

  EXPECT_FALSE(service_.HasPendingChanges());
  EXPECT_FLOAT_EQ(service_.GetAerialPerspectiveScale(), 700.0F);
  EXPECT_FLOAT_EQ(service_.GetAerialPerspectiveStartDepthMeters(), 40.0F);
  EXPECT_EQ(service_.GetFogModel(),
    static_cast<int>(scene::environment::FogModel::kVolumetric));
  EXPECT_FLOAT_EQ(service_.GetFogExtinctionSigmaTPerMeter(), 0.00028F);
  EXPECT_FLOAT_EQ(service_.GetFogHeightFalloffPerMeter(), 0.02F);
  EXPECT_TRUE(service_.GetHeightFogPassRequested());
  EXPECT_TRUE(service_.GetLocalFogPassRequested());
  EXPECT_EQ(service_.GetLocalFogVolumeCount(), 1);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SceneAuthoredModePreservesDefaultSceneLightingEnvironment)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.DefaultSceneLightingEnvironment");
  const auto sun_node = EnsureDefaultSceneLighting(*scene);
  ASSERT_TRUE(sun_node.IsAlive());

  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
  });

  EXPECT_FALSE(service_.HasPendingChanges());
  EXPECT_TRUE(service_.GetSkyAtmosphereEnabled());
  EXPECT_TRUE(service_.GetSkyLightEnabled());
  EXPECT_TRUE(service_.GetSunPresent());
  EXPECT_TRUE(service_.GetSunEnabled());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SceneAuthoredModeIgnoresPersistedCustomSunOnStartup)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  settings->SetFloat("environment_preset_index", -1.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetFloat("env.sun.atmosphere_light_slot",
    static_cast<float>(scene::AtmosphereLightSlot::kSecondary));
  settings->SetFloat("env.sun.illuminance_lx", 120000.0F);
  settings->Save();
  settings->Load();

  auto scene = MakeScene("DemoShell.SceneAuthoredIgnoresCustomStartup");
  const auto sun_node = EnsureDefaultSceneLighting(*scene);
  ASSERT_TRUE(sun_node.IsAlive());

  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
  });

  EXPECT_EQ(service_.GetPresetIndex(), -2);
  EXPECT_FALSE(service_.HasPendingChanges());
  EXPECT_EQ(service_.GetSunAtmosphereLightSlot(),
    static_cast<int>(scene::AtmosphereLightSlot::kPrimary));
  EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), 100000.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SceneAuthoredSunEditSwitchesToCustomBeforeApplying)
{
  ResetDemoSettings();
  auto scene = MakeScene("DemoShell.SceneAuthoredSunManualOverride");
  const auto sun_node = EnsureDefaultSceneLighting(*scene);
  ASSERT_TRUE(sun_node.IsAlive());

  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
  });
  service_.SetPresetIndex(-2);
  service_.ActivateUseSceneMode();
  ASSERT_EQ(service_.GetPresetIndex(), -2);
  ASSERT_FALSE(service_.HasPendingChanges());

  auto vm = ui::EnvironmentVm(
    observer_ptr<EnvironmentSettingsService> { &service_ }, nullptr, nullptr);
  vm.SetSunAzimuthDeg(123.0F);

  EXPECT_EQ(service_.GetPresetIndex(), -1);
  EXPECT_TRUE(service_.HasPendingChanges());
  EXPECT_FLOAT_EQ(service_.GetSunAzimuthDeg(), 123.0F);

  service_.ApplyPendingChanges();

  EXPECT_FALSE(service_.HasPendingChanges());
  EXPECT_FLOAT_EQ(service_.GetSunAzimuthDeg(), 123.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  AddedLocalFogVolumeStartsFromSceneComponentDefaults)
{
  auto scene = MakeScene("DemoShell.LocalFogDefaults");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  service_.AddLocalFogVolume();

  EXPECT_EQ(service_.GetLocalFogVolumeCount(), 1);
  EXPECT_TRUE(service_.GetSelectedLocalFogVolumeEnabled());
  EXPECT_FLOAT_EQ(
    service_.GetSelectedLocalFogVolumeRadialFogExtinction(), 1.0F);
  EXPECT_FLOAT_EQ(
    service_.GetSelectedLocalFogVolumeHeightFogExtinction(), 1.0F);
  EXPECT_FLOAT_EQ(
    service_.GetSelectedLocalFogVolumeHeightFogFalloff(), 1000.0F);
  EXPECT_FLOAT_EQ(service_.GetSelectedLocalFogVolumeHeightFogOffset(), 0.0F);
  EXPECT_FLOAT_EQ(service_.GetSelectedLocalFogVolumeFogPhaseG(), 0.2F);
  EXPECT_EQ(
    service_.GetSelectedLocalFogVolumeFogAlbedo(), glm::vec3(1.0F, 1.0F, 1.0F));
  EXPECT_EQ(service_.GetSelectedLocalFogVolumeFogEmissive(),
    glm::vec3(0.0F, 0.0F, 0.0F));
  EXPECT_EQ(service_.GetSelectedLocalFogVolumeSortPriority(), 0);

  service_.ApplyPendingChanges();

  auto fog_nodes = CollectLocalFogVolumeNodes(*scene);
  ASSERT_EQ(fog_nodes.size(), 1U);
  const auto impl_opt = fog_nodes.front().GetImpl();
  ASSERT_TRUE(impl_opt.has_value());
  const auto& local_fog
    = impl_opt->get().GetComponent<scene::environment::LocalFogVolume>();
  EXPECT_TRUE(local_fog.IsEnabled());
  EXPECT_FLOAT_EQ(local_fog.GetRadialFogExtinction(), 1.0F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogExtinction(), 1.0F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogFalloff(), 1000.0F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogOffset(), 0.0F);
  EXPECT_FLOAT_EQ(local_fog.GetFogPhaseG(), 0.2F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  LocalFogVolumesAreCreatedEditedAndRemovedAsSceneNodeComponents)
{
  auto scene = MakeScene("DemoShell.LocalFogVolumes");
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
  });

  service_.AddLocalFogVolume();
  EXPECT_EQ(service_.GetLocalFogVolumeCount(), 1);
  EXPECT_EQ(service_.GetSelectedLocalFogVolumeIndex(), 0);

  service_.SetSelectedLocalFogVolumeEnabled(true);
  service_.SetSelectedLocalFogVolumeRadialFogExtinction(0.3F);
  service_.SetSelectedLocalFogVolumeHeightFogExtinction(0.2F);
  service_.SetSelectedLocalFogVolumeHeightFogFalloff(0.15F);
  service_.SetSelectedLocalFogVolumeHeightFogOffset(1.25F);
  service_.SetSelectedLocalFogVolumeFogPhaseG(0.4F);
  service_.SetSelectedLocalFogVolumeFogAlbedo({ 0.7F, 0.8F, 0.9F });
  service_.SetSelectedLocalFogVolumeFogEmissive({ 0.1F, 0.2F, 0.3F });
  service_.SetSelectedLocalFogVolumeSortPriority(2);
  service_.ApplyPendingChanges();

  auto fog_nodes = CollectLocalFogVolumeNodes(*scene);
  ASSERT_EQ(fog_nodes.size(), 1U);

  const auto impl_opt = fog_nodes.front().GetImpl();
  ASSERT_TRUE(impl_opt.has_value());
  const auto& local_fog
    = impl_opt->get().GetComponent<scene::environment::LocalFogVolume>();
  EXPECT_TRUE(local_fog.IsEnabled());
  EXPECT_FLOAT_EQ(local_fog.GetRadialFogExtinction(), 0.3F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogExtinction(), 0.2F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogFalloff(), 0.15F);
  EXPECT_FLOAT_EQ(local_fog.GetHeightFogOffset(), 1.25F);
  EXPECT_FLOAT_EQ(local_fog.GetFogPhaseG(), 0.4F);
  EXPECT_EQ(local_fog.GetFogAlbedo(), glm::vec3(0.7F, 0.8F, 0.9F));
  EXPECT_EQ(local_fog.GetFogEmissive(), glm::vec3(0.1F, 0.2F, 0.3F));
  EXPECT_EQ(local_fog.GetSortPriority(), 2);

  service_.RemoveSelectedLocalFogVolume();
  service_.ApplyPendingChanges();

  EXPECT_EQ(service_.GetLocalFogVolumeCount(), 0);
  EXPECT_TRUE(CollectLocalFogVolumeNodes(*scene).empty());
}

NOLINT_TEST_F(
  EnvironmentSettingsServiceTest, HydratesCompletePostProcessAndBackground)
{
  namespace world = data::pak::world;
  auto descriptor = world::SceneAssetDesc {};
  descriptor.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
  descriptor.header.version = world::kSceneAssetVersion;
  auto post = world::PostProcessVolumeEnvironmentRecord {};
  post.tone_mapper = engine::ToneMapper::kReinhard;
  post.exposure_mode = engine::ExposureMode::kManualCamera;
  post.auto_exposure_metering_mode = engine::MeteringMode::kSpot;
  post.exposure_enabled = 0;
  post.exposure_compensation_ev = 1.25F;
  post.exposure_key = 8.75F;
  post.manual_exposure_ev = 11.5F;
  post.auto_exposure_min_ev = -3.25F;
  post.auto_exposure_max_ev = 12.75F;
  post.auto_exposure_speed_up = 5.5F;
  post.auto_exposure_speed_down = 1.75F;
  post.auto_exposure_low_percentile = 0.2F;
  post.auto_exposure_high_percentile = 0.85F;
  post.auto_exposure_min_log_luminance = -10.5F;
  post.auto_exposure_log_luminance_range = 21.25F;
  post.auto_exposure_target_luminance = 0.27F;
  post.auto_exposure_spot_meter_radius = 0.35F;
  post.bloom_intensity = 0.6F;
  post.bloom_threshold = 2.25F;
  post.saturation = 0.8F;
  post.contrast = 1.4F;
  post.vignette_intensity = 0.3F;
  post.display_gamma = 2.4F;
  auto background = world::BackgroundEnvironmentRecord {};
  background.color_rgb[0] = 0.05F;
  background.color_rgb[1] = 0.25F;
  background.color_rgb[2] = 0.75F;
  const auto block = world::SceneEnvironmentBlockHeader {
    .byte_size = sizeof(world::SceneEnvironmentBlockHeader) + sizeof(post)
      + sizeof(background),
    .systems_count = 2,
  };
  auto bytes = std::vector<std::byte> {};
  const auto append = [&bytes]<typename T>(const T& value) {
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  };
  append(descriptor);
  append(block);
  append(post);
  append(background);
  const auto asset = data::SceneAsset(data::AssetKey {}, std::move(bytes));
  auto environment = scene::SceneEnvironment {};
  EnvironmentSettingsService::HydrateEnvironment(environment, asset);
  const auto volume
    = environment.TryGetSystem<scene::environment::PostProcessVolume>();
  ASSERT_TRUE(volume);
  EXPECT_EQ(volume->GetToneMapper(), engine::ToneMapper::kReinhard);
  EXPECT_EQ(volume->GetExposureMode(), engine::ExposureMode::kManualCamera);
  EXPECT_EQ(volume->GetAutoExposureMeteringMode(), engine::MeteringMode::kSpot);
  EXPECT_FALSE(volume->GetExposureEnabled());
  EXPECT_FLOAT_EQ(volume->GetExposureCompensationEv(), 1.25F);
  EXPECT_FLOAT_EQ(volume->GetExposureKey(), 8.75F);
  EXPECT_FLOAT_EQ(volume->GetManualExposureEv(), 11.5F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureMinEv(), -3.25F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureMaxEv(), 12.75F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureSpeedUp(), 5.5F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureSpeedDown(), 1.75F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureLowPercentile(), 0.2F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureHighPercentile(), 0.85F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureMinLogLuminance(), -10.5F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureLogLuminanceRange(), 21.25F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureTargetLuminance(), 0.27F);
  EXPECT_FLOAT_EQ(volume->GetAutoExposureSpotMeterRadius(), 0.35F);
  EXPECT_FLOAT_EQ(volume->GetBloomIntensity(), 0.6F);
  EXPECT_FLOAT_EQ(volume->GetBloomThreshold(), 2.25F);
  EXPECT_FLOAT_EQ(volume->GetSaturation(), 0.8F);
  EXPECT_FLOAT_EQ(volume->GetContrast(), 1.4F);
  EXPECT_FLOAT_EQ(volume->GetVignetteIntensity(), 0.3F);
  EXPECT_FLOAT_EQ(volume->GetDisplayGamma(), 2.4F);
  const auto backdrop
    = environment.TryGetSystem<scene::environment::Background>();
  ASSERT_TRUE(backdrop);
  EXPECT_EQ(backdrop->GetColorRgb(), Vec3(0.05F, 0.25F, 0.75F));
  EXPECT_FALSE(environment.TryGetSystem<scene::environment::SkyAtmosphere>());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SceneProfileIsPureAndPreviewIsIndependentAndReversible)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  settings->SetFloat("environment_preset_index", -2.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetBool("env.atmo.enabled", true);
  settings->SetBool("env.sky_sphere.enabled", true);
  auto scene = MakeScene("Preview.SceneOnly");
  auto candidate = CreateDirectionalLightNode(*scene, "Candidate", false);
  candidate.GetLightAs<scene::DirectionalLight>()->get().SetIntensityLux(
    1234.0F);
  service_.OnSceneActivated(*scene);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_preview_sun_enabled = false,
  });
  service_.ApplyPendingChanges();
  EXPECT_EQ(service_.GetPresetIndex(), -2);
  EXPECT_FALSE(service_.IsPreviewSunActive());
  EXPECT_FALSE(scene->GetEnvironment());
  EXPECT_FALSE(
    candidate.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());

  service_.SetPreviewSunEnabled(true);
  service_.ApplyPendingChanges();
  EXPECT_EQ(service_.GetPresetIndex(), -2);
  EXPECT_TRUE(service_.IsPreviewSunActive());
  EXPECT_EQ(service_.GetSunSourceDescription(),
    "Preview sun: Candidate (scene directional)");
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
  EXPECT_FALSE(scene->GetEnvironment());
  EXPECT_FLOAT_EQ(
    candidate.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    1234.0F);
  EXPECT_TRUE(
    candidate.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());

  service_.SetPreviewSunEnabled(false);
  service_.ApplyPendingChanges();
  EXPECT_EQ(service_.GetPresetIndex(), -2);
  EXPECT_FALSE(service_.IsPreviewSunActive());
  EXPECT_FALSE(
    candidate.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
  EXPECT_FLOAT_EQ(
    candidate.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    1234.0F);
  EXPECT_FALSE(scene->GetEnvironment());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  RenderSceneCustomRestoresAtStartupAndUseSceneRestoresAuthoredState)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  settings->SetFloat("environment_preset_index", -1.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetBool("env.atmo.enabled", true);
  settings->SetBool("env.sun.enabled", true);
  settings->SetFloat("env.sun.illuminance_lx", 100000.0F);
  settings->SetFloat("env.sun.shadow.bias", 0.03F);
  auto scene = MakeScene("Preview.Custom");
  auto candidate = CreateDirectionalLightNode(*scene, "Candidate", false);
  candidate.GetLightAs<scene::DirectionalLight>()->get().SetIntensityLux(
    1234.0F);
  service_.OnSceneActivated(*scene);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_preview_sun_enabled = true,
  });
  service_.ApplyPendingChanges();
  EXPECT_EQ(service_.GetPresetIndex(), -1);
  EXPECT_TRUE(service_.IsPreviewSunActive());
  ASSERT_TRUE(scene->GetEnvironment());
  const auto atmosphere = scene->GetEnvironment()
                            ->TryGetSystem<scene::environment::SkyAtmosphere>();
  ASSERT_TRUE(atmosphere);
  EXPECT_TRUE(atmosphere->IsEnabled());
  EXPECT_FLOAT_EQ(
    candidate.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    100000.0F);

  service_.ActivateUseSceneMode();
  service_.ApplyPendingChanges();
  EXPECT_EQ(service_.GetPresetIndex(), -2);
  EXPECT_TRUE(service_.IsPreviewSunActive());
  EXPECT_FALSE(scene->GetEnvironment());
  EXPECT_FLOAT_EQ(
    candidate.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    1234.0F);
  EXPECT_TRUE(
    settings->GetBool("env.settings.custom_state_present").value_or(false));

  service_.RestoreCustomMode();
  service_.ApplyPendingChanges();
  EXPECT_EQ(service_.GetPresetIndex(), -1);
  ASSERT_TRUE(scene->GetEnvironment());
  EXPECT_TRUE(scene->GetEnvironment()
      ->TryGetSystem<scene::environment::SkyAtmosphere>()
      ->IsEnabled());
  EXPECT_FLOAT_EQ(
    candidate.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    100000.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  PreviewAndProfileWaitForLoadedSceneAndDefaultToUseScene)
{
  ResetDemoSettings();
  auto placeholder = MakeScene("Placeholder");
  service_.OnSceneActivated(*placeholder);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { placeholder.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_preview_sun_enabled = true,
    .preview_scene_ready = false,
  });
  service_.ApplyPendingChanges();
  EXPECT_EQ(service_.GetPresetIndex(), -2);
  EXPECT_TRUE(placeholder->GetRootNodes().empty());
  EXPECT_FALSE(placeholder->GetEnvironment());

  auto loaded = MakeScene("Loaded");
  service_.OnSceneActivated(*loaded);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { loaded.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_preview_sun_enabled = true,
    .preview_scene_ready = true,
  });
  service_.ApplyPendingChanges();
  EXPECT_TRUE(service_.IsPreviewSunActive());
  EXPECT_EQ(
    service_.GetSunSourceDescription(), "Preview sun: Preview Sun (created)");
  EXPECT_EQ(loaded->GetRootNodes().size(), 1U);
  EXPECT_FALSE(loaded->GetEnvironment());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  StartupProfileOverrideRemainsTransientUntilExplicitUserEdit)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  settings->SetFloat("environment_preset_index", -2.0F);
  auto scene = MakeScene("Transient");
  service_.OnSceneActivated(*scene);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_environment_profile = -1,
    .initial_preview_sun_enabled = false,
  });
  service_.SetSkyAtmosphereEnabled(true);
  service_.ApplyPendingChanges();
  service_.SetPresetIndex(
    0); // Flush pending persistence for the prior profile.
  EXPECT_FLOAT_EQ(
    settings->GetFloat("environment_preset_index").value(), -2.0F);
  service_.SetProfilePersistenceEnabled(true);
  service_.ActivateCustomMode();
  service_.SetSunIlluminanceLx(4321.0F);
  service_.ActivateUseSceneMode();
  EXPECT_FLOAT_EQ(
    settings->GetFloat("env.sun.illuminance_lx").value(), 4321.0F);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  SelectingCustomWithoutEditingAFieldPersistsTheSelection)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  settings->SetFloat("environment_preset_index", -2.0F);
  auto scene = MakeScene("CleanCustomSelection");
  service_.OnSceneActivated(*scene);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
  });
  PersistPendingSettings(service_);
  service_.RestoreCustomMode();
  PersistPendingSettings(service_);
  EXPECT_FLOAT_EQ(
    settings->GetFloat("environment_preset_index").value(), -1.0F);
  EXPECT_TRUE(
    settings->GetBool("env.settings.custom_state_present").value_or(false));
  EnvironmentSettingsService reopened;
  reopened.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
  });
  EXPECT_EQ(reopened.GetPresetIndex(), -1);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  EveryProfileKeepsTheSamePreviewDirectionalAndPrimaryRole)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  settings->SetFloat("environment_preset_index", -2.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetBool("env.atmo.enabled", true);
  settings->SetBool("env.sun.enabled", true);
  settings->SetFloat("env.sun.atmosphere_light_slot", 0.0F);
  auto scene = MakeScene("SinglePreviewSource");
  auto candidate = CreateDirectionalLightNode(*scene, "OnlyDirectional", false);
  const auto original_handle = candidate.GetHandle();
  candidate.GetFlags()->get().SetLocalValue(
    scene::SceneNodeFlags::kCastsShadows, false);
  scene->Update(false);
  service_.OnSceneActivated(*scene);
  ui::EnvironmentVm vm { observer_ptr { &service_ }, nullptr, nullptr };
  vm.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_preview_sun_enabled = true,
  });
  service_.ApplyPendingChanges();
  for (const int profile : { 1, 2, 3, 4, 5, 6, 0, 1 }) {
    SCOPED_TRACE(profile);
    vm.ApplyPreset(profile);
    service_.ApplyPendingChanges();
    ASSERT_EQ(scene->GetRootNodes().size(), 1U);
    EXPECT_EQ(scene->GetRootNodes().front().GetHandle(), original_handle);
    EXPECT_EQ(service_.GetSunSourceDescription(),
      "Preview sun: OnlyDirectional (scene directional)");
    EXPECT_TRUE(service_.IsPreviewSunActive());
    const auto light = candidate.GetLightAs<scene::DirectionalLight>();
    ASSERT_TRUE(light.has_value());
    EXPECT_TRUE(light->get().IsSunLight());
    EXPECT_EQ(light->get().GetAtmosphereLightSlot(),
      scene::AtmosphereLightSlot::kPrimary);
    EXPECT_EQ(service_.GetSunAtmosphereLightSlot(),
      static_cast<int>(scene::AtmosphereLightSlot::kPrimary));
    const auto primary
      = scene->GetDirectionalLightResolver().ResolvePrimarySun();
    ASSERT_TRUE(primary.has_value());
    EXPECT_EQ(primary->NodeHandle(), original_handle);
    EXPECT_FALSE(candidate.GetFlags()->get().GetEffectiveValue(
      scene::SceneNodeFlags::kCastsShadows));
  }
  service_.ActivateUseSceneMode();
  service_.SetPreviewSunEnabled(false);
  service_.ApplyPendingChanges();
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
  EXPECT_EQ(candidate.GetHandle(), original_handle);
  EXPECT_FALSE(
    candidate.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
  EXPECT_EQ(candidate.GetLightAs<scene::DirectionalLight>()
              ->get()
              .GetAtmosphereLightSlot(),
    scene::AtmosphereLightSlot::kNone);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  PublicationBeforeRuntimeConfigPreservesProfileAndPreviewOwnership)
{
  ResetDemoSettings();
  const auto settings = SettingsService::ForDemoApp();
  settings->SetFloat("environment_preset_index", -1.0F);
  settings->SetBool("env.settings.custom_state_present", true);
  settings->SetBool("env.atmo.enabled", true);
  settings->SetBool("env.sun.enabled", true);
  settings->SetFloat("env.sun.illuminance_lx", 3456.0F);
  auto placeholder = MakeScene("Placeholder");
  service_.OnSceneActivated(*placeholder);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { placeholder.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_preview_sun_enabled = true,
    .preview_scene_ready = false,
  });
  service_.ApplyPendingChanges();
  auto loaded = MakeScene("Loaded");
  auto candidate
    = CreateDirectionalLightNode(*loaded, "OnlyDirectional", false);
  candidate.GetLightAs<scene::DirectionalLight>()->get().SetIntensityLux(
    1234.0F);
  service_.OnSceneActivated(*loaded);
  // The real shell runs domain frame-start before refreshing runtime config.
  engine::FrameContext frame;
  service_.OnFrameStart(frame);
  service_.SyncFromSceneIfNeeded();
  EXPECT_FALSE(service_.IsPreviewSunActive());
  EXPECT_FALSE(
    candidate.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
  EXPECT_TRUE(service_.GetSkyAtmosphereEnabled());
  EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), 3456.0F);
  const EnvironmentRuntimeConfig config {
    .scene = observer_ptr { loaded.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_preview_sun_enabled = true,
    .preview_scene_ready = true,
  };
  service_.SetRuntimeConfig(config);
  service_.OnFrameStart(frame);
  service_.SetRuntimeConfig(config);
  service_.OnFrameStart(frame);
  EXPECT_TRUE(service_.IsPreviewSunActive());
  EXPECT_TRUE(service_.CanEnablePreviewSun());
  EXPECT_EQ(loaded->GetRootNodes().size(), 1U);
  EXPECT_EQ(service_.GetSunSourceDescription(),
    "Preview sun: OnlyDirectional (scene directional)");
  EXPECT_FLOAT_EQ(
    candidate.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    3456.0F);
  ASSERT_TRUE(loaded->GetEnvironment());
  EXPECT_TRUE(loaded->GetEnvironment()
      ->TryGetSystem<scene::environment::SkyAtmosphere>()
      ->IsEnabled());
  service_.ActivateUseSceneMode();
  service_.SetPreviewSunEnabled(false);
  service_.ApplyPendingChanges();
  EXPECT_FALSE(loaded->GetEnvironment());
  EXPECT_FLOAT_EQ(
    candidate.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    1234.0F);
  EXPECT_FALSE(
    candidate.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  NativeLightMutationImmediatelyYieldsPreviewWithoutChangingProfile)
{
  for (const bool reuse : { false, true }) {
    for (const int profile : { -2, -1, 0 }) {
      SCOPED_TRACE(reuse);
      SCOPED_TRACE(profile);
      ResetDemoSettings();
      const auto settings = SettingsService::ForDemoApp();
      settings->SetFloat(
        "environment_preset_index", static_cast<float>(profile));
      settings->SetBool("env.settings.custom_state_present", true);
      settings->SetBool("env.sun.enabled", true);
      auto scene = MakeScene("LateAuthoredSun");
      if (reuse) {
        CreateDirectionalLightNode(*scene, "Candidate", false);
      }
      EnvironmentSettingsService service;
      service.OnSceneActivated(*scene);
      service.SetRuntimeConfig(EnvironmentRuntimeConfig {
        .scene = observer_ptr { scene.get() },
        .force_environment_override = false,
        .restore_environment_profile = true,
        .initial_preview_sun_enabled = true,
      });
      service.ApplyPendingChanges();
      ASSERT_TRUE(service.IsPreviewSunActive());
      const auto preview
        = FindNodeByName(*scene, reuse ? "Candidate" : "Preview Sun");
      ASSERT_TRUE(preview);
      auto authored
        = CreateDirectionalLightNode(*scene, "Late Authored Sun", true);
      authored.GetLightAs<scene::DirectionalLight>()
        ->get()
        .SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
      const auto sync_count = scene->GetMutationDispatchCounters().sync_calls;
      scene->SyncObservers();

      EXPECT_EQ(
        scene->GetMutationDispatchCounters().sync_calls, sync_count + 1U);
      EXPECT_EQ(service.GetPresetIndex(), profile);
      EXPECT_FALSE(service.IsPreviewSunActive());
      EXPECT_FALSE(service.CanEnablePreviewSun());
      EXPECT_TRUE(preview->IsAlive()); // Topology is unchanged during dispatch.
      const auto primary
        = scene->GetDirectionalLightResolver().ResolvePrimarySun();
      ASSERT_TRUE(primary);
      EXPECT_EQ(primary->NodeHandle(), authored.GetHandle());

      service.ApplyPendingChanges();
      EXPECT_EQ(preview->IsAlive(), reuse);
      EXPECT_EQ(
        service.GetSunSourceDescription(), "Scene sun: Late Authored Sun");
      EXPECT_EQ(service.GetPresetIndex(), profile);
      ASSERT_TRUE(scene->DestroyNode(authored));
      scene->SyncObservers();
      service.ApplyPendingChanges();
      EXPECT_TRUE(service.IsPreviewSunActive());
      EXPECT_EQ(service.GetPresetIndex(), profile);
      EXPECT_EQ(scene->GetRootNodes().size(), 1U);
    }
  }
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  NativeUntaggedDirectionalMutationReplacesInjectionAtNextApply)
{
  ResetDemoSettings();
  auto scene = MakeScene("LateDirectional");
  service_.OnSceneActivated(*scene);
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_preview_sun_enabled = true,
  });
  service_.ApplyPendingChanges();
  ASSERT_EQ(
    service_.GetSunSourceDescription(), "Preview sun: Preview Sun (created)");
  auto candidate = CreateDirectionalLightNode(*scene, "Late Candidate", false);
  scene->SyncObservers();
  EXPECT_EQ(scene->GetRootNodes().size(), 2U);
  service_.ApplyPendingChanges();
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
  EXPECT_EQ(service_.GetSunSourceDescription(),
    "Preview sun: Late Candidate (scene directional)");
  ASSERT_TRUE(
    candidate.ReplaceLight(std::make_unique<scene::DirectionalLight>()));
  scene->SyncObservers();
  service_.ApplyPendingChanges();
  EXPECT_TRUE(service_.IsPreviewSunActive());
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
}

NOLINT_TEST_F(EnvironmentSettingsServiceTest,
  PreviewObserverUnregistersOnRebindAndServiceDestruction)
{
  ResetDemoSettings();
  auto first = MakeScene("OldScene");
  auto second = MakeScene("NewScene");
  {
    EnvironmentSettingsService service;
    service.OnSceneActivated(*first);
    service.SetRuntimeConfig(EnvironmentRuntimeConfig {
      .scene = observer_ptr { first.get() },
      .force_environment_override = false,
      .restore_environment_profile = true,
      .initial_preview_sun_enabled = true,
    });
    service.ApplyPendingChanges();
    service.OnSceneActivated(*second);
    service.SetRuntimeConfig(EnvironmentRuntimeConfig {
      .scene = observer_ptr { second.get() },
      .force_environment_override = false,
      .restore_environment_profile = true,
      .initial_preview_sun_enabled = true,
    });
    service.ApplyPendingChanges();
    auto unrelated = first->CreateNode("Old Scene Mutation");
    ASSERT_TRUE(
      unrelated.AttachLight(std::make_unique<scene::DirectionalLight>()));
    first->SyncObservers();
    EXPECT_TRUE(service.IsPreviewSunActive());
    EXPECT_EQ(second->GetRootNodes().size(), 1U);
  }
  auto after_destroy = second->CreateNode("After Service Destruction");
  ASSERT_TRUE(
    after_destroy.AttachLight(std::make_unique<scene::DirectionalLight>()));
  EXPECT_NO_THROW(second->SyncObservers());
}

} // namespace oxygen::examples::testing
