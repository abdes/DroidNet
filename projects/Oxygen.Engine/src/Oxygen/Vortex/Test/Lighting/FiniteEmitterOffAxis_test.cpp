//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <numbers>
#include <optional>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/FiniteEmitter.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/Photometry.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/ReferenceQuadrature.h>

namespace oxygen::vortex::testing::reference {
namespace {
  constexpr auto kPi = std::numbers::pi_v<double>;
  constexpr double kDistanceGuardSquared = 1.0e-6;
  constexpr double kSpecularReflectance = 0.04;

  auto ControlledLobes(const UnitDirection& direction,
    const PerceptualRoughness roughness, const ViewCosine view)
    -> std::expected<BrdfLobes, BrdfReferenceError>
  {
    const auto single = EvaluateGgxSingleScatteringChannel(
      {
        .roughness = roughness,
        .light = LightCosine { direction.z },
        .view = view,
        .azimuth = RelativeAzimuth { std::atan2(direction.y, direction.x) },
      },
      kSpecularReflectance);
    if (!single) {
      return std::unexpected(single.error());
    }
    return BrdfLobes { .single_scattering = *single, .diffuse = 1.0 / kPi };
  }

  struct AngleInterval {
    double lower { -kPi / 2.0 };
    double upper { kPi / 2.0 };
  };
  auto AngleBreaks(const std::optional<double> normalized_peak,
    const AngleInterval limits = {}) -> std::vector<double>
  {
    auto result = std::vector<double> { limits.lower };
    if (normalized_peak && std::abs(*normalized_peak) < 1.0) {
      const auto angle = std::asin(*normalized_peak);
      if (angle > limits.lower && angle < limits.upper) {
        result.push_back(angle);
      }
    }
    result.push_back(limits.upper);
    return result;
  }

  auto Dot(const UnitDirection& a, const UnitDirection& b) -> double
  {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
  }
  struct AzimuthEquation {
    double constant;
    double cosine;
    double sine;
  };
  auto PartitionViewPlane(std::vector<double> breaks,
    const AzimuthEquation equation) -> std::vector<double>
  {
    const auto amplitude = std::hypot(equation.cosine, equation.sine);
    if (amplitude == 0.0 || std::abs(equation.constant) > amplitude) {
      return breaks;
    }
    const auto phase = std::atan2(equation.sine, equation.cosine);
    const auto angle
      = std::acos(std::clamp(-equation.constant / amplitude, -1.0, 1.0));
    for (const auto sign : { -1.0, 1.0 }) {
      auto root = phase + (sign * angle);
      if (root < 0.0) {
        root += 2.0 * kPi;
      }
      if (root > 0.0 && root < 2.0 * kPi) {
        breaks.push_back(root);
      }
    }
    std::ranges::sort(breaks);
    breaks.erase(std::ranges::unique(breaks).begin(), breaks.end());
    return breaks;
  }

