//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/ImGui/api_export.h>

namespace oxygen::imgui {

//! One entry of a `DropdownMenu`.
struct MenuItem {
  enum class Kind : std::uint8_t {
    //! Runs `action` when clicked.
    kAction,
    //! Shows a check mark when `checked`; clicking runs `action`.
    kCheck,
    //! Shows a bullet when `checked`; clicking runs `action`.
    kRadio,
    //! A non-clickable heading for the items that follow.
    kSection,
    kSeparator,
    //! Opens `children` on hover.
    kSubmenu,
  };

  Kind kind { Kind::kAction };
  std::string label;
  //! A glyph from the icon fonts, drawn before the label; may be empty.
  std::string_view icon;
  //! Shown right-aligned, for example "Alt+4"; the menu does not bind it.
  std::string shortcut;
  //! Shown on hover; for a disabled item, why it is disabled.
  std::string tooltip;
  bool checked { false };
  bool enabled { true };
  std::function<void()> action;
  std::vector<MenuItem> children;
};

//! What a `DropdownMenu` remembers between frames.
struct DropdownMenuState {
  static constexpr std::size_t kFilterCapacity = 64;
  //! The search text, null-terminated.
  std::array<char, kFilterCapacity> filter {};
};

//! How the button that opens a `DropdownMenu` looks.
struct DropdownButton {
  //! A glyph from the icon fonts; may be empty.
  std::string_view icon;
  //! May be empty for an icon-only button.
  std::string_view label;
  std::string_view tooltip;
};

//! A toolbar button that opens a menu of `items`.
/*!
 With `searchable`, the menu starts with a search field that keeps only the
 items whose label contains the text, ignoring case; sections and separators
 are hidden while searching and submenus are flattened. Returns true while
 the menu is open.
*/
OXGN_IMGUI_API auto DropdownMenu(std::string_view id,
  const DropdownButton& button, std::span<const MenuItem> items,
  DropdownMenuState& state, bool searchable = false) -> bool;

struct ToolbarButtonOptions {
  //! A glyph from the icon fonts.
  std::string_view icon;
  std::string_view tooltip;
  bool enabled { true };
  //! Drawn highlighted, for a toggle that is on.
  bool active { false };
  //! Adds a small down arrow, for a button that opens a menu.
  bool dropdown_arrow { false };
};

//! An icon button sized for a toolbar strip. Returns true when clicked.
OXGN_IMGUI_API auto ToolbarButton(
  std::string_view id, const ToolbarButtonOptions& options) -> bool;

//! Play controls state, provided by the caller each frame.
struct PlayControlsState {
  //! False hides Stop, and Play then only resumes a paused engine.
  bool sessions_available { false };
  bool session_running { false };
  bool paused { false };
};

enum class PlayAction : std::uint8_t {
  kNone,
  kPlay,
  kPause,
  kStep,
  kStop,
};

//! Play, Pause, Step and Stop as one group centred in the remaining width of
//! the current line. Each button is enabled only when its action applies.
OXGN_IMGUI_NDAPI auto PlayControls(const PlayControlsState& state)
  -> PlayAction;

} // namespace oxygen::imgui
