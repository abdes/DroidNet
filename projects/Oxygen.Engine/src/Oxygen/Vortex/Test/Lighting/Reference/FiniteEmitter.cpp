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
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Vortex/Test/Lighting/Reference/FiniteEmitter.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/Photometry.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/ReferenceQuadrature.h>

namespace oxygen::vortex::testing::reference {
namespace {
  constexpr auto kPi = std::numbers::pi_v<double>;
  constexpr auto kCircle = 2.0 * kPi;
  using LobeResult = std::expected<BrdfLobes, EmitterIntegrationFailure>;

  struct Vector {
    double x { 0.0 };
    double y { 0.0 };
    double z { 0.0 };
  };

  auto Add(const Vector a, const Vector b) -> Vector
  {
    return { .x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z };
  }
  auto Scale(const Vector a, const double scale) -> Vector
  {
    return { .x = a.x * scale, .y = a.y * scale, .z = a.z * scale };
  }
  auto Dot(const Vector a, const Vector b) -> double
  {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
  }
  auto Cross(const Vector a, const Vector b) -> Vector
  {
    return {
      .x = (a.y * b.z) - (a.z * b.y),
      .y = (a.z * b.x) - (a.x * b.z),
      .z = (a.x * b.y) - (a.y * b.x),
    };
  }
  auto Length(const Vector a) -> double { return std::hypot(a.x, a.y, a.z); }
  auto Normalize(const Vector a, const double length) -> Vector
  {
    return { .x = a.x / length, .y = a.y / length, .z = a.z / length };
  }
  auto Basis(const Vector axis) -> std::pair<Vector, Vector>
  {
    const auto helper = std::abs(axis.z) < 0.9
      ? Vector { .x = 0.0, .y = 0.0, .z = 1.0 }
      : Vector { .x = 0.0, .y = 1.0, .z = 0.0 };
    const auto perpendicular = Cross(helper, axis);
    const auto first = Scale(perpendicular, 1.0 / Length(perpendicular));
    return { first, Cross(axis, first) };
  }
  auto Failure(const EmitterIntegrationError reason)
    -> std::unexpected<EmitterIntegrationFailure>
  {
    return std::unexpected(EmitterIntegrationFailure {
      .reason = reason,
      .brdf_error = std::nullopt,
    });
  }
  auto Failure(const PhotometryError error)
    -> std::unexpected<EmitterIntegrationFailure>
  {
    return Failure(error == PhotometryError::kInvalidInput
        ? EmitterIntegrationError::kInvalidInput
        : EmitterIntegrationError::kUnrepresentable);
  }
  auto NonnegativeFinite(const double value) -> bool
  {
    return std::isfinite(value) && value >= 0.0;
  }
  auto Valid(const FiniteEmitter& emitter, const IncidentBrdf& brdf,
    const EmitterIntegrationSettings settings) -> bool
  {
    return brdf && std::isfinite(emitter.center.x)
      && std::isfinite(emitter.center.y) && std::isfinite(emitter.center.z)
      && NonnegativeFinite(emitter.radius.get())
      && NonnegativeFinite(emitter.range.get())
      && NonnegativeFinite(emitter.flux.get())
      && std::isfinite(emitter.compensation.get())
      && std::isfinite(settings.absolute_tolerance)
      && settings.absolute_tolerance > 0.0
      && NonnegativeFinite(settings.relative_tolerance)
      && settings.initial_order >= 4U && settings.maximum_order <= 512U
      && std::has_single_bit(settings.initial_order)
      && std::has_single_bit(settings.maximum_order)
      && settings.initial_order <= settings.maximum_order / 4U;
  }

  struct Arc {
    double start;
    double end;
  };
  struct AngularHalfSpace {
    double constant;
    double cosine;
    double sine;
  };
  struct AngularWindow {
    double phase;
    double half_width;
  };
  auto ClipWindow(const std::vector<Arc>& arcs, const AngularWindow window)
    -> std::vector<Arc>
  {
    auto phase = window.phase;
    if (phase < 0.0) {
      phase += kCircle;
    }
    const auto half_width = window.half_width;
    auto result = std::vector<Arc> {};
    for (const auto shift : { -kCircle, 0.0, kCircle }) {
      const auto start = phase - half_width + shift;
      const auto end = phase + half_width + shift;
      for (const auto arc : arcs) {
        const auto clipped = Arc {
          .start = std::max(arc.start, start),
          .end = std::min(arc.end, end),
        };
        if (clipped.start < clipped.end) {
          result.push_back(clipped);
        }
      }
    }
    return result;
  }