  auto SphereSurface(const FiniteEmitter& source, const IncidentBrdf& brdf,
    const ViewCosine view, const std::uint32_t order)
    -> std::expected<BrdfLobes, BrdfReferenceError>
  {
    const auto radius = source.radius.get();
    const auto distance
      = std::hypot(source.center.x, source.center.y, source.center.z);
    const auto axis = UnitDirection {
      .x = -source.center.x / distance,
      .y = -source.center.y / distance,
      .z = -source.center.z / distance,
    };
    auto first = std::abs(axis.z) < 0.9
      ? UnitDirection { .x = -axis.y, .y = axis.x, .z = 0.0 }
      : UnitDirection { .x = 0.0, .y = -axis.z, .z = axis.y };
    const auto first_length = std::hypot(first.x, first.y, first.z);
    first.x /= first_length;
    first.y /= first_length;
    first.z /= first_length;
    const auto second = UnitDirection {
      .x = (axis.y * first.z) - (axis.z * first.y),
      .y = (axis.z * first.x) - (axis.x * first.z),
      .z = (axis.x * first.y) - (axis.y * first.x),
    };
    auto cosine_breaks = std::vector<double> { radius / distance };
    auto phi_breaks = std::vector<double> { 0.0 };
    const auto mirror = UnitDirection {
      .x = -std::sqrt((1.0 - view.get()) * (1.0 + view.get())),
      .y = 0.0,
      .z = view.get(),
    };
    const auto projection = (source.center.x * mirror.x)
      + (source.center.y * mirror.y) + (source.center.z * mirror.z);
    const auto discriminant
      = (radius * radius) - (distance * distance) + (projection * projection);
    if (projection > 0.0 && discriminant > 0.0) {
      const auto hit_distance = projection - std::sqrt(discriminant);
      const auto normal = UnitDirection {
        .x = ((hit_distance * mirror.x) - source.center.x) / radius,
        .y = ((hit_distance * mirror.y) - source.center.y) / radius,
        .z = ((hit_distance * mirror.z) - source.center.z) / radius,
      };
      const auto cosine = Dot(normal, axis);
      if (cosine > cosine_breaks.front() && cosine < 1.0) {
        cosine_breaks.push_back(cosine);
      }
    }
    auto phase = std::atan2(Dot(mirror, second), Dot(mirror, first));
    if (phase < 0.0) {
      phase += 2.0 * kPi;
    }
    if (phase > 0.0) {
      phi_breaks.push_back(phase);
    }
    cosine_breaks.push_back(1.0);
    phi_breaks.push_back(2.0 * kPi);
    auto single = detail::Sum {};
    auto multiple = detail::Sum {};
    auto diffuse = detail::Sum {};
    // Integrate actual emitting surface normals. The visible cap satisfies
    // n_y.dot(-center/R)>a/R; the emission cosine remains explicit.
    const auto emission = source.flux.get()
      * std::exp2(source.compensation.get()) / (4.0 * kPi * kPi);
    for (std::size_t radial = 1U; radial < cosine_breaks.size(); ++radial) {
      const auto start = cosine_breaks.at(radial - 1U);
      const auto radial_scale = (cosine_breaks.at(radial) - start) * 2.0 / kPi;
      for (const auto& radial_node : detail::AngularRule(order)) {
        const auto cosine = start + (radial_scale * radial_node.angle);
        const auto sine = std::sqrt((1.0 - cosine) * (1.0 + cosine));
        // Views lie in the receiver x/z plane. Partition surface-normal rows
        // at y=0, independently of the apparent-cap parameterization.
        const auto row_breaks = PartitionViewPlane(phi_breaks,
          {
            .constant = source.center.y + (radius * cosine * axis.y),
            .cosine = radius * sine * first.y,
            .sine = radius * sine * second.y,
          });
        for (std::size_t angular = 1U; angular < row_breaks.size(); ++angular) {
          const auto phi_start = row_breaks.at(angular - 1U);
          const auto phi_scale
            = (row_breaks.at(angular) - phi_start) * 2.0 / kPi;
          for (const auto& phi_node : detail::AngularRule(order)) {
            const auto phi = phi_start + (phi_scale * phi_node.angle);
            const auto cosine_phi = std::cos(phi);
            const auto sine_phi = std::sin(phi);
            const auto normal = UnitDirection {
              .x = (cosine * axis.x)
                + (sine * ((cosine_phi * first.x) + (sine_phi * second.x))),
              .y = (cosine * axis.y)
                + (sine * ((cosine_phi * first.y) + (sine_phi * second.y))),
              .z = (cosine * axis.z)
                + (sine * ((cosine_phi * first.z) + (sine_phi * second.z))),
            };
            const auto x = source.center.x + (radius * normal.x);
            const auto y = source.center.y + (radius * normal.y);
            const auto z = source.center.z + (radius * normal.z);
            const auto ray_distance = std::hypot(x, y, z);
            if (z <= 0.0 || ray_distance >= source.range.get()) {
              continue;
            }
            const auto ray = UnitDirection {
              .x = x / ray_distance,
              .y = y / ray_distance,
              .z = z / ray_distance,
            };
            const auto lobes = brdf(ray);
            if (!lobes) {
              return std::unexpected(lobes.error());
            }
            const auto window = std::pow(
              1.0 - std::pow(ray_distance / source.range.get(), 4), 2);
            const auto emission_cosine = std::max(0.0, -Dot(normal, ray));
            const auto weight = emission * ray.z * emission_cosine * window
              / std::max(ray_distance * ray_distance, kDistanceGuardSquared)
              * radial_scale * phi_scale * radial_node.weight * phi_node.weight;
            single.Add(lobes->single_scattering * weight);
            multiple.Add(lobes->multiple_scattering * weight);
            diffuse.Add(lobes->diffuse * weight);
          }
        }
      }
    }
    return BrdfLobes {
      .single_scattering = single.value,
      .multiple_scattering = multiple.value,
      .diffuse = diffuse.value,
    };
  }

