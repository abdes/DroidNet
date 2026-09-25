//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>

namespace oxygen::vortex::testing {
namespace {

  using environment::IblProductMetadata;
  using environment::internal::GetIblBrdfLookup;

  NOLINT_TEST_F(
    LightingGpuAbiTest, IblMetadataRejectsIncompleteOrWrongGeneration)
  {
    const auto records = std::array {
      IblProductMetadata {},
      IblProductMetadata { 8.0F, 0.5F, 3U, 77U },
      IblProductMetadata { 8.0F, 0.5F, 1U, 77U },
      IblProductMetadata { 8.0F, 0.5F, 2U, 77U },
      IblProductMetadata { 8.0F, 0.5F, 3U, 76U },
      IblProductMetadata { 8.0F, 0.5F, 3U, 0U },
      IblProductMetadata { 8.0F, 0.5F, 7U, 77U },
      IblProductMetadata {
        std::numeric_limits<float>::infinity(), 0.5F, 3U, 77U },
      IblProductMetadata {
        1.0F, std::numeric_limits<float>::quiet_NaN(), 3U, 77U },
      IblProductMetadata { 0.5F, 0.5F, 3U, 77U },
    };
    const auto result = Decode({
      .records = std::as_bytes(std::span(records)),
      .stride = sizeof(IblProductMetadata),
      .record_kind = 27U,
      .decoded_words = 5U,
      .count = static_cast<std::uint32_t>(records.size()),
    });
    for (auto index = 0U; index < records.size(); ++index) {
      SCOPED_TRACE(index);
      EXPECT_EQ(result[index * 5U],
        std::bit_cast<std::uint32_t>(records[index].source_radiance_scale));
      EXPECT_EQ(result[index * 5U + 2U], records[index].processing_flags);
      EXPECT_EQ(result[index * 5U + 3U], records[index].product_revision);
      EXPECT_EQ(result[index * 5U + 4U], index == 1U ? 1U : 0U);
    }
  }

