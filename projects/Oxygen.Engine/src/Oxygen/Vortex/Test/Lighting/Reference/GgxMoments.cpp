//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <numbers>
#include <optional>
#include <span>

#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/ReferenceQuadrature.h>

namespace oxygen::vortex::testing::reference {
namespace {
  constexpr double kMinimumRoughness = 0.045;
  constexpr std::uint32_t kMaximumOrder = 4096U;
  constexpr double kChangeSafetyFactor = 8.0;
  constexpr auto kPi = std::numbers::pi_v<double>;

  using detail::AngularRule;
  using detail::Sum;

  struct MomentConfiguration {
    double alpha;
    double view_cosine;
  };

  struct AngularFeatures {
    double extent { 0.0 };
    double left_scale { 0.0 };
    double right_scale { 0.0 };
  };
  // The roughness floor allows at most five left cuts in [0,pi/2]. Distinct
  // right cuts are bounded by floating-point precision; include endpoints.
  using AngleBreaks
    = StaticVector<double, std::numeric_limits<double>::digits + 8>;
  struct MomentWorkspace {
    AngleBreaks angular_breaks;
    AngleBreaks radial_breaks;
  };

  // Split the same angular domain into geometric intervals around known
  // narrow features. No interval is omitted and no integrand is approximated.
  auto PartitionAngles(const AngularFeatures features, AngleBreaks& breaks)
    -> void
  {
    breaks.clear();
    breaks.push_back(0.0);
    const auto midpoint = features.extent * 0.5;
    // Integer binary exponents cover even subnormal scales without a floating
    // loop counter. Scaling by 2^2 preserves the exact geometric progression.
    constexpr int exponent_span = std::numeric_limits<double>::max_exponent
      - std::numeric_limits<double>::min_exponent
      + std::numeric_limits<double>::digits;
    const auto append = [&](const double scale, const bool right) -> void {
      // Broad features are already resolved by the unsplit Gaussian rule.
      // Its unchanged refinement test still controls convergence.
      if (scale <= 0.0 || scale >= features.extent * 0.25) {
        return;
      }
      for (int exponent = 0; exponent < exponent_span; exponent += 2) {
        const auto offset = std::ldexp(scale, exponent);
        if (offset >= midpoint) {
          break;
        }
        const auto point = right ? features.extent - offset : offset;
        if (point < features.extent) {
          breaks.push_back(point);
        }
      }
    };
    append(features.left_scale, false);
    const auto right_start = breaks.size();
    append(features.right_scale, true);
    // Each side is already monotone and lies in a separate half of the
    // interval. Reverse the right side instead of sorting every sample row.
    if (breaks.size() > right_start + 1U) {
      std::ranges::reverse(std::span(breaks).subspan(right_start));
    }
    breaks.push_back(features.extent);
  }

