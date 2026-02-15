//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

namespace oxygen::renderer {

//! Render mode selection for pipelines.
enum class RenderMode : std::uint8_t {
  kSolid,
  kWireframe,
  kOverlayWireframe,
};

[[nodiscard]] inline auto to_string(RenderMode mode) -> std::string_view
{
  using enum RenderMode;
  switch (mode) {
    // clang-format off
  case kSolid: return "Solid";
  case kWireframe: return "Wireframe";
  case kOverlayWireframe: return "OverlayWireframe";
  default: return "__Unknown__";
    // clang-format on
  }
}

} // namespace oxygen::renderer