  auto CartesianDisk(const FiniteEmitter& source, const UnitDirection& axis,
    const SpotCone& cone, const IncidentBrdf& brdf, const ViewCosine view,
    const std::uint32_t order) -> std::expected<BrdfLobes, BrdfReferenceError>
  {
    const auto radius = source.radius.get();
    const auto height = -((source.center.x * axis.x)
      + (source.center.y * axis.y) + (source.center.z * axis.z));
    if (height <= 0.0 || source.range.get() <= height) {
      return BrdfLobes {};
    }
    const auto horizontal = std::hypot(axis.x, axis.y);
    const auto first = horizontal > 0.0
      ? UnitDirection { .x = -axis.y / horizontal,
          .y = axis.x / horizontal,
          .z = 0.0, }
      : UnitDirection { .x = 1.0, .y = 0.0, .z = 0.0 };
    const auto second = UnitDirection {
      .x = -axis.z * first.y,
      .y = axis.z * first.x,
      .z = (axis.x * first.y) - (axis.y * first.x),
    };
    // first.z is zero, making the receiver-horizon restriction monotone in psi.
    const auto center_u
      = (source.center.x * first.x) + (source.center.y * first.y);
    const auto center_v = (source.center.x * second.x)
      + (source.center.y * second.y) + (source.center.z * second.z);
    const auto inner_cosine = std::cos(cone.inner.get());
    const auto outer_cosine
      = cone.outer.get() == kPi / 2.0 ? 0.0 : std::cos(cone.outer.get());
    const auto solid_angle = 2.0 * kPi
      * ((1.0 - inner_cosine) + ((inner_cosine - outer_cosine) / 3.0));
    const auto peak
      = source.flux.get() * std::exp2(source.compensation.get()) / solid_angle;
    const auto range = source.range.get();
    const auto range_radius = std::sqrt((range - height) * (range + height));
    const auto support = cone.outer.get() == kPi / 2.0
      ? range_radius
      : std::min(range_radius, height * std::tan(cone.outer.get()));
    const auto lower_x = std::max(-1.0, (-support - center_u) / radius);
    const auto upper_x = std::min(1.0, (support - center_u) / radius);
    if (lower_x >= upper_x) {
      return BrdfLobes {};
    }
    auto theta_limits = AngleInterval {
      .lower = std::asin(lower_x),
      .upper = std::asin(upper_x),
    };
    if (source.center.z <= 0.0) {
      const auto vertical_extent = radius * std::abs(second.z);
      if (vertical_extent <= -source.center.z) {
        return BrdfLobes {};
      }
      const auto maximum_angle = std::acos(-source.center.z / vertical_extent);
      theta_limits.lower = std::max(theta_limits.lower, -maximum_angle);
      theta_limits.upper = std::min(theta_limits.upper, maximum_angle);
      if (theta_limits.lower >= theta_limits.upper) {
        return BrdfLobes {};
      }
    }
    auto peak_u = std::optional<double> {};
    auto peak_v = std::optional<double> {};
    const auto mirror = UnitDirection {
      .x = -std::sqrt((1.0 - view.get()) * (1.0 + view.get())),
      .y = 0.0,
      .z = view.get(),
    };
    const auto facing = -Dot(axis, mirror);
    if (facing > 0.0) {
      const auto distance = height / facing;
      peak_u = ((distance * Dot(mirror, first)) - center_u) / radius;
      peak_v = ((distance * Dot(mirror, second)) - center_v) / radius;
    }
    const auto theta_breaks = AngleBreaks(peak_u, theta_limits);
    auto single = detail::Sum {};
    auto multiple = detail::Sum {};
    auto diffuse = detail::Sum {};
    for (std::size_t part = 1U; part < theta_breaks.size(); ++part) {
      const auto theta_start = theta_breaks.at(part - 1U);
      const auto theta_scale
        = (theta_breaks.at(part) - theta_start) * 2.0 / kPi;
      for (const auto& x_node : detail::AngularRule(order)) {
        const auto theta = theta_start + (x_node.angle * theta_scale);
        const auto cosine_theta = std::cos(theta);
        const auto plane_u = center_u + (radius * std::sin(theta));
        if (std::abs(plane_u) >= support) {
          continue;
        }
        const auto limit_v = std::sqrt(
          (support - std::abs(plane_u)) * (support + std::abs(plane_u)));
        auto lower_y
          = std::max(-1.0, (-limit_v - center_v) / (radius * cosine_theta));
        auto upper_y
          = std::min(1.0, (limit_v - center_v) / (radius * cosine_theta));
        if (second.z > 0.0) {
          lower_y = std::max(
            lower_y, -source.center.z / (radius * second.z * cosine_theta));
        } else if (second.z < 0.0) {
          upper_y = std::min(
            upper_y, -source.center.z / (radius * second.z * cosine_theta));
        }
        if (lower_y >= upper_y) {
          continue;
        }
        const auto psi_breaks = AngleBreaks(
          peak_v ? std::optional { *peak_v / cosine_theta } : std::nullopt,
          { .lower = std::asin(lower_y), .upper = std::asin(upper_y) });
        for (std::size_t section = 1U; section < psi_breaks.size(); ++section) {
          const auto psi_start = psi_breaks.at(section - 1U);
          const auto psi_scale
            = (psi_breaks.at(section) - psi_start) * 2.0 / kPi;
          for (const auto& y_node : detail::AngularRule(order)) {
            const auto psi = psi_start + (y_node.angle * psi_scale);
            const auto local_u = radius * std::sin(theta);
            const auto local_v = radius * cosine_theta * std::sin(psi);
            const auto x
              = source.center.x + (local_u * first.x) + (local_v * second.x);
            const auto y
              = source.center.y + (local_u * first.y) + (local_v * second.y);
            const auto z
              = source.center.z + (local_u * first.z) + (local_v * second.z);
            const auto distance = std::hypot(x, y, z);
            const auto emission_cosine = height / distance;
            if (z <= 0.0 || emission_cosine <= outer_cosine
              || distance >= range) {
              continue;
            }
            const auto profile = emission_cosine >= inner_cosine
              ? 1.0
              : std::pow((emission_cosine - outer_cosine)
                    / (inner_cosine - outer_cosine),
                  2);
            const auto window
              = std::pow(1.0 - std::pow(distance / range, 4), 2);
            const auto lobes = brdf(
              { .x = x / distance, .y = y / distance, .z = z / distance });
            if (!lobes) {
              return std::unexpected(lobes.error());
            }
            const auto weight = peak / kPi * profile * window * z / distance
              / std::max(distance * distance, kDistanceGuardSquared)
              * cosine_theta * cosine_theta * std::cos(psi) * theta_scale
              * psi_scale * x_node.weight * y_node.weight;
            single.Add(lobes->single_scattering * weight);
            multiple.Add(lobes->multiple_scattering * weight);
            diffuse.Add(lobes->diffuse * weight);
          }
        }
      }
    }
    return BrdfLobes {
      .single_scattering = single.value,
      .multiple_scattering = multiple.value,
      .diffuse = diffuse.value,
    };
  }

