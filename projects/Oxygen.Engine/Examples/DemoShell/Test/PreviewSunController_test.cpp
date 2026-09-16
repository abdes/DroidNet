//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/PreviewSunController.h"

namespace oxygen::examples::testing {

NOLINT_TEST(
  PreviewSunController, InspectionNamesTheVisibleCandidateBeforeEnabling)
{
  auto scene = std::make_shared<scene::Scene>("InspectCandidate", 16);
  auto hidden = scene->CreateNode("Hidden Directional");
  ASSERT_TRUE(hidden.AttachLight(std::make_unique<scene::DirectionalLight>()));
  hidden.GetFlags()->get().SetFlag(scene::SceneNodeFlags::kVisible,
    scene::SceneFlag {}.SetEffectiveValueBit(false));
  auto visible = scene->CreateNode("PreviewCandidate");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetIntensityLux(1000.0F);
  ASSERT_TRUE(visible.AttachLight(std::move(light)));

  auto controller = PreviewSunController {};
  const auto sources = controller.InspectSources(*scene);
  EXPECT_FALSE(sources.authored_sun.IsAlive());
  EXPECT_EQ(sources.candidate.GetHandle(), visible.GetHandle());
  EXPECT_EQ(sources.candidate.GetName(), "PreviewCandidate");
  EXPECT_FALSE(controller.GetSun().IsAlive());
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kDisabled);
  const auto& unchanged = visible.GetLightAs<scene::DirectionalLight>()->get();
  EXPECT_FALSE(unchanged.IsSunLight());
  EXPECT_FALSE(unchanged.GetEnvironmentContribution());
  EXPECT_FLOAT_EQ(unchanged.GetIntensityLux(), 1000.0F);

  controller.Update(*scene, true);
  EXPECT_EQ(controller.GetSun().GetHandle(), sources.candidate.GetHandle());
  controller.Update(*scene, true, "Hidden Directional");
  const auto borrowed = controller.InspectSources(*scene);
  EXPECT_FALSE(borrowed.authored_sun.IsAlive());
  EXPECT_EQ(borrowed.candidate.GetHandle(), hidden.GetHandle());
}

NOLINT_TEST(
  PreviewSunController, InspectionNamesTheHiddenAuthoredSunBlockingPreview)
{
  auto scene = std::make_shared<scene::Scene>("InspectBlocker", 16);
  auto node = scene->CreateNode("Authored Hidden Sun");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->Common().affects_world = false;
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  ASSERT_TRUE(node.AttachLight(std::move(light)));
  node.GetFlags()->get().SetFlag(scene::SceneNodeFlags::kVisible,
    scene::SceneFlag {}.SetEffectiveValueBit(false));

  const auto controller = PreviewSunController {};
  const auto sources = controller.InspectSources(*scene);
  EXPECT_EQ(sources.authored_sun.GetHandle(), node.GetHandle());
  EXPECT_EQ(sources.authored_sun.GetName(), "Authored Hidden Sun");
  EXPECT_FALSE(controller.CanEnable(*scene));
  const auto& unchanged = node.GetLightAs<scene::DirectionalLight>()->get();
  EXPECT_FALSE(unchanged.Common().affects_world);
  EXPECT_FALSE(unchanged.IsSunLight());
  EXPECT_EQ(
    unchanged.GetAtmosphereLightSlot(), scene::AtmosphereLightSlot::kPrimary);
  EXPECT_FALSE(
    node.GetFlags()->get().GetEffectiveValue(scene::SceneNodeFlags::kVisible));
}

NOLINT_TEST(
  PreviewSunController, InspectionReportsInjectionWithoutCreatingNodes)
{
  auto scene = std::make_shared<scene::Scene>("InspectInjection", 16);
  auto controller = PreviewSunController {};
  const auto sources = controller.InspectSources(*scene);
  EXPECT_FALSE(sources.authored_sun.IsAlive());
  EXPECT_FALSE(sources.candidate.IsAlive());
  EXPECT_TRUE(controller.CanEnable(*scene));
  EXPECT_TRUE(scene->GetRootNodes().empty());
  EXPECT_EQ(scene->GetEnvironment(), nullptr);

  controller.Update(*scene, true);
  const auto injected = controller.InspectSources(*scene);
  EXPECT_FALSE(injected.authored_sun.IsAlive());
  EXPECT_FALSE(injected.candidate.IsAlive());
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
  EXPECT_TRUE(controller.IsInjected());
}