  //! Intersect azimuth arcs with a + b*cos(phi) + c*sin(phi) > 0.
  auto Clip(const std::vector<Arc>& arcs, const AngularHalfSpace constraint)
    -> std::vector<Arc>
  {
    const auto a = constraint.constant;
    const auto b = constraint.cosine;
    const auto c = constraint.sine;
    const auto amplitude = std::hypot(b, c);
    if (amplitude == 0.0) {
      return a > 0.0 ? arcs : std::vector<Arc> {};
    }
    if (a >= amplitude) {
      return arcs;
    }
    if (a <= -amplitude) {
      return {};
    }
    auto phase = std::atan2(c, b);
    if (phase < 0.0) {
      phase += kCircle;
    }
    const auto half_width = std::acos(std::clamp(-a / amplitude, -1.0, 1.0));
    return ClipWindow(arcs, { .phase = phase, .half_width = half_width });
  }

  struct CircleSupport {
    double center_distance;
    double radius;
    double phase;
  };
  auto ClipCircle(const std::vector<Arc>& arcs, const CircleSupport support,
    const double ring_radius) -> std::vector<Arc>
  {
    const auto p = support.center_distance;
    const auto b = support.radius;
    if (b >= p + ring_radius) {
      return arcs;
    }
    const auto difference = std::abs(p - ring_radius);
    if (b <= difference || p == 0.0 || ring_radius == 0.0) {
      return {};
    }
    // (r-p)^2 + 4*r*p*sin(delta/2)^2 < b^2. This retains tiny
    // circle intersections instead of subtracting squared center distances.
    const auto half_sine_squared
      = (b - difference) * (b + difference) / (4.0 * p * ring_radius);
    return ClipWindow(arcs,
      {
        .phase = support.phase,
        .half_width
        = 2.0 * std::asin(std::sqrt(std::clamp(half_sine_squared, 0.0, 1.0))),
      });
  }

  auto Sample(const IncidentBrdf& brdf, const Vector ray,
    const double illumination) -> LobeResult
  {
    if (!NonnegativeFinite(illumination)) {
      return Failure(EmitterIntegrationError::kUnrepresentable);
    }
    const auto distance = Length(ray);
    if (!std::isfinite(distance)) {
      return Failure(EmitterIntegrationError::kUnrepresentable);
    }
    if (illumination == 0.0 || ray.z <= 0.0 || distance == 0.0) {
      return BrdfLobes {};
    }
    const auto direction = Normalize(ray, distance);
    const auto value
      = brdf({ .x = direction.x, .y = direction.y, .z = direction.z });
    if (!value) {
      return std::unexpected(EmitterIntegrationFailure {
        .reason = EmitterIntegrationError::kBrdfEvaluationFailed,
        .brdf_error = value.error(),
      });
    }
    if (!NonnegativeFinite(value->single_scattering)
      || !NonnegativeFinite(value->multiple_scattering)
      || !NonnegativeFinite(value->diffuse)) {
      return Failure(EmitterIntegrationError::kBrdfEvaluationFailed);
    }
    const auto factor = illumination * direction.z;
    const auto result = BrdfLobes {
      .single_scattering = value->single_scattering * factor,
      .multiple_scattering = value->multiple_scattering * factor,
      .diffuse = value->diffuse * factor,
    };
    if (!std::isfinite(result.single_scattering)
      || !std::isfinite(result.multiple_scattering)
      || !std::isfinite(result.diffuse)) {
      return Failure(EmitterIntegrationError::kUnrepresentable);
    }
    return result;
  }

  struct LobeSum {
    detail::Sum single;
    detail::Sum multiple;
    detail::Sum diffuse;
    auto Add(const BrdfLobes& value, const double weight) -> void
    {
      single.Add(value.single_scattering * weight);
      multiple.Add(value.multiple_scattering * weight);
      diffuse.Add(value.diffuse * weight);
    }
    [[nodiscard]] auto Value() const -> BrdfLobes
    {
      return {
        .single_scattering = single.value,
        .multiple_scattering = multiple.value,
        .diffuse = diffuse.value,
      };
    }
  };