  NOLINT_TEST(FiniteEmitterOffAxisTest, DiskMatchesIndependentCartesianIntegral)
  {
    double maximum_relative_error = 0.0;
    for (const auto roughness : { 0.045, 0.25, 1.0 }) {
      for (const auto view : { 0.5, 1.0 }) {
        for (const auto offset : { -0.8, 0.37, 0.99 }) {
          SCOPED_TRACE(roughness);
          SCOPED_TRACE(view);
          SCOPED_TRACE(offset);
          const auto source = FiniteEmitter {
            .center = { .x = offset, .y = 0.19, .z = 1.0 },
            .radius = SourceRadiusMetres { 1.0 },
            .range = InfluenceRangeMetres { 20.0 },
            .flux = LuminousFluxLumens { 2.0 * kPi / 3.0 },
            .compensation = SourceExposureEv { 1.0 },
          };
          const auto cone = SpotCone {
            .inner = InnerHalfAngleRadians { 0.0 },
            .outer = OuterHalfAngleRadians { kPi / 2.0 },
          };
          const auto kernel = [&](const UnitDirection& direction)
            -> std::expected<BrdfLobes, BrdfReferenceError> {
            return ControlledLobes(direction, PerceptualRoughness { roughness },
              ViewCosine { view });
          };
          const auto polar = IntegrateSpotDisk(source,
            { .x = 0.0, .y = 0.0, .z = -1.0 }, cone, kernel,
            { .maximum_order = 1024U,
              .peak_direction
              = UnitDirection { .x = -std::sqrt((1.0 - view) * (1.0 + view)),
                .y = 0.0,
                .z = view, }, });
          ASSERT_TRUE(polar.has_value())
            << "order=" << polar.error().last_estimate.order << " change="
            << polar.error()
                 .last_estimate.estimated_absolute_change.single_scattering
            << " last single="
            << polar.error().last_estimate.radiance.single_scattering;
          const auto first
            = CartesianDisk(source, { .x = 0.0, .y = 0.0, .z = -1.0 }, cone,
              kernel, ViewCosine { view }, 256U);
          const auto second
            = CartesianDisk(source, { .x = 0.0, .y = 0.0, .z = -1.0 }, cone,
              kernel, ViewCosine { view }, 512U);
          ASSERT_TRUE(first.has_value());
          ASSERT_TRUE(second.has_value());
          for (const auto& values : {
                 std::array {
                   first->single_scattering,
                   second->single_scattering,
                   polar->radiance.single_scattering,
                 },
                 std::array {
                   first->diffuse,
                   second->diffuse,
                   polar->radiance.diffuse,
                 },
               }) {
            const auto expected = values.at(1);
            EXPECT_NEAR(values.at(0), expected, 1.0e-7 + (1.0e-5 * expected));
            EXPECT_NEAR(values.at(2), expected, 1.0e-7 + (1.0e-5 * expected));
            maximum_relative_error = std::max(maximum_relative_error,
              std::abs(values.at(2) - expected) / expected);
          }
        }
      }
    }
    RecordProperty("maximum_relative_error", maximum_relative_error);
  }
  NOLINT_TEST(
    FiniteEmitterOffAxisTest, TiltedDisksMatchAtGrazingAndAcrossTheHorizon)
  {
    struct Geometry {
      EmitterOffsetMetres center;
      UnitDirection axis;
      SourceRadiusMetres radius { 0.0 };
    };
    const auto geometries = std::array {
      Geometry {
        .center = { .x = -0.8, .y = 0.19, .z = 1.0 },
        .axis = { .x = 0.6, .y = 0.0, .z = -0.8 },
        .radius = SourceRadiusMetres { 1.0 },
      },
      Geometry {
        .center = { .x = -0.8, .y = 0.19, .z = 1.0 },
        .axis = { .x = -0.6, .y = 0.0, .z = -0.8 },
        .radius = SourceRadiusMetres { 1.0 },
      },
      Geometry {
        .center = { .x = -1.0, .y = 0.0, .z = -0.2 },
        .axis = { .x = 1.0, .y = 0.0, .z = 0.0 },
        .radius = SourceRadiusMetres { 0.5 },
      },
      Geometry {
        .center = { .x = 1.0, .y = 0.0, .z = -0.2 },
        .axis = { .x = -1.0, .y = 0.0, .z = 0.0 },
        .radius = SourceRadiusMetres { 0.5 },
      },
    };
    double maximum_scaled_error = 0.0;
    unsigned checked = 0U;
    for (const auto& geometry : geometries) {
      for (const auto roughness : { 0.045, 0.25, 1.0 }) {
        for (const auto view : { 0.01, 0.5, 1.0 }) {
          SCOPED_TRACE(geometry.center.x);
          SCOPED_TRACE(geometry.axis.x);
          SCOPED_TRACE(roughness);
          SCOPED_TRACE(view);
          const auto source = FiniteEmitter {
            .center = geometry.center,
            .radius = geometry.radius,
            .range = InfluenceRangeMetres { 20.0 },
            .flux = LuminousFluxLumens { 2.0 * kPi / 3.0 },
          };
          const auto cone = SpotCone {
            .inner = InnerHalfAngleRadians { 0.0 },
            .outer = OuterHalfAngleRadians { kPi / 2.0 },
          };
          const auto kernel = [&](const UnitDirection& direction)
            -> std::expected<BrdfLobes, BrdfReferenceError> {
            return ControlledLobes(direction, PerceptualRoughness { roughness },
              ViewCosine { view });
          };
          const auto actual
            = IntegrateSpotDisk(source, geometry.axis, cone, kernel,
              { .maximum_order = 1024U,
                .peak_direction
                = UnitDirection { .x = -std::sqrt((1.0 - view) * (1.0 + view)),
                  .y = 0.0,
                  .z = view, }, });
          ASSERT_TRUE(actual.has_value())
            << "order=" << actual.error().last_estimate.order << " change="
            << actual.error()
                 .last_estimate.estimated_absolute_change.single_scattering;
          const std::uint32_t coarse_order
            = roughness == 0.045 && view == 0.01 ? 1024U : 512U;
          const auto coarse = CartesianDisk(source, geometry.axis, cone, kernel,
            ViewCosine { view }, coarse_order);
          const auto fine = CartesianDisk(source, geometry.axis, cone, kernel,
            ViewCosine { view }, 2U * coarse_order);
          ASSERT_TRUE(coarse.has_value());
          ASSERT_TRUE(fine.has_value());
          for (const auto& values : {
                 std::array {
                   coarse->single_scattering,
                   fine->single_scattering,
                   actual->radiance.single_scattering,
                 },
                 std::array {
                   coarse->diffuse,
                   fine->diffuse,
                   actual->radiance.diffuse,
                 },
               }) {
            const auto tolerance = 1.0e-7 + (1.0e-5 * values.at(1));
            EXPECT_NEAR(values.at(0), values.at(1), tolerance);
            EXPECT_NEAR(values.at(2), values.at(1), tolerance);
            maximum_scaled_error = std::max(maximum_scaled_error,
              std::abs(values.at(2) - values.at(1)) / tolerance);
          }
          ++checked;
        }
      }
    }
    EXPECT_EQ(checked, 36U);
    RecordProperty("maximum_fraction_of_error_budget", maximum_scaled_error);
  }