NOLINT_TEST(PreviewSunController, DisabledDoesNotInjectOrPromote)
{
  auto scene = std::make_shared<scene::Scene>("DefaultPreview", 16);
  auto controller = PreviewSunController {};
  controller.Update(*scene, false);
  EXPECT_TRUE(scene->GetRootNodes().empty());
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kDisabled);
  auto node = scene->CreateNode("Directional");
  ASSERT_TRUE(node.AttachLight(std::make_unique<scene::DirectionalLight>()));
  controller.Update(*scene, false);
  EXPECT_FALSE(controller.GetSun().IsAlive());
  EXPECT_FALSE(node.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
}

NOLINT_TEST(PreviewSunController, InjectionIsIdempotentAndUsesOxygenBasis)
{
  auto scene = std::make_shared<scene::Scene>("UnlitImport", 16);
  auto controller = PreviewSunController {};
  ASSERT_TRUE(controller.CanEnable(*scene));
  controller.Update(*scene, true);
  const auto sun = controller.GetSun();
  ASSERT_TRUE(sun.IsAlive());
  EXPECT_EQ(sun.GetName(), "Preview Sun");
  EXPECT_TRUE(controller.IsInjected());
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kInjected);
  EXPECT_TRUE(controller.CanEnable(*scene));
  controller.Update(*scene, true);
  EXPECT_EQ(controller.GetSun().GetHandle(), sun.GetHandle());
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
  EXPECT_EQ(scene->GetEnvironment(), nullptr);

  const auto primary = scene->GetDirectionalLightResolver().ResolvePrimarySun();
  ASSERT_TRUE(primary.has_value());
  EXPECT_EQ(primary->NodeHandle(), sun.GetHandle());
  EXPECT_FLOAT_EQ(primary->Light().GetIntensityLux(), 100000.0F);
  EXPECT_FLOAT_EQ(primary->Light().Common().shadow.bias, 0.03F);
  EXPECT_EQ(primary->Light().GetAtmosphereLightSlot(),
    scene::AtmosphereLightSlot::kPrimary);
  const auto expected_direction
    = glm::normalize(glm::vec3 { -10.0F, 10.0F, 15.0F });
  EXPECT_NEAR(
    glm::dot(primary->DirectionToLightWs(), expected_direction), 1.0F, 0.001F);

  controller.Update(*scene, false);
  EXPECT_FALSE(sun.IsAlive());
  EXPECT_FALSE(controller.GetSun().IsAlive());
  EXPECT_FALSE(controller.IsInjected());
  EXPECT_TRUE(scene->GetRootNodes().empty());
  EXPECT_FALSE(scene->GetDirectionalLightResolver().ResolvePrimarySun());
  controller.Update(*scene, false);
  EXPECT_TRUE(scene->GetRootNodes().empty());
}

