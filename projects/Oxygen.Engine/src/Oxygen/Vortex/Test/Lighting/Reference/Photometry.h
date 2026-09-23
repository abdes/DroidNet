//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::vortex::testing::reference {

using LuminousFluxLumens
  = NamedType<double, struct LuminousFluxTag, Comparable>;
using LuminousIntensityCandela
  = NamedType<double, struct LuminousIntensityTag, Comparable>;
using IlluminanceLux = NamedType<double, struct IlluminanceTag, Comparable>;
using SourceExposureEv
  = NamedType<double, struct SourceExposureTag, Comparable>;
using InnerHalfAngleRadians
  = NamedType<double, struct InnerHalfAngleTag, Comparable>;
using OuterHalfAngleRadians
  = NamedType<double, struct OuterHalfAngleTag, Comparable>;
using OffAxisAngleRadians
  = NamedType<double, struct OffAxisAngleTag, Comparable>;
using DistanceMetres = NamedType<double, struct DistanceTag, Comparable>;
using InfluenceRangeMetres
  = NamedType<double, struct InfluenceRangeTag, Comparable>;

struct SpotCone {
  InnerHalfAngleRadians inner { 0.0 };
  OuterHalfAngleRadians outer { 0.5 };
};

enum class PhotometryError : std::uint8_t {
  kInvalidInput,
  kUnrepresentable,
};

//! Independent angular integral of the specified spot profile, in steradians.
[[nodiscard]] auto SpotEffectiveSolidAngle(const SpotCone& cone)
  -> std::expected<double, PhotometryError>;

//! Relative intensity, including hard-cone and 90-degree soft-cone boundaries.
[[nodiscard]] auto SpotAngularWeight(const SpotCone& cone,
  OffAxisAngleRadians angle) -> std::expected<double, PhotometryError>;

//! Resolve source compensation exactly once; camera exposure is not an input.
[[nodiscard]] auto ResolvePointIntensity(LuminousFluxLumens flux,
  SourceExposureEv compensation = SourceExposureEv { 0.0 })
  -> std::expected<LuminousIntensityCandela, PhotometryError>;

[[nodiscard]] auto ResolveSpotPeakIntensity(LuminousFluxLumens flux,
  const SpotCone& cone,
  SourceExposureEv compensation = SourceExposureEv { 0.0 })
  -> std::expected<LuminousIntensityCandela, PhotometryError>;

[[nodiscard]] auto ResolveDirectionalIlluminance(IlluminanceLux illuminance,
  SourceExposureEv compensation = SourceExposureEv { 0.0 })
  -> std::expected<IlluminanceLux, PhotometryError>;

//! Reverse the spot conversion for imported candela, using both authored
//! angles.
[[nodiscard]] auto SpotFluxFromPeakIntensity(LuminousIntensityCandela intensity,
  const SpotCone& cone) -> std::expected<LuminousFluxLumens, PhotometryError>;

//! Squared quartic window and guarded inverse-square distance factor, in m^-2.
//! Zero range/separation or a receiver at/beyond range has zero influence.
[[nodiscard]] auto PunctualDistanceFactor(DistanceMetres distance,
  InfluenceRangeMetres range) -> std::expected<double, PhotometryError>;

} // namespace oxygen::vortex::testing::reference
