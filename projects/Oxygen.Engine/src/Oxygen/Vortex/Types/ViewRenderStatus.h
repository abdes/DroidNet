//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>

namespace oxygen::vortex {

enum class ViewRenderState : std::uint8_t { kPending, kSubmitted, kFailed };
enum class ViewRenderFailure : std::uint8_t {
  kNone, kRequiredInput, kAllocation, kRecording, kSubmission, kLighting,
};

//! Owned frame identity and outcome. Submitted is not a completed GPU certificate.
struct ViewRenderStatus {
  ViewId view_id { kInvalidViewId };
  frame::SequenceNumber frame_sequence { 0U };
  ViewRenderState state { ViewRenderState::kPending };
  ViewRenderFailure failure { ViewRenderFailure::kNone };
  std::optional<LightingPreparationFailure> lighting_failure;
  bool output_checks_lighting { false };
  bool lighting_validated { false };
  std::uint32_t gpu_lighting_failure { 0U };
  std::vector<ViewId> required_input_views;

  //! Call after waiting for the output's GPU completion, using its exact frame.
  [[nodiscard]] auto IsCaptureEligible(frame::SequenceNumber sequence) const
    noexcept -> bool
  {
    return frame_sequence == sequence && state == ViewRenderState::kSubmitted
      && lighting_validated;
  }
};

} // namespace oxygen::vortex
