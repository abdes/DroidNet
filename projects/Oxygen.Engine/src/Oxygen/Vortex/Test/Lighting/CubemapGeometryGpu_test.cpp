//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>

namespace oxygen::vortex::testing {
namespace {
  constexpr std::uint32_t kGeometryProbe = 39U;
  constexpr std::uint32_t kResultWords = 16U;
  constexpr std::uint32_t kFaceCount = 6U;
  constexpr std::size_t kUvWord = 4U;
  constexpr std::size_t kCubeDirectionWord = 8U;
  constexpr std::size_t kRestoredDirectionWord = 12U;
  constexpr float kCoordinateTolerance = 2.0e-6F;
  constexpr double kTexelTolerance = 2.0e-7;
  constexpr double kSphereTolerance = 1.0e-4;

  struct GeometryInput {
    std::array<float, 3> direction { 1.0F, 0.0F, 0.0F };
    std::uint32_t face { 0U };
    std::array<float, 2> uv { 0.5F, 0.5F };
    std::array<std::uint32_t, 2> pixel {};
    std::uint32_t size { 1U };
    std::array<std::uint32_t, 3> reserved {};
  };
  static_assert(sizeof(GeometryInput) == sizeof(float) * 3U * 4U);

  // Independent face frames: normal, image-right and image-down directions.
  constexpr auto kNormals = std::array {
    glm::dvec3 { 1, 0, 0 },
    glm::dvec3 { -1, 0, 0 },
    glm::dvec3 { 0, 1, 0 },
    glm::dvec3 { 0, -1, 0 },
    glm::dvec3 { 0, 0, 1 },
    glm::dvec3 { 0, 0, -1 },
  };
  constexpr auto kRight = std::array {
    glm::dvec3 { 0, 0, -1 },
    glm::dvec3 { 0, 0, 1 },
    glm::dvec3 { 1, 0, 0 },
    glm::dvec3 { 1, 0, 0 },
    glm::dvec3 { 1, 0, 0 },
    glm::dvec3 { -1, 0, 0 },
  };
  constexpr auto kDown = std::array {
    glm::dvec3 { 0, -1, 0 },
    glm::dvec3 { 0, -1, 0 },
    glm::dvec3 { 0, 0, 1 },
    glm::dvec3 { 0, 0, -1 },
    glm::dvec3 { 0, -1, 0 },
    glm::dvec3 { 0, -1, 0 },
  };

  auto Direction(std::uint32_t face, const std::array<double, 2>& uv)
    -> glm::dvec3
  {
    return glm::normalize(kNormals.at(face)
      + ((2.0 * uv.at(0)) - 1.0) * kRight.at(face)
      + ((2.0 * uv.at(1)) - 1.0) * kDown.at(face));
  }

  struct FaceUv {
    std::uint32_t face;
    std::array<double, 2> uv;
  };
  auto FaceCoordinates(const glm::dvec3& direction) -> FaceUv
  {
    const auto components
      = std::array { direction.x, direction.y, direction.z };
    std::size_t axis = 0;
    for (std::size_t candidate = 1; candidate < components.size();
      ++candidate) {
      if (std::abs(components.at(candidate)) > std::abs(components.at(axis))) {
        axis = candidate;
      }
    }
    const auto face = static_cast<std::uint32_t>(2U * axis)
      + (components.at(axis) < 0.0 ? 1U : 0U);
    const auto scale = std::abs(components.at(axis));
    return {
      .face = face,
      .uv = { 0.5 * ((glm::dot(direction, kRight.at(face)) / scale) + 1.0),
        0.5 * ((glm::dot(direction, kDown.at(face)) / scale) + 1.0), },
    };
  }

