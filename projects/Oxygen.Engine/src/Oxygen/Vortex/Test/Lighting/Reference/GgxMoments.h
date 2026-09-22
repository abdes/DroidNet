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

using PerceptualRoughness
  = NamedType<double, struct PerceptualRoughnessTag, Comparable>;
using ViewCosine = NamedType<double, struct ViewCosineTag, Comparable>;

struct MomentIntegrationSettings {
  double refinement_tolerance { 1.0e-8 };
  //! Power-of-two orders bound both refinement work and the cached rule set.
  std::uint32_t initial_order { 16U };
  std::uint32_t maximum_order { 4096U };
};

struct GgxMomentEstimate {
  double directional_albedo { 0.0 };
  double schlick_moment { 0.0 };
  //! Successive-rule estimate, not a rigorous uncertainty certificate.
  double estimated_absolute_change { 0.0 };
  std::uint32_t order { 0U };
  std::uint64_t evaluations { 0U };
};

enum class MomentIntegrationError : std::uint8_t {
  kInvalidInput,
  kDidNotConverge,
};

struct MomentIntegrationFailure {
  MomentIntegrationError reason { MomentIntegrationError::kInvalidInput };
  GgxMomentEstimate last_estimate {};
};

//! Integrate the specified correlated-GGX E/B moments independently of shaders.
//! mu=0 evaluates the grazing limit; roughness keeps the model's 0.045 floor.
[[nodiscard]] auto IntegrateGgxMoments(PerceptualRoughness roughness,
  ViewCosine view, MomentIntegrationSettings settings = {})
  -> std::expected<GgxMomentEstimate, MomentIntegrationFailure>;

} // namespace oxygen::vortex::testing::reference
