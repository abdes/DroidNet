//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/EnvironmentSceneSnapshot.h"

namespace oxygen::examples::testing {

namespace {

  namespace env = scene::environment;

  class EnvironmentSceneSnapshotTest : public ::testing::Test {
  protected:
    std::shared_ptr<scene::Scene> scene_
      = std::make_shared<scene::Scene>("Snapshot", 32);
    EnvironmentSceneSnapshot snapshot_;
  };

} // namespace

NOLINT_TEST_F(EnvironmentSceneSnapshotTest,
  RestoresEditedSystemsAndExistenceWithoutReplacingUntouchedSystems)
{
  auto environment = std::make_unique<scene::SceneEnvironment>();
  auto& atmosphere = environment->AddSystem<env::SkyAtmosphere>();
  atmosphere.SetEnabled(false);
  atmosphere.SetPlanetRadiusMeters(6200000.0F);
  atmosphere.SetPlanetAnchorWorldPosition({ 11.0F, 22.0F, 33.0F });
  auto& sky_light = environment->AddSystem<env::SkyLight>();
  sky_light.SetIntensityMul(1.75F);
  auto& fog = environment->AddSystem<env::Fog>();
  fog.SetExtinctionSigmaTPerMeter(0.037F);
  auto& background = environment->AddSystem<env::Background>();
  auto& post_process = environment->AddSystem<env::PostProcessVolume>();
  auto& clouds = environment->AddSystem<env::VolumetricClouds>();
  auto* const original_environment = environment.get();
  scene_->SetEnvironment(std::move(environment));
  snapshot_.Capture(*scene_);

  atmosphere.SetEnabled(true);
  atmosphere.SetPlanetRadiusMeters(6300000.0F);
  atmosphere.SetPlanetAnchorWorldPosition({ 0.0F, 0.0F, 0.0F });
  fog.SetExtinctionSigmaTPerMeter(0.001F);
  original_environment->RemoveSystem<env::SkyLight>();
  original_environment->AddSystem<env::SkySphere>();
  background.SetColorRgb({ 0.2F, 0.4F, 0.6F });
  post_process.SetManualExposureEv(8.5F);
  clouds.SetCoverage(0.73F);

  // Repeated restoration must preserve component existence and values.
  for (int i = 0; i < 2; ++i) {
    snapshot_.Restore(*scene_);
    const auto restored = scene_->GetEnvironment();
    ASSERT_EQ(restored.get(), original_environment);
    const auto restored_atmosphere
      = restored->TryGetSystem<env::SkyAtmosphere>();
    ASSERT_TRUE(restored_atmosphere);
    EXPECT_FALSE(restored_atmosphere->IsEnabled());
    EXPECT_FLOAT_EQ(restored_atmosphere->GetPlanetRadiusMeters(), 6200000.0F);
    EXPECT_EQ(restored_atmosphere->GetPlanetAnchorWorldPosition(),
      glm::vec3(11.0F, 22.0F, 33.0F));
    ASSERT_TRUE(restored->HasSystem<env::SkyLight>());
    EXPECT_FLOAT_EQ(
      restored->TryGetSystem<env::SkyLight>()->GetIntensityMul(), 1.75F);
    ASSERT_TRUE(restored->HasSystem<env::Fog>());
    EXPECT_FLOAT_EQ(
      restored->TryGetSystem<env::Fog>()->GetExtinctionSigmaTPerMeter(),
      0.037F);
    EXPECT_FALSE(restored->HasSystem<env::SkySphere>());
    EXPECT_EQ(restored->TryGetSystem<env::Background>().get(), &background);
    EXPECT_EQ(background.GetColorRgb(), glm::vec3(0.2F, 0.4F, 0.6F));
    EXPECT_EQ(
      restored->TryGetSystem<env::PostProcessVolume>().get(), &post_process);
    EXPECT_FLOAT_EQ(post_process.GetManualExposureEv(), 8.5F);
    EXPECT_EQ(restored->TryGetSystem<env::VolumetricClouds>().get(), &clouds);
    EXPECT_FLOAT_EQ(clouds.GetCoverage(), 0.73F);
  }
}

NOLINT_TEST_F(EnvironmentSceneSnapshotTest,
  RestoresAbsentEnvironmentAndPreservesNewUntouchedSystems)
{
  snapshot_.Capture(*scene_);
  auto environment = std::make_unique<scene::SceneEnvironment>();
  environment->AddSystem<env::SkyAtmosphere>();
  environment->AddSystem<env::SkyLight>();
  scene_->SetEnvironment(std::move(environment));

  snapshot_.Restore(*scene_);
  EXPECT_FALSE(scene_->HasEnvironment());

  environment = std::make_unique<scene::SceneEnvironment>();
  environment->AddSystem<env::SkyAtmosphere>();
  environment->AddSystem<env::Background>().SetColorRgb({ 0.3F, 0.2F, 0.1F });
  scene_->SetEnvironment(std::move(environment));

  snapshot_.Restore(*scene_);
  ASSERT_TRUE(scene_->HasEnvironment());
  EXPECT_FALSE(scene_->GetEnvironment()->HasSystem<env::SkyAtmosphere>());
  EXPECT_EQ(
    scene_->GetEnvironment()->TryGetSystem<env::Background>()->GetColorRgb(),
    glm::vec3(0.3F, 0.2F, 0.1F));
}

NOLINT_TEST_F(EnvironmentSceneSnapshotTest, RestoresOriginallyEmptyEnvironment)
{
  scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  snapshot_.Capture(*scene_);
  scene_->SetEnvironment(nullptr);

  snapshot_.Restore(*scene_);

  ASSERT_TRUE(scene_->HasEnvironment());
  EXPECT_EQ(scene_->GetEnvironment()->GetSystemCount(), 0U);
}

NOLINT_TEST_F(EnvironmentSceneSnapshotTest,
  RestoresDirectionalLightAndLocalRotationWithResolverNotification)
{
  auto parent = scene_->CreateNode("Parent");
  ASSERT_TRUE(parent.GetTransform().SetLocalRotation(
    glm::angleAxis(0.7F, glm::vec3(0.0F, 0.0F, 1.0F))));
  auto sun = scene_->CreateChildNode(parent, "Authored Sun");
  ASSERT_TRUE(sun.has_value());
  const auto original_rotation
    = glm::angleAxis(0.3F, glm::vec3(1.0F, 0.0F, 0.0F));
  ASSERT_TRUE(sun->GetTransform().SetLocalRotation(original_rotation));
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);

  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  light->SetIntensityLux(4321.0F);
  light->SetAngularSizeRadians(0.013F);
  light->Common().shadow.bias = 0.03F;
  light->Common().color_rgb = { 0.3F, 0.6F, 0.9F };
  ASSERT_TRUE(sun->AttachLight(std::move(light)));
  scene_->Update(false);
  scene_->SyncObservers();
  const auto before = scene_->GetDirectionalLightResolver().ResolvePrimarySun();
  ASSERT_TRUE(before.has_value());
  const auto original_direction = before->DirectionToLightWs();
  snapshot_.Capture(*scene_);

