//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex {

enum class LightingPreparationError : std::uint8_t {
  kInvalidInput,
  kUnrepresentable,
  kAllocationFailed,
  kGenerationMismatch,
};

enum class LightingSelectionFamily : std::uint8_t {
  kNone,
  kDirectional,
  kLocal,
};

//! A failed preparation never authorizes a partially populated light package.
struct LightingPreparationFailure {
  LightingPreparationError error { LightingPreparationError::kInvalidInput };
  LightingSelectionFamily family { LightingSelectionFamily::kNone };
  LightSelectionIndex selection_index { kInvalidLightSelectionIndex };
  ViewId view_id { kInvalidViewId };
};

} // namespace oxygen::vortex
