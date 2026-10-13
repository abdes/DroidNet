//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/api_export.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen::console {
class Console;
} // namespace oxygen::console

namespace oxygen::host {

//! Console variable that selects the main view's view mode from
//! `vortex::EnumerateViewModes()`.
inline constexpr std::string_view kViewModeCVar = "host.view_mode";
inline constexpr std::string_view kDefaultViewMode = "lit";

//! A main-view feature that a `host.show.*` variable can remove.
/*!
 There is no lighting flag: the view's scene-lighting bit gates the whole
 scene pass rather than lighting, and the `unlit` view mode already shows
 the scene without lighting.
*/
enum class ShowFlag : std::uint8_t {
  kShadows,
  kEnvironment,
  kTranslucency,
  kVolumetrics,
  kGrid,
  kAtmosphere,
  kHeightFog,
  kLocalFog,
};

struct ShowFlagInfo {
  ShowFlag flag;
  std::string_view cvar;
  std::string_view label;
};

//! The show flags in menu order.
inline constexpr auto kShowFlags = std::array {
  ShowFlagInfo { .flag = ShowFlag::kShadows,
    .cvar = "host.show.shadows",
    .label = "Shadows" },
  ShowFlagInfo {
    .flag = ShowFlag::kEnvironment,
    .cvar = "host.show.environment",
    .label = "Environment",
  },
  ShowFlagInfo {
    .flag = ShowFlag::kTranslucency,
    .cvar = "host.show.translucency",
    .label = "Translucency",
  },
  ShowFlagInfo {
    .flag = ShowFlag::kVolumetrics,
    .cvar = "host.show.volumetrics",
    .label = "Volumetrics",
  },
  ShowFlagInfo {
    .flag = ShowFlag::kGrid, .cvar = "host.show.grid", .label = "Grid" },
  ShowFlagInfo { .flag = ShowFlag::kAtmosphere,
    .cvar = "host.show.atmosphere",
    .label = "Atmosphere" },
  ShowFlagInfo { .flag = ShowFlag::kHeightFog,
    .cvar = "host.show.height_fog",
    .label = "Height Fog" },
  ShowFlagInfo { .flag = ShowFlag::kLocalFog,
    .cvar = "host.show.local_fog",
    .label = "Local Fog" },
};

//! How the main game view renders, read from the `host.*` console variables.
/*!
 Only the main view gets these settings; other views are configured by the
 code that creates them. A show flag only removes a feature the view asked
 for. The view mode replaces the view's render and debug modes; `lit`
 removes any the view set.
*/
struct MainViewOptions {
  std::string view_mode { kDefaultViewMode };
  //! Indexed by `ShowFlag`; true shows the feature.
  std::array<bool, kShowFlags.size()> show {
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
  };

  [[nodiscard]] auto Shows(ShowFlag flag) const -> bool
  {
    return show.at(static_cast<std::size_t>(flag));
  }

  //! Registers `host.view_mode` and the `host.show.*` variables.
  OXGN_HOST_API static auto RegisterConsoleBindings(console::Console& console)
    -> void;

  //! The current values; defaults for variables that are not registered.
  OXGN_HOST_NDAPI static auto FromConsole(const console::Console& console)
    -> MainViewOptions;

  //! Applies the options to `view`.
  OXGN_HOST_API auto ApplyTo(vortex::CompositionView& view) const -> void;

  //! Applies the options to the view whose id is `main`, and to no other.
  OXGN_HOST_API auto ApplyToMain(std::span<vortex::CompositionView> views,
    std::optional<ViewId> main) const -> void;
};

} // namespace oxygen::host
