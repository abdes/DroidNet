//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

namespace oxygen::vortex {

//! Explicit scene-depth policy for the scene renderer.
/*!
 `DepthPrePassMode` is the view-plan contract for canonical scene depth.

 Current supported modes:
 - `kDisabled`: do not schedule the canonical scene `DepthPrePass`.
 - `kOpaqueAndMasked`: run the canonical scene `DepthPrePass` that publishes
   shared depth products for downstream consumers.

 More granular UE5-style early-Z modes remain future work and must not be
 implied by this enum.
*/
enum class DepthPrePassMode : std::uint8_t {
  kDisabled,
  kOpaqueAndMasked,
};

[[nodiscard]] constexpr auto to_string(const DepthPrePassMode mode) noexcept
  -> std::string_view
{
  switch (mode) {
  case DepthPrePassMode::kDisabled:
    return "Disabled";
  case DepthPrePassMode::kOpaqueAndMasked:
    return "OpaqueAndMasked";
  default:
    return "__Unknown__";
  }
}

//! Published completeness state for early scene depth on the active view.
/*!
 This is intentionally separate from `DepthPrePassMode`:
 - mode answers what the planner requested
 - completeness answers what later passes may rely on
*/
enum class DepthPrePassCompleteness : std::uint8_t {
  kDisabled,
  kIncomplete,
  kComplete,
};

[[nodiscard]] constexpr auto to_string(
  const DepthPrePassCompleteness completeness) noexcept -> std::string_view
{
  switch (completeness) {
  case DepthPrePassCompleteness::kDisabled:
    return "Disabled";
  case DepthPrePassCompleteness::kIncomplete:
    return "Incomplete";
  case DepthPrePassCompleteness::kComplete:
    return "Complete";
  default:
    return "__Unknown__";
  }
}

} // namespace oxygen::vortex
