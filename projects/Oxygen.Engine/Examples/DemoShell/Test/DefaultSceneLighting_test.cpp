//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/DefaultSceneLighting.h"

namespace oxygen::examples::testing {

NOLINT_TEST(DefaultSceneLighting, AuthorsSunAndEnvironment)
{
  auto scene = std::make_shared<scene::Scene>("DefaultLightingTest", 16);

  const auto sun_node = EnsureDefaultSceneLighting(*scene,
    DefaultSceneLightingDesc {
      .sun_node_name = "TestSun",
      .sun_position = { -4.0F, -6.0F, 8.0F },
      .focus_point = { 0.0F, 0.0F, 1.0F },
    });
  ASSERT_TRUE(sun_node.IsAlive());

  const auto env = scene->GetEnvironment();
  ASSERT_NE(env, nullptr);
  EXPECT_NE(env->TryGetSystem<scene::environment::SkyAtmosphere>(), nullptr);
  EXPECT_NE(env->TryGetSystem<scene::environment::SkyLight>(), nullptr);

  const auto primary = scene->GetDirectionalLightResolver().ResolvePrimarySun();
  ASSERT_TRUE(primary.has_value());
  EXPECT_EQ(primary->Node().GetName(), "TestSun");
  EXPECT_TRUE(primary->Light().IsSunLight());
  EXPECT_TRUE(primary->Light().GetEnvironmentContribution());
  EXPECT_TRUE(primary->Light().Common().casts_shadows);

  const glm::vec3 expected_direction_to_light
    = glm::normalize(glm::vec3 { -4.0F, -6.0F, 8.0F }
      - glm::vec3 { 0.0F, 0.0F, 1.0F });
  EXPECT_NEAR(
    glm::dot(primary->DirectionToLightWs(), expected_direction_to_light), 1.0F,
    0.001F);
}

NOLINT_TEST(DefaultSceneLighting, IsIdempotent)
{
  auto scene = std::make_shared<scene::Scene>("DefaultLightingTest", 16);

  const auto sun_node = EnsureDefaultSceneLighting(*scene);
  const auto same_sun_node = EnsureDefaultSceneLighting(*scene);
  ASSERT_TRUE(sun_node.IsAlive());
  ASSERT_EQ(same_sun_node.GetHandle(), sun_node.GetHandle());

  EXPECT_EQ(scene->GetEnvironment()->GetSystemCount(), 2U);
  EXPECT_EQ(
    scene->GetDirectionalLightResolver().ResolveDirectionalLights().size(), 1U);
}

} // namespace oxygen::examples::testing
