//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/Sun.h>

namespace oxygen::scene::environment::testing {

namespace {

  class SunTemperatureTest : public ::testing::Test { };

} // namespace

//! Verifies SceneEnvironment can host a Sun system.
NOLINT_TEST_F(SunTemperatureTest, SceneEnvironmentHostsSunSystem)
{
  // Arrange
  SceneEnvironment environment;

  // Act
  auto& sun = environment.AddSystem<Sun>();

  // Assert
  EXPECT_EQ(environment.GetSystemCount(), 1U);
  EXPECT_TRUE(environment.HasSystem<Sun>());
  auto sun_ptr = environment.TryGetSystem<Sun>();
  ASSERT_NE(sun_ptr.get(), nullptr);
  EXPECT_TRUE(sun_ptr->IsEnabled());
  EXPECT_EQ(&sun, sun_ptr.get());
}

//! Validates the 2000K temperature conversion (warm sunrise tone).
NOLINT_TEST_F(SunTemperatureTest, Temperature2000KProducesWarmColor)
{
  // Arrange
  Sun sun;

  // Act
  sun.SetLightTemperatureKelvin(2000.0F);
  const Vec3 color = sun.GetColorRgb();

  // Assert
  EXPECT_TRUE(sun.HasLightTemperature());
  EXPECT_FLOAT_EQ(sun.GetLightTemperatureKelvin(), 2000.0F);

  const float max_component = std::max({ color.r, color.g, color.b });
  EXPECT_NEAR(max_component, 1.0F, 0.001F);
  EXPECT_NEAR(color.r, 1.0F, 0.02F);
  EXPECT_NEAR(color.g, 0.54F, 0.03F);
  EXPECT_NEAR(color.b, 0.05F, 0.03F);
}

//! Validates the 5500K temperature conversion (neutral daylight).
NOLINT_TEST_F(SunTemperatureTest, Temperature5500KProducesDaylightColor)
{
  // Arrange
  Sun sun;

  // Act
  sun.SetLightTemperatureKelvin(5500.0F);
  const Vec3 color = sun.GetColorRgb();

  // Assert
  EXPECT_TRUE(sun.HasLightTemperature());
  EXPECT_FLOAT_EQ(sun.GetLightTemperatureKelvin(), 5500.0F);

  const float max_component = std::max({ color.r, color.g, color.b });
  EXPECT_NEAR(max_component, 1.0F, 0.001F);
  EXPECT_NEAR(color.r, 1.0F, 0.02F);
  EXPECT_NEAR(color.g, 0.93F, 0.03F);
  EXPECT_NEAR(color.b, 0.87F, 0.03F);
}

//! Validates the 6500K temperature conversion (D65-like white).
NOLINT_TEST_F(SunTemperatureTest, Temperature6500KProducesNeutralWhite)
{
  // Arrange
  Sun sun;

  // Act
  sun.SetLightTemperatureKelvin(6500.0F);
  const Vec3 color = sun.GetColorRgb();

  // Assert
  EXPECT_TRUE(sun.HasLightTemperature());
  EXPECT_FLOAT_EQ(sun.GetLightTemperatureKelvin(), 6500.0F);

  const float max_component = std::max({ color.r, color.g, color.b });
  EXPECT_NEAR(max_component, 1.0F, 0.001F);
  EXPECT_NEAR(color.r, 1.0F, 0.02F);
  EXPECT_NEAR(color.g, 1.0F, 0.02F);
  EXPECT_NEAR(color.b, 0.98F, 0.03F);
}

//! Validates the 10000K temperature conversion (cooler blue tone).
NOLINT_TEST_F(SunTemperatureTest, Temperature10000KProducesCoolColor)
{
  // Arrange
  Sun sun;

  // Act
  sun.SetLightTemperatureKelvin(10000.0F);
  const Vec3 color = sun.GetColorRgb();

  // Assert
  EXPECT_TRUE(sun.HasLightTemperature());
  EXPECT_FLOAT_EQ(sun.GetLightTemperatureKelvin(), 10000.0F);

  const float max_component = std::max({ color.r, color.g, color.b });
  EXPECT_NEAR(max_component, 1.0F, 0.001F);
  EXPECT_NEAR(color.r, 0.79F, 0.04F);
  EXPECT_NEAR(color.g, 0.86F, 0.04F);
  EXPECT_NEAR(color.b, 1.0F, 0.02F);
}

} // namespace oxygen::scene::environment::testing
