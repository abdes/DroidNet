//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>

#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialEvaluation.h>

namespace oxygen::vortex::testing::reference {
namespace {
  using Vector = std::array<double, 3>;
  constexpr double kNormalizeLengthMinimum = 1.0e-10;
  constexpr double kTangentLengthMinimum = 1.0e-3;
  constexpr double kNormalMapZMinimum = 1.0e-4;
  constexpr double kFallbackAxisThreshold = 0.9;

  auto Dot(const Vector& a, const Vector& b) -> double
  {
    return (a.at(0) * b.at(0)) + (a.at(1) * b.at(1)) + (a.at(2) * b.at(2));
  }
  auto Cross(const Vector& a, const Vector& b) -> Vector
  {
    return {
      (a.at(1) * b.at(2)) - (a.at(2) * b.at(1)),
      (a.at(2) * b.at(0)) - (a.at(0) * b.at(2)),
      (a.at(0) * b.at(1)) - (a.at(1) * b.at(0)),
    };
  }
  auto Length(const Vector& value) -> double
  {
    return std::hypot(value.at(0), value.at(1), value.at(2));
  }
  auto Normalize(const Vector& value) -> Vector
  {
    const auto scale = std::max(
      { std::abs(value.at(0)), std::abs(value.at(1)), std::abs(value.at(2)) });
    if (scale == 0.0) {
      return {};
    }
    const auto scaled = Vector {
      value.at(0) / scale,
      value.at(1) / scale,
      value.at(2) / scale,
    };
    const auto length = Length(scaled);
    if (scale <= kNormalizeLengthMinimum / length) {
      return {};
    }
    return {
      scaled.at(0) / length,
      scaled.at(1) / length,
      scaled.at(2) / length,
    };
  }
  auto Finite(const Vector& value) -> bool
  {
    return std::ranges::all_of(value,
      [](const double component) -> bool { return std::isfinite(component); });
  }
  auto Nonnegative(const LinearRgb& rgb) -> bool
  {
    return std::isfinite(rgb.red) && rgb.red >= 0.0 && std::isfinite(rgb.green)
      && rgb.green >= 0.0 && std::isfinite(rgb.blue) && rgb.blue >= 0.0;
  }
  auto Unit(const double value) -> bool
  {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
  }
  auto Multiply(const LinearRgb& a, const LinearRgb& b) -> LinearRgb
  {
    return {
      .red = a.red * b.red,
      .green = a.green * b.green,
      .blue = a.blue * b.blue,
    };
  }
} // namespace

auto EvaluateMaterial(const MaterialEvaluationInput& input)
  -> std::expected<EvaluatedMaterial, BrdfReferenceError>
{
  const auto& factors = input.factors;
  const auto geometric = Vector {
    input.basis.normal.x,
    input.basis.normal.y,
    input.basis.normal.z,
  };
  if (!Finite(geometric) || !Finite(input.basis.tangent)
    || !Finite(input.basis.bitangent) || !Unit(factors.base_color.rgb.red)
    || !Unit(factors.base_color.rgb.green) || !Unit(factors.base_color.rgb.blue)
    || !Unit(factors.base_color.alpha) || !Unit(factors.metallic)
    || !Unit(factors.roughness.get()) || !Unit(factors.ambient_occlusion)
    || !Nonnegative(factors.emissive) || !std::isfinite(factors.normal_scale)
    || !Unit(input.alpha_cutoff)) {
    return std::unexpected(BrdfReferenceError::kInvalidInput);
  }
  auto output = EvaluatedMaterial {
    .material = { .base_color = factors.base_color.rgb,
      .metallic = factors.metallic,
      .roughness = factors.roughness, },
    .emissive = factors.emissive,
    .alpha = factors.base_color.alpha,
    .ambient_occlusion = factors.ambient_occlusion,
  };
  auto normal = Normalize(geometric);
  if (Length(normal) == 0.0) {
    normal = { 0.0, 0.0, 1.0 };
  }
  if (input.textures_enabled) {
    const auto& samples = input.samples;
    if (samples.base_color) {
      if (!Nonnegative(samples.base_color->rgb)
        || !Unit(samples.base_color->alpha)) {
        return std::unexpected(BrdfReferenceError::kInvalidInput);
      }
      output.material.base_color
        = Multiply(output.material.base_color, samples.base_color->rgb);
      output.alpha *= samples.base_color->alpha;
    }
    const auto metallic
      = samples.orm ? samples.orm->metallic : samples.metallic.value_or(1.0);
    const auto roughness
      = samples.orm ? samples.orm->roughness : samples.roughness.value_or(1.0);
    const auto occlusion
      = samples.occlusion.value_or(samples.orm ? samples.orm->occlusion : 1.0);
    if (!std::isfinite(metallic) || !std::isfinite(roughness)
      || !std::isfinite(occlusion)) {
      return std::unexpected(BrdfReferenceError::kInvalidInput);
    }
    output.material.metallic *= std::clamp(metallic, 0.0, 1.0);
    output.material.roughness = PerceptualRoughness { factors.roughness.get()
      * std::clamp(roughness, 0.0, 1.0) };
    output.ambient_occlusion *= std::clamp(occlusion, 0.0, 1.0);
    if (samples.emissive) {
      if (!Nonnegative(*samples.emissive)) {
        return std::unexpected(BrdfReferenceError::kInvalidInput);
      }
      output.emissive = Multiply(output.emissive, *samples.emissive);
    }
    if (samples.normal) {
      const auto& encoded = *samples.normal;
      if (!Unit(encoded.red) || !Unit(encoded.green) || !Unit(encoded.blue)) {
        return std::unexpected(BrdfReferenceError::kInvalidInput);
      }
      auto mapped = Normalize({
        (2.0 * encoded.red) - 1.0,
        (2.0 * encoded.green) - 1.0,
        (2.0 * encoded.blue) - 1.0,
      });
      mapped = Normalize({
        mapped.at(0) * factors.normal_scale,
        mapped.at(1) * factors.normal_scale,
        std::max(mapped.at(2), kNormalMapZMinimum),
      });
      auto tangent = Normalize(input.basis.tangent);
      const auto projection = Dot(normal, tangent);
      for (std::size_t component = 0U; component < tangent.size();
        ++component) {
        tangent.at(component) -= normal.at(component) * projection;
      }
      if (Length(tangent) <= kTangentLengthMinimum) {
        const auto axis = std::abs(normal.at(2)) > kFallbackAxisThreshold
          ? Vector { 1.0, 0.0, 0.0 }
          : Vector { 0.0, 0.0, 1.0 };
        tangent = Cross(normal, axis);
      }
      tangent = Normalize(tangent);
      auto bitangent = Cross(normal, tangent);
      const auto authored_bitangent = Normalize(input.basis.bitangent);
      if (Length(bitangent) <= kTangentLengthMinimum) {
        bitangent = authored_bitangent;
      }
      const auto sign = Dot(bitangent, authored_bitangent) < 0.0 ? -1.0 : 1.0;
      auto perturbed = Vector {};
      bitangent = Normalize(bitangent);
      for (std::size_t component = 0U; component < perturbed.size();
        ++component) {
        perturbed.at(component) = (tangent.at(component) * mapped.at(0))
          + (sign * bitangent.at(component) * mapped.at(1))
          + (normal.at(component) * mapped.at(2));
      }
      perturbed = Normalize(perturbed);
      if (Length(perturbed) > 0.0 && Dot(perturbed, normal) >= 0.0) {
        normal = perturbed;
      }
    }
  }
  if (!Nonnegative(output.emissive) || !Finite(normal)) {
    return std::unexpected(BrdfReferenceError::kUnrepresentable);
  }
  if (!ResolveMaterialBrdf(output.material)) {
    return std::unexpected(BrdfReferenceError::kInvalidInput);
  }
  const auto face_sign = input.double_sided && !input.front_face ? -1.0 : 1.0;
  output.normal = {
    .x = face_sign * normal.at(0),
    .y = face_sign * normal.at(1),
    .z = face_sign * normal.at(2),
  };
  output.fragment_visible = (input.front_face || input.double_sided)
    && (!input.alpha_test || output.alpha >= input.alpha_cutoff);
  return output;
}

auto TransformMaterialUv(
  const UvCoordinate uv, const MaterialUvTransform& transform)
  -> std::expected<UvCoordinate, BrdfReferenceError>
{
  if (!std::isfinite(uv.u) || !std::isfinite(uv.v)
    || !std::isfinite(transform.scale.at(0))
    || !std::isfinite(transform.scale.at(1))
    || !std::isfinite(transform.rotation.get())
    || !std::isfinite(transform.offset.u)
    || !std::isfinite(transform.offset.v)) {
    return std::unexpected(BrdfReferenceError::kInvalidInput);
  }
  const auto x = uv.u * transform.scale.at(0);
  const auto y = uv.v * transform.scale.at(1);
  const auto cosine = std::cos(transform.rotation.get());
  const auto sine = std::sin(transform.rotation.get());
  const auto result = UvCoordinate {
    .u = (cosine * x) - (sine * y) + transform.offset.u,
    .v = (sine * x) + (cosine * y) + transform.offset.v,
  };
  if (!std::isfinite(result.u) || !std::isfinite(result.v)) {
    return std::unexpected(BrdfReferenceError::kUnrepresentable);
  }
  return result;
}

} // namespace oxygen::vortex::testing::reference
