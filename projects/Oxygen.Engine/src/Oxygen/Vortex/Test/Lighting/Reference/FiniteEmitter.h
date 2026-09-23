//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/Photometry.h>

namespace oxygen::vortex::testing::reference {

using SourceRadiusMetres
  = NamedType<double, struct SourceRadiusTag, Comparable>;

//! All geometry is in a receiver-local orthonormal frame, with N=(0,0,1).
struct EmitterOffsetMetres {
  double x { 0.0 };
  double y { 0.0 };
  double z { 1.0 };
};

struct UnitDirection {
  double x { 0.0 };
  double y { 0.0 };
  double z { 1.0 };
};

struct FiniteEmitter {
  EmitterOffsetMetres center {};
  SourceRadiusMetres radius { 0.0 };
  InfluenceRangeMetres range { 10.0 };
  LuminousFluxLumens flux { 100.0 };
  SourceExposureEv compensation { 0.0 };
};

//! The callback returns a BRDF only; the integrator supplies N.l and
//! illumination.
using IncidentBrdf = std::function<std::expected<BrdfLobes, BrdfReferenceError>(
  const UnitDirection&)>;

struct EmitterIntegrationSettings {
  double absolute_tolerance { 1.0e-7 };
  double relative_tolerance { 1.0e-5 };
  std::uint32_t initial_order { 8U };
  std::uint32_t maximum_order { 256U };
};

struct EmitterIntegral {
  BrdfLobes radiance {};
  //! Eight times successive-rule change per lobe, not a rigorous bound.
  BrdfLobes estimated_absolute_change {};
  std::uint32_t order { 0U };
  std::uint64_t evaluations { 0U };
};

enum class EmitterIntegrationError : std::uint8_t {
  kInvalidInput,
  kUnrepresentable,
  kBrdfEvaluationFailed,
  kDidNotConverge,
};

struct EmitterIntegrationFailure {
  EmitterIntegrationError reason { EmitterIntegrationError::kInvalidInput };
  std::optional<BrdfReferenceError> brdf_error;
  EmitterIntegral last_estimate {};
};

//! Unoccluded, untinted one-channel outgoing radiance, resolved per BRDF lobe.
[[nodiscard]] auto IntegratePointSphere(const FiniteEmitter& emitter,
  const IncidentBrdf& brdf, EmitterIntegrationSettings settings = {})
  -> std::expected<EmitterIntegral, EmitterIntegrationFailure>;

//! Disk axis is the emitted-ray direction. Receivers on/behind its plane get
//! zero.
[[nodiscard]] auto IntegrateSpotDisk(const FiniteEmitter& emitter,
  const UnitDirection& axis, const SpotCone& cone, const IncidentBrdf& brdf,
  EmitterIntegrationSettings settings = {})
  -> std::expected<EmitterIntegral, EmitterIntegrationFailure>;

} // namespace oxygen::vortex::testing::reference