NOLINT_TEST(PreviewSunController, AuthoredSunBlocksEvenWhenInactiveOrHidden)
{
  for (const auto state : { 0, 1, 2 }) {
    auto scene = std::make_shared<scene::Scene>("AuthoredSun", 16);
    auto parent = scene->CreateNode("Parent");
    const auto child = scene->CreateChildNode(parent, "Authored Sun");
    ASSERT_TRUE(child.has_value());
    auto node = *child;
    auto light = std::make_unique<scene::DirectionalLight>();
    light->SetIsSunLight(true);
    light->SetEnvironmentContribution(true);
    light->SetIntensityLux(0.0F);
    light->Common().affects_world = state != 1;
    ASSERT_TRUE(node.AttachLight(std::move(light)));
    if (state == 2) {
      node.GetFlags()->get().SetFlag(scene::SceneNodeFlags::kVisible,
        scene::SceneFlag {}.SetEffectiveValueBit(false));
    }
    auto controller = PreviewSunController {};
    EXPECT_FALSE(controller.CanEnable(*scene));
    controller.Update(*scene, true);
    EXPECT_FALSE(controller.GetSun().IsAlive());
    EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kBlockedByAuthoredSun);
    EXPECT_EQ(scene->GetRootNodes().size(), 1U);
    const auto& authored = node.GetLightAs<scene::DirectionalLight>()->get();
    EXPECT_EQ(authored.Common().affects_world, state != 1);
    EXPECT_FLOAT_EQ(authored.GetIntensityLux(), 0.0F);
    EXPECT_TRUE(authored.IsSunLight());
    EXPECT_TRUE(authored.GetEnvironmentContribution());
  }
}

NOLINT_TEST(PreviewSunController, ReusesAndRestoresAnUntaggedDirectionalExactly)
{
  auto scene = std::make_shared<scene::Scene>("AuthoredLighting", 16);
  auto node = scene->CreateNode("Directional");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->Common().affects_world = false;
  light->Common().casts_shadows = false;
  light->Common().shadow.bias = 0.08F;
  light->Common().color_rgb = { 0.2F, 0.4F, 0.6F };
  light->SetEnvironmentContribution(true);
  light->SetIntensityLux(1000.0F);
  ASSERT_TRUE(node.AttachLight(std::move(light)));
  node.GetTransform().SetLocalPosition({ 3.0F, 7.0F, 11.0F });
  node.GetTransform().SetLocalRotation(
    glm::angleAxis(0.3F, glm::vec3 { 0.0F, 0.0F, 1.0F }));
  node.GetTransform().SetLocalScale({ 1.0F, 2.0F, 3.0F });
  const auto position = node.GetTransform().GetLocalPosition();
  const auto rotation = node.GetTransform().GetLocalRotation();
  const auto scale = node.GetTransform().GetLocalScale();
  scene->Update(false);
  scene->SyncObservers();
  EXPECT_FALSE(scene->GetDirectionalLightResolver().ResolvePrimarySun());

  auto controller = PreviewSunController {};
  controller.Update(*scene, true);
  EXPECT_EQ(controller.GetSun().GetHandle(), node.GetHandle());
  EXPECT_FALSE(controller.IsInjected());
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kReusedDirectional);
  controller.Update(*scene, true);
  EXPECT_TRUE(controller.CanEnable(*scene));
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
  const auto primary = scene->GetDirectionalLightResolver().ResolvePrimarySun();
  ASSERT_TRUE(primary.has_value());
  EXPECT_EQ(primary->NodeHandle(), node.GetHandle());
  EXPECT_TRUE(primary->Light().Common().affects_world);
  EXPECT_TRUE(primary->Light().IsSunLight());
  EXPECT_TRUE(primary->Light().GetEnvironmentContribution());
  EXPECT_EQ(primary->Light().GetAtmosphereLightSlot(),
    scene::AtmosphereLightSlot::kPrimary);
  EXPECT_FLOAT_EQ(primary->Light().GetIntensityLux(), 1000.0F);

  controller.Update(*scene, false);
  EXPECT_FALSE(controller.GetSun().IsAlive());
  EXPECT_TRUE(node.IsAlive());
  const auto& restored = node.GetLightAs<scene::DirectionalLight>()->get();
  EXPECT_FALSE(restored.Common().affects_world);
  EXPECT_TRUE(restored.GetEnvironmentContribution());
  EXPECT_FALSE(restored.IsSunLight());
  EXPECT_EQ(
    restored.GetAtmosphereLightSlot(), scene::AtmosphereLightSlot::kNone);
  EXPECT_FLOAT_EQ(restored.GetIntensityLux(), 1000.0F);
  EXPECT_FALSE(restored.Common().casts_shadows);
  EXPECT_FLOAT_EQ(restored.Common().shadow.bias, 0.08F);
  EXPECT_EQ(restored.Common().color_rgb, (glm::vec3 { 0.2F, 0.4F, 0.6F }));
  EXPECT_EQ(node.GetTransform().GetLocalPosition(), position);
  EXPECT_EQ(node.GetTransform().GetLocalRotation(), rotation);
  EXPECT_EQ(node.GetTransform().GetLocalScale(), scale);
  EXPECT_FALSE(scene->GetDirectionalLightResolver().ResolvePrimarySun());
}

