//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <expected>
#include <optional>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>

namespace oxygen::vortex::testing::reference {

struct LinearRgba {
  LinearRgb rgb { .red = 1.0, .green = 1.0, .blue = 1.0 };
  double alpha { 1.0 };
};

struct MaterialFactors {
  LinearRgba base_color {};
  double metallic { 0.0 };
  PerceptualRoughness roughness { 1.0 };
  double ambient_occlusion { 1.0 };
  LinearRgb emissive {};
  double normal_scale { 1.0 };
};

struct OrmSample {
  double occlusion { 1.0 };
  double roughness { 1.0 };
  double metallic { 1.0 };
};

//! Samples are already transfer-decoded. ORM owns metallic/roughness when set;
//! a separate occlusion sample overrides its R channel.
//! Normal samples contain encoded XYZ channels. Format expansion and texture
//! filtering occur before this interface.
struct MaterialSamples {
  std::optional<LinearRgba> base_color { std::nullopt };
  std::optional<LinearRgb> normal { std::nullopt };
  std::optional<OrmSample> orm { std::nullopt };
  std::optional<double> metallic { std::nullopt };
  std::optional<double> roughness { std::nullopt };
  std::optional<double> occlusion { std::nullopt };
  std::optional<LinearRgb> emissive { std::nullopt };
};

struct SurfaceBasis {
  WorldNormal normal {};
  std::array<double, 3> tangent { 1.0, 0.0, 0.0 };
  std::array<double, 3> bitangent { 0.0, 1.0, 0.0 };
};

struct MaterialEvaluationInput {
  MaterialFactors factors {};
  MaterialSamples samples {};
  SurfaceBasis basis {};
  bool textures_enabled { true };
  bool double_sided { false };
  bool front_face { true };
  bool alpha_test { false };
  double alpha_cutoff { 0.5 };
};

struct EvaluatedMaterial {
  StandardMaterial material {};
  WorldNormal normal {};
  LinearRgb emissive {};
  double alpha { 1.0 };
  double ambient_occlusion { 1.0 };
  bool fragment_visible { true };
};

//! Independent default metallic/roughness material evaluation before packing.
//! Extended material lobes and procedural materials are not inputs to this API.
[[nodiscard]] auto EvaluateMaterial(const MaterialEvaluationInput& input)
  -> std::expected<EvaluatedMaterial, BrdfReferenceError>;

using UvRotationRadians = NamedType<double, struct UvRotationTag, Comparable>;
struct UvCoordinate {
  double u { 0.0 };
  double v { 0.0 };
};
struct MaterialUvTransform {
  std::array<double, 2> scale { 1.0, 1.0 };
  UvRotationRadians rotation { 0.0 };
  UvCoordinate offset {};
};

//! Scale, rotate counterclockwise about the origin, then translate.
[[nodiscard]] auto TransformMaterialUv(
  UvCoordinate uv, const MaterialUvTransform& transform)
  -> std::expected<UvCoordinate, BrdfReferenceError>;

} // namespace oxygen::vortex::testing::reference
