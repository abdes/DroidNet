//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {

using LightCosine = NamedType<double, struct LightCosineTag, Comparable>;
using RelativeAzimuth
  = NamedType<double, struct RelativeAzimuthTag, Comparable>;

struct BrdfQuery {
  PerceptualRoughness roughness { 1.0 };
  LightCosine light { 1.0 };
  ViewCosine view { 1.0 };
  RelativeAzimuth azimuth { 0.0 };
};

//! One RGB channel; the model's reflectance operations are componentwise.
struct BrdfReflectance {
  double f0 { 0.04 };
  double diffuse { 0.5 };
};

//! Caller supplies moments matching the query's roughness and two cosines.
struct BrdfMoments {
  GgxMomentEstimate light {};
  GgxMomentEstimate view {};
  GgxMeanMomentEstimate mean {};
};

struct BrdfLobes {
  double single_scattering { 0.0 };
  double multiple_scattering { 0.0 };
  double diffuse { 0.0 };
};

enum class BrdfReferenceError : std::uint8_t {
  kInvalidInput,
  kUnrepresentable,
};

//! Evaluate the common BRDF, without incident power, receiver cosine or
//! exposure.
[[nodiscard]] auto EvaluateGgxBrdfChannel(const BrdfQuery& query,
  const BrdfReflectance& material, const BrdfMoments& moments)
  -> std::expected<BrdfLobes, BrdfReferenceError>;

//! The same single-scattering lobe, usable for integration without E/B inputs.
[[nodiscard]] auto EvaluateGgxSingleScatteringChannel(const BrdfQuery& query,
  double f0) -> std::expected<double, BrdfReferenceError>;

} // namespace oxygen::vortex::testing::reference