  auto TriangleArea(
    const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) -> double
  {
    return 2.0
      * std::atan2(std::abs(glm::dot(a, glm::cross(b, c))),
        1.0 + glm::dot(a, b) + glm::dot(b, c) + glm::dot(c, a));
  }
  auto TexelArea(const std::array<std::uint32_t, 2>& pixel, std::uint32_t size)
    -> double
  {
    const auto x = pixel.at(0);
    const auto y = pixel.at(1);
    const auto u0 = static_cast<double>(x) / size;
    const auto v0 = static_cast<double>(y) / size;
    const auto u1 = static_cast<double>(x + 1U) / size;
    const auto v1 = static_cast<double>(y + 1U) / size;
    const auto a = Direction(0U, { u0, v0 });
    const auto b = Direction(0U, { u1, v0 });
    const auto c = Direction(0U, { u1, v1 });
    const auto d = Direction(0U, { u0, v1 });
    return TriangleArea(a, b, c) + TriangleArea(a, c, d);
  }
  auto Float(std::uint32_t word) -> float { return std::bit_cast<float>(word); }
} // namespace

NOLINT_TEST_F(LightingGpuAbiTest, CubemapGeometryFacesEdgesCornersAndAxisTies)
{
  auto inputs = std::vector<GeometryInput> {};
  constexpr auto coordinates = std::array { 0.0F, 0.25F, 0.5F, 0.75F, 1.0F };
  for (std::uint32_t face = 0U; face < kFaceCount; ++face) {
    for (const auto u : coordinates) {
      for (const auto v : coordinates) {
        const auto direction = Direction(face, { u, v });
        inputs.push_back({
          .direction = { static_cast<float>(direction.x), static_cast<float>(direction.y),
            static_cast<float>(direction.z), },
          .face = face,
          .uv = { u, v },
        });
      }
    }
  }
  for (const auto x : { -1.0F, 0.0F, 1.0F }) {
    for (const auto y : { -1.0F, 0.0F, 1.0F }) {
      for (const auto z : { -1.0F, 0.0F, 1.0F }) {
        if (x != 0.0F || y != 0.0F || z != 0.0F) {
          inputs.push_back({ .direction = { x, y, z } });
        }
      }
    }
  }
  constexpr auto adjacent = 1.0F + std::numeric_limits<float>::epsilon();
  inputs.push_back({ .direction = { 1.0F, adjacent, 1.0F } });
  inputs.push_back({ .direction = { 1.0F, 1.0F, adjacent } });
  inputs.push_back({ .direction = { -adjacent, -1.0F, -1.0F } });
  const auto result = Decode({
    .records = std::as_bytes(std::span(inputs)),
    .stride = sizeof(GeometryInput),
    .record_kind = kGeometryProbe,
    .decoded_words = kResultWords,
    .count = static_cast<std::uint32_t>(inputs.size()),
    .prepare = {},
  });
  ASSERT_EQ(result.size(), inputs.size() * kResultWords);
  for (std::size_t i = 0U; i < inputs.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& input = inputs.at(i);
    const auto offset = i * kResultWords;
    const auto expected
      = Direction(input.face, { input.uv.at(0), input.uv.at(1) });
    EXPECT_NEAR(Float(result.at(offset)), expected.x, kCoordinateTolerance);
    EXPECT_NEAR(
      Float(result.at(offset + 1U)), expected.y, kCoordinateTolerance);
    EXPECT_NEAR(
      Float(result.at(offset + 2U)), expected.z, kCoordinateTolerance);
    const auto source = glm::dvec3(
      input.direction.at(0), input.direction.at(1), input.direction.at(2));
    const auto inverse = FaceCoordinates(source);
    EXPECT_EQ(result.at(offset + 3U), inverse.face);
    EXPECT_NEAR(Float(result.at(offset + kUvWord)), inverse.uv.at(0),
      kCoordinateTolerance);
    EXPECT_NEAR(Float(result.at(offset + kUvWord + 1U)), inverse.uv.at(1),
      kCoordinateTolerance);
    const auto roundtrip = Direction(result.at(offset + 3U),
      {
        Float(result.at(offset + kUvWord)),
        Float(result.at(offset + kUvWord + 1U)),
      });
    EXPECT_NEAR(
      glm::dot(roundtrip, glm::normalize(source)), 1.0, kCoordinateTolerance);
    EXPECT_FLOAT_EQ(
      Float(result.at(offset + kCubeDirectionWord)), input.direction.at(0));
    EXPECT_FLOAT_EQ(Float(result.at(offset + kCubeDirectionWord + 1U)),
      input.direction.at(2));
    EXPECT_FLOAT_EQ(Float(result.at(offset + kCubeDirectionWord + 2U)),
      -input.direction.at(1));
    for (std::size_t component = 0; component < input.direction.size();
      ++component) {
      EXPECT_FLOAT_EQ(
        Float(result.at(offset + kRestoredDirectionWord + component)),
        input.direction.at(component));
    }
  }
}

NOLINT_TEST_F(
  LightingGpuAbiTest, CubemapGeometrySolidAnglesMatchSphericalTriangles)
{
  for (const auto size : { 1U, 2U, 8U, 32U }) {
    SCOPED_TRACE(size);
    auto inputs = std::vector<GeometryInput> {};
    for (std::uint32_t y = 0; y < size; ++y) {
      for (std::uint32_t x = 0; x < size; ++x) {
        inputs.push_back({ .pixel = { x, y }, .size = size });
      }
    }
    const auto result = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(GeometryInput),
      .record_kind = kGeometryProbe,
      .decoded_words = kResultWords,
      .count = static_cast<std::uint32_t>(inputs.size()),
      .prepare = {},
    });
    ASSERT_EQ(result.size(), inputs.size() * kResultWords);
    double sum = 0.0;
    constexpr auto kSolidAngleWord = 6U;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      const auto& input = inputs.at(i);
      const auto area = Float(result.at((i * kResultWords) + kSolidAngleWord));
      EXPECT_GT(area, 0.0F);
      EXPECT_NEAR(area, TexelArea(input.pixel, size), kTexelTolerance);
      sum += area;
    }
    EXPECT_NEAR(sum * kFaceCount, 4.0 * std::numbers::pi, kSphereTolerance);
  }
}
} // namespace oxygen::vortex::testing