  template <typename Evaluate>
  auto Refine(
    const Evaluate& evaluate, const EmitterIntegrationSettings settings)
    -> std::expected<EmitterIntegral, EmitterIntegrationFailure>
  {
    auto previous = EmitterIntegral {};
    std::uint64_t evaluations = 0U;
    unsigned converged = 0U;
    for (auto order = settings.initial_order; order <= settings.maximum_order;
      order *= 2U) {
      const auto result = evaluate(order, evaluations);
      if (!result) {
        auto failure = result.error();
        failure.last_estimate = previous;
        return std::unexpected(failure);
      }
      auto current = EmitterIntegral {
        .radiance = *result,
        .order = order,
        .evaluations = evaluations,
      };
      if (!NonnegativeFinite(result->single_scattering)
        || !NonnegativeFinite(result->multiple_scattering)
        || !NonnegativeFinite(result->diffuse)) {
        return Failure(EmitterIntegrationError::kUnrepresentable);
      }
      if (previous.order != 0U) {
        // No sampled geometric support is not a proof of zero contribution.
        auto passes = evaluations > previous.evaluations;
        const auto compare
          = [&](const double now, const double before) -> double {
          const auto change = 8.0 * std::abs(now - before);
          passes = passes
            && change <= settings.absolute_tolerance
                + (settings.relative_tolerance * std::abs(now));
          return change;
        };
        current.estimated_absolute_change = {
          compare(
            result->single_scattering, previous.radiance.single_scattering),
          compare(
            result->multiple_scattering, previous.radiance.multiple_scattering),
          compare(result->diffuse, previous.radiance.diffuse),
        };
        converged = passes ? converged + 1U : 0U;
        if (converged == 2U) {
          return current;
        }
      }
      previous = current;
    }
    return std::unexpected(EmitterIntegrationFailure {
      .reason = EmitterIntegrationError::kDidNotConverge,
      .brdf_error = std::nullopt,
      .last_estimate = previous,
    });
  }

