//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>

namespace oxygen::vortex::environment::internal {
namespace {

  auto RadicalInverse(std::uint32_t bits) -> float
  {
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 0x1p-32F;
  }

  auto Integrate(const float nv, const float roughness) -> IblBrdfTexel
  {
    const auto alpha = roughness * roughness;
    const auto alpha_squared = alpha * alpha;
    const auto vx = std::sqrt(1.0F - nv * nv);
    auto sum = std::array<float, 2> {};
    for (auto sample = 0U; sample < kIblBrdfSamples; ++sample) {
      const auto azimuth = 2.0F * std::numbers::pi_v<float>
        * static_cast<float>(sample) / static_cast<float>(kIblBrdfSamples);
      const auto sequence = RadicalInverse(sample);
      const auto nh = std::sqrt(
        (1.0F - sequence) / (1.0F + (alpha_squared - 1.0F) * sequence));
      const auto hx
        = std::sqrt((std::max)(0.0F, 1.0F - nh * nh)) * std::cos(azimuth);
      const auto vh = (std::max)(0.0F, vx * hx + nv * nh);
      const auto nl = 2.0F * vh * nh - nv;
      if (nl <= 0.0F) {
        continue;
      }
      // Smith-joint approximation and reflected-half-vector sampling Jacobian.
      const auto visibility_denominator = nl * (nv * (1.0F - alpha) + alpha)
        + nv * (nl * (1.0F - alpha) + alpha);
      const auto weight = 2.0F * nl * vh / (nh * visibility_denominator);
      const auto one_minus_vh = 1.0F - vh;
      const auto squared = one_minus_vh * one_minus_vh;
      const auto fresnel = squared * squared * one_minus_vh;
      sum[0] += weight * (1.0F - fresnel);
      sum[1] += weight * fresnel;
    }
    auto result = IblBrdfTexel {};
    for (auto channel = 0U; channel < result.size(); ++channel) {
      const auto value = std::clamp(
        sum[channel] / static_cast<float>(kIblBrdfSamples), 0.0F, 1.0F);
      result[channel] = static_cast<std::uint16_t>(value * 65535.0F + 0.5F);
    }
    return result;
  }

} // namespace

auto GetIblBrdfLookup()
  -> std::span<const IblBrdfTexel, kIblBrdfWidth * kIblBrdfHeight>
{
  static const auto lookup = [] {
    auto result = std::array<IblBrdfTexel, kIblBrdfWidth * kIblBrdfHeight> {};
    for (auto y = 0U; y < kIblBrdfHeight; ++y) {
      for (auto x = 0U; x < kIblBrdfWidth; ++x) {
        result[y * kIblBrdfWidth + x] = Integrate(
          (static_cast<float>(x) + 0.5F) / static_cast<float>(kIblBrdfWidth),
          (static_cast<float>(y) + 0.5F) / static_cast<float>(kIblBrdfHeight));
      }
    }
    return result;
  }();
  return lookup;
}

} // namespace oxygen::vortex::environment::internal
