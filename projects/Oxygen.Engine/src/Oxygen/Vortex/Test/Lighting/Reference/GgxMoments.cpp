//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {
namespace {
  constexpr double kMinimumRoughness = 0.045;
  constexpr std::uint32_t kMaximumOrder = 4096U;
  constexpr double kChangeSafetyFactor = 8.0;
  constexpr auto kPi = std::numbers::pi_v<double>;

  struct AngularNode {
    double angle;
    double cosine;
    double weight;
  };

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

  struct MomentConfiguration {
    double alpha;
    double view_cosine;
  };

  auto Evaluate(const MomentConfiguration configuration,
    const std::uint32_t order) -> GgxMomentEstimate
  {
    const auto alpha = configuration.alpha;
    const auto mu = configuration.view_cosine;
    const auto& rule = AngularRule(order);
    const double alpha_squared = alpha * alpha;
    const double view_sine = std::sqrt((1.0 - mu) * (1.0 + mu));
    const double view_root
      = std::sqrt((mu * mu) + (alpha_squared * (1.0 - (mu * mu))));
    auto energy = Sum {};
    auto fresnel = Sum {};
    std::uint64_t evaluations = 0U;
    for (const auto& azimuth : rule) {
      for (const double sign : { -1.0, 1.0 }) {
        const double tangent_projection = view_sine * azimuth.cosine * sign;
        double maximum_angle = 0.0;
        if (mu == 0.0) {
          maximum_angle = sign > 0.0 ? kPi / 2.0 : 0.0;
        } else {
          // N.l>0 becomes a quadratic bound on tan(theta_h). Rationalize
          // its negative-projection root instead of subtracting close values.
          const double root = std::hypot(tangent_projection, mu);
          const double maximum_tangent = tangent_projection >= 0.0
            ? (root + tangent_projection) / mu
            : mu / (root - tangent_projection);
          maximum_angle = std::atan(maximum_tangent);
        }
        if (maximum_angle == 0.0) {
          continue;
        }
        const double angle_scale = 2.0 * maximum_angle / kPi;
        for (const auto& radial : rule) {
          ++evaluations;
          const double half_angle = radial.angle * angle_scale;
          const double half_cosine = std::cos(half_angle);
          const double half_sine = std::sin(half_angle);
          const double view_half = std::clamp(
            (mu * half_cosine) + (tangent_projection * half_sine), 0.0, 1.0);
          const double light_cosine = (2.0 * view_half * half_cosine) - mu;
          if (light_cosine <= 0.0) {
            continue;
          }
          const double light_root = std::sqrt((light_cosine * light_cosine)
            + (alpha_squared * (1.0 - (light_cosine * light_cosine))));
          const double visibility
            = 0.5 / ((light_cosine * view_root) + (mu * light_root));
          const double distribution_denominator = (half_sine * half_sine)
            + (alpha_squared * half_cosine * half_cosine);
          const double distribution = alpha_squared
            / (kPi * distribution_denominator * distribution_denominator);
          // dOmega_l = 4(v.h)dOmega_h; azimuth reflection contributes factor 2.
          const double weight = 8.0 * view_half * distribution * visibility
            * light_cosine * half_sine * angle_scale * radial.weight
            * azimuth.weight;
          const double schlick = std::pow(1.0 - view_half, 5);
          energy.Add(weight);
          fresnel.Add(weight * schlick);
        }
      }
    }
    return {
      .directional_albedo = energy.value,
      .schlick_moment = fresnel.value,
      .order = order,
      .evaluations = evaluations,
    };
  }
} // namespace

auto IntegrateGgxMoments(const PerceptualRoughness authored_roughness,
  const ViewCosine view, const MomentIntegrationSettings settings)
  -> std::expected<GgxMomentEstimate, MomentIntegrationFailure>
{
  const double perceptual_roughness = authored_roughness.get();
  const double view_cosine = view.get();
  if (!std::isfinite(perceptual_roughness) || perceptual_roughness < 0.0
    || perceptual_roughness > 1.0 || !std::isfinite(view_cosine)
    || view_cosine < 0.0 || view_cosine > 1.0
    || !std::isfinite(settings.refinement_tolerance)
    || settings.refinement_tolerance <= 0.0 || settings.initial_order < 4U
    || settings.maximum_order > kMaximumOrder
    || !std::has_single_bit(settings.initial_order)
    || !std::has_single_bit(settings.maximum_order)
    || settings.initial_order > settings.maximum_order / 4U) {
    return std::unexpected(MomentIntegrationFailure {});
  }
  const double roughness = std::max(perceptual_roughness, kMinimumRoughness);
  const double alpha = roughness * roughness;
  auto previous = Evaluate(
    { .alpha = alpha, .view_cosine = view_cosine }, settings.initial_order);
  auto evaluations = previous.evaluations;
  unsigned converged_refinements = 0U;
  for (auto order = settings.initial_order * 2U;
    order <= settings.maximum_order; order *= 2U) {
    auto current
      = Evaluate({ .alpha = alpha, .view_cosine = view_cosine }, order);
    evaluations += current.evaluations;
    const double change = kChangeSafetyFactor
      * std::max(
        std::abs(current.directional_albedo - previous.directional_albedo),
        std::abs(current.schlick_moment - previous.schlick_moment));
    converged_refinements
      = std::isfinite(change) && change <= settings.refinement_tolerance
      ? converged_refinements + 1U
      : 0U;
    current.estimated_absolute_change = change;
    current.evaluations = evaluations;
    if (converged_refinements == 2U) {
      return current;
    }
    previous = current;
  }
  return std::unexpected(MomentIntegrationFailure {
    .reason = MomentIntegrationError::kDidNotConverge,
    .last_estimate = previous,
  });
}

} // namespace oxygen::vortex::testing::reference
