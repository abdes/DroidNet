//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <string>

#include <imgui.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/UiEvent.h>

namespace oxygen {
class Platform;
} // namespace oxygen

namespace oxygen::imgui {

//! Connects one platform window to an ImGui context.
/*!
 Feeds the window's input, size and display scale to ImGui, and gives ImGui the
 clipboard, the mouse cursor and the text input (IME) placement. It talks only
 to the platform abstraction, so it works with any windowing backend the
 platform supports.

 It registers as the platform's UI event filter: ImGui sees every input event
 first, and the events it wants (per `WantCaptureKeyboard` and
 `WantCaptureMouse`) are consumed so that the game input never sees them.
*/
class PlatformBackend final {
public:
  PlatformBackend(std::shared_ptr<Platform> platform,
    platform::WindowIdType window_id, ImGuiContext* context);
  ~PlatformBackend();

  OXYGEN_MAKE_NON_COPYABLE(PlatformBackend)
  OXYGEN_MAKE_NON_MOVABLE(PlatformBackend)

  //! Updates display size, time step, mouse capture and cursor, and the text
  //! input placement. Call once per frame, before `ImGui::NewFrame`.
  auto NewFrame() -> void;

private:
  // Platform hooks that ImGui calls back through its `ImGuiPlatformIO`.
  static auto GetClipboardText(ImGuiContext* context) -> const char*;
  static auto SetClipboardText(ImGuiContext* context, const char* text) -> void;
  static auto SetImeData(ImGuiContext* context, ImGuiViewport* viewport,
    ImGuiPlatformImeData* data) -> void;

  //! Handles one event for this window; true when ImGui consumed it.
  auto HandleEvent(const platform::UiEvent& event) -> bool;
  auto OnKey(const platform::UiKeyEvent& event) -> bool;
  auto OnMouseMotion(const platform::UiMouseMotionEvent& event) const -> bool;
  auto OnMouseButton(const platform::UiMouseButtonEvent& event) -> bool;
  auto OnMouseWheel(const platform::UiMouseWheelEvent& event) const -> bool;
  auto OnMouseLeave(const platform::UiMouseLeaveEvent& event) -> bool;
  auto OnTextInput(const platform::UiTextInputEvent& event) const -> bool;
  auto OnFocus(const platform::UiFocusEvent& event) -> bool;

  auto UpdateModifiers(platform::Key key, bool pressed) -> void;
  auto ClearModifiers() -> void;
  auto UpdateMouseCapture() -> void;
  auto UpdateMouseCursor() -> void;
  auto UpdateTextInput() -> void;

  std::shared_ptr<Platform> platform_;
  platform::WindowIdType window_id_ { platform::kInvalidWindowId };
  observer_ptr<ImGuiContext> context_;

  std::chrono::steady_clock::time_point last_frame_time_ {};
  bool has_last_frame_time_ { false };

  // Modifier keys are reported as ordinary keys; ImGui wants the combined
  // state.
  std::array<bool, 2> control_down_ {};
  std::array<bool, 2> shift_down_ {};
  std::array<bool, 2> alt_down_ {};
  std::array<bool, 2> super_down_ {};

  unsigned mouse_buttons_down_ { 0U };
  int pending_mouse_leave_frame_ { 0 };
  bool mouse_captured_ { false };

  ImGuiPlatformImeData ime_data_ {};
  bool ime_dirty_ { false };
  bool text_input_active_ { false };

  std::string clipboard_text_;
};

} // namespace oxygen::imgui
