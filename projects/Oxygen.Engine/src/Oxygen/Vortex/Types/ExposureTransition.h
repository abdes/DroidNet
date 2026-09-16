//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

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

//! Captured source definition and affected lifetimes for deferred source loss.
//! Numerical continuity stays in GPU records retained by PostProcessService.
struct ExposureSourceLoss {
  struct Consumer {
    CompositionView::ViewStateHandle handle;
    std::uint64_t lifetime;
  };
  ViewId source_view_id { kInvalidViewId };
  CompositionView::ViewStateHandle source_handle {
    CompositionView::kInvalidViewStateHandle
  };
  std::uint64_t source_lifetime { 0U };
  scene::ExposureSettings settings;
  std::optional<float> camera_ev;
  std::optional<ExposureTransitionToken> transition;
  std::optional<ExposureTransitionError> rejection;
  std::vector<Consumer> consumers;
};

} // namespace oxygen::vortex