NOLINT_TEST(PreviewSunController, ExplicitAtmosphereRolesBlockEvenWhenInactive)
{
  for (const auto slot : { scene::AtmosphereLightSlot::kPrimary,
         scene::AtmosphereLightSlot::kSecondary }) {
    auto scene = std::make_shared<scene::Scene>("AuthoredRole", 16);
    auto node = scene->CreateNode("Role");
    auto light = std::make_unique<scene::DirectionalLight>();
    light->Common().affects_world = false;
    light->SetAtmosphereLightSlot(slot);
    ASSERT_TRUE(node.AttachLight(std::move(light)));
    node.GetFlags()->get().SetFlag(scene::SceneNodeFlags::kVisible,
      scene::SceneFlag {}.SetEffectiveValueBit(false));
    auto controller = PreviewSunController {};
    EXPECT_FALSE(controller.CanEnable(*scene));
    controller.Update(*scene, true);
    EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kBlockedByAuthoredSun);
    EXPECT_FALSE(controller.GetSun().IsAlive());
    EXPECT_EQ(node.GetLightAs<scene::DirectionalLight>()
                ->get()
                .GetAtmosphereLightSlot(),
      slot);
    EXPECT_FALSE(
      node.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
  }
}

NOLINT_TEST(
  PreviewSunController, PrefersVisibleSourceWithoutChangingZeroIntensity)
{
  auto scene = std::make_shared<scene::Scene>("VisibleSource", 16);
  auto hidden = scene->CreateNode("Hidden");
  ASSERT_TRUE(hidden.AttachLight(std::make_unique<scene::DirectionalLight>()));
  hidden.GetFlags()->get().SetFlag(scene::SceneNodeFlags::kVisible,
    scene::SceneFlag {}.SetEffectiveValueBit(false));
  auto visible = scene->CreateNode("Visible");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetIntensityLux(0.0F);
  ASSERT_TRUE(visible.AttachLight(std::move(light)));
  auto controller = PreviewSunController {};
  controller.Update(*scene, true);
  EXPECT_EQ(controller.GetSun().GetHandle(), visible.GetHandle());
  EXPECT_FLOAT_EQ(
    visible.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    0.0F);
  EXPECT_FALSE(
    hidden.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
  controller.Update(*scene, true, "Hidden");
  EXPECT_EQ(controller.GetSun().GetHandle(), hidden.GetHandle());
  EXPECT_FALSE(hidden.GetFlags()->get().GetEffectiveValue(
    scene::SceneNodeFlags::kVisible));
  controller.Update(*scene, false);
  EXPECT_FLOAT_EQ(
    visible.GetLightAs<scene::DirectionalLight>()->get().GetIntensityLux(),
    0.0F);
}

NOLINT_TEST(
  PreviewSunController, SelectsAUniqueNamedSourceAndRestoresPreviousSource)
{
  auto scene = std::make_shared<scene::Scene>("SelectSource", 16);
  auto first = scene->CreateNode("First");
  auto second = scene->CreateNode("Second");
  ASSERT_TRUE(first.AttachLight(std::make_unique<scene::DirectionalLight>()));
  ASSERT_TRUE(second.AttachLight(std::make_unique<scene::DirectionalLight>()));
  auto controller = PreviewSunController {};
  controller.Update(*scene, true);
  EXPECT_EQ(controller.GetSun().GetHandle(), first.GetHandle());
  EXPECT_TRUE(
    second.GetLightAs<scene::DirectionalLight>()->get().Common().affects_world);
  EXPECT_FALSE(
    second.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
  controller.Update(*scene, true, "Second");
  EXPECT_EQ(controller.GetSun().GetHandle(), second.GetHandle());
  EXPECT_FALSE(first.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
  EXPECT_TRUE(
    first.GetLightAs<scene::DirectionalLight>()->get().Common().affects_world);
  controller.Update(*scene, true, "Missing");
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kSourceUnavailable);
  EXPECT_FALSE(controller.GetSun().IsAlive());
  EXPECT_FALSE(
    second.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
  EXPECT_EQ(scene->GetRootNodes().size(), 2U);
  auto duplicate = scene->CreateNode("First");
  ASSERT_TRUE(
    duplicate.AttachLight(std::make_unique<scene::DirectionalLight>()));
  controller.Update(*scene, true, "First");
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kSourceUnavailable);
  EXPECT_FALSE(controller.GetSun().IsAlive());
}

NOLINT_TEST(
  PreviewSunController, NewDirectionalReplacesInjectionWithoutDuplication)
{
  auto scene = std::make_shared<scene::Scene>("IncrementalScene", 16);
  auto controller = PreviewSunController {};
  controller.Update(*scene, true);
  const auto injected = controller.GetSun();
  auto authored = scene->CreateNode("Directional");
  ASSERT_TRUE(
    authored.AttachLight(std::make_unique<scene::DirectionalLight>()));
  controller.Update(*scene, true);
  EXPECT_FALSE(injected.IsAlive());
  EXPECT_EQ(controller.GetSun().GetHandle(), authored.GetHandle());
  EXPECT_FALSE(controller.IsInjected());
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kReusedDirectional);
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
}

NOLINT_TEST(PreviewSunController, ProfileChangesDoNotReplaceTheAuthoredSnapshot)
{
  auto scene = std::make_shared<scene::Scene>("ProfileChange", 16);
  auto node = scene->CreateNode("Directional");
  ASSERT_TRUE(node.AttachLight(std::make_unique<scene::DirectionalLight>()));
  auto controller = PreviewSunController {};
  controller.Update(*scene, true);
  auto& light = node.GetLightAs<scene::DirectionalLight>()->get();
  light.Common().affects_world = false;
  light.SetEnvironmentContribution(false);
  light.SetIsSunLight(false);
  light.SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kSecondary);
  controller.Update(*scene, true);
  EXPECT_TRUE(light.Common().affects_world);
  EXPECT_TRUE(light.GetEnvironmentContribution());
  EXPECT_TRUE(light.IsSunLight());
  EXPECT_EQ(
    light.GetAtmosphereLightSlot(), scene::AtmosphereLightSlot::kPrimary);
  controller.Update(*scene, false);
  EXPECT_TRUE(light.Common().affects_world);
  EXPECT_FALSE(light.GetEnvironmentContribution());
  EXPECT_FALSE(light.IsSunLight());
  EXPECT_EQ(light.GetAtmosphereLightSlot(), scene::AtmosphereLightSlot::kNone);
}

