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
#include <span>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>

namespace oxygen::vortex::testing {
namespace {
  struct MaterialDecodeInput {
    ShaderVisibleIndex normal_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex material_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex base_color_srv { kInvalidShaderVisibleIndex };
    std::uint32_t pixel_x { 0U };
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(MaterialDecodeInput) == 16U);
  static_assert(offsetof(MaterialDecodeInput, normal_srv) == 0U);
  static_assert(offsetof(MaterialDecodeInput, material_srv) == 4U);
  static_assert(offsetof(MaterialDecodeInput, base_color_srv) == 8U);
  static_assert(offsetof(MaterialDecodeInput, pixel_x) == 12U);
  // NOLINTEND(*-magic-numbers)

  NOLINT_TEST_F(
    LightingGpuAbiTest, NativeGBufferFormatsMatchIndependentMaterialDecoder)
  {
    constexpr std::uint32_t count = 256U;
    constexpr std::uint32_t output_words = 20U;
    auto normals = std::vector<std::uint32_t> {};
    auto materials = std::vector<std::uint32_t> {};
    auto colors = std::vector<std::uint32_t> {};
    const auto normal_landmarks = std::array<std::uint32_t, 8> {
      0U,
      0x000FFFFFU,
      512U | (512U << 10U),
      511U | (511U << 10U),
      767U | (767U << 10U),
      768U | (767U << 10U),
      256U | (256U << 10U),
      255U | (256U << 10U),
    };
    for (std::uint32_t index = 0U; index < count; ++index) {
      const auto normal = index < normal_landmarks.size()
        ? normal_landmarks.at(index)
        : ((index * 47U) & 1023U) | (((index * 149U) & 1023U) << 10U);
      normals.push_back(normal | 0xDAB00000U);
      materials.push_back(index | ((255U - index) << 8U)
        | ((index ^ 0x55U) << 16U) | (index << 24U));
      colors.push_back(index | (((index * 73U) & 255U) << 8U)
        | ((255U - index) << 16U) | (index << 24U));
    }
    const auto normal_srv
      = PublishPackedTexture(Format::kR10G10B10A2UNorm, normals);
    const auto material_srv
      = PublishPackedTexture(Format::kRGBA8UNorm, materials);
    const auto color_srv
      = PublishPackedTexture(Format::kRGBA8UNormSRGB, colors);
    auto requests = std::vector<MaterialDecodeInput> {};
    for (std::uint32_t index = 0U; index < count; ++index) {
      requests.push_back({
        .normal_srv = normal_srv,
        .material_srv = material_srv,
        .base_color_srv = color_srv,
        .pixel_x = index,
      });
    }
    const auto output = Decode({
      .records = std::as_bytes(std::span(requests)),
      .stride = sizeof(MaterialDecodeInput),
      .record_kind = 17U,
      .decoded_words = output_words,
      .count = count,
    });
    ASSERT_EQ(output.size(), static_cast<std::size_t>(count) * output_words);
    double maximum_normal_error = 0.0;
    double maximum_srgb_code_error = 0.0;
    double maximum_linear_color_error = 0.0;
    for (std::uint32_t index = 0U; index < count; ++index) {
      SCOPED_TRACE(index);
      const auto decoded = reference::DecodeGBufferTexel({
        .normal = reference::PackedGBufferNormal { normals.at(index) },
        .material = reference::PackedGBufferMaterial { materials.at(index) },
        .base_color = reference::PackedGBufferBaseColor { colors.at(index) },
      });
      const auto brdf = reference::ResolveMaterialBrdf(decoded.material);
      ASSERT_TRUE(brdf.has_value());
      const auto value = [&](const std::uint32_t lane) -> double {
        return std::bit_cast<float>(
          output.at((static_cast<std::size_t>(index) * output_words) + lane));
      };
      const auto normal
        = std::array { decoded.normal.x, decoded.normal.y, decoded.normal.z };
      for (std::uint32_t component = 0U; component < normal.size();
        ++component) {
        const auto error = std::abs(value(component) - normal.at(component));
        EXPECT_LT(error, 2.0e-6);
        maximum_normal_error = std::max(maximum_normal_error, error);
      }
      EXPECT_NEAR(value(3U), decoded.material.metallic, 1.0e-7);
      EXPECT_NEAR(value(4U), decoded.material.specular, 1.0e-7);
      EXPECT_NEAR(value(5U), decoded.material.roughness.get(), 1.0e-7);
      EXPECT_NEAR(value(6U), decoded.ambient_occlusion, 1.0e-7);
      EXPECT_EQ(
        output.at((static_cast<std::size_t>(index) * output_words) + 7U),
        decoded.shading_model.get());
      const auto base = std::array {
        decoded.material.base_color.red,
        decoded.material.base_color.green,
        decoded.material.base_color.blue,
      };
      const auto channels = std::array { brdf->red, brdf->green, brdf->blue };
      for (std::uint32_t component = 0U; component < base.size(); ++component) {
        const auto gpu_base = value(8U + component);
        ASSERT_GE(gpu_base, 0.0);
        ASSERT_LE(gpu_base, 1.0);
        const auto stored_code = (colors.at(index) >> (component * 8U)) & 255U;
        // D3D SRGB->FLOAT permits 0.5 ULP on the encoded integer side;
        // endpoints must remain exact. This is not a widened BRDF tolerance.
        const auto encoded = gpu_base <= 0.0031308
          ? 12.92 * gpu_base
          : (1.055 * std::pow(gpu_base, 1.0 / 2.4)) - 0.055;
        const auto code_error
          = std::abs((encoded * 255.0) - static_cast<double>(stored_code));
        EXPECT_LE(code_error, 0.5 + 1.0e-6);
        if (stored_code == 0U || stored_code == 255U) {
          EXPECT_EQ(gpu_base, base.at(component));
        }
        maximum_srgb_code_error = std::max(maximum_srgb_code_error, code_error);
        const auto linear_error = std::abs(gpu_base - base.at(component));
        maximum_linear_color_error
          = std::max(maximum_linear_color_error, linear_error);
        EXPECT_NEAR(value(12U + component), channels.at(component).f0,
          (decoded.material.metallic * linear_error) + 1.0e-6);
        EXPECT_NEAR(value(16U + component), channels.at(component).diffuse,
          ((1.0 - decoded.material.metallic) * linear_error) + 1.0e-6);
      }
    }
    ::testing::Test::RecordProperty(
      "maximum_normal_error", maximum_normal_error);
    ::testing::Test::RecordProperty(
      "maximum_srgb_code_error", maximum_srgb_code_error);
    ::testing::Test::RecordProperty(
      "maximum_linear_color_error", maximum_linear_color_error);
  }
} // namespace
} // namespace oxygen::vortex::testing
