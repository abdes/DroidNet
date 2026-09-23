//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Vortex/Lighting/Internal/BrdfEnergyData.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {

struct GgxEnergyLookup {
  GgxMomentEstimate moments {};
  double filtering_error_bound { 0.0 };
};

//! CPU interpolation of the shipped texels verifies native sampling separately
//! from the independent numerical-integrator comparison.
inline auto SampleProductionGgxEnergy(double roughness, double cosine)
  -> GgxEnergyLookup
{
  const auto data = lighting::internal::GetBrdfEnergyData();
  if (!data) throw std::runtime_error("Missing energy table in native test");
  const auto x = std::sqrt(std::clamp(cosine, 0.0, 1.0)) * (data->view_nodes - 1U);
  const auto y = std::clamp((roughness - 0.045) / 0.955, 0.0, 1.0)
    * (data->roughness_nodes - 1U);
  const auto ix = static_cast<std::uint32_t>(x);
  const auto iy = static_cast<std::uint32_t>(y);
  const auto nx = std::min(ix + 1U, data->view_nodes - 1U);
  const auto ny = std::min(iy + 1U, data->roughness_nodes - 1U);
  const auto fetch = [&](std::uint32_t row, std::uint32_t column) {
    std::array<float, 2> value {};
    std::memcpy(value.data(), data->energy.data()
        + (row * data->view_nodes + column) * sizeof(value), sizeof(value));
    return value;
  };
  const auto a = fetch(iy, ix), b = fetch(iy, nx);
  const auto c = fetch(ny, ix), d = fetch(ny, nx);
  auto values = std::array<double, 2> {};
  double bound = 0.0;
  for (std::size_t channel = 0; channel < values.size(); ++channel) {
    values.at(channel) = std::lerp(
      std::lerp(static_cast<double>(a.at(channel)), static_cast<double>(b.at(channel)), x - ix),
      std::lerp(static_cast<double>(c.at(channel)), static_cast<double>(d.at(channel)), x - ix), y - iy);
    const auto dx = std::max(std::abs(b.at(channel) - a.at(channel)), std::abs(d.at(channel) - c.at(channel)));
    const auto dy = std::max(std::abs(c.at(channel) - a.at(channel)), std::abs(d.at(channel) - b.at(channel)));
    // D3D filtering weights have at least eight fractional bits.
    bound = std::max(bound, (dx + dy) / 256.0 + 2.0e-6);
  }
  return { .moments = { .directional_albedo = values.at(0), .schlick_moment = values.at(1) },
    .filtering_error_bound = bound };
}

} // namespace oxygen::vortex::testing::reference