NOLINT_TEST(PreviewSunController, NewAuthoredSunDisablesTheTemporarySun)
{
  for (const auto reuse : { false, true }) {
    auto scene = std::make_shared<scene::Scene>("NewAuthoredSun", 16);
    auto untagged = scene::SceneNode {};
    if (reuse) {
      untagged = scene->CreateNode("Untagged");
      ASSERT_TRUE(
        untagged.AttachLight(std::make_unique<scene::DirectionalLight>()));
    }
    auto controller = PreviewSunController {};
    controller.Update(*scene, true);
    const auto preview = controller.GetSun();
    auto authored = scene->CreateNode("Authored Sun");
    auto light = std::make_unique<scene::DirectionalLight>();
    light->SetIsSunLight(true);
    light->SetEnvironmentContribution(true);
    ASSERT_TRUE(authored.AttachLight(std::move(light)));
    controller.Update(*scene, true);
    EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kBlockedByAuthoredSun);
    EXPECT_FALSE(controller.GetSun().IsAlive());
    EXPECT_EQ(preview.IsAlive(), reuse);
    if (reuse) {
      EXPECT_FALSE(
        untagged.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
    }
  }
}

NOLINT_TEST(
  PreviewSunController, YieldRetainsInactiveInjectionUntilNormalUpdate)
{
  auto scene = std::make_shared<scene::Scene>("YieldInjection", 16);
  PreviewSunController controller;
  controller.Update(*scene, true);
  auto preview = controller.GetSun();
  auto authored = scene->CreateNode("Late Authored Sun");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetEnvironmentContribution(true);
  light->SetIsSunLight(true);
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  ASSERT_TRUE(authored.AttachLight(std::move(light)));
  scene->SyncObservers();
  const auto sync_count = scene->GetMutationDispatchCounters().sync_calls;

  controller.YieldToAuthoredSun(*scene);
  controller.YieldToAuthoredSun(*scene);
  EXPECT_EQ(scene->GetMutationDispatchCounters().sync_calls, sync_count);
  EXPECT_TRUE(preview.IsAlive());
  EXPECT_EQ(scene->GetRootNodes().size(), 2U);
  EXPECT_FALSE(controller.GetSun().IsAlive());
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kBlockedByAuthoredSun);
  const auto& disabled = preview.GetLightAs<scene::DirectionalLight>()->get();
  EXPECT_FALSE(disabled.Common().affects_world);
  EXPECT_FALSE(disabled.GetEnvironmentContribution());
  EXPECT_FALSE(disabled.IsSunLight());
  EXPECT_EQ(
    disabled.GetAtmosphereLightSlot(), scene::AtmosphereLightSlot::kNone);
  const auto primary = scene->GetDirectionalLightResolver().ResolvePrimarySun();
  ASSERT_TRUE(primary);
  EXPECT_EQ(primary->NodeHandle(), authored.GetHandle());

  controller.Update(*scene, true);
  EXPECT_FALSE(preview.IsAlive());
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
}

