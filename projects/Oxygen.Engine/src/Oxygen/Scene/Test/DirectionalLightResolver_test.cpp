//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>

namespace {

using oxygen::scene::AtmosphereLightSlot;
using oxygen::scene::DirectionalLight;
using oxygen::scene::DirectionalLightContractError;
using oxygen::scene::DirectionalLightResolver;
using oxygen::scene::Scene;

class DirectionalLightResolverTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    scene_ = std::make_shared<Scene>("DirectionalLightResolverTest", 32U);
  }

  auto AddDirectionalLight(std::string_view name,
    const bool environment_contribution, const bool is_sun_light,
    const AtmosphereLightSlot atmosphere_light_slot,
    const glm::quat& local_rotation) -> oxygen::scene::SceneNode
  {
    auto node = scene_->CreateNode(std::string(name));
    auto light = std::make_unique<DirectionalLight>();
    light->Common().affects_world = true;
    light->Common().color_rgb = { 1.0F, 0.95F, 0.9F };
    light->SetIntensityLux(100000.0F);
    light->SetEnvironmentContribution(environment_contribution);
    light->SetIsSunLight(is_sun_light);
    light->SetAtmosphereLightSlot(atmosphere_light_slot);
    EXPECT_TRUE(node.AttachLight(std::move(light)));
    node.GetTransform().SetLocalRotation(local_rotation);
    scene_->Update(false);
    return node;
  }

  std::shared_ptr<Scene> scene_;
};

NOLINT_TEST_F(DirectionalLightResolverTest, ResolveReturnsEmptyWhenSceneHasNoDirectionalLights)
{
  auto& resolver = scene_->GetDirectionalLightResolver();
  EXPECT_TRUE(resolver.IsValid());
  EXPECT_TRUE(resolver.ResolveDirectionalLights().empty());
  const auto& atmosphere_lights = resolver.ResolveAtmosphereLights();
  EXPECT_FALSE(atmosphere_lights.slots[0].has_value());
  EXPECT_FALSE(atmosphere_lights.slots[1].has_value());
  EXPECT_EQ(atmosphere_lights.conflict_count, 0U);
  EXPECT_FALSE(resolver.ResolvePrimarySun().has_value());
  EXPECT_FALSE(resolver.ResolveSecondarySun().has_value());
  EXPECT_FALSE(resolver.ResolveMoon().has_value());
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ResolvePrimarySlotPrefersExplicitPrimaryOverFallbackSun)
{
  static_cast<void>(AddDirectionalLight("Fill", false, false,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Right)));
  static_cast<void>(AddDirectionalLight("Sun", true, true,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right)));
  const auto primary = AddDirectionalLight("ExplicitPrimary", true, false,
    AtmosphereLightSlot::kPrimary,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Up));

  auto& resolver = scene_->GetDirectionalLightResolver();
  ASSERT_TRUE(resolver.ResolvePrimarySun().has_value());
  EXPECT_EQ(resolver.ResolvePrimarySun()->NodeHandle(), primary.GetHandle());
  EXPECT_FALSE(resolver.ResolveSecondarySun().has_value());
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ResolveSecondarySlotUsesOnlyExplicitSecondaryBinding)
{
  const auto sun = AddDirectionalLight("Sun", true, true,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right));
  const auto moon = AddDirectionalLight("Moon", true, false,
    AtmosphereLightSlot::kSecondary,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Up));

  auto& resolver = scene_->GetDirectionalLightResolver();
  ASSERT_TRUE(resolver.ResolvePrimarySun().has_value());
  ASSERT_TRUE(resolver.ResolveSecondarySun().has_value());
  EXPECT_EQ(resolver.ResolvePrimarySun()->NodeHandle(), sun.GetHandle());
  EXPECT_EQ(resolver.ResolveSecondarySun()->NodeHandle(), moon.GetHandle());
  EXPECT_EQ(resolver.ResolveMoon()->NodeHandle(), moon.GetHandle());
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ResolveLeavesSecondaryUnboundWhenNoExplicitSecondaryExists)
{
  const auto sun = AddDirectionalLight("Sun", true, true,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right));
  static_cast<void>(AddDirectionalLight("Moon", true, false,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Up)));

  auto& resolver = scene_->GetDirectionalLightResolver();
  ASSERT_TRUE(resolver.ResolvePrimarySun().has_value());
  EXPECT_EQ(resolver.ResolvePrimarySun()->NodeHandle(), sun.GetHandle());
  EXPECT_FALSE(resolver.ResolveSecondarySun().has_value());
  EXPECT_FALSE(resolver.ResolveMoon().has_value());
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ResolveTracksFirstWinsConflictMetadataForExplicitPrimaryClaims)
{
  static_cast<void>(AddDirectionalLight("PrimaryA", true, false,
    AtmosphereLightSlot::kPrimary,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Up)));
  static_cast<void>(AddDirectionalLight("PrimaryB", true, true,
    AtmosphereLightSlot::kPrimary,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right)));

  auto& resolver = scene_->GetDirectionalLightResolver();
  ASSERT_TRUE(resolver.ResolvePrimarySun().has_value());
  const auto& atmosphere_lights = resolver.ResolveAtmosphereLights();
  const auto resolved_directional_lights = resolver.ResolveDirectionalLights();
  const auto first_primary = std::ranges::find_if(resolved_directional_lights,
    [](const oxygen::scene::ResolvedDirectionalLightView& entry) {
      return entry.Light().GetEnvironmentContribution()
        && entry.Light().GetAtmosphereLightSlot()
          == oxygen::scene::AtmosphereLightSlot::kPrimary;
    });
  ASSERT_NE(first_primary, resolved_directional_lights.end());

  EXPECT_EQ(resolver.ResolvePrimarySun()->NodeHandle(), first_primary->NodeHandle());
  EXPECT_EQ(atmosphere_lights.conflict_count, 1U);
  EXPECT_EQ(atmosphere_lights.first_conflict_slot, 0U);
  EXPECT_TRUE(atmosphere_lights.explicit_slot_claims[0]);
  EXPECT_FALSE(atmosphere_lights.slots[1].has_value());
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ValidateThrowsWhenSunLightLacksEnvironmentContribution)
{
  static_cast<void>(AddDirectionalLight("BadSun", false, true,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right)));

  EXPECT_THROW(scene_->GetDirectionalLightResolver().Validate(),
    DirectionalLightContractError);
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ValidateThrowsWhenMoreThanTwoEnvironmentContributingLightsExist)
{
  static_cast<void>(AddDirectionalLight("Sun", true, true,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right)));
  static_cast<void>(AddDirectionalLight("Moon", true, false,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Up)));
  static_cast<void>(AddDirectionalLight("ThirdEnv", true, false,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Left)));

  EXPECT_THROW(scene_->GetDirectionalLightResolver().Validate(),
    DirectionalLightContractError);
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ValidateThrowsWhenMoreThanOneSunLightExists)
{
  static_cast<void>(AddDirectionalLight("SunA", true, true,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right)));
  static_cast<void>(AddDirectionalLight("SunB", true, true,
    AtmosphereLightSlot::kNone,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Right)));

  EXPECT_THROW(scene_->GetDirectionalLightResolver().Validate(),
    DirectionalLightContractError);
}

} // namespace