  NOLINT_TEST(FiniteEmitterOffAxisTest, SphereMatchesIndependentSurfaceIntegral)
  {
    double maximum_relative_error = 0.0;
    for (const auto roughness : { 0.045, 0.25, 1.0 }) {
      for (const auto view : { 0.01, 0.5, 1.0 }) {
        for (const auto offset : { -0.8, 0.37, 0.99 }) {
          SCOPED_TRACE(roughness);
          SCOPED_TRACE(view);
          SCOPED_TRACE(offset);
          const auto source = FiniteEmitter {
            .center = { .x = offset, .y = 0.19, .z = 1.0 },
            .radius = SourceRadiusMetres { 1.0 },
            .range = InfluenceRangeMetres { 20.0 },
            .flux = LuminousFluxLumens { 2.0 * kPi / 3.0 },
            .compensation = SourceExposureEv { 1.0 },
          };
          const auto kernel = [&](const UnitDirection& direction)
            -> std::expected<BrdfLobes, BrdfReferenceError> {
            return ControlledLobes(direction, PerceptualRoughness { roughness },
              ViewCosine { view });
          };
          const auto cap = IntegratePointSphere(source, kernel,
            { .maximum_order = 2048U,
              .peak_direction
              = UnitDirection { .x = -std::sqrt((1.0 - view) * (1.0 + view)),
                .y = 0.0,
                .z = view, }, });
          ASSERT_TRUE(cap.has_value())
            << "order=" << cap.error().last_estimate.order << " change="
            << cap.error()
                 .last_estimate.estimated_absolute_change.single_scattering;
          std::uint32_t coarse_order = 256U;
          if (view == 0.01) {
            coarse_order = roughness == 0.045 ? 1024U : 512U;
          }
          const auto first
            = SphereSurface(source, kernel, ViewCosine { view }, coarse_order);
          const auto second = SphereSurface(
            source, kernel, ViewCosine { view }, 2U * coarse_order);
          ASSERT_TRUE(first.has_value());
          ASSERT_TRUE(second.has_value());
          for (const auto& values : {
                 std::array {
                   first->single_scattering,
                   second->single_scattering,
                   cap->radiance.single_scattering,
                 },
                 std::array {
                   first->diffuse,
                   second->diffuse,
                   cap->radiance.diffuse,
                 },
               }) {
            const auto expected = values.at(1);
            EXPECT_NEAR(values.at(0), expected, 1.0e-7 + (1.0e-5 * expected));
            EXPECT_NEAR(values.at(2), expected, 1.0e-7 + (1.0e-5 * expected));
            maximum_relative_error = std::max(maximum_relative_error,
              std::abs(values.at(2) - expected) / expected);
          }
        }
      }
    }
    RecordProperty("maximum_relative_error", maximum_relative_error);
  }

