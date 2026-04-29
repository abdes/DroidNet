//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

namespace oxygen::vortex {

//! Render mode selection for pipelines.
enum class RenderMode : std::uint8_t {
  kSolid,
  kWireframe,
  kOverlayWireframe,
};

[[nodiscard]] inline auto to_string(RenderMode mode) -> std::string_view
{
  switch (mode) {
    // clang-format off
  case RenderMode::kSolid: return "Solid";
  case RenderMode::kWireframe: return "Wireframe";
  case RenderMode::kOverlayWireframe: return "OverlayWireframe";
  default: return "__Unknown__";
    // clang-format on
  }
}

} // namespace oxygen::vortex
