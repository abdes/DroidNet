//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  constexpr std::uint32_t kPlanetShadowProbe = 131072U;
  using Ray = std::array<float, 8>;
} // namespace

NOLINT_TEST_F(ExposureGpuTest, AtmospherePlanetDoesNotOccludeOutwardSurfaceRays)
{
  constexpr unsigned kElevationSteps = 401U;
  constexpr unsigned kPositionSteps = 113U;
  constexpr float kMinimumElevationDegrees = 20.0F;
  constexpr float kElevationStepDegrees = 0.05F;
  constexpr float kPositionStepKm = 0.0005F;
  constexpr float kGroundHalfExtentKm = 0.028F;
  constexpr float kAtmosphereHeightKm = 100.0F;
  constexpr std::array kPlanetRadiiKm { 6360.0F, 6360.00048828125F };
  auto rays = std::vector<Ray> {};
  rays.reserve(kPlanetRadiiKm.size() * kElevationSteps * kPositionSteps);
  for (const auto radius : kPlanetRadiiKm) {
    for (unsigned elevation = 0U; elevation < kElevationSteps; ++elevation) {
      const auto angle
        = (kMinimumElevationDegrees
            + (static_cast<float>(elevation) * kElevationStepDegrees))
        * std::numbers::pi_v<float> / 180.0F;
      for (unsigned position = 0U; position < kPositionSteps; ++position) {
        // Every origin is on or outside the sphere and every direction is
        // outward. Moving across a ground plane cannot create a planet shadow.
        rays.push_back(Ray {
          0.0F,
          (static_cast<float>(position) * kPositionStepKm)
            - kGroundHalfExtentKm,
          radius,
          radius,
          0.0F,
          -std::cos(angle),
          std::sin(angle),
          kAtmosphereHeightKm,
        });
      }
    }
  }
  const auto results = RunToneProbe(std::as_bytes(std::span(rays)),
    static_cast<std::uint32_t>(rays.size()), kPlanetShadowProbe, false);
  ASSERT_EQ(results.size(), rays.size());
  auto false_shadows = std::size_t { 0U };
  for (const auto& result : results) {
    if (result.at(0) != 1.0F || result.at(1) != 1.0F || result.at(2) != 1.0F) {
      ++false_shadows;
    }
  }
  EXPECT_EQ(false_shadows, 0U)
    << "Outward surface rays tested: " << rays.size();
}

NOLINT_TEST_F(
  ExposureGpuTest, AtmospherePlanetPreservesNightAndElevatedHorizons)
{
  constexpr float kRadiusKm = 6360.0F;
  constexpr float kAtmosphereHeightKm = 100.0F;
  constexpr float kLowAltitudeKm = 1.0F;
  constexpr float kOrbitAltitudeKm = 1000.0F;
  struct Case {
    glm::vec3 origin;
    glm::vec3 direction;
    bool occluded;
  };
  constexpr std::array cases {
    Case {
      .origin = { 0, 0, kRadiusKm },
      .direction = { 0, 0, 1 },
      .occluded = false,
    },
    Case {
      .origin = { 0, 0, kRadiusKm },
      .direction = { 0, 0, -1 },
      .occluded = true,
    },
    Case {
      .origin = { 0, 0, kRadiusKm },
      .direction = { 1, 0, 0 },
      .occluded = false,
    },
    Case {
      .origin = { kRadiusKm, 0, 0 },
      .direction = { 1, 0, 0 },
      .occluded = false,
    },
    Case {
      .origin = { kRadiusKm, 0, 0 },
      .direction = { -1, 0, 0 },
      .occluded = true,
    },
    Case {
      .origin = { 0, 0, kRadiusKm - kLowAltitudeKm },
      .direction = { 0, 0, 1 },
      .occluded = true,
    },
    Case {
      .origin = { 0, 0, 0 },
      .direction = { 0, 0, 1 },
      .occluded = true,
    },
    // At 1 km the depressed horizon is about one degree below horizontal.
    Case {
      .origin = { 0, 0, kRadiusKm + kLowAltitudeKm },
      .direction = { 1, 0, -0.01F },
      .occluded = false,
    },
    Case {
      .origin = { 0, 0, kRadiusKm + kLowAltitudeKm },
      .direction = { 1, 0, -0.1F },
      .occluded = true,
    },
    Case {
      .origin = { 0, 0, kRadiusKm + kOrbitAltitudeKm },
      .direction = { 1, 0, -0.1F },
      .occluded = false,
    },
    Case {
      .origin = { 0, 0, kRadiusKm + kOrbitAltitudeKm },
      .direction = { 0, 0, -1 },
      .occluded = true,
    },
  };
  auto rays = std::vector<Ray> {};
  rays.reserve(cases.size());
  for (const auto& test : cases) {
    const auto direction = glm::normalize(test.direction);
    rays.push_back(Ray {
      test.origin.x,
      test.origin.y,
      test.origin.z,
      kRadiusKm,
      direction.x,
      direction.y,
      direction.z,
      kAtmosphereHeightKm,
    });
  }
  const auto results = RunToneProbe(std::as_bytes(std::span(rays)),
    static_cast<std::uint32_t>(rays.size()), kPlanetShadowProbe, false);
  ASSERT_EQ(results.size(), cases.size());
  for (std::size_t index = 0U; index < cases.size(); ++index) {
    SCOPED_TRACE(index);
    const auto expected = cases.at(index).occluded ? 0.0F : 1.0F;
    EXPECT_EQ(results.at(index).at(0), expected);
    EXPECT_EQ(results.at(index).at(1), expected);
    EXPECT_EQ(results.at(index).at(2), expected);
  }
}

} // namespace oxygen::vortex::testing::exposure
