//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <variant>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen::platform {

//! A key went down, repeated or went up while a window had keyboard focus.
struct UiKeyEvent {
  WindowIdType window_id { kInvalidWindowId };
  Key key { Key::kNone };
  bool pressed { false };
  bool repeat { false };
};

//! The mouse moved over, or is captured by, a window. Positions are in window
//! coordinates.
struct UiMouseMotionEvent {
  WindowIdType window_id { kInvalidWindowId };
  SubPixelPosition position {};
};

//! A mouse button went down or up.
struct UiMouseButtonEvent {
  WindowIdType window_id { kInvalidWindowId };
  MouseButton button { MouseButton::kNone };
  bool pressed { false };
  SubPixelPosition position {};
};

//! The mouse wheel or touchpad scrolled. Positive `scroll.dx` is to the right
//! and positive `scroll.dy` is away from the user.
struct UiMouseWheelEvent {
  WindowIdType window_id { kInvalidWindowId };
  SubPixelMotion scroll {};
};

//! The mouse left the window.
struct UiMouseLeaveEvent {
  WindowIdType window_id { kInvalidWindowId };
};

//! Text produced by the keyboard or an input method editor.
struct UiTextInputEvent {
  WindowIdType window_id { kInvalidWindowId };
  //! UTF-8 text; valid only for the duration of the filter call.
  std::string_view text;
};

//! A window gained or lost keyboard focus.
struct UiFocusEvent {
  WindowIdType window_id { kInvalidWindowId };
  bool gained { false };
};

//! Backend-independent input and focus events, as delivered to the UI filter.
/*!
 These carry what an immediate-mode UI layer needs and nothing that depends on
 the windowing backend. The platform produces them from its native events.
*/
using UiEvent = std::variant<UiKeyEvent, UiMouseMotionEvent, UiMouseButtonEvent,
  UiMouseWheelEvent, UiMouseLeaveEvent, UiTextInputEvent, UiFocusEvent>;

//! The shape of the mouse cursor over a window.
enum class MouseCursor : uint8_t {
  kHidden,
  kArrow,
  kTextInput,
  kResizeAll,
  kResizeNS,
  kResizeEW,
  kResizeNESW,
  kResizeNWSE,
  kHand,
  kWait,
  kProgress,
  kNotAllowed,
};

//! Where the text caret is in window coordinates, for placing an input method
//! editor's candidate window.
struct TextInputArea {
  float x { 0.0F };
  float y { 0.0F };
  float width { 1.0F };
  float height { 0.0F };
};

} // namespace oxygen::platform
