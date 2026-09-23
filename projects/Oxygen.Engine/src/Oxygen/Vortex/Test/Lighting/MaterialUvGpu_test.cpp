//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include <glm/ext/vector_float2.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialEvaluation.h>

namespace oxygen::vortex::testing {
namespace {
  struct MaterialUvInput {
    glm::vec2 uv { 0.0F };
    glm::vec2 scale { 1.0F };
    glm::vec2 offset { 0.0F };
    float rotation_radians { 0.0F };
    std::uint32_t reserved { 0U };
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(MaterialUvInput) == 32U);
  static_assert(offsetof(MaterialUvInput, uv) == 0U);
  static_assert(offsetof(MaterialUvInput, scale) == 8U);
  static_assert(offsetof(MaterialUvInput, offset) == 16U);
  static_assert(offsetof(MaterialUvInput, rotation_radians) == 24U);
  static_assert(offsetof(MaterialUvInput, reserved) == 28U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(
    LightingGpuAbiTest, MaterialUvTransformMatchesIndependentReference)
  {
    auto inputs = std::vector<MaterialUvInput> {};
    for (const auto uv : {
           glm::vec2 { 0.0F },
           glm::vec2 { -0.5F, 0.75F },
           glm::vec2 { 0.25F, 0.5F },
           glm::vec2 { 8.0F, -7.0F },
         }) {
      for (const auto scale : {
             glm::vec2 { 1.0F },
             glm::vec2 { 2.0F, -3.0F },
             glm::vec2 { 0.25F, 4.0F },
           }) {
        for (const auto rotation : {
               0.0F,
               std::numbers::pi_v<float> / 2.0F,
               std::numbers::pi_v<float>,
             }) {
          inputs.push_back({
            .uv = uv,
            .scale = scale,
            .offset = { -0.5F, 0.25F },
            .rotation_radians = rotation,
          });
        }
      }
    }
    const auto output = Decode({
      .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(MaterialUvInput),
      .record_kind = 20U,
      .decoded_words = 2U,
      .count = static_cast<std::uint32_t>(inputs.size()),
    });
    ASSERT_EQ(output.size(), inputs.size() * 2U);
    double maximum_error = 0.0;
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      const auto& input = inputs.at(index);
      const auto expected
        = reference::TransformMaterialUv({ .u = input.uv.x, .v = input.uv.y },
          {
            .scale = { input.scale.x, input.scale.y },
            .rotation = reference::UvRotationRadians { input.rotation_radians },
            .offset = { .u = input.offset.x, .v = input.offset.y },
          });
      ASSERT_TRUE(expected.has_value());
      const auto coordinates = std::array { expected->u, expected->v };
      for (std::size_t component = 0U; component < coordinates.size();
        ++component) {
        const auto actual = static_cast<double>(
          std::bit_cast<float>(output.at((index * 2U) + component)));
        ASSERT_TRUE(std::isfinite(actual));
        const auto error = std::abs(actual - coordinates.at(component));
        EXPECT_LE(
          error, (1.0e-6 * std::abs(coordinates.at(component))) + 2.0e-7);
        maximum_error = std::max(maximum_error, error);
      }
    }
    RecordProperty("uv_cases", inputs.size());
    RecordProperty("maximum_absolute_error", maximum_error);
  }
} // namespace
} // namespace oxygen::vortex::testing
