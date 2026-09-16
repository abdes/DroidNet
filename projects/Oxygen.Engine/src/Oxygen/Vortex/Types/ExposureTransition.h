//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen::vortex {

enum class ExposureTransitionPolicy : std::uint32_t {
  kPreserve,
  kRemeter,
  kSeedFromEv100,
};

enum class ExposureTransitionError : std::uint8_t {
  kInvalidTarget,
  kRendererUnavailable,
  kInvalidPolicy,
  kInvalidSeed,
  kGenerationExhausted,
  kUnknownToken,
  kConflictingToken,
  kNotAuto,
  kSharedConsumer,
  kUnsupportedSeed,
};

enum class ExposureTransitionPhase : std::uint8_t {
  kQueued,
  kApplied,
  kRejected,
  kSuperseded,
};

//! Renderer-issued runtime request identity; never serialize into scene assets.
struct ExposureTransitionToken {
  CompositionView::ViewStateHandle target {
    CompositionView::kInvalidViewStateHandle
  };
  std::uint64_t lifetime { 0U };
  std::uint64_t generation { 0U };
  ExposureTransitionPolicy policy { ExposureTransitionPolicy::kRemeter };
  std::optional<float> seed_ev;

  auto operator==(const ExposureTransitionToken&) const -> bool = default;
};

//! Bounded control status; numerical gain remains GPU-owned.
struct ExposureTransitionStatus {
  ExposureTransitionToken request;
  ExposureTransitionPhase phase { ExposureTransitionPhase::kQueued };
  std::uint64_t applied_generation { 0U };
  std::optional<ExposureTransitionError> error;
};

} // namespace oxygen::vortex