NOLINT_TEST(PreviewSunController, YieldedInjectionRecoversIfBlockerDisappears)
{
  auto scene = std::make_shared<scene::Scene>("RecoverYieldedInjection", 16);
  PreviewSunController controller;
  controller.Update(*scene, true);
  auto preview = controller.GetSun();
  auto authored = scene->CreateNode("Short-Lived Authored Sun");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetEnvironmentContribution(true);
  light->SetIsSunLight(true);
  ASSERT_TRUE(authored.AttachLight(std::move(light)));
  controller.YieldToAuthoredSun(*scene);
  ASSERT_FALSE(controller.GetSun().IsAlive());
  ASSERT_TRUE(scene->DestroyNode(authored));
  controller.Update(*scene, true);
  EXPECT_EQ(controller.GetSun().GetHandle(), preview.GetHandle());
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kInjected);
  EXPECT_EQ(scene->GetRootNodes().size(), 1U);
  EXPECT_TRUE(
    preview.GetLightAs<scene::DirectionalLight>()->get().IsSunLight());
}

NOLINT_TEST(PreviewSunController, YieldRestoresBorrowedFlagsWithoutObserverSync)
{
  auto scene = std::make_shared<scene::Scene>("YieldBorrowed", 16);
  auto candidate = scene->CreateNode("Borrowed Directional");
  auto original = std::make_unique<scene::DirectionalLight>();
  original->Common().affects_world = false;
  original->SetEnvironmentContribution(false);
  original->SetIsSunLight(false);
  original->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kNone);
  ASSERT_TRUE(candidate.AttachLight(std::move(original)));
  PreviewSunController controller;
  controller.Update(*scene, true);
  auto authored = scene->CreateNode("Late Secondary");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetEnvironmentContribution(true);
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kSecondary);
  ASSERT_TRUE(authored.AttachLight(std::move(light)));
  const auto sync_count = scene->GetMutationDispatchCounters().sync_calls;
  controller.YieldToAuthoredSun(*scene);
  EXPECT_EQ(scene->GetMutationDispatchCounters().sync_calls, sync_count);
  EXPECT_FALSE(controller.GetSun().IsAlive());
  EXPECT_TRUE(candidate.IsAlive());
  const auto& restored = candidate.GetLightAs<scene::DirectionalLight>()->get();
  EXPECT_FALSE(restored.Common().affects_world);
  EXPECT_FALSE(restored.GetEnvironmentContribution());
  EXPECT_FALSE(restored.IsSunLight());
  EXPECT_EQ(
    restored.GetAtmosphereLightSlot(), scene::AtmosphereLightSlot::kNone);
}