  NOLINT_TEST_F(LightingGpuAbiTest, IblEnvironmentAbiDecodesAdjacentRecords)
  {
    auto records = std::array<EnvironmentStaticData, 2> {};
    records[1].sky_light.diffuse_sh_slot = 0x01234567U;
    records[1].sky_light.product_metadata_srv = 0x07654321U;
    records[1].sky_sphere.intensity = 9.0F;
    records[1].post_process.exposure_compensation = -3.0F;
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(records)),
                .stride = sizeof(EnvironmentStaticData),
                .record_kind = 28U,
                .decoded_words = 4U,
                .first_element = 1U,
                .count = 1U,
              }),
      (std::vector<std::uint32_t> { 0x01234567U, 0x07654321U,
        std::bit_cast<std::uint32_t>(9.0F),
        std::bit_cast<std::uint32_t>(-3.0F) }));

    auto bindings = std::array<EnvironmentFrameBindings, 2> {};
    bindings[1].probes.product_metadata_srv = ShaderVisibleIndex { 123U };
    bindings[1].evaluation.ambient_intensity = 4.0F;
    bindings[1].ambient_bridge.flags = 0x80000001U;
    EXPECT_EQ(Decode({
                .records = std::as_bytes(std::span(bindings)),
                .stride = sizeof(EnvironmentFrameBindings),
                .record_kind = 29U,
                .decoded_words = 3U,
                .first_element = 1U,
                .count = 1U,
              }),
      (std::vector<std::uint32_t> {
        123U, std::bit_cast<std::uint32_t>(4.0F), 0x80000001U }));
  }

  NOLINT_TEST_F(LightingGpuAbiTest, IblRoughnessMappingAndNearBlackMetalFresnel)
  {
    // Golden M=7 values distinguish the required logarithmic mapping from a
    // linear mip lookup, including clamping and otherwise unused coarse mips.
    const auto records = std::array {
      std::array<float, 4> { 0.0F, 0.0F, 7.0F, 0.0F },
      std::array<float, 4> { 0.1F, 1.0F, 7.0F, 0.001F },
      std::array<float, 4> { 0.5F, 5.0F, 7.0F, 0.04F },
      std::array<float, 4> { 1.0F, 7.0F, 7.0F, 1.0F },
    };
    const auto result = Decode({
      .records = std::as_bytes(std::span(records)),
      .stride = sizeof(records[0]),
      .record_kind = 30U,
      .decoded_words = 3U,
      .count = 4U,
    });
    constexpr auto expected_mips
      = std::array { 0.0F, 1.013686286F, 3.8F, 5.0F };
    constexpr auto expected_roughness
      = std::array { 0.055681169F, 0.099212566F, 1.0F, 3.174802104F };
    constexpr auto expected_specular
      = std::array { 0.0F, 0.0265F, 0.56F, 2.0F };
    for (auto index = 0U; index < records.size(); ++index) {
      SCOPED_TRACE(index);
      EXPECT_NEAR(std::bit_cast<float>(result[index * 3U]),
        expected_mips[index], 2.0e-6F);
      EXPECT_NEAR(std::bit_cast<float>(result[index * 3U + 1U]),
        expected_roughness[index], 2.0e-6F);
      EXPECT_NEAR(std::bit_cast<float>(result[index * 3U + 2U]),
        expected_specular[index], 2.0e-6F);
    }
  }

  // Independent double-precision halfway-vector quadrature. The transform
  // cancels the GGX NDF; integrate only the Smith/Fresnel response and
  // Jacobian.
  auto ReferenceBrdf(const double nv, const double roughness)
    -> std::array<double, 2>
  {
    constexpr auto order = 256U;
    const auto alpha = roughness * roughness;
    auto integral = std::array<double, 2> {};
    for (auto radial = 0U; radial < order; ++radial) {
      const auto u = (static_cast<double>(radial) + 0.5) / order;
      const auto tangent = alpha * std::sqrt(u / (1.0 - u));
      const auto nh = 1.0 / std::sqrt(1.0 + tangent * tangent);
      const auto sh = tangent * nh;
      for (auto azimuth = 0U; azimuth < order; ++azimuth) {
        const auto phi = 2.0 * std::numbers::pi
          * (static_cast<double>(azimuth) + 0.5) / order;
        const auto vh = std::sqrt(1.0 - nv * nv) * sh * std::cos(phi) + nv * nh;
        const auto nl = 2.0 * vh * nh - nv;
        if (nl <= 0.0 || vh <= 0.0) {
          continue;
        }
        const auto smith
          = 2.0 * nv * nl / (2.0 * nv * nl * (1.0 - alpha) + alpha * (nv + nl));
        const auto contribution = smith * vh / (nv * nh);
        const auto fresnel = std::pow(1.0 - vh, 5);
        integral[0] += contribution * (1.0 - fresnel);
        integral[1] += contribution * fresnel;
      }
    }
    integral[0] /= order * order;
    integral[1] /= order * order;
    return integral;
  }

  NOLINT_TEST(IblBrdfLookupTest, GeneratedLookupMatchesIndependentQuadrature)
  {
    const auto lookup = GetIblBrdfLookup();
    EXPECT_EQ(lookup.data(), GetIblBrdfLookup().data());
    for (const auto y : { 0U, 3U, 15U, 31U }) {
      for (const auto x : { 0U, 7U, 31U, 63U, 127U }) {
        SCOPED_TRACE(::testing::Message() << "texel " << x << ", " << y);
        const auto reference
          = ReferenceBrdf((x + 0.5) / 128.0, (y + 0.5) / 32.0);
        const auto& texel = lookup[y * 128U + x];
        EXPECT_NEAR(texel[0] / 65535.0, reference[0], 0.035);
        EXPECT_NEAR(texel[1] / 65535.0, reference[1], 0.035);
      }
    }
  }

  NOLINT_TEST_F(LightingGpuAbiTest, IblBrdfNativeUnormSamplingHasNoSecondRemap)
  {
    const auto lookup = GetIblBrdfLookup();
    auto resources = environment::internal::IblBrdfResources(Backend());
    const auto product = resources.Prepare();
    ASSERT_TRUE(product.has_value());
    const auto texture = (*product)->srv;
    auto records = std::vector<std::array<float, 4>> {};
    auto expected = std::vector<std::array<float, 2>> {};
    for (auto y = 0U; y < 32U; ++y) {
      for (auto x = 0U; x < 128U; ++x) {
        records.push_back({ (x + 0.5F) / 128.0F, (y + 0.5F) / 32.0F,
          std::bit_cast<float>(texture.get()), 0.0F });
        expected.push_back({ lookup[y * 128U + x][0] / 65535.0F,
          lookup[y * 128U + x][1] / 65535.0F });
      }
    }
    // Hardware clamp at the four domain corners, including zero roughness.
    for (const auto y : { 0U, 31U }) {
      for (const auto x : { 0U, 127U }) {
        records.push_back({ x == 0U ? 0.0F : 1.0F, y == 0U ? 0.0F : 1.0F,
          std::bit_cast<float>(texture.get()), 0.0F });
        expected.push_back({ lookup[y * 128U + x][0] / 65535.0F,
          lookup[y * 128U + x][1] / 65535.0F });
      }
    }
    const auto result = Decode({
      .records = std::as_bytes(std::span(records)),
      .stride = sizeof(records[0]),
      .record_kind = 31U,
      .decoded_words = 2U,
      .count = static_cast<std::uint32_t>(records.size()),
      .prepare =
        [&](graphics::CommandRecorder& recorder) {
          ASSERT_TRUE(static_cast<bool>(recorder.RetainRegistration(
            Backend().GetResourceRegistry(), (*product)->registration)));
          recorder.RecordDependency((*product)->producer);
        },
    });
    for (auto index = 0U; index < expected.size(); ++index) {
      SCOPED_TRACE(index);
      for (auto channel = 0U; channel < 2U; ++channel) {
        EXPECT_NEAR(std::bit_cast<float>(result[index * 2U + channel]),
          expected[index][channel], 1.0F / 65535.0F);
      }
    }
  }

} // namespace
} // namespace oxygen::vortex::testing