  NOLINT_TEST(
    FiniteEmitterOffAxisTest, CoupledRgbMatchesIndependentAreaIntegrals)
  {
    const auto material = StandardMaterial {
      .base_color = { .red = 0.8, .green = 0.4, .blue = 0.2 },
      .metallic = 0.25,
      .specular = 0.5,
      .roughness = PerceptualRoughness { 1.0 },
    };
    const auto channels = ResolveMaterialBrdf(material);
    ASSERT_TRUE(channels.has_value());
    const auto view = ViewCosine { 0.5 };
    const auto view_moments = IntegrateGgxMoments(material.roughness, view);
    ASSERT_TRUE(view_moments.has_value());
    // Exact mean integrals at alpha=1; no interpolated moment table.
    const auto mean = GgxMeanMomentEstimate {
      .hemispherical_albedo = (4.0 / 3.0) * (1.0 - std::numbers::ln2),
      .schlick_moment = (111.0 / 35.0) - ((32.0 / 7.0) * std::numbers::ln2),
    };
    const auto source = FiniteEmitter {
      .center = { .x = -0.4, .y = 0.1, .z = 2.0 },
      .radius = SourceRadiusMetres { 0.4 },
      .range = InfluenceRangeMetres { 20.0 },
      .flux = LuminousFluxLumens { 100.0 },
      .compensation = SourceExposureEv { 1.0 },
    };
    const auto axis = UnitDirection { .x = 0.6, .y = 0.0, .z = -0.8 };
    const auto cone = SpotCone {
      .inner = InnerHalfAngleRadians { 0.0 },
      .outer = OuterHalfAngleRadians { kPi / 2.0 },
    };
    struct Channel {
      BrdfReflectance reflectance;
      double tint { 1.0 };
    };
    // Light tint is radiometric scaling, not bounded material reflectance.
    const auto colors = std::array {
      Channel { .reflectance = channels->red, .tint = 0.5 },
      Channel { .reflectance = channels->green, .tint = 1.25 },
      Channel { .reflectance = channels->blue, .tint = 2.0 },
    };
    // Reuse only identical double-precision queries across color channels.
    auto incident_moments = std::map<ViewCosine, GgxMomentEstimate> {};
    double maximum_scaled_error = 0.0;
    unsigned checked_lobes = 0U;
    for (const auto& color : colors) {
      SCOPED_TRACE(color.tint);
      const auto kernel = [&](const UnitDirection& direction)
        -> std::expected<BrdfLobes, BrdfReferenceError> {
        const auto cosine = ViewCosine { direction.z };
        auto found = incident_moments.find(cosine);
        if (found == incident_moments.end()) {
          const auto moments = IntegrateGgxMoments(material.roughness, cosine);
          if (!moments) {
            ADD_FAILURE() << "Directional moment integration did not converge";
            return std::unexpected(BrdfReferenceError::kUnrepresentable);
          }
          found = incident_moments.emplace(cosine, *moments).first;
        }
        return EvaluateGgxBrdfChannel(
          {
            .roughness = material.roughness,
            .light = LightCosine { direction.z },
            .view = view,
            .azimuth = RelativeAzimuth { std::atan2(direction.y, direction.x) },
          },
          color.reflectance,
          { .light = found->second, .view = *view_moments, .mean = mean });
      };
      auto tinted = source;
      tinted.flux = LuminousFluxLumens { source.flux.get() * color.tint };
      for (const bool disk : { false, true }) {
        SCOPED_TRACE(disk);
        const auto actual = disk ? IntegrateSpotDisk(tinted, axis, cone, kernel)
                                 : IntegratePointSphere(tinted, kernel);
        ASSERT_TRUE(actual.has_value());
        const auto coarse = disk
          ? CartesianDisk(source, axis, cone, kernel, view, 32U)
          : SphereSurface(source, kernel, view, 32U);
        const auto fine = disk
          ? CartesianDisk(source, axis, cone, kernel, view, 64U)
          : SphereSurface(source, kernel, view, 64U);
        ASSERT_TRUE(coarse.has_value());
        ASSERT_TRUE(fine.has_value());
        for (const auto member : {
               &BrdfLobes::single_scattering,
               &BrdfLobes::multiple_scattering,
               &BrdfLobes::diffuse,
             }) {
          const auto expected = ((*fine).*member) * color.tint;
          const auto tolerance = 1.0e-7 + (1.0e-5 * expected);
          EXPECT_GT(expected, 0.0);
          EXPECT_NEAR(((*coarse).*member) * color.tint, expected, tolerance);
          const auto measured = actual->radiance.*member;
          EXPECT_NEAR(measured, expected, tolerance);
          maximum_scaled_error = std::max(
            maximum_scaled_error, std::abs(measured - expected) / tolerance);
          ++checked_lobes;
        }
      }
    }
    EXPECT_EQ(checked_lobes, 18U);
    RecordProperty("maximum_fraction_of_error_budget", maximum_scaled_error);
    RecordProperty("distinct_moment_queries", incident_moments.size());
  }