  auto custom = std::make_unique<scene::DirectionalLight>();
  custom->Common().affects_world = false;
  ASSERT_TRUE(sun->ReplaceLight(std::move(custom)));
  ASSERT_TRUE(sun->GetTransform().SetLocalRotation(
    glm::angleAxis(1.2F, glm::vec3(0.0F, 1.0F, 0.0F))));
  ASSERT_TRUE(sun->GetTransform().SetLocalPosition({ 3.0F, 4.0F, 5.0F }));
  scene_->Update(false);
  scene_->SyncObservers();
  EXPECT_FALSE(scene_->GetDirectionalLightResolver().ResolvePrimarySun());

  snapshot_.Restore(*scene_);

  const auto restored = sun->GetLightAs<scene::DirectionalLight>();
  ASSERT_TRUE(restored.has_value());
  EXPECT_TRUE(restored->get().GetAtmosphereLightSlot() == scene::AtmosphereLightSlot::kPrimary);

  EXPECT_EQ(restored->get().GetAtmosphereLightSlot(),
    scene::AtmosphereLightSlot::kPrimary);
  EXPECT_FLOAT_EQ(restored->get().GetIntensityLux(), 4321.0F);
  EXPECT_FLOAT_EQ(restored->get().GetAngularSizeRadians(), 0.013F);
  EXPECT_FLOAT_EQ(restored->get().Common().shadow.bias, 0.03F);
  EXPECT_EQ(restored->get().Common().color_rgb, glm::vec3(0.3F, 0.6F, 0.9F));
  EXPECT_EQ(sun->GetTransform().GetLocalRotation(), original_rotation);
  EXPECT_EQ(
    sun->GetTransform().GetLocalPosition(), glm::vec3(3.0F, 4.0F, 5.0F));
  const auto after = scene_->GetDirectionalLightResolver().ResolvePrimarySun();
  ASSERT_TRUE(after.has_value());
  EXPECT_LT(
    glm::length(after->DirectionToLightWs() - original_direction), 1e-5F);
}