  auto Evaluate(const MomentConfiguration configuration,
    const std::uint32_t order, MomentWorkspace& workspace) -> GgxMomentEstimate
  {
    const auto alpha = configuration.alpha;
    const auto mu = configuration.view_cosine;
    const auto& rule = AngularRule(order);
    const double alpha_squared = alpha * alpha;
    const bool uniform_ndf = alpha == 1.0;
    const double view_sine = std::sqrt((1.0 - mu) * (1.0 + mu));
    const double view_root
      = std::sqrt((mu * mu) + (alpha_squared * (1.0 - (mu * mu))));
    const auto peak_width = std::atan(alpha);
    auto& angular_breaks = workspace.angular_breaks;
    auto& radial_breaks = workspace.radial_breaks;
    // Around phi=pi/2 the projected view component crosses the scale mu.
    PartitionAngles(
      { .extent = kPi / 2.0, .right_scale = std::atan2(mu, view_sine) },
      angular_breaks);
    auto energy = Sum {};
    auto fresnel = Sum {};
    std::uint64_t evaluations = 0U;
    for (std::size_t angular_part = 1U; angular_part < angular_breaks.size();
      ++angular_part) {
      const auto angular_start = angular_breaks.at(angular_part - 1U);
      const auto angular_scale
        = (angular_breaks.at(angular_part) - angular_start) * 2.0 / kPi;
      for (const auto& azimuth : rule) {
        const auto phi = angular_start + (angular_scale * azimuth.angle);
        const auto azimuth_weight = azimuth.weight * angular_scale;
        const auto cosine_phi = std::cos(phi);
        for (const double sign : { -1.0, 1.0 }) {
          const double tangent_projection = view_sine * cosine_phi * sign;
          const double root = std::hypot(tangent_projection, mu);
          double maximum_angle = 0.0;
          if (mu == 0.0) {
            maximum_angle = sign > 0.0 ? kPi / 2.0 : 0.0;
          } else {
            const double maximum_tangent = tangent_projection >= 0.0
              ? (root + tangent_projection) / mu
              : mu / (root - tangent_projection);
            maximum_angle = std::atan(maximum_tangent);
          }
          if (maximum_angle == 0.0) {
            continue;
          }
          // At the hemisphere boundary |d(N.l)/d(theta_h)|=2*root.
          // Smith visibility changes on N.l ~ mu*alpha/view_root. Form the
          // resulting angular width as bounded ratios to avoid overflow.
          const auto horizon_width
            = mu > 0.0 ? 0.5 * (mu / root) * (alpha / view_root) : 0.0;
          PartitionAngles(
            {
              .extent = maximum_angle,
              .left_scale = peak_width,
              .right_scale = horizon_width,
            },
            radial_breaks);
          for (std::size_t radial_part = 1U; radial_part < radial_breaks.size();
            ++radial_part) {
            const auto radial_start = radial_breaks.at(radial_part - 1U);
            const auto angle_scale
              = (radial_breaks.at(radial_part) - radial_start) * 2.0 / kPi;
            for (const auto& radial : rule) {
              ++evaluations;
              const double half_angle
                = radial_start + (radial.angle * angle_scale);
              const double half_cosine = std::cos(half_angle);
              const double half_sine = std::sin(half_angle);
              const double view_half = std::clamp(
                (mu * half_cosine) + (tangent_projection * half_sine), 0.0,
                1.0);
              const double light_cosine = (2.0 * view_half * half_cosine) - mu;
              if (light_cosine <= 0.0) {
                continue;
              }
              const double light_root = uniform_ndf
                ? 1.0
                : std::sqrt((light_cosine * light_cosine)
                    + (alpha_squared * (1.0 - (light_cosine * light_cosine))));
              const double visibility
                = 0.5 / ((light_cosine * view_root) + (mu * light_root));
              // At alpha=1 the NDF is exactly 1/pi and both Smith roots are
              // exactly one. Integrate the same kernel without redundant
              // trigonometric normalization and square roots.
              double distribution = 1.0 / kPi;
              if (!uniform_ndf) {
                const double distribution_denominator = (half_sine * half_sine)
                  + (alpha_squared * half_cosine * half_cosine);
                distribution = alpha_squared
                  / (kPi * distribution_denominator * distribution_denominator);
              }
              // Reflection gives 4(v.h)dOmega_h; azimuth symmetry gives 2.
              const double weight = 8.0 * view_half * distribution * visibility
                * light_cosine * half_sine * angle_scale * radial.weight
                * azimuth_weight;
              const auto complement = 1.0 - view_half;
              const auto complement_squared = complement * complement;
              const double schlick
                = complement_squared * complement_squared * complement;
              energy.Add(weight);
              fresnel.Add(weight * schlick);
            }
          }
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
  auto workspace = MomentWorkspace {};
  auto previous = Evaluate({ .alpha = alpha, .view_cosine = view_cosine },
    settings.initial_order, workspace);
  auto evaluations = previous.evaluations;
  unsigned converged_refinements = 0U;
  for (auto order = settings.initial_order * 2U;
    order <= settings.maximum_order; order *= 2U) {
    auto current = Evaluate(
      { .alpha = alpha, .view_cosine = view_cosine }, order, workspace);
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

auto IntegrateGgxMeanMoments(const PerceptualRoughness roughness,
  const MeanMomentIntegrationSettings settings)
  -> std::expected<GgxMeanMomentEstimate, MeanMomentIntegrationFailure>
{
  constexpr auto kMaximumMeanOrder = 256U;
  constexpr double kGrazingCutoff = 1.0e-4;
  constexpr double kTailMidpoint = kGrazingCutoff * kGrazingCutoff / 2.0;
  const auto tail_radius
    = std::nextafter(kTailMidpoint, std::numeric_limits<double>::infinity());
  const auto log_span = -std::log(kGrazingCutoff);
  if (!std::isfinite(roughness.get()) || roughness.get() < 0.0
    || roughness.get() > 1.0 || !std::isfinite(settings.refinement_tolerance)
    || settings.refinement_tolerance <= 0.0 || settings.initial_order < 4U
    || settings.maximum_order > kMaximumMeanOrder
    || !std::has_single_bit(settings.initial_order)
    || !std::has_single_bit(settings.maximum_order)
    || settings.initial_order > settings.maximum_order / 4U) {
    return std::unexpected(MeanMomentIntegrationFailure {});
  }
  auto previous = GgxMeanMomentEstimate {};
  auto evaluations = std::uint64_t { 0U };
  unsigned converged_refinements = 0U;
  for (auto order = settings.initial_order; order <= settings.maximum_order;
    order *= 2U) {
    auto energy = Sum {};
    auto fresnel = Sum {};
    // 0<=B<=E<=1 encloses each omitted integral in [0, cutoff^2].
    // Carry its midpoint and explicit radius instead of declaring it zero.
    energy.Add(kTailMidpoint);
    fresnel.Add(kTailMidpoint);
    for (const auto& node : AngularRule(order)) {
      const auto fraction = 2.0 * node.angle / kPi;
      const auto mu = std::exp(-log_span * (1.0 - fraction));
      const auto sample = IntegrateGgxMoments(
        roughness, ViewCosine { mu }, settings.directional);
      if (!sample) {
        return std::unexpected(MeanMomentIntegrationFailure {
          .reason = sample.error().reason,
          .failed_view = ViewCosine { mu },
          .last_estimate = previous,
        });
      }
      // Logarithmic cosine coordinates resolve the smooth/grazing transition.
      // Include the cosine measure 2*mu and dmu=mu*d(log(mu)).
      const auto weight = 4.0 * mu * mu * log_span * node.weight / kPi;
      energy.Add(sample->directional_albedo * weight);
      fresnel.Add(sample->schlick_moment * weight);
      evaluations += sample->evaluations;
    }
    auto current = GgxMeanMomentEstimate {
      .hemispherical_albedo = energy.value,
      .schlick_moment = fresnel.value,
      .endpoint_absolute_bound = tail_radius,
      .order = order,
      .evaluations = evaluations,
    };
    if (previous.order != 0U) {
      current.estimated_absolute_change = kChangeSafetyFactor
        * std::max(std::abs(current.hemispherical_albedo
                     - previous.hemispherical_albedo),
          std::abs(current.schlick_moment - previous.schlick_moment));
      converged_refinements = std::isfinite(current.estimated_absolute_change)
          && current.estimated_absolute_change <= settings.refinement_tolerance
        ? converged_refinements + 1U
        : 0U;
      if (converged_refinements == 2U) {
        return current;
      }
    }
    previous = current;
  }
  return std::unexpected(MeanMomentIntegrationFailure {
    .reason = MomentIntegrationError::kDidNotConverge,
    .failed_view = std::nullopt,
    .last_estimate = previous,
  });
}

} // namespace oxygen::vortex::testing::reference
