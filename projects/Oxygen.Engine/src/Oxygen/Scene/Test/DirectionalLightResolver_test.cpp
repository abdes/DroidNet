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
    const glm::quat& local_rotation) -> oxygen::scene::SceneNode
  {
    auto node = scene_->CreateNode(std::string(name));
    auto light = std::make_unique<DirectionalLight>();
    light->Common().affects_world = true;
    light->Common().color_rgb = { 1.0F, 0.95F, 0.9F };
    light->SetIntensityLux(100000.0F);
    light->SetEnvironmentContribution(environment_contribution);
    light->SetIsSunLight(is_sun_light);
    light->SetAtmosphereLightSlot(AtmosphereLightSlot::kPrimary);
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
  EXPECT_FALSE(resolver.ResolvePrimarySun().has_value());
  EXPECT_FALSE(resolver.ResolveSecondarySun().has_value());
  EXPECT_FALSE(resolver.ResolveMoon().has_value());
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ResolvePrefersEnvironmentContributingSunOverEarlierNonEnvironmentDirectional)
{
  static_cast<void>(AddDirectionalLight("Fill", false, false,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Right)));
  const auto sun = AddDirectionalLight("Sun", true, true,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right));

  auto& resolver = scene_->GetDirectionalLightResolver();
  ASSERT_TRUE(resolver.ResolvePrimarySun().has_value());
  EXPECT_EQ(resolver.ResolvePrimarySun()->NodeHandle(), sun.GetHandle());
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ResolveExposesSecondaryEnvironmentLightWhenTwoEnvironmentLightsExist)
{
  const auto sun = AddDirectionalLight("Sun", true, true,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right));
  const auto moon = AddDirectionalLight("Moon", true, false,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Up));

  auto& resolver = scene_->GetDirectionalLightResolver();
  ASSERT_TRUE(resolver.ResolvePrimarySun().has_value());
  ASSERT_TRUE(resolver.ResolveSecondarySun().has_value());
  EXPECT_EQ(resolver.ResolvePrimarySun()->NodeHandle(), sun.GetHandle());
  EXPECT_EQ(resolver.ResolveSecondarySun()->NodeHandle(), moon.GetHandle());
  EXPECT_EQ(resolver.ResolveMoon()->NodeHandle(), moon.GetHandle());
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ValidateThrowsWhenSunLightLacksEnvironmentContribution)
{
  static_cast<void>(AddDirectionalLight("BadSun", false, true,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right)));

  EXPECT_THROW(scene_->GetDirectionalLightResolver().Validate(),
    DirectionalLightContractError);
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ValidateThrowsWhenMoreThanTwoEnvironmentContributingLightsExist)
{
  static_cast<void>(AddDirectionalLight("Sun", true, true,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right)));
  static_cast<void>(AddDirectionalLight("Moon", true, false,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Up)));
  static_cast<void>(AddDirectionalLight("ThirdEnv", true, false,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Left)));

  EXPECT_THROW(scene_->GetDirectionalLightResolver().Validate(),
    DirectionalLightContractError);
}

NOLINT_TEST_F(DirectionalLightResolverTest,
  ValidateThrowsWhenMoreThanOneSunLightExists)
{
  static_cast<void>(AddDirectionalLight("SunA", true, true,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right)));
  static_cast<void>(AddDirectionalLight("SunB", true, true,
    glm::angleAxis(+oxygen::math::HalfPi, oxygen::space::move::Right)));

  EXPECT_THROW(scene_->GetDirectionalLightResolver().Validate(),
    DirectionalLightContractError);
}

} // namespace