NOLINT_TEST_F(EnvironmentSceneSnapshotTest,
  RestoresLocalFogComponentsWithoutRecreatingOrRemovingNodes)
{
  auto authored = scene_->CreateNode("Authored Fog");
  auto& fog = authored.GetImpl()->get().AddComponent<env::LocalFogVolume>();
  fog.SetRadialFogExtinction(0.023F);
  fog.SetFogAlbedo({ 0.2F, 0.4F, 0.6F });
  fog.SetSortPriority(3);
  auto removed = scene_->CreateNode("Removed Fog");
  removed.GetImpl()
    ->get()
    .AddComponent<env::LocalFogVolume>()
    .SetHeightFogExtinction(0.047F);
  auto destroyed = scene_->CreateNode("Destroyed Fog");
  destroyed.GetImpl()->get().AddComponent<env::LocalFogVolume>();
  auto empty = scene_->CreateNode("Initially Empty");
  snapshot_.Capture(*scene_);

  fog.SetRadialFogExtinction(0.5F);
  fog.SetFogAlbedo({ 1.0F, 1.0F, 1.0F });
  fog.SetSortPriority(9);
  removed.GetImpl()->get().RemoveComponent<env::LocalFogVolume>();
  empty.GetImpl()->get().AddComponent<env::LocalFogVolume>();
  ASSERT_TRUE(scene_->DestroyNode(destroyed));
  auto added = scene_->CreateNode("Destroyed Fog");
  added.GetImpl()->get().AddComponent<env::LocalFogVolume>();
  const auto node_count = scene_->GetNodeCount();

  snapshot_.Restore(*scene_);

  const auto& restored
    = authored.GetImpl()->get().GetComponent<env::LocalFogVolume>();
  EXPECT_FLOAT_EQ(restored.GetRadialFogExtinction(), 0.023F);
  EXPECT_EQ(restored.GetFogAlbedo(), glm::vec3(0.2F, 0.4F, 0.6F));
  EXPECT_EQ(restored.GetSortPriority(), 3);
  ASSERT_TRUE(removed.GetImpl()->get().HasComponent<env::LocalFogVolume>());
  EXPECT_FLOAT_EQ(removed.GetImpl()
                    ->get()
                    .GetComponent<env::LocalFogVolume>()
                    .GetHeightFogExtinction(),
    0.047F);
  EXPECT_FALSE(empty.GetImpl()->get().HasComponent<env::LocalFogVolume>());
  EXPECT_FALSE(added.GetImpl()->get().HasComponent<env::LocalFogVolume>());
  EXPECT_EQ(scene_->GetNodeCount(), node_count);
  EXPECT_TRUE(added.IsAlive());
  EXPECT_FALSE(destroyed.IsAlive());
}

NOLINT_TEST_F(EnvironmentSceneSnapshotTest,
  DoesNotRestoreDestroyedDirectionalOntoReplacementNode)
{
  auto original = scene_->CreateNode("Sun");
  ASSERT_TRUE(
    original.AttachLight(std::make_unique<scene::DirectionalLight>()));
  snapshot_.Capture(*scene_);
  ASSERT_TRUE(scene_->DestroyNode(original));
  auto replacement = scene_->CreateNode("Sun");

  snapshot_.Restore(*scene_);

  EXPECT_FALSE(replacement.GetLightAs<scene::DirectionalLight>());
  EXPECT_EQ(scene_->GetNodeCount(), 1U);
}

NOLINT_TEST_F(
  EnvironmentSceneSnapshotTest, SceneIdentityAndResetPreventStaleRestore)
{
  snapshot_.Capture(*scene_);
  auto other = std::make_shared<scene::Scene>("Other", 32);
  auto environment = std::make_unique<scene::SceneEnvironment>();
  environment->AddSystem<env::SkyAtmosphere>();
  other->SetEnvironment(std::move(environment));

  snapshot_.Restore(*other);
  EXPECT_TRUE(other->GetEnvironment()->HasSystem<env::SkyAtmosphere>());
  snapshot_.Capture(*other);
  scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene_->GetEnvironment()->AddSystem<env::SkyLight>();
  snapshot_.Restore(*scene_);
  EXPECT_TRUE(scene_->GetEnvironment()->HasSystem<env::SkyLight>());
  snapshot_.Reset();
  other->GetEnvironment()->RemoveSystem<env::SkyAtmosphere>();
  snapshot_.Restore(*other);
  EXPECT_FALSE(other->GetEnvironment()->HasSystem<env::SkyAtmosphere>());
}

NOLINT_TEST_F(EnvironmentSceneSnapshotTest, SnapshotDoesNotKeepSourceSceneAlive)
{
  const std::weak_ptr<scene::Scene> lifetime = scene_;
  snapshot_.Capture(*scene_);
  scene_.reset();
  EXPECT_TRUE(lifetime.expired());
  auto other = std::make_shared<scene::Scene>("After Expiry", 32);
  auto environment = std::make_unique<scene::SceneEnvironment>();
  environment->AddSystem<env::Fog>();
  other->SetEnvironment(std::move(environment));

  snapshot_.Restore(*other);

  EXPECT_TRUE(other->GetEnvironment()->HasSystem<env::Fog>());
}

} // namespace oxygen::examples::testing
