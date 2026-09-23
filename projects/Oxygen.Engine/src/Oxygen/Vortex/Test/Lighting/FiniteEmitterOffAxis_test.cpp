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
#include <numbers>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/FiniteEmitter.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
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

  auto AngleBreaks(const double normalized_peak) -> std::vector<double>
  {
    auto result = std::vector<double> { -kPi / 2.0 };
    if (std::abs(normalized_peak) < 1.0) {
      result.push_back(std::asin(normalized_peak));
    }
    result.push_back(kPi / 2.0);
    return result;
  }

  auto Dot(const UnitDirection& a, const UnitDirection& b) -> double
  {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
  }

  auto SphereSurface(const FiniteEmitter& source,
    const PerceptualRoughness roughness, const ViewCosine view,
    const std::uint32_t order) -> std::expected<BrdfLobes, BrdfReferenceError>
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
        for (std::size_t angular = 1U; angular < phi_breaks.size(); ++angular) {
          const auto phi_start = phi_breaks.at(angular - 1U);
          const auto phi_scale
            = (phi_breaks.at(angular) - phi_start) * 2.0 / kPi;
          for (const auto& phi_node : detail::AngularRule(order)) {
            const auto phi = phi_start + (phi_scale * phi_node.angle);
            const auto normal = UnitDirection {
              .x = (cosine * axis.x)
                + (sine
                  * ((std::cos(phi) * first.x) + (std::sin(phi) * second.x))),
              .y = (cosine * axis.y)
                + (sine
                  * ((std::cos(phi) * first.y) + (std::sin(phi) * second.y))),
              .z = (cosine * axis.z)
                + (sine
                  * ((std::cos(phi) * first.z) + (std::sin(phi) * second.z))),
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
            const auto lobes = ControlledLobes(ray, roughness, view);
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
            diffuse.Add(lobes->diffuse * weight);
          }
        }
      }
    }
    return BrdfLobes {
      .single_scattering = single.value,
      .diffuse = diffuse.value,
    };
  }

  auto CartesianDisk(const FiniteEmitter& source, const SpotCone& cone,
    const PerceptualRoughness roughness, const ViewCosine view,
    const std::uint32_t order) -> std::expected<BrdfLobes, BrdfReferenceError>
  {
    const auto radius = source.radius.get();
    const auto height = source.center.z;
    const auto mirror_x = -height
      * std::sqrt((1.0 - view.get()) * (1.0 + view.get())) / view.get();
    const auto theta_breaks
      = AngleBreaks((mirror_x - source.center.x) / radius);
    const auto inner_cosine = std::cos(cone.inner.get());
    const auto outer_cosine
      = cone.outer.get() == kPi / 2.0 ? 0.0 : std::cos(cone.outer.get());
    const auto solid_angle = 2.0 * kPi
      * ((1.0 - inner_cosine) + ((inner_cosine - outer_cosine) / 3.0));
    const auto peak
      = source.flux.get() * std::exp2(source.compensation.get()) / solid_angle;
    auto single = detail::Sum {};
    auto diffuse = detail::Sum {};
    for (std::size_t part = 1U; part < theta_breaks.size(); ++part) {
      const auto theta_start = theta_breaks.at(part - 1U);
      const auto theta_scale
        = (theta_breaks.at(part) - theta_start) * 2.0 / kPi;
      for (const auto& x_node : detail::AngularRule(order)) {
        const auto theta = theta_start + (x_node.angle * theta_scale);
        const auto cosine_theta = std::cos(theta);
        const auto x = source.center.x + (radius * std::sin(theta));
        const auto psi_breaks
          = AngleBreaks(-source.center.y / (radius * cosine_theta));
        for (std::size_t section = 1U; section < psi_breaks.size(); ++section) {
          const auto psi_start = psi_breaks.at(section - 1U);
          const auto psi_scale
            = (psi_breaks.at(section) - psi_start) * 2.0 / kPi;
          for (const auto& y_node : detail::AngularRule(order)) {
            // Unit disk: x=sin(theta), y=cos(theta)*sin(psi).
            // The area Jacobian is cos(theta)^2*cos(psi); a^2 cancels.
            const auto psi = psi_start + (y_node.angle * psi_scale);
            const auto y
              = source.center.y + (radius * cosine_theta * std::sin(psi));
            const auto distance = std::hypot(x, y, height);
            const auto cosine = height / distance;
            if (cosine <= outer_cosine || distance >= source.range.get()) {
              continue;
            }
            const auto profile = cosine >= inner_cosine
              ? 1.0
              : std::pow(
                  (cosine - outer_cosine) / (inner_cosine - outer_cosine), 2);
            const auto window
              = std::pow(1.0 - std::pow(distance / source.range.get(), 4), 2);
            const auto lobes = ControlledLobes(
              { .x = x / distance, .y = y / distance, .z = cosine }, roughness,
              view);
            if (!lobes) {
              return std::unexpected(lobes.error());
            }
            const auto weight = peak / kPi * profile * window * cosine
              / std::max(distance * distance, kDistanceGuardSquared)
              * cosine_theta * cosine_theta * std::cos(psi) * theta_scale
              * psi_scale * x_node.weight * y_node.weight;
            single.Add(lobes->single_scattering * weight);
            diffuse.Add(lobes->diffuse * weight);
          }
        }
      }
    }
    return BrdfLobes {
      .single_scattering = single.value,
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
          const auto first = CartesianDisk(source, cone,
            PerceptualRoughness { roughness }, ViewCosine { view }, 256U);
          const auto second = CartesianDisk(source, cone,
            PerceptualRoughness { roughness }, ViewCosine { view }, 512U);
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
  NOLINT_TEST(FiniteEmitterOffAxisTest, SphereMatchesIndependentSurfaceIntegral)
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
          const auto kernel = [&](const UnitDirection& direction)
            -> std::expected<BrdfLobes, BrdfReferenceError> {
            return ControlledLobes(direction, PerceptualRoughness { roughness },
              ViewCosine { view });
          };
          const auto cap = IntegratePointSphere(source, kernel,
            { .maximum_order = 1024U,
              .peak_direction
              = UnitDirection { .x = -std::sqrt((1.0 - view) * (1.0 + view)),
                .y = 0.0,
                .z = view, }, });
          ASSERT_TRUE(cap.has_value())
            << "order=" << cap.error().last_estimate.order;
          const auto first = SphereSurface(source,
            PerceptualRoughness { roughness }, ViewCosine { view }, 256U);
          const auto second = SphereSurface(source,
            PerceptualRoughness { roughness }, ViewCosine { view }, 512U);
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
