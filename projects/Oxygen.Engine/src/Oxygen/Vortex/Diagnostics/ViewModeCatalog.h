//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>
#include <string_view>

#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

//! One way to render a view: the lit scene, a wireframe, or a debug mode.
/*!
 The catalog lists the common modes first (`family` is
 `ShaderDebugModeFamily::kNone`), then every other shader debug mode grouped
 by family. An unsupported entry is listed with its reason so a menu can show
 it disabled; selecting it must fail.
*/
struct ViewModeInfo {
  //! Console value, e.g. `lit` or `direct-brdf-core`.
  std::string_view canonical_name;
  std::string_view display_name;
  //! One line for a tooltip; may be empty.
  std::string_view description;
  ShaderDebugModeFamily family { ShaderDebugModeFamily::kNone };
  //! None renders solid.
  std::optional<RenderMode> render_mode;
  //! None renders the lit scene.
  std::optional<ShaderDebugMode> shader_debug_mode;
  bool supported { true };
  std::string_view unsupported_reason;

  [[nodiscard]] constexpr auto IsCommon() const noexcept -> bool
  {
    return family == ShaderDebugModeFamily::kNone;
  }
};

OXGN_VRTX_NDAPI auto EnumerateViewModes() noexcept
  -> std::span<const ViewModeInfo>;

//! Null for an unknown name.
OXGN_VRTX_NDAPI auto FindViewModeInfo(std::string_view canonical_name) noexcept
  -> const ViewModeInfo*;

//! The entry whose render and debug modes match exactly; null when no entry
//! does, for example wireframe combined with a debug mode.
OXGN_VRTX_NDAPI auto FindViewModeInfo(RenderMode render_mode,
  ShaderDebugMode shader_debug_mode) noexcept -> const ViewModeInfo*;

} // namespace oxygen::vortex
