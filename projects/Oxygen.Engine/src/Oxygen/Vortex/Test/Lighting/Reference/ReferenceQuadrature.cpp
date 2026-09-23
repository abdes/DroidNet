//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

#include <Oxygen/Vortex/Test/Lighting/Reference/ReferenceQuadrature.h>

namespace oxygen::vortex::testing::reference::detail {

constexpr auto kPi = std::numbers::pi_v<double>;

// Gauss-Legendre on [0, pi/2]. The rule has no endpoint samples.
auto AngularRule(const std::uint32_t order) -> const std::vector<AngularNode>&
{
  thread_local auto rules
    = std::map<std::uint32_t, std::vector<AngularNode>> {};
  if (const auto found = rules.find(order); found != rules.end()) {
    return found->second;
  }
  auto nodes = std::vector<AngularNode> {};
  nodes.reserve(order);
  for (std::uint32_t index = 0U; index < order; ++index) {
    double root = std::cos(kPi * (static_cast<double>(index) + 0.75)
      / (static_cast<double>(order) + 0.5));
    double derivative = 0.0;
    for (unsigned iteration = 0U; iteration < 32U; ++iteration) {
      double polynomial = 1.0;
      double previous = 0.0;
      for (std::uint32_t degree = 1U; degree <= order; ++degree) {
        const double older = previous;
        previous = polynomial;
        polynomial = ((((2.0 * degree) - 1.0) * root * previous)
                       - ((degree - 1.0) * older))
          / degree;
      }
      derivative
        = order * ((root * polynomial) - previous) / ((root * root) - 1.0);
      const double step = polynomial / derivative;
      root -= step;
      if (std::abs(step) <= 4.0 * std::numeric_limits<double>::epsilon()) {
        break;
      }
    }
    const double angle = (root + 1.0) * kPi / 4.0;
    const double weight
      = (kPi / 2.0) / ((1.0 - (root * root)) * derivative * derivative);
    nodes.push_back(
      { .angle = angle, .cosine = std::cos(angle), .weight = weight });
  }
  return rules.emplace(order, std::move(nodes)).first->second;
}

} // namespace oxygen::vortex::testing::reference::detail