  NOLINT_TEST(FiniteEmitterOffAxisTest,
    PeakHintsPreserveFlatResponseAndRejectInvalidDirections)
  {
    const auto source = FiniteEmitter {
      .center = { .x = 0.0, .y = 0.0, .z = 2.0 },
      .radius = SourceRadiusMetres { 0.5 },
    };
    const auto flat =
      [](const UnitDirection&) -> std::expected<BrdfLobes, BrdfReferenceError> {
      return BrdfLobes { .diffuse = 1.0 / kPi };
    };
    const auto settings = EmitterIntegrationSettings {
      .peak_direction
      = UnitDirection { .x = 0.12, .y = 0.16, .z = std::sqrt(0.96) },
    };
    const auto sphere = IntegratePointSphere(source, flat);
    const auto split_sphere = IntegratePointSphere(source, flat, settings);
    ASSERT_TRUE(sphere.has_value());
    ASSERT_TRUE(split_sphere.has_value());
    EXPECT_NEAR(
      sphere->radiance.diffuse, split_sphere->radiance.diffuse, 1.0e-8);
    const auto disk
      = IntegrateSpotDisk(source, { .x = 0.0, .y = 0.0, .z = -1.0 }, {}, flat);
    const auto split_disk = IntegrateSpotDisk(
      source, { .x = 0.0, .y = 0.0, .z = -1.0 }, {}, flat, settings);
    ASSERT_TRUE(disk.has_value());
    ASSERT_TRUE(split_disk.has_value());
    EXPECT_NEAR(disk->radiance.diffuse, split_disk->radiance.diffuse, 1.0e-8);
    EXPECT_FALSE(IntegratePointSphere(source, flat,
      { .peak_direction = UnitDirection { .x = 0.0, .y = 0.0, .z = 0.0 } }));
  }
} // namespace
} // namespace oxygen::vortex::testing::reference