NOLINT_TEST(PreviewSunController, PreservesLocalLightsAndEnvironment)
{
  auto scene = std::make_shared<scene::Scene>("LocalLighting", 16);
  auto point = scene->CreateNode("Point");
  auto point_light = std::make_unique<scene::PointLight>();
  point_light->Common().affects_world = false;
  point_light->SetLuminousFluxLm(345.0F);
  ASSERT_TRUE(point.AttachLight(std::move(point_light)));
  auto spot = scene->CreateNode("Spot");
  auto spot_light = std::make_unique<scene::SpotLight>();
  spot_light->Common().casts_shadows = false;
  spot_light->SetLuminousFluxLm(678.0F);
  ASSERT_TRUE(spot.AttachLight(std::move(spot_light)));
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& sky = environment->AddSystem<scene::environment::SkyLight>();
  sky.SetEnabled(false);
  sky.SetIntensityMul(3.0F);
  scene->SetEnvironment(std::move(environment));
  const auto original_environment = scene->GetEnvironment();
  auto controller = PreviewSunController {};
  controller.Update(*scene, true);
  controller.Update(*scene, false);
  EXPECT_EQ(scene->GetRootNodes().size(), 2U);
  const auto& point_after = point.GetLightAs<scene::PointLight>()->get();
  EXPECT_FALSE(point_after.Common().affects_world);
  EXPECT_FLOAT_EQ(point_after.GetLuminousFluxLm(), 345.0F);
  const auto& spot_after = spot.GetLightAs<scene::SpotLight>()->get();
  EXPECT_TRUE(spot_after.Common().affects_world);
  EXPECT_FALSE(spot_after.Common().casts_shadows);
  EXPECT_FLOAT_EQ(spot_after.GetLuminousFluxLm(), 678.0F);
  EXPECT_EQ(scene->GetEnvironment(), original_environment);
  EXPECT_EQ(original_environment->GetSystemCount(), 1U);
  EXPECT_FALSE(sky.IsEnabled());
  EXPECT_FLOAT_EQ(sky.GetIntensityMul(), 3.0F);
}

NOLINT_TEST(PreviewSunController, HandlesDestroyedReusedNodeAndSceneTransitions)
{
  auto first = std::make_shared<scene::Scene>("First", 16);
  auto node = first->CreateNode("Directional");
  ASSERT_TRUE(node.AttachLight(std::make_unique<scene::DirectionalLight>()));
  auto controller = PreviewSunController {};
  controller.Update(*first, true);
  ASSERT_TRUE(first->DestroyNode(node));
  controller.Update(*first, false);
  EXPECT_FALSE(controller.GetSun().IsAlive());
  controller.Update(*first, true);
  const auto first_sun = controller.GetSun();
  auto second = std::make_shared<scene::Scene>("Second", 16);
  first.reset();
  controller.Update(*second, true);
  EXPECT_FALSE(first_sun.IsAlive());
  EXPECT_TRUE(controller.GetSun().IsAlive());
  EXPECT_EQ(second->GetRootNodes().size(), 1U);
  second.reset();
  controller.Reset();
  controller.Reset();
  EXPECT_FALSE(controller.GetSun().IsAlive());
  EXPECT_EQ(controller.GetStatus(), PreviewSunStatus::kDisabled);
}

} // namespace oxygen::examples::testing
