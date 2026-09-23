//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

namespace oxygen::vortex::testing::reference::detail {

struct AngularNode {
  double angle;
  double cosine;
  double weight;
};

//! Cached power-of-two quadrature rule on [0, pi/2]; callers bound its order.
auto AngularRule(std::uint32_t order) -> const std::vector<AngularNode>&;

// Compensated summation keeps small Fresnel moments independent of order.
struct Sum {
  double value { 0.0 };
  double correction { 0.0 };
  auto Add(const double term) -> void
  {
    const double adjusted = term - correction;
    const double next = value + adjusted;
    correction = (next - value) - adjusted;
    value = next;
  }
};

} // namespace oxygen::vortex::testing::reference::detail