  auto Punctual(const FiniteEmitter& emitter, const double intensity,
    const IncidentBrdf& brdf, const double angular_weight = 1.0)
    -> std::expected<EmitterIntegral, EmitterIntegrationFailure>
  {
    const auto center = Vector {
      .x = emitter.center.x,
      .y = emitter.center.y,
      .z = emitter.center.z,
    };
    const auto factor = PunctualDistanceFactor(
      DistanceMetres { Length(center) }, emitter.range);
    if (!factor) {
      return Failure(factor.error());
    }
    const auto sample
      = Sample(brdf, center, intensity * angular_weight * *factor);
    if (!sample) {
      return std::unexpected(sample.error());
    }
    return EmitterIntegral { .radiance = *sample, .evaluations = 1U };
  }
} // namespace

auto IntegratePointSphere(const FiniteEmitter& emitter,
  const IncidentBrdf& brdf, const EmitterIntegrationSettings settings)
  -> std::expected<EmitterIntegral, EmitterIntegrationFailure>
{
  if (!Valid(emitter, brdf, settings)) {
    return Failure(EmitterIntegrationError::kInvalidInput);
  }
  if (emitter.range.get() == 0.0) {
    return EmitterIntegral {};
  }
  const auto intensity
    = ResolvePointIntensity(emitter.flux, emitter.compensation);
  if (!intensity) {
    return Failure(intensity.error());
  }
  if (intensity->get() == 0.0) {
    return EmitterIntegral {};
  }
  if (emitter.radius.get() == 0.0) {
    return Punctual(emitter, intensity->get(), brdf);
  }
  const auto center = Vector {
    .x = emitter.center.x,
    .y = emitter.center.y,
    .z = emitter.center.z,
  };
  const auto distance = Length(center);
  const auto radius = emitter.radius.get();
  if (!std::isfinite(distance)) {
    return Failure(EmitterIntegrationError::kUnrepresentable);
  }
  if (distance <= radius || center.z + radius <= 0.0
    || emitter.range.get() <= distance - radius) {
    return EmitterIntegral {};
  }
  const auto axis = Normalize(center, distance);
  const auto [first, second] = Basis(axis);
  const auto ratio = radius / distance;
  const auto ratio_squared = ratio * ratio;
  if (ratio_squared == 0.0) {
    return Failure(EmitterIntegrationError::kUnrepresentable);
  }
  // Apparent sphere cap: sin(theta)^2=(a/R)^2*u. Its Jacobian cancels a^2
  // against emitted radiance analytically, retaining the positive-radius limit.
  const auto minimum = center.z < 0.0 ? std::pow(-center.z / radius, 2) : 0.0;
  double maximum = 1.0;
  const auto tangent_distance
    = std::sqrt(distance - radius) * std::sqrt(distance + radius);
  if (emitter.range.get() < tangent_distance) {
    const auto q = emitter.range.get() / distance;
    maximum = std::clamp((q - (1.0 - ratio)) * ((1.0 + ratio) - q)
        * ((1.0 + ratio) + q) * ((1.0 - ratio) + q)
        / (4.0 * ratio_squared * q * q),
      0.0, 1.0);
  }
  if (minimum >= maximum) {
    return EmitterIntegral {};
  }
  const auto source_scale = intensity->get() / distance / distance / kCircle;
  return Refine(
    [&](const std::uint32_t order, std::uint64_t& evaluations) -> LobeResult {
      auto sum = LobeSum {};
      for (const auto& radial : detail::AngularRule(order)) {
        const auto u
          = minimum + ((maximum - minimum) * radial.angle * 2.0 / kPi);
        const auto sine = ratio * std::sqrt(u);
        const auto cosine = std::sqrt(1.0 - (ratio_squared * u));
        const auto ray_distance = (distance - radius) * (1.0 + ratio)
          / (cosine + (ratio * std::sqrt(1.0 - u)));
        const auto factor = PunctualDistanceFactor(
          DistanceMetres { ray_distance }, emitter.range);
        if (!factor) {
          return Failure(factor.error());
        }
        const auto illumination
          = source_scale / cosine * ray_distance * ray_distance * *factor;
        const auto arcs = Clip({ { .start = 0.0, .end = kCircle } },
          {
            .constant = axis.z * cosine,
            .cosine = first.z * sine,
            .sine = second.z * sine,
          });
        for (const auto arc : arcs) {
          for (const auto& angular : detail::AngularRule(order)) {
            const auto phi
              = arc.start + ((arc.end - arc.start) * angular.angle * 2.0 / kPi);
            const auto ray = Add(Scale(axis, cosine),
              Add(Scale(first, sine * std::cos(phi)),
                Scale(second, sine * std::sin(phi))));
            ++evaluations;
            const auto sample = Sample(brdf, ray, illumination);
            if (!sample) {
              return std::unexpected(sample.error());
            }
            const auto weight = (maximum - minimum) * (arc.end - arc.start)
              * radial.weight * angular.weight * 4.0 / (kPi * kPi);
            sum.Add(*sample, weight);
          }
        }
      }
      return sum.Value();
    },
    settings);
}

auto IntegrateSpotDisk(const FiniteEmitter& emitter,
  const UnitDirection& emitted_axis, const SpotCone& cone,
  const IncidentBrdf& brdf, const EmitterIntegrationSettings settings)
  -> std::expected<EmitterIntegral, EmitterIntegrationFailure>
{
  if (!Valid(emitter, brdf, settings)) {
    return Failure(EmitterIntegrationError::kInvalidInput);
  }
  const auto axis_input
    = Vector { .x = emitted_axis.x, .y = emitted_axis.y, .z = emitted_axis.z };
  const auto axis_length = Length(axis_input);
  constexpr double kUnitLengthTolerance = 1.0e-12;
  if (!std::isfinite(axis_length)
    || std::abs(axis_length - 1.0) > kUnitLengthTolerance) {
    return Failure(EmitterIntegrationError::kInvalidInput);
  }
  const auto omega = SpotEffectiveSolidAngle(cone);
  if (!omega) {
    return Failure(omega.error());
  }
  if (emitter.range.get() == 0.0) {
    return EmitterIntegral {};
  }
  const auto intensity
    = ResolveSpotPeakIntensity(emitter.flux, cone, emitter.compensation);
  if (!intensity) {
    return Failure(intensity.error());
  }
  if (intensity->get() == 0.0) {
    return EmitterIntegral {};
  }
  const auto axis = Normalize(axis_input, axis_length);
  const auto center = Vector {
    .x = emitter.center.x,
    .y = emitter.center.y,
    .z = emitter.center.z,
  };
  const auto height = -Dot(center, axis);
  if (height <= 0.0) {
    return EmitterIntegral {};
  }
  const auto radius = emitter.radius.get();
  const auto angular_weight
    = [&](const Vector ray) -> std::expected<double, PhotometryError> {
    return SpotAngularWeight(cone,
      OffAxisAngleRadians {
        std::atan2(Length(Cross(axis, ray)), -Dot(axis, ray)) });
  };
  if (radius == 0.0) {
    const auto weight = angular_weight(center);
    if (!weight) {
      return Failure(weight.error());
    }
    return Punctual(emitter, intensity->get(), brdf, *weight);
  }
  const auto [first, second] = Basis(axis);
  const auto normal_extent = std::hypot(first.z, second.z);
  if (center.z + (radius * normal_extent) <= 0.0) {
    return EmitterIntegral {};
  }
  const auto distance = Length(center);
  if (!std::isfinite(distance) || !std::isfinite(distance + radius)) {
    return Failure(EmitterIntegrationError::kUnrepresentable);
  }
  const auto range = emitter.range.get();
  if (range <= height) {
    return EmitterIntegral {};
  }
  const auto range_radius
    = range * std::sqrt((range - height) / range * (1.0 + (height / range)));
  const auto support_radius = cone.outer.get() == kPi / 2.0
    ? range_radius
    : std::min(range_radius, height * std::tan(cone.outer.get()));
  if (support_radius == 0.0) {
    return Failure(EmitterIntegrationError::kUnrepresentable);
  }
  const auto projected_distance = Length(Add(center, Scale(axis, height)));
  if (projected_distance > radius
    && projected_distance - radius >= support_radius) {
    return EmitterIntegral {};
  }
  double minimum = 0.0;
  double maximum = 1.0;
  const auto clipped_distance = support_radius < projected_distance + radius;
  if (clipped_distance) {
    minimum = std::max(0.0, (projected_distance - support_radius) / radius);
    maximum = std::min(1.0, (projected_distance + support_radius) / radius);
    // Overlap exists geometrically; a collapsed numerical interval is not zero
    // light.
    if (minimum >= maximum) {
      return Failure(EmitterIntegrationError::kUnrepresentable);
    }
  }
  const auto support = CircleSupport {
    .center_distance = projected_distance,
    .radius = support_radius,
    .phase = std::atan2(-Dot(center, second), -Dot(center, first)),
  };
  if (center.z < 0.0) {
    minimum = std::max(minimum, -center.z / (radius * normal_extent));
  }
  if (minimum >= maximum) {
    return EmitterIntegral {};
  }
  const auto geometry_scale = std::max(distance, radius);
  const auto scaled_center = Scale(center, 1.0 / geometry_scale);
  return Refine(
    [&](const std::uint32_t order, std::uint64_t& evaluations) -> LobeResult {
      auto sum = LobeSum {};
      for (const auto& radial : detail::AngularRule(order)) {
        const auto rho
          = minimum + ((maximum - minimum) * radial.angle * 2.0 / kPi);
        const auto scaled_radius = radius * rho / geometry_scale;
        auto arcs = Clip({ { .start = 0.0, .end = kCircle } },
          {
            .constant = scaled_center.z,
            .cosine = scaled_radius * first.z,
            .sine = scaled_radius * second.z,
          });
        if (clipped_distance) {
          arcs = ClipCircle(arcs, support, radius * rho);
        }
        for (const auto arc : arcs) {
          for (const auto& angular : detail::AngularRule(order)) {
            const auto phi
              = arc.start + ((arc.end - arc.start) * angular.angle * 2.0 / kPi);
            const auto ray = Add(center,
              Add(Scale(first, radius * rho * std::cos(phi)),
                Scale(second, radius * rho * std::sin(phi))));
            const auto factor = PunctualDistanceFactor(
              DistanceMetres { Length(ray) }, emitter.range);
            if (!factor) {
              return Failure(factor.error());
            }
            const auto profile = angular_weight(ray);
            if (!profile) {
              return Failure(profile.error());
            }
            ++evaluations;
            const auto sample
              = Sample(brdf, ray, intensity->get() / kPi * *profile * *factor);
            if (!sample) {
              return std::unexpected(sample.error());
            }
            const auto weight = rho * (maximum - minimum)
              * (arc.end - arc.start) * radial.weight * angular.weight * 4.0
              / (kPi * kPi);
            sum.Add(*sample, weight);
          }
        }
      }
      return sum.Value();
    },
    settings);
}

} // namespace oxygen::vortex::testing::reference
